// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TAB_LIST_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TAB_LIST_VIEW_H_

#include <memory>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/tabs/public/tab_interface.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/point.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/drag_controller.h"
#include "ui/views/view.h"

class Browser;
#include "maho/browser/ui/context_menu/maho_folder_context_menu.h"
#include "maho/browser/ui/context_menu/maho_tab_context_menu.h"

namespace ui {
class Event;
}

namespace views {
class ImageButton;
class ImageView;
class LabelButton;
class MenuRunner;
class ScrollView;
class Textfield;
class View;
class Widget;
}  // namespace views

namespace content {
class WebContents;
}

namespace maho {

class SidebarTabRowView;
class SidebarFolderRowView;
class MahoTabPreviewController;
class MahoSidebarFolderHoverController;

const ui::ClipboardFormatType& GetMahoDragFormatType();

std::string SerializeDragPayload(const SidebarDragPayload& payload);
bool DeserializeDragPayload(const std::string& data, SidebarDragPayload& out);

void WriteMahoDragData(const SidebarDragPayload& payload,
                       ui::OSExchangeData* data);
bool ReadMahoDragData(const ui::OSExchangeData& data,
                      SidebarDragPayload& out);

std::unique_ptr<views::View> CreateSidebarSpaceHeaderRow();
void UpdateSidebarSpaceHeaderRow(views::View* row,
                                 const std::string& icon,
                                 const std::u16string& name);

class MahoSidebarTabListView : public views::View,
                               public views::ContextMenuController {
  METADATA_HEADER(MahoSidebarTabListView, views::View)

 public:
  explicit MahoSidebarTabListView(Browser* browser);
  MahoSidebarTabListView(const MahoSidebarTabListView&) = delete;
  MahoSidebarTabListView& operator=(const MahoSidebarTabListView&) = delete;
  ~MahoSidebarTabListView() override;

  void Update(const MahoSidebarTabListModel& model, Browser* browser);
  void SetLastFavoritesModel(const MahoSidebarFavoritesModel& model);

  // Sets a callback that section drop targets will invoke when a favorites-
  // origin tab is successfully dropped on a section container.  The callback
  // receives the tab_id of the dropped tab and should call
  // MahoSidebarFavoritesGridView::MarkDropAccepted to suppress the duplicate
  // unfavorite_tab that OnTileDragDone would otherwise fire.
  void SetFavoritesDropAcceptedCallback(
      base::RepeatingCallback<void(const std::string&)> callback);

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;

  void SetAutoScrollScrollView(views::ScrollView* scroll_view);

  void OnAutoScrollDragOver(const gfx::Point& screen_point);
  void OnAutoScrollDragEnded();

  views::View* FindSectionDropTargetForTesting(MahoSidebarTabSection section);
  SidebarTabRowView* FindTabRowByIdForTesting(const std::string& tab_id);
  SidebarTabRowView* FindTabRowByTabIndexForTesting(int tab_index);
  SidebarTabRowView* FindActiveTabRowForTesting();
  SidebarTabRowView* FindFirstInactiveTabRowForTesting();
  SidebarFolderRowView* FindFolderRowByIdForTesting(
      const std::string& folder_id);
  views::View* FindInsertionLaneBeforeTabByIdForTesting(
      const std::string& tab_id);
  views::View* FindInsertionLaneBeforeFolderByIdForTesting(
      const std::string& folder_id);
  views::LabelButton* new_tab_button_for_testing() { return new_tab_button_; }
  views::View* tab_rows_for_testing() { return tab_rows_; }
  views::View* pinned_separator_for_testing() { return pinned_separator_; }

  bool IsTabPreviewShowingForTesting() const;
  views::Widget* tab_preview_widget_for_testing() const;
  const std::u16string& tab_preview_title_for_testing() const;
  const std::u16string& tab_preview_url_for_testing() const;
  void SetTabPreviewShowDelayForTesting(base::TimeDelta delay);
  bool is_pinned_drop_lane_revealed_for_testing() const {
    return pinned_drop_lane_revealed_;
  }
  // Returns the maho-core tab_id for the given WebContents, or empty string.
  std::string FindCoreTabIdByWebContents(content::WebContents* contents) const;

  void ShowTabRowContextMenu(views::View* source,
                             int live_index,
                             const gfx::Point& point,
                             ui::mojom::MenuSourceType source_type,
                             MahoTabContextMenu::Delegate* delegate = nullptr);

  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source_type) override;

