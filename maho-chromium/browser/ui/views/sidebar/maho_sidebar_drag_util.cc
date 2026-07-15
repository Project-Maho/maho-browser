// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_drag_util.h"

#include "base/check.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/canvas_painter.h"
#include "ui/compositor/paint_recorder.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/paint_info.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

float GetSidebarDragImageScaleFactor(views::View* view) {
  if (view) {
    if (views::Widget* widget = view->GetWidget()) {
      if (widget->GetCompositor()) {
        return widget->GetCompositor()->device_scale_factor();
      }

      if (const display::Screen* screen = display::Screen::Get()) {
        return screen->GetDisplayNearestView(widget->GetNativeView())
            .device_scale_factor();
      }
    }
  }

  return 1.0f;
}

}  // namespace

gfx::ImageSkia CreateSidebarDragImage(views::View* view) {
  const gfx::Size size =
      (view && !view->size().IsEmpty()) ? view->size() : gfx::Size(1, 1);
  const float scale_factor = GetSidebarDragImageScaleFactor(view);

  SkBitmap bitmap;
  {
    ui::CanvasPainter canvas_painter(&bitmap, size, scale_factor,
                                     SK_ColorTRANSPARENT, true);
    if (view) {
      view->Paint(views::PaintInfo::CreateRootPaintInfo(
          canvas_painter.context(), size));
    }
  }
  gfx::ImageSkia drag_image =
      gfx::ImageSkia::CreateFromBitmap(bitmap, scale_factor);
  if (drag_image.isNull() || drag_image.size().IsEmpty()) {
    SkBitmap fallback_bitmap;
    fallback_bitmap.allocN32Pixels(1, 1, true);
    fallback_bitmap.eraseColor(SK_ColorTRANSPARENT);
    drag_image = gfx::ImageSkia::CreateFromBitmap(fallback_bitmap, 1.0f);
  }
  return drag_image;
}

gfx::ImageSkia CreateSidebarDragImageWithBackground(views::View* view,
                                                     SkColor base_color,
                                                     SkColor overlay_color,
                                                     float corner_radius_dp) {
  gfx::ImageSkia inner = CreateSidebarDragImage(view);
  if (inner.isNull() || inner.size().IsEmpty()) {
    return inner;
  }
  const gfx::Size size = inner.size();
  const float scale_factor = GetSidebarDragImageScaleFactor(view);
  const int pw = std::max(1, static_cast<int>(size.width() * scale_factor));
  const int ph = std::max(1, static_cast<int>(size.height() * scale_factor));

  SkBitmap bitmap;
  bitmap.allocN32Pixels(pw, ph, true);
  bitmap.eraseColor(SK_ColorTRANSPARENT);
  {
    SkCanvas sk_canvas(bitmap);
    sk_canvas.scale(scale_factor, scale_factor);
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kFill_Style);
    const SkRect rect = SkRect::MakeIWH(size.width(), size.height());
    paint.setColor(SkColorSetA(base_color, 255));
    sk_canvas.drawRoundRect(rect, corner_radius_dp, corner_radius_dp, paint);
    paint.setColor(overlay_color);
    sk_canvas.drawRoundRect(rect, corner_radius_dp, corner_radius_dp, paint);
    const SkBitmap inner_bitmap = *inner.bitmap();
    sk_canvas.drawImage(inner_bitmap.asImage(), 0, 0);
  }
  return gfx::ImageSkia::CreateFromBitmap(bitmap, scale_factor);
}

gfx::ImageSkia CreateTransparentPixelDragImage() {
  SkBitmap bmp;
  bmp.allocN32Pixels(1, 1, true);
  bmp.eraseColor(SK_ColorTRANSPARENT);
  return gfx::ImageSkia::CreateFromBitmap(bmp, 1.f);
}

}  // namespace maho
