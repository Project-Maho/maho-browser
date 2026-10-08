// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_VIEW_H_

#include <map>
#include <memory>
#include <set>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/callback_list.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "base/functional/callback_forward.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/translate/content/browser/content_translate_driver.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/downloads/maho_download_bridge_service.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/controls/scroll_view.h"
#include "url/gurl.h"

class Browser;
class TabStripModel;

namespace ui {
class Event;
}  // namespace ui

namespace gfx {
class Canvas;
}  // namespace gfx

namespace content {
class WebContents;
}  // namespace content

namespace views {
class ViewTracker;
}  // namespace views

namespace maho {

class MahoSidebarViewSpaceSwitchSlideTest;

// Body-mode switch for the sidebar (tabs vs library).
enum class MahoSidebarBodyMode {
  kTabs,
  kLibrary,
};

class MahoSidebarArchiveView;
class MahoSidebarCreateSpaceView;
class MahoSidebarDownloadsView;

class MahoSidebarFavoritesGridView;
class MahoSidebarFooterView;
class MahoSidebarGrainOverlayView;
class MahoSidebarLibraryActionPaneView;
class MahoSidebarMediaView;
class MahoSidebarNowPlayingView;
class MahoSidebarScrollBar;
class MahoSidebarTabListView;
class MahoSidebarUpdateNotificationController;
class MahoSidebarUpdateNotificationView;
class MahoSidebarTopBarView;
class MahoSidebarSpacesView;
class MahoSpacesOverlayBoardView;

class MahoSidebarView
    : public views::View,
      public views::ViewObserver,
      public TabStripModelObserver,
      public MahoSpaceProfileBridge::Observer,
      public MahoDownloadBridgeService::Observer,
      public translate::ContentTranslateDriver::TranslationObserver {
  METADATA_HEADER(MahoSidebarView, views::View)
  friend class MahoSidebarViewSpaceSwitchSlideTest;

 public:
  explicit MahoSidebarView(Browser* browser);
  MahoSidebarView(const MahoSidebarView&) = delete;
  MahoSidebarView& operator=(const MahoSidebarView&) = delete;
  ~MahoSidebarView() override;

  void OnThemeChanged() override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void ViewHierarchyChanged(
      const views::ViewHierarchyChangedDetails& details) override;
  void Layout(PassKey) override;

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  void OnDragEnded();
  void OnDragStarted(const std::string& tab_id);
  void SetActiveDragGhostImage(const gfx::ImageSkia& image,
                               const gfx::Vector2d& offset);

  // maho::MahoDownloadBridgeService::Observer:
  // Pushes the aggregated Downloads progress onto the library rail's Downloads
  // icon, so it is current even while the Downloads pane is closed.
  void OnMahoDownloadsChanged() override;

  void OpenLibrary();
  void OpenLibrary(MahoSidebarLibraryRailView::Category category);
  // User-facing entry point (footer button, ToggleArchiveMode): enters library
  // mode and routes the category through the same logic as a rail selection, so
  // kDownloads/kArchivedTabs open the fixed-width overlay identically.
  void OpenLibraryCategory(MahoSidebarLibraryRailView::Category category);
  void ExitLibraryToTabs();
  void RestoreArchivedTabAndActivate(const std::string& tab_id,
                                     const std::string& space_id);

  // Inline native create-space surface. ShowCreateSpace replaces the entire
  // sidebar UI with the create-space form; HideCreateSpace restores the prior
  // sidebar state.
  void ShowCreateSpace();
  void HideCreateSpace();

  // Legacy alias: toggle Library mode (backward compat for tests/footer).
  void ToggleArchiveMode();
  void ExitArchiveModeToTabs();

  bool IsPositionInWindowCaption(const gfx::Point& point) const;

  void ScheduleRefreshAll();
  static void RefreshSpaceThemeForBrowser(Browser* browser);
  // Synchronous, inline variant of RefreshAll for latency-sensitive callers
  // (e.g. a drag drop): rebuilds now instead of posting to base::ThreadPool.
  void RefreshAllSynchronously();
  void RefreshFavoritesSynchronouslyAfterDrop();
  void SetSharedBrowserGlassActive(bool active);
  // T5: Coalesced cosmetic refresh — debounces title/URL/favicon updates so
  // rapid-fire cosmetic events collapse into a single UpdateTabList() pass.
  // Structural changes (create/close/reorder) cancel any pending cosmetic
  // refresh and go through the full ScheduleRefreshAll() path.
  // B2: While the ScrollView is actively scrolling, cosmetic refreshes are
  // held (pending) and applied only after a short scroll-idle interval.
  void ScheduleCosmeticRefresh();
  using SidebarPaletteChangedCallback =
      base::RepeatingCallback<void(const MahoSidebarPalette&)>;
  base::CallbackListSubscription AddSidebarPaletteChangedCallback(
      SidebarPaletteChangedCallback callback);
  const MahoSidebarPalette& sidebar_palette() const {
    return palette_host_.palette();
  }
  // Test-only accessors for live browser tests. Keep minimal.
  MahoSidebarFavoritesGridView* favorites_view_for_testing() {
    return favorites_view_;
  }
  MahoSidebarTabListView* tab_list_view() { return tab_list_view_; }
  MahoSidebarTabListView* tab_list_view_for_testing() {
    return tab_list_view_;
  }
  MahoSidebarFooterView* footer_view_for_testing() { return footer_view_; }
  MahoSidebarTopBarView* top_bar_view_for_testing() { return top_bar_view_; }
  MahoSidebarTopBarView* top_bar_view() { return top_bar_view_; }
  views::View* tabs_surface_for_testing() { return tabs_surface_; }
  views::View* space_header_view_for_testing() { return space_header_view_; }
  views::ScrollView* tab_scroll_view_for_testing() { return tab_scroll_view_; }
  MahoSidebarScrollBar* tab_scroll_bar_for_testing() { return tab_scroll_bar_; }
  MahoSidebarArchiveView* archive_view_for_testing() { return archive_view_; }
  MahoSidebarDownloadsView* downloads_view_for_testing() {
    return downloads_view_;
  }
  MahoSidebarMediaView* media_view_for_testing() { return media_view_; }
  MahoSidebarLibraryRailView* library_rail_view() { return library_rail_view_; }
  base::WeakPtr<MahoSidebarView> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }
  MahoSidebarLibraryRailView* library_rail_view_for_testing() {
    return library_rail_view_;
  }
  bool is_archive_mode_for_testing() const {
    return current_body_mode_ == MahoSidebarBodyMode::kLibrary &&
           active_library_category_ ==
               MahoSidebarLibraryRailView::Category::kArchivedTabs;
  }
  bool IsInLibraryMode() const {
    return current_body_mode_ == MahoSidebarBodyMode::kLibrary;
  }
  MahoSidebarLibraryRailView::Category GetActiveLibraryCategory() const {
    return active_library_category_;
  }
  bool is_library_mode_for_testing() const { return IsInLibraryMode(); }
  MahoSpacesOverlayBoardView* spaces_view_for_testing() {
    return spaces_view_;
  }
  // B2 test-only: observe scroll-idle deferral state.
  bool is_scrolling_for_testing() const { return IsTabListScrolling(); }
  bool cosmetic_refresh_pending_for_testing() const {
    return cosmetic_refresh_pending_;
  }
  int cosmetic_apply_count_for_testing() const {
    return cosmetic_apply_count_for_testing_;
  }
  const MahoSidebarPalette& sidebar_palette_for_testing() const {
    return sidebar_palette();
  }
  int palette_compute_count_for_testing() const {
    return palette_host_.compute_count_for_testing();
  }
  int palette_apply_count_for_testing() const {
    return palette_host_.apply_count_for_testing();
  }
  int palette_repaint_count_for_testing() const {
    return palette_repaint_count_for_testing_;
  }
  bool palette_opaque_for_testing() const {
    return palette_host_.opaque_for_testing();
  }
  float grain_texture_for_testing() const;
  // B2 test-only: fire the scroll-idle transition immediately (bypasses the
  // real timer so tests remain deterministic without wall-clock sleeps).
  void SimulateScrollIdleForTesting() {
    scroll_idle_timer_.Stop();
    last_tab_list_scroll_time_ = base::TimeTicks();
    OnScrollIdle();
  }
  // B2 test-only: stop and immediately fire the pending cosmetic debounce
  // timer. RunUntilIdle() does not advance OneShotTimers, so tests that need
  // to verify apply-count without sleeping must call this after idle.
  void FireCosmeticDebounceForTesting() {
    if (cosmetic_refresh_timer_.IsRunning()) {
      cosmetic_refresh_timer_.Stop();
      ApplyCosmeticRefresh();
    }
  }
  void OnTabChangedAtForTesting(tabs::TabInterface* tab,
                                TabChangeType change_type) {
    OnTabChangedAt(tab, change_type);
  }
  void SetPrivateFlagsForTesting(bool is_private, bool is_otr) {
    is_private_ = is_private;
    is_otr_ = is_otr;
  }
  MahoSidebarLibraryRailView::Category active_library_category_for_testing()
      const;
  using OnBackgroundStateReadyCallbackForTesting =
      base::RepeatingCallback<bool(MahoSidebarView*,
                                   SidebarStateBackgroundResult&)>;
  void SetOnBackgroundStateReadyCallbackForTesting(
      OnBackgroundStateReadyCallbackForTesting callback) {
    background_state_ready_callback_for_testing_ = std::move(callback);
  }
  void OnBackgroundStateReadyForTesting(SidebarStateBackgroundResult result) {
    OnBackgroundStateReady(std::move(result));
  }

