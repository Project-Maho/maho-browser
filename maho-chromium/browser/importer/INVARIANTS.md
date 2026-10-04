# INVARIANTS.md

This document specifies the safety invariants for the Maho Importer subsystem.

## Invariants

- **INV-1**: All NSS decrypt calls wrapped in `DCHECK(!content.empty())` pre-strip.
- **INV-2**: Threading — importer parsers run on `ImporterThread`; bridge/orchestrator dispatch posts to UI.
- **INV-3**: `MahoSpaceProfileBridge` callers must hold `DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_)`.
- **INV-4**: No parser returns success with zero items (empty-result = error).
- **INV-5**: Cookie parser MUST check 3-byte prefix (`v10`/`v20`) before attempting decrypt.
- **INV-6**: Test gate — `test_data/` fixtures required for build.
- **INV-7**: `ImportOrchestrator` is the ONLY caller of `OnImportProgress(complete=true)`.
- **INV-8**: FFI calls to `maho_core_handle_event` MUST be on UI thread.
- **INV-9**: Plaintext key material zeroed after use via RAII SecureBuffer or `memset_s`.

## §Essentials — Dual-path Architecture

`EssentialImporter` is used via two paths:

1. **Orchestrated path** (`kImportEssential` bitmask via `ImportOrchestrator::StartImport`): caller must invoke `orchestrator->SetSelectedEssentials(urls)` before `StartImport`. The orchestrator passes the URLs to the worker via `EssentialImporter::SetSelectedUrls`. This is the canonical path for browser-migration flows (e.g., `MahoMigrationDialogView`).

2. **Standalone Welcome path** (`MahoWelcomePageHandler::FavoriteEssentialSites` Mojo handler): calls `EssentialImporter::FavoriteEssentialSites` directly. Used for the onboarding Welcome flow where no browser migration is happening — the user is just selecting which sites to favorite. `FavoriteEssentialSites` also calls `orchestrator->SetSelectedEssentials(urls)` so the orchestrated path stays consistent if later invoked.

## §Consumers

Tracked callsites to `MahoSpaceProfileBridge` in the codebase:

| Category | File | Line | Call |
| --- | --- | --- | --- |
| IMPORTER | `maho-chromium/browser/importer/importers/workspace_importer.cc` | 124 | `bridge->NotifyChanged();` |
| WEBUI | `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config_page_handler.cc` | 275 | `bridge->RegisterSpace(space_id_, ...);` |
| WEBUI | `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` | 3215 | `bridge->RequestDisable(id);` |
| WEBUI | `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` | 3217 | `bridge->RequestEnable(id);` |
| WEBUI | `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` | 3240 | `bridge->RequestUninstall(id);` |
| WEBUI | `maho-chromium/browser/ui/webui/maho_test/maho_test_ui.cc` | 391 | `space_bridge->GetActiveSpaceId();` |
| WEBUI | `maho-chromium/browser/ui/webui/maho_test/maho_test_ui.cc` | 946 | `bridge->NotifyChanged();` |
| WEBUI | `maho-chromium/browser/ui/webui/maho_test/maho_test_ui.cc` | 1018 | `bridge->GetActiveSpaceId();` |
| WEBUI | `maho-chromium/browser/ui/webui/maho_test/maho_test_ui.cc` | 1029 | `space_bridge->GetRegisteredSpaceCount();` |
| WEBUI | `maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc` | 655 | `bridge->GetActiveSpaceId();` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc` | 1285 | `bridge->RegisterSpace(new_space_id, ...);` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc` | 1286 | `bridge->SwitchToSpace(browser_, new_space_id);` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc` | 97 | `bridge->AddObserver(this);` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc` | 108 | `bridge->RemoveObserver(this);` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc` | 424 | `bridge->GetActiveSpaceId(browser_);` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc` | 627 | `bridge->GetActiveSpaceId(browser_);` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_view.cc` | 82 | `bridge->SwitchToSpace(browser_, space_id);` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc` | 460 | `bridge->GetActiveSpaceId();` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc` | 623 | `bridge->SwitchToSpace(browser_, dot->space_id());` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc` | 847 | `bridge->SwitchToSpace(browser_, dot->space_id());` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc` | 864 | `bridge->SwitchToSpace(browser_, dot->space_id());` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc` | 878 | `bridge->SwitchToSpace(browser_, dot->space_id());` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` | 3387 | `bridge->GetActiveSpaceId(browser_);` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` | 3414 | `bridge->GetActiveSpaceId(browser_);` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.cc` | 867 | `bridge->GetActiveSpaceId(browser_);` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` | 235 | `bridge->GetActiveSpaceId(browser);` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` | 247 | `bridge->GetActiveSpaceId(browser);` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` | 553 | `bridge->NotifyChanged();` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` | 587 | `bridge->NotifyChanged();` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` | 606 | `bridge->NotifyChanged();` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` | 935 | `bridge->GetRegisteredSpaceCount();` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` | 943 | `bridge->GetActiveSpaceIndex(browser);` |
| SIDEBAR | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` | 951 | `bridge->GetActiveSpaceId(browser);` |
| COMMANDS | `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` | 347 | `bridge->GetActiveSpaceId(browser);` |
| COMMANDS | `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` | 502 | `bridge->SwitchToSpace(browser, space_id);` |
| COMMANDS | `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` | 516 | `bridge->GetActiveSpaceId(browser);` |
| COMMANDS | `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` | 535 | `bridge->SwitchToSpace(browser, *target_id);` |
| COMMANDS | `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` | 566 | `bridge->SwitchToSpace(browser, *id);` |
| COMMANDS | `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` | 645 | `bridge->GetActiveSpaceId(browser);` |
| COMMANDS | `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` | 807 | `bridge->GetActiveSpaceId();` |
| CORE | `maho-chromium/browser/maho_browser_main_extra_parts.cc` | 99 | `bridge->RegisterSpace(*id, ...);` |
| CORE | `maho-chromium/browser/maho_browser_main_extra_parts.cc` | 102 | `bridge->SetActiveSpaceId(...);` |
| CORE | `maho-chromium/browser/maho_browser_main_extra_parts.cc` | 113 | `bridge->GetRegisteredSpaceCount();` |
| CORE | `maho-chromium/browser/maho_tab_registry.cc` | 136 | `bridge->NotifyChanged();` |
| CORE | `maho-chromium/browser/maho_tab_registry.cc` | 159 | `bridge->GetActiveSpaceId(browser);` |
