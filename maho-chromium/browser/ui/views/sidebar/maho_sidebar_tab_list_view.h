// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TAB_LIST_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TAB_LIST_VIEW_H_

#include <memory>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
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
#include "url/gurl.h"

class Browser;
class MahoTabContextMenu;

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
}  // namespace views

namespace content {
class WebContents;
}

namespace maho {

class SidebarTabRowView;
class SidebarFolderRowView;

const ui::ClipboardFormatType& GetMahoDragFormatType();

std::string SerializeDragPayload(const SidebarDragPayload& payload);
bool DeserializeDragPayload(const std::string& data, SidebarDragPayload& out);

void WriteMahoDragData(const SidebarDragPayload& payload,
                       ui::OSExchangeData* data);
bool ReadMahoDragData(const ui::OSExchangeData& data,
                      SidebarDragPayload& out);

enum class SidebarDropIntent {
  kNone,
  kTabToFolder,
  kTabToPinnedSection,
  kTabToNormalSection,
  kFolderToFolder,
  kFolderToPinnedSection,
  kFolderToNormalSection,
  kExternalUrlToTab,
  kExternalUrlToSection,
};

class MahoSidebarTabListView : public views::View {
  METADATA_HEADER(MahoSidebarTabListView, views::View)

 public:
  explicit MahoSidebarTabListView(Browser* browser);
  MahoSidebarTabListView(const MahoSidebarTabListView&) = delete;
  MahoSidebarTabListView& operator=(const MahoSidebarTabListView&) = delete;
  ~MahoSidebarTabListView() override;

  void Update(const MahoSidebarTabListModel& model, Browser* browser);

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;

  views::View* FindSectionDropTargetForTesting(MahoSidebarTabSection section);
  SidebarTabRowView* FindTabRowByIdForTesting(const std::string& tab_id);
  SidebarFolderRowView* FindFolderRowByIdForTesting(
      const std::string& folder_id);
  views::View* FindInsertionLaneBeforeTabByIdForTesting(
      const std::string& tab_id);
  views::View* FindInsertionLaneBeforeFolderByIdForTesting(
      const std::string& folder_id);
  views::LabelButton* new_tab_button_for_testing() { return new_tab_button_; }
  views::View* tab_rows_for_testing() { return tab_rows_; }
  bool is_pinned_drop_lane_revealed_for_testing() const {
    return pinned_drop_lane_revealed_;
  }
  // Returns the maho-core tab_id for the given WebContents, or empty string.
  std::string FindCoreTabIdByWebContents(content::WebContents* contents) const;

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
  void CloseTab(int tab_index);
  void CloseTabById(const std::string& tab_id);
  int ResolveTabStripIndexForTabId(const std::string& tab_id) const;
  void OpenNewTab(const ui::Event& event);
  void ToggleFolderExpanded(const std::string& folder_id);
  void CommitTabTitleChange(const std::string& tab_id,
                            const std::u16string& new_title);
  std::u16string ResolveTabDisplayText(const SidebarTreeNode& node) const;
  void PruneLocalTitleCache(const MahoSidebarTabListModel& model);

  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<views::LabelButton> new_tab_button_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> tab_rows_ = nullptr;
  raw_ptr<views::View> pinned_section_target_ = nullptr;
  raw_ptr<views::View> normal_section_target_ = nullptr;
  std::string active_space_id_;
  std::map<std::string, std::u16string> local_custom_titles_;
  MahoSidebarTabListModel last_model_;
  bool has_last_model_ = false;
  bool pinned_drop_lane_revealed_ = false;
  raw_ptr<views::View> pinned_separator_ = nullptr;
  std::map<content::WebContents*, std::string> contents_to_core_id_;
  base::WeakPtrFactory<MahoSidebarTabListView> weak_factory_{this};
};

class SidebarTabRowView : public views::View,
                           public views::DragController,
                           public views::ContextMenuController,
                           public views::TextfieldController {
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
                         resolve_tab_index);
  SidebarTabRowView(const SidebarTabRowView&) = delete;
  SidebarTabRowView& operator=(const SidebarTabRowView&) = delete;
  ~SidebarTabRowView() override;

  void OnThemeChanged() override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source_type) override;

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;

  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& current_pt) override;

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

  const std::string& tab_id_for_testing() const { return tab_id_; }
  MahoSidebarTabSection section_for_testing() const { return section_; }
  views::ImageButton* close_button_for_testing() { return close_button_; }
  views::Textfield* title_field_for_testing() { return title_field_; }
  views::LabelButton* title_button_for_testing() { return title_button_; }

 private:
  SidebarTabDropZone ResolveDropZone(const ui::DropTargetEvent& event,
                                     const SidebarDragPayload& payload) const;
  void BeginTitleEditing();
  void EndTitleEditing(bool commit);
  void ApplyDisplayedTitle(const std::u16string& title);
  void UpdateAppearance();

  const bool active_;
  const bool pinned_;
  const bool loading_;
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
  std::unique_ptr<MahoTabContextMenu> context_menu_;
  GURL drop_url_;
  std::u16string drop_title_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  bool drag_over_drop_target_ = false;
  SidebarTabDropZone active_drop_zone_ = SidebarTabDropZone::kNone;
};

class SidebarFolderRowView : public views::View,
                             public views::DragController {
  METADATA_HEADER(SidebarFolderRowView, views::View)

 public:
  SidebarFolderRowView(const SidebarTreeNode& node,
                       MahoSidebarTabSection section,
                       std::string active_space_id,
                       views::Button::PressedCallback toggle_callback,
                       std::string next_sibling_folder_id = std::string(),
                       std::string parent_folder_id = std::string());
  SidebarFolderRowView(const SidebarFolderRowView&) = delete;
  SidebarFolderRowView& operator=(const SidebarFolderRowView&) = delete;
  ~SidebarFolderRowView() override;

  void OnThemeChanged() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;

  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& current_pt) override;

  bool is_drop_indicator_visible_for_testing() const {
    return drag_over_drop_target_;
  }

  enum class SidebarFolderDropZone {
    kNone,
    kInto,
  };

  SidebarFolderDropZone drop_zone_for_testing() const {
    return active_drop_zone_;
  }

  const std::string& folder_id_for_testing() const { return folder_id_; }

 private:
  SidebarFolderDropZone ResolveDropZone(const ui::DropTargetEvent& event,
                                        const SidebarDragPayload& payload) const;
  void UpdateAppearance();

  const std::string folder_id_;
  const std::string space_id_;
  const bool folder_is_pinned_;
  const std::string next_sibling_folder_id_;
  const std::string parent_folder_id_;
  const bool is_expanded_;
  raw_ptr<views::ImageView> chevron_ = nullptr;
  raw_ptr<views::ImageView> folder_icon_ = nullptr;
  raw_ptr<views::LabelButton> label_button_ = nullptr;
  bool drag_over_drop_target_ = false;
  SidebarFolderDropZone active_drop_zone_ = SidebarFolderDropZone::kNone;
};

struct SidebarSectionDropIndicatorStateForTesting {
  bool visible_after_update = false;
  bool visible_after_exit = false;
};

views::View::DropCallback GetSectionDropCallbackForTesting(
    MahoSidebarTabSection section,
    const ui::DropTargetEvent& event);
SidebarSectionDropIndicatorStateForTesting
GetSectionDropIndicatorStateForTesting(MahoSidebarTabSection section,
                                       const ui::DropTargetEvent& event);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TAB_LIST_VIEW_H_
