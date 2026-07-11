// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_drag_util.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_now_playing_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "ui/base/ui_base_features.h"
#include "ui/compositor/layer.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_utils.h"

#include <memory>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/timer/elapsed_timer.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/translate/content/browser/content_translate_driver.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "maho/browser/maho_tab_id_helper.h"
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
#include "ui/views/controls/scroll_view.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_class_properties.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_action_pane_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_media_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_update_notification_controller.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_update_notification_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_search_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_archive_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_top_bar_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_grain_overlay_view.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_notes_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_easels_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_spaces_view.h"
#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_board_view.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/sidebar/maho_spaces_board_data.h"
#include "net/base/url_util.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/compositor/layer_tree_owner.h"
#include "maho/browser/ui/webui/maho_live_folders/maho_live_folder_cache_warmer.h"
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
  // kGlassFrame path: give the sidebar its own non-opaque compositor layer so
  // the translucent kMahoColorSidebarBackground fill composites over the
  // window's NSVisualEffectView (desktop blur) instead of an opaque backdrop.
  // NOTE: do NOT call layer()->SetOpacity() here - that would multiply text
  // alpha too and make tab labels translucent. Translucency comes solely from
  // the background color's alpha; opaque text stays opaque.
  if (features::IsGlassFrameEnabled()) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }
  if (auto* bridge = MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->AddObserver(this);
  }
  if (tab_strip_model()) {
    tab_strip_model()->AddObserver(this);
  }
  RefreshTranslateObservation();
  // Synchronously seed favorites so Cmd+1..8 works immediately at startup,
  // before the async BuildFavoritesModelOnBackground round-trip completes.
  // Payload is tiny (~12 items) so the FFI cost is <1ms.
  {
    std::string active_space_id_json;
    if (auto* bridge = MahoSpaceProfileBridge::GetInstance(); bridge) {
      const std::string& active_space_id = bridge->GetActiveSpaceId(browser_);
      if (!active_space_id.empty()) {
        active_space_id_json = ::base::GetQuotedJSONString(active_space_id);
      }
    }
    state_adapter_.SetFavoritesModel(
        MahoSidebarStateAdapter::BuildFavoritesModelForSpaceIdJson(
            active_space_id_json));
  }
  ScheduleRefreshAll();

  // Warm the live folder item cache so expanded live folders show children
  // inline immediately, without requiring the user to open the WebUI first.
  LiveFolderCacheWarmer::WarmCache(browser_->profile());
}

MahoSidebarView::~MahoSidebarView() {
  if (auto* bridge = MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->RemoveObserver(this);
  }
  if (tab_strip_model()) {
    tab_strip_model()->RemoveObserver(this);
  }
  translate_observation_.Reset();
}

MahoSidebarNowPlayingView* MahoSidebarView::AddNowPlayingView(std::unique_ptr<MahoSidebarNowPlayingView> view) {
  if (!body_host_ || !footer_view_) {
    return nullptr;
  }
  size_t footer_index = body_host_->GetIndexOf(footer_view_).value_or(0);
  now_playing_view_ = body_host_->AddChildViewAt(std::move(view), footer_index);
  return now_playing_view_;
}

void MahoSidebarView::UpdateSidebarBorderForOverlay() {
  if (IsInLibraryMode()) {
    SetBorder(nullptr);
  } else {
    SetBorder(views::CreateRoundedRectBorder(
        1, sidebar_layout::kRailCornerRadiusDp, ui::kColorSysSurfaceVariant));
  }
}

void MahoSidebarView::OnThemeChanged() {
  views::View::OnThemeChanged();

  std::optional<MahoSpaceThemeState::ThemeData> theme_data =
      MahoSpaceThemeState::GetThemeData();
  const float texture = theme_data ? theme_data->texture : 0.0f;

  SetBackground(views::CreateRoundedRectBackground(
      kMahoColorSidebarBackground, sidebar_layout::kRailCornerRadiusDp));
  UpdateSidebarBorderForOverlay();
  if (library_divider_) {
    library_divider_->SetBackground(
        views::CreateSolidBackground(ui::kColorSeparator));
  }

  if (grain_overlay_) {
    grain_overlay_->SetTexture(texture);
  }

  auto model = state_adapter_.BuildViewStateModel(browser_);
  UpdateTopBar(model.top_bar);
  UpdateSearch(model.search);
  UpdateFavorites(model.favorites);
  UpdateFooter(model.footer);
}

