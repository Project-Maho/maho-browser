// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/theme/maho_color_mixer.h"

#include "base/logging.h"
#include "base/no_destructor.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/ui/downloads/maho_download_theme.h"
#include "maho/browser/ui/find_bar/maho_find_bar_theme.h"
#include "maho/browser/ui/permissions/maho_permission_theme.h"
#include "maho/browser/ui/reader/maho_reader_theme.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"

namespace {

constexpr SkColor kWindowBackgroundDark = SkColorSetARGB(255, 16, 15, 21);
constexpr SkColor kWindowBackgroundLight = SkColorSetARGB(255, 240, 240, 245);
constexpr SkColor kTitlebarGlassDark = SkColorSetARGB(209, 34, 31, 50);
constexpr SkColor kTitlebarGlassLight = SkColorSetARGB(235, 237, 238, 247);
constexpr SkColor kContentBackgroundDark = SkColorSetARGB(255, 28, 27, 45);
constexpr SkColor kContentBackgroundLight = SkColorSetARGB(255, 246, 247, 251);
constexpr SkColor kContentGradientTopDark = SkColorSetARGB(255, 35, 33, 55);
constexpr SkColor kContentGradientTopLight = SkColorSetARGB(255, 252, 252, 254);
constexpr SkColor kContentGradientBottomDark = SkColorSetARGB(255, 24, 23, 39);
constexpr SkColor kContentGradientBottomLight = SkColorSetARGB(255, 237, 239, 248);

void AddChromeSurfaceColors(ui::ColorMixer& mixer, bool dark_mode) {
  mixer[kMahoColorWindowBackground] = {dark_mode
      ? kWindowBackgroundDark
      : kWindowBackgroundLight};
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
  // Split view draws a 1dp stroke around each content pane (upstream
  // ContentsContainerOutline, kColorSysOutline/kColorSysNeutralOutline). Make
  // the always-on active/inactive outlines transparent so panes sit flush; the
  // drag-to-split highlight outline is left visible as the drop indicator.
  mixer[kColorMultiContentsViewActiveContentOutline] = {SK_ColorTRANSPARENT};
  mixer[kColorMultiContentsViewInactiveContentOutline] = {SK_ColorTRANSPARENT};
  // MultiContentsView frames the split content region with ContentsSeparator
  // views painted in kColorToolbarContentAreaSeparator (material
  // kColorSysSurfaceVariant). Transparent drops that gray framing. The same id
  // is the single-pane toolbar/content underline, so it is borderless there too,
  // matching Maho's minimal glass surface.
  mixer[kColorToolbarContentAreaSeparator] = {SK_ColorTRANSPARENT};
  mixer[kMahoColorCardBackground] = {dark_mode
      ? SkColorSetARGB(255, 42, 39, 61)
      : SkColorSetARGB(255, 248, 249, 252)};
  mixer[kMahoColorCardBorder] = {dark_mode
      ? SkColorSetARGB(255, 60, 56, 85)
      : SkColorSetARGB(184, 194, 199, 222)};
  mixer[kMahoColorCtrlTabOverlayBackground] = {dark_mode
      ? SkColorSetARGB(240, 24, 24, 27)
      : SkColorSetARGB(240, 246, 247, 252)};
  mixer[kMahoColorCtrlTabAccent] = {dark_mode
      ? SkColorSetARGB(255, 110, 169, 255)
      : SkColorSetARGB(255, 42, 130, 245)};
  mixer[kMahoColorTitlebarGlass] = {dark_mode
      ? kTitlebarGlassDark
      : kTitlebarGlassLight};
  mixer[kMahoColorTitlebarEdge] = {dark_mode
      ? SkColorSetARGB(19, 255, 255, 255)
      : SkColorSetARGB(148, 188, 194, 218)};
  mixer[kMahoColorTitlebarControlTint] = {dark_mode
      ? SkColorSetARGB(255, 231, 231, 239)
      : SkColorSetARGB(255, 67, 73, 99)};
  mixer[kMahoColorMiniFrameWash] = {dark_mode
      ? SkColorSetARGB(224, 42, 25, 47)
      : SkColorSetARGB(24, 255, 255, 255)};
  mixer[kMahoColorMiniFrameEdge] = {dark_mode
      ? SkColorSetARGB(46, 214, 177, 224)
      : SkColorSetARGB(30, 0, 0, 0)};
  mixer[kMahoColorMiniControlBackground] = {dark_mode
      ? SkColorSetARGB(196, 58, 39, 68)
      : SkColorSetARGB(222, 255, 255, 255)};
  mixer[kMahoColorMiniControlBorder] = {dark_mode
      ? SkColorSetARGB(54, 225, 195, 232)
      : SkColorSetARGB(46, 0, 0, 0)};
  mixer[kMahoColorMiniPrimaryText] = {dark_mode
      ? SkColorSetARGB(255, 245, 241, 247)
      : SkColorSetARGB(255, 38, 40, 45)};
  mixer[kMahoColorMiniSecondaryText] = {dark_mode
      ? SkColorSetARGB(255, 196, 181, 202)
      : SkColorSetARGB(255, 96, 100, 108)};
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

void AddBrowserChromeInteractiveColors(ui::ColorMixer& mixer,
                                       bool dark_mode) {
  mixer[kMahoColorSidebarHover] = {dark_mode
      ? SkColorSetARGB(15, 255, 255, 255)
      : SkColorSetARGB(15, 30, 41, 77)};
  mixer[kMahoColorSearchFieldBackground] = {dark_mode
      ? SkColorSetARGB(230, 43, 40, 62)
      : SkColorSetARGB(245, 242, 243, 249)};
  mixer[kMahoColorTabRowActiveBackground] = {dark_mode
      ? SkColorSetARGB(51, 255, 255, 255)
      : SkColorSetARGB(38, 0, 0, 0)};
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
      ? SkColorSetARGB(40, 42, 39, 61)
      : SkColorSetARGB(40, 255, 255, 255)};
  mixer[kMahoColorToastForeground] = {dark_mode
      ? SkColorSetARGB(255, 243, 243, 247)
      : SkColorSetARGB(255, 45, 51, 74)};
  mixer[kMahoColorToastBodyForeground] = {dark_mode
      ? SkColorSetARGB(255, 188, 188, 202)
      : SkColorSetARGB(255, 89, 97, 128)};
  mixer[kMahoColorToastBorder] = {dark_mode
      ? SkColorSetARGB(80, 255, 255, 255)
      : SkColorSetARGB(50, 0, 0, 0)};
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
      ? SkColorSetARGB(112, 210, 155, 145)  // warm rose tint (Arc parity)
      : SkColorSetARGB(28, 120, 55, 50)};   // warm rose tint (light)
  mixer[kMahoColorCommandBarSelectionBorder] = {
      dark_mode ? SkColorSetARGB(0, 0, 0, 0)
                : SkColorSetARGB(0, 0, 0, 0)};
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
  mixer[kMahoColorCommandBarDividerHairline] = {dark_mode
      ? SkColorSetARGB(0, 0, 0, 0)
      : SkColorSetARGB(0, 0, 0, 0)};
  mixer[kMahoColorCommandBarRowTitleSelected] = {dark_mode
      ? SkColorSetARGB(0xFF, 0xFF, 0xFF, 0xFF)
      : kMahoColorPrimaryText};
  mixer[kMahoColorCommandBarRowSubtitleSelected] = {dark_mode
      ? SkColorSetARGB(0xC8, 0xF7, 0xF9, 0xFF)
      : kMahoColorTertiaryText};

