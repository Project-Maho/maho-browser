# Extension State Sync Implementation Plan

**Created:** 2026-05-25
**Status:** Draft, grounded in current main (commit `e2e5fbf`)
**Primary scope:**
- `maho-chromium/browser/extensions/` (new directory: `maho_extension_state_bridge.{cc,h}`)
- `maho-chromium/browser/maho_browser_main_extra_parts.cc`
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.cc`
- `maho-chromium/third_party/maho/maho_ffi.h` (regenerated via cbindgen + `build_maho_core.py`)
- `maho/crates/maho-core/src/extension_bridge.rs`
- `maho/crates/maho-core/src/maho_core.rs`
- `maho/crates/maho-ffi/src/lib.rs`
- `maho/crates/maho-types/src/events/core_update.rs`

**Secondary scope:**
- `maho/crates/maho-core/tests/` (new `extension_bridge_sync.rs`)
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_ui.cc` (cleanup of misleading `TabHelper::CreateForWebContents()` call)
- `maho-chromium/browser/net/maho_webstore_ua_throttle.cc` (read-only verification only — not a producer of state)

---

## 1. Objective

Replace the current "Maho extension UI talks to an empty in-memory map" reality with a real producer/consumer chain:

1. **Producer:** Chromium's per-profile `extensions::ExtensionRegistry` (the canonical source of truth for installed/enabled/disabled extensions), observed by a new C++ `MahoExtensionStateBridge`.
2. **Carrier:** new FFI calls (`maho_core_register_extension`, `maho_core_set_installed_extensions`, `maho_core_clear_installed_extensions`) plus a new `CoreUpdate::InstalledExtensionsUpdated` event.
3. **Consumer:** existing Maho UI surfaces (`MahoSettingsPageHandler`, `MahoLocationBarUtilityPanelProvider`) that already read `maho_core_get_installed_extensions()` but currently get an empty list because Rust is never seeded.
4. **Mutation round-trip:** Maho-side toggle/uninstall actions go through Chromium's `ExtensionRegistrar` (or `ExtensionService` equivalent), and the registry observer pushes the resulting authoritative state back into Rust — so Chromium remains the single source of truth.

The end result: turning extensions on/off, installing them from the Web Store, or uninstalling them from `chrome://extensions` all reflect immediately in Maho's settings page and location-bar utility panel without divergent state.

---

## 2. Verified Current-State Review

Captured by exhaustive reading of the listed files at commit `e2e5fbf`. Numbers refer to current line offsets.

### 2.1 Rust side (`maho/crates/maho-core/src/extension_bridge.rs`)

