// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LIBRARY_ACTION_PANE_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LIBRARY_ACTION_PANE_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Button;
class Label;
}  // namespace views

namespace maho {

class MahoSidebarLibraryActionPaneView : public views::View {
  METADATA_HEADER(MahoSidebarLibraryActionPaneView, views::View)

 public:
  struct ActionSpec {
    std::u16string label;
    std::u16string accessible_name;
    bool enabled = true;
  };

  using ActionCallback = base::RepeatingCallback<void(size_t)>;

  MahoSidebarLibraryActionPaneView(std::u16string title,
                                   std::u16string description,
                                   std::vector<ActionSpec> actions,
                                   ActionCallback action_callback);
  MahoSidebarLibraryActionPaneView(
      const MahoSidebarLibraryActionPaneView&) = delete;
  MahoSidebarLibraryActionPaneView& operator=(
      const MahoSidebarLibraryActionPaneView&) = delete;
  ~MahoSidebarLibraryActionPaneView() override;

  void OnThemeChanged() override;
  void SetSidebarPalette(const MahoSidebarPalette& palette);

  void SetActionEnabled(size_t index, bool enabled);

  views::Button* button_for_action_for_testing(size_t index);
  views::Label* title_label_for_testing() { return title_label_; }
  views::Label* description_label_for_testing() { return description_label_; }
  const MahoSidebarPalette& sidebar_palette_for_testing() const {
    return palette_;
  }

 private:
  void HandleActionPressed(size_t index);
  void UpdateAppearance();

  ActionCallback action_callback_;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> description_label_ = nullptr;
  std::vector<raw_ptr<views::Button>> action_buttons_;
  MahoSidebarPalette palette_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LIBRARY_ACTION_PANE_VIEW_H_
