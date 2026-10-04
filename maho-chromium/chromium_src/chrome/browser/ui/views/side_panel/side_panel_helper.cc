// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay reference for side_panel_helper.cc
//
// Documents the upstream patch applied to side_panel_helper.cc:
//
// 1. In SidePanelHelper::PopulateGlobalEntries(), inside the early-return path
//    for `webui_browser::IsWebUIBrowserEnabled()`, insert:
//
//      maho::RegisterMahoAiSidePanel(browser, window_registry);
//
//    immediately before `return;`.
//
//    This keeps the existing non-WebUI registration at the bottom of the
//    function intact, while also registering `kMahoAiPanel` for Webium /
//    WebUI Browser windows where the original code otherwise returned after
//    only Reading List and Bookmarks.
//
// 2. The tracked source of truth is materialized into the local Chromium
//    checkout via:
//      build/scripts/apply_chromium_src_overrides.py
//
//    The canonical build wrapper now runs that script before autoninja so the
//    tracked registration patch is reproducibly applied during Chromium builds.
