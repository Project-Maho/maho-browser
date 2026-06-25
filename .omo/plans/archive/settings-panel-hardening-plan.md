# Maho Settings Panel Hardening Plan

**Date:** 2026-05-10
**Branch:** `main`
**Scope:** `maho-chromium/browser/resources/maho_settings/`, `maho-chromium/browser/ui/webui/maho_settings/`, optional `chromium/src/chrome/browser/resources/settings/settings_ui/`

---

## Problem Statement

The settings panel currently mixes security-sensitive password data with broad page state, rerenders the full app on every settings change, and handles several async flows without strong failure boundaries. That creates three classes of risk:

1. Sensitive password data can leak into WebUI state and DOM.
2. Async actions can fail or race without clear user feedback.
3. The panel is too monolithic, which makes correctness fixes hard and increases the chance of regressions.

This PR plan focuses on the audited issues already identified:

1. Password plaintext exposure in WebUI
2. Full app rerender architecture
3. Async failure handling gaps
4. Password retry loop
5. Search race and debounce issues
6. Accessibility gaps
7. Oversized page handler
8. Embedded Chromium settings boundary

---

## Goals

1. Stop plaintext password data from living in long-lived UI state.
2. Make async settings actions fail safely and visibly.
3. Prevent stale search results and repeated password reload loops.
4. Keep the embedded Chromium settings surface tightly bounded.
5. Reduce the size of the page handler by extracting domain helpers.
6. Leave the door open for a later refactor that removes the full app rerender pattern.

## Non-Goals

1. No redesign of unrelated settings panes.
2. No migration of unrelated browser settings infrastructure.
3. No broad Chromium upstream edits unless the embedded settings boundary needs it.
4. No new product features.

---

## Prioritized Issue Inventory

| Priority | Issue | Why it matters | Primary files | PR slice |
|---|---|---|---|---|
| P0 | Password plaintext exposure in WebUI | Raw password values should not sit in shared UI state or be rendered unless the user explicitly asks for them | `browser/resources/maho_settings/app.ts`, `browser/ui/webui/maho_settings/maho_settings_page_handler.cc`, possibly `maho_settings.mojom` | Slice 1 |
| P0 | Embedded Chromium settings boundary | The iframe to `chrome://settings` needs a strict, intentional boundary so the Maho settings shell does not become a loose gateway to upstream settings surfaces | `browser/ui/webui/maho_settings/maho_settings_ui.cc`, `browser/resources/maho_settings/app.ts`, possibly `chromium/src/chrome/browser/resources/settings/settings_ui/` | Slice 1 |
| P1 | Async failure handling gaps | Several handlers disable controls and call Mojo methods without a reliable error path or user-visible recovery | `browser/resources/maho_settings/app.ts` | Slice 1 |
| P1 | Password retry loop | Password loading and search refresh paths can re-enter loading or refresh states without a clean retry contract | `browser/resources/maho_settings/app.ts`, `browser/ui/webui/maho_settings/maho_settings_page_handler.cc` | Slice 1 |
| P1 | Search race and debounce issues | Fast typing can cause stale search responses to win over newer ones | `browser/resources/maho_settings/app.ts`, `browser/ui/webui/maho_settings/maho_settings_page_handler.cc` | Slice 2 |
| P2 | Accessibility gaps | The settings panel has interactive controls that need stronger labels, focus handling, and status announcements | `browser/resources/maho_settings/app.ts`, `browser/resources/maho_settings/settings.html`, `browser/resources/maho_settings/settings.css` | Slice 2 |
| P2 | Full app rerender architecture | Replacing the whole shell on every settings update causes focus loss, stale async work, and brittle state handling | `browser/resources/maho_settings/app.ts` | Slice 3 |
| P2 | Oversized page handler | `MahoSettingsPageHandler` currently bundles pref mapping, profiles, ATC, sync, theme store, autofill, content blocker, and extensions in one file | `browser/ui/webui/maho_settings/maho_settings_page_handler.cc` | Slice 3 |

---

## Execution Plan

### Slice 1 — Security and correctness fixes

This slice should land first.

#### 1. Remove plaintext password exposure from shared UI state

