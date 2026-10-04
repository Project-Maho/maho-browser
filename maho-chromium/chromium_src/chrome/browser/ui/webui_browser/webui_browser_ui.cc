// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay reference for webui_browser_ui.cc
//
// Documents the upstream patch applied to webui_browser_ui.cc:
//
// 1. In the local `SidePanelEntryIdToTitle(SidePanelEntryId id)` helper,
//    add:
//
//      case SidePanelEntryId::kMahoAiPanel:
//        return "Maho AI";
//
//    before the `default:` branch.
//
//    Without this, WebUI Browser side-panel showing reaches `NOTREACHED()` for
//    `kMahoAiPanel`, which can abort the panel-show path even when the entry is
//    registered correctly.
//
// 2. The tracked source of truth is materialized into the local Chromium
//    checkout via:
//      build/scripts/apply_chromium_src_overrides.py
//
//    We use this sync step because this workspace does not yet have active
//    compile-time wiring that consumes `maho-chromium/chromium_src/` directly
//    for these `.cc` overrides.
