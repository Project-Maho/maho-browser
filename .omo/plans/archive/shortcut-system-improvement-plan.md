# Shortcut System Improvement Plan

**Date:** 2026-05-05
**Branch:** main (e2e5fbf)
**Scope:** maho-ffi, maho-core, maho-types, maho-chromium

---

## Problem Statement

Maho's keyboard shortcut system has a custom architecture that intercepts key events before Chromium's accelerator table via `MahoShortcutInterceptor`, resolves them through Rust `ShortcutManager`, and dispatches actions via C++ `ExecuteCommandAction()`. Analysis reveals 5 concrete defects and 2 enhancement opportunities across this pipeline.

---

## Issue Inventory

### P0-1: FFI Persistence Bug — Settings Changes Lost on Restart

**Symptom:** User customizes shortcuts in `chrome://maho-settings` → changes work during session → lost after app restart.

**Root Cause:** FFI functions call non-persisted `MahoCore` methods:
- `maho_core_set_shortcut()` → `core.set_shortcut_json()` (in-memory only)
- `maho_core_reset_shortcut()` → `core.reset_shortcut()` (in-memory only)
- `maho_core_reset_all_shortcuts()` → `core.reset_all_shortcuts()` (in-memory only)

Persisted variants exist (`set_shortcut_persisted()`, `reset_shortcut_persisted()`, `reset_all_shortcuts_persisted()`) but are not wired to FFI.

**Files:**
- `maho/crates/maho-ffi/src/lib.rs` lines 2213-2258

**Fix:** Change FFI functions to call the `_persisted` variants. Specifically:
- `maho_core_set_shortcut` → call `core.set_shortcut_persisted()` instead of `core.set_shortcut_json()`
- `maho_core_reset_shortcut` → call `core.reset_shortcut_persisted()` instead of `core.reset_shortcut()`
- `maho_core_reset_all_shortcuts` → call `core.reset_all_shortcuts_persisted()` instead of `core.reset_all_shortcuts()`

**Note:** `set_shortcut_persisted()` takes `(action, KeyCombo)` not JSON strings. The FFI function currently deserializes to call `set_shortcut_json()`. Adjust to: deserialize JSON → `KeyCombo` → call `set_shortcut_persisted()`.

**Verification:**
1. `cargo test --workspace` passes
2. Manual: Set shortcut in settings → restart app → shortcut persists
3. Manual: Reset shortcut → restart → default restored

**Estimated effort:** Small (single file, 3 function bodies)

---

### P0-2: Action ID Mismatch Between Rust Defaults and C++ Handler

**Symptom:** User presses a registered shortcut → Rust resolves action ID → C++ handler doesn't recognize it → falls through to `maho_core_handle_event()` → possibly unhandled or silently ignored.

**Known mismatches:**

| Rust ShortcutManager (action) | C++ ExecuteCommandAction | Status |
|-------------------------------|--------------------------|--------|
| `reload` | `reload_tab` | Name mismatch |
| `reopen_tab` | `restore_tab` | Name mismatch |
| `toggle_devtools` | `toggle_dev_tools` | Name mismatch |
| `focus_address_bar` | `focus_url_bar` | Name mismatch |
| `zoom_reset` | `reset_zoom` | Name mismatch |
| `show_downloads` | `open_downloads` | Name mismatch |
| `open_settings` | `settings` | Name mismatch |
| `show_bookmarks` | `open_bookmarks` | Name mismatch |

