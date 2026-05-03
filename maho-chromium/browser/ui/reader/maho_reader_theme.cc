// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/reader/maho_reader_theme.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_recipe.h"

// static
void MahoReaderTheme::AddColors(ui::ColorMixer& mixer, bool dark_mode) {
  // Reader mode palettes matching the Swift reference
  // (ReaderModeController.ReaderBackground enum).

  // Light: white background, near-black text.
  mixer[kMahoColorReaderLightBackground] = {SK_ColorWHITE};
  mixer[kMahoColorReaderLightText] = {SkColorSetRGB(33, 33, 33)};

  // Sepia: warm background, near-black text.
  mixer[kMahoColorReaderSepiaBackground] = {SkColorSetRGB(250, 242, 227)};
  mixer[kMahoColorReaderSepiaText] = {SkColorSetRGB(33, 33, 33)};

  // Dark: dark background, light text.
  mixer[kMahoColorReaderDarkBackground] = {SkColorSetRGB(38, 38, 38)};
  mixer[kMahoColorReaderDarkText] = {SkColorSetRGB(224, 224, 224)};

  // Remap Chromium ReadAnything colors to match current mode.
  mixer[kColorReadAnythingForeground] = {dark_mode
      ? kMahoColorReaderDarkText
      : kMahoColorReaderLightText};
  mixer[kColorReadAnythingCurrentReadAloudHighlight] = {
      kMahoColorSelectionHighlight};
}