void MahoSidebarView::ViewHierarchyChanged(
    const views::ViewHierarchyChangedDetails& details) {
  views::View::ViewHierarchyChanged(details);
  if (!details.is_add || !details.child) {
    return;
  }
  // Sidebar sits under a translucent BrowserFrameView when kGlassFrame is
  // enabled. Any Label descendant that would otherwise paint subpixel-AA
  // text against a non-opaque ancestor trips a DCHECK in Label::PaintText.
  // details.child is the root of the added subtree; walk it recursively.
  std::vector<views::View*> stack{details.child};
  while (!stack.empty()) {
    views::View* view = stack.back();
    stack.pop_back();
    if (auto* label = views::AsViewClass<views::Label>(view)) {
      label->SetSubpixelRenderingEnabled(false);
    }
    for (views::View* child : view->children()) {
      stack.push_back(child);
    }
  }
}

void MahoSidebarView::OnTabListScrolled() {
  if (tab_list_view_) {
    tab_list_view_->OnScrollChanged();
  }
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
  space_header_view_ =
      top_region->AddChildView(CreateSidebarSpaceHeaderRow());
  space_header_view_->SetVisible(false);  // Hidden until first model update.

  auto scroll = std::make_unique<views::ScrollView>();
  scroll->SetBackgroundColor(std::nullopt);
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  scroll->SetDrawOverflowIndicator(false);
  // Use (0, 0) so ScrollView reports preferred height = 0 and accepts whatever
  // height the parent BoxLayout flex assigns. (0, INT_MAX) would make preferred
  // height equal to content height, pushing footer_view_ off-screen.
  scroll->ClipHeightTo(0, 0);

  auto tab_list = std::make_unique<MahoSidebarTabListView>(browser_);
  tab_list_view_ = scroll->SetContents(std::move(tab_list));
  tab_scroll_view_ = tabs_surface_->AddChildView(std::move(scroll));

  tab_list_view_->SetAutoScrollScrollView(tab_scroll_view_);

  tab_list_view_->SetFavoritesSyncCallback(base::BindRepeating(
      [](MahoSidebarView* self) {
        if (self->favorites_view_) {
          self->UpdateFavorites(
              self->state_adapter_.BuildFavoritesModel(self->browser_));
        }
      },
      base::Unretained(this)));

  tab_scroll_subscription_ = tab_scroll_view_->AddContentsScrolledCallback(
      base::BindRepeating(&MahoSidebarView::OnTabListScrolled,
                          base::Unretained(this)));

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


  update_notification_controller_ =
      std::make_unique<MahoSidebarUpdateNotificationController>(
          browser_,
          base::BindRepeating(&MahoSidebarView::ScheduleRefreshAll,
                              base::Unretained(this)));
  update_notification_view_ = AddChildView(
      std::make_unique<MahoSidebarUpdateNotificationView>());
  update_notification_view_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  update_notification_view_->AddObserver(this);

  footer_view_ = body_host_->AddChildView(
      std::make_unique<MahoSidebarFooterView>(browser_));

  normal_body_layout->SetFlexForView(tab_scroll_view_, 1, true);
  body_layout->SetFlexForView(content_host_, 1, false);
  root_layout->SetFlexForView(body_host_, 1, true);

  create_space_view_ =
      AddChildView(std::make_unique<MahoSidebarCreateSpaceView>(browser_));
  create_space_view_->SetVisible(false);
  create_space_view_->SetCloseCallback(base::BindRepeating(
      &MahoSidebarView::HideCreateSpace, weak_factory_.GetWeakPtr()));
  root_layout->SetFlexForView(create_space_view_, 1, true);

  grain_overlay_ = AddChildView(std::make_unique<MahoSidebarGrainOverlayView>());
  grain_overlay_->SetProperty(views::kViewIgnoredByLayoutKey, true);
}

void MahoSidebarView::Layout(PassKey pass_key) {
  LayoutSuperclass<views::View>(this);
  if (grain_overlay_) {
    grain_overlay_->SetBoundsRect(GetLocalBounds());
  }
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
    top_bar_view_->SetNavClusterVisible(mode != MahoSidebarBodyMode::kLibrary);
  }
  if (library_container_) {
    library_container_->SetVisible(mode == MahoSidebarBodyMode::kLibrary);
  }
  if (mode != previous_mode) {
    if (auto* container =
            views::AsViewClass<MahoSidebarContainerView>(parent())) {
      container->ApplyPreferredWidthFromPrefs();
    }
  }
  if (footer_view_) {
    footer_view_->SetVisible(mode != MahoSidebarBodyMode::kLibrary);
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

  return !IsDescendantOfView(view, library_container_);
}

