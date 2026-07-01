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
#include "base/values.h"
#include "base/functional/callback_forward.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/translate/content/browser/content_translate_driver.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/controls/scroll_view.h"
#include "url/gurl.h"

class Browser;
class TabStripModel;

namespace ui {
class Event;
}  // namespace ui

namespace content {
class WebContents;
}  // namespace content

namespace views {
class ViewTracker;
}  // namespace views

namespace maho {

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
class MahoSidebarLibraryActionPaneView;
class MahoSidebarMediaView;
class MahoSidebarSearchView;
class MahoSidebarTabListView;
class MahoSidebarUpdateNotificationController;
class MahoSidebarUpdateNotificationView;
class MahoSidebarTopBarView;
class MahoSidebarNotesView;
class MahoSidebarEaselsView;
class MahoSidebarSpacesView;

class MahoSidebarView
    : public views::View,
      public views::ViewObserver,
      public TabStripModelObserver,
      public MahoSpaceProfileBridge::Observer,
      public translate::ContentTranslateDriver::TranslationObserver {
  METADATA_HEADER(MahoSidebarView, views::View)

 public:
  explicit MahoSidebarView(Browser* browser);
  MahoSidebarView(const MahoSidebarView&) = delete;
  MahoSidebarView& operator=(const MahoSidebarView&) = delete;
  ~MahoSidebarView() override;

  void OnThemeChanged() override;

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  void OnDragEnded();

  void OpenLibrary();
  void OpenLibrary(MahoSidebarLibraryRailView::Category category);
  void ExitLibraryToTabs();

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
  // Test-only accessors for live browser tests. Keep minimal.
  MahoSidebarFavoritesGridView* favorites_view_for_testing() {
    return favorites_view_;
  }
  MahoSidebarTabListView* tab_list_view_for_testing() {
    return tab_list_view_;
  }
  MahoSidebarFooterView* footer_view_for_testing() { return footer_view_; }
  MahoSidebarSearchView* search_view_for_testing() { return search_view_; }
  MahoSidebarTopBarView* top_bar_view_for_testing() { return top_bar_view_; }
  views::View* tabs_surface_for_testing() { return tabs_surface_; }
  views::View* space_header_view_for_testing() { return space_header_view_; }
  views::ScrollView* tab_scroll_view_for_testing() { return tab_scroll_view_; }
  MahoSidebarArchiveView* archive_view_for_testing() { return archive_view_; }
  MahoSidebarDownloadsView* downloads_view_for_testing() {
    return downloads_view_;
  }
  MahoSidebarMediaView* media_view_for_testing() { return media_view_; }
  MahoSidebarLibraryRailView* library_rail_view_for_testing() {
    return library_rail_view_;
  }
  bool is_archive_mode_for_testing() const {
    return current_body_mode_ == MahoSidebarBodyMode::kLibrary &&
           active_library_category_ ==
               MahoSidebarLibraryRailView::Category::kArchivedTabs;
  }
  bool is_library_mode_for_testing() const {
    return current_body_mode_ == MahoSidebarBodyMode::kLibrary;
  }
  MahoSidebarSpacesView* spaces_view_for_testing() {
    return spaces_view_;
  }
  MahoSidebarEaselsView* easel_view_for_testing() {
    return easel_view_;
  }
  MahoSidebarNotesView* notes_view_for_testing() {
    return notes_view_;
  }
  MahoSidebarLibraryRailView::Category active_library_category_for_testing()
      const;

 private:
  void BuildUi();
  void OnTabListScrolled();
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

  void RefreshFavoritesAndViewState();
  static MahoSidebarFavoritesModel BuildFavoritesModelOnBackground(
      const std::string& active_space_id_json);
  void OnFavoritesModelReady(MahoSidebarFavoritesModel favorites_model);
  void RefreshAll();
  void ApplyViewState(const MahoSidebarViewStateModel& model);
  void UpdateTopBar(const MahoSidebarTopBarModel& model);
  void UpdateSearch(const MahoSidebarSearchModel& model);
  void UpdateFavorites(const MahoSidebarFavoritesModel& model);
  void UpdateTabList(const MahoSidebarTabListModel& model);
  void UpdateFooter(const MahoSidebarFooterModel& model);
  void PositionUpdateNotification();

  // views::ViewObserver:
  void OnViewPreferredSizeChanged(views::View* observed_view) override;
  void OnViewBoundsChanged(views::View* observed_view) override;

  void OpenWebUIInNewTab(const GURL& url);

  TabStripModel* tab_strip_model() const;

  void OnSpaceProfileBridgeChanged() override;

  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      int index,
                      TabChangeType change_type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void OnTabGroupChanged(const TabGroupChange& change) override;

  void OnTranslateEnabledChanged(content::WebContents* source) override;
  void OnIsPageTranslatedChanged(content::WebContents* source) override;

  void RefreshTranslateObservation();

  raw_ptr<Browser> browser_;
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
  raw_ptr<MahoSidebarSpacesView> spaces_view_ = nullptr;
  raw_ptr<MahoSidebarEaselsView> easel_view_ = nullptr;
  raw_ptr<MahoSidebarNotesView> notes_view_ = nullptr;
  raw_ptr<MahoSidebarLibraryRailView> library_rail_view_ = nullptr;
  raw_ptr<MahoSidebarTopBarView> top_bar_view_ = nullptr;
  raw_ptr<MahoSidebarSearchView> search_view_ = nullptr;
  raw_ptr<MahoSidebarFavoritesGridView> favorites_view_ = nullptr;
  raw_ptr<MahoSidebarTabListView> tab_list_view_ = nullptr;
  raw_ptr<views::View> space_header_view_ = nullptr;
  raw_ptr<views::ScrollView> tab_scroll_view_ = nullptr;
  base::CallbackListSubscription tab_scroll_subscription_;
  raw_ptr<MahoSidebarFooterView> footer_view_ = nullptr;
  std::unique_ptr<MahoSidebarUpdateNotificationController>
      update_notification_controller_;
  raw_ptr<MahoSidebarUpdateNotificationView> update_notification_view_ =
      nullptr;
  raw_ptr<MahoSidebarCreateSpaceView> create_space_view_ = nullptr;
  std::unique_ptr<views::ViewTracker> last_focused_body_view_tracker_;
  MahoSidebarStateAdapter state_adapter_;
  MahoSidebarBodyMode current_body_mode_ = MahoSidebarBodyMode::kTabs;
  MahoSidebarLibraryRailView::Category active_library_category_ =
      MahoSidebarLibraryRailView::Category::kArchivedTabs;
  std::map<MahoSidebarLibraryRailView::Category, raw_ptr<views::View>>
      library_pane_cache_;

  bool refresh_scheduled_ = false;
  bool needs_re_refresh_ = false;
  std::set<content::WebContents*> recently_registered_contents_;
  base::ScopedObservation<translate::ContentTranslateDriver,
                         translate::ContentTranslateDriver::TranslationObserver>
      translate_observation_{this};
  base::WeakPtrFactory<MahoSidebarView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_VIEW_H_
