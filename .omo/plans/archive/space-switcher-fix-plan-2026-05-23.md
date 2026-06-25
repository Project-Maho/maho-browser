# Space Switcher Fix Plan

**Created:** 2026-05-23  
**Status:** Draft — pending Momus review  
**Primary scope:** `maho-chromium/browser/maho_space_profile_bridge.{h,cc}`  
**Secondary scope:** `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`, `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc`, `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_view.cc`, `maho-chromium/browser/ui/views/spaces_overlay/maho_spaces_overlay_view.cc`, `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`, related sidebar tests

---

## 1. Objective

Fix the Chromium-fork Space Switcher so every user-facing switch path updates the same activation model, while preserving the requirement that active-space state is browser/window-scoped.

This plan addresses the current broken behavior where command actions, footer dots, and overlay selections write different active-space stores and leave the UI out of sync.

---

## 2. Grounded Decisions

### 2.1 Browser-scoped active space is the canonical UI state

For any browser-bound surface, the canonical active space is `MahoSpaceProfileBridge::GetActiveSpaceId(browser)`.

- `browser_active_space_[browser]` is the state that determines which space a given browser window considers active.
- The bridge-global `active_space_id_` remains a fallback/seed for code that has no browser context and for startup hydration, but it is not the preferred source for browser UI.

### 2.2 Rust core remains the canonical data engine, not the canonical per-window UI state

Rust core still owns space metadata, tab/favorites/folder data, and activation side effects that must persist or emit updates.

However, the current core active-space value is process-global, so it must not be treated as the sole browser-window UI truth.

### 2.3 All user-initiated space switches must go through one bridge entry point

No UI path should call `maho_core_activate_space(...)` directly.

The bridge activation path must be the single writer chokepoint for user-initiated switches so it can:

1. activate the target space in core,
2. update the browser-scoped bridge state,
3. update the global bridge fallback,
4. refresh theme state from core,
5. notify observers exactly once.

### 2.4 Existing ADRs stay in force

This plan does **not** change:

- the theme picker ADR in `.sisyphus/plans/theme-picker-architecture-decision.md`,
- the native create-space form architecture,
- GN/build configuration,
- the larger Arc-parity roadmap in `maho-chromium/.sisyphus/plans/space-profile-gap-fill.md`.

---

## 3. Current Failure Topology

### 3.1 Divergent writers

The current code has three incompatible write paths:

1. **Command actions** (`maho_command_action_handler.cc`) call `maho_core_activate_space(...)` directly.  
   Result: core changes, bridge stays stale.

2. **Footer dots** (`maho_sidebar_footer_view.cc`) call `bridge->SwitchToSpace(space_id)` without a `Browser*`.  
   Result: global bridge changes, browser-scoped bridge may stay stale.

3. **Overlay / spaces view / create-space success path** call `bridge->SwitchToSpace(browser_, space_id)`.  
   Result: browser-scoped bridge changes, but this is not consistently shared by all switch writers.

### 3.2 Divergent readers

`maho_sidebar_state_adapter.cc` reads both:

- core active space (`GetCoreFooterSpaceState()`), and
- browser-scoped bridge active space (`BuildFooterModel()` override).

This means the same refresh can combine data from one source with highlighting from another.

### 3.3 Startup is the only explicit core → bridge sync

`maho_browser_main_extra_parts.cc` hydrates the bridge once from core. There is no steady-state subscriber that reconciles later direct core activations back into the bridge.

---

## 4. Scope

### In scope

1. Unify all space-switch writers behind the bridge.
2. Make footer switching browser-scoped.
3. Keep bridge/global fallback behavior explicit instead of accidental.
4. Ensure command-triggered switching updates bridge state, theme state, and observers.
5. Add tests that catch the current desync.

### Out of scope

1. Arc-parity redesign of the switcher (emoji icons, multi-step flows, etc.).
2. Full N:M profile model redesign.
3. Full per-window theme-state rearchitecture across the entire app.
4. Create-space visual remediation except where this flow depends on a correct switch handoff.