void MahoSidebarView::HandleLibraryCategorySelected(
    MahoSidebarLibraryRailView::Category category) {
  if (!library_rail_view_) {
    return;
  }
  PrefService* prefs = browser_->profile()->GetPrefs();
  const bool use_overlay =
      prefs->GetBoolean(sidebar_prefs::kSpacesFullViewport);
  auto* container = views::AsViewClass<MahoSidebarContainerView>(parent());

  if (category == MahoSidebarLibraryRailView::Category::kSpaces && use_overlay) {
    // D-11: Hide embedded library content instead of destroying views to avoid dangling pointers
    library_content_host_->SetVisible(false);
    if (library_divider_) {
      library_divider_->SetVisible(false);
    }
    active_library_category_ = category;
    UpdateSidebarBorderForOverlay();
    if (container) {
      container->ApplyPreferredWidthFromPrefs();
      container->ToggleSpacesOverlay();
    }
    return;
  }

  // Non-Spaces category (or Spaces with flag=false): if overlay is showing,
  // dismiss it before switching the embedded library pane.
  if (container && container->IsSpacesOverlayVisible()) {
    container->DismissSpacesOverlay();
  }
  ShowLibraryPane(category);
}

void MahoSidebarView::HandleLibraryBack() {
  SetBodyMode(MahoSidebarBodyMode::kTabs);
}

void MahoSidebarView::UpdateSpacesPaneActions() {
}

void MahoSidebarView::OpenSpaceCreateSurface() {
  ShowCreateSpace();
}

void MahoSidebarView::ShowCreateSpace() {
  if (!create_space_view_) {
    return;
  }
  if (top_bar_view_) {
    top_bar_view_->SetVisible(false);
  }
  if (body_host_) {
    body_host_->SetVisible(false);
  }
  create_space_view_->SetVisible(true);
  create_space_view_->PrepareForOpen();
  create_space_view_->RequestNameFocus();
  InvalidateLayout();
}

void MahoSidebarView::HideCreateSpace() {
  if (!create_space_view_) {
    return;
  }
  create_space_view_->SetVisible(false);
  if (top_bar_view_) {
    top_bar_view_->SetVisible(true);
  }
  if (body_host_) {
    body_host_->SetVisible(true);
  }
  InvalidateLayout();
}

void MahoSidebarView::OpenSpaceConfigSurface() {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  const std::string active_space_id =
      bridge ? bridge->GetActiveSpaceId(browser_) : std::string();
  if (active_space_id.empty()) {
    return;
  }
  return;
}

