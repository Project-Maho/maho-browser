// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TRAFFIC_LIGHT_GEOMETRY_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TRAFFIC_LIGHT_GEOMETRY_H_

#include <optional>

namespace views {
class View;
}  // namespace views

namespace maho {

// Returns the y coordinate (in `view`'s local coordinate space, DIPs, top
// origin) of the vertical center of the macOS traffic-light window controls,
// as reported by AppKit at runtime. This is the sole authority for aligning
// sidebar action icons against the macOS window controls; no compile-time
// fallback constant exists.
//
// Returns std::nullopt when the traffic lights are not present or cannot be
// measured:
//   - Non-macOS build.
//   - `view` is null or not yet attached to a widget.
//   - The widget has no NSWindow (headless / offscreen / unit tests).
//   - The NSWindow is a popup/devtools/borderless variant with no caption
//     buttons.
//   - The close button is hidden (e.g. tabbed immersive fullscreen).
//
// Callers must treat std::nullopt as "leave existing layout in place" — no
// compile-time constant substitutes for the measured value.
std::optional<int> GetTrafficLightCenterYInView(const views::View* view);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TRAFFIC_LIGHT_GEOMETRY_H_
