// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"

#include <memory>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "url/gurl.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_class_properties.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_action_pane_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_media_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_search_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_archive_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_top_bar_view.h"
#include "net/base/url_util.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/compositor/layer_tree_owner.h"
#include "maho/components/constants/webui_url_constants.h"

namespace maho {

namespace {

constexpr int kLibraryDividerThicknessDp = 1;

bool IsDescendantOfView(const views::View* candidate,
                        const views::View* ancestor) {
  if (!candidate || !ancestor) {
    return false;
  }

  for (const views::View* current = candidate; current;
       current = current->parent()) {
    if (current == ancestor) {
      return true;
    }
  }

  return false;
}

}  // namespace

BEGIN_METADATA(MahoSidebarView)
END_METADATA

MahoSidebarView::MahoSidebarView(Browser* browser)
    : browser_(browser),
      last_focused_body_view_tracker_(std::make_unique<views::ViewTracker>()) {
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
  if (library_divider_) {
    library_divider_->SetBackground(
        views::CreateSolidBackground(ui::kColorSeparator));
  }
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

  if (current_body_mode_ == MahoSidebarBodyMode::kLibrary &&
      library_container_ && library_container_->GetVisible()) {
    if (library_rail_view_) {
      gfx::Point point_in_rail = point;
      views::View::ConvertPointToTarget(this, library_rail_view_,
                                        &point_in_rail);
      if (library_rail_view_->HitTestPoint(point_in_rail)) {
        return library_rail_view_->IsPositionInWindowCaption(point_in_rail);
      }
    }

    // Delegate caption hit-testing to whichever library pane is active.
    views::View* active_pane = ActiveLibraryPane();
    if (active_pane && active_pane->GetVisible()) {
      gfx::Point point_in_pane = point;
      views::View::ConvertPointToTarget(this, active_pane, &point_in_pane);
      if (active_pane->HitTestPoint(point_in_pane)) {
        // Archive view has its own IsPositionInWindowCaption; other panes
        // default to treating the entire area as non-caption (interactive).
        if (archive_view_ && active_pane == archive_view_) {
          return archive_view_->IsPositionInWindowCaption(point_in_pane);
        }
        return false;
      }
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

  top_bar_view_ =
      AddChildView(std::make_unique<MahoSidebarTopBarView>(browser_));

  body_host_ = AddChildView(std::make_unique<views::View>());
  auto* body_layout = body_host_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  body_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  content_host_ = body_host_->AddChildView(std::make_unique<views::View>());
  content_host_->SetLayoutManager(std::make_unique<views::FillLayout>());
  content_host_->SetBackground(nullptr);
  content_host_->SetBorder(nullptr);

  tabs_surface_ = content_host_->AddChildView(std::make_unique<views::View>());
  auto* normal_body_layout = tabs_surface_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          sidebar_layout::kSectionSpacingDp));
  normal_body_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  tabs_surface_->SetBackground(nullptr);
  tabs_surface_->SetBorder(nullptr);

  auto* top_region =
      tabs_surface_->AddChildView(std::make_unique<views::View>());
  top_region_ = top_region;
  auto* top_region_layout = top_region->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(),
                                         sidebar_layout::kTopRegionSpacingDp));
  top_region_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  top_region->SetBackground(nullptr);
  top_region->SetBorder(nullptr);
  top_region->SetProperty(views::kMarginsKey, gfx::Insets());

  search_view_ =
      top_region->AddChildView(std::make_unique<MahoSidebarSearchView>(browser_));
  favorites_view_ =
      top_region->AddChildView(std::make_unique<MahoSidebarFavoritesGridView>(browser_));
  tab_list_view_ = tabs_surface_->AddChildView(
      std::make_unique<MahoSidebarTabListView>(browser_));

  library_container_ =
      content_host_->AddChildView(std::make_unique<views::View>());
  auto* library_layout = library_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  library_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  library_container_->SetBackground(nullptr);
  library_container_->SetBorder(nullptr);
  library_container_->SetVisible(false);

  library_rail_view_ = library_container_->AddChildView(
      std::make_unique<MahoSidebarLibraryRailView>());
  library_rail_view_->SetCategorySelectedCallback(base::BindRepeating(
      &MahoSidebarView::HandleLibraryCategorySelected,
      weak_factory_.GetWeakPtr()));
  library_rail_view_->SetBackCallback(base::BindRepeating(
      &MahoSidebarView::HandleLibraryBack, weak_factory_.GetWeakPtr()));

  library_divider_ =
      library_container_->AddChildView(std::make_unique<views::View>());
  library_divider_->SetPreferredSize(
      gfx::Size(kLibraryDividerThicknessDp, 0));
  library_divider_->SetBackground(
      views::CreateSolidBackground(ui::kColorSeparator));
  library_divider_->SetBorder(nullptr);

  library_content_host_ =
      library_container_->AddChildView(std::make_unique<views::View>());
  library_content_host_->SetLayoutManager(std::make_unique<views::FillLayout>());
  library_content_host_->SetBackground(nullptr);
  library_content_host_->SetBorder(nullptr);

  library_layout->SetFlexForView(library_content_host_, 1, true);


  create_space_view_ = content_host_->AddChildView(
      std::make_unique<MahoSidebarCreateSpaceView>(browser_));
  create_space_view_->SetVisible(false);
  create_space_view_->SetCloseCallback(base::BindRepeating(
      [](base::WeakPtr<MahoSidebarView> sidebar_view) {
        if (sidebar_view) {
          sidebar_view->SetBodyMode(MahoSidebarBodyMode::kTabs);
        }
      },
      weak_factory_.GetWeakPtr()));


  footer_view_ = body_host_->AddChildView(
      std::make_unique<MahoSidebarFooterView>(browser_));

  normal_body_layout->SetFlexForView(tab_list_view_, 1, true);
  body_layout->SetFlexForView(content_host_, 1, true);
  root_layout->SetFlexForView(body_host_, 1, true);
}