- `ExtensionBridge` owns `extensions: HashMap<String, InstalledExtension>` (line 89). It starts **empty** at `ExtensionBridge::new` (line 111) and is never populated by any startup hook, FFI call, or storage load.
- Public mutation API on the bridge (lines 301–322):
  - `register_extension(InstalledExtension)`
  - `toggle_extension(&str) -> Option<bool>` (flips the `enabled` flag in place)
  - `remove_extension(&str) -> Option<InstalledExtension>` (also wipes the extension's storage and message queue)
  - `get_installed_extensions() -> Vec<&InstalledExtension>`
  - `get_extension(&str) -> Option<&InstalledExtension>`
- **Missing:** no `set_installed_extensions(Vec<...>)` (bulk replace), no `clear_installed_extensions()`. Bulk seeding today would require many `register_extension` calls.

### 2.2 `MahoCore` re-exports (`maho/crates/maho-core/src/maho_core.rs`)

- `MahoCore` exposes (lines ~3681–3698):
  - `pub fn get_installed_extensions(&self) -> Vec<InstalledExtension>` (clones bridge data)
  - `pub fn toggle_extension(&mut self, &str) -> Option<bool>`
  - `pub fn remove_extension(&mut self, &str) -> Option<InstalledExtension>`
- `register_extension` exists on `ExtensionBridge` but is **not re-exposed on `MahoCore`**, so even Rust callers cannot insert an extension via the public surface today.
- `toggle_extension` and `remove_extension` already dispatch a `CoreUpdate` (lines ~2255–2278) — but those events use ad-hoc `"toggle_extension"` / `"remove_extension"` strings, not a typed variant.

### 2.3 FFI surface (`maho/crates/maho-ffi/src/lib.rs` + `maho-chromium/third_party/maho/maho_ffi.h`)

- Exposed FFI calls today:
  - `maho_core_get_installed_extensions(MahoCore*) -> char*` (lib.rs:403)
  - `maho_core_is_bitwarden_extension_available(MahoCore*) -> bool` (lib.rs:425)
  - `maho_core_is_onepassword_extension_available(MahoCore*) -> bool` (lib.rs:438)
  - `maho_core_toggle_extension(MahoCore*, const char*) -> bool` (lib.rs:1780)
  - `maho_core_remove_extension(MahoCore*, const char*) -> bool` (lib.rs:1796)
- **Missing entirely:**
  - `maho_core_register_extension`
  - `maho_core_set_installed_extensions`
  - `maho_core_clear_installed_extensions`
- The header (`third_party/maho/maho_ffi.h`) is the cbindgen output checked in alongside the prebuilt static lib produced by `maho-chromium/build/scripts/build_maho_core.py`.

### 2.4 C++ consumers

- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` calls `maho_core_get_installed_extensions`, `maho_core_toggle_extension`, `maho_core_remove_extension` (verified by grep). Today these all act on the empty in-memory map, so the Settings page renders a blank list and toggling does nothing visible.
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.cc` likewise reads `maho_core_get_installed_extensions` to render extension utility entries; same empty-list problem.
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_ui.cc` calls `extensions::TabHelper::CreateForWebContents()`. This is a **per-`WebContents` tab helper attachment**, not a state producer. It does not subscribe to the registry. It is misleading in its current location and should be cleaned up (see Phase 5).
- `maho-chromium/browser/net/maho_webstore_ua_throttle.cc` is a `URLLoaderRequestInterceptor`-style throttle that adjusts the User-Agent for `chrome.google.com/webstore` requests. **It is not a state observer** and does not interact with the `ExtensionRegistry`. Listed only because earlier discovery touched it.
- `maho-chromium/browser/maho_browser_main_extra_parts.cc` is the canonical place to bootstrap profile-scoped Maho services after `PostBrowserCreated` / `PostMainMessageLoopRun`, and is where the new bridge will be attached.

### 2.5 Upstream Chromium reference points (read-only)

- `extensions::ExtensionRegistry` (`extensions/browser/extension_registry.h`) — per-profile registry holding `enabled_extensions()`, `disabled_extensions()`, `terminated_extensions()`, `blocklisted_extensions()`, and `blocked_extensions()` sets.
- `extensions::ExtensionRegistryObserver` (`extensions/browser/extension_registry_observer.h`) — virtual hooks: `OnExtensionLoaded`, `OnExtensionReady`, `OnExtensionUnloaded`, `OnExtensionInstalled`, `OnExtensionUninstalled`. This is the canonical observer to mirror state changes from.
- `extensions::ExtensionRegistrar` (`extensions/browser/extension_registrar.h`) — write-side API: `EnableExtension`, `DisableExtension`, `UninstallExtension`. The right entry point for round-tripping Maho-UI mutations into Chromium.
- Brave's `BraveExtensionsHandler` (and its Brave-specific settings handlers under `chrome/browser/ui/webui/settings/brave/`) is the closest established UI integration pattern; it observes the registry and pushes JSON updates to the WebUI front-end. Used as a structural reference, not a dependency.

### 2.6 Concrete summary of the gap

| Layer | Has | Missing |
|---|---|---|
| Rust bridge state | in-memory map, single-item register/toggle/remove | bulk set, clear |
| Rust public surface (`MahoCore`) | get/toggle/remove | register, set, clear |
| Rust→C FFI | get/toggle/remove (+ password-manager probes) | register, set, clear |
| C++ producer | none | new `MahoExtensionStateBridge` observing `ExtensionRegistry` |
| C++ bootstrap | `MahoBrowserMainExtraParts` exists | attaching the new bridge per profile |
| `CoreUpdate` event | ad-hoc string events | typed `InstalledExtensionsUpdated` variant + dispatch on every mutation |
| C++ consumer refresh | one-shot reads on UI mount | subscribe to the new event and re-fetch |
| Mutation round-trip | toggle/remove flip Rust state only | proxy through `ExtensionRegistrar`, let observer push canonical state back |

---

## 3. Architecture and Ownership Boundaries

Hard rule: **Chromium's `ExtensionRegistry` is the single source of truth.** Rust holds a cached read-model only.

### 3.1 Direction of dataflow

```
Chromium ExtensionRegistry  ──OnExtensionLoaded/Unloaded/Installed/Uninstalled──►  MahoExtensionStateBridge (C++)
                                                                                      │
                                                                                      │ FFI: maho_core_set_installed_extensions(json)
                                                                                      ▼
                                                                           ExtensionBridge::set_installed_extensions  (Rust)
                                                                                      │
                                                                                      │ CoreUpdate::InstalledExtensionsUpdated
                                                                                      ▼
                                                                              event_dispatcher.rs
                                                                                      │
                                                                                      ▼
                                            MahoSettingsPageHandler / MahoLocationBarUtilityPanelProvider
                                                                                      │
                                                                                      │ re-fetch via maho_core_get_installed_extensions
                                                                                      ▼
                                                                           UI re-render
```

For Maho-UI-initiated mutations:

```
Settings WebUI / Utility Panel  ──user action──►  C++ handler
                                                       │
                                                       │ extensions::ExtensionRegistrar::DisableExtension / EnableExtension / UninstallExtension
                                                       ▼
                                              ExtensionRegistry (mutates)
                                                       │
                                                       │ OnExtension* observer callback
                                                       ▼
                                              MahoExtensionStateBridge → FFI seed → Rust → CoreUpdate → UI
```

The Maho-UI handler does **not** call `maho_core_toggle_extension` or `maho_core_remove_extension` on its own when the registrar path is wired (Phase 4). Those FFI calls remain available for Rust-internal callers and tests, but become side-effect-free on the canonical state path.

### 3.2 Ownership boundaries

| Responsibility | Owner | Why |
|---|---|---|
| Source of truth for installed/enabled state | C++ `ExtensionRegistry` | Upstream Chromium subsystem; must not be forked |
| Read-model cache for Rust consumers | `ExtensionBridge.extensions` | Already exists; just needs seeding |
| Mapping `extensions::Extension` → `InstalledExtension` | C++ bridge | Avoids leaking `Extension*` semantics into Rust |
| JSON wire format between C++ bridge and Rust | matches existing `InstalledExtension` `serde` derive | No new type; `{id, name, version, enabled}` |
| Lifecycle of the bridge | profile-scoped (BrowserContextKeyedService or owned by `MahoBrowserMainExtraParts`) | Profiles can come and go (incognito); registry is per-profile |
| Round-trip mutations | C++ handler → `ExtensionRegistrar` → registry → observer → FFI seed | Avoids two-writer race |

### 3.3 What does *not* change

- The `InstalledExtension` JSON shape produced by `maho_core_get_installed_extensions` — existing UI consumers continue to parse the same fields.
- `extension_bridge.rs`'s storage / runtime-message / tabs APIs — those are orthogonal to the registry sync.
- `maho_webstore_ua_throttle.cc` — confirmed not on this path.

---

## 4. Phases and PR Breakdown

Five PRs ordered by dependency and risk. Each is independently buildable; later PRs assume earlier ones have landed but do not require them to function (each phase is a no-op without the next).

### PR-1 — Rust bulk API + new `CoreUpdate` variant

**Files:**
- `maho/crates/maho-core/src/extension_bridge.rs`
- `maho/crates/maho-core/src/maho_core.rs`
- `maho/crates/maho-types/src/events/core_update.rs`
- `maho/crates/maho-core/tests/extension_bridge_sync.rs` (new)

**Changes:**

1. `extension_bridge.rs`: add
   ```rust
   pub fn set_installed_extensions(&mut self, list: Vec<InstalledExtension>) {
       self.extensions = list.into_iter().map(|e| (e.id.clone(), e)).collect();
   }

   pub fn clear_installed_extensions(&mut self) {
       self.extensions.clear();
   }
   ```
   `register_extension` stays as-is (single-insert convenience).

2. `maho_core.rs`: re-expose the three mutation methods (`register_extension`, `set_installed_extensions`, `clear_installed_extensions`) and dispatch a typed `CoreUpdate::InstalledExtensionsUpdated` from each one (and from existing `toggle_extension` / `remove_extension`, replacing the ad-hoc string events at lines ~2262, ~2278).

3. `maho-types::events::core_update.rs`: add
   ```rust
   InstalledExtensionsUpdated,
   ```
   to the `CoreUpdate` enum. Carries no payload — consumers re-fetch via `maho_core_get_installed_extensions`. This matches the existing pattern used elsewhere (e.g. settings updates that send a "things changed" pulse and let the consumer pull).

4. New regression test `extension_bridge_sync.rs`:
   - `set_installed_extensions_replaces_map`
   - `clear_installed_extensions_empties_map`
   - `set_then_toggle_keeps_id_and_flips_enabled`
   - `register_extension_emits_update` (intercepts dispatched `CoreUpdate`s)

**Verification:**
- `cd maho && bun run test`
- `cd maho && bun run typecheck`
- `cd maho && cargo test -p maho-core extension_bridge_sync`

**Risk:** Low. Pure Rust, additive. No FFI consumer is calling the new variant yet.

---

### PR-2 — FFI surface for register/set/clear and header regen

**Files:**
- `maho/crates/maho-ffi/src/lib.rs`
- `maho-chromium/third_party/maho/maho_ffi.h` (regenerated)
- `maho-chromium/build/scripts/build_maho_core.py` invocation (rebuild prebuilt static lib)

**Changes:**

1. `maho-ffi/src/lib.rs`: add three new `extern "C"` functions, mirroring the existing toggle/remove style (null guards, `CStr::from_ptr`, JSON parse via `serde_json`):
   ```rust
   pub unsafe extern "C" fn maho_core_register_extension(
       ptr: *mut MahoCore,
       json: *const c_char,
   ) -> bool;

   pub unsafe extern "C" fn maho_core_set_installed_extensions(
       ptr: *mut MahoCore,
       json_array: *const c_char,
   ) -> bool;

   pub unsafe extern "C" fn maho_core_clear_installed_extensions(
       ptr: *mut MahoCore,
   ) -> bool;
   ```
   The `json` payload for `register_extension` is `{"id":"...","name":"...","version":"...","enabled":bool}`. The `json_array` for `set_installed_extensions` is a JSON array of the same shape.

2. Regenerate `third_party/maho/maho_ffi.h` via cbindgen (the existing build flow). Inspect the diff: only the three new declarations should be added.

3. Run `python3 maho-chromium/build/scripts/build_maho_core.py` to rebuild the prebuilt static library checked into `third_party/maho/`. Commit the updated `.a` and the regenerated header together.

4. Do **not** touch GN files (per `AGENTS.md` rule).

**Verification:**
- `cd maho && bun run typecheck`
- `cd maho && bun run test`
- `python3 maho-chromium/build/scripts/build_maho_core.py` succeeds
- Spot-check: linking `maho-chromium` still resolves all old symbols (no removed names).

**Risk:** Low. Header is regenerated, not hand-edited. Existing symbols are unaffected.

---

### PR-3 — `MahoExtensionStateBridge` (C++ producer, observer-only)

**Files (new):**
- `maho-chromium/browser/extensions/maho_extension_state_bridge.h`
- `maho-chromium/browser/extensions/maho_extension_state_bridge.cc`

**Files (modified):**
- `maho-chromium/browser/maho_browser_main_extra_parts.cc`

**Changes:**

1. New class `MahoExtensionStateBridge`:
   - Constructor takes a `Profile*` (or `content::BrowserContext*`).
   - Calls `extensions::ExtensionRegistry::Get(profile)` and stores it as a raw_ptr.
   - Adds itself as a `ScopedObservation<extensions::ExtensionRegistry, extensions::ExtensionRegistryObserver>`.
   - Implements:
     - `OnExtensionLoaded(BrowserContext*, const Extension*)`
     - `OnExtensionUnloaded(BrowserContext*, const Extension*, UnloadedExtensionReason)`
     - `OnExtensionInstalled(BrowserContext*, const Extension*, bool is_update)`
     - `OnExtensionUninstalled(BrowserContext*, const Extension*, UninstallReason)`
   - Each handler calls a private `PushFullSnapshot()` that:
     1. Walks `registry_->enabled_extensions()` and `registry_->disabled_extensions()`.
     2. Maps each `extensions::Extension*` to the `InstalledExtension` JSON shape `{id, name, version, enabled}`. `enabled` is true iff the extension is in `enabled_extensions()`.
     3. Builds a `base::Value::List`, serializes via `base::JSONWriter::Write`, and calls `maho_core_set_installed_extensions(maho::GetCore(), json.c_str())`.
   - On construction, calls `PushFullSnapshot()` once to seed.
   - On destruction, calls `maho_core_clear_installed_extensions(maho::GetCore())` so a profile teardown doesn't leave stale data.
   - Threading: `DCHECK_CALLED_ON_VALID_SEQUENCE` on the UI thread; the FFI calls are short and synchronous, matching the rest of the Maho FFI usage.

2. `maho_browser_main_extra_parts.cc`: in the existing post-profile-init hook (`PostProfileInit` or wherever Maho already attaches per-profile services — match the existing style of nearby attachments), construct the bridge owned by a member `std::unique_ptr<MahoExtensionStateBridge>`. Only attach for the original (non-incognito) profile in the first cut; OTR profiles stay out of scope (see Section 7).

3. Excluded from this PR: the round-trip from Maho UI → `ExtensionRegistrar` (PR-4), and the `CoreUpdate` consumer subscription (PR-5). After PR-3 lands, Rust holds a correctly-seeded mirror but the existing UI surfaces still won't pick up live changes until they subscribe.

**Verification:**
- `python3 maho-chromium/build/scripts/build_maho.py` succeeds.
- Manual: launch the browser, open `chrome://maho-settings`, observe the previously-blank extension list now contains the same extensions visible in `chrome://extensions`.
- Manual: install an extension from the Web Store. Reload `chrome://maho-settings` — the new extension appears.
- Manual: uninstall via `chrome://extensions`. Reload `chrome://maho-settings` — the entry is gone.
- C++ unit test for `MahoExtensionStateBridge` mapping logic, using a `TestExtensionRegistry` fixture and a fake `MahoCore` FFI shim that records the JSON it received.

**Risk:** Medium. New file, new dependency on Chromium extensions APIs, but the observer pattern is well-established and Brave's settings handlers provide a working reference.

---

### PR-4 — Round-trip mutations through `ExtensionRegistrar`

**Files:**
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.cc`
- `maho-chromium/browser/extensions/maho_extension_state_bridge.{cc,h}` (helper)

**Changes:**

1. Add a small helper on `MahoExtensionStateBridge` (or a free function in the same TU):
   ```cpp
   bool RequestEnable(const std::string& id);
   bool RequestDisable(const std::string& id);
   bool RequestUninstall(const std::string& id);
   ```
   These call `extensions::ExtensionRegistrar::Get(profile)->EnableExtension(id)` / `DisableExtension(id, disable_reason::DISABLE_USER_ACTION)` / `UninstallExtension(id, ...)`. They do **not** push state directly — the registry observer will fire and PushFullSnapshot covers it.

2. Update `MahoSettingsPageHandler` and `MahoLocationBarUtilityPanelProvider`:
   - The current code path that calls `maho_core_toggle_extension` is replaced with a call to `RequestEnable` / `RequestDisable` (decided by the current Rust-side `enabled` field of the targeted extension).
   - The current call to `maho_core_remove_extension` is replaced with `RequestUninstall`.
   - The Rust-side mutating FFI calls (`maho_core_toggle_extension`, `maho_core_remove_extension`) are kept on the FFI surface but no longer called from these UI handlers. They remain available for Rust unit tests and any future Rust-only flows.

3. Reach the bridge from a UI handler: a small `BrowserContextKeyedServiceFactory` (or simply `MahoExtensionStateBridge::FromProfile(profile)`) is the cleanest accessor; choose whichever is consistent with the existing Maho service-attachment style in `maho_browser_main_extra_parts.cc`.

**Verification:**
- Build green.
- Manual: from `chrome://maho-settings`, click the toggle on an extension. Confirm `chrome://extensions` reflects the same disabled state. Confirm `chrome://maho-settings` itself updates (will only fully update once PR-5 lands; until then a manual refresh shows the new state).
- Manual: from `chrome://maho-settings`, click uninstall. Confirm the extension disappears from both pages.
- C++ unit test: a fake `ExtensionRegistrar` fixture verifying that the handler's "toggle" call results in `EnableExtension` or `DisableExtension` being invoked with the correct id.

**Risk:** Medium. Touches two production handlers but only swaps one FFI call for one C++ method call per action.

---

### PR-5 — Live UI refresh via `CoreUpdate::InstalledExtensionsUpdated`

**Files:**
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.cc`

**Changes:**

1. Both handlers already participate in Maho's `CoreUpdate` subscription mechanism for other concerns (settings, search engines, etc.). Add a handler arm for `InstalledExtensionsUpdated`:
   - On receipt: re-call `maho_core_get_installed_extensions`, parse the JSON, and push the new list into the WebUI / Views surface they own.

2. `MahoSettingsPageHandler`: emit a Mojo notification (e.g. `OnInstalledExtensionsChanged(list)`) so the Settings WebUI can re-render without a full page reload. The Mojo interface stub for this likely already exists for sibling pull-style endpoints; if not, add a minimal `OnInstalledExtensionsChanged` notification (no schema-shape changes elsewhere — extensions remain `[{id,name,version,enabled}]`).

3. `MahoLocationBarUtilityPanelProvider`: invalidate / rebuild whatever cached views depend on the extension list. Match the existing invalidation pattern used by other utility-panel sources.

**Verification:**
- Build green.
- Manual: with `chrome://maho-settings` open, install a new extension from the Web Store in another tab — the Settings page updates without a refresh.
- Manual: disable an extension from `chrome://extensions` — the Settings page reflects it within the next event-dispatch tick.
- Manual: same flow exercising the location-bar utility panel (open the panel, observe live updates as extensions are enabled/disabled).

**Risk:** Low–medium. Adds a subscriber to an already-running event stream; failure mode is "UI doesn't refresh until manual reload," which is the pre-PR-5 baseline.

---

## 5. Sequencing and Dependencies

| PR | Depends on | Can land independently? | If landed alone, observable result |
|---|---|---|---|
| PR-1 | None | Yes | New Rust API + `CoreUpdate` variant; no behavior change |
| PR-2 | PR-1 | Yes | New FFI symbols + regenerated header; no behavior change |
| PR-3 | PR-2 | Yes | Settings page now shows real extensions on first paint; no live updates |
| PR-4 | PR-3 | Yes | Toggle / uninstall from Maho UI flow through `ExtensionRegistrar`; live updates still require manual refresh |
| PR-5 | PR-3 (PR-4 strongly recommended) | Yes | Settings + utility panel update live in response to registry changes |

Recommended landing order: **PR-1 → PR-2 → PR-3 → PR-4 → PR-5.**

Each PR is reversible without backing out subsequent ones (e.g. PR-4 can be reverted independently if `ExtensionRegistrar` integration is regressing; PR-3's seeding will still work).

---

## 6. Out of Scope (Non-Goals)

Explicit, do not violate:

1. **Persistence of installed extensions in Rust storage.** Today `maho-storage` does not have a `extensions` table, and this plan does not add one. The bridge re-seeds on every browser start by walking the live `ExtensionRegistry`. Persistence would be redundant with Chromium's own `Preferences`/`Extensions State` JSON store.
2. **OTR / incognito profile support.** The bridge is attached to the original profile only. Extensions in incognito are a separate concern that would need its own seeded `MahoCore` instance per profile.
3. **Multi-profile support.** Same reason. One `MahoCore` per browser today; a real multi-profile world is its own project.
4. **GN edits.** Per `AGENTS.md`, do not modify any `BUILD.gn` outside what cbindgen / build-script regen produces. New C++ files in `maho-chromium/browser/extensions/` will be added to whatever `BUILD.gn` already covers `maho-chromium/browser/` only via the existing build-script regeneration flow if applicable; otherwise the user is asked to approve the GN edit explicitly.
5. **`maho_settings_ui.cc::TabHelper::CreateForWebContents()` cleanup.** Listed as secondary scope. It is not the producer path and was misidentified during earlier discovery. It can be removed in a follow-up cleanup PR if it has no remaining callers; this plan does not depend on or block on its removal.
6. **`maho-extension` crate.** That crate exists for runtime extension execution semantics (storage, messaging, etc.). State sync of *installed* extensions is `maho-core`'s responsibility; this plan does not touch `maho-extension`.
7. **Extension permissions, blocklisting, policy-installed extensions.** All currently uniformly mapped to `enabled: true/false`. Surfacing additional disable reasons (`policy`, `blocklist`, `corrupt`) is a future enhancement.
8. **Web Store install / drag-and-drop install UX.** Out of scope; the existing `maho_webstore_ua_throttle.cc` handles UA spoofing for `chrome.google.com/webstore` and is not touched by this plan.
9. **Localization of the new strings.** No new user-facing strings introduced.

---

## 7. Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| `cbindgen` regeneration produces unrelated diffs in `maho_ffi.h` | Medium | Low | Review the header diff manually; if upstream-unrelated changes appear, file a separate cleanup PR for them and limit this PR to the three new symbols |
| `MahoExtensionStateBridge` outlives its `Profile` and crashes on observer callback | Medium | High | Use `ScopedObservation`; reset in dtor; own the bridge in `MahoBrowserMainExtraParts` so its lifetime ≤ Profile lifetime |
| Snapshot push on every `OnExtension*` event is too chatty | Low | Low | Snapshot is small (typical user has ≤30 extensions), JSON is short. If profiling shows hot spots, debounce via a `base::OneShotTimer` with a 50ms delay |
| Rust-side `set_installed_extensions` race with concurrent `toggle_extension` from another caller | Low | Medium | `MahoCore` is `Rc<RefCell<>>`-based and serialized on a single thread today; the FFI layer already enforces this. The `set_installed_extensions` call always runs on the same UI sequence as the existing FFI calls |
| `ExtensionRegistrar` API differs across Chromium milestones | Low | Medium | The plan targets the API as it exists at the pinned Chromium SHA; if upstream renames during a roll, the bridge is the only file that needs to follow |
| Toggling an extension from Maho UI triggers a registry callback that re-pushes state, which triggers another UI refresh — feedback loop | Low | Low | The push is content-addressed (full snapshot replaces map); no second push originates from the seed call. Verify in unit test that `set_installed_extensions` + same content does not re-emit `CoreUpdate` (or that it's idempotent) |
| `maho_core_clear_installed_extensions` called during shutdown races with consumer reads | Very low | Low | Shutdown ordering: bridge dtor runs after UI handlers detach. Add a `DCHECK` |

---

## 8. Verification Matrix

| Surface | PR-1 | PR-2 | PR-3 | PR-4 | PR-5 |
|---|---|---|---|---|---|
| Rust unit tests | New `extension_bridge_sync` suite | — | — | — | — |
| C++ unit tests | — | — | Bridge mapping test | Registrar round-trip test | — |
| `cd maho && bun run typecheck` | Pass | Pass | Pass | Pass | Pass |
| `cd maho && bun run test` | Pass | Pass | Pass | Pass | Pass |
| `python3 maho-chromium/build/scripts/build_maho_core.py` | Not required | Required | Not required | Not required | Not required |
| `python3 maho-chromium/build/scripts/build_maho.py` | Not required | Required | Required | Required | Required |
| Manual: `chrome://maho-settings` shows real extensions | — | — | Verify | Verify | Verify |
| Manual: toggle from Maho UI flips state in `chrome://extensions` | — | — | — | Verify | Verify |
| Manual: change in `chrome://extensions` reflects live in Maho UI | — | — | — | — | Verify |
| Manual: utility panel updates with extension list | — | — | Verify (post-mount) | Verify | Verify (live) |

---

## 9. Open Questions

None blocking. Possible follow-ups discovered during planning:

1. **Should incognito / OTR profiles get their own bridge?** Deferred — see Out of Scope #2.
2. **Should `disable_reason` (policy, blocklist, corrupt) surface in `InstalledExtension`?** Possible enhancement; would extend the JSON shape and require a UI surface decision.
3. **Should `maho-storage` cache the snapshot for fast first-paint before the registry has finished loading?** Not needed — registry is available synchronously from `Profile` initialization in the existing bootstrap hook, and `PushFullSnapshot()` runs immediately after construction.
4. **Should `maho_settings_ui.cc::TabHelper::CreateForWebContents()` be removed in this same series?** Likely yes, but it is independent and noted for a separate cleanup.

---

## 10. Files Touched (full list)

### New
- `maho-chromium/browser/extensions/maho_extension_state_bridge.h`
- `maho-chromium/browser/extensions/maho_extension_state_bridge.cc`
- `maho/crates/maho-core/tests/extension_bridge_sync.rs`

### Modified
- `maho/crates/maho-core/src/extension_bridge.rs`
- `maho/crates/maho-core/src/maho_core.rs`
- `maho/crates/maho-ffi/src/lib.rs`
- `maho/crates/maho-types/src/events/core_update.rs`
- `maho-chromium/third_party/maho/maho_ffi.h` (regenerated)
- `maho-chromium/third_party/maho/` prebuilt static lib (rebuilt via `build_maho_core.py`)
- `maho-chromium/browser/maho_browser_main_extra_parts.cc`
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.cc`

### Read-only verification
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_ui.cc` (confirms `TabHelper::CreateForWebContents()` is not the producer path)
- `maho-chromium/browser/net/maho_webstore_ua_throttle.cc` (confirms it is not on this path)
- Upstream: `extensions/browser/extension_registry.h`, `extensions/browser/extension_registry_observer.h`, `extensions/browser/extension_registrar.h`

### Untouched (per scope / `AGENTS.md`)
- All `BUILD.gn` files (require explicit user approval)
- `maho/crates/maho-extension` (unrelated runtime concern)
- `maho-storage` (no persistence added — see Out of Scope #1)
- `maho/crates/maho-types/src/events/shell_event.rs` (no shell-event surface required; pull-based refresh via `CoreUpdate` is sufficient)

---

## 11. Acceptance Checklist

- [ ] User reviews and approves this plan
- [ ] PR-1 lands: Rust bulk API + typed `CoreUpdate::InstalledExtensionsUpdated`
- [ ] PR-2 lands: FFI symbols + regenerated header + rebuilt prebuilt static lib
- [ ] PR-3 lands: `MahoExtensionStateBridge` seeds Rust on profile start
- [ ] PR-4 lands: Maho-UI mutations route through `ExtensionRegistrar`
- [ ] PR-5 lands: live UI refresh on `InstalledExtensionsUpdated`
- [ ] Manual matrix from Section 8 passes end-to-end
- [ ] No GN edits made without explicit user approval
- [ ] No persistence layer added to `maho-storage`
- [ ] Out-of-scope items in Section 6 remain untouched
