// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_drag_util.h"
#include "ui/views/widget/widget.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_now_playing_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "maho/browser/maho_features.h"
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
#include "maho/browser/maho_tab_registry.h"
#include "url/gurl.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/scrollbar/overlay_scroll_bar.h"
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

  // R-11: OTR (Incognito/Guest/system) sidebar reads only the current
  // window's TabStripModel. Never touch MahoCore, SpaceBridge, favorites
  // FFI, ScheduleRefreshAll, or the live-folder warmer for any OTR profile.
  is_otr_ = browser_ && browser_->profile() &&
            browser_->profile()->IsOffTheRecord();
  if (!is_otr_) {
    if (auto* bridge = MahoSpaceProfileBridge::GetInstance(); bridge) {
      bridge->AddObserver(this);
    }
    RefreshTranslateObservation();
    // Synchronously seed favorites so Cmd+1..8 works immediately at startup,
    // before the async ScheduleRefreshAll() state build completes.
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
  } else {
    // R-11: Populate the private rail from the window-local strip only.
    RefreshPrivateSidebar();
  }

  // TabStripModel observation is always registered: the private sidebar
  // needs OnTabStripModelChanged to keep its local tab list current.
  if (tab_strip_model()) {
    tab_strip_model()->AddObserver(this);
  }
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
    // Left corners rounded, right edge square so the sidebar sits flush against
    // the content pane with no seam gap. CreateRoundedRectBorder only supports a
    // uniform radius, so drop the visible outline (keep the 1dp inset to
    // preserve layout) and let the per-corner background define the shape.
    SetBorder(views::CreateEmptyBorder(1));
  }
}

void MahoSidebarView::OnThemeChanged() {
  views::View::OnThemeChanged();

  std::optional<MahoSpaceThemeState::ThemeData> theme_data =
      MahoSpaceThemeState::GetThemeData();
  const float texture = theme_data ? theme_data->texture : 0.0f;

  SetBackground(views::CreateRoundedRectBackground(
      kMahoColorSidebarBackground,
      gfx::RoundedCornersF(sidebar_layout::kRailCornerRadiusDp, 0, 0,
                           sidebar_layout::kRailCornerRadiusDp)));
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
  if (cosmetic_refresh_timer_.IsRunning()) {
    cosmetic_refresh_timer_.Stop();
    cosmetic_refresh_pending_ = true;
  }
  constexpr base::TimeDelta kScrollIdleDelay = base::Milliseconds(120);
  last_tab_list_scroll_time_ = base::TimeTicks::Now();
  // Lazy arming: only pay the timer cost when cosmetic work is actually
  // pending. The hot path (no cosmetic work armed) exits here with zero timer
  // churn.
  if (cosmetic_refresh_pending_ && !scroll_idle_timer_.IsRunning()) {
    scroll_idle_timer_.Start(FROM_HERE, kScrollIdleDelay,
                             base::BindOnce(&MahoSidebarView::OnScrollIdle,
                                            weak_factory_.GetWeakPtr()));
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
      views::ScrollView::ScrollBarMode::kEnabled);
  // Arc-style thin, auto-hiding OVERLAY scrollbar: fades in during scroll and
  // out when idle, slim rounded thumb, no track chrome, and overlays the
  // content (reserves no layout width). Scroll extent still derives from the
  // spacer-based content height, so GetMaxPosition() stays correct.
  scroll->SetVerticalScrollBar(std::make_unique<views::OverlayScrollBar>(
      views::ScrollBar::Orientation::kVertical));
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
    if (auto* container =
            views::AsViewClass<MahoSidebarContainerView>(parent())) {
      if (container->IsSpacesOverlayVisible()) {
        container->DismissSpacesOverlay();
      }
      if (container->IsLibraryOverlayVisible()) {
        container->DismissLibraryOverlay();
      }
    }
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
    if (container && container->IsLibraryOverlayVisible()) {
      container->DismissLibraryOverlay();
    }
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

  if (category == MahoSidebarLibraryRailView::Category::kDownloads ||
      category == MahoSidebarLibraryRailView::Category::kArchivedTabs) {
    if (container && container->IsSpacesOverlayVisible()) {
      container->DismissSpacesOverlay();
    }
    library_content_host_->SetVisible(false);
    if (library_divider_) {
      library_divider_->SetVisible(false);
    }
    active_library_category_ = category;
    if (library_rail_view_) {
      library_rail_view_->SetSelectedCategory(category);
    }
    UpdateSidebarBorderForOverlay();
    if (container) {
      container->ApplyPreferredWidthFromPrefs();
      container->ToggleLibraryOverlay(category);
    }
    return;
  }

  // Non-overlay category (kMedia, or kSpaces with flag=false): if an overlay is
  // showing, dismiss it before switching the embedded library pane.
  if (container && container->IsSpacesOverlayVisible()) {
    container->DismissSpacesOverlay();
  }
  if (container && container->IsLibraryOverlayVisible()) {
    container->DismissLibraryOverlay();
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
            &MahoSidebarView::RestoreArchivedTabAndActivate,
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
            std::make_unique<MahoSidebarDownloadsView>(browser_));
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
                  MahoSidebarView::ActivateSpaceAndTab(browser.get(), space_id);

                },
                browser_->AsWeakPtr()));
        spaces_view_ = library_content_host_->AddChildView(std::move(view));
        spaces_view_->SetVisible(false);
      }
      pane = spaces_view_;
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