void MahoSidebarView::SetBodyMode(MahoSidebarBodyMode mode) {
  const MahoSidebarBodyMode previous_mode = current_body_mode_;
  if (mode == MahoSidebarBodyMode::kLibrary &&
      previous_mode != MahoSidebarBodyMode::kLibrary) {
    CaptureLastFocusedBodyView();
  }

  current_body_mode_ = mode;
  if (tabs_surface_) {
    tabs_surface_->SetVisible(mode == MahoSidebarBodyMode::kTabs);
  }
  if (top_bar_view_) {
    top_bar_view_->SetVisible(true);
  }
  if (library_container_) {
    library_container_->SetVisible(mode == MahoSidebarBodyMode::kLibrary);
  }
  if (footer_view_) {
    footer_view_->SetVisible(true);
    footer_view_->RefreshPreferredSize();
  }
  if (mode == MahoSidebarBodyMode::kLibrary) {
    ShowLibraryPane(active_library_category_);
  } else {
    // Hide whichever library pane was active.
    views::View* active_pane = ActiveLibraryPane();
    if (active_pane) {
      active_pane->SetVisible(false);
    }
  }
  if (create_space_view_) {
    create_space_view_->SetVisible(mode == MahoSidebarBodyMode::kCreateSpace);
    if (mode == MahoSidebarBodyMode::kCreateSpace) {
      create_space_view_->PrepareForOpen();
    }
  }

  if (mode == MahoSidebarBodyMode::kCreateSpace && create_space_view_) {
    create_space_view_->RequestNameFocus();
  }

  if (previous_mode == MahoSidebarBodyMode::kLibrary &&
      mode == MahoSidebarBodyMode::kTabs) {
    RestoreLastFocusedBodyView();
  }
}

void MahoSidebarView::CaptureLastFocusedBodyView() {
  last_focused_body_view_tracker_->SetView(nullptr);

  views::FocusManager* focus_manager = GetFocusManager();
  if (!focus_manager) {
    return;
  }

  views::View* focused_view = focus_manager->GetFocusedView();
  if (!focused_view || !focused_view->GetVisible()) {
    return;
  }

  if (CanRestoreFocusToView(focused_view)) {
    last_focused_body_view_tracker_->SetView(focused_view);
  }
}

void MahoSidebarView::RestoreLastFocusedBodyView() {
  views::View* last_focused_body_view = last_focused_body_view_tracker_->view();
  if (CanRestoreFocusToView(last_focused_body_view)) {
    last_focused_body_view->RequestFocus();
    return;
  }

  last_focused_body_view_tracker_->SetView(nullptr);

  if (search_view_ && search_view_->GetVisible()) {
    search_view_->FocusVisibleControl();
  }
}

bool MahoSidebarView::CanRestoreFocusToView(const views::View* view) const {
  if (!view || !view->GetVisible() || !view->GetEnabled() ||
      !IsDescendantOfView(view, this)) {
    return false;
  }

  return !IsDescendantOfView(view, library_container_) &&
         !IsDescendantOfView(view, create_space_view_);
}

