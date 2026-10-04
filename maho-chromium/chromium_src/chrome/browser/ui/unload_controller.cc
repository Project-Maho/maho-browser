// Copyright 2026 Maho Browser. All rights reserved.
//
// Patches for chrome/browser/ui/unload_controller.cc are applied at build time
// by build/scripts/apply_chromium_src_overrides.py (see REPLACEMENTS entry for
// "chrome/browser/ui/unload_controller.cc").
//
// Applied changes:
//   - In UnloadController::TabStripEmpty(): skip setting
//     is_attempting_to_close_browser_ = true when browser_->is_type_normal()
//     and the flag is not already set.  This allows a normal browser window to
//     survive with zero tabs and accept new tabs afterwards.  For all other
//     browser types, and for normal windows already in an explicit close flow
//     (TryToCloseWindow / GetBrowserClosingStatus set the flag first), the
//     original behaviour is preserved.
