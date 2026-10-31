// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_SRC_CHROME_BROWSER_UI_VIEWS_AUTOFILL_POPUP_POPUP_ROW_VIEW_H_
#define MAHO_CHROMIUM_SRC_CHROME_BROWSER_UI_VIEWS_AUTOFILL_POPUP_POPUP_ROW_VIEW_H_

#include <memory>

namespace views {
class Background;
}

// Preserve Chromium's complete PopupRowView API and inject only the narrow
// presentation hooks used by the Maho source wrapper. The upstream method
// remains untouched; its implementation naturally re-enters these hooks when
// Chromium changes selection/child-suggestion state.
#define SetChildSuggestionsDisplayed(...)           \
  SetChildSuggestionsDisplayed(__VA_ARGS__);        \
  void SetMahoMargins();                            \
  void SetMahoBackground(std::unique_ptr<views::Background> background); \
  void ApplyMahoPasswordTypography()
#include "../src/chrome/browser/ui/views/autofill/popup/popup_row_view.h"  // IWYU pragma: export
#undef SetChildSuggestionsDisplayed

#endif  // MAHO_CHROMIUM_SRC_CHROME_BROWSER_UI_VIEWS_AUTOFILL_POPUP_POPUP_ROW_VIEW_H_
