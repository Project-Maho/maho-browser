// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

#include "base/json/json_reader.h"
#include "base/values.h"
#include "base/strings/stringprintf.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace {

std::string GetTabIdForContents(content::WebContents* contents) {
  if (!contents) {
    return "";
  }
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  return helper ? helper->stable_tab_id() : "";
}

std::string GetTabIdForIndex(TabStripModel* model, int index) {
  if (index == TabStripModel::kNoTab) {
    return "";
  }
  return GetTabIdForContents(model->GetWebContentsAt(index));
}

}  // namespace

MahoSplitViewController::MahoSplitViewController(Browser* browser)
    : browser_(browser) {}

MahoSplitViewController::~MahoSplitViewController() = default;

void MahoSplitViewController::AddSplit() {
  AddSplitWithURL(GURL("about:blank"));
}

void MahoSplitViewController::AddSplitWithURL(const GURL& url) {
  if (IsSplitActive()) {
    return;
  }

  TabStripModel* model = browser_->tab_strip_model();
  if (!model) {
    return;
  }

  int active_index = model->active_index();
  if (active_index == TabStripModel::kNoTab) {
    return;
  }

  content::WebContents::CreateParams create_params(browser_->profile());
  auto new_contents = content::WebContents::Create(create_params);
  content::NavigationController::LoadURLParams load_params(url);
  load_params.transition_type = ui::PAGE_TRANSITION_GENERATED;
  new_contents->GetController().LoadURLWithParams(load_params);

  int new_index = active_index + 1;
  model->InsertWebContentsAt(
      new_index, std::move(new_contents),
      AddTabTypes::ADD_NONE);

  split_tabs::SplitTabVisualData visual_data(
      split_tabs::SplitTabLayout::kVertical, 0.5);
  // Split membership is owned by TabStripModel and is persisted through the
  // session service; no extra session bookkeeping is needed here.
  model->AddToNewSplit(
      {active_index, new_index}, visual_data,
      split_tabs::SplitTabCreatedSource::kToolbarButton);

  std::string active_id = GetTabIdForIndex(model, active_index);
  std::string new_id = GetTabIdForIndex(model, new_index);
  if (!active_id.empty() && !new_id.empty()) {
    std::string event_json = base::StringPrintf(
        "{\"kind\":\"create_split\",\"window_id\":\"%s\",\"tab_ids\":[\"%s\",\"%s\"],\"orientation\":\"horizontal\"}",
        std::to_string(browser_->session_id().id()).c_str(),
        active_id.c_str(), new_id.c_str());
    if (auto* core = maho::GetCore()) {
      char* result = maho_core_handle_event(core, event_json.c_str());
      if (result) {
        maho_core_free_string(result);
      }
    }
  }
}

void MahoSplitViewController::ToggleSplit() {
  if (IsSplitActive()) {
    RemoveSplit();
  } else {
    AddSplit();
  }
}

void MahoSplitViewController::ToggleSplitWithURL(const GURL& url) {
  if (IsSplitActive()) {
    RemoveSplit();
  } else {
    AddSplitWithURL(url);
  }
}

void MahoSplitViewController::RemoveSplit() {
  TabStripModel* model = browser_->tab_strip_model();
  if (!model) {
    return;
  }

  int active_index = model->active_index();
  if (active_index == TabStripModel::kNoTab) {
    return;
  }

  std::optional<split_tabs::SplitTabId> split_id =
      model->GetSplitForTab(active_index);
  if (!split_id.has_value()) {
    return;
  }

  model->RemoveSplit(split_id.value());

  std::string event_json = base::StringPrintf(
      "{\"kind\":\"clear_split_view\",\"window_id\":\"%s\"}",
      std::to_string(browser_->session_id().id()).c_str());
  if (auto* core = maho::GetCore()) {
    char* result = maho_core_handle_event(core, event_json.c_str());
    if (result) {
      maho_core_free_string(result);
    }
  }
}

