// Copyright 2026 Maho Browser. All rights reserved.

#import <AppKit/AppKit.h>

#include "maho/browser/ui/views/sidebar/maho_traffic_light_geometry.h"

#include <cmath>
#include <optional>

#include "ui/gfx/geometry/point.h"
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

}  // namespace maho
