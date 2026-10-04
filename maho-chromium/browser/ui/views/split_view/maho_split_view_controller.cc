// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"

#include <algorithm>
#include <cmath>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
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

MahoSplitViewController::MahoSplitViewController(Browser* browser,
                                                 MahoSplitPersistence persistence)
    : browser_(browser), persistence_(persistence) {}

MahoSplitViewController::~MahoSplitViewController() = default;

// static
bool MahoSplitViewController::ShouldDispatchPersistentSplitEvent(
    MahoSplitPersistence mode) {
  return mode == MahoSplitPersistence::kPersistent;
}

// static
MahoSplitDropSide MahoSplitViewController::ResolveDropSide(int x, int width) {
  return x < std::max(width, 1) / 2 ? MahoSplitDropSide::kLeft
                                    : MahoSplitDropSide::kRight;
}

// static
bool MahoSplitViewController::IsSplitDropEligible(bool is_multi_drag,
                                                  bool source_is_target,
                                                  bool source_already_split,
                                                  bool target_already_split) {
  return !is_multi_drag && !source_is_target && !source_already_split &&
         !target_already_split;
}

void MahoSplitViewController::AddSplit() {
  AddSplitWithURL(GURL("about:blank"));
}

void MahoSplitViewController::AddSplitWithURL(const GURL& url) {
  if (IsSplitActive()) {
    return;
  }

  TabStripModel* model = browser_->GetTabStripModel();
  if (!model) {
    return;
  }

  int active_index = model->active_index();
  if (active_index == TabStripModel::kNoTab) {
    return;
  }

  content::WebContents::CreateParams create_params(browser_->GetProfile());
  auto new_contents = content::WebContents::Create(create_params);
  content::NavigationController::LoadURLParams load_params(url);
  load_params.transition_type = ui::PAGE_TRANSITION_GENERATED;
  new_contents->GetController().LoadURLWithParams(load_params);

  int new_index = active_index + 1;
  model->InsertWebContentsAt(
      new_index, std::move(new_contents),
      AddTabTypes::ADD_NONE);

  split_tabs::SplitTabVisualData visual_data(
      split_tabs::SplitTabLayout::kSideBySide, 0.5);
  // Split membership is owned by TabStripModel and is persisted through the
  // session service; no extra session bookkeeping is needed here.
  // Upstream AddToNewSplit CHECK_EQs exactly one partner index and pivots on
  // the active tab itself; passing active_index too re-trips that CHECK.
  model->AddToNewSplit(
      {new_index}, visual_data,
      split_tabs::SplitTabCreatedSource::kToolbarButton);

  std::string active_id = GetTabIdForIndex(model, active_index);
  std::string new_id = GetTabIdForIndex(model, new_index);
  if (ShouldDispatchPersistentSplitEvent(persistence_) && !active_id.empty() &&
      !new_id.empty()) {
    std::string event_json = base::StringPrintf(
        "{\"kind\":\"create_split\",\"window_id\":\"%s\",\"tab_ids\":[\"%s\",\"%s\"],\"orientation\":\"horizontal\"}",
        std::to_string(browser_->GetSessionID().id()).c_str(),
        active_id.c_str(), new_id.c_str());
    if (auto* core = maho::GetCore()) {
      char* result = maho_core_handle_event(core, event_json.c_str());
      if (result) {
        maho_core_free_string(result);
      }
    }
  }
}

void MahoSplitViewController::AddSplitForExistingTab(int clicked_index) {
  TabStripModel* model = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!model || !model->ContainsIndex(clicked_index)) {
    return;
  }

  int active_index = model->active_index();
  if (active_index == TabStripModel::kNoTab) {
    return;
  }

  // The active tab is Chromium's implicit split pivot; when the clicked tab IS
  // the active tab there is no distinct existing partner, so create a fresh
  // blank partner through the toolbar/self-split path instead.
  if (clicked_index == active_index) {
    AddSplit();
    return;
  }

  // AddToNewSplit re-splits neither participant: reject if the clicked tab or
  // the active pivot is already a split member.
  if (model->GetSplitForTab(clicked_index).has_value() ||
      model->GetSplitForTab(active_index).has_value()) {
    return;
  }

  // Capture stable ids before AddToNewSplit moves the partner adjacent to the
  // pivot, which would invalidate clicked_index for a post-call lookup.
  std::string active_id = GetTabIdForIndex(model, active_index);
  std::string clicked_id = GetTabIdForIndex(model, clicked_index);

  split_tabs::SplitTabVisualData visual_data(
      split_tabs::SplitTabLayout::kSideBySide, 0.5);
  // Only the clicked existing tab is passed as the single partner index; the
  // active tab stays canonical active as the implicit pivot.
  model->AddToNewSplit(
      {clicked_index}, visual_data,
      split_tabs::SplitTabCreatedSource::kTabContextMenu);

  if (ShouldDispatchPersistentSplitEvent(persistence_) && !active_id.empty() &&
      !clicked_id.empty()) {
    std::string event_json = base::StringPrintf(
        "{\"kind\":\"create_split\",\"window_id\":\"%s\",\"tab_ids\":[\"%s\",\"%s\"],\"orientation\":\"horizontal\"}",
        std::to_string(browser_->GetSessionID().id()).c_str(),
        active_id.c_str(), clicked_id.c_str());
    if (auto* core = maho::GetCore()) {
      char* result = maho_core_handle_event(core, event_json.c_str());
      if (result) {
        maho_core_free_string(result);
      }
    }
  }
}

