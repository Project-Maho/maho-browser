// Copyright 2026 Maho Browser. All rights reserved.
//
// Patches for chrome/browser/ui/browser_tabrestore.cc are applied at build
// time by build/scripts/apply_chromium_src_overrides.py (see REPLACEMENTS
// entry for "chrome/browser/ui/browser_tabrestore.cc").
//
// Applied changes:
//   - Adds #include "maho/browser/maho_tab_id_helper.h"
//   - In CreateRestoredTab(): reads MahoTabIdHelper::kExtraDataKey from
//     extra_data and calls SetRestoredTabId() to restore the stable tab ID