// static
bool MahoSidebarView::ActivateSpaceAndTab(Browser* browser, const std::string& space_id) {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();

  if (!bridge) {
    return false;
  }
  if (!bridge->SwitchToSpace(browser, space_id)) {
    return false;
  }

  if (base::FeatureList::IsEnabled(maho::features::kMahoSpaceSwitchActivatesTab)) {
    auto* core = maho::GetCore();
    if (core) {
      char* target_tab_raw = maho_core_get_activation_target_for_space(core, space_id.c_str());
      if (target_tab_raw) {
        std::string target_tab_id(target_tab_raw);
        maho_core_free_string(target_tab_raw);

        if (!target_tab_id.empty()) {
          // Implement split-view teardown policy (T5)
          MahoSplitViewController split_controller(browser);
          if (split_controller.IsSplitActive()) {
            TabStripModel* model = browser->tab_strip_model();
            if (model) {
              int active_index = model->active_index();
              bool target_is_in_split = false;
              if (active_index != TabStripModel::kNoTab) {
                std::optional<split_tabs::SplitTabId> split_id = model->GetSplitForTab(active_index);
                if (split_id.has_value()) {
                  if (auto* split_data = model->GetSplitData(split_id.value())) {
                    for (auto* split_tab : split_data->ListTabs()) {
                      if (split_tab) {
                        auto* helper = MahoTabIdHelper::FromWebContents(split_tab->GetContents());
                        if (helper && helper->stable_tab_id() == target_tab_id) {
                          target_is_in_split = true;
                          break;
                        }
                      }
                    }
                  }
                }
              }
              if (!target_is_in_split) {
                split_controller.RemoveSplit();
              }
            }
          }

          // Activate the target tab by ID
          BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
          if (browser_view && browser_view->maho_sidebar_container()) {
            auto* sidebar_container = static_cast<MahoSidebarContainerView*>(
                browser_view->maho_sidebar_container());
            if (auto* sidebar = static_cast<MahoSidebarView*>(
                    sidebar_container->sidebar_view())) {
              sidebar->ActivateTabById(target_tab_id);
            }
          }
        }
      }
    }
  }

  return true;
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

void MahoSidebarView::RestoreArchivedTabAndActivate(const std::string& tab_id,
                                                   const std::string& space_id) {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (bridge) {
    std::string active_space_id = bridge->GetActiveSpaceId(browser_);
    if (!space_id.empty() && space_id != active_space_id) {
      if (!bridge->SwitchToSpace(browser_, space_id)) {
        DLOG(WARNING) << "Failed to switch to space: " << space_id << " for restored tab: " << tab_id;
        return;
      }
    }
  }

  DispatchShellEvent("restore_archived_tab", {{"tab_id", tab_id}});

  if (tab_list_view_) {
    tab_list_view_->ActivateTabById(tab_id);
  }

  if (archive_view_) {
    archive_view_->ReloadArchivedTabs();
  }
  ExitLibraryToTabs();
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
  cosmetic_refresh_pending_ = false;
  if (refresh_state_ == RefreshState::kInFlight) {
    refresh_dirty_ = true;
    return;
  }
  refresh_state_ = RefreshState::kInFlight;
  refresh_dirty_ = false;
  RefreshAll();
}

void MahoSidebarView::RefreshAll() {
  if (is_otr_) {
    // R-11: OTR never posts the background state build (which reads MahoCore
    // and the Space bridge). Rebuild synchronously from the window-local strip.
    RefreshPrivateSidebar();
    refresh_state_ = RefreshState::kIdle;
    if (refresh_dirty_) {
      refresh_dirty_ = false;
    }
    return;
  }
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

void MahoSidebarView::RefreshAllSynchronously() {
  if (is_otr_) {
    RefreshPrivateSidebar();
    return;
  }
  std::string space_id_json;
  if (auto* bridge = MahoSpaceProfileBridge::GetInstance()) {
    space_id_json =
        base::GetQuotedJSONString(bridge->GetActiveSpaceId(browser_));
  }
  const bool safe_mode = IsSidebarSafeMode();
  // The state build is a synchronous FFI fetch + parse (~3-21ms), cheap enough
  // to run inline. Doing so on a drop avoids RefreshAll()'s base::ThreadPool
  // round-trip, whose USER_VISIBLE scheduling latency (worst under load) is what
  // makes a dropped tab take hundreds of ms to settle into its final position.
  auto result = MahoSidebarStateAdapter::BuildStateOnBackground(space_id_json,
                                                                safe_mode);
  ApplyViewState(
      state_adapter_.BuildViewStateModelFromResult(browser_, std::move(result)));
}

void MahoSidebarView::RefreshPrivateSidebar() {
  // R-11: Window-local, MahoCore/Space-free rebuild. BuildTabListModel reads
  // only browser_->tab_strip_model() when no v2 FFI tree is cached (which the
  // private path never populates); BuildSearchModel/BuildTopBarModel read only
  // the active WebContents. No favorites/folders/footer/library sources.
  UpdateTabList(state_adapter_.BuildTabListModel(browser_));
  UpdateSearch(state_adapter_.BuildSearchModel(browser_));
  UpdateTopBar(state_adapter_.BuildTopBarModel(browser_));
}

void MahoSidebarView::OnBackgroundStateReady(SidebarStateBackgroundResult result) {
  LOG(INFO) << "OnBackgroundStateReady start";
  if (background_state_ready_callback_for_testing_) {
    if (!background_state_ready_callback_for_testing_.Run(this, result)) {
      return;
    }
  }
  if (refresh_dirty_) {
    refresh_dirty_ = false;
    refresh_state_ = RefreshState::kIdle;
    ScheduleRefreshAll();
    return;
  }

  auto model = state_adapter_.BuildViewStateModelFromResult(browser_, std::move(result));
  ApplyViewState(model);

  if (current_body_mode_ == MahoSidebarBodyMode::kLibrary &&
      active_library_category_ ==
          MahoSidebarLibraryRailView::Category::kArchivedTabs &&
      archive_view_) {
    archive_view_->ReloadArchivedTabs();
  }

  refresh_state_ = RefreshState::kIdle;
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

  // Strip-index-shifting changes invalidate the tab_id->index cache; clear it
  // so a stale entry can't resolve a row click / CloseTabById to a neighbor.
  // Must run before the fast paths below, which return without a full refresh.
  if (tab_list_view_ &&
      (change.type() == TabStripModelChange::kMoved ||
       change.type() == TabStripModelChange::kRemoved ||
       change.type() == TabStripModelChange::kInserted)) {
    tab_list_view_->InvalidateTabIndexCache();
  }

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

  if (selection.active_tab_changed() && selection.new_contents) {
    std::string new_active_tab_id = get_new_active_tab_id();
    if (!new_active_tab_id.empty()) {
      DispatchShellEvent("activate_tab", {{"tab_id", new_active_tab_id}});
    }
  }

  // The fast paths below skip the full RefreshAll (RebuildRows on 870+ rows),
  // but the search pill and top bar mirror the ACTIVE tab's live state
  // (URL/host, display mode, back/forward/reload). A plain tab activation —
  // sidebar row click, Ctrl+Tab, or focusing the other pane of a split
  // (MultiContentsView::OnWebContentsFocused → ActivateTabAt, kSelectionOnly)
  // — produces no follow-up event for an idle tab, so without this the pill
  // keeps showing the previous tab's address (and Copy URL copies it). Both
  // builders only read the active WebContents (no FFI), so they are cheap
  // enough for the fast path.
  auto refresh_active_tab_chrome = [&]() {
    UpdateSearch(state_adapter_.BuildSearchModel(browser_));
    UpdateTopBar(state_adapter_.BuildTopBarModel(browser_));
  };

  if (change.type() == TabStripModelChange::kSelectionOnly &&
      selection.active_tab_changed() && tab_list_view_) {
    const std::string new_active_tab_id = get_new_active_tab_id();
    // An empty id with a live active tab means the stable id is not assigned
    // yet (startup race). Passing "" would clear the old highlight and never
    // paint the new one, so fall through to the full refresh instead; an
    // empty id with no active contents is a legitimate "clear highlight".
    if (!selection.new_contents || !new_active_tab_id.empty()) {
      tab_list_view_->UpdateActiveTabHighlightOnly(new_active_tab_id);
      if (favorites_view_) {
        UpdateFavorites(state_adapter_.BuildFavoritesModel(browser_));
      }
      refresh_active_tab_chrome();
      LOG(INFO) << "[MAHO_PERF] Sidebar::OnTabStripModelChanged END(fast_select) "
                << maho_perf_t.Elapsed().InMillisecondsF() << "ms";
      return;
    }
  }

  if (change.type() == TabStripModelChange::kInserted &&
      selection.active_tab_changed() && tab_list_view_) {
    const std::string new_active_tab_id = get_new_active_tab_id();
    if (tab_list_view_->HasTabInLastModel(new_active_tab_id)) {
      tab_list_view_->UpdateActiveTabHighlightOnly(new_active_tab_id);
      if (favorites_view_) {
        UpdateFavorites(state_adapter_.BuildFavoritesModel(browser_));
      }
      refresh_active_tab_chrome();
      LOG(INFO) << "[MAHO_PERF] Sidebar::OnTabStripModelChanged END(fast_wake) "
                << maho_perf_t.Elapsed().InMillisecondsF() << "ms";
      return;
    }
  }

  // Suspending a close-protected (pinned/favorite) tab arrives as kRemoved:
  // the tab stays in the model, so only the row (minus->X) and the active
  // highlight need updating — a full RebuildRows over hundreds of rows (the
  // ~1.5s freeze) is avoidable. TrySuspendFastPath returns false for real
  // closes / structural changes, falling through to ScheduleRefreshAll below.
  if (change.type() == TabStripModelChange::kRemoved && tab_list_view_) {
    const TabStripModelChange::Remove* remove = change.GetRemove();
    if (remove && remove->contents.size() == 1) {
      const std::string removed_tab_id =
          tab_list_view_->FindCoreTabIdByWebContents(
              remove->contents[0].contents);
      bool active_race = false;
      std::string new_active_tab_id;
      if (selection.active_tab_changed()) {
        new_active_tab_id = get_new_active_tab_id();
        // Active changed but the successor id isn't resolvable yet (no
        // contents / stable id unassigned): fall through to the full refresh.
        active_race = new_active_tab_id.empty();
      }
      if (!active_race &&
          tab_list_view_->TrySuspendFastPath(removed_tab_id,
                                             new_active_tab_id)) {
        if (favorites_view_) {
          UpdateFavorites(state_adapter_.BuildFavoritesModel(browser_));
        }
        refresh_active_tab_chrome();
        LOG(INFO)
            << "[MAHO_PERF] Sidebar::OnTabStripModelChanged END(fast_suspend) "
            << maho_perf_t.Elapsed().InMillisecondsF() << "ms";
        return;
      }
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
  if (applying_pin_) {
    return;
  }
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
      // Favorites are close-protected but NOT strictly pinned. Mirroring a
      // native pin toggle into pin_tab/unpin_tab would silently demote the
      // favorite to Pinned (or Normal), pulling its tile off the grid. Role
      // exits from Favorite go only through change_tab_role (grid remove /
      // drag-off), so skip the mirror for favorites entirely.
      const bool is_favorite_role =
          IsCoreCloseProtectedTab(core_tab_id) && !IsCorePinnedTab(core_tab_id);
      if (is_favorite_role) {
        sync_skipped = true;
      } else {
        DispatchShellEvent(pinned ? "pin_tab" : "unpin_tab",
                           {{"tab_id", core_tab_id}});
      }
    }
  }
  LOG(INFO) << "MahoPinnedDebug OnTabPinnedStateChanged: index=" << index
            << " pinned=" << pinned << " core_tab_id='" << core_tab_id
            << "' sync_skipped=" << sync_skipped;
  if (tab_list_view_ && sync_skipped) {
    if (core_tab_id.empty()) {
      content::WebContents* contents =
          tab_strip_model()->GetWebContentsAt(index);

      // Clean up null weak pointers
      std::erase_if(pending_pin_retries_, [](const auto& ptr) { return !ptr; });

      bool already_pending = false;
      for (const auto& ptr : pending_pin_retries_) {
        if (ptr.get() == contents) {
          already_pending = true;
          break;
        }
      }

      if (contents && !already_pending) {
        pending_pin_retries_.push_back(contents->GetWeakPtr());
        LOG(WARNING) << "MahoSidebarView: Could not resolve core tab_id for "
                     << "strip index " << index << " during pin state change. Scheduling retry.";

        base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(
                [](base::WeakPtr<MahoSidebarView> self,
                   base::WeakPtr<content::WebContents> web_contents) {
                  if (!self) {
                    return;
                  }
                  if (web_contents) {
                    std::erase_if(self->pending_pin_retries_, [&](const auto& ptr) {
                      return !ptr || ptr.get() == web_contents.get();
                    });
                  }
                  if (!self->browser_ || !self->tab_list_view_ || !web_contents) {
                    return;
                  }
                  TabStripModel* model = self->browser_->tab_strip_model();
                  if (!model) {
                    return;
                  }
                  int current_index = model->GetIndexOfWebContents(web_contents.get());
                  if (current_index == TabStripModel::kNoTab) {
                    return;
                  }
                  std::string resolved_id =
                      self->tab_list_view_->FindCoreTabIdByWebContents(web_contents.get());
                  if (resolved_id.empty()) {
                    LOG(WARNING) << "MahoSidebarView: Retry failed to resolve core tab_id for WebContents";
                    return;
                  }
                  const bool pinned = model->IsTabPinned(current_index);
                  const bool is_favorite_role =
                      IsCoreCloseProtectedTab(resolved_id) && !IsCorePinnedTab(resolved_id);
                  if (!is_favorite_role) {
                    DispatchShellEvent(pinned ? "pin_tab" : "unpin_tab",
                                       {{"tab_id", resolved_id}});
                    self->ScheduleRefreshAll();
                  }
                },
                weak_factory_.GetWeakPtr(), contents->GetWeakPtr()),
            base::Milliseconds(150));
      }
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
  cosmetic_refresh_pending_ = false;
  UpdateSpacesPaneActions();
  // Grain is a cached overlay value (not a live ColorProvider token like the
  // sidebar color/opacity), so it must be refreshed per-space here; otherwise
  // it stays at the previously active space's noise level.
  if (grain_overlay_) {
    std::optional<MahoSpaceThemeState::ThemeData> theme_data =
        MahoSpaceThemeState::GetThemeData();
    grain_overlay_->SetTexture(theme_data ? theme_data->texture : 0.0f);
  }
  ScheduleRefreshAll();
}

bool MahoSidebarView::IsTabListScrolling() const {
  constexpr base::TimeDelta kScrollIdleDelay = base::Milliseconds(120);
  return !last_tab_list_scroll_time_.is_null() &&
         (base::TimeTicks::Now() - last_tab_list_scroll_time_) <
             kScrollIdleDelay;
}

void MahoSidebarView::ScheduleCosmeticRefresh() {
  // 16ms debounce: coalesces bursts of title/URL/favicon updates from rapid
  // navigation into a single UpdateTabList() pass at frame boundary.
  constexpr base::TimeDelta kCosmeticDebounce = base::Milliseconds(16);
  if (IsTabListScrolling()) {
    cosmetic_refresh_pending_ = true;
    // Arm the idle timer now that cosmetic work is pending — lazy arming
    // ensures we only pay the timer cost when there is actual work to defer.
    if (!scroll_idle_timer_.IsRunning()) {
      constexpr base::TimeDelta kScrollIdleDelay = base::Milliseconds(120);
      const base::TimeDelta elapsed =
          base::TimeTicks::Now() - last_tab_list_scroll_time_;
      const base::TimeDelta remaining =
          (kScrollIdleDelay - elapsed).is_negative()
              ? base::TimeDelta()
              : kScrollIdleDelay - elapsed;
      scroll_idle_timer_.Start(FROM_HERE, remaining,
                               base::BindOnce(&MahoSidebarView::OnScrollIdle,
                                              weak_factory_.GetWeakPtr()));
    }
    return;
  }
  cosmetic_refresh_pending_ = false;
  cosmetic_refresh_timer_.Start(
      FROM_HERE, kCosmeticDebounce,
      base::BindOnce(&MahoSidebarView::ApplyCosmeticRefresh,
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarView::OnScrollIdle() {
  if (cosmetic_refresh_pending_) {
    ScheduleCosmeticRefresh();
  }
}

void MahoSidebarView::ApplyCosmeticRefresh() {
  cosmetic_refresh_pending_ = false;
  ++cosmetic_apply_count_for_testing_;
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

  const bool was_over = last_over_favorites_;
  const bool now_over =
      gfx::Rect(favorites_view_->size()).Contains(fav_location);

  if (was_over && !now_over) {
    favorites_view_->OnFavoritesZoneExited();
  }
  last_over_favorites_ = now_over;

  if (!now_over) {
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }

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
  // Capture release can lag OnDragExited, so flush any model deferred during
  // capture on the next loop turn rather than synchronously here.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<MahoSidebarView> self) {
                       if (self && self->tab_list_view_) {
                         self->tab_list_view_->FlushDeferredRebuild();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarView::OnDragEnded() {
  last_over_favorites_ = false;
  if (favorites_view_) {
    favorites_view_->OnDragEnded();
  }
  if (tab_list_view_) {
    tab_list_view_->FlushDeferredRebuild();
  }
}

void MahoSidebarView::OnDragStarted(const std::string& tab_id) {
  if (favorites_view_) {
    favorites_view_->OnExternalDragStarted(tab_id);
  }
  last_over_favorites_ = false;
}

void MahoSidebarView::SetActiveDragGhostImage(const gfx::ImageSkia& image,
                                              const gfx::Vector2d& offset) {
  if (favorites_view_) {
    favorites_view_->SetActiveDragGhostImage(image, offset);
  }
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

void MahoSidebarView::SetPrivateAppearance(
    const ui::ImageModel& identity_icon,
    ui::ColorId surface_color_id,
    ui::ColorId text_color_id) {
  // R-11: Presentation-only. No profile/pref reads.
  // Sets private-window background and stores identity icon for top-bar.
  SetBackground(views::CreateRoundedRectBackground(
      surface_color_id,
      gfx::RoundedCornersF(sidebar_layout::kRailCornerRadiusDp, 0, 0,
                           sidebar_layout::kRailCornerRadiusDp)));
  if (top_bar_view_) {
    top_bar_view_->SetPrivateIdentityIcon(identity_icon, text_color_id);
  }
}

}  // namespace maho
