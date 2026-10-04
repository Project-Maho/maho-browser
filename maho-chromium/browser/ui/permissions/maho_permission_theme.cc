// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/permissions/maho_permission_theme.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_recipe.h"

// static
void MahoPermissionTheme::AddColors(ui::ColorMixer& mixer, bool dark_mode) {
  (void)dark_mode;
  // Permission prompt text follows Maho text colors.
  mixer[kColorPermissionPromptRequestText] = {kMahoColorSecondaryText};

  // Page info permission icons use Maho accent.
  mixer[kColorPageInfoPermissionUsedIcon] = {kMahoColorAccentBlue};
}