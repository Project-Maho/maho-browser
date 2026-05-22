// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

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
}

bool MahoSplitViewController::IsSplitActive() const {
  TabStripModel* model = browser_->tab_strip_model();
  if (!model) {
    return false;
  }
  return model->IsActiveTabSplit();
}
