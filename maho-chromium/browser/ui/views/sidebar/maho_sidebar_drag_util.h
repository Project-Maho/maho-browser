// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DRAG_UTIL_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DRAG_UTIL_H_

#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/image/image_skia.h"

namespace views {
class View;
}

namespace maho {

gfx::ImageSkia CreateSidebarDragImage(views::View* view);

gfx::ImageSkia CreateSidebarDragImageWithBackground(views::View* view,
                                                     SkColor base_color,
                                                     SkColor overlay_color,
                                                     float corner_radius_dp);

gfx::ImageSkia CreateTransparentPixelDragImage();

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DRAG_UTIL_H_
