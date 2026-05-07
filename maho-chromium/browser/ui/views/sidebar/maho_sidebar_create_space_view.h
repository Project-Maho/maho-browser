// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CREATE_SPACE_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CREATE_SPACE_VIEW_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class Label;
class MdTextButton;
class Textfield;
}  // namespace views

namespace maho {

// Dedicated sidebar space-creation surface shown in place of the normal
// sidebar content. Provides a name input and create/cancel controls without
// opening a separate popup window.
class MahoSidebarCreateSpaceView : public views::View,
                                   public views::TextfieldController {
  METADATA_HEADER(MahoSidebarCreateSpaceView, views::View)

 public:
  explicit MahoSidebarCreateSpaceView(Browser* browser);
  MahoSidebarCreateSpaceView(const MahoSidebarCreateSpaceView&) = delete;
  MahoSidebarCreateSpaceView& operator=(const MahoSidebarCreateSpaceView&) =
      delete;
  ~MahoSidebarCreateSpaceView() override;

  void OnThemeChanged() override;

  void SetCloseCallback(base::RepeatingClosure close_callback);
  void RequestNameFocus();

  // Test-only accessors/seams for interactive browser tests.
  views::Textfield* name_field_for_testing() { return name_field_; }
  views::MdTextButton* create_button_for_testing() { return create_button_; }
  views::Label* error_label_for_testing() { return error_label_; }
  void SetNameForTesting(const std::u16string& text);
  void CompleteCreateSpaceForTesting(bool success,
                                     const std::string& new_space_id);

 private:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& event) override;
  void UpdateCreateButtonState();
  void OnCreatePressed();
  void OnCancelPressed();
  void OnColorSwatchPressed(int index);
  void OnSpaceCreated(bool success, const std::string& new_space_id);

  raw_ptr<Browser> browser_;
  raw_ptr<views::Textfield> name_field_ = nullptr;
  raw_ptr<views::MdTextButton> create_button_ = nullptr;
  raw_ptr<views::Label> error_label_ = nullptr;
  raw_ptr<views::MdTextButton> cancel_button_ = nullptr;
  raw_ptr<views::View> swatch_row_ = nullptr;
  int selected_color_index_ = 7;
  base::RepeatingClosure close_callback_;
  base::WeakPtrFactory<MahoSidebarCreateSpaceView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CREATE_SPACE_VIEW_H_
