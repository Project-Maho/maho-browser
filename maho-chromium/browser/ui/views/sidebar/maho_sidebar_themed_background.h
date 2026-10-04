// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_THEMED_BACKGROUND_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_THEMED_BACKGROUND_H_

#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "third_party/skia/include/core/SkPoint.h"

namespace gfx {
class Canvas;
class Rect;
}  // namespace gfx

namespace maho {

// Paints an immutable browser-local palette snapshot as a 1-3 stop linear
// gradient, clipped to the per-corner `radii`. `opaque` selects the palette's
// opaque reference stops; otherwise actual-alpha surface stops are used.
//
// `gradient_span_dp` bounds the diagonal over which the gradient stops are laid
// out: 0 (default) spans the full `bounds` (TL->BR) — the original behavior for
// the rail and the narrow library overlay; a positive value lays the stops over
// a `span x span` diagonal anchored at the top-left, with SkTileMode::kClamp
// holding the final stop color beyond it. Used by the full-viewport Spaces
// overlay so its tint density is not washed out across a very wide surface.
void PaintMahoSidebarThemedBackground(gfx::Canvas* canvas,
                                      const MahoSidebarPalette& palette,
                                      const gfx::Rect& bounds,
                                      bool opaque,
                                      const SkVector radii[4],
                                      int gradient_span_dp = 0,
                                      const gfx::Rect* gradient_bounds = nullptr);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_THEMED_BACKGROUND_H_