**Target:**
- `browser/resources/maho_settings/app.ts`
- `browser/ui/webui/maho_settings/maho_settings_page_handler.cc`
- `maho_settings.mojom` if an explicit reveal RPC or error callback is needed

**Actions:**
1. Stop copying `password.password` into the general `currentSavedPasswords` array unless the row is explicitly revealed.
2. Keep reveal state separate from the base password list data.
3. Render only masked or empty password placeholders by default.
4. Require an explicit per-row reveal action for any plaintext display.
5. Clear any reveal state when the password list is reloaded or the pane changes.

**Notes:**
- The current app code maps password payloads directly into shared state in `loadPasswordProviderState()` and `refreshSavedPasswords()`.
- If the backend currently returns plaintext for every saved password, this slice should narrow the UI contract first, then adjust the handler contract if needed.

#### 2. Harden password loading and retry behavior

**Target:**
- `browser/resources/maho_settings/app.ts`
- `browser/ui/webui/maho_settings/maho_settings_page_handler.cc`

**Actions:**
1. Replace the current load and refresh flags with a single in-flight guard and a single retry path.
2. Ensure the password loader always resets loading state in both success and failure branches.
3. Avoid automatic reload loops after a failed search or failed provider fetch.
4. Show a visible retry control instead of silently re-calling the loader from render paths.
5. Keep the last successful list visible until a deliberate retry replaces it.

#### 3. Add explicit async failure handling for password, sync, theme, and shortcut actions

**Target:**
- `browser/resources/maho_settings/app.ts`

**Actions:**
1. Wrap async calls in a small local helper that captures rejected promises.
2. Re-enable controls in `finally` blocks, not only on the success path.
3. Show an inline error state for the affected row or pane section when the action fails.
4. Keep success feedback local to the control that was used, rather than rerendering the whole page.

#### 4. Lock down the embedded Chromium settings boundary

**Target:**
- `browser/ui/webui/maho_settings/maho_settings_ui.cc`
- `browser/resources/maho_settings/app.ts`
- `chromium/src/chrome/browser/resources/settings/settings_ui/` only if the boundary requires an upstream adjustment

**Actions:**
1. Keep the iframe limited to the exact embedded settings surface that is intentionally supported.
2. Do not broaden the allowed `child-src` policy beyond the minimum set needed.
3. If a pane only needs a narrow upstream surface, keep the embed isolated to that pane and do not reuse the iframe shell elsewhere.
4. If the upstream settings dependency can be replaced with a native Maho pane later, mark that as a follow-up, not part of the security fix.

### Slice 2 — Interaction quality fixes

This slice should land after the security and failure-handling work.

#### 5. Fix password search races and add debounce

**Target:**
- `browser/resources/maho_settings/app.ts`
- `browser/ui/webui/maho_settings/maho_settings_page_handler.cc` if search requests need stronger request scoping

**Actions:**
1. Debounce password search input before calling the backend.
2. Track a monotonically increasing search request token.
3. Ignore stale responses that arrive after a newer query.
4. Reset or preserve the visible list based on the active query, not on response order.
5. Keep search behavior consistent between empty-query and typed-query modes.

#### 6. Close accessibility gaps in the settings shell

**Target:**
- `browser/resources/maho_settings/app.ts`
- `browser/resources/maho_settings/settings.html`
- `browser/resources/maho_settings/settings.css`

**Actions:**
1. Verify every interactive control has a stable accessible name.
2. Add focus management when a pane changes, especially after actions that currently rerender content.
3. Add aria-live or status text for loading, success, and failure states.
4. Make icon-only buttons and toggles readable by assistive tech.
5. Confirm keyboard navigation works across search, password reveal, retry, and embed entry points.

### Slice 3 — Architecture cleanup and handler split

This slice should come last because it is the least urgent and the easiest to break.

#### 7. Reduce the full app rerender pattern

**Target:**
- `browser/resources/maho_settings/app.ts`

**Actions:**
1. Keep the shell, sidebar, and top-level layout stable.
2. Update only the active pane body when settings data changes.
3. Preserve focus and scroll position during pane updates.
4. Separate passive state updates from user-initiated navigation.
5. Move toward pane-level render functions instead of one global rerender entry point.