  void ActivateTabById(const std::string& tab_id);
  static bool ActivateSpaceAndTab(Browser* browser, const std::string& space_id);

  bool ActivateFavoriteByIndex(size_t index);
  MahoSidebarNowPlayingView* AddNowPlayingView(std::unique_ptr<MahoSidebarNowPlayingView> view);

  // R-11: Called by MahoSidebarContainerView::SetPrivateAppearance to
  // propagate private-window visual identity into the sidebar surface.
  // No profile/pref reads; purely presentation.
  void SetPrivateAppearance(const ui::ImageModel& identity_icon,
                            ui::ColorId surface_color_id,
                            ui::ColorId text_color_id);

  void SetApplyingPin(bool applying) { applying_pin_ = applying; }
  bool IsApplyingPin() const { return applying_pin_; }



  // Repaints the sidebar's themed background: opaque while a fixed-width
  // overlay (Downloads/Archive library, or the Spaces full-viewport overlay)
  // is active so the docked rail matches the opaque overlay; otherwise the
  // translucent glass-frame look. Public so overlay dismiss paths can restore
  // the translucent look when their overlay closes.
  void ApplySidebarBackground();
  void ApplyViewState(MahoSidebarViewStateModel model);
  bool ShouldAnimateSpaceSwitch(const MahoSidebarViewStateModel& model,
                                int* out_dir) const;
  void StartSpaceSlide(int dir);
  void FinishSpaceSlide();
  bool space_switch_anim_in_progress_for_testing() const {
    return space_switch_anim_in_progress_;
  }
  ui::LayerTreeOwner* outgoing_space_layer_owner_for_testing() const {
    return outgoing_space_layer_owner_.get();
  }
  const std::string& last_rendered_active_space_id_for_testing() const {
    return last_rendered_active_space_id_;
  }