void MahoSidebarView::HandleLibraryCategorySelected(
    MahoSidebarLibraryRailView::Category category) {
  if (!library_rail_view_) {
    return;
  }

  ShowLibraryPane(category);
}

void MahoSidebarView::HandleLibraryBack() {
  SetBodyMode(MahoSidebarBodyMode::kTabs);
}

void MahoSidebarView::UpdateSpacesPaneActions() {
  if (!spaces_view_) {
    return;
  }
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  const bool has_active_space = bridge && !bridge->GetActiveSpaceId().empty();
  spaces_view_->SetActionEnabled(1, has_active_space);
}

void MahoSidebarView::OpenSpaceCreateSurface() {
  OpenWebUIInNewTab(GURL(maho::kMahoSpaceCreateURL));
}

void MahoSidebarView::OpenSpaceConfigSurface() {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  const std::string active_space_id =
      bridge ? bridge->GetActiveSpaceId() : std::string();
  if (active_space_id.empty()) {
    return;
  }
  GURL url(maho::kMahoSpaceConfigURL);
  url = net::AppendQueryParameter(url, "space_id", active_space_id);
  url = net::AppendQueryParameter(url, "focus", "name");
  OpenWebUIInNewTab(url);
}

views::View* MahoSidebarView::GetOrCreateLibraryPane(
    MahoSidebarLibraryRailView::Category category) {
  auto it = library_pane_cache_.find(category);
  if (it != library_pane_cache_.end()) {
    return it->second;
  }

  auto make_action_pane = [this](std::u16string title,
                                 std::u16string description,
                                 std::vector<MahoSidebarLibraryActionPaneView::ActionSpec> actions,
                                 MahoSidebarLibraryActionPaneView::ActionCallback callback) {
    auto* pane = library_content_host_->AddChildView(
        std::make_unique<MahoSidebarLibraryActionPaneView>(
            std::move(title), std::move(description), std::move(actions),
            std::move(callback)));
    pane->SetVisible(false);
    return pane;
  };

  views::View* pane = nullptr;
  switch (category) {
    case MahoSidebarLibraryRailView::Category::kArchivedTabs: {
      if (!archive_view_) {
        archive_view_ = library_content_host_->AddChildView(
            std::make_unique<MahoSidebarArchiveView>(browser_));
        archive_view_->SetVisible(false);
        archive_view_->SetRestoreCallback(base::BindRepeating(
            [](base::WeakPtr<MahoSidebarView> sidebar_view,
               const std::string& tab_id) {
              DispatchShellEvent("restore_archived_tab", {{"tab_id", tab_id}});
              if (sidebar_view && sidebar_view->archive_view_) {
                sidebar_view->archive_view_->ReloadArchivedTabs();
              }
            },
            weak_factory_.GetWeakPtr()));
        archive_view_->SetDeleteCallback(base::BindRepeating(
            [](base::WeakPtr<MahoSidebarView> sidebar_view,
               const std::string& tab_id) {
              DispatchShellEvent("delete_archived_tab", {{"tab_id", tab_id}});
              if (sidebar_view && sidebar_view->archive_view_) {
                sidebar_view->archive_view_->ReloadArchivedTabs();
              }
            },
            weak_factory_.GetWeakPtr()));
      }
      pane = archive_view_;
      break;
    }
    case MahoSidebarLibraryRailView::Category::kDownloads: {
      if (!downloads_view_) {
        downloads_view_ = library_content_host_->AddChildView(
            std::make_unique<MahoSidebarDownloadsView>());
        downloads_view_->SetVisible(false);
      }
      pane = downloads_view_;
      break;
    }
    case MahoSidebarLibraryRailView::Category::kMedia: {
      if (!media_view_) {
        media_view_ = library_content_host_->AddChildView(
            std::make_unique<MahoSidebarMediaView>());
        media_view_->SetVisible(false);
      }
      pane = media_view_;
      break;
    }
    case MahoSidebarLibraryRailView::Category::kSpaces: {
      if (!spaces_view_) {
        spaces_view_ = make_action_pane(
            u"Spaces",
            u"Create a new space or edit the active space with the canonical flows.",
            {{u"New Space", u"Create a fresh workspace", true},
             {u"Edit Active Space", u"Edit the active space", true}},
            base::BindRepeating([](base::WeakPtr<MahoSidebarView> sidebar,
                                   size_t index) {
              if (!sidebar) {
                return;
              }
              if (index == 0) {
                sidebar->OpenSpaceCreateSurface();
                return;
              }
              sidebar->OpenSpaceConfigSurface();
            }, weak_factory_.GetWeakPtr()));
      }
      UpdateSpacesPaneActions();
      pane = spaces_view_;
      break;
    }
    case MahoSidebarLibraryRailView::Category::kEasels: {
      if (!easel_view_) {
        easel_view_ = make_action_pane(
            u"Easel",
            u"Open the canonical Easel surface from the library.",
            {{u"Open Easel", u"Open the canonical Easel surface", true}},
            base::BindRepeating([](base::WeakPtr<MahoSidebarView> sidebar,
                                   size_t index) {
              if (!sidebar || index != 0) {
                return;
              }
              sidebar->OpenWebUIInNewTab(GURL(maho::kMahoEaselURL));
            }, weak_factory_.GetWeakPtr()));
      }
      pane = easel_view_;
      break;
    }
    case MahoSidebarLibraryRailView::Category::kBoosts: {
      if (!boost_view_) {
        boost_view_ = make_action_pane(
            u"Boost",
            u"Open the canonical Boost surface from the library.",
            {{u"Open Boost", u"Open the canonical Boost surface", true}},
            base::BindRepeating([](base::WeakPtr<MahoSidebarView> sidebar,
                                   size_t index) {
              if (!sidebar || index != 0) {
                return;
              }
              sidebar->OpenWebUIInNewTab(GURL(maho::kMahoBoostURL));
            }, weak_factory_.GetWeakPtr()));
      }
      pane = boost_view_;
      break;
    }
  }

  if (pane) {
    library_pane_cache_[category] = pane;
  }
  return pane;
}

