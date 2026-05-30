// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_FEATURES_H_
#define MAHO_BROWSER_MAHO_FEATURES_H_

#include "base/feature_list.h"

namespace maho::features {

// When enabled, user-initiated keyboard focus events on the location bar
// (LocationBarView::OnFocus() with focus_reason == ui::TextInputClient::FOCUS_REASON_MOUSE
// or FOCUS_REASON_KEYBOARD) are intercepted and delegated to
// BrowserView::SetFocusToLocationBar(/*is_user_initiated=*/true), which routes
// them through the Maho command overlay path rather than the upstream omnibox
// focus path.
//
// Left-click (LocationBarView::OnMousePressed()) already routes through
// SetFocusToLocationBar(true) upstream and is NOT affected by this flag.
// Right-click / context-menu focus is explicitly excluded regardless of flag
// state; the context menu path does not call OnFocus() with a user-initiated
// reason in a way that should open the overlay.
//
// The SetFocusToLocationBar() intercept in the chromium_src overlay reads this
// flag at runtime:
//   if (is_user_initiated && maho_sidebar_container_ &&
//       base::FeatureList::IsEnabled(kMahoUnifiedClickPath)) { ... }
// Direct callers (Cmd+L shortcut interceptor, Cmd+T sidebar button) are not
// gated by this flag and always open the overlay.
//
// Enabled by default; disabling it restores the upstream location-bar
// mouse/keyboard focus path. The flag is retained as a kill-switch pending
// end-to-end QA sign-off.
BASE_DECLARE_FEATURE(kMahoUnifiedClickPath);
BASE_DECLARE_FEATURE(kMahoCommandPaletteSeededHistory);

// When enabled, Maho replaces the upstream `ToolbarButtonProvider`
// (registered by `ToolbarView` on `BrowserWindowInterface`) with a
// Maho-owned `MahoToolbarButtonProvider` whenever
// `BrowserView::IsMahoArcLayoutActive()` is true.  The Maho provider routes
// bubble anchors (translate, zoom, memory saver, etc.) to the sidebar pill
// instead of the zero-bounded upstream toolbar, and forwards every
// non-anchor virtual to the still-constructed upstream `ToolbarView`.
//
// Disabling this flag restores the upstream registration: bubbles anchor to
// the invisible toolbar (broken-but-unchanged Arc behaviour) and the
// sidebar's "Translation Available" label loses its click handler.  Used
// as a kill-switch pending end-to-end QA sign-off.
BASE_DECLARE_FEATURE(kMahoArcToolbarButtonProvider);

}  // namespace maho::features

#endif  // MAHO_BROWSER_MAHO_FEATURES_H_
