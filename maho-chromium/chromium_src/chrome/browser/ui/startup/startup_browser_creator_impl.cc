// Copyright 2026 Maho Browser. All rights reserved.
//
// Patches for chrome/browser/ui/startup/startup_browser_creator_impl.cc are
// applied at build time by build/scripts/apply_chromium_src_overrides.py (see
// REPLACEMENTS entry for
// "chrome/browser/ui/startup/startup_browser_creator_impl.cc").
//
// Applied changes:
//   - In DetermineStartupTabs(): the declaration
//     `bool prefs_tabs_originally_empty = prefs_tabs.empty();` is removed
//     (it would otherwise be unused after the NTP-append block below is
//     deleted, triggering -Wunused-variable).
//   - In DetermineStartupTabs(): the block that calls
//     provider.GetNewTabPageTabs() when prefs_tabs_originally_empty is true
//     is replaced with a no-op comment, so |tabs| stays empty for a default
//     startup and the zero-tab RestoreOrCreateBrowser branch can fire.
//   - In RestoreOrCreateBrowser(): when |tabs| is empty, bypass
//     OpenTabsInBrowser() (which DCHECKs on empty input) and instead
//     create a plain normal Browser window and call Show() on it, so the
//     app launches with zero tabs.
//   - In OpenTabsInBrowser(): the post-navigation fallback that calls
//     chrome::AddTabAt(browser, GURL(), -1, true) is suppressed for normal
//     browser windows, preventing a blank tab from being synthesised when the
//     tab strip is still empty after navigation processing.
