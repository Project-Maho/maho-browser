#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_SPARKLE_GEOMETRY_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_SPARKLE_GEOMETRY_H_

#include <array>

#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"

namespace maho::command_palette {

constexpr std::array<gfx::PointF, 8> kSparkleOuterPoints = {{
    {12.0f, 2.6f},
    {14.3f, 8.2f},
    {20.0f, 10.5f},
    {14.3f, 12.8f},
    {12.0f, 18.4f},
    {9.7f, 12.8f},
    {4.0f, 10.5f},
    {9.7f, 8.2f},
}};

constexpr std::array<gfx::PointF, 8> kSparkleInnerPoints = {{
    {12.0f, 6.5f},
    {13.5f, 10.2f},
    {17.2f, 11.7f},
    {13.5f, 13.2f},
    {12.0f, 16.9f},
    {10.5f, 13.2f},
    {6.8f, 11.7f},
    {10.5f, 10.2f},
}};

inline gfx::RectF MakeSparkleRect(const gfx::Rect& rect) {
  return gfx::RectF(rect.x() + rect.width() * 0.08f,
                    rect.y() + rect.height() * 0.08f,
                    rect.width() * 0.84f,
                    rect.height() * 0.84f);
}

inline gfx::RectF MakeSparkleRect(const gfx::RectF& rect) {
  return gfx::RectF(rect.x() + rect.width() * 0.08f,
                    rect.y() + rect.height() * 0.08f,
                    rect.width() * 0.84f,
                    rect.height() * 0.84f);
}

inline SkPath BuildSparklePath(const gfx::RectF& rect,
                               const std::array<gfx::PointF, 8>& points) {
  auto map_point = [&](const gfx::PointF& point) {
    return SkPoint::Make(rect.x() + rect.width() * (point.x() / 24.0f),
                         rect.y() + rect.height() * (point.y() / 24.0f));
  };

  SkPathBuilder builder;
  builder.moveTo(map_point(points.front()));
  for (size_t i = 1; i < points.size(); ++i) {
    builder.lineTo(map_point(points[i]));
  }
  builder.close();
  return builder.detach();
}

}  // namespace maho::command_palette

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_SPARKLE_GEOMETRY_H_