---

## 5. Implementation Plan

### Phase 1 — Establish one activation contract in `MahoSpaceProfileBridge`

**Files:**

- `maho-chromium/browser/maho_space_profile_bridge.h`
- `maho-chromium/browser/maho_space_profile_bridge.cc`

**Changes:**

1. Make `SwitchToSpace(Browser*, const std::string&)` the canonical user-switch entry point.
2. Ensure that this path explicitly performs the core activation step instead of relying on callers to do it elsewhere.
3. After core activation, update:
   - `browser_active_space_[browser]` when `browser` exists,
   - `active_space_id_` as the process-global fallback / last-activated value.
4. Keep `MahoSpaceThemeState::UpdateFromCore()` and observer notification inside this chokepoint.
5. Preserve existing profile-switch behavior, but only as part of this single activation path.

**Required invariant after Phase 1:**

Any UI-initiated space switch that succeeds leaves core, browser bridge state, and bridge-global fallback in a consistent post-switch state.

**QA scenario:**

- Tool: `grep`
- Step: verify all bridge activation entry points still resolve through `MahoSpaceProfileBridge::SwitchToSpace` and no new parallel writer was introduced in bridge-adjacent code.
- Expected result: the bridge remains the only intended activation chokepoint in `maho_space_profile_bridge.{h,cc}`.

### Phase 2 — Remove direct core activation from command actions

**File:** `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`

**Changes:**

1. Replace direct `maho_core_activate_space(...)` calls in:
   - `open_space:*`
   - `next_space`
   - `prev_space`
   - `select_space_N`
   with bridge-mediated switching.
2. When the target space id is derived from core JSON, resolve the target id first, then call the canonical bridge switch with the current `browser`.

**Required invariant after Phase 2:**

Keyboard/command switching produces the same bridge notifications and UI updates as sidebar switching.

**QA scenario:**

- Tool: `grep`
- Step: search `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` for `maho_core_activate_space\(`.
- Expected result: zero command-path direct calls remain in that file; command actions route through bridge-mediated switching only.

