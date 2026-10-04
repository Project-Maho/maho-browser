// Copyright 2026 Maho Browser. All rights reserved.
//
// Patches for chrome/browser/ui/tab_helpers.cc are applied at build time by
// build/scripts/apply_chromium_src_overrides.py (see REPLACEMENTS entry for
// "chrome/browser/ui/tab_helpers.cc").
//
// Applied changes:
//   - Adds #include "maho/browser/maho_tab_id_helper.h"
//   - Calls MahoTabIdHelper::CreateForWebContents() in AttachTabHelpers()