**Files:**
- `maho/crates/maho-core/src/shortcut_manager.rs` — `register_default_bindings()`
- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` — `ExecuteCommandAction()`

**Fix approach — pick ONE source of truth, align the other:**

**Option A (Recommended): Rename Rust action IDs to match C++**
- C++ handler is the execution layer closest to Chromium APIs
- Changing Rust strings is cheaper (no header/include changes)
- Requires migration of any persisted custom shortcuts referencing old IDs (storage migration)

**Option B: Rename C++ to match Rust**
- Rust defaults are more descriptive (`focus_address_bar` > `focus_url_bar`)
- More churn in C++ handler
- No storage migration needed (C++ doesn't persist)

**Verification:**
1. For each renamed action: register default → trigger shortcut → confirm action executes
2. `cargo test --workspace` passes
3. Confirm no leftover references to old action IDs in codebase
4. If Option A: write storage migration for any previously persisted custom shortcuts

**Estimated effort:** Medium (audit all action IDs, rename in one place, test each)

---

### P0-3: Missing C++ Action Handlers

**Symptom:** Many Rust-registered shortcuts have no explicit C++ handler. They fall through to the generic `maho_core_handle_event()` path which wraps the action_id in a `CommandBarAction` JSON event and sends it back to Rust.

**Missing handlers for Rust-registered defaults:**

| Action | Expected behavior | Current fallthrough path |
|--------|-------------------|--------------------------|
| `navigate_back` | `chrome::GoBack()` | → Rust event (likely unhandled) |
| `navigate_forward` | `chrome::GoForward()` | → Rust event (likely unhandled) |
| `stop_loading` | `chrome::Stop()` | → Rust event (likely unhandled) |
| `fullscreen` | `chrome::ToggleFullscreenMode()` | → Rust event (likely unhandled) |
| `minimize` | platform window minimize | → Rust event (likely unhandled) |
| `new_window` | `chrome::NewWindow()` | → Rust event (likely unhandled) |
| `little_arc` | Little Arc popup | → Rust event (needs verification) |
| `close_window` | `chrome::CloseWindow()` | → Rust event (likely unhandled) |
| `clear_unpinned_tabs` | close all unpinned tabs | → Rust event (needs verification) |
| `copy_url` | copy active tab URL | → Rust event (needs verification) |
| `copy_url_markdown` | copy URL as markdown link | → Rust event (needs verification) |
| `next_tab` / `prev_tab` | `chrome::SelectNextTab()` / `SelectPreviousTab()` | → Rust event (likely unhandled) |
| `next_space` / `prev_space` | space switching | → Rust event (needs verification) |
| `new_space` | create new space | → Rust event (needs verification) |
| `select_tab_1-9` | `chrome::SelectNumberedTab()` | → Rust event (likely unhandled) |
| `select_space_1-9` | space selection | → Rust event (needs verification) |
| `show_archive` | show archived tabs | → Rust event (needs verification) |
| `new_note` | create new note | → Rust event (needs verification) |
| `open_boost_editor` | open boost editor | → Rust event (needs verification) |
| `open_atc_rules` | open ATC rules | → Rust event (needs verification) |
| `js_console` | `chrome::ToggleDevToolsWindow(Console)` | → Rust event (likely unhandled) |
| `command_bar` | open command overlay | → Rust event (needs verification) |

**Fix:** Audit each action. For those that need Chromium browser APIs (navigate, tab switching, window management), add explicit C++ handlers. For Maho-specific features handled by the Rust event loop (spaces, notes, boost), verify the fallthrough works and document it.

**Files:**
- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`
- `maho/crates/maho-core/src/maho_core.rs` — verify `process_command()` handling for `CommandBarAction` events

**Verification:**
1. For each action: press shortcut → confirm correct behavior
2. No silent failures or console errors

**Estimated effort:** Large (20+ actions to audit, ~15 new C++ handler branches)

---

### P1-1: Command Palette Shortcut Labels Hardcoded

**Symptom:** Command palette shows hardcoded shortcut labels (e.g., `⌘N` for `new_tab`). When user customizes the shortcut, the palette still shows the old label.

**Root Cause:** `maho_command_model.cc` `PopulateShortcutLabels()` uses static `if/else` chains mapping action_id → label string.

**Files:**
- `maho-chromium/browser/ui/views/command/maho_command_model.cc` lines 219-239

**Fix:** Replace hardcoded labels with dynamic lookup:
1. Add FFI function `maho_core_get_shortcut_label(action)` that returns the display label for a given action from ShortcutManager
2. Or: modify `maho_core_command_bar_search()` to include shortcut labels in its JSON response (Rust side already has access to ShortcutManager)
3. Remove `PopulateShortcutLabels()` entirely

**Option 2 is recommended** — enriching the Rust search response avoids extra FFI round-trips per suggestion.

**Verification:**
1. Default state: command palette shows correct shortcut labels
2. Customize shortcut in settings → command palette reflects new key combo
3. Reset shortcut → palette shows default again

**Estimated effort:** Medium (Rust search response + remove C++ hardcoding)

---

### P1-2: Command Palette Label Coverage Incomplete

**Symptom:** Even the hardcoded labels only cover ~15 of ~60 registered shortcuts. Most actions appear in the command palette without any shortcut hint.

**Files:**
- `maho-chromium/browser/ui/views/command/maho_command_model.cc` lines 219-239

**Fix:** Subsumed by P1-1. If shortcut labels come from Rust ShortcutManager dynamically, all actions with bindings will automatically show labels.

---

### P2-1: No Platform-Specific Default Shortcuts

**Symptom:** All default shortcuts use `KeyModifier::Meta` (⌘). On Linux/Windows, the conventional modifier is Ctrl. Users on those platforms get non-functional defaults.

