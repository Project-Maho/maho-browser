// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TRAFFIC_LIGHT_GEOMETRY_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TRAFFIC_LIGHT_GEOMETRY_H_

#include <optional>
#include <vector>

#include "build/build_config.h"
#include "ui/gfx/geometry/rect.h"

#if !BUILDFLAG(IS_MAC)
struct BrowserLayoutParams;
#endif

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

// Returns the union of the native window-control (caption) button frames,
// converted into `view`'s local coordinate space (DIPs, top-left origin).
// This is the sole authority for excluding window controls from a header
// laid out on top of the browser window; no compile-time fallback constant
// substitutes for the measured value.
//
// Platform behavior:
//   - macOS: union of the NSWindow standard close/miniaturize/zoom button
//     frames (leading edge).
//   - Windows/Linux: the union of the leading and trailing exclusion areas
//     (content plus padding) from the widget's public
//     `BrowserFrameView::GetBrowserLayoutParams()`. Linux can split its
//     buttons across both edges, so use `GetWindowControlsRectsInView()`
//     when each edge must be cleared separately. `GetCaptionButtonBounds()`
//     is never used: it is protected, and on Linux it is a fatal
//     NOTREACHED(); see maho_traffic_light_geometry_stub.cc.
//
// Returns std::nullopt when the window controls are not present or cannot be
// measured:
//   - `view` is null or not yet attached to a widget.
//   - macOS: the widget has no NSWindow, or it is a popup/devtools/borderless
//     variant with no caption buttons, or the close button is hidden (e.g.
//     tabbed immersive fullscreen).
//   - Windows/Linux: the widget has no NonClientView, its frame view is not
//     a `BrowserFrameView`, or the frame draws no caption buttons (empty
//     rect).
//   - `SetWindowControlsRectForTesting(std::nullopt)` installed an override
//     that forces nullopt (this is different from no override being
//     installed at all; see the testing-seam comment below).
//
// Callers must treat std::nullopt as "leave existing layout in place" — no
// compile-time constant substitutes for the measured value.
std::optional<gfx::Rect> GetWindowControlsRectInView(const views::View* view);

// Like `GetWindowControlsRectInView()`, but returns each separate cluster of
// window controls on its own (at most one per horizontal edge), in `view`'s
// local coordinates. Linux window managers can put caption buttons on both
// edges. On macOS the vector holds at most the single traffic-light rect.
// Empty wherever `GetWindowControlsRectInView()` returns std::nullopt.
std::vector<gfx::Rect> GetWindowControlsRectsInView(const views::View* view);

#if !BUILDFLAG(IS_MAC)
// Converts the leading/trailing exclusion areas of `params` into rects in
// the same (frame view) coordinate space as `params.visual_client_area`.
// Each exclusion is measured from its edge of the visual client area and
// includes its padding. Empty exclusions are omitted, so the result has 0-2
// rects, leading first. Exposed for unit tests.
std::vector<gfx::Rect> GetWindowControlsRectsFromLayoutParams(
    const BrowserLayoutParams& params);
#endif

// Testing seam: installs an override so `GetWindowControlsRectInView()`
// returns `rect` verbatim (already expressed in the caller's target
// coordinate space) instead of measuring the real platform window controls,
// for every subsequent call regardless of `view`. Passing `rect =
// std::nullopt` is a valid override that forces the getter to return
// std::nullopt (e.g. to simulate "no window controls present"); it is NOT
// the same as no override, because the override, once installed, replaces
// platform measurement entirely. Call `ClearWindowControlsRectForTesting()`
// to remove the override and restore normal platform-measured behavior.
// Intended for header layout unit tests that inject a fake (or absent)
// window-control rect without a real NSWindow/HWND.
void SetWindowControlsRectForTesting(std::optional<gfx::Rect> rect);

// As `SetWindowControlsRectForTesting()`, but installs several rects (e.g.
// Linux buttons on both edges). `GetWindowControlsRectsInView()` returns
// `rects` verbatim, and `GetWindowControlsRectInView()` returns their union,
// or std::nullopt when `rects` is empty.
void SetWindowControlsRectsForTesting(std::vector<gfx::Rect> rects);

// Removes any override installed by `SetWindowControlsRectForTesting()`, so
// `GetWindowControlsRectInView()` resumes measuring the real platform window
// controls. A no-op if no override is installed.
void ClearWindowControlsRectForTesting();

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TRAFFIC_LIGHT_GEOMETRY_H_