**Suggested structure:**
- Static shell creation
- Pane selection and pane body update
- Local render function per domain
- Shared helper for async and error state

#### 8. Split the oversized page handler into domain helpers

**Target:**
- `browser/ui/webui/maho_settings/maho_settings_page_handler.cc`

**Recommended extraction targets:**
1. Pref bridge helpers
2. Password provider helpers
3. Profiles helpers
4. ATC helpers
5. Sync helpers
6. Theme store cache helpers
7. Autofill helpers
8. Content blocker helpers
9. Extension helpers

**Actions:**
1. Move domain-specific parse and serialize helpers into small files under `browser/ui/webui/maho_settings/`.
2. Keep `MahoSettingsPageHandler` as the Mojo-facing coordinator.
3. Preserve current behavior in the first pass, then simplify call sites.
4. Add focused helper-level tests where the repo already has a test harness for the same area.

---

## File Targets

### Primary files
1. `maho-chromium/browser/resources/maho_settings/app.ts`
2. `maho-chromium/browser/resources/maho_settings/settings.html`
3. `maho-chromium/browser/resources/maho_settings/settings.css`
4. `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc`
5. `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.h`
6. `maho-chromium/browser/ui/webui/maho_settings/maho_settings_ui.cc`

### Conditional files
1. `maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom` if a new reveal or error contract is needed
2. `chromium/src/chrome/browser/resources/settings/settings_ui/` only if the embedded Chromium settings surface needs an upstream boundary change
3. `maho-chromium/browser/resources/maho_settings/models.ts` if UI state types need to distinguish masked data from revealed data
4. `maho-chromium/browser/resources/maho_settings/schema/*` if pane metadata or domain grouping needs to change for the new render split

---

## Recommended Execution Order

1. Remove plaintext password exposure.
2. Add explicit async error handling.
3. Fix the password retry loop.
4. Lock down the embedded Chromium settings boundary.
5. Add search debounce and stale-result guards.
6. Patch accessibility gaps.
7. Refactor the render flow to stop full app rerenders.
8. Split the page handler into domain helpers.

---

## Verification Strategy

### Static checks
1. Run TypeScript diagnostics on `maho_settings` resources.
2. Run C++ diagnostics on the settings WebUI handler and UI files.
3. Confirm no new diagnostics in touched files.

### Build checks
1. Rebuild the Maho overlay after the settings changes.
2. Rebuild any touched generated interface files if the Mojo contract changes.

### Runtime smoke checks
1. Open `chrome://maho-settings`.
2. Verify the passwords pane shows no plaintext by default.
3. Trigger password search, then type quickly and confirm stale results do not win.
4. Force a backend failure path and confirm the pane shows a usable retry state.
5. Switch to the embedded Chromium settings pane and confirm only the intended surface loads.
6. Check keyboard navigation and focus after pane changes and async actions.

### Regression focus
1. Password list loading and reveal behavior
2. Search typing cadence
3. Sync and theme-store actions
4. Embedded settings pane load
5. Sidebar navigation and focus retention

---

## Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Changing password data shape could break current render logic | High | Make the UI contract change first, then adjust the backend or Mojo shape only if needed |
| Search token or debounce logic could hide valid results | Medium | Keep the last good result visible until the next confirmed search completes |
| Splitting the page handler could introduce behavior drift | Medium | Move code in small domain chunks and keep the first-pass behavior identical |
| Tightening the iframe boundary could break the embed pane | Medium | Keep the boundary as narrow as possible and verify the exact allowed surface before broadening it |
| Partial rerender work could regress focus handling | Medium | Land the refactor after the correctness fixes and verify keyboard flow manually |

---

## Acceptance Criteria

1. Password plaintext is no longer kept in shared WebUI state by default.
2. Password loading and search have a clear retry path with no silent loops.
3. Async failures produce visible, local feedback.
4. Search results cannot race newer queries.
5. Accessibility gaps are reduced in the touched panes.
6. The embedded Chromium settings surface stays intentionally bounded.
7. The page handler is split enough that each domain concern is easier to reason about.
8. The full app rerender pattern is either removed or clearly isolated for a later follow-up.
