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
// Phase 4 of the click-path unification plan. Disabled by default pending
// end-to-end QA sign-off.
BASE_DECLARE_FEATURE(kMahoUnifiedClickPath);

}  // namespace maho::features

#endif  // MAHO_BROWSER_MAHO_FEATURES_H_