 private:
  void RebuildRows(const MahoSidebarTabListModel& model, Browser* browser);
  void RevealEmptyPinnedDropLane();
  void HideEmptyPinnedDropLane();
  void UpdatePinnedDropLaneVisibility(bool visible);
  void RebuildTreeSection(views::View* parent,
                          const std::vector<SidebarTreeNode>& nodes,
                          MahoSidebarTabSection section,
                          Browser* browser);
  void RebuildTreeNodes(views::View* parent,
                        const std::vector<SidebarTreeNode>& nodes,
                        MahoSidebarTabSection section,
                        Browser* browser,
                        const std::string& parent_folder_id);
  std::u16string GetNodeDisplayText(const SidebarTreeNode& node) const;
  std::u16string GetNodeTooltipText(const SidebarTreeNode& node) const;
  void ActivateTab(int tab_index);
  void ActivateTabById(const std::string& tab_id);
  void WakeSuspendedTab(const std::string& tab_id);
  void CloseTab(int tab_index);
  void CloseTabById(const std::string& tab_id);
  int ResolveTabStripIndexForTabId(const std::string& tab_id) const;
  void OpenNewTab(const ui::Event& event);
  void ToggleFolderExpanded(const std::string& folder_id);
  void CommitTabTitleChange(const std::string& tab_id,
                            const std::u16string& new_title);
  void CommitFolderNameChange(const std::string& folder_id,
                              const std::u16string& new_name);
  std::u16string ResolveTabDisplayText(const SidebarTreeNode& node) const;
  void PruneLocalTitleCache(const MahoSidebarTabListModel& model);
  void HandleTabRowHoverStart(views::View* anchor_view, int tab_index);
  void HandleTabRowHoverEnd();

  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<views::LabelButton> new_tab_button_ = nullptr;
  raw_ptr<views::View> tab_rows_ = nullptr;
  raw_ptr<views::View> pinned_section_target_ = nullptr;
  raw_ptr<views::View> normal_section_target_ = nullptr;
  std::string active_space_id_;
  base::RepeatingCallback<void(const std::string&)> favorites_drop_accepted_callback_;
  std::map<std::string, std::u16string> local_custom_titles_;
  MahoSidebarTabListModel last_model_;
  MahoSidebarFavoritesModel last_favorites_model_;
  bool has_last_model_ = false;
  bool pinned_drop_lane_revealed_ = false;
  raw_ptr<views::View> pinned_separator_ = nullptr;

  std::unique_ptr<MahoTabContextMenu> active_context_menu_;
  std::unique_ptr<ui::SimpleMenuModel> active_menu_model_;
  std::unique_ptr<views::MenuRunner> active_menu_runner_;
  std::unique_ptr<MahoTabPreviewController> tab_preview_controller_;
  std::unique_ptr<MahoSidebarFolderHoverController> folder_hover_controller_;

  class SidebarAutoScroller;
  std::unique_ptr<SidebarAutoScroller> auto_scroller_;

  base::WeakPtrFactory<MahoSidebarTabListView> weak_factory_{this};
};