void MahoSidebarView::ShowLibraryPane(
    MahoSidebarLibraryRailView::Category category) {
  // Hide the previously active pane.
  views::View* old_pane = ActiveLibraryPane();
  if (old_pane) {
    old_pane->SetVisible(false);
  }

  active_library_category_ = category;
  if (library_rail_view_) {
    library_rail_view_->SetSelectedCategory(category);
  }

  views::View* new_pane = GetOrCreateLibraryPane(category);
  if (new_pane) {
    new_pane->SetVisible(true);
  }

  // Category-specific activation hooks.
  if (category == MahoSidebarLibraryRailView::Category::kArchivedTabs &&
      archive_view_) {
    archive_view_->ResetState();
    archive_view_->ReloadArchivedTabs();
    archive_view_->FocusSearchField();
  } else if (category == MahoSidebarLibraryRailView::Category::kDownloads &&
              downloads_view_) {
    downloads_view_->ReloadDownloads();
  } else if (category == MahoSidebarLibraryRailView::Category::kMedia &&
              media_view_) {
    media_view_->ReloadMedia();
  } else if (category == MahoSidebarLibraryRailView::Category::kSpaces) {
    UpdateSpacesPaneActions();
  }
}

views::View* MahoSidebarView::ActiveLibraryPane() const {
  auto it = library_pane_cache_.find(active_library_category_);
  if (it != library_pane_cache_.end()) {
    return it->second;
  }
  return nullptr;
}

MahoSidebarLibraryRailView::Category
MahoSidebarView::active_library_category_for_testing() const {
  return active_library_category_;
}

void MahoSidebarView::OpenLibrary() {
  OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
}

void MahoSidebarView::OpenLibrary(
    MahoSidebarLibraryRailView::Category category) {
  active_library_category_ = category;
  SetBodyMode(MahoSidebarBodyMode::kLibrary);
}

void MahoSidebarView::ExitLibraryToTabs() {
  if (current_body_mode_ == MahoSidebarBodyMode::kLibrary) {
    SetBodyMode(MahoSidebarBodyMode::kTabs);
  }
}

void MahoSidebarView::ToggleArchiveMode() {
  if (current_body_mode_ == MahoSidebarBodyMode::kLibrary) {
    SetBodyMode(MahoSidebarBodyMode::kTabs);
  } else {
    OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
  }
}

void MahoSidebarView::ExitArchiveModeToTabs() {
  ExitLibraryToTabs();
}

void MahoSidebarView::ShowCreateSpaceSurface() {
  SetBodyMode(MahoSidebarBodyMode::kCreateSpace);
}

