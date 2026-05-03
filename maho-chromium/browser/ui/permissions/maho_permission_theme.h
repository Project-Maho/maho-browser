// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_PERMISSIONS_MAHO_PERMISSION_THEME_H_
#define MAHO_BROWSER_UI_PERMISSIONS_MAHO_PERMISSION_THEME_H_

namespace ui {
class ColorMixer;
}  // namespace ui

class MahoPermissionTheme {
 public:
  MahoPermissionTheme() = delete;

  // Remaps Chromium permission prompt colors to Maho equivalents.
  static void AddColors(ui::ColorMixer& mixer, bool dark_mode);
};

#endif  // MAHO_BROWSER_UI_PERMISSIONS_MAHO_PERMISSION_THEME_H_