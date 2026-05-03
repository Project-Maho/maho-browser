# Swift vs Chromium settings parity matrix and hybrid implementation plan

This document is the current planning contract for the browser-hosted Maho settings redesign. It captures what the Swift app still defines as the product reference, what the Chromium/WebUI path actually ships today, and how to close the gap without drifting away from the approved hybrid direction.

The product direction is fixed:

- settings remain browser-hosted at `chrome://maho-settings`
- the new surface should adopt the Swift settings shell, pane structure, and visual primitives
- existing Chromium pref wiring should be reused where valid
- richer panes should be split between Chromium-side service APIs and Maho-core bridge work

## What this document tracks

- the Swift settings source-of-truth under `macos-shell/Sources/Settings/`
- the current browser-hosted WebUI surface under `chromium/src/maho/browser/resources/maho_settings/`
- exactly which settings are wired today through the current Mojo bridge
- confirmed menu and command routing into settings
- what is still missing before the hybrid redesign can replace the current flat page

## Status labels

- **wired**: backed by real code and current repo evidence
- **partial**: present, but structurally incomplete versus the Swift reference
- **missing**: not present in the current browser-hosted surface
- **legacy path**: still reachable in code, but not aligned with the intended `chrome://maho-settings` destination

## Source-of-truth: Swift settings structure

The Swift pane registry in `macos-shell/Sources/Settings/SettingsWindowController.swift` defines 16 panes in this order:

1. General
2. Appearance
3. Privacy
4. Shortcuts
5. Profiles
6. Advanced
7. Notifications
8. Autofill
9. Content Blocker
10. Air Traffic Control
11. Passwords
12. Account & Sync
13. CSS Mods
14. Theme Store
15. Extensions
16. Maho AI

This pane list is the structural reference for the hybrid redesign.

### Swift shell/layout reference

The current Swift shell also establishes the target layout vocabulary:

- window size: `940x980`
- dark appearance: `darkAqua`
- sidebar width: `222`
- sidebar row height: `40`
- split sidebar/content structure with card-based pane content

`GeneralSettingsView.swift` also confirms that the old native app contained significantly richer pane content than the current browser-hosted WebUI.

## Current browser-hosted settings surface

The live WebUI at `chromium/src/maho/browser/resources/maho_settings/app.ts` is not a Swift-pane implementation yet. It is a flat preference shell that groups currently supported settings into 6 categories:

1. General
2. Appearance
3. Search
4. Privacy
5. Advanced
6. Notifications

The page currently renders:

- a custom sidebar generated from the grouped categories
- a hero/header section
- one card per category
- rows generated directly from the current settings payload

This is a functioning pref surface, but it is not yet the hybrid Swift-style pane host.

## Current wired settings

The current bridge in `chromium/src/maho/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` wires 14 settings today:

| Key | Status | Notes |
| --- | --- | --- |
| `general.homepage` | wired | String pref-backed. |
| `appearance.show_home_button` | wired | Bool pref-backed. |
| `general.restore_on_startup` | wired | Int pref-backed. |
| `appearance.font_size` | wired | Int pref-backed, clamped on write. |
| `appearance.minimum_font_size` | wired | Int pref-backed, clamped on write. |
| `appearance.theme` | wired | Int pref-backed. |
| `search.suggestions` | wired | Bool pref-backed. |
| `privacy.do_not_track` | wired | Bool pref-backed. |
| `privacy.block_third_party_cookies` | wired | Bool-like WebUI control backed by Chromium cookie mode int values. |
| `privacy.safe_browsing` | wired | Bool pref-backed. |
| `advanced.hardware_acceleration` | wired | Uses `g_browser_process->local_state()` rather than profile prefs. |
| `advanced.preload_pages` | wired | Int pref-backed; deprecated value `1` is normalized to standard value `0` on read. |
| `general.prompt_for_download` | wired | Bool pref-backed. |
| `notifications.quiet_permission_ui` | wired | Bool pref-backed. |

### Backend behaviors that matter

- `advanced.hardware_acceleration` is special-cased to read and write through `g_browser_process->local_state()`.
- `advanced.preload_pages` maps Chromium network prediction values into the WebUI and normalizes deprecated read value `1` to `0`.
- the page handler registers change listeners for all mapped prefs and pushes live updates back to the page.

## What does not exist yet: settings search

There is currently no implemented settings search box, query model, or filter UI in:

- `chromium/src/maho/browser/resources/maho_settings/settings.html`
- `chromium/src/maho/browser/resources/maho_settings/app.ts`

The only current use of the word “search” in the live page is:

- the **Search** category itself
- the single pref-backed control `search.suggestions`

This means the hybrid redesign must treat settings search as a net-new feature if it is desired.

## Confirmed routing into settings

The current routing state is improved, but not fully unified.

### Confirmed `chrome://maho-settings` entry points

These routes are already aligned with Maho settings:

- `chrome/browser/ui/browser_command_controller.cc`
  - `IDC_OPTIONS` calls `chrome::ShowMahoSettings(browser_->GetBrowserForOpeningWebUi())`
- `chrome/browser/ui/browser_actions.cc`
  - the browser action calls `chrome::ShowMahoSettings(bwi)`
- `chrome/browser/app_controller_mac.mm`
  - macOS app menu `showPreferences:` -> `showPreferencesForProfile:` -> `chrome::ShowMahoSettingsForProfile(profile)`

