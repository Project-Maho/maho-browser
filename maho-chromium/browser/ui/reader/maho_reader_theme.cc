// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/reader/maho_reader_theme.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_recipe.h"

// static
void MahoReaderTheme::AddColors(ui::ColorMixer& mixer, bool dark_mode) {
  // Foreground/background stay with Chromium so they remain a matched pair
  // per ReadAnything theme; overriding only one breaks contrast.
  (void)dark_mode;
  mixer[kColorReadAnythingCurrentReadAloudHighlight] = {
      kMahoColorSelectionHighlight};
}