**Files:**
- `maho/crates/maho-core/src/shortcut_manager.rs` — `register_default_bindings()`

**Fix:**
1. Add platform detection at `ShortcutManager::new()` time (compile-time `cfg` or runtime parameter)
2. Create platform-specific binding sets or a modifier substitution layer:
   - macOS: `Meta` primary modifier
   - Linux/Windows: `Ctrl` primary modifier, `Meta` → `Ctrl` substitution
3. Some shortcuts need platform-specific keys (e.g., `F11` for fullscreen on Linux vs `⌃⌘F` on macOS)

**Note:** Current build targets appear macOS-focused. This can be deferred until Linux/Windows builds become active.

**Verification:**
1. `cargo test` with platform-conditional assertions
2. Manual test on target platform

**Estimated effort:** Medium (conditional binding registration + testing on each platform)

---

### P2-2: No Enable/Disable Toggle for Individual Shortcuts

**Symptom:** `ShortcutBinding` has `enabled: bool` field. `ShortcutManager::find_binding_by_key_combo()` respects it. But there's no Mojo/FFI method to toggle `enabled`, and no UI for it.

**Files:**
- `maho-types/src/keyboard.rs` — `ShortcutBinding.enabled`
- `maho-core/src/shortcut_manager.rs` — filtering logic exists
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom` — missing toggle method

**Fix:**
1. Add `toggle_shortcut(action, enabled)` to ShortcutManager
2. Add FFI function `maho_core_toggle_shortcut(action, enabled)`
3. Add Mojo method `ToggleShortcut(string action, bool enabled) => (bool success)`
4. Add toggle UI element in settings WebUI shortcut rows

**Verification:**
1. Disable shortcut → shortcut no longer triggers
2. Re-enable → shortcut works again
3. Persistence across restart

**Estimated effort:** Medium (vertical slice through all layers)

---

## Execution Order

```
Phase 1 — Critical Fixes (P0)
  1. P0-1: FFI persistence bug                    [Small]
  2. P0-2: Action ID mismatch audit + rename      [Medium]
  3. P0-3: Missing C++ handlers                   [Large]

Phase 2 — UX Polish (P1)
  4. P1-1: Dynamic shortcut labels in palette      [Medium]

Phase 3 — Platform & Features (P2, defer until needed)
  5. P2-1: Platform-specific defaults              [Medium]
  6. P2-2: Enable/disable toggle                   [Medium]
```

**Dependencies:**
- P0-2 (action ID alignment) should be done before P0-3 (missing handlers) — no point adding handlers for mismatched IDs
- P1-1 depends on P0-2 being complete — labels should reference the canonical action IDs
- P0-1 (persistence fix) is independent and can be done first or in parallel

---

## Files Changed Per Phase

### Phase 1
| File | Changes |
|------|---------|
| `maho/crates/maho-ffi/src/lib.rs` | Wire FFI to `_persisted` variants |
| `maho/crates/maho-core/src/shortcut_manager.rs` | Rename action IDs in defaults |
| `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` | Add ~15 missing handler branches, align action IDs |
| `maho-chromium/browser/ui/views/command/maho_command_model.cc` | Update hardcoded labels to match renamed IDs (interim) |

### Phase 2
| File | Changes |
|------|---------|
| `maho/crates/maho-core/src/command_bar.rs` | Enrich search results with shortcut labels |
| `maho-chromium/browser/ui/views/command/maho_command_model.cc` | Remove `PopulateShortcutLabels()`, read from Rust response |

### Phase 3
| File | Changes |
|------|---------|
| `maho/crates/maho-core/src/shortcut_manager.rs` | Platform-conditional defaults |
| `maho/crates/maho-types/src/keyboard.rs` | Optional: platform enum |
| `maho/crates/maho-ffi/src/lib.rs` | `maho_core_toggle_shortcut()` |
| `maho/crates/maho-core/src/shortcut_manager.rs` | `toggle_shortcut()` method |
| `maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom` | `ToggleShortcut()` |
| `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` | Handler |
| `maho-chromium/browser/resources/maho_settings/app.ts` | Toggle UI |

---

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| Renaming action IDs breaks existing persisted shortcuts | Write a one-time storage migration in `MahoCore::init()` that maps old→new IDs |
| Adding C++ handlers for window/tab ops may conflict with Chromium defaults | Test that `PreHandleKeyboardEvent` returns HANDLED before Chromium's table runs |
| Dynamic palette labels add FFI overhead per search | Use Rust-side enrichment (no extra FFI call) |
| Platform defaults may break existing macOS user customizations | Only apply platform branching to the `defaults` vec, not user `bindings` |
