// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CREATE_SPACE_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CREATE_SPACE_VIEW_H_

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

  void SetCloseCallback(base::RepeatingClosure close_callback);
  void RequestNameFocus();

 private:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& event) override;
  void OnCreatePressed();
  void OnCancelPressed();
  void OnSpaceCreated(const std::string& new_space_id);

  raw_ptr<Browser> browser_;
  raw_ptr<views::Textfield> name_field_ = nullptr;
  raw_ptr<views::MdTextButton> create_button_ = nullptr;
  raw_ptr<views::MdTextButton> cancel_button_ = nullptr;
  base::RepeatingClosure close_callback_;
  base::WeakPtrFactory<MahoSidebarCreateSpaceView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CREATE_SPACE_VIEW_H_