### Phase 3 — Make footer switching browser-scoped

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc`

**Changes:**

1. Change footer dot activation and keyboard navigation handlers to call `SwitchToSpace(browser_, space_id)` instead of the no-browser overload.
2. Keep local selection UX behavior, but make actual activation browser-scoped.

**Required invariant after Phase 3:**

Footer interactions update the same browser-scoped state model expected by the rest of the sidebar.

**QA scenario:**

- Tool: `grep`
- Step: search `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc` for `SwitchToSpace\(`.
- Expected result: footer handlers pass `browser_` to `SwitchToSpace(browser_, ...)`; no production footer activation path uses the no-browser overload.

### Phase 4 — Keep other writers on the same canonical path

**Files:**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_view.cc`
- `maho-chromium/browser/ui/views/spaces_overlay/maho_spaces_overlay_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`

**Changes:**

1. Verify all of these writers already use the browser-scoped bridge path.
2. Adjust only if needed so they rely on the same contract added in Phase 1, without duplicate theme refresh / duplicate notify behavior around the call.

**Required invariant after Phase 4:**

There is one switch contract and all production writers use it.

**QA scenario:**

- Tool: `grep`
- Step: inspect production switch writers in:
  - `maho_sidebar_spaces_view.cc`
  - `maho_spaces_overlay_view.cc`
  - `maho_sidebar_create_space_view.cc`
- Expected result: each production writer calls the canonical bridge switch path and does not reintroduce direct core activation or duplicate post-switch notify logic.

### Phase 5 — Narrow reader ambiguity where it causes visible drift

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`

**Changes:**

1. Keep browser-scoped bridge state as the active marker source for browser-bound footer UI.
2. Document the contract in code comments where footer model composition mixes core data with browser bridge state.
3. Remove any unnecessary reliance on the global bridge value for browser-bound surfaces when `browser` is available.

**Required invariant after Phase 5:**

Browser UI reads browser-scoped active-space state intentionally, not accidentally.

**QA scenario:**

- Tool: `read` + `grep`
- Step: inspect `BuildFooterModel` and adjacent helper functions in `maho_sidebar_state_adapter.cc`.
- Expected result: comments and code make the browser-scoped bridge read explicit for browser-bound active markers, with no accidental fallback to global state when `browser` is available.

### Phase 6 — Add regression coverage for the real broken paths

**Files (expected):**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_edge_cases_interactive_test.cc`
- command/sidebar tests nearest to `maho_command_action_handler.cc`

**Required test additions:**

1. A command-path regression test: command/shortcut switch updates sidebar/footer active state.
2. A footer regression test: switching from footer updates browser-scoped active space, not only global fallback.
3. A multi-window regression test: switching in window A does not silently overwrite window B’s browser-scoped active space.
4. A create-space handoff regression test if the created space activation relies on the same bridge path.

**QA scenario:**

- Tool: targeted browser/interactive tests
- Step: run the narrowest relevant sidebar/command tests covering the four cases above.
- Expected result: all new regression tests fail before the fix, pass after the fix, and demonstrate that command, footer, and multi-window switch paths now share the same contract.

---

## 6. Verification Plan

### Automated

1. Run relevant sidebar/browser tests covering:
   - footer dot switching,
   - command-driven switching,
   - multi-window active-space isolation.
2. Run `lsp_diagnostics` on all touched Chromium C++ files.
3. If test targets are narrow enough, run the closest targeted browser/interactive tests rather than a wide rebuild-only pass.
4. Run file-level grep verification after edits:
   - command handler has no direct `maho_core_activate_space(...)` writes,
   - footer uses browser-scoped `SwitchToSpace(browser_, ...)`,
   - production switch writers all converge on the same bridge contract.

### Manual

Verify these exact flows in a local build:

1. Click footer dot → footer highlight, visible tab list, and theme all move together.
2. Use `next_space` / `prev_space` / direct command action → same result as footer click.
3. Open two browser windows on different spaces → switching one does not immediately clobber the other window’s active marker.
4. Create a new space → it becomes active through the same canonical path.

---

## 7. Risks and Mitigations

### Risk 1 — Hidden callers rely on direct core activation

**Mitigation:** grep all `maho_core_activate_space(...)` call sites and migrate browser/UI callers in the same pass.

### Risk 2 — Duplicate side effects after bridge unification

`SwitchToSpace(...)` already updates theme state and notifies observers. Some callers may currently do extra refresh work after calling it.

**Mitigation:** audit create-space and overlay callers for duplicate refresh/update calls once the bridge contract is expanded.

### Risk 3 — Core-global active space still exists

This plan fixes writer consistency, not the full architectural gap of process-global core active space versus true per-window engine state.

**Mitigation:** keep the plan scoped. Treat this as a switcher correctness fix, and only open a broader architecture follow-up if tests expose remaining cross-window issues that cannot be solved at the bridge layer.

---

## 8. Acceptance Criteria

The fix is complete when all of the following are true:

1. No production UI path calls `maho_core_activate_space(...)` directly.
2. Footer, overlay, spaces panel, and command actions all switch through the same bridge contract.
3. Active-space highlight and effective active-space content no longer diverge for the known broken flows.
4. Multi-window tests still pass, with browser-scoped active-space isolation preserved.
5. Existing create-space and theme-picker architecture constraints remain unchanged.

---

## 9. Follow-up Work (Not Part of This Fix)

If this fix lands cleanly but deeper issues remain, the next architecture investigation should cover:

1. whether `MahoSpaceThemeState` must become browser-scoped,
2. whether browser-bound board/overlay models should stop consulting ambient core active-space state entirely,
3. whether the larger `space-profile-gap-fill.md` roadmap should be updated to codify this bridge contract explicitly.
