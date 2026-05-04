// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_VIEW_H_

#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class Label;
class ScrollView;
class Textfield;
}  // namespace views

namespace maho {

class MahoCommandModel;
struct CommandSuggestion;

class MahoCommandOverlayView : public views::View,
                               public views::TextfieldController {
  METADATA_HEADER(MahoCommandOverlayView, views::View)

 public:
  using DismissCallback = base::OnceClosure;

  explicit MahoCommandOverlayView(Browser* browser,
                                  DismissCallback dismiss_callback);
  MahoCommandOverlayView(const MahoCommandOverlayView&) = delete;
  MahoCommandOverlayView& operator=(const MahoCommandOverlayView&) = delete;
  ~MahoCommandOverlayView() override;

  views::Textfield* textfield() { return textfield_; }

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  void OnSearchResults(std::vector<CommandSuggestion> results);
  void UpdateResultViews(const std::vector<CommandSuggestion>& results);
  void UpdateSelection(int new_index);
  void ActivateSelected();
  void Dismiss();
  void ResizeWidgetToContents();

  raw_ptr<Browser> browser_;
  std::unique_ptr<MahoCommandModel> model_;
  DismissCallback dismiss_callback_;

  raw_ptr<views::Textfield> textfield_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> results_container_ = nullptr;
  raw_ptr<views::Label> hint_label_ = nullptr;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_VIEW_H_
