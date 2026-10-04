// Copyright 2026 Maho Browser. All rights reserved.
//
// Patches for chrome/browser/sessions/session_restore.cc are applied at build
// time by build/scripts/apply_chromium_src_overrides.py (see REPLACEMENTS
// entry for "chrome/browser/sessions/session_restore.cc").
//
// Applied changes:
//   - In SessionRestore::FinishedTabCreation(): the block that synthesises a
//     chrome://newtab tab when startup_tabs_ is empty (or contains only the
//     What's New URL) is replaced so that no NTP is injected.  If
//     startup_tabs_ is non-empty the tabs are still appended via
//     AppendURLsToBrowser(); if it is empty the newly created browser window
//     is shown with zero tabs, allowing the Maho empty-window UX to take over
//     without an unwanted NTP loading in the background.
