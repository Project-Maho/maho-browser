// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_search_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_top_bar_view.h"

namespace maho {

BEGIN_METADATA(MahoSidebarView)
END_METADATA

MahoSidebarView::MahoSidebarView(Browser* browser) : browser_(browser) {
  BuildUi();
  if (auto* bridge = MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->AddObserver(this);
  }
  if (tab_strip_model()) {
    tab_strip_model()->AddObserver(this);
  }
  ScheduleRefreshAll();
}

MahoSidebarView::~MahoSidebarView() {
  if (auto* bridge = MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->RemoveObserver(this);
  }
  if (tab_strip_model()) {
    tab_strip_model()->RemoveObserver(this);
  }
}

void MahoSidebarView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface, sidebar_layout::kRailCornerRadiusDp));
  SetBorder(views::CreateRoundedRectBorder(
      1, sidebar_layout::kRailCornerRadiusDp, ui::kColorSysSurfaceVariant));
  RefreshAll();
}

bool MahoSidebarView::IsPositionInWindowCaption(
    const gfx::Point& point) const {
  if (top_bar_view_ && top_bar_view_->GetVisible()) {
    gfx::Point point_in_top_bar = point;
    views::View::ConvertPointToTarget(this, top_bar_view_, &point_in_top_bar);
    if (top_bar_view_->HitTestPoint(point_in_top_bar)) {
      return top_bar_view_->IsPositionInWindowCaption(point_in_top_bar);
    }
  }
  return false;
}

void MahoSidebarView::BuildUi() {
  auto* root_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, sidebar_layout::kRailInsets,
      sidebar_layout::kSectionSpacingDp));
  root_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  normal_surface_ = AddChildView(std::make_unique<views::View>());
  auto* normal_surface_layout = normal_surface_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          sidebar_layout::kSectionSpacingDp));
  normal_surface_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  normal_surface_->SetBackground(nullptr);
  normal_surface_->SetBorder(nullptr);

  auto* top_region =
      normal_surface_->AddChildView(std::make_unique<views::View>());
  auto* top_region_layout = top_region->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(),
                                         sidebar_layout::kTopRegionSpacingDp));
  top_region_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  top_region->SetBackground(nullptr);
  top_region->SetBorder(nullptr);
  top_region->SetProperty(views::kMarginsKey, gfx::Insets());

  top_bar_view_ =
      top_region->AddChildView(std::make_unique<MahoSidebarTopBarView>(browser_));
  search_view_ =
      top_region->AddChildView(std::make_unique<MahoSidebarSearchView>(browser_));
  favorites_view_ =
      top_region->AddChildView(std::make_unique<MahoSidebarFavoritesGridView>(browser_));
  tab_list_view_ = normal_surface_->AddChildView(
      std::make_unique<MahoSidebarTabListView>(browser_));
  footer_view_ = normal_surface_->AddChildView(
      std::make_unique<MahoSidebarFooterView>(browser_));
  create_space_view_ =
      AddChildView(std::make_unique<MahoSidebarCreateSpaceView>(browser_));
  create_space_view_->SetCloseCallback(
      base::BindRepeating(&MahoSidebarView::HandleCreateSpaceClosed,
                          weak_factory_.GetWeakPtr()));
  create_space_view_->SetVisible(false);

  normal_surface_layout->SetFlexForView(tab_list_view_, 1, true);
  root_layout->SetFlexForView(normal_surface_, 1, true);
  root_layout->SetFlexForView(create_space_view_, 1, true);
}

void MahoSidebarView::SetCreateMode(bool in_create_mode) {
  if (normal_surface_) {
    normal_surface_->SetVisible(!in_create_mode);
  }
  if (create_space_view_) {
    create_space_view_->SetVisible(in_create_mode);
  }
}

void MahoSidebarView::HandleCreateSpaceClosed() {
  SetCreateMode(false);
}

void MahoSidebarView::ScheduleRefreshAll() {
  if (refresh_scheduled_) {
    return;
  }
  refresh_scheduled_ = true;
  RefreshFavoritesAndViewState();
}