 private:
  void RefreshSpaceTheme();
  void BuildUi();
  void UpdateSidebarPalette(MahoSidebarPaletteUpdateReason reason);
  void ApplyPaletteSnapshot(const MahoSidebarPalette& palette);
  void OnTabListScrolled();
  void UpdateSidebarBorderForOverlay();
  // Returns true when the docked sidebar must paint an opaque themed background
  // so the rail matches a fully-opaque overlay (Downloads/Archive library, or
  // the Spaces full-viewport overlay); false keeps the translucent glass look.
  bool ShouldUseOpaqueSidebarBackground() const;
  void SetBodyMode(MahoSidebarBodyMode mode);
  void CaptureLastFocusedBodyView();
  void RestoreLastFocusedBodyView();
  bool CanRestoreFocusToView(const views::View* view) const;
  void HandleLibraryCategorySelected(
      MahoSidebarLibraryRailView::Category category);
  void HandleLibraryBack();
  views::View* GetOrCreateLibraryPane(
      MahoSidebarLibraryRailView::Category category);
  void ShowLibraryPane(MahoSidebarLibraryRailView::Category category);
  views::View* ActiveLibraryPane() const;
  void UpdateSpacesPaneActions();
  void OpenSpaceCreateSurface();
  void OpenSpaceConfigSurface();