  mixer[kMahoColorCommandBarSparkleOuter] = {SkColorSetRGB(0x73, 0x7C, 0xFF)};
  mixer[kMahoColorCommandBarSparkleInner] = {SkColorSetRGB(0xFF, 0xC7, 0x57)};

  mixer[kMahoColorCommandGlyphTab] = {SkColorSetRGB(0xEE, 0xF1, 0xF8)};
  mixer[kMahoColorCommandGlyphBookmark] = {SkColorSetRGB(0xF0, 0xC6, 0x66)};
  mixer[kMahoColorCommandGlyphHistory] = {SkColorSetRGB(0xBE, 0xC7, 0xD9)};
  mixer[kMahoColorCommandGlyphAction] = {SkColorSetRGB(0xC8, 0x92, 0xFF)};
  mixer[kMahoColorCommandGlyphNavigation] = {SkColorSetRGB(0x86, 0xE0, 0xCB)};
  mixer[kMahoColorCommandGlyphSearch] = {SkColorSetRGB(0xA8, 0xC4, 0xF0)};
  mixer[kMahoColorCommandGlyphCalculator] = {SkColorSetRGB(0xFF, 0xA3, 0x7E)};
  mixer[kMahoColorCommandGlyphUnitConversion] = {SkColorSetRGB(0xF2, 0x9A, 0x9A)};
  mixer[kMahoColorCommandGlyphArchivedTab] = {SkColorSetRGB(0xA5, 0xB7, 0xD2)};
  mixer[kMahoColorCommandGlyphClosedTab] = {SkColorSetRGB(0xC3, 0xA6, 0xEB)};
  mixer[kMahoColorCommandGlyphFolder] = {SkColorSetRGB(0x8C, 0xA6, 0xFF)};
  mixer[kMahoColorCommandGlyphRecentSearch] = {SkColorSetRGB(0xB1, 0xBC, 0xD7)};
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

// R-12: Private/Incognito semantic colors. These are unconditional
// constants — never conditioned on profile in the mixer layer.
// Profile-aware Views code calls SetPrivateAppearance() to inject them.
void AddPrivateColors(ui::ColorMixer& mixer) {
  // Frozen Todo-1 (DESIGN.md) private-shell tokens, assigned verbatim:
  // shell #111214, surface #202124, elevated/search #2F3033,
  // primary text #ECEDEF, secondary text #A7ABB1, border #858A91,
  // hover #303134. Values are fixed and never tinted by the active Space.
  mixer[kMahoColorPrivateSidebarBackground] = {SkColorSetRGB(0x20, 0x21, 0x24)};
  mixer[kMahoColorPrivateSidebarText]        = {SkColorSetRGB(0xEC, 0xED, 0xEF)};
  mixer[kMahoColorPrivateTabRowBackground]   = {SkColorSetRGB(0x20, 0x21, 0x24)};
  mixer[kMahoColorPrivateTabRowHover]        = {SkColorSetRGB(0x30, 0x31, 0x34)};
  mixer[kMahoColorPrivateSearchBackground]   = {SkColorSetRGB(0x2F, 0x30, 0x33)};
  mixer[kMahoColorPrivateSearchBorder]       = {SkColorSetRGB(0x85, 0x8A, 0x91)};
  mixer[kMahoColorPrivateFooterBackground]   = {SkColorSetRGB(0x11, 0x12, 0x14)};
  mixer[kMahoColorPrivateIdentityIcon]       = {SkColorSetRGB(0xA7, 0xAB, 0xB1)};
  mixer[kMahoColorPrivateTopBarBackground]   = {SkColorSetRGB(0x11, 0x12, 0x14)};
}

// R-12: Identity marker attached to key.app_controller for OTR/Incognito
// windows so AddMahoColorMixers can recognize them. It MUST be a real
// InitializerSupplier: the color pipeline unconditionally dereferences
// key.app_controller and invokes the pure-virtual AddColorMixers()
// (chrome/browser/ui/color/chrome_color_mixers.cc), so a fake pointer would be
// an illegal virtual call. This subclass makes that call a safe no-op.
class MahoOtrSentinelSupplier
    : public ui::ColorProviderKey::InitializerSupplier {
 public:
  void AddColorMixers(ui::ColorProvider* provider,
                      const ui::ColorProviderKey& key) const override {}

 protected:
  ~MahoOtrSentinelSupplier() override = default;
};

}  // namespace

