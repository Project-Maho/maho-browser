// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_DOWNLOADS_MAHO_DOWNLOAD_THEME_H_
#define MAHO_BROWSER_UI_DOWNLOADS_MAHO_DOWNLOAD_THEME_H_

namespace ui {
class ColorMixer;
}  // namespace ui

class MahoDownloadTheme {
 public:
  MahoDownloadTheme() = delete;

  // Remaps Chromium download component colors to Maho equivalents.
  static void AddColors(ui::ColorMixer& mixer, bool dark_mode);
};

#endif  // MAHO_BROWSER_UI_DOWNLOADS_MAHO_DOWNLOAD_THEME_H_