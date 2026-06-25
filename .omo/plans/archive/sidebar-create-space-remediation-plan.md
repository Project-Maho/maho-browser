# Sidebar Create Space Remediation Plan

**Date:** 2026-05-05  
**Branch:** main  
**Scope:** `chromium/src/maho/browser/ui/views/sidebar/*` for remediation work, plus only the specific validation blockers named below when documenting build status.

---

## Objective

Remediate the sidebar create-space flow so the entry path opens reliably, create failures are visible instead of silent, and validation is organized around what can run in parallel versus what must wait on earlier fixes. The plan stays tightly focused on the sidebar create-space path and does not expand into unrelated feature work or speculative backend refactors.

---

## Known Facts

1. The footer ancestor handoff regressed after the body-host refactor. The old parent-chain assumption is no longer valid, so `New Space` can miss the real sidebar host unless the ancestor walk is corrected.
2. `CreateSpaceOnPool()` currently passes `nullptr` as `profile_id` to `maho_core_create_space`, which guarantees an FFI null return. This is a hard stop in the current flow and must remain explicitly called out.
3. `OnSpaceCreated()` currently treats an empty id failure as a silent close and reset path, which hides the failure from the user.
4. Build validation is currently blocked by the TS2367 error in `maho_settings` and by a GN regeneration dependency cycle. Those blockers are external to the sidebar remediation itself and must be tracked separately from the sidebar work.

---

## Serial Gates

These are the hard ordering constraints. Everything else should be treated as parallelizable only after the relevant gate is satisfied.

### Gate 1, Restore the sidebar entry handoff

**Purpose:** Make sure `+` to `New Space` reaches the actual sidebar host after the body-host refactor.

**Why it is gated:** Until this is correct, later create-space validation is not trustworthy because the panel may never open from the footer path.

**Target file:** `chromium/src/maho/browser/ui/views/sidebar/maho_sidebar_footer_view.cc`

**Acceptance criteria:** Clicking `+`, then `New Space`, reliably calls `MahoSidebarView::ShowCreateSpace()` on the real sidebar host and opens the create-space body inside the sidebar.

### Gate 2, Preserve a visible failure path before tightening inputs

**Purpose:** Stop the panel from silently closing on failure, including the empty-id case.

**Why it is gated:** Input-state improvements are only meaningful once failure is visible and retryable.

**Target files:**
`chromium/src/maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h`  
`chromium/src/maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`

**Acceptance criteria:** A failed create keeps the panel open, preserves the typed input, restores the button state, and shows a visible error message instead of closing silently.

### Gate 3, Validate the known FFI show-stopper in context

**Purpose:** Keep the `profile_id == nullptr` failure path explicit in the plan and confirm whether the sidebar flow can observe it cleanly.

**Why it is gated:** This cannot be treated as a normal success path. It is the current guaranteed failure condition in `CreateSpaceOnPool()`.

**Target files:**
`chromium/src/maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`  
`maho/crates/maho-ffi/src/lib.rs` for reference only, not as a scope expansion target

**Acceptance criteria:** The plan and implementation notes clearly preserve the fact that `CreateSpaceOnPool()` passes `nullptr` for `profile_id` and that this produces a null FFI return. The sidebar UI must surface that failure rather than hiding it.

---

## Parallel Tracks

These tracks can proceed in parallel once their dependency gates are satisfied. Each track stays narrow and file scoped.

### Track A, Footer host routing correction

**Target file:** `chromium/src/maho/browser/ui/views/sidebar/maho_sidebar_footer_view.cc`

**Scope:** Fix the ancestor lookup so the footer action hands off to the real sidebar host after the body-host refactor.

**Dependencies:** Gate 1.

**Acceptance criteria:** The footer action path no longer depends on the broken pre-refactor parent chain. The correct sidebar host receives the create-space request every time.

### Track B, Create panel failure visibility

**Target files:**
`chromium/src/maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h`  
`chromium/src/maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`

**Scope:** Add or adjust the minimal sidebar-side failure state so `OnSpaceCreated()` does not silently close and reset on empty id, and so the user can retry with the current input intact.

**Dependencies:** Gate 2.

**Acceptance criteria:** Empty-id or otherwise failed create requests leave the panel open, show an explicit error state, and do not clear the form as if the operation succeeded.

### Track C, Input-state semantics cleanup

**Target file:** `chromium/src/maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`

**Scope:** Tighten the create button enablement logic so whitespace-only input does not count as valid, while preserving the existing keyboard behavior for valid input and cancel.

**Dependencies:** Gate 2, because input-state logic should sit on top of the corrected failure UX.

**Acceptance criteria:** Empty or whitespace-only input never enables Create, valid input does, Enter submits valid input, and Escape or cancel still exits cleanly when idle.

### Track D, Minimal diagnostic visibility for create failures

**Target file:** `chromium/src/maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`

**Scope:** Add only enough local logging to separate UI routing failure, null FFI return, and empty-id parse failure.

**Dependencies:** Gate 3.

**Acceptance criteria:** When create still fails, the logs make it possible to tell whether the issue is the sidebar entry path or the FFI and contract path, without broad or noisy instrumentation.

---

## Critical Path

The critical path is short and intentionally serial:

1. Fix the footer ancestor handoff regression so `New Space` reaches the real sidebar host.
2. Make sidebar create failures visible, especially the silent empty-id close path.
3. Keep the `profile_id == nullptr` FFI failure path explicit so the current show-stopper is not lost in the remediation.
4. Tighten input-state semantics once the failure path is observable.
5. Add minimal logging only after the visible UX path is stable.

---

## Validation Strategy

Validation should follow the same gate order. Do not treat downstream checks as meaningful until the earlier gates pass.

### UI and flow validation

1. `+` then `New Space` opens the create-space body inside the sidebar.
2. The top bar stays persistent while the create-space body is shown.
3. `Cancel` returns to the normal sidebar body.
4. Empty input keeps Create disabled.
5. Whitespace-only input keeps Create disabled.
6. Valid input enables Create.
7. Successful create closes the panel and reflects the new space.
8. Forced failure keeps the panel open and shows the error state.

### Build and verification validation

Primary build command:

```bash
PATH="/Users/indo/code/project/maho-workspace/chromium/src/third_party/depot_tools:$PATH" autoninja -C out/Default chrome
```

Validation remains blocked right now by the external issues named below. If full build validation cannot complete, record the exact blocker and do not widen scope beyond the sidebar create-space work.

---

## Known External Blockers

These blockers are outside the sidebar create-space remediation scope, but they currently prevent full validation.

1. `maho_settings` TypeScript build error.
   * Location: `gen/maho/browser/resources/maho_settings/preprocessed/app.ts:148`
   * Error: `TS2367` comparing `pane.kind === 'placeholder'`

2. GN regeneration dependency cycle during narrow rebuild attempts.
   * Chain includes `//chrome/browser:browser -> ... -> //maho/browser:maho_browser_ai_tools -> //chrome/browser:browser`

These blockers do not change the sidebar remediation plan. They only affect how far validation can go in the current environment.

---

## Definition of Done

The work is done when all of the following are true:

1. The footer ancestor handoff regression is fixed and `New Space` reaches the correct sidebar host.
2. The create-space panel no longer silently closes on failure.
3. The empty-id failure path is visible, retryable, and preserves the user’s input.
4. The plan still explicitly records that `CreateSpaceOnPool()` passes `nullptr` as `profile_id`, which guarantees a null FFI return.
5. Input-state behavior is correct for empty, whitespace-only, and valid names.
6. Validation has been run as far as the current external blockers allow, and any remaining blockers are documented exactly.
