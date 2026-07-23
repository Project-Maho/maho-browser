// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_THEMED_BACKGROUND_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_THEMED_BACKGROUND_H_

#include "third_party/skia/include/core/SkPoint.h"

namespace gfx {
class Canvas;
class Rect;
}  // namespace gfx

namespace ui {
class ColorProvider;
}  // namespace ui

namespace maho {

// Paints the space-theme-tinted sidebar background (a 1-3 stop linear gradient,
// top-left -> bottom-right, resolved from MahoSpaceThemeState) into `bounds`,
// clipped to the per-corner `radii`. When `opaque` is true every stop color is
// forced to full alpha (0xFF) so subpixel text composited above stays legal and
// no web content bleeds through; when false the themed colors keep their native
// alpha (the translucent glass-frame look). No-op if `provider` is null or
// `bounds` is empty. Shared by the docked sidebar and the fixed-width library
// overlay so both columns follow the same theme.
//
// `gradient_span_dp` bounds the diagonal over which the gradient stops are laid
// out: 0 (default) spans the full `bounds` (TL->BR) — the original behavior for
// the rail and the narrow library overlay; a positive value lays the stops over
// a `span x span` diagonal anchored at the top-left, with SkTileMode::kClamp
// holding the final stop color beyond it. Used by the full-viewport Spaces
// overlay so its tint density is not washed out across a very wide surface.
void PaintMahoSidebarThemedBackground(gfx::Canvas* canvas,
                                      const gfx::Rect& bounds,
                                      const ui::ColorProvider* provider,
                                      bool opaque,
                                      const SkVector radii[4],
                                      int gradient_span_dp = 0);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_THEMED_BACKGROUND_H_