ui::ColorProviderKey::InitializerSupplier* GetMahoOtrSentinel() {
  static base::NoDestructor<MahoOtrSentinelSupplier> sentinel;
  return sentinel.get();
}

void AddMahoColorMixers(ui::ColorProvider* provider,
                        const ui::ColorProviderKey& key) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  const bool is_otr = (key.app_controller == GetMahoOtrSentinel());

  const bool browser_theme_dark =
      is_otr || (key.color_mode == ui::ColorProviderKey::ColorMode::kDark);

  const SkColor kFixedAccentColor = SkColorSetRGB(99, 102, 241);
  // Fixed brand accent (indigo-500); no longer user-configurable.

  ui::ColorMixer& mixer = provider->AddMixer();

  AddChromeSurfaceColors(mixer, browser_theme_dark);
  AddCanvasColors(mixer, browser_theme_dark);
  AddBrowserChromeInteractiveColors(mixer, browser_theme_dark);
  AddTextColors(mixer, browser_theme_dark);
  AddToastColors(mixer, browser_theme_dark);
  AddLegacyAliasColors(mixer, browser_theme_dark);
  AddCommandBarColors(mixer, browser_theme_dark);
  AddAccentAndSemanticColors(mixer, kFixedAccentColor);
  AddPrivateColors(mixer);  // R-12: unconditional; always registered.

  // Phase 4: Component theme overrides.
  MahoDownloadTheme::AddColors(mixer, browser_theme_dark);
  MahoFindBarTheme::AddColors(mixer, browser_theme_dark);
  MahoPermissionTheme::AddColors(mixer, browser_theme_dark);
  MahoReaderTheme::AddColors(mixer, browser_theme_dark);

  DVLOG(1) << "MahoColorMixers: registered " << (kMahoColorsEnd - kMahoColorsStart)
           << " color IDs, browser_theme_dark=" << browser_theme_dark;
}
