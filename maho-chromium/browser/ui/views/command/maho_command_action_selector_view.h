// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_SELECTOR_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_SELECTOR_VIEW_H_

#include "ui/views/view.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "base/functional/callback.h"
#include "ui/base/metadata/metadata_header_macros.h"

namespace views {
class Label;
class Button;
}

namespace maho {

class MahoCommandActionSelectorView : public views::View {
  friend class MahoCommandActionSelectorViewTest;
 public:
  METADATA_HEADER(MahoCommandActionSelectorView, views::View)

 public:
  explicit MahoCommandActionSelectorView(
      base::RepeatingCallback<void(PaletteAction)> on_change);
  MahoCommandActionSelectorView(const MahoCommandActionSelectorView&) = delete;
  MahoCommandActionSelectorView& operator=(const MahoCommandActionSelectorView&) = delete;
  ~MahoCommandActionSelectorView() override;

  void SetSelected(PaletteAction action);
  PaletteAction selected() const { return selected_; }

  void SetEnabled(bool enabled);
  bool enabled() const { return enabled_; }

  // views::View overrides:
  void OnThemeChanged() override;
  void Layout(PassKey) override;

  void OnSegmentClickedForTesting(PaletteAction action) {
    OnSegmentClicked(action);
  }

 private:
  void OnSegmentClicked(PaletteAction action);
  void UpdateVisuals();

  base::RepeatingCallback<void(PaletteAction)> on_change_;
  PaletteAction selected_ = PaletteAction::kSearch;
  bool enabled_ = true;

  raw_ptr<views::Button> search_button_ = nullptr;
  raw_ptr<views::Button> ask_maho_button_ = nullptr;
  raw_ptr<views::Label> hint_label_ = nullptr;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_SELECTOR_VIEW_H_