  void RefreshAll();
  // R-11: Rebuilds the OTR sidebar synchronously from the window-local
  // TabStripModel only. Reads no MahoCore, Space bridge, favorites, folders,
  // archive, library, or footer state. Used for both initial population and
  // every tab-strip change while |is_otr_| is true.
  void RefreshPrivateSidebar();
  // Applies the coalesced cosmetic refresh when |cosmetic_refresh_timer_|
  // fires. Structural changes cancel the timer before this runs, preventing
  // stale partial updates after a tab strip structural change.
  void ApplyCosmeticRefresh();
  // B2: Called when |scroll_idle_timer_| fires (or by test hook). Schedules
  // the cosmetic debounce if work is pending.
  void OnScrollIdle();
  // B2: Returns true if a tab-list scroll event arrived within |kScrollIdleDelay|.
  bool IsTabListScrolling() const;

  void UpdateTopBar(const MahoSidebarTopBarModel& model);
  void UpdateFavorites(const MahoSidebarFavoritesModel& model);
  void UpdateTabList(MahoSidebarTabListModel model);
  void UpdateFooter(const MahoSidebarFooterModel& model);
  void PositionUpdateNotification();
  void PositionNowPlayingView();

  // views::ViewObserver:
  void OnViewPreferredSizeChanged(views::View* observed_view) override;
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewVisibilityChanged(views::View* observed_view,
                               views::View* starting_view,
                               bool visible) override;

  void OpenWebUIInNewTab(const GURL& url);

  TabStripModel* tab_strip_model() const;

  void OnSpaceProfileBridgeChanged(bool is_structural) override;
  void OnSpaceProfileBridgeChanged() override;

  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnSplitTabChanged(const SplitTabChange& change) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void OnTabGroupChanged(const TabGroupChange& change) override;

  void OnTranslateEnabledChanged(content::WebContents* source) override;
  void OnIsPageTranslatedChanged(content::WebContents* source) override;

  void RefreshTranslateObservation();

  raw_ptr<Browser> browser_;
  // R-11: True for any off-the-record profile. Gates every MahoCore/Space read
  // out of the sidebar so an OTR window rebuilds only from its own TabStripModel.
  bool is_otr_ = false;
  // R-12: True once SetPrivateAppearance() runs for an exact primary Incognito
  // window. Gates the fixed Incognito identity header and keeps favorites,
  // footer switcher, and pinned section suppressed against model refreshes.
  bool is_private_ = false;
  raw_ptr<views::View> body_host_ = nullptr;
  raw_ptr<views::View> content_host_ = nullptr;
  raw_ptr<views::View> tabs_surface_ = nullptr;
  raw_ptr<views::View> library_container_ = nullptr;
  raw_ptr<views::View> library_divider_ = nullptr;
  raw_ptr<views::View> library_content_host_ = nullptr;
  raw_ptr<views::View> top_region_ = nullptr;
  raw_ptr<MahoSidebarArchiveView> archive_view_ = nullptr;
  raw_ptr<MahoSidebarDownloadsView> downloads_view_ = nullptr;
  raw_ptr<MahoSidebarMediaView> media_view_ = nullptr;
  raw_ptr<MahoSpacesOverlayBoardView> spaces_view_ = nullptr;
  raw_ptr<MahoSidebarLibraryRailView> library_rail_view_ = nullptr;
  raw_ptr<MahoSidebarTopBarView> top_bar_view_ = nullptr;
  raw_ptr<MahoSidebarFavoritesGridView> favorites_view_ = nullptr;
  raw_ptr<MahoSidebarTabListView> tab_list_view_ = nullptr;
  raw_ptr<views::View> space_header_view_ = nullptr;
  raw_ptr<views::ScrollView> tab_scroll_view_ = nullptr;
  raw_ptr<MahoSidebarScrollBar> tab_scroll_bar_ = nullptr;
  base::CallbackListSubscription tab_scroll_subscription_;
  raw_ptr<MahoSidebarFooterView> footer_view_ = nullptr;
  raw_ptr<MahoSidebarNowPlayingView> now_playing_view_ = nullptr;
  std::unique_ptr<MahoSidebarUpdateNotificationController>
      update_notification_controller_;
  raw_ptr<MahoSidebarUpdateNotificationView> update_notification_view_ =
      nullptr;
  raw_ptr<MahoSidebarCreateSpaceView> create_space_view_ = nullptr;
  raw_ptr<MahoSidebarGrainOverlayView> grain_overlay_ = nullptr;
  MahoSidebarPaletteHost palette_host_;
  base::RepeatingCallbackList<void(const MahoSidebarPalette&)>
      palette_changed_callbacks_;
  base::CallbackListSubscription library_rail_palette_subscription_;
  base::CallbackListSubscription top_bar_palette_subscription_;
  base::CallbackListSubscription favorites_palette_subscription_;
  base::CallbackListSubscription footer_palette_subscription_;
  base::CallbackListSubscription create_space_palette_subscription_;
  base::CallbackListSubscription archive_palette_subscription_;
  base::CallbackListSubscription downloads_palette_subscription_;
  base::CallbackListSubscription media_palette_subscription_;
  base::CallbackListSubscription now_playing_palette_subscription_;
  base::CallbackListSubscription update_notification_palette_subscription_;
  // Library rail Downloads indicator source. Registered only for a regular,
  // capability-allowed profile; see the constructor and OnMahoDownloadsChanged.
  base::ScopedObservation<MahoDownloadBridgeService,
                          MahoDownloadBridgeService::Observer>
      downloads_observation_{this};
  int palette_repaint_count_for_testing_ = 0;
  bool shared_browser_glass_active_ = false;

