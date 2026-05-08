// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_VIEW_H_

#include <memory>
#include <set>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "base/functional/callback_forward.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/views/view.h"

class Browser;
class TabStripModel;

namespace ui {
class Event;
}  // namespace ui

namespace views {
class ViewTracker;
}  // namespace views

namespace maho {

class MahoSidebarArchiveView;
class MahoSidebarCreateSpaceView;
class MahoSidebarFavoritesGridView;
class MahoSidebarFooterView;
class MahoSidebarSearchView;
class MahoSidebarTabListView;
class MahoSidebarTopBarView;

enum class MahoSidebarBodyMode {
  kTabs,
  kArchive,
  kCreateSpace,
};

class MahoSidebarView : public views::View,
                        public TabStripModelObserver,
                        public MahoSpaceProfileBridge::Observer {
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
  void OnDragExited() override;

  void ShowCreateSpace();
  void ToggleArchiveMode();
  void ExitArchiveModeToTabs();

  bool IsPositionInWindowCaption(const gfx::Point& point) const;

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
  MahoSidebarArchiveView* archive_view_for_testing() { return archive_view_; }
  MahoSidebarLibraryRailView* library_rail_view_for_testing() {
    return library_rail_view_;
  }
  MahoSidebarCreateSpaceView* create_space_view_for_testing() {
    return create_space_view_;
  }
  bool is_create_mode_for_testing() const {
    return current_body_mode_ == MahoSidebarBodyMode::kCreateSpace;
  }
  bool is_archive_mode_for_testing() const {
    return current_body_mode_ == MahoSidebarBodyMode::kArchive;
  }

 private:
  void BuildUi();
  void SetBodyMode(MahoSidebarBodyMode mode);
  void CaptureLastFocusedBodyView();
  void RestoreLastFocusedBodyView();
  bool CanRestoreFocusToView(const views::View* view) const;
  void HandleLibraryCategorySelected(MahoSidebarLibraryRailView::Category category);
  void HandleLibraryBack();
  void HandleCreateSpaceClosed();
  void ScheduleRefreshAll();
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

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> body_host_ = nullptr;
  raw_ptr<views::View> content_host_ = nullptr;
  raw_ptr<views::View> tabs_surface_ = nullptr;
  raw_ptr<views::View> library_container_ = nullptr;
  raw_ptr<views::View> library_divider_ = nullptr;
  raw_ptr<views::View> library_content_host_ = nullptr;
  raw_ptr<views::View> top_region_ = nullptr;
  raw_ptr<MahoSidebarArchiveView> archive_view_ = nullptr;
  raw_ptr<MahoSidebarLibraryRailView> library_rail_view_ = nullptr;
  raw_ptr<MahoSidebarTopBarView> top_bar_view_ = nullptr;
  raw_ptr<MahoSidebarSearchView> search_view_ = nullptr;
  raw_ptr<MahoSidebarFavoritesGridView> favorites_view_ = nullptr;
  raw_ptr<MahoSidebarTabListView> tab_list_view_ = nullptr;
  raw_ptr<MahoSidebarFooterView> footer_view_ = nullptr;
  raw_ptr<MahoSidebarCreateSpaceView> create_space_view_ = nullptr;
  std::unique_ptr<views::ViewTracker> last_focused_body_view_tracker_;
  MahoSidebarStateAdapter state_adapter_;
  MahoSidebarBodyMode current_body_mode_ = MahoSidebarBodyMode::kTabs;

  bool refresh_scheduled_ = false;
  base::WeakPtrFactory<MahoSidebarView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_VIEW_H_
