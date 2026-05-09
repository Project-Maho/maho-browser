// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LAYOUT_TOKENS_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LAYOUT_TOKENS_H_

#include <cstddef>

#include "ui/gfx/geometry/insets.h"

namespace maho::sidebar_layout {

inline constexpr int kCollapsedRailWidthDp = 40;
inline constexpr int kDefaultRailWidthDp = 224;
inline constexpr int kRuntimeRailWidthMinDp = 176;
inline constexpr int kRuntimeRailWidthMaxDp = 400;
inline constexpr int kLegacyRailWidthResetThresholdDp = 80;
inline constexpr int kCompactNavigationThresholdDp = 208;
inline constexpr int kRailCornerRadiusDp = 18;
inline constexpr int kSectionCornerRadiusDp = 12;
inline constexpr int kSectionSpacingDp = 5;
inline constexpr int kTopRegionSpacingDp = 6;
// Width reserved for macOS native traffic-light buttons (close/min/zoom).
// Derived from upstream BrowserFrameViewMac::GetCaptionButtonBounds():
//   pre-macOS 26: RectF(20, 11, 54, 16) → right edge x=74
//   macOS 26+:    RectF(12, 10, 62, 18) → right edge x=74
// Spacer starts at x = kRailInsets.left() (10dp), so width = 74 − 10.
inline constexpr int kTrafficLightAlignmentWidthDp = 64;
inline constexpr int kTopBarTrafficLightAlignmentHeightDp = 22;
inline constexpr int kTopBarButtonSizeDp = 22;
inline constexpr int kTopBarButtonCornerRadiusDp = 8;
inline constexpr int kTopBarIconSizeDp = 15;
inline constexpr int kTopBarButtonSpacingDp = 1;
inline constexpr int kTopBarCompactLeadingGapDp = 4;
inline constexpr int kTopBarCompactNavSpacingDp = 4;
inline constexpr int kTopBarCompactTrailingInsetDp = 3;
inline constexpr int kSearchPillHeightDp = 38;
inline constexpr int kSearchPillCornerRadiusDp = 16;
inline constexpr int kSearchPillCompactHeightDp = 30;
inline constexpr int kSearchPillCompactCornerRadiusDp = 15;
inline constexpr int kSearchPillIconSizeDp = 14;
inline constexpr std::size_t kMahoSidebarFavoriteSlotCount = 10;
inline constexpr int kFavoritesColumns = 3;
inline constexpr int kFavoriteTileHeightDp = 47;
inline constexpr int kFavoriteTileCornerRadiusDp = 12;
inline constexpr int kFavoriteTileSpacingDp = 6;
inline constexpr int kFavoriteGlyphContainerSizeDp = 28;
inline constexpr int kFavoriteGlyphContainerRadiusDp = 10;
inline constexpr int kFavoriteGlyphSizeDp = 15;
inline constexpr int kFavoriteLabelMaxWidthDp = 60;
inline constexpr int kHoverTriggerWidthDp = 2;      // Invisible trigger zone width
inline constexpr int kSidebarRevealDurationMs = 250; // Reveal animation duration
inline constexpr int kSidebarHideDelayMs = 300;      // Delay before hide on mouse exit

inline const gfx::Insets kRailInsets = gfx::Insets::TLBR(8, 8, 8, 8);
inline const gfx::Insets kSectionInsets = gfx::Insets::TLBR(2, 0, 2, 0);
inline const gfx::Insets kRowInsets = gfx::Insets::TLBR(2, 5, 2, 5);
inline const gfx::Insets kTopBarInsets = gfx::Insets::TLBR(0, 1, 0, 0);
inline const gfx::Insets kSearchPillInsets = gfx::Insets::TLBR(0, 2, 0, 2);
inline const gfx::Insets kFavoriteTileInsets = gfx::Insets::TLBR(6, 4, 6, 4);

}  // namespace maho::sidebar_layout

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LAYOUT_TOKENS_H_