void MahoSidebarView::RefreshFavoritesAndViewState() {
  std::string active_space_id_json;
  if (auto* bridge = MahoSpaceProfileBridge::GetInstance(); bridge) {
    const std::string& active_space_id = bridge->GetActiveSpaceId();
    if (!active_space_id.empty()) {
      active_space_id_json = base::GetQuotedJSONString(active_space_id);
    }
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarView::BuildFavoritesModelOnBackground,
                     active_space_id_json),
      base::BindOnce(&MahoSidebarView::OnFavoritesModelReady,
                     weak_factory_.GetWeakPtr()));
}

// static
MahoSidebarFavoritesModel MahoSidebarView::BuildFavoritesModelOnBackground(
    const std::string& active_space_id_json) {
  return MahoSidebarStateAdapter::BuildFavoritesModelForSpaceIdJson(
      active_space_id_json);
}

void MahoSidebarView::OnFavoritesModelReady(
    MahoSidebarFavoritesModel favorites_model) {
  state_adapter_.SetFavoritesModel(std::move(favorites_model));
  RefreshAll();
}

void MahoSidebarView::RefreshAll() {
  refresh_scheduled_ = false;
  ApplyViewState(state_adapter_.BuildViewStateModel(browser_));
}

void MahoSidebarView::ApplyViewState(const MahoSidebarViewStateModel& model) {
  UpdateTopBar(model.top_bar);
  UpdateSearch(model.search);
  UpdateFavorites(model.favorites);
  UpdateTabList(model.tab_list);
  UpdateFooter(model.footer);
}

void MahoSidebarView::UpdateTopBar(const MahoSidebarTopBarModel& model) {
  if (top_bar_view_) {
    top_bar_view_->Update(model);
  }
}

void MahoSidebarView::UpdateSearch(const MahoSidebarSearchModel& model) {
  if (search_view_) {
    search_view_->Update(model);
  }
}

void MahoSidebarView::UpdateFavorites(const MahoSidebarFavoritesModel& model) {
  if (favorites_view_) {
    favorites_view_->Update(model);
  }
}

void MahoSidebarView::UpdateTabList(const MahoSidebarTabListModel& model) {
  if (tab_list_view_) {
    tab_list_view_->Update(model, browser_);
  }
}

void MahoSidebarView::UpdateFooter(const MahoSidebarFooterModel& model) {
  if (footer_view_) {
    footer_view_->Update(model);
  }
}

TabStripModel* MahoSidebarView::tab_strip_model() const {
  return browser_ ? browser_->tab_strip_model() : nullptr;
}

void MahoSidebarView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  ScheduleRefreshAll();
}

void MahoSidebarView::OnTabChangedAt(tabs::TabInterface* tab,
                                     int index,
                                     TabChangeType change_type) {
  ScheduleRefreshAll();
}

void MahoSidebarView::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                              int index) {
  // Sync pin state to maho-core before refreshing UI.
  if (tab_list_view_) {
    content::WebContents* contents =
        tab_strip_model()->GetWebContentsAt(index);
    std::string core_tab_id =
        tab_list_view_->FindCoreTabIdByWebContents(contents);
    if (!core_tab_id.empty()) {
      bool pinned = tab_strip_model()->IsTabPinned(index);
      DispatchShellEvent(pinned ? "pin_tab" : "unpin_tab",
                         {{"tab_id", core_tab_id}});
    } else {
      DLOG(WARNING) << "MahoSidebarView: Could not resolve core tab_id for "
                    << "strip index " << index << " during pin state change";
    }
  }
  ScheduleRefreshAll();
}

void MahoSidebarView::OnTabGroupChanged(const TabGroupChange& change) {
  ScheduleRefreshAll();
}

void MahoSidebarView::OnSpaceProfileBridgeChanged() {
  ScheduleRefreshAll();
}

void MahoSidebarView::ShowCreateSpace() {
  if (create_space_view_) {
    SetCreateMode(true);
    create_space_view_->RequestNameFocus();
  }
}

}  // namespace maho
