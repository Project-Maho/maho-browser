// Copyright 2026 Maho Browser. All rights reserved.
//
// Patches for chrome/browser/ui/browser.cc are applied at build time by
// build/scripts/apply_chromium_src_overrides.py (see REPLACEMENTS entry for
// "chrome/browser/ui/browser.cc").
//
// Applied changes:
//   - In Browser::TabStripEmpty(): skip window_->Close() for normal browser
//     windows when is_attempting_to_close_browser_ is false, enabling
//     zero-tab persistence.  Window close still fires for all other browser
//     types (popup, app, devtools) and for normal windows that are already in
//     an explicit close flow (TryToCloseWindow / GetBrowserClosingStatus has
//     already set is_attempting_to_close_browser_ = true).
