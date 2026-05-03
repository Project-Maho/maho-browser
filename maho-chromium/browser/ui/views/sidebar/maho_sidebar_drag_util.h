// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DRAG_UTIL_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DRAG_UTIL_H_

#include "ui/gfx/image/image_skia.h"

namespace views {
class View;
}

namespace maho {

gfx::ImageSkia CreateSidebarDragImage(views::View* view);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DRAG_UTIL_H_
