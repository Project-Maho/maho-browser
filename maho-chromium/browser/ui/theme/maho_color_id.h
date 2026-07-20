// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_THEME_MAHO_COLOR_ID_H_
#define MAHO_BROWSER_UI_THEME_MAHO_COLOR_ID_H_

#include "chrome/browser/ui/color/chrome_color_id.h"

// Maho-specific color IDs.
//
// These extend Chromium's layered color-range system:
//   ui (kUiColorsStart..kUiColorsEnd)
//     → components (..kComponentsColorsEnd)
//       → chrome (..kChromeColorsEnd)
//         → maho (kMahoColorsStart..kMahoColorsEnd)
//
// All values must stay ≤ ui::kUiColorsLast (0xFFFF).

// clang-format off
#define MAHO_COLOR_IDS \
  /* Window & Surface */ \
  E_CPONLY(kMahoColorWindowBackground, kMahoColorsStart, kMahoColorsStart) \
  E_CPONLY(kMahoColorSidebarBackground) \
  E_CPONLY(kMahoColorContentBackground) \
  E_CPONLY(kMahoColorContentGradientTop) \
  E_CPONLY(kMahoColorContentGradientBottom) \
  E_CPONLY(kMahoColorContentBorder) \
  E_CPONLY(kMahoColorCardBackground) \
  E_CPONLY(kMahoColorCardBorder) \
  E_CPONLY(kMahoColorCtrlTabOverlayBackground) \
  E_CPONLY(kMahoColorCtrlTabAccent) \
  E_CPONLY(kMahoColorTitlebarGlass) \
  E_CPONLY(kMahoColorTitlebarEdge) \
  E_CPONLY(kMahoColorTitlebarControlTint) \
  /* Canvas */ \
  E_CPONLY(kMahoColorCanvasAccent) \
  E_CPONLY(kMahoColorCanvasInnerGlow) \
  E_CPONLY(kMahoColorCanvasBadgeFill) \
  E_CPONLY(kMahoColorCanvasBadgeBorder) \
  /* Interactive Surfaces */ \
  E_CPONLY(kMahoColorSidebarHover) \
  E_CPONLY(kMahoColorSidebarSelected) \
  E_CPONLY(kMahoColorSpaceStripBorder) \
  E_CPONLY(kMahoColorSpaceControlTint) \
  E_CPONLY(kMahoColorSpaceChipHover) \
  E_CPONLY(kMahoColorSpaceChipSelected) \
  E_CPONLY(kMahoColorSpaceChipIdleBorder) \
  E_CPONLY(kMahoColorSpaceChipCountFill) \
  E_CPONLY(kMahoColorSpaceChipCountBorder) \
  E_CPONLY(kMahoColorSearchFieldBackground) \
  E_CPONLY(kMahoColorSearchFieldBorder) \
  E_CPONLY(kMahoColorNewTabStripBackground) \
  E_CPONLY(kMahoColorNewTabStripBorder) \
  E_CPONLY(kMahoColorTabRowHoverBackground) \
  E_CPONLY(kMahoColorTabRowActiveBackground) \
  E_CPONLY(kMahoColorTabRowActiveHoveredBackground) \
  E_CPONLY(kMahoColorTabRowSelectedBackground) \
  E_CPONLY(kMahoColorTabRowSelectedHoveredBackground) \
  E_CPONLY(kMahoColorInteractiveChipIdleBackground) \
  /* Favorites */ \
  E_CPONLY(kMahoColorFavoritesEmptyCardBackground) \
  E_CPONLY(kMahoColorFavoritesEmptyCardBorder) \
  E_CPONLY(kMahoColorFavoritesEmptyCardDash) \
  E_CPONLY(kMahoColorFavoritesEmptyBadgeBackground) \
  E_CPONLY(kMahoColorFavoritesEmptyBadgeBorder) \
  E_CPONLY(kMahoColorFavoritesEmptyBadgeIcon) \
  E_CPONLY(kMahoColorFavoritesEmptyCloseGlyph) \
  /* Library Content (shared across Archive, Downloads, Boosts panels) */ \
  E_CPONLY(kMahoColorLibraryContentBackground) \
  E_CPONLY(kMahoColorLibraryListRowBackground) \
  E_CPONLY(kMahoColorLibraryRowHover) \
  E_CPONLY(kMahoColorLibraryRowSelected) \
  E_CPONLY(kMahoColorLibrarySectionHeaderText) \
  E_CPONLY(kMahoColorLibraryGlyphBorder) \
  E_CPONLY(kMahoColorLibrarySearchPillBackground) \
  E_CPONLY(kMahoColorLibraryProgressFill) \
  /* Archive */ \
  E_CPONLY(kMahoColorArchivePanelBackground) \
  E_CPONLY(kMahoColorArchiveControlBackground) \
  E_CPONLY(kMahoColorArchiveControlBorder) \
  E_CPONLY(kMahoColorArchiveControlActiveBackground) \
  E_CPONLY(kMahoColorArchiveRowHover) \
  E_CPONLY(kMahoColorArchiveRowSelected) \
  E_CPONLY(kMahoColorArchiveSectionHeaderText) \
  E_CPONLY(kMahoColorArchiveDivider) \
  E_CPONLY(kMahoColorArchiveDetailPanelBackground) \
  E_CPONLY(kMahoColorArchiveDetailPanelInnerBackground) \
  E_CPONLY(kMahoColorArchiveDetailPanelBorder) \
  E_CPONLY(kMahoColorArchiveDetailPanelMutedFill) \
  E_CPONLY(kMahoColorArchiveDetailPanelAccent) \
  /* Library Rail */ \
  E_CPONLY(kMahoColorLibraryRailBackground) \
  E_CPONLY(kMahoColorLibraryRailItemSelected) \
  E_CPONLY(kMahoColorLibraryRailItemHover) \
  E_CPONLY(kMahoColorLibraryRailItemText) \
  E_CPONLY(kMahoColorLibraryRailItemSelectedText) \
  E_CPONLY(kMahoColorLibraryRailDivider) \
  /* Text */ \
  E_CPONLY(kMahoColorPrimaryText) \
  E_CPONLY(kMahoColorSecondaryText) \
  E_CPONLY(kMahoColorTertiaryText) \
  /* Sidebar Text */ \
  E_CPONLY(kMahoColorSidebarPrimaryText) \
  E_CPONLY(kMahoColorSidebarSecondaryText) \
  E_CPONLY(kMahoColorSidebarTertiaryText) \
  /* Legacy Aliases */ \
  E_CPONLY(kMahoColorDivider) \
  E_CPONLY(kMahoColorFooterDivider) \
  /* Command Bar */ \
  E_CPONLY(kMahoColorCommandBarBackground) \
  E_CPONLY(kMahoColorCommandBarPanelBorder) \
  E_CPONLY(kMahoColorCommandBarInputFill) \
  E_CPONLY(kMahoColorCommandBarInputBorder) \
  E_CPONLY(kMahoColorCommandBarSelectionFill) \
  E_CPONLY(kMahoColorCommandBarSelectionBorder) \
  E_CPONLY(kMahoColorCommandBarKeycapBorder) \
  E_CPONLY(kMahoColorCommandBarInputSelection) \
  E_CPONLY(kMahoColorCommandBarEmptyText) \
  E_CPONLY(kMahoColorCommandBarShortcutBadge) \
  E_CPONLY(kMahoColorCommandBarShortcutText) \
  E_CPONLY(kMahoColorCommandBarDivider) \
  E_CPONLY(kMahoColorCommandBarDividerHairline) \
  E_CPONLY(kMahoColorCommandBarRowTitleSelected) \
  E_CPONLY(kMahoColorCommandBarRowSubtitleSelected) \
  E_CPONLY(kMahoColorCommandBarSparkleOuter) \
  E_CPONLY(kMahoColorCommandBarSparkleInner) \
  E_CPONLY(kMahoColorCommandGlyphTab) \
  E_CPONLY(kMahoColorCommandGlyphBookmark) \
  E_CPONLY(kMahoColorCommandGlyphHistory) \
  E_CPONLY(kMahoColorCommandGlyphAction) \
  E_CPONLY(kMahoColorCommandGlyphNavigation) \
  E_CPONLY(kMahoColorCommandGlyphSearch) \
  E_CPONLY(kMahoColorCommandGlyphCalculator) \
  E_CPONLY(kMahoColorCommandGlyphUnitConversion) \
  E_CPONLY(kMahoColorCommandGlyphArchivedTab) \
  E_CPONLY(kMahoColorCommandGlyphClosedTab) \
  E_CPONLY(kMahoColorCommandGlyphFolder) \
  E_CPONLY(kMahoColorCommandGlyphRecentSearch) \
  /* Accent & Semantic */ \
  E_CPONLY(kMahoColorAccentBlue) \
  E_CPONLY(kMahoColorAccentBlueBright) \
  E_CPONLY(kMahoColorSelectionHighlight) \
  E_CPONLY(kMahoColorFolderIcon) \
  E_CPONLY(kMahoColorDangerRed) \
  E_CPONLY(kMahoColorSuccessGreen) \
  /* Reader Mode */ \
  E_CPONLY(kMahoColorReaderLightBackground) \
  E_CPONLY(kMahoColorReaderLightText) \
  E_CPONLY(kMahoColorReaderSepiaBackground) \
  E_CPONLY(kMahoColorReaderSepiaText) \
  E_CPONLY(kMahoColorReaderDarkBackground) \
  E_CPONLY(kMahoColorReaderDarkText) \
  E_CPONLY(kMahoColorDropIndicator) \
  /* Toast Notification */ \
  E_CPONLY(kMahoColorToastBackground) \
  E_CPONLY(kMahoColorToastForeground) \
  E_CPONLY(kMahoColorToastBodyForeground) \
  E_CPONLY(kMahoColorToastBorder) \
  /* Private / Incognito semantic colors (R-12) */ \
  E_CPONLY(kMahoColorPrivateSidebarBackground) \
  E_CPONLY(kMahoColorPrivateSidebarText) \
  E_CPONLY(kMahoColorPrivateTabRowBackground) \
  E_CPONLY(kMahoColorPrivateTabRowHover) \
  E_CPONLY(kMahoColorPrivateSearchBackground) \
  E_CPONLY(kMahoColorPrivateSearchBorder) \
  E_CPONLY(kMahoColorPrivateFooterBackground) \
  E_CPONLY(kMahoColorPrivateIdentityIcon) \
  E_CPONLY(kMahoColorPrivateTopBarBackground)
// clang-format on

#include "ui/color/color_id_macros.inc"

enum MahoColorIds : ui::ColorId {
  kMahoColorsStart = kChromeColorsEnd,

  MAHO_COLOR_IDS

  kMahoColorsEnd,
};

#include "ui/color/color_id_macros.inc"  // NOLINT(build/include)

#endif  // MAHO_BROWSER_UI_THEME_MAHO_COLOR_ID_H_