class SidebarTabRowView : public views::View,
                           public views::DragController,
                           public views::ContextMenuController,
                           public views::TextfieldController,
                           public MahoTabContextMenu::Delegate {
  METADATA_HEADER(SidebarTabRowView, views::View)

 public:
  SidebarTabRowView(const SidebarTreeNode& node,
                    ui::ImageModel favicon,
                     Browser* browser,
                     MahoSidebarTabSection section,
                     std::string active_space_id,
                     std::u16string display_title,
                     views::Button::PressedCallback activate_callback,
                     views::Button::PressedCallback close_callback,
                     base::RepeatingCallback<void(const std::string&, const std::u16string&)>
                          rename_callback,
                     base::RepeatingCallback<int(const std::string&)>
                         resolve_tab_index,
                     base::RepeatingCallback<void(views::View*, int)>
                         preview_show_callback,
                     base::RepeatingClosure preview_hide_callback);
  SidebarTabRowView(const SidebarTabRowView&) = delete;
  SidebarTabRowView& operator=(const SidebarTabRowView&) = delete;
  ~SidebarTabRowView() override;

  void OnThemeChanged() override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source_type) override;

  // MahoTabContextMenu::Delegate:
  void RenameTab(int tab_index) override;

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  void OnDragDone() override;

  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& current_pt) override;

  void SetFavoritesDropAcceptedCallback(
      base::RepeatingCallback<void(const std::string&)> callback) {
    favorites_drop_accepted_callback_ = std::move(callback);
  }

  void SetRevealEmptyPinnedDropLaneCallback(
      base::RepeatingClosure callback) {
    reveal_empty_pinned_drop_lane_callback_ = std::move(callback);
  }

  views::View::DropCallback GetDropCallbackForTesting(
      const ui::DropTargetEvent& event) {
    return GetDropCallback(event);
  }

  bool is_drop_indicator_visible_for_testing() const {
    return drag_over_drop_target_;
  }

  enum class SidebarTabDropZone {
    kNone,
    kBefore,
    kAfter,
    kSplit,
  };

  SidebarTabDropZone drop_zone_for_testing() const {
    return active_drop_zone_;
  }

  bool is_active_for_testing() const { return active_; }
  const std::string& tab_id_for_testing() const { return tab_id_; }
  int tab_index_for_testing() const { return tab_index_; }
  int live_tab_index_for_testing() const;
  MahoSidebarTabSection section_for_testing() const { return section_; }
  views::ImageButton* close_button_for_testing() { return close_button_; }
  views::Textfield* title_field_for_testing() { return title_field_; }
  views::LabelButton* title_button_for_testing() { return title_button_; }

  void SetSuspended(bool suspended);
  bool is_suspended() const { return is_suspended_; }

 private:
  void ActivateTabFromEvent(const ui::Event& event);
  void ActivateFromRowClick(const ui::MouseEvent& event);
  SidebarTabDropZone ResolveDropZone(const ui::DropTargetEvent& event,
                                     const SidebarDragPayload& payload) const;
  void BeginTitleEditing();
  void EndTitleEditing(bool commit);
  void ApplyDisplayedTitle(const std::u16string& title);
  void UpdateAppearance();

  const bool active_;
  const bool pinned_;
  const bool audible_;
  const bool muted_;
  const std::string tab_id_;
  const MahoSidebarTabSection section_;
  const std::string space_id_;
  const std::string source_parent_folder_id_;
  const int folder_child_index_;
  raw_ptr<Browser> browser_ = nullptr;
  int tab_index_ = -1;
  std::u16string committed_title_;
  bool is_editing_title_ = false;
  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  raw_ptr<views::ImageView> audio_indicator_view_ = nullptr;
  raw_ptr<views::LabelButton> title_button_ = nullptr;
  raw_ptr<views::Textfield> title_field_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  base::RepeatingCallback<void(const std::string&, const std::u16string&)> rename_callback_;
  base::RepeatingCallback<int(const std::string&)> resolve_tab_index_;
  base::RepeatingCallback<void(views::View*, int)> preview_show_callback_;
  base::RepeatingClosure preview_hide_callback_;
  views::Button::PressedCallback activate_callback_;
  base::RepeatingCallback<void(const std::string&)>
      favorites_drop_accepted_callback_;
  base::RepeatingClosure reveal_empty_pinned_drop_lane_callback_;
  bool drag_over_drop_target_ = false;
  SidebarTabDropZone active_drop_zone_ = SidebarTabDropZone::kNone;
  bool activate_pending_ = false;
  bool activation_handled_for_current_press_ = false;
  bool is_suspended_ = false;
};

