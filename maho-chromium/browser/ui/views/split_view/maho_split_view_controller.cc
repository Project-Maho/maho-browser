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
void MahoSplitViewController::RestoreForWindow(Browser* /*browser*/) {
  // Intentionally a no-op. Chromium's session service persists and restores
  // split membership AND visual layout (kCommandSetSplitTab in
  // session_service_commands.cc; RestoreSplitTabVisualData in
  // session_restore.cc, round-tripping layout via SplitTabLayoutToString), so
  // side-by-side splits come back side-by-side without our help.
  // Maho's former re-apply here was guarded by !GetSplitForTab(...): when the
  // session service had already restored the split it did nothing, and the
  // only case it acted on was session data being absent — where it mapped the
  // always-"horizontal" persisted orientation to kHorizontal and flipped the
  // split to stacked. Dead on the normal path, wrong on the exception path, so
  // it is removed and restore is left entirely to the session service.
}


