// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/find_bar/maho_find_bar_theme.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_recipe.h"

// static
void MahoFindBarTheme::AddColors(ui::ColorMixer& mixer, bool dark_mode) {
  (void)dark_mode;
  // Find bar background matches Maho search field.
  mixer[kColorFindBarBackground] = {kMahoColorSearchFieldBackground};

  // Text and icons follow Maho text hierarchy.
  mixer[kColorFindBarForeground] = {kMahoColorPrimaryText};
  mixer[kColorFindBarButtonIcon] = {kMahoColorSecondaryText};
  mixer[kColorFindBarButtonIconHovered] = {kMahoColorPrimaryText};
  mixer[kColorFindBarButtonIconDisabled] = {kMahoColorTertiaryText};
  mixer[kColorFindBarMatchCount] = {kMahoColorTertiaryText};
}