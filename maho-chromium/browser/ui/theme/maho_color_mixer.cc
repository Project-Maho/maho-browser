// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/theme/maho_color_mixer.h"

#include <algorithm>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/values.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/downloads/maho_download_theme.h"
#include "maho/browser/ui/find_bar/maho_find_bar_theme.h"
#include "maho/browser/ui/permissions/maho_permission_theme.h"
#include "maho/browser/ui/reader/maho_reader_theme.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"

namespace {

constexpr SkColor kTitlebarGlassDark = SkColorSetARGB(209, 34, 31, 50);
constexpr SkColor kTitlebarGlassLight = SkColorSetARGB(235, 237, 238, 247);
constexpr SkColor kSidebarBackgroundDark = SkColorSetARGB(255, 39, 35, 54);
constexpr SkColor kSidebarBackgroundLight = SkColorSetARGB(255, 228, 229, 238);
constexpr SkColor kContentBackgroundDark = SkColorSetARGB(255, 28, 27, 45);
constexpr SkColor kContentBackgroundLight = SkColorSetARGB(255, 246, 247, 251);
constexpr SkColor kContentGradientTopDark = SkColorSetARGB(255, 35, 33, 55);
constexpr SkColor kContentGradientTopLight = SkColorSetARGB(255, 252, 252, 254);
constexpr SkColor kContentGradientBottomDark = SkColorSetARGB(255, 24, 23, 39);
constexpr SkColor kContentGradientBottomLight = SkColorSetARGB(255, 237, 239, 248);

void AddWindowAndSurfaceColors(ui::ColorMixer& mixer, bool dark_mode) {
  mixer[kMahoColorWindowBackground] = {dark_mode
      ? SkColorSetARGB(255, 16, 15, 21)
      : SkColorSetARGB(255, 240, 240, 245)};
  mixer[kMahoColorSidebarBackground] = {dark_mode
      ? kSidebarBackgroundDark
      : kSidebarBackgroundLight};
  mixer[kMahoColorContentBackground] = {dark_mode
      ? kContentBackgroundDark
      : kContentBackgroundLight};
  mixer[kMahoColorContentGradientTop] = {dark_mode
      ? kContentGradientTopDark
      : kContentGradientTopLight};
  mixer[kMahoColorContentGradientBottom] = {dark_mode
      ? kContentGradientBottomDark
      : kContentGradientBottomLight};
  mixer[kMahoColorContentBorder] = {dark_mode
      ? SkColorSetARGB(46, 55, 51, 81)
      : SkColorSetARGB(133, 189, 195, 219)};
  mixer[kMahoColorCardBackground] = {dark_mode
      ? SkColorSetARGB(255, 42, 39, 61)
      : SkColorSetARGB(255, 248, 249, 252)};
  mixer[kMahoColorCardBorder] = {dark_mode
      ? SkColorSetARGB(255, 60, 56, 85)
      : SkColorSetARGB(184, 194, 199, 222)};
  mixer[kMahoColorTitlebarGlass] = {dark_mode
      ? kTitlebarGlassDark
      : kTitlebarGlassLight};
  mixer[kMahoColorTitlebarEdge] = {dark_mode
      ? SkColorSetARGB(19, 255, 255, 255)
      : SkColorSetARGB(148, 188, 194, 218)};
  mixer[kMahoColorTitlebarControlTint] = {dark_mode
      ? SkColorSetARGB(255, 231, 231, 239)
      : SkColorSetARGB(255, 67, 73, 99)};
}

void AddCanvasColors(ui::ColorMixer& mixer, bool dark_mode) {
  mixer[kMahoColorCanvasAccent] = {dark_mode
      ? SkColorSetARGB(56, 103, 109, 189)
      : SkColorSetARGB(31, 113, 141, 250)};
  mixer[kMahoColorCanvasInnerGlow] = {dark_mode
      ? SkColorSetARGB(122, 55, 53, 82)
      : SkColorSetARGB(107, 196, 209, 248)};
  mixer[kMahoColorCanvasBadgeFill] = {SkColorSetARGB(199, 43, 41, 65)};
  mixer[kMahoColorCanvasBadgeBorder] = {SkColorSetARGB(112, 66, 62, 93)};
}

void AddInteractiveSurfaceColors(ui::ColorMixer& mixer, bool dark_mode) {
  mixer[kMahoColorSidebarHover] = {dark_mode
      ? SkColorSetARGB(15, 255, 255, 255)
      : SkColorSetARGB(15, 30, 41, 77)};
  mixer[kMahoColorSidebarSelected] = {dark_mode
      ? SkColorSetARGB(27, 255, 255, 255)
      : SkColorSetARGB(28, 54, 75, 140)};
  mixer[kMahoColorSpaceStripBorder] = {dark_mode
      ? SkColorSetARGB(10, 255, 255, 255)
      : SkColorSetARGB(168, 184, 190, 216)};
  mixer[kMahoColorSpaceControlTint] = {dark_mode
      ? SkColorSetARGB(122, 200, 201, 222)
      : SkColorSetARGB(128, 88, 97, 129)};
  mixer[kMahoColorSpaceChipHover] = {dark_mode
      ? SkColorSetARGB(20, 255, 255, 255)
      : SkColorSetARGB(20, 33, 43, 82)};
  mixer[kMahoColorSpaceChipSelected] = {dark_mode
      ? SkColorSetARGB(30, 255, 255, 255)
      : SkColorSetARGB(41, 45, 62, 115)};
  mixer[kMahoColorSpaceChipIdleBorder] = {dark_mode
      ? SkColorSetARGB(17, 255, 255, 255)
      : SkColorSetARGB(199, 164, 174, 210)};
  mixer[kMahoColorSpaceChipCountFill] = {SkColorSetARGB(235, 47, 45, 71)};
  mixer[kMahoColorSpaceChipCountBorder] = {SkColorSetARGB(173, 71, 66, 99)};
  mixer[kMahoColorSearchFieldBackground] = {dark_mode
      ? SkColorSetARGB(230, 43, 40, 62)
      : SkColorSetARGB(245, 242, 243, 249)};
  mixer[kMahoColorSearchFieldBorder] = {dark_mode
      ? SkColorSetARGB(77, 65, 61, 89)
      : SkColorSetARGB(107, 180, 186, 213)};
  mixer[kMahoColorNewTabStripBackground] = {dark_mode
      ? SkColorSetARGB(173, 40, 37, 59)
      : SkColorSetARGB(209, 237, 239, 248)};
  mixer[kMahoColorNewTabStripBorder] = {dark_mode
      ? SkColorSetARGB(66, 63, 59, 90)
      : SkColorSetARGB(97, 176, 182, 210)};
}

void AddTabRowColors(ui::ColorMixer& mixer, bool dark_mode) {
  mixer[kMahoColorTabRowHoverBackground] = {dark_mode
      ? SkColorSetARGB(26, 255, 255, 255)
      : SkColorSetARGB(20, 0, 0, 0)};
  mixer[kMahoColorTabRowActiveBackground] = {dark_mode
      ? SkColorSetARGB(51, 255, 255, 255)
      : SkColorSetARGB(38, 0, 0, 0)};
  mixer[kMahoColorTabRowActiveHoveredBackground] = {
      kMahoColorTabRowActiveBackground};
  mixer[kMahoColorInteractiveChipIdleBackground] = {dark_mode
      ? SkColorSetARGB(16, 255, 255, 255)
      : SkColorSetARGB(13, 0, 0, 0)};
}

void AddFavoritesColors(ui::ColorMixer& mixer, bool dark_mode) {
  mixer[kMahoColorFavoritesEmptyCardBackground] = {dark_mode
      ? SkColorSetARGB(245, 38, 35, 59)
      : SkColorSetARGB(245, 246, 247, 253)};
  mixer[kMahoColorFavoritesEmptyCardBorder] = {dark_mode
      ? SkColorSetARGB(128, 68, 63, 98)
      : SkColorSetARGB(138, 184, 193, 230)};
  mixer[kMahoColorFavoritesEmptyCardDash] = {dark_mode
      ? SkColorSetARGB(184, 124, 143, 250)
      : SkColorSetARGB(122, 108, 135, 237)};
  mixer[kMahoColorFavoritesEmptyBadgeBackground] = {dark_mode
      ? SkColorSetARGB(255, 48, 45, 74)
      : SkColorSetARGB(255, 236, 240, 252)};
  mixer[kMahoColorFavoritesEmptyBadgeBorder] = {dark_mode
      ? SkColorSetARGB(143, 102, 116, 228)
      : SkColorSetARGB(133, 163, 181, 242)};
  mixer[kMahoColorFavoritesEmptyBadgeIcon] = {dark_mode
      ? SkColorSetARGB(255, 194, 206, 255)
      : SkColorSetARGB(255, 79, 107, 216)};
  mixer[kMahoColorFavoritesEmptyCloseGlyph] = {dark_mode
      ? SkColorSetARGB(158, 196, 198, 226)
      : SkColorSetARGB(158, 118, 129, 167)};
}

void AddArchiveColors(ui::ColorMixer& mixer, bool dark_mode) {
  mixer[kMahoColorArchivePanelBackground] = {dark_mode
      ? SkColorSetARGB(235, 49, 48, 52)
      : SkColorSetARGB(214, 242, 243, 248)};
  mixer[kMahoColorArchiveControlBackground] = {dark_mode
      ? SkColorSetARGB(194, 62, 60, 64)
      : SkColorSetARGB(194, 245, 246, 250)};
  mixer[kMahoColorArchiveControlBorder] = {dark_mode
      ? SkColorSetARGB(31, 93, 91, 98)
      : SkColorSetARGB(10, 176, 184, 211)};
  mixer[kMahoColorArchiveControlActiveBackground] = {dark_mode
      ? SkColorSetARGB(194, 69, 68, 73)
      : SkColorSetARGB(128, 233, 237, 250)};
  mixer[kMahoColorArchiveRowHover] = {dark_mode
      ? SkColorSetARGB(6, 255, 255, 255)
      : SkColorSetARGB(3, 48, 61, 97)};
  mixer[kMahoColorArchiveRowSelected] = {dark_mode
      ? SkColorSetARGB(11, 255, 255, 255)
      : SkColorSetARGB(8, 55, 74, 129)};
  mixer[kMahoColorArchiveSectionHeaderText] = {dark_mode
      ? SkColorSetARGB(219, 186, 186, 193)
      : SkColorSetARGB(214, 90, 98, 124)};
  mixer[kMahoColorArchiveDivider] = {dark_mode
      ? SkColorSetARGB(5, 255, 255, 255)
      : SkColorSetARGB(9, 163, 171, 201)};
  mixer[kMahoColorArchiveDetailPanelBackground] = {dark_mode
      ? SkColorSetARGB(255, 21, 20, 30)
      : SkColorSetARGB(255, 237, 239, 248)};
  mixer[kMahoColorArchiveDetailPanelInnerBackground] = {dark_mode
      ? SkColorSetARGB(250, 30, 29, 43)
      : SkColorSetARGB(250, 244, 245, 252)};
  mixer[kMahoColorArchiveDetailPanelBorder] = {dark_mode
      ? SkColorSetARGB(209, 55, 53, 75)
      : SkColorSetARGB(122, 183, 190, 216)};
  mixer[kMahoColorArchiveDetailPanelMutedFill] = {dark_mode
      ? SkColorSetARGB(199, 43, 42, 61)
      : SkColorSetARGB(163, 225, 230, 248)};
  mixer[kMahoColorArchiveDetailPanelAccent] = {dark_mode
      ? SkColorSetARGB(61, 118, 130, 243)
      : SkColorSetARGB(46, 92, 116, 237)};
}

void AddLibraryRailColors(ui::ColorMixer& mixer, bool dark_mode) {
  mixer[kMahoColorLibraryRailBackground] = {dark_mode
      ? SkColorSetARGB(255, 43, 35, 48)
      : SkColorSetARGB(255, 224, 226, 234)};
  // Selected background: rgba(255,255,255,0.08) per mockup (.rail-item.active).
  mixer[kMahoColorLibraryRailItemSelected] = {dark_mode
      ? SkColorSetARGB(20, 255, 255, 255)
      : SkColorSetARGB(214, 217, 220, 230)};
  // Hover: slightly lifted above idle-transparent; no hover state in mockup so
  // we use half the selected alpha (0.06) for a subtle-but-perceptible feel.
  mixer[kMahoColorLibraryRailItemHover] = {dark_mode
      ? SkColorSetARGB(15, 255, 255, 255)
      : SkColorSetARGB(10, 0, 0, 0)};
  // Idle icon/label: #8a8290 per mockup (.rail-item .label / .rail-item svg stroke).
  mixer[kMahoColorLibraryRailItemText] = {dark_mode
      ? SkColorSetARGB(219, 138, 130, 144)
      : SkColorSetARGB(255, 87, 92, 112)};
  // Selected icon/label: #e8e4ec per mockup (.rail-item.active svg / .label).
  mixer[kMahoColorLibraryRailItemSelectedText] = {dark_mode
      ? SkColorSetARGB(255, 232, 228, 236)
      : SkColorSetARGB(255, 46, 51, 74)};
  mixer[kMahoColorLibraryRailDivider] = {dark_mode
      ? SkColorSetARGB(15, 255, 255, 255)
      : SkColorSetARGB(51, 179, 184, 204)};
}

void AddLibraryContentColors(ui::ColorMixer& mixer, bool dark_mode) {
  mixer[kMahoColorLibraryContentBackground] = {dark_mode
      ? SkColorSetARGB(255, 61, 52, 68)
      : SkColorSetARGB(255, 236, 234, 240)};
  mixer[kMahoColorLibraryListRowBackground] = {dark_mode
      ? SkColorSetARGB(10, 255, 255, 255)
      : SkColorSetARGB(10, 0, 0, 0)};
  mixer[kMahoColorLibraryRowHover] = {dark_mode
      ? SkColorSetARGB(14, 255, 255, 255)
      : SkColorSetARGB(12, 0, 0, 0)};
  mixer[kMahoColorLibraryRowSelected] = {dark_mode
      ? SkColorSetARGB(22, 255, 255, 255)
      : SkColorSetARGB(20, 0, 0, 0)};
  mixer[kMahoColorLibrarySectionHeaderText] = {dark_mode
      ? SkColorSetARGB(219, 122, 114, 128)
      : SkColorSetARGB(214, 90, 98, 124)};
  mixer[kMahoColorLibraryGlyphBorder] = {dark_mode
      ? SkColorSetARGB(25, 255, 255, 255)
      : SkColorSetARGB(20, 0, 0, 0)};
  mixer[kMahoColorLibrarySearchPillBackground] = {dark_mode
      ? SkColorSetARGB(64, 0, 0, 0)
      : SkColorSetARGB(40, 0, 0, 0)};
  mixer[kMahoColorLibraryProgressFill] = {SkColorSetARGB(255, 119, 215, 161)};
}

void AddTextColors(ui::ColorMixer& mixer, bool dark_mode) {
  mixer[kMahoColorPrimaryText] = {dark_mode
      ? SkColorSetARGB(255, 243, 243, 247)
      : SkColorSetARGB(255, 45, 51, 74)};
  mixer[kMahoColorSecondaryText] = {dark_mode
      ? SkColorSetARGB(255, 188, 188, 202)
      : SkColorSetARGB(255, 89, 97, 128)};
  mixer[kMahoColorTertiaryText] = {dark_mode
      ? SkColorSetARGB(255, 143, 143, 159)
      : SkColorSetARGB(255, 128, 136, 166)};
}

void AddToastColors(ui::ColorMixer& mixer, bool dark_mode) {
  mixer[kMahoColorToastBackground] = {dark_mode
      ? SkColorSetARGB(255, 42, 39, 61)
      : SkColorSetARGB(255, 255, 255, 255)};
  mixer[kMahoColorToastForeground] = {dark_mode
      ? SkColorSetARGB(255, 243, 243, 247)
      : SkColorSetARGB(255, 45, 51, 74)};
  mixer[kMahoColorToastBodyForeground] = {dark_mode
      ? SkColorSetARGB(255, 188, 188, 202)
      : SkColorSetARGB(255, 89, 97, 128)};
  mixer[kMahoColorToastBorder] = {dark_mode
      ? SkColorSetARGB(30, 255, 255, 255)
      : SkColorSetARGB(20, 0, 0, 0)};
}

void AddLegacyAliasColors(ui::ColorMixer& mixer, bool dark_mode) {
  mixer[kMahoColorDivider] = {dark_mode
      ? SkColorSetARGB(4, 255, 255, 255)
      : SkColorSetARGB(46, 162, 170, 199)};
  mixer[kMahoColorFooterDivider] = {dark_mode
      ? SkColorSetARGB(3, 255, 255, 255)
      : SkColorSetARGB(36, 162, 170, 199)};
}

void AddCommandBarColors(ui::ColorMixer& mixer, bool dark_mode) {
  mixer[kMahoColorCommandBarBackground] = {dark_mode
      ? SkColorSetRGB(0x17, 0x19, 0x20)
      : SkColorSetRGB(0xF6, 0xF8, 0xFC)};
  mixer[kMahoColorCommandBarPanelBorder] = {
      dark_mode ? SkColorSetARGB(0x12, 0xFF, 0xFF, 0xFF)
                : SkColorSetARGB(0x22, 0x8D, 0x98, 0xB5)};
  mixer[kMahoColorCommandBarInputFill] = {dark_mode
      ? SkColorSetRGB(0x22, 0x24, 0x2B)
      : SkColorSetRGB(0xFC, 0xFD, 0xFF)};
  mixer[kMahoColorCommandBarInputBorder] = {
      dark_mode ? SkColorSetARGB(0x0E, 0xFF, 0xFF, 0xFF)
                : SkColorSetARGB(0x28, 0xA3, 0xAE, 0xCB)};
  mixer[kMahoColorCommandBarSelectionFill] = {dark_mode
      ? SkColorSetRGB(0x4A, 0x54, 0xE8)
      : SkColorSetARGB(88, 95, 121, 244)};
  mixer[kMahoColorCommandBarSelectionBorder] = {
      dark_mode ? SkColorSetARGB(18, 255, 255, 255)
                : SkColorSetARGB(44, 92, 126, 237)};
  mixer[kMahoColorCommandBarKeycapBorder] = {
      dark_mode ? SkColorSetARGB(0x16, 0xFF, 0xFF, 0xFF)
                : SkColorSetARGB(0x2C, 0x91, 0x9C, 0xB8)};
  mixer[kMahoColorCommandBarInputSelection] = {
      dark_mode ? SkColorSetARGB(96, 88, 129, 214)
                : SkColorSetARGB(104, 150, 182, 255)};
  mixer[kMahoColorCommandBarEmptyText] = {dark_mode
      ? SkColorSetRGB(0x7E, 0x87, 0x97)
      : SkColorSetARGB(89, 0, 0, 0)};
  mixer[kMahoColorCommandBarShortcutBadge] = {dark_mode
      ? SkColorSetARGB(0x0E, 0xFF, 0xFF, 0xFF)
      : SkColorSetARGB(20, 0, 0, 0)};
  mixer[kMahoColorCommandBarShortcutText] = {dark_mode
      ? SkColorSetRGB(0x96, 0x9E, 0xAF)
      : SkColorSetARGB(115, 0, 0, 0)};
  mixer[kMahoColorCommandBarDivider] = {dark_mode
      ? SkColorSetARGB(8, 255, 255, 255)
      : SkColorSetARGB(18, 120, 129, 157)};
  mixer[kMahoColorCommandBarRowTitleSelected] = {dark_mode
      ? SkColorSetARGB(0xFF, 0xFF, 0xFF, 0xFF)
      : SkColorSetARGB(0xE0, 0xFF, 0xFF, 0xFF)};
  mixer[kMahoColorCommandBarRowSubtitleSelected] = {dark_mode
      ? SkColorSetARGB(0xC8, 0xF7, 0xF9, 0xFF)
      : SkColorSetARGB(0x98, 0xFF, 0xFF, 0xFF)};
}

void AddAccentAndSemanticColors(ui::ColorMixer& mixer, SkColor accent_color) {
  mixer[kMahoColorAccentBlue] = {accent_color};
  SkColor bright = SkColorSetARGB(
      SkColorGetA(accent_color),
      SkColorGetR(accent_color) + (255 - SkColorGetR(accent_color)) * 3 / 10,
      SkColorGetG(accent_color) + (255 - SkColorGetG(accent_color)) * 3 / 10,
      SkColorGetB(accent_color) + (255 - SkColorGetB(accent_color)) * 3 / 10);
  mixer[kMahoColorAccentBlueBright] = {bright};
  mixer[kMahoColorSelectionHighlight] = {accent_color};
  mixer[kMahoColorFolderIcon] = {SkColorSetRGB(96, 165, 250)};
  mixer[kMahoColorDangerRed] = {SkColorSetRGB(239, 68, 68)};
  mixer[kMahoColorSuccessGreen] = {SkColorSetRGB(34, 197, 94)};
  mixer[kMahoColorDropIndicator] = {SkColorSetARGB(
      204, SkColorGetR(accent_color), SkColorGetG(accent_color),
      SkColorGetB(accent_color))};
}

SkColor AdjustThemeTint(const MahoSpaceThemeState::SpaceHSV& hsv,
                       bool dark_mode,
                       uint8_t alpha,
                       float saturation_scale,
                       float dark_value_bias,
                       float light_saturation_scale,
                       float light_value_scale) {
  SkScalar saturation = std::clamp(hsv.saturation * saturation_scale, 0.0f, 1.0f);
  SkScalar value = dark_mode
                       ? std::clamp(hsv.value + dark_value_bias, 0.0f, 1.0f)
                       : std::clamp(hsv.value * light_value_scale, 0.0f, 1.0f);
  if (!dark_mode) {
    saturation = std::clamp(saturation * light_saturation_scale, 0.0f, 0.95f);
  }

  SkScalar adjusted[3] = {hsv.hue, saturation, value};
  return SkHSVToColor(alpha, adjusted);
}

float GetSidebarBlendForKind(MahoSpaceThemeState::ThemeKind kind) {
  switch (kind) {
    case MahoSpaceThemeState::ThemeKind::kSolid:
      return 0.08f;
    case MahoSpaceThemeState::ThemeKind::kGradient:
      return 0.11f;
    case MahoSpaceThemeState::ThemeKind::kZen:
      return 0.09f;
  }

  return 0.08f;
}

float GetContentBlendForKind(MahoSpaceThemeState::ThemeKind kind) {
  switch (kind) {
    case MahoSpaceThemeState::ThemeKind::kSolid:
      return 0.05f;
    case MahoSpaceThemeState::ThemeKind::kGradient:
      return 0.08f;
    case MahoSpaceThemeState::ThemeKind::kZen:
      return 0.065f;
  }

  return 0.05f;
}

float GetGradientBlendForKind(MahoSpaceThemeState::ThemeKind kind) {
  switch (kind) {
    case MahoSpaceThemeState::ThemeKind::kSolid:
      return 0.07f;
    case MahoSpaceThemeState::ThemeKind::kGradient:
      return 0.16f;
    case MahoSpaceThemeState::ThemeKind::kZen:
      return 0.13f;
  }

  return 0.07f;
}

float GetTitlebarBlendForKind(MahoSpaceThemeState::ThemeKind kind) {
  switch (kind) {
    case MahoSpaceThemeState::ThemeKind::kSolid:
      return 0.15f;
    case MahoSpaceThemeState::ThemeKind::kGradient:
      return 0.18f;
    case MahoSpaceThemeState::ThemeKind::kZen:
      return 0.12f;
  }

  return 0.15f;
}

}  // namespace