bool MahoSplitViewController::AddViewportSplitForExistingTab(
    int source_index,
    split_tabs::SplitTabLayout layout,
    bool source_before_target) {
  return AddViewportSplitForExistingTab(source_index, 0, layout,
                                        source_before_target);
}

bool MahoSplitViewController::AddViewportSplitForExistingTab(
    int source_index,
    size_t target_pane_index,
    split_tabs::SplitTabLayout layout,
    bool source_before_target) {
  TabStripModel* model = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!model || !model->ContainsIndex(source_index)) {
    return false;
  }

  int active_index = model->active_index();
  if (active_index == TabStripModel::kNoTab || source_index == active_index) {
    return false;
  }

  if (model->GetSplitForTab(source_index).has_value()) {
    return false;
  }

  if (std::optional<split_tabs::SplitTabId> split_id =
          model->GetSplitForTab(active_index)) {
    split_tabs::SplitTabData* split_data = model->GetSplitData(*split_id);
    if (!split_data) {
      return false;
    }
    const std::vector<tabs::TabInterface*> tabs = split_data->ListTabs();
    if (tabs.size() < 2u || tabs.size() >= 4u ||
        target_pane_index >= tabs.size()) {
      return false;
    }

    if (!split_data->visual_data()) {
      return false;
    }

    split_tabs::SplitTabVisualData visual_data = *split_data->visual_data();
    const split_tabs::SplitPaneInsertionSide insertion_side =
        source_before_target
            ? split_tabs::SplitPaneInsertionSide::kBefore
            : split_tabs::SplitPaneInsertionSide::kAfter;
    if (!visual_data.InsertPaneAtLeaf(target_pane_index, layout, 0.5,
                                      insertion_side)) {
      return false;
    }

    const size_t insertion_index =
        target_pane_index + (source_before_target ? 0u : 1u);
    return model->AddToExistingSplit(
        *split_id, source_index, insertion_index, visual_data,
        split_tabs::SplitTabCreatedSource::kDragAndDropTab);
  }

  split_tabs::SplitTabVisualData visual_data(layout, 0.5);
  return !model
              ->AddToNewSplit(
                  {source_index}, visual_data,
                  split_tabs::SplitTabCreatedSource::kDragAndDropTab)
              .is_empty();
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
  TabStripModel* model = browser_->GetTabStripModel();
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

  if (!ShouldDispatchPersistentSplitEvent(persistence_)) {
    return;
  }
  std::string event_json = base::StringPrintf(
      "{\"kind\":\"clear_split_view\",\"window_id\":\"%s\"}",
      std::to_string(browser_->GetSessionID().id()).c_str());
  if (auto* core = maho::GetCore()) {
    char* result = maho_core_handle_event(core, event_json.c_str());
    if (result) {
      maho_core_free_string(result);
    }
  }
}

void MahoSplitViewController::RemoveSplitForTab(int tab_strip_index) {
  TabStripModel* model = browser_->GetTabStripModel();
  if (!model || !model->ContainsIndex(tab_strip_index)) {
    return;
  }
  std::optional<split_tabs::SplitTabId> split_id =
      model->GetSplitForTab(tab_strip_index);
  if (!split_id.has_value()) {
    return;
  }
  model->RemoveSplit(split_id.value());

  if (!ShouldDispatchPersistentSplitEvent(persistence_)) {
    return;
  }
  std::string event_json = base::StringPrintf(
      "{\"kind\":\"clear_split_view\",\"window_id\":\"%s\"}",
      std::to_string(browser_->GetSessionID().id()).c_str());
  if (auto* core = maho::GetCore()) {
    char* result = maho_core_handle_event(core, event_json.c_str());
    if (result) {
      maho_core_free_string(result);
    }
  }
}

