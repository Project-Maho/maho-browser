// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_drag_util.h"

#include "base/check.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/canvas_painter.h"
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

}  // namespace maho
