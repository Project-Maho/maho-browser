// Copyright 2026 Maho Browser. All rights reserved.
//
// Patches for chrome/browser/ui/views/toolbar/toolbar_view.cc are applied at build time
// by build/scripts/apply_chromium_src_overrides.py (see REPLACEMENTS entry for
// "chrome/browser/ui/views/toolbar/toolbar_view.cc").
//
// Applied changes:
//   - Suppress MediaToolbarButtonView addition to the toolbar since media
//     playback controls are integrated directly into the vertical sidebar.
//   - Guard the overflow button during minimum-size queries because macOS can
//     ask for titlebar sizing before ToolbarView initialization completes.