  std::unique_ptr<views::ViewTracker> last_focused_body_view_tracker_;
  MahoSidebarStateAdapter state_adapter_;
  MahoSidebarBodyMode current_body_mode_ = MahoSidebarBodyMode::kTabs;
  MahoSidebarLibraryRailView::Category active_library_category_ =
      MahoSidebarLibraryRailView::Category::kArchivedTabs;
  std::map<MahoSidebarLibraryRailView::Category, raw_ptr<views::View>>
      library_pane_cache_;

  // The favorites+state build is a single in-flight unit: kIdle means no
  // background build is outstanding, kInFlight means one is running. Requests
  // arriving while kInFlight set |refresh_dirty_| so exactly one follow-up
  // refresh runs after the current build completes.
  enum class RefreshState { kIdle, kInFlight };
  RefreshState refresh_state_ = RefreshState::kIdle;
  bool refresh_dirty_ = false;
  base::OneShotTimer cosmetic_refresh_timer_;
  // B2: Scroll-idle deferral state. |scroll_idle_timer_| is lazily armed only
  // when cosmetic work is pending; IsTabListScrolling() gates cosmetic work via
  // |last_tab_list_scroll_time_|; |cosmetic_refresh_pending_| tracks held work;
  // |cosmetic_apply_count_for_testing_| is a monotonic counter for test
  // assertions.
  base::OneShotTimer scroll_idle_timer_;
  // B2: Timestamp of last OnTabListScrolled() call. IsTabListScrolling()
  // returns true when this is within kScrollIdleDelay of now.
  base::TimeTicks last_tab_list_scroll_time_;
  bool cosmetic_refresh_pending_ = false;
  int cosmetic_apply_count_for_testing_ = 0;
  std::set<content::WebContents*> recently_registered_contents_;
  base::ScopedObservation<translate::ContentTranslateDriver,
                         translate::ContentTranslateDriver::TranslationObserver>
      translate_observation_{this};

  void OnBackgroundStateReady(SidebarStateBackgroundResult result);

  bool last_over_favorites_ = false;
  bool applying_pin_ = false;
  std::vector<base::WeakPtr<content::WebContents>> pending_pin_retries_;
  OnBackgroundStateReadyCallbackForTesting
      background_state_ready_callback_for_testing_;

  std::unique_ptr<ui::LayerTreeOwner> outgoing_space_layer_owner_;
  std::string last_rendered_active_space_id_;
  bool space_switch_anim_in_progress_ = false;

  base::WeakPtrFactory<MahoSidebarView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_VIEW_H_
