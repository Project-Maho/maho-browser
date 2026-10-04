// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FOLDER_HOVER_POPUP_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FOLDER_HOVER_POPUP_VIEW_H_

#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class ImageView;
class Label;
class LabelButton;
class ScrollView;
class Textfield;
class Widget;
} // namespace views

namespace maho {

class MahoSidebarFolderHoverPopupView : public views::View,
                                        public views::TextfieldController {
  METADATA_HEADER(MahoSidebarFolderHoverPopupView, views::View)

public:
  using ActivateTabCallback =
      base::RepeatingCallback<void(const std::string &tab_id)>;

  static views::Widget *Show(views::View *anchor_view, Browser *browser,
                             const std::u16string &folder_name,
                             std::string folder_id,
                             std::vector<SidebarTreeNode> children,
                             ActivateTabCallback on_activate);

  MahoSidebarFolderHoverPopupView(Browser *browser, std::u16string folder_name,
                                  std::string folder_id,
                                  std::vector<SidebarTreeNode> children,
                                  ActivateTabCallback on_activate);
  MahoSidebarFolderHoverPopupView(const MahoSidebarFolderHoverPopupView &) =
      delete;
  MahoSidebarFolderHoverPopupView &
  operator=(const MahoSidebarFolderHoverPopupView &) = delete;
  ~MahoSidebarFolderHoverPopupView() override;

  // views::View:
  void Init();
  void OnThemeChanged() override;
  void SetSidebarPalette(const MahoSidebarPalette &palette);

  // TextfieldController:
  void ContentsChanged(views::Textfield *sender,
                       const std::u16string &new_contents) override;
  bool HandleKeyEvent(views::Textfield *sender,
                      const ui::KeyEvent &key_event) override;

  const std::string &current_root_folder_id() const { return root_folder_id_; }
  views::Textfield *search_field() { return search_field_; }

private:
  struct Level {
    Level(std::u16string name, std::string folder_id,
          std::vector<SidebarTreeNode> children);
    Level(const Level &);
    Level(Level &&) noexcept;
    Level &operator=(const Level &);
    Level &operator=(Level &&) noexcept;
    ~Level();

    std::u16string name;
    std::string folder_id;
    std::vector<SidebarTreeNode> children;
  };

  void RebuildResultRows();
  void ApplyFilter(const std::u16string &query);
  void MoveSelection(int delta);
  void ActivateSelected();
  void PushLevel(const SidebarTreeNode &folder_node);
  void PopLevel();
  void RebuildBreadcrumb();

  void OnRowHovered(int index);
  void OnRowClicked(int index);

  raw_ptr<Browser> browser_;
  std::string root_folder_id_;
  std::vector<Level> nav_stack_;
  std::vector<size_t> filtered_indices_;
  int selected_index_ = 0;

  raw_ptr<views::View> breadcrumb_row_ = nullptr;
  raw_ptr<views::LabelButton> back_button_ = nullptr;
  raw_ptr<views::Label> path_label_ = nullptr;
  raw_ptr<views::ImageView> search_icon_ = nullptr;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<views::View> separator_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> rows_container_ = nullptr;

  ActivateTabCallback activate_callback_;
  MahoSidebarPalette palette_;
  base::CallbackListSubscription palette_subscription_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoSidebarFolderHoverPopupView> weak_factory_{this};
};

} // namespace maho

#endif // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FOLDER_HOVER_POPUP_VIEW_H_