void MahoSidebarView::OpenWebUIInNewTab(const GURL& url) {
  if (!browser_) {
    return;
  }

  content::OpenURLParams params(url, content::Referrer(),
                                WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                ui::PAGE_TRANSITION_GENERATED, false);
  browser_->OpenURL(params, /*navigation_handle_callback=*/{});
}

void MahoSidebarView::ScheduleRefreshAll() {
  if (refresh_scheduled_) {
    needs_re_refresh_ = true;
    return;
  }
  refresh_scheduled_ = true;
  needs_re_refresh_ = false;
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
  if (current_body_mode_ == MahoSidebarBodyMode::kLibrary &&
      active_library_category_ ==
          MahoSidebarLibraryRailView::Category::kArchivedTabs &&
      archive_view_) {
    archive_view_->ReloadArchivedTabs();
  }
  if (needs_re_refresh_) {
    needs_re_refresh_ = false;
    ScheduleRefreshAll();
  }
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
  const bool pinned = tab_strip_model() ? tab_strip_model()->IsTabPinned(index)
                                        : false;
  std::string core_tab_id;
  bool sync_skipped = true;
  if (tab_list_view_) {
    content::WebContents* contents =
        tab_strip_model()->GetWebContentsAt(index);
    core_tab_id = tab_list_view_->FindCoreTabIdByWebContents(contents);
    sync_skipped = core_tab_id.empty();
    if (!core_tab_id.empty()) {
      DispatchShellEvent(pinned ? "pin_tab" : "unpin_tab",
                         {{"tab_id", core_tab_id}});
    }
  }
  LOG(INFO) << "MahoPinnedDebug OnTabPinnedStateChanged: index=" << index
            << " pinned=" << pinned << " core_tab_id='" << core_tab_id
            << "' sync_skipped=" << sync_skipped;
  if (tab_list_view_ && sync_skipped) {
    DLOG(WARNING) << "MahoSidebarView: Could not resolve core tab_id for "
                  << "strip index " << index << " during pin state change";
  }
  ScheduleRefreshAll();
}

void MahoSidebarView::OnTabGroupChanged(const TabGroupChange& change) {
  ScheduleRefreshAll();
}

void MahoSidebarView::OnSpaceProfileBridgeChanged() {
  if (footer_view_) {
    UpdateFooter(state_adapter_.BuildFooterModel(browser_));
  }
  UpdateSpacesPaneActions();
  ScheduleRefreshAll();
}

bool MahoSidebarView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  if (favorites_view_) {
    return favorites_view_->GetDropFormats(formats, format_types);
  }
  return false;
}

bool MahoSidebarView::CanDrop(const ui::OSExchangeData& data) {
  if (favorites_view_) {
    return favorites_view_->CanDrop(data);
  }
  return false;
}

int MahoSidebarView::OnDragUpdated(const ui::DropTargetEvent& event) {
  if (!favorites_view_) {
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }

  SidebarDragPayload payload;
  if (!ReadMahoDragData(event.data(), payload) ||
      payload.node_kind != SidebarNodeKind::kTab) {
    if (favorites_view_->is_drag_reveal_active_for_testing()) {
      favorites_view_->CollapseFromDrag();
    }
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }

  if (!favorites_view_->GetVisible()) {
    favorites_view_->RevealForDrag();
  }

  gfx::Point fav_location = event.location();
  views::View::ConvertPointToTarget(this, favorites_view_, &fav_location);
  ui::DropTargetEvent fav_event(event.data(), gfx::PointF(fav_location),
                                event.root_location_f(),
                                event.source_operations());
  return favorites_view_->OnDragUpdated(fav_event);
}

views::View::DropCallback MahoSidebarView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  if (!favorites_view_) {
    return base::BindOnce(
        [](const ui::DropTargetEvent&, ui::mojom::DragOperation& output_drag_op,
           std::unique_ptr<ui::LayerTreeOwner>) {
          output_drag_op = ui::mojom::DragOperation::kNone;
        });
  }

  gfx::Point fav_location = event.location();
  views::View::ConvertPointToTarget(this, favorites_view_, &fav_location);
  ui::DropTargetEvent fav_event(event.data(), gfx::PointF(fav_location),
                                event.root_location_f(),
                                event.source_operations());
  return favorites_view_->GetDropCallback(fav_event);
}

void MahoSidebarView::OnDragExited() {
  if (favorites_view_) {
    favorites_view_->OnDragExited();
    favorites_view_->CollapseFromDrag();
  }
}


}  // namespace maho