class SidebarFolderRowView : public views::View,
                             public views::DragController,
                             public views::ContextMenuController,
                             public views::TextfieldController {
  METADATA_HEADER(SidebarFolderRowView, views::View)

 public:
  SidebarFolderRowView(const SidebarTreeNode& node,
                       MahoSidebarTabSection section,
                       std::string active_space_id,
                       Browser* browser,
                       views::Button::PressedCallback toggle_callback,
                       base::RepeatingCallback<void(const std::string&,
                                                    const std::u16string&)>
                           rename_callback,
                       std::string next_sibling_folder_id = std::string(),
                       std::string parent_folder_id = std::string());
  SidebarFolderRowView(const SidebarFolderRowView&) = delete;
  SidebarFolderRowView& operator=(const SidebarFolderRowView&) = delete;
  ~SidebarFolderRowView() override;

  void OnThemeChanged() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  void OnDragDone() override;

  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& current_pt) override;

  void BeginFolderEditing();

  void SetFavoritesDropAcceptedCallback(
      base::RepeatingCallback<void(const std::string&)> callback) {
    favorites_drop_accepted_callback_ = std::move(callback);
  }

  void SetRevealEmptyPinnedDropLaneCallback(
      base::RepeatingClosure callback) {
    reveal_empty_pinned_drop_lane_callback_ = std::move(callback);
  }

  void SetHoverPopupCallbacks(base::RepeatingClosure show, base::RepeatingClosure hide) {
    show_popup_callback_ = std::move(show);
    hide_popup_callback_ = std::move(hide);
  }

  void SetSpringExpandCallback(base::RepeatingClosure callback) {
    spring_expand_callback_ = std::move(callback);
  }

  std::vector<SidebarTreeNode> children_snapshot_;

  bool is_drop_indicator_visible_for_testing() const {
    return drag_over_drop_target_;
  }

  bool is_editing_for_testing() const { return is_editing_name_; }
  views::Textfield* name_field_for_testing() { return name_field_; }

  enum class SidebarFolderDropZone {
    kNone,
    kBefore,
    kInto,
    kAfter,
  };

  SidebarFolderDropZone drop_zone_for_testing() const {
    return active_drop_zone_;
  }

  const std::string& folder_id_for_testing() const { return folder_id_; }

 private:
  SidebarFolderDropZone ResolveDropZone(const ui::DropTargetEvent& event,
                                         const SidebarDragPayload& payload) const;
  void EndFolderEditing(bool commit);
  void UpdateAppearance();

  const std::string folder_id_;
  const std::string space_id_;
  const bool folder_is_pinned_;
  const std::string next_sibling_folder_id_;
  const std::string parent_folder_id_;
  const bool is_expanded_;
  raw_ptr<Browser> browser_ = nullptr;
  base::RepeatingCallback<void(const std::string&, const std::u16string&)>
      rename_callback_;
  base::RepeatingCallback<void(const std::string&)>
      favorites_drop_accepted_callback_;
  base::RepeatingClosure reveal_empty_pinned_drop_lane_callback_;
  base::RepeatingClosure show_popup_callback_;
  base::RepeatingClosure hide_popup_callback_;
  base::RepeatingClosure spring_expand_callback_;
  base::OneShotTimer spring_load_timer_;
  std::u16string committed_name_;
  bool is_editing_name_ = false;
  raw_ptr<views::ImageView> chevron_ = nullptr;
  raw_ptr<views::ImageView> folder_icon_ = nullptr;
  raw_ptr<views::LabelButton> label_button_ = nullptr;
  raw_ptr<views::Textfield> name_field_ = nullptr;
  bool drag_over_drop_target_ = false;
  SidebarFolderDropZone active_drop_zone_ = SidebarFolderDropZone::kNone;
  std::unique_ptr<MahoFolderContextMenu> active_folder_context_menu_;
  std::unique_ptr<ui::SimpleMenuModel> active_folder_menu_model_;
  std::unique_ptr<views::MenuRunner> active_folder_menu_runner_;
  base::WeakPtrFactory<SidebarFolderRowView> weak_factory_{this};
};

struct SidebarSectionDropIndicatorStateForTesting {
  bool visible_after_update = false;
  bool visible_after_exit = false;
};

views::View::DropCallback GetSectionDropCallbackForTesting(
    MahoSidebarTabSection section,
    const ui::DropTargetEvent& event,
    bool populated = false);
SidebarSectionDropIndicatorStateForTesting
GetSectionDropIndicatorStateForTesting(MahoSidebarTabSection section,
                                       const ui::DropTargetEvent& event,
                                       bool populated = false);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TAB_LIST_VIEW_H_
