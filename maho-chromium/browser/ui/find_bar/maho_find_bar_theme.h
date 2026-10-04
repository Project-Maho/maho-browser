// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_FIND_BAR_MAHO_FIND_BAR_THEME_H_
#define MAHO_BROWSER_UI_FIND_BAR_MAHO_FIND_BAR_THEME_H_

namespace ui {
class ColorMixer;
}  // namespace ui

class MahoFindBarTheme {
 public:
  MahoFindBarTheme() = delete;

  // Remaps Chromium find bar colors to Maho equivalents.
  static void AddColors(ui::ColorMixer& mixer, bool dark_mode);
};

#endif  // MAHO_BROWSER_UI_FIND_BAR_MAHO_FIND_BAR_THEME_H_