void MahoSplitViewController::SwapSplit() {
  TabStripModel* model = browser_->tab_strip_model();
  if (!model) {
    return;
  }

  int active_index = model->active_index();
  if (active_index == TabStripModel::kNoTab) {
    return;
  }

  std::optional<split_tabs::SplitTabId> split_id =
      model->GetSplitForTab(active_index);
  if (!split_id.has_value()) {
    return;
  }

  model->ReverseTabsInSplit(split_id.value());

  if (auto* split_data = model->GetSplitData(split_id.value())) {
    auto split_tabs = split_data->ListTabs();
    if (split_tabs.size() == 2) {
      std::string tab1_id = GetTabIdForContents(split_tabs[0]->GetContents());
      std::string tab2_id = GetTabIdForContents(split_tabs[1]->GetContents());
      if (!tab1_id.empty() && !tab2_id.empty()) {
        std::string event_json = base::StringPrintf(
            "{\"kind\":\"create_split\",\"window_id\":\"%s\",\"tab_ids\":[\"%s\",\"%s\"],\"orientation\":\"horizontal\"}",
            std::to_string(browser_->session_id().id()).c_str(),
            tab1_id.c_str(), tab2_id.c_str());
        if (auto* core = maho::GetCore()) {
          char* result = maho_core_handle_event(core, event_json.c_str());
          if (result) {
            maho_core_free_string(result);
          }
        }
      }
    }
  }
}

bool MahoSplitViewController::IsSplitActive() const {
  TabStripModel* model = browser_->tab_strip_model();
  if (!model) {
    return false;
  }
  return model->IsActiveTabSplit();
}

// static
void MahoSplitViewController::RestoreForWindow(Browser* browser) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  std::string window_id = std::to_string(browser->session_id().id());
  char* config_json =
      maho_core_get_split_view_config(core, window_id.c_str());
  if (!config_json) {
    return;
  }

  std::optional<base::Value> value = base::JSONReader::Read(config_json, base::JSON_PARSE_RFC);
  maho_core_free_string(config_json);

  if (!value || !value->is_dict()) {
    return;
  }

  const base::DictValue& dict = value->GetDict();
  const base::ListValue* panes = dict.FindList("panes");
  if (!panes || panes->size() < 2) {
    return;
  }

  const std::string* orientation_str = dict.FindString("orientation");
  split_tabs::SplitTabLayout layout = split_tabs::SplitTabLayout::kVertical;
  if (orientation_str && *orientation_str == "horizontal") {
    layout = split_tabs::SplitTabLayout::kHorizontal;
  }

  const base::ListValue* ratios = dict.FindList("ratios");
  double split_ratio = 0.5;
  if (ratios && ratios->size() > 0) {
    if (ratios->front().is_double()) {
      split_ratio = ratios->front().GetDouble();
    }
  }

  TabStripModel* model = browser->tab_strip_model();
  if (!model) {
    return;
  }

  std::vector<int> indices;
  for (const base::Value& pane : *panes) {
    if (!pane.is_dict()) {
      continue;
    }
    const std::string* tab_id = pane.GetDict().FindString("tabId");
    if (!tab_id) {
      continue;
    }

    int index = TabStripModel::kNoTab;
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* contents = model->GetWebContentsAt(i);
      auto* helper = MahoTabIdHelper::FromWebContents(contents);
      if (helper && helper->stable_tab_id() == *tab_id) {
        index = i;
        break;
      }
    }

    if (index != TabStripModel::kNoTab) {
      indices.push_back(index);
    }
  }

  if (indices.size() == 2) {
    if (!model->GetSplitForTab(indices[0]).has_value() &&
        !model->GetSplitForTab(indices[1]).has_value()) {
      split_tabs::SplitTabVisualData visual_data(layout, split_ratio);
      model->AddToNewSplit(indices, visual_data,
                           split_tabs::SplitTabCreatedSource::kToolbarButton);
    }
  }
}