views::View* MahoSidebarView::GetOrCreateLibraryPane(
    MahoSidebarLibraryRailView::Category category) {
  auto it = library_pane_cache_.find(category);
  if (it != library_pane_cache_.end()) {
    return it->second;
  }

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
        auto view = std::make_unique<MahoSpacesOverlayBoardView>(
            browser_,
            SpacesBoardRenderMode::kEmbedded,
            BuildSpacesBoardModel(),
            base::BindRepeating(
                [](base::WeakPtr<Browser> browser,
                   const std::string& space_id) {
                  if (!browser) {
                    return;
                  }
                  auto* bridge = MahoSpaceProfileBridge::GetInstance();
                  if (bridge) {
                    bridge->SwitchToSpace(browser.get(), space_id);
                  }
                },
                browser_->AsWeakPtr()));
        spaces_view_ = library_content_host_->AddChildView(std::move(view));
        spaces_view_->SetVisible(false);
      }
      pane = spaces_view_;
      break;
    }
    case MahoSidebarLibraryRailView::Category::kNotes: {
      if (!notes_view_) {
        notes_view_ = library_content_host_->AddChildView(
            std::make_unique<MahoSidebarNotesView>(browser_));
        notes_view_->SetVisible(false);
      }
      pane = notes_view_;
      break;
    }
    case MahoSidebarLibraryRailView::Category::kEasels: {
      if (!easel_view_) {
        easel_view_ = library_content_host_->AddChildView(
            std::make_unique<MahoSidebarEaselsView>(browser_));
        easel_view_->SetVisible(false);
      }
      pane = easel_view_;
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
  library_content_host_->SetVisible(true);
  if (library_divider_) {
    library_divider_->SetVisible(false);
  }
  UpdateSidebarBorderForOverlay();

  // Hide the previously active pane.
  views::View* old_pane = ActiveLibraryPane();
  if (old_pane) {
    old_pane->SetVisible(false);
  }

  active_library_category_ = category;
  if (library_rail_view_) {
    library_rail_view_->SetSelectedCategory(category);
  }
  if (auto* container = views::AsViewClass<MahoSidebarContainerView>(parent())) {
    container->ApplyPreferredWidthFromPrefs();
  }

  // Rebuild the Spaces board on every activation so it reflects the latest
  // space list (previous view is torn down before recreation below).
  if (category == MahoSidebarLibraryRailView::Category::kSpaces &&
      spaces_view_) {
    library_content_host_->RemoveChildViewT(spaces_view_.get());
    spaces_view_ = nullptr;
    library_pane_cache_.erase(category);
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
  } else if (category == MahoSidebarLibraryRailView::Category::kNotes &&
             notes_view_) {
    notes_view_->EnsureWebUILoaded();
  } else if (category == MahoSidebarLibraryRailView::Category::kEasels &&
             easel_view_) {
    easel_view_->EnsureWebUILoaded();
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

void MahoSidebarView::ActivateTabById(const std::string& tab_id) {
  if (tab_list_view_) {
    tab_list_view_->ActivateTabById(tab_id);
  }
}

bool MahoSidebarView::ActivateFavoriteByIndex(size_t index) {
  return favorites_view_ && favorites_view_->ActivateFavoriteByIndex(index);
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
  // Any full-rebuild request supersedes a pending cosmetic patch: the full
  // rebuild will already emit fresh titles/URLs/favicons.
  cosmetic_refresh_timer_.Stop();
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
    const std::string& active_space_id = bridge->GetActiveSpaceId(browser_);
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
  if (refresh_in_progress_) {
    needs_re_refresh_ = true;
    return;
  }
  refresh_in_progress_ = true;
  refresh_scheduled_ = false;

  std::string space_id_json;
  if (auto* bridge = MahoSpaceProfileBridge::GetInstance()) {
    space_id_json = base::GetQuotedJSONString(bridge->GetActiveSpaceId(browser_));
  }
  bool safe_mode = IsSidebarSafeMode();

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&MahoSidebarStateAdapter::BuildStateOnBackground,
                     space_id_json, safe_mode),
      base::BindOnce(&MahoSidebarView::OnBackgroundStateReady,
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarView::OnBackgroundStateReady(SidebarStateBackgroundResult result) {
  refresh_in_progress_ = false;

  auto model = state_adapter_.BuildViewStateModelFromResult(browser_, std::move(result));
  ApplyViewState(model);

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
  if (tab_list_view_) {
    tab_list_view_->SetLastFavoritesModel(model);
  }
}

void MahoSidebarView::UpdateTabList(const MahoSidebarTabListModel& model) {
  if (tab_list_view_) {
    tab_list_view_->Update(model, browser_);
  }
  if (space_header_view_) {
    UpdateSidebarSpaceHeaderRow(space_header_view_, model.active_space_icon,
                                model.active_space_name);
    space_header_view_->SetVisible(!model.active_space_name.empty());
  }
}

void MahoSidebarView::UpdateFooter(const MahoSidebarFooterModel& model) {
  if (footer_view_) {
    footer_view_->Update(model);
  }
  if (update_notification_view_ && update_notification_controller_) {
    update_notification_view_->Update(
        update_notification_controller_->BuildModel());
    PositionUpdateNotification();
  }
}

void MahoSidebarView::OnViewPreferredSizeChanged(views::View* observed_view) {
  if (observed_view == update_notification_view_) {
    PositionUpdateNotification();
  }
}

void MahoSidebarView::OnViewBoundsChanged(views::View* observed_view) {
  if (observed_view == footer_view_ || observed_view == this) {
    PositionUpdateNotification();
  }
}

void MahoSidebarView::OnViewVisibilityChanged(views::View* observed_view,
                                              views::View* starting_view,
                                              bool visible) {
}

void MahoSidebarView::PositionUpdateNotification() {
  if (!update_notification_view_ || !footer_view_ || !body_host_) {
    return;
  }
  const gfx::Rect local_bounds = GetLocalBounds();
  if (local_bounds.IsEmpty()) {
    return;
  }
  // Anchor the floating card to the same horizontal extent as `body_host_`
  // (the visible sidebar rail) and add an inner gutter so the card does not
  // bleed to the rail's edge.
  constexpr int kHorizontalMargin = 10;
  const int rail_x = body_host_->x();
  const int rail_w = body_host_->width();
  const int width = std::max(0, rail_w - kHorizontalMargin * 2);
  const gfx::Size pref =
      update_notification_view_->GetPreferredSize(views::SizeBounds(width, {}));
  const int footer_top =
      footer_view_->GetVisible()
          ? body_host_->y() + footer_view_->y()
          : local_bounds.bottom();
  const int top = footer_top - pref.height();
  update_notification_view_->SetBoundsRect(
      gfx::Rect(rail_x + kHorizontalMargin, top, width, pref.height()));
}

void MahoSidebarView::PositionNowPlayingView() {
  if (!now_playing_view_ || !footer_view_ || !body_host_) {
    return;
  }
  if (!now_playing_view_->GetVisible()) {
    now_playing_view_->SetBoundsRect(gfx::Rect());
    return;
  }
  const gfx::Rect local_bounds = GetLocalBounds();
  if (local_bounds.IsEmpty()) {
    return;
  }
  constexpr int kHorizontalMargin = 10;
  const int rail_x = body_host_->x();
  const int rail_w = body_host_->width();
  const int width = std::max(0, rail_w - kHorizontalMargin * 2);
  const gfx::Size pref =
      now_playing_view_->GetPreferredSize(views::SizeBounds(width, {}));
  const int footer_top =
      footer_view_->GetVisible()
          ? body_host_->y() + footer_view_->y()
          : local_bounds.bottom();
  const int top = footer_top - pref.height();
  now_playing_view_->SetBoundsRect(
      gfx::Rect(rail_x + kHorizontalMargin, top, width, pref.height()));
}

TabStripModel* MahoSidebarView::tab_strip_model() const {
  return browser_ ? browser_->tab_strip_model() : nullptr;
}

void MahoSidebarView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  base::ElapsedTimer maho_perf_t;
  const char* ch = "?";
  switch (change.type()) {
    case TabStripModelChange::kSelectionOnly: ch = "SelectionOnly"; break;
    case TabStripModelChange::kInserted: ch = "Inserted"; break;
    case TabStripModelChange::kRemoved: ch = "Removed"; break;
    case TabStripModelChange::kMoved: ch = "Moved"; break;
    case TabStripModelChange::kReplaced: ch = "Replaced"; break;
  }
  LOG(INFO) << "[MAHO_PERF] Sidebar::OnTabStripModelChanged type=" << ch
            << " active_tab_changed=" << selection.active_tab_changed();

  if (selection.active_tab_changed() ||
      change.type() == TabStripModelChange::kReplaced) {
    RefreshTranslateObservation();
  }

  auto get_new_active_tab_id = [&selection]() -> std::string {
    if (!selection.new_contents) {
      return {};
    }
    auto* helper = MahoTabIdHelper::FromWebContents(selection.new_contents);
    return helper ? helper->stable_tab_id() : std::string();
  };

  if (change.type() == TabStripModelChange::kSelectionOnly &&
      selection.active_tab_changed() && tab_list_view_) {
    tab_list_view_->UpdateActiveTabHighlightOnly(get_new_active_tab_id());
    if (favorites_view_) {
      UpdateFavorites(state_adapter_.BuildFavoritesModel(browser_));
    }
    LOG(INFO) << "[MAHO_PERF] Sidebar::OnTabStripModelChanged END(fast_select) "
              << maho_perf_t.Elapsed().InMillisecondsF() << "ms";
    return;
  }

  if (change.type() == TabStripModelChange::kInserted &&
      selection.active_tab_changed() && tab_list_view_) {
    const std::string new_active_tab_id = get_new_active_tab_id();
    if (tab_list_view_->HasTabInLastModel(new_active_tab_id)) {
      tab_list_view_->UpdateActiveTabHighlightOnly(new_active_tab_id);
      if (favorites_view_) {
        UpdateFavorites(state_adapter_.BuildFavoritesModel(browser_));
      }
      LOG(INFO) << "[MAHO_PERF] Sidebar::OnTabStripModelChanged END(fast_wake) "
                << maho_perf_t.Elapsed().InMillisecondsF() << "ms";
      return;
    }
  }

  LOG(INFO) << "[MAHO_PERF] Sidebar::OnTabStripModelChanged -> ScheduleRefreshAll";
  ScheduleRefreshAll();
  LOG(INFO) << "[MAHO_PERF] Sidebar::OnTabStripModelChanged END(slow) "
            << maho_perf_t.Elapsed().InMillisecondsF() << "ms";
}

void MahoSidebarView::OnSplitTabChanged(const SplitTabChange& change) {
  ScheduleRefreshAll();
}

void MahoSidebarView::OnTabChangedAt(tabs::TabInterface* tab,
                                     int index,
                                     TabChangeType change_type) {
  if (change_type == TabChangeType::kAll) {
    ScheduleCosmeticRefresh();
    return;
  }
  // P3: Skip ScheduleRefreshAll for non-active tab changes; only the active
  // tab's title/favicon update needs immediate sidebar reflection. With 870+
  // tabs, background loads fire OnTabChangedAt hundreds of times per second.
  TabStripModel* strip = browser_ ? browser_->tab_strip_model() : nullptr;
  if (strip && index != strip->active_index()) {
    return;
  }
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

void MahoSidebarView::OnSpaceProfileBridgeChanged(bool is_structural) {
  if (is_structural) {
    OnSpaceProfileBridgeChanged();
  } else {
    LOG(INFO) << "[MAHO_PERF] NotifyChanged cosmetic_skipped_rebuild=true";
    ScheduleCosmeticRefresh();
  }
}

void MahoSidebarView::OnSpaceProfileBridgeChanged() {
  state_adapter_.InvalidateFooterState();
  // Structural change preempts any pending cosmetic refresh so we don't apply
  // stale title/URL patches on top of a freshly-rebuilt tab list.
  cosmetic_refresh_timer_.Stop();
  UpdateSpacesPaneActions();
  ScheduleRefreshAll();
}

void MahoSidebarView::ScheduleCosmeticRefresh() {
  // 16ms debounce: coalesces bursts of title/URL/favicon updates from rapid
  // navigation into a single UpdateTabList() pass at frame boundary.
  constexpr base::TimeDelta kCosmeticDebounce = base::Milliseconds(16);
  cosmetic_refresh_timer_.Start(
      FROM_HERE, kCosmeticDebounce,
      base::BindOnce(&MahoSidebarView::ApplyCosmeticRefresh,
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarView::ApplyCosmeticRefresh() {
  if (tab_list_view_) {
    tab_list_view_->ApplyCosmeticRefresh();
  }
  UpdateFavorites(state_adapter_.BuildFavoritesModel(browser_));
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

  const bool now_over =
      gfx::Rect(favorites_view_->size()).Contains(fav_location);

  if (last_over_favorites_ && !now_over) {
    favorites_view_->OnFavoritesZoneExited();
  }
  last_over_favorites_ = now_over;

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
  last_over_favorites_ = false;
  if (favorites_view_) {
    favorites_view_->OnDragExited();
    favorites_view_->CollapseFromDrag();
  }
}

void MahoSidebarView::OnDragEnded() {
  last_over_favorites_ = false;
  if (favorites_view_) {
    favorites_view_->OnDragEnded();
  }
}

void MahoSidebarView::OnDragStarted(const std::string& tab_id) {
  if (favorites_view_) {
    favorites_view_->OnExternalDragStarted(tab_id);
  }
  last_over_favorites_ = false;
}

void MahoSidebarView::OnTranslateEnabledChanged(content::WebContents* source) {
  ScheduleRefreshAll();
}

void MahoSidebarView::OnIsPageTranslatedChanged(content::WebContents* source) {
  ScheduleRefreshAll();
}

void MahoSidebarView::RefreshTranslateObservation() {
  translate_observation_.Reset();
  TabStripModel* tsm = tab_strip_model();
  if (!tsm) {
    return;
  }
  const int active_index = tsm->active_index();
  if (active_index == TabStripModel::kNoTab ||
      !tsm->ContainsIndex(active_index)) {
    return;
  }
  content::WebContents* contents = tsm->GetWebContentsAt(active_index);
  if (!contents) {
    return;
  }
  auto* translate_client = ChromeTranslateClient::FromWebContents(contents);
  if (!translate_client) {
    return;
  }
  translate::ContentTranslateDriver* driver =
      translate_client->translate_driver();
  if (driver) {
    translate_observation_.Observe(driver);
  }
}

}  // namespace maho
