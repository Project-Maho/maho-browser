// Copyright 2026 Maho Browser. All rights reserved.

#import <AppKit/AppKit.h>

#include "maho/browser/ui/views/sidebar/maho_traffic_light_geometry.h"

#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include "base/no_destructor.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {

std::optional<int> GetTrafficLightCenterYInView(const views::View* view) {
  if (!view) {
    return std::nullopt;
  }
  const views::Widget* widget = view->GetWidget();
  if (!widget) {
    return std::nullopt;
  }

  NSWindow* ns_window = widget->GetNativeWindow().GetNativeNSWindow();
  if (!ns_window) {
    return std::nullopt;
  }

  NSButton* close_button =
      [ns_window standardWindowButton:NSWindowCloseButton];
  if (!close_button || close_button.isHidden) {
    // Popup / devtools / borderless windows have no standard caption
    // buttons; tabbed-immersive-fullscreen hides them.
    return std::nullopt;
  }

  NSView* content_view = ns_window.contentView;
  if (!content_view) {
    return std::nullopt;
  }

  // Convert the close button's rect into the window's content-view coordinate
  // space (AppKit points, bottom-origin).
  //
  // The macOS window frame view (which hosts the standard buttons) is a
  // sibling/superview of the content view; -[NSView convertRect:toView:]
  // walks up to a common ancestor and back down, giving us the button's
  // rect measured in the content view's coordinate system.
  NSRect rect_in_content =
      [close_button convertRect:close_button.bounds toView:content_view];
  const CGFloat center_y_bottom_pt = NSMidY(rect_in_content);
  const CGFloat content_height = NSHeight(content_view.bounds);

  // Flip AppKit's bottom-origin y into Views' top-origin y. Chromium's browser
  // widget on macOS is hosted inside the NSWindow's content view, and the
  // widget's coordinate space origin (0,0) coincides with the top-left of the
  // content view. On macOS, 1 AppKit point == 1 Views DIP; no scale factor
  // conversion is needed. The value can be slightly negative if the traffic
  // lights sit above the content-view top edge (rare, e.g. custom title-bar
  // heights); callers may clamp.
  const CGFloat center_y_top_pt = content_height - center_y_bottom_pt;
  const int center_y_widget_dip =
      static_cast<int>(std::round(center_y_top_pt));

  // Widget-local coords → the caller view's local coords. Uses the standard
  // Views coordinate bridge; correctly accounts for any offset between the
  // widget origin and the view's placement in the widget tree.
  gfx::Point point_in_widget(0, center_y_widget_dip);
  views::View::ConvertPointFromWidget(view, &point_in_widget);
  return point_in_widget.y();
}

namespace {

// std::nullopt (outer) means "no override installed - measure normally".
// A present outer optional means an override is installed; its inner value
// (which may itself be std::nullopt) is what GetWindowControlsRectInView()
// returns verbatim. See SetWindowControlsRectForTesting() /
// ClearWindowControlsRectForTesting().
std::optional<std::optional<gfx::Rect>>&
GetTestingWindowControlsRectOverride() {
  static base::NoDestructor<std::optional<std::optional<gfx::Rect>>>
      override_rect;
  return *override_rect;
}

// Converts one AppKit content-view-space button frame (bottom-origin points)
// into the browser widget's Views coordinate space (top-origin DIPs). Flips
// the y axis the same way GetTrafficLightCenterYInView() does above, but
// keeps all four edges (not just a center point) since the header needs the
// button's full width and height to exclude it from layout.
gfx::RectF ConvertButtonFrameToWidgetDip(NSRect rect_in_content,
                                          CGFloat content_height) {
  const CGFloat top_pt = content_height - NSMaxY(rect_in_content);
  return gfx::RectF(NSMinX(rect_in_content), top_pt,
                     NSWidth(rect_in_content), NSHeight(rect_in_content));
}

}  // namespace

