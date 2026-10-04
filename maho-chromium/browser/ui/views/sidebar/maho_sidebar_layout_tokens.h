// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LAYOUT_TOKENS_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LAYOUT_TOKENS_H_

#include <cstddef>

#include "ui/gfx/geometry/insets.h"

namespace maho::sidebar_layout {

// A collapsed sidebar is fully hidden and must not reserve content width.
inline constexpr int kCollapsedRailWidthDp = 0;
// Library mode still spends width on root insets, the 64dp category rail, and
// a 1dp divider before archive/download/media content, but the top-left shell
// reservation now comes from the library structure itself rather than from an
// oversized global width floor. Keep only a modest minimum so the library pane
// still has usable room at narrow persisted widths.
inline constexpr int kDefaultRailWidthDp = 288;
inline constexpr int kRuntimeRailWidthMinDp = 280;
inline constexpr int kRuntimeRailWidthMaxDp = 1600;
inline constexpr int kLegacyRailWidthResetThresholdDp = 80;
inline constexpr int kCompactNavigationThresholdDp = 208;
inline constexpr int kRailCornerRadiusDp = 18;
inline constexpr int kSectionCornerRadiusDp = 12;
inline constexpr int kSectionSpacingDp = 7;
inline constexpr int kTopRegionSpacingDp = 8;
// Width reserved for macOS native traffic-light buttons (close/min/zoom).
// Derived from upstream BrowserFrameViewMac::GetCaptionButtonBounds():
//   pre-macOS 26: RectF(20, 11, 54, 16) → right edge x=74
//   macOS 26+:    RectF(12, 10, 62, 18) → right edge x=74
// Spacer starts at x = kRailInsets.left() (10dp), so width = 74 − 10.
inline constexpr int kTrafficLightAlignmentWidthDp = 64;
inline constexpr int kTopBarTrafficLightAlignmentHeightDp = 20;
// Top-bar action button hit target (width × height).
inline constexpr int kTopBarButtonSizeDp = 20;
inline constexpr int kTopBarButtonCornerRadiusDp = 8;
// Rendered icon size within the button hit target.
inline constexpr int kTopBarIconSizeDp = 16;
// Gap between adjacent buttons in a cluster (tight packing).
inline constexpr int kTopBarButtonSpacingDp = 0;
// Extra gap after the sidebar-toggle button when navigation is active,
// visually separating the toggle from back/forward/reload.
inline constexpr int kTopBarCompactLeadingGapDp = 2;
inline constexpr int kTopBarCompactNavSpacingDp = 4;
inline constexpr int kTopBarCompactTrailingInsetDp = 3;
// Control activity indicator chip. Sized against the existing top-bar and
// search-pill scale so the chip reads as part of the same sidebar system
// rather than a bolted-on surface.
inline constexpr int kControlIndicatorHeightDp = 32;
inline constexpr int kControlIndicatorCompactWidthDp = 76;
inline constexpr int kControlIndicatorCornerRadiusDp = 10;
inline constexpr int kControlIndicatorIconSizeDp = 16;
inline constexpr int kControlIndicatorHorizontalInsetDp = 8;
inline constexpr int kControlIndicatorVerticalInsetDp = 4;
inline constexpr int kControlIndicatorItemSpacingDp = 6;
// Floor for the elided status/target text column before the chip falls back to
// its compact form. Keeps controller identity readable at narrow widths.
inline constexpr int kControlIndicatorMinTextColumnDp = 64;

inline constexpr int kLibraryRailWidthDp = 92;
// Fixed content width of the floating Downloads/Archive library overlay. The
// overlay widget is pinned to this width regardless of the sidebar's persisted
// pref width, so the pane never resizes with the sidebar (Arc-style panel).
inline constexpr int kLibraryOverlayContentWidthDp = 380;
// Canonical diagonal span (dp) over which the sidebar palette's gradient stops
// are laid out, shared by every surface that paints that palette: the docked
// rail, the Downloads/Archive library overlay, and the full-viewport Spaces
// overlay. With one span, one palette resolves to one tint at the same screen
// position. A per-surface span (the rail's own variable width, the overlay's
// 380dp column) stretches the identical stops differently and makes two
// surfaces of the same Space theme read as different shades. Anchored at the
// library-overlay content width, the narrowest surface that hosts full palette
// content, so the rail's tint density matches the panel it opens into.
inline constexpr int kThemedBackgroundGradientSpanDp =
    kLibraryOverlayContentWidthDp;
inline constexpr int kLibraryContentSeparatorThicknessDp = 1;
inline constexpr int kLibraryRailTopInsetDp = 14;
inline constexpr int kLibraryRailSideInsetDp = 0;
inline constexpr int kLibraryRailBottomInsetDp = 14;
inline constexpr int kLibraryRailUtilitySpacingDp = 8;
inline constexpr int kLibraryRailUtilityButtonSizeDp = 24;
inline constexpr int kLibraryRailUtilityButtonCornerRadiusDp = 10;
inline constexpr int kLibraryRailItemWidthDp = 80;
// Extra margin reserved on the right edge of the icon rail; the centered items
// shift left by this amount for more breathing room on the right. Tunable.
inline constexpr int kLibraryRailRightMarginDp = 8;
inline constexpr int kLibraryRailItemCornerRadiusDp = 12;
inline constexpr int kLibraryRailItemSpacingDp = 4;
inline constexpr int kLibraryRailItemStackVisualLiftDp = 28;
inline constexpr int kLibraryRailItemVerticalInsetDp = 8;
inline constexpr int kLibraryRailItemHorizontalInsetDp = 6;
inline constexpr int kLibraryRailItemContentWidthDp =
    kLibraryRailItemWidthDp - (kLibraryRailItemHorizontalInsetDp * 2);
inline constexpr int kLibraryRailTileCornerRadiusDp = 12;
inline constexpr int kLibraryRailIconSizeDp = 24;
inline constexpr int kLibraryRailIconLabelSpacingDp = 4;
inline constexpr int kArchiveEmptyStateIconSizeDp = 28;
inline constexpr int kArchiveEmptyStateContentMaxWidthDp = 252;
inline constexpr int kArchiveEmptyStateItemSpacingDp = 8;
inline constexpr int kSearchPillHeightDp = 35;
inline constexpr int kSearchPillCornerRadiusDp = 10;
inline constexpr int kSearchPillCompactHeightDp = 32;
inline constexpr int kSearchPillCompactCornerRadiusDp = 12;
inline constexpr int kSearchPillIconSizeDp = 16;
inline constexpr std::size_t kMahoSidebarFavoriteSlotCount = 12;
inline constexpr int kFavoritesColumns = 4;
inline constexpr int kFavoriteTileHeightDp = 47;
inline constexpr int kFavoriteTileCornerRadiusDp = 12;
inline constexpr int kFavoriteTileSpacingDp = 6;
inline constexpr int kFavoriteGlyphContainerSizeDp = 28;
inline constexpr int kFavoriteGlyphContainerRadiusDp = 10;
inline constexpr int kFavoriteGlyphSizeDp = 16;
inline constexpr int kFavoriteLabelMaxWidthDp = 60;
inline constexpr int kSpacesScrollPaddingDp = 12;
inline constexpr int kHoverTriggerWidthDp = 2;      // Invisible trigger zone width
inline constexpr int kSidebarRevealDurationMs = 250; // Reveal animation duration
inline constexpr int kSidebarHideDelayMs = 300;      // Delay before hide on mouse exit

// Sidebar root vertical layout inset. The top inset only pushes the top-bar
// row down from y=0; the row's own top padding is computed at runtime from
// the live NSWindow traffic-light position, not from any constant here.
inline constexpr int kRailInsetTopDp = 6;
inline constexpr int kRailInsetLeftDp = 8;
inline constexpr int kRailInsetBottomDp = 8;
inline constexpr int kRailInsetRightDp = 8;
inline const gfx::Insets kRailInsets =
    gfx::Insets::TLBR(kRailInsetTopDp, kRailInsetLeftDp,
                      kRailInsetBottomDp, kRailInsetRightDp);
inline const gfx::Insets kSectionInsets = gfx::Insets::TLBR(2, 0, 2, 0);
inline constexpr int kContentInsetDp = 4;
inline const gfx::Insets kRowInsets = gfx::Insets::TLBR(2, 7, 2, 7);
// Top-bar row padding. Vertical top inset is 0 at construction; the row's
// action-icon center Y is set at runtime by MahoSidebarTopBarView from the
// live NSWindow traffic-light position (see maho_traffic_light_geometry.h).
// No compile-time invariant is asserted here — runtime measurement is the
// sole authority for vertical alignment against macOS window controls.
inline constexpr int kTopBarInsetTopDp = 0;
inline constexpr int kTopBarInsetLeftDp = 1;
inline constexpr int kTopBarInsetBottomDp = 1;
inline constexpr int kTopBarInsetRightDp = 1;
inline const gfx::Insets kTopBarInsets =
    gfx::Insets::TLBR(kTopBarInsetTopDp, kTopBarInsetLeftDp,
                      kTopBarInsetBottomDp, kTopBarInsetRightDp);
inline const gfx::Insets kSearchPillInsets = gfx::Insets::TLBR(0, 2, 0, 2);
inline const gfx::Insets kFavoriteTileInsets = gfx::Insets::TLBR(6, 4, 6, 4);

}  // namespace maho::sidebar_layout

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LAYOUT_TOKENS_H_
