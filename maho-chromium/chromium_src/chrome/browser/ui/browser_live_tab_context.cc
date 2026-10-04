// Copyright 2026 Maho Browser. All rights reserved.
//
// Patches for chrome/browser/ui/browser_live_tab_context.cc are applied at
// build time by build/scripts/apply_chromium_src_overrides.py (see REPLACEMENTS
// entry for "chrome/browser/ui/browser_live_tab_context.cc").
//
// Applied changes:
//   - Adds #include "maho/browser/maho_tab_id_helper.h"
//   - In GetExtraDataForTab(): writes MahoTabIdHelper::stable_tab_id() into
//     extra_data[MahoTabIdHelper::kExtraDataKey] for session persistence