std::optional<gfx::Rect> GetWindowControlsRectInView(const views::View* view) {
  if (GetTestingWindowControlsRectOverride().has_value()) {
    return *GetTestingWindowControlsRectOverride();
  }

  if (!view) {
    return std::nullopt;
  }
  const views::Widget* widget = view->GetWidget();
  if (!widget) {
    return std::nullopt;
  }

  NSWindow* ns_window = widget->GetNativeWindow().GetNativeNSWindow();
  if (!ns_window) {
    return std::nullopt;
  }

  NSButton* close_button =
      [ns_window standardWindowButton:NSWindowCloseButton];
  if (!close_button || close_button.isHidden) {
    // Popup / devtools / borderless windows have no standard caption
    // buttons; tabbed-immersive-fullscreen hides them.
    return std::nullopt;
  }

  NSView* content_view = ns_window.contentView;
  if (!content_view) {
    return std::nullopt;
  }

  const CGFloat content_height = NSHeight(content_view.bounds);

  // Union of close/miniaturize/zoom button frames. Miniaturize/zoom may be
  // absent (e.g. resizable=false hides zoom) or hidden; skip those but keep
  // close (already checked above) as the mandatory anchor.
  NSRect union_rect_in_content =
      [close_button convertRect:close_button.bounds toView:content_view];
  for (NSWindowButton button_type :
       {NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    NSButton* button = [ns_window standardWindowButton:button_type];
    if (!button || button.isHidden) {
      continue;
    }
    NSRect button_rect_in_content =
        [button convertRect:button.bounds toView:content_view];
    union_rect_in_content =
        NSUnionRect(union_rect_in_content, button_rect_in_content);
  }

  gfx::RectF rect_in_widget_f =
      ConvertButtonFrameToWidgetDip(union_rect_in_content, content_height);

  // Round to integer widget-local DIPs by the far corner, not by
  // independently-rounded width/height: gfx::ToEnclosingRect rounds the
  // origin down and the far (bottom-right) corner up, so
  // enclosing_rect.origin() + enclosing_rect.size() always equals the
  // rounded far corner exactly. Rounding width/height separately from the
  // origin (as an earlier version of this function did) can make the two
  // roundings disagree by one DIP on the right/bottom edge.
  gfx::Rect rect_in_widget = gfx::ToEnclosingRect(rect_in_widget_f);

  // Widget-local coords -> the caller view's local coords. There is no
  // View::ConvertRectFromWidget (or a float-point overload of
  // ConvertPointFromWidget) on views::View, so convert the rect's origin
  // and opposite (bottom-right) corner as two already-integer points
  // through the same point bridge GetTrafficLightCenterYInView() uses
  // above. This step is a pure coordinate-space translation (widget space
  // to a descendant view's space), so converting two corners independently
  // does not reintroduce rounding error: both points move by the same
  // integer offset.
  gfx::Point origin_in_widget = rect_in_widget.origin();
  gfx::Point bottom_right_in_widget = rect_in_widget.bottom_right();
  views::View::ConvertPointFromWidget(view, &origin_in_widget);
  views::View::ConvertPointFromWidget(view, &bottom_right_in_widget);
  return gfx::BoundingRect(origin_in_widget, bottom_right_in_widget);
}

std::vector<gfx::Rect> GetWindowControlsRectsInView(const views::View* view) {
  // The traffic lights are one leading-edge cluster, so this is at most the
  // single union rect.
  if (const std::optional<gfx::Rect> rect = GetWindowControlsRectInView(view)) {
    return {*rect};
  }
  return {};
}

void SetWindowControlsRectForTesting(std::optional<gfx::Rect> rect) {
  GetTestingWindowControlsRectOverride() = std::move(rect);
}

void SetWindowControlsRectsForTesting(std::vector<gfx::Rect> rects) {
  std::optional<gfx::Rect> united;
  for (const gfx::Rect& rect : rects) {
    if (!united) {
      united = rect;
    } else {
      united->Union(rect);
    }
  }
  GetTestingWindowControlsRectOverride() = united;
}

void ClearWindowControlsRectForTesting() {
  GetTestingWindowControlsRectOverride() = std::nullopt;
}

}  // namespace maho
