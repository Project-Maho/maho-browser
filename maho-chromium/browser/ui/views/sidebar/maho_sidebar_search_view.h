// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SEARCH_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SEARCH_VIEW_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

class Browser;
namespace views {
class ContextMenuController;
class MenuRunner;
class Label;
class Textfield;
class View;
class ImageView;
class LabelButton;
}  // namespace views

namespace ui {
class Event;
}  // namespace ui

namespace maho {

class MahoSidebarSearchView : public views::View,
                              public views::ContextMenuController,
                              public views::TextfieldController,
                              public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(MahoSidebarSearchView, views::View)

 public:
  explicit MahoSidebarSearchView(Browser* browser);
  MahoSidebarSearchView(const MahoSidebarSearchView&) = delete;
  MahoSidebarSearchView& operator=(const MahoSidebarSearchView&) = delete;
  ~MahoSidebarSearchView() override;

  void Update(const MahoSidebarSearchModel& model);

  views::Textfield* textfield_for_testing() { return textfield_; }
  bool is_editing_for_testing() const { return is_editing_; }

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source_type) override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdChecked(int command_id) const override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  void OnSearchPressed(const ui::Event& event);
  void BeginEditing();
  void EndEditing(bool submit);
  void FocusCommandSurface();
  void SubmitText(const std::u16string& text);
  void UpdateIdleSurface();
  void UpdateSearchChrome();
  bool LooksLikeUrl(const std::string& input) const;
  std::string NormalizeAddressInput(const std::string& input) const;
  void OnIdleButtonPressed(const ui::Event& event);
  std::u16string BuildPrimaryText() const;
  std::u16string BuildSecondaryText() const;

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> search_shell_ = nullptr;
  raw_ptr<views::LabelButton> idle_button_ = nullptr;
  raw_ptr<views::View> leading_icon_shell_ = nullptr;
  raw_ptr<views::ImageView> leading_icon_ = nullptr;
  raw_ptr<views::Label> primary_label_ = nullptr;
  raw_ptr<views::Label> secondary_label_ = nullptr;
  raw_ptr<views::View> trailing_actions_view_ = nullptr;
  raw_ptr<views::ImageView> action_icon_primary_ = nullptr;
  raw_ptr<views::ImageView> action_icon_secondary_ = nullptr;
  raw_ptr<views::Textfield> textfield_ = nullptr;
  MahoSidebarSearchModel model_;
  bool is_editing_ = false;
  base::WeakPtrFactory<MahoSidebarSearchView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SEARCH_VIEW_H_
