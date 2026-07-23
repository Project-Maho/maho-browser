// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_themed_background.h"

#include <algorithm>
#include <cstddef>
#include <optional>

#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"

namespace maho {

namespace {

SkColor MaybeForceOpaque(SkColor color, bool opaque) {
  return opaque ? SkColorSetA(color, 0xFF) : color;
}

}  // namespace

void PaintMahoSidebarThemedBackground(gfx::Canvas* canvas,
                                      const gfx::Rect& bounds,
                                      const ui::ColorProvider* provider,
                                      bool opaque,
                                      const SkVector radii[4],
                                      int gradient_span_dp) {
  if (!provider || bounds.IsEmpty()) {
    return;
  }

  SkRRect rrect;
  rrect.setRectRadii(gfx::RectToSkRect(bounds), radii);
  const SkPath clip_path = SkPath::RRect(rrect);

  // Decide how many themed stops to render (1 = solid).
  std::optional<MahoSpaceThemeState::ThemeData> theme_data =
      MahoSpaceThemeState::GetThemeData();
  int stop_count = 1;
  if (theme_data && !theme_data->achromatic &&
      (theme_data->kind == MahoSpaceThemeState::ThemeKind::kGradient ||
       theme_data->kind == MahoSpaceThemeState::ThemeKind::kZen)) {
    stop_count = static_cast<int>(
        std::min<size_t>(theme_data->stops.size(), static_cast<size_t>(3)));
    if (stop_count < 1) {
      stop_count = 1;
    }
  }

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);

  if (stop_count <= 1) {
    flags.setColor(MaybeForceOpaque(
        provider->GetColor(kMahoColorSidebarBackground), opaque));
  } else {
    const SkColor4f colors[3] = {
        SkColor4f::FromColor(MaybeForceOpaque(
            provider->GetColor(kMahoColorSidebarBackground), opaque)),
        SkColor4f::FromColor(MaybeForceOpaque(
            provider->GetColor(kMahoColorSidebarGradientStop1), opaque)),
        SkColor4f::FromColor(MaybeForceOpaque(
            provider->GetColor(kMahoColorSidebarGradientStop2), opaque)),
    };
    const SkScalar stops_two[2] = {0.0f, 1.0f};
    const SkScalar stops_three[3] = {0.0f, 0.5f, 1.0f};
    // Subtle top-left -> bottom-right diagonal; reads mostly vertical on the
    // tall, narrow sidebar while keeping Arc/Zen lateral variation. A positive
    // `gradient_span_dp` bounds the diagonal to a `span x span` box anchored at
    // the top-left (kClamp holds the last stop beyond it) so a very wide surface
    // (full-viewport Spaces overlay) keeps the rail's tint density.
    const float span =
        gradient_span_dp > 0 ? static_cast<float>(gradient_span_dp) : 0.0f;
    const SkPoint points[2] = {
        SkPoint::Make(bounds.x(), bounds.y()),
        span > 0.0f
            ? SkPoint::Make(bounds.x() + span, bounds.y() + span)
            : SkPoint::Make(bounds.right(), bounds.bottom()),
    };
    flags.setShader(cc::PaintShader::MakeLinearGradient(
        points, colors, stop_count == 2 ? stops_two : stops_three, stop_count,
        SkTileMode::kClamp));
  }

  canvas->Save();
  canvas->ClipPath(clip_path, true);
  canvas->DrawRect(gfx::RectF(bounds), flags);
  canvas->Restore();
}

}  // namespace maho
