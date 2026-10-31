// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_themed_background.h"

#include <algorithm>
#include <cstddef>

#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"

namespace maho {

void PaintMahoSidebarThemedBackground(gfx::Canvas* canvas,
                                      const MahoSidebarPalette& palette,
                                      const gfx::Rect& bounds,
                                      bool opaque,
                                      const SkVector radii[4],
                                      int gradient_span_dp,
                                      const gfx::Rect* gradient_bounds) {
  // When no text color is readable on the translucent stops over every
  // backdrop, the palette resolved its roles against the opaque stops only
  // (`used_surface_fallback`). Paint those opaque stops so the surface under
  // the text is the surface the text was resolved for.
  const bool paint_opaque = opaque || palette.used_surface_fallback;
  const std::vector<SkColor>& colors =
      paint_opaque ? palette.opaque_contrast_stops : palette.surface_stops;
  if (bounds.IsEmpty() || colors.empty()) {
    return;
  }

  SkRRect rrect;
  rrect.setRectRadii(gfx::RectToSkRect(bounds), radii);
  const SkPath clip_path = SkPath::RRect(rrect);

  const int stop_count = static_cast<int>(
      std::min<size_t>(colors.size(), static_cast<size_t>(3)));

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);

  if (stop_count <= 1) {
    flags.setColor(colors.front());
  } else {
    const SkColor4f gradient_colors[3] = {
        SkColor4f::FromColor(colors[0]),
        SkColor4f::FromColor(colors[1]),
        SkColor4f::FromColor(stop_count == 3 ? colors[2] : colors[1]),
    };
    const SkScalar stops_two[2] = {0.0f, 1.0f};
    const SkScalar stops_three[3] = {0.0f, 0.5f, 1.0f};
    // Subtle top-left -> bottom-right diagonal; reads mostly vertical on the
    // tall, narrow sidebar while keeping Arc/Zen lateral variation. A positive
    // `gradient_span_dp` bounds the diagonal to a `span x span` box anchored at
    // the top-left (kClamp holds the last stop beyond it) so a very wide surface
    // (full-viewport Spaces overlay) keeps the rail's tint density.
    const gfx::Rect& shader_bounds = gradient_bounds ? *gradient_bounds : bounds;
    const float span =
        gradient_span_dp > 0 ? static_cast<float>(gradient_span_dp) : 0.0f;
    const SkPoint points[2] = {
        SkPoint::Make(shader_bounds.x(), shader_bounds.y()),
        span > 0.0f
            ? SkPoint::Make(shader_bounds.x() + span,
                            shader_bounds.y() + span)
            : SkPoint::Make(shader_bounds.right(), shader_bounds.bottom()),
    };
    flags.setShader(cc::PaintShader::MakeLinearGradient(
        points, gradient_colors, stop_count == 2 ? stops_two : stops_three,
        stop_count, SkTileMode::kClamp));
  }

  canvas->Save();
  canvas->ClipPath(clip_path, true);
  canvas->DrawRect(gfx::RectF(bounds), flags);
  canvas->Restore();
}

}  // namespace maho