void AddMahoColorMixers(ui::ColorProvider* provider,
                        const ui::ColorProviderKey& key) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  const bool dark_mode =
      key.color_mode == ui::ColorProviderKey::ColorMode::kDark;
  // Incognito windows use dark mode via key.color_mode.

  SkColor accent_color = SkColorSetRGB(99, 102, 241);  // Default indigo.
  MahoCore* core = maho::GetCore();
  if (core) {
    char* json_str = maho_core_get_settings(core);
    if (json_str) {
      auto parsed = base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
      maho_string_free(json_str);
      if (parsed && parsed->is_dict()) {
        const auto* appearance = parsed->GetDict().FindDict("appearance");
        if (appearance) {
          const auto* color_dict = appearance->FindDict("accentColor");
          if (color_dict) {
            auto r = color_dict->FindInt("r");
            auto g = color_dict->FindInt("g");
            auto b = color_dict->FindInt("b");
            if (r && g && b) {
              accent_color = SkColorSetRGB(
                  static_cast<uint8_t>(std::clamp(*r, 0, 255)),
                  static_cast<uint8_t>(std::clamp(*g, 0, 255)),
                  static_cast<uint8_t>(std::clamp(*b, 0, 255)));
            }
          }
        }
      }
    }
  }

  ui::ColorMixer& mixer = provider->AddMixer();

  AddWindowAndSurfaceColors(mixer, dark_mode);
  AddCanvasColors(mixer, dark_mode);
  AddInteractiveSurfaceColors(mixer, dark_mode);
  AddTabRowColors(mixer, dark_mode);
  AddFavoritesColors(mixer, dark_mode);
  AddArchiveColors(mixer, dark_mode);
  AddLibraryRailColors(mixer, dark_mode);
  AddLibraryContentColors(mixer, dark_mode);
  AddTextColors(mixer, dark_mode);
  AddToastColors(mixer, dark_mode);
  AddLegacyAliasColors(mixer, dark_mode);
  AddCommandBarColors(mixer, dark_mode);
  AddAccentAndSemanticColors(mixer, accent_color);

  // Phase 4: Component theme overrides.
  MahoDownloadTheme::AddColors(mixer, dark_mode);
  MahoFindBarTheme::AddColors(mixer, dark_mode);
  MahoPermissionTheme::AddColors(mixer, dark_mode);
  MahoReaderTheme::AddColors(mixer, dark_mode);

  // Phase 3: Space theme tinting.
  // TODO: Skip for incognito/guest windows. ColorProviderKey lacks an incognito
  // signal, so we cannot distinguish incognito from normal dark-mode windows here.
  std::optional<MahoSpaceThemeState::ThemeData> theme_data =
      MahoSpaceThemeState::GetThemeData();
  if (theme_data) {
    const SkColor base_titlebar = dark_mode ? kTitlebarGlassDark
                                            : kTitlebarGlassLight;
    const SkColor base_sidebar = dark_mode ? kSidebarBackgroundDark
                                           : kSidebarBackgroundLight;
    const SkColor base_content = dark_mode ? kContentBackgroundDark
                                           : kContentBackgroundLight;
    const SkColor base_gradient_top = dark_mode ? kContentGradientTopDark
                                                : kContentGradientTopLight;
    const SkColor base_gradient_bottom = dark_mode ? kContentGradientBottomDark
                                                   : kContentGradientBottomLight;

    const float opacity_bias = 0.65f + theme_data->opacity * 0.35f;
    const float texture_bias = 0.04f + theme_data->texture * 0.08f;
    const float grain_bias = theme_data->grain * 0.04f;

    const SkColor primary_tint = AdjustThemeTint(
        theme_data->primary_hsv, dark_mode,
        theme_data->kind == MahoSpaceThemeState::ThemeKind::kSolid ? 22 : 18,
        1.0f, dark_mode ? 0.02f : 0.0f, 0.85f, 1.08f);

    const MahoSpaceThemeState::SpaceHSV& secondary_hsv =
        theme_data->secondary_hsv.value_or(theme_data->primary_hsv);
    const SkColor secondary_tint = AdjustThemeTint(
        secondary_hsv, dark_mode,
        theme_data->kind == MahoSpaceThemeState::ThemeKind::kZen ? 15 : 18,
        theme_data->kind == MahoSpaceThemeState::ThemeKind::kZen ? 0.72f : 0.9f,
        dark_mode ? -0.03f : 0.0f, 0.78f,
        theme_data->kind == MahoSpaceThemeState::ThemeKind::kZen ? 1.02f : 1.05f);

    ui::ColorMixer& tint_mixer = provider->AddMixer();
    tint_mixer[kMahoColorTitlebarGlass] = {maho_theme::BlendColors(
        base_titlebar, primary_tint,
        GetTitlebarBlendForKind(theme_data->kind) * opacity_bias)};
    tint_mixer[kMahoColorSidebarBackground] = {maho_theme::BlendColors(
        base_sidebar, primary_tint,
        GetSidebarBlendForKind(theme_data->kind) * opacity_bias + grain_bias)};
    tint_mixer[kMahoColorContentBackground] = {maho_theme::BlendColors(
        base_content, primary_tint,
        GetContentBlendForKind(theme_data->kind) * opacity_bias)};
    tint_mixer[kMahoColorContentGradientTop] = {maho_theme::BlendColors(
        base_gradient_top, primary_tint,
        GetGradientBlendForKind(theme_data->kind) * opacity_bias + texture_bias)};
    tint_mixer[kMahoColorContentGradientBottom] = {maho_theme::BlendColors(
        base_gradient_bottom, secondary_tint,
        GetGradientBlendForKind(theme_data->kind) * opacity_bias +
            texture_bias * 0.75f)};
    const SkColor base_tab_active = dark_mode
        ? SkColorSetARGB(51, 255, 255, 255)
        : SkColorSetARGB(38, 0, 0, 0);
    tint_mixer[kMahoColorTabRowActiveBackground] = {
        maho_theme::BlendColors(base_tab_active, primary_tint, 0.05f)};
  }

  DVLOG(1) << "MahoColorMixers: registered " << (kMahoColorsEnd - kMahoColorsStart)
           << " color IDs, dark_mode=" << dark_mode;
}