### Remaining legacy settings routes

These code paths still target standard Chromium settings behavior:

- `chrome/browser/ui/chrome_pages.cc`
  - `ShowSettings(...)` still routes to `ShowSettingsSubPage(...)`
  - `ShowSettingsSubPageInTabbedBrowser(...)` still opens the standard settings URL via `GetSettingsUrl(sub_page)`
- `chrome/browser/ui/browser_mac.cc`
  - `OpenOptionsWindow(Profile*)` still calls `ShowSettings(browser)`

### Routing conclusion

Do not claim full route unification yet.

The dominant user-facing menu and command paths already open `chrome://maho-settings`, but legacy `ShowSettings(...)` code still exists and should be resolved explicitly during the hybrid migration.

## Swift vs current WebUI parity matrix

| Swift pane | Current browser-hosted equivalent | Status | Notes |
| --- | --- | --- | --- |
| General | General | partial | Present, but only a small pref-backed subset versus the richer Swift pane. |
| Appearance | Appearance | partial | Present, pref-backed subset only. |
| Privacy | Privacy | partial | Present, pref-backed subset only. |
| Shortcuts | none | missing | No pane or structured CRUD model in the current WebUI. |
| Profiles | none | missing | No browser-hosted pane yet. |
| Advanced | Advanced | partial | Present, limited pref-backed subset only. |
| Notifications | Notifications | partial | Present, currently just quiet notification permission UI. |
| Autofill | none | missing | No pane yet. |
| Content Blocker | none | missing | No pane yet. |
| Air Traffic Control | none | missing | No pane yet. |
| Passwords | none | missing | No pane yet. |
| Account & Sync | none | missing | No dedicated current WebUI pane. |
| CSS Mods | none | missing | No pane yet. |
| Theme Store | none | missing | No pane yet. |
| Extensions | none | missing | No pane yet. |
| Maho AI | none | missing | No dedicated current WebUI pane. |

## Work buckets for the hybrid redesign

### Bucket A: Swift-shell WebUI architecture + pref-backed pane migration

Use this for work that can stay within the browser-hosted settings stack and existing Chromium pref/service seams.

Examples:

- WebUI shell/sidebar/pane-host architecture
- reusable card/row/table primitives modeled after the Swift app
- migration of the current 14 pref-backed settings into the new pane structure
- General, Appearance, Privacy, Advanced, and Notifications first-pass parity slices where existing wiring is already available

### Bucket B: Chromium-side structured service APIs

Use this for browser-hosted panes that need list-based CRUD, search, or table interactions beyond a flat pref map.

Examples:

- Shortcuts
- Autofill
- Passwords
- Extensions
- parts of Profiles that can be represented entirely from Chromium-side data/services

### Bucket C: Maho-core or product-specific bridge work

Use this for panes whose state or workflows live outside the current Chromium pref/service model.

Examples:

- Profiles beyond simple browser-side metadata
- Content Blocker management
- Air Traffic Control
- CSS Mods
- Theme Store
- Account & Sync
- Maho AI

## Recommended implementation sequence

### Phase 0: replace the flat page with a real hybrid shell

Goal: move from the current metadata-driven 6-category page to a Swift-structured browser-hosted shell.

Deliverables:

- sidebar modeled after the Swift pane registry
- pane registry and pane host in WebUI
- reusable Swift-inspired card/row/layout primitives
- ability to render empty, pref-backed, and future structured panes in one shell

### Phase 1: migrate the currently wired settings into that shell

Goal: keep all current live settings functional while moving them into the new pane structure.

Initial pane targets:

- General
- Appearance
- Privacy
- Advanced
- Notifications

Important constraint:

- this phase should reuse the current 14-setting bridge, not redesign backend APIs unnecessarily

### Phase 2: add structured Chromium-side panes

Goal: introduce explicit service APIs for panes that do not fit the flat pref map.

Likely first candidates:

- Shortcuts
- Autofill
- Passwords
- Extensions

### Phase 3: add Maho-specific bridge panes

Goal: close the gap for product-specific panes that need Maho-core or shell-owned state.

Likely candidates:

- Profiles
- Content Blocker
- Air Traffic Control
- CSS Mods
- Theme Store
- Account & Sync
- Maho AI

### Phase 4: route cleanup and polish

Goal: finish the platform integration so settings entry points are explicit and coherent.

This phase should include:

- explicit decision on remaining legacy `ShowSettings(...)` callers
- cleanup or redirection of stale settings routes where appropriate
- visual polish toward the Swift settings feel
- optional settings-search feature only after the shell exists

## Recommended next slice

The next practical implementation slice should be:

1. build the browser-hosted shell/sidebar/pane-host architecture
2. migrate the existing 14 pref-backed settings into the corresponding Swift-aligned panes
3. decide what to do with remaining legacy `ShowSettings(...)` routes before claiming route unification

That sequence preserves working behavior, moves the product toward the approved hybrid design, and avoids mixing shell work with deeper backend work too early.

## Engineer notes

- Do not treat the current 6-category WebUI as structural parity with the Swift app.
- Do not claim settings search exists today.
- Do not claim route unification is complete.
- Keep the browser-hosted delivery model fixed throughout the migration.
- Treat the Swift pane registry as the structural source-of-truth and the current 14-setting bridge as the initial functional base.
