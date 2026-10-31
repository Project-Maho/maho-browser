// Copyright 2026 Maho Browser. All rights reserved.

// Non-macOS implementation. Traffic lights are a macOS-only concept, so
// GetTrafficLightCenterYInView() always returns std::nullopt here and
// MahoSidebarTopBarView leaves the BoxLayout top inset at its constructor
// default (kTopBarInsetTopDp = 0). GetWindowControlsRect(s)InView() measures
// the Windows/Linux caption buttons through BrowserFrameView. This TU exists
// so the sidebar links on every platform without a per-platform GN branch in
// every consumer.

#include "maho/browser/ui/views/sidebar/maho_traffic_light_geometry.h"

#include <utility>

#include "base/no_destructor.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_params.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size_f.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

namespace maho {

namespace {

// std::nullopt means "no override installed - measure normally". A present
// value means an override is installed; GetWindowControlsRectsInView()
// returns it verbatim (an empty vector simulates "no window controls"). See
// SetWindowControlsRect(s)ForTesting() / ClearWindowControlsRectForTesting().
std::optional<std::vector<gfx::Rect>>& GetTestingWindowControlsRectsOverride() {
  static base::NoDestructor<std::optional<std::vector<gfx::Rect>>>
      override_rects;
  return *override_rects;
}

}  // namespace

std::optional<int> GetTrafficLightCenterYInView(const views::View* /*view*/) {
  return std::nullopt;
}

std::vector<gfx::Rect> GetWindowControlsRectsFromLayoutParams(
    const BrowserLayoutParams& params) {
  std::vector<gfx::Rect> rects;
  const gfx::Rect& area = params.visual_client_area;
  if (!params.leading_exclusion.IsEmpty()) {
    const gfx::SizeF size = params.leading_exclusion.ContentWithPadding();
    rects.push_back(gfx::ToEnclosingRect(
        gfx::RectF(area.x(), area.y(), size.width(), size.height())));
  }
  if (!params.trailing_exclusion.IsEmpty()) {
    const gfx::SizeF size = params.trailing_exclusion.ContentWithPadding();
    rects.push_back(gfx::ToEnclosingRect(gfx::RectF(
        area.right() - size.width(), area.y(), size.width(), size.height())));
  }
  return rects;
}

// Windows/Linux caption buttons.
//
// The exclusions come from the public virtual
// `BrowserFrameView::GetBrowserLayoutParams()`, the same data upstream
// BrowserView layout uses to keep the tab strip clear of the frame controls.
// Every browser frame view implements it safely:
//   - BrowserFrameViewWin builds it from caption_button_container_->bounds()
//     (trailing, or leading in native-titlebar RTL; the window icon adds a
//     leading exclusion).
//   - BrowserFrameViewLinux(Native) unions the visible leading and trailing
//     frame buttons separately, because Linux window managers can place
//     buttons on both edges.
//   - OpaqueBrowserFrameView and the base class fall back to
//     GetCaptionButtonBounds() internally.
// Do NOT call the protected `GetCaptionButtonBounds()` directly: on Linux it
// is a fatal NOTREACHED() (browser_frame_view_linux.cc), which would crash on
// the first header layout. The exclusions are in frame-view coordinates, so
// each rect is converted from the frame view into `view`. Non-browser frames
// (e.g. dialogs) yield no rects.
std::vector<gfx::Rect> GetWindowControlsRectsInView(const views::View* view) {
  if (GetTestingWindowControlsRectsOverride().has_value()) {
    return *GetTestingWindowControlsRectsOverride();
  }
  if (!view) {
    return {};
  }
  const views::Widget* widget = view->GetWidget();
  if (!widget || !widget->non_client_view()) {
    return {};
  }
  const BrowserFrameView* frame_view = views::AsViewClass<BrowserFrameView>(
      widget->non_client_view()->frame_view());
  if (!frame_view) {
    return {};
  }
  std::vector<gfx::Rect> rects = GetWindowControlsRectsFromLayoutParams(
      frame_view->GetBrowserLayoutParams());
  for (gfx::Rect& rect : rects) {
    rect = views::View::ConvertRectToTarget(frame_view, view, rect);
  }
  return rects;
}

std::optional<gfx::Rect> GetWindowControlsRectInView(const views::View* view) {
  const std::vector<gfx::Rect> rects = GetWindowControlsRectsInView(view);
  if (rects.empty()) {
    return std::nullopt;
  }
  gfx::Rect united;
  for (const gfx::Rect& rect : rects) {
    united.Union(rect);
  }
  return united;
}

void SetWindowControlsRectForTesting(std::optional<gfx::Rect> rect) {
  std::vector<gfx::Rect> rects;
  if (rect) {
    rects.push_back(*rect);
  }
  SetWindowControlsRectsForTesting(std::move(rects));
}

void SetWindowControlsRectsForTesting(std::vector<gfx::Rect> rects) {
  GetTestingWindowControlsRectsOverride() = std::move(rects);
}

void ClearWindowControlsRectForTesting() {
  GetTestingWindowControlsRectsOverride() = std::nullopt;
}

}  // namespace maho
