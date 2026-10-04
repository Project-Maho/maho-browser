// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/downloads/maho_download_theme.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_recipe.h"

// static
void MahoDownloadTheme::AddColors(ui::ColorMixer& mixer, bool dark_mode) {
  (void)dark_mode;
  // Download bubble surfaces use Maho card colors.
  mixer[kColorDownloadBubbleInfoBackground] = {kMahoColorCardBackground};
  mixer[kColorDownloadBubbleRowHover] = {kMahoColorSidebarHover};

  // Icons follow Maho accent and semantic colors.
  mixer[kColorDownloadBubblePrimaryIcon] = {kMahoColorAccentBlue};
  mixer[kColorDownloadBubbleShowAllDownloadsIcon] = {kMahoColorSecondaryText};
  mixer[kColorDownloadBubbleInfoIcon] = {kMahoColorSecondaryText};

  // Danger/warning states use Maho semantic colors.
  mixer[kColorDownloadItemIconDangerous] = {kMahoColorDangerRed};
  mixer[kColorDownloadItemTextDangerous] = {kMahoColorDangerRed};
  mixer[kColorDownloadItemIconWarning] = {kMahoColorSecondaryText};
  mixer[kColorDownloadItemTextWarning] = {kMahoColorSecondaryText};

  // Toolbar button follows Maho accent.
  mixer[kColorDownloadToolbarButtonActive] = {kMahoColorAccentBlue};
  mixer[kColorDownloadToolbarButtonInactive] = {kMahoColorTertiaryText};
}