void MahoSplitViewController::SwapSplit() {
  TabStripModel* model = browser_->GetTabStripModel();
  if (!model) {
    return;  }

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
      if (ShouldDispatchPersistentSplitEvent(persistence_) &&
          !tab1_id.empty() && !tab2_id.empty()) {
        std::string event_json = base::StringPrintf(
            "{\"kind\":\"create_split\",\"window_id\":\"%s\",\"tab_ids\":[\"%s\",\"%s\"],\"orientation\":\"horizontal\"}",
            std::to_string(browser_->GetSessionID().id()).c_str(),
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
  TabStripModel* model = browser_->GetTabStripModel();
  if (!model) {
    return false;
  }
  return model->ContainsIndex(model->active_index()) &&
         model->GetSplitForTab(model->active_index()).has_value();
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



std::optional<split_tabs::SplitTabLayout>
MahoSplitViewController::GetSplitOrientation() const {
  TabStripModel* model = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!model) {
    return std::nullopt;
  }
  int active_index = model->active_index();
  if (active_index == TabStripModel::kNoTab) {
    return std::nullopt;
  }
  std::optional<split_tabs::SplitTabId> split_id =
      model->GetSplitForTab(active_index);
  if (!split_id.has_value()) {
    return std::nullopt;
  }
  split_tabs::SplitTabData* split_data = model->GetSplitData(*split_id);
  if (!split_data || !split_data->visual_data()) {
    return std::nullopt;
  }
  return split_data->visual_data()->split_layout();
}

bool MahoSplitViewController::SetSplitOrientation(
    split_tabs::SplitTabLayout orientation) {
  TabStripModel* model = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!model) {
    return false;
  }
  int active_index = model->active_index();
  if (active_index == TabStripModel::kNoTab) {
    return false;
  }
  std::optional<split_tabs::SplitTabId> split_id =
      model->GetSplitForTab(active_index);
  if (!split_id.has_value()) {
    return false;
  }
  split_tabs::SplitTabData* split_data = model->GetSplitData(*split_id);
  if (!split_data || !split_data->visual_data()) {
    return false;
  }
  split_tabs::SplitTabVisualData visual_data = *split_data->visual_data();
  if (visual_data.split_layout() == orientation) {
    return true;
  }
  visual_data.set_split_layout(orientation);
  // No layout tree at this upstream pin: SplitTabVisualData is layout+ratio.
  *split_data->visual_data() = visual_data;
  return true;
}

std::optional<split_tabs::SplitTabLayout>
MahoSplitViewController::ToggleSplitOrientation() {
  auto current = GetSplitOrientation();
  if (!current.has_value()) {
    return std::nullopt;
  }
  auto target = (*current == split_tabs::SplitTabLayout::kSideBySide)
                    ? split_tabs::SplitTabLayout::kStacked
                    : split_tabs::SplitTabLayout::kSideBySide;
  if (SetSplitOrientation(target)) {
    return target;
  }
  return std::nullopt;
}

bool MahoSplitViewController::ResizeSplit(double delta) {
  TabStripModel* model = browser_->GetTabStripModel();
  if (!model) {
    return false;
  }
  const int active_index = model->active_index();
  if (active_index == TabStripModel::kNoTab) {
    return false;
  }
  const std::optional<split_tabs::SplitTabId> split_id =
      model->GetSplitForTab(active_index);
  if (!split_id.has_value()) {
    return false;
  }
  split_tabs::SplitTabData* split_data = model->GetSplitData(*split_id);
  if (!split_data || !split_data->visual_data()) {
    return false;
  }
  const double current_ratio = split_data->visual_data()->split_ratio();
  const double new_ratio = std::clamp(current_ratio + delta, 0.05, 0.95);
  if (std::abs(new_ratio - current_ratio) < 0.001) {
    return false;
  }
  model->UpdateSplitRatio(*split_id, new_ratio);
  if (auto* session_service =
          SessionServiceFactory::GetForProfile(browser_->GetProfile())) {
    if (const split_tabs::SplitTabVisualData* visual_data =
            model->GetSplitData(*split_id)->visual_data()) {
      session_service->SetSplitTabData(browser_->GetSessionID(), *split_id,
                                       visual_data);
    }
  }
  return true;
}
