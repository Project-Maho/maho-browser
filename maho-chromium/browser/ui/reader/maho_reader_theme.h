// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_READER_MAHO_READER_THEME_H_
#define MAHO_BROWSER_UI_READER_MAHO_READER_THEME_H_

namespace ui {
class ColorMixer;
}  // namespace ui

class MahoReaderTheme {
 public:
  MahoReaderTheme() = delete;

  // Registers Maho reader mode palettes and remaps ReadAnything colors.
  static void AddColors(ui::ColorMixer& mixer, bool dark_mode);
};

#endif  // MAHO_BROWSER_UI_READER_MAHO_READER_THEME_H_