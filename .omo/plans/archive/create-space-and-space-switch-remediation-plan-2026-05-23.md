# Create Space and Space Switch Remediation Plan

**Created:** 2026-05-23
**Status:** Draft, pending Momus review
**Primary scope:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`, `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`
**Secondary scope:** `maho-chromium/browser/maho_space_profile_bridge.{h,cc}`, `maho-chromium/browser/maho_browser_main_extra_parts.cc`, related sidebar interactive tests

---

## 1. Objective

Land the smallest correctness fix that closes the two user-visible regressions currently blocking space workflows in the Chromium fork:

1. The native sidebar can drift out of sync with the browser-scoped active space because `MahoSidebarView::RefreshFavoritesAndViewState()` reads the process-global `MahoSpaceProfileBridge::GetActiveSpaceId()` instead of the browser-scoped `GetActiveSpaceId(browser_)`. The same file has additional latent global-getter callsites that should be brought in line.
2. The native create-space flow can complete in core but appear to no-op in the UI when the post-create switch-or-refresh hand-off silently fails.

This plan focuses on the user-visible root causes inside `maho-chromium/browser/ui/views/sidebar/`. It complements, but does not replace, the broader switch-writer unification covered by `space-switcher-fix-plan-2026-05-23.md` and the visual cleanup covered by `create-space-current-regressions-remediation-plan-2026-05-22.md`.

---

## 2. Non-Negotiable Constraints

1. Do not rework the existing footer, command, overlay, or spaces-view switch writers. Those already route through `bridge->SwitchToSpace(browser, ...)` and stay as-is in this pass.
2. Do not change the create-space architecture (native inline form plus WebUI theme picker at `chrome://maho-space-create`). The theme picker ADR at `.sisyphus/plans/theme-picker-architecture-decision.md` remains in force.
3. No GN edits as part of this remediation pass.
4. No Rust core rewrite. `maho_core_*` FFI surface stays the same.
5. No speculative architecture changes (no per-window theme state redesign, no N:M profile redesign, no new bridge method beyond what is strictly required).
6. No fake verification claims. Every verification step listed in Section 7 must be actually run before the remediation is marked done.

---

## 3. Code-Grounded Current Diagnosis

### 3.1 Sidebar reads the wrong active-space scope

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`

`RefreshFavoritesAndViewState()` currently reads the process-global active-space id:

```cpp
void MahoSidebarView::RefreshFavoritesAndViewState() {
  std::string active_space_id_json;
  if (auto* bridge = MahoSpaceProfileBridge::GetInstance(); bridge) {
    const std::string& active_space_id = bridge->GetActiveSpaceId();
    ...
  }
  ...
}
```

The view holds `browser_`, so the canonical read is `bridge->GetActiveSpaceId(browser_)`. The current global read means a refresh in window A can pick up window B's last activation, or pick up a stale process-global value, depending on which window most recently wrote the global fallback.

Additional latent global-getter callsites in the same file:

- `UpdateSpacesPaneActions()` at the `has_active_space` check uses `bridge->GetActiveSpaceId()` to enable a spaces-pane action button.
- `OpenSpaceConfigSurface()` reads `bridge->GetActiveSpaceId()` to gate the config surface.

These two are gating reads, not refresh reads, but they are still in the same file and exhibit the same scope mismatch. They should be normalized in the same pass to prevent re-introduction.

### 3.2 Create-space post-create hand-off is the silent-failure path

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`

The end-to-end flow is:

```
OnCreatePressed()
  -> base::ThreadPool::PostTaskAndReplyWithResult(CreateSpaceOnPool, ...)
  -> OnSpaceCreated(success, space_id, profile_id)
  -> bridge->RegisterSpace(new_space_id, profile_path)
  -> bridge->SwitchToSpace(browser_, new_space_id)
```

Confirmed call sites:

- `OnCreatePressed()` posts `CreateSpaceOnPool` and binds `OnSpaceCreated` as the reply.
- `OnSpaceCreated()` calls `bridge->RegisterSpace(...)` followed by `bridge->SwitchToSpace(browser_, new_space_id)`.

There are three structural failure modes for "create succeeded in core but the UI does not move":

1. **Core-null:** `maho_core_handle()` returns null inside `CreateSpaceOnPool`, the call short-circuits, and `OnSpaceCreated(false, ...)` does nothing user-visible.
2. **FFI-null:** `maho_core_create_space(...)` returns a null/empty id, `CreateResult.success` ends up false, and the user sees no state change and no surfaced error.
3. **Post-create switch-or-refresh:** core created the space successfully, `RegisterSpace` ran, `SwitchToSpace(browser_, new_space_id)` ran, but the sidebar never refreshes its favorites/view state for that space because the refresh path is reading the wrong scope (see 3.1) or because the bridge state did not propagate to the sidebar in time.

Mode 3 is the dominant user-reported symptom and is the only one that is fully addressable from the C++ layer in a narrow pass. Modes 1 and 2 require either core-side investigation or surfaced error UX, which is explicitly deferred out of this pass.

### 3.3 Existing switch writers are not the bug

The footer, command action handler, spaces view, overlay view, and create-space success path already invoke `bridge->SwitchToSpace(browser, ...)` (see `space-switcher-fix-plan-2026-05-23.md` for the current writer audit). The failure surfaces in the *reader* in `maho_sidebar_view.cc`, not in the writers. This plan therefore does not touch writer code paths.

### 3.4 Default-profile sentinel is inconsistent across surfaces

`create-space-current-regressions-remediation-plan-2026-05-22.md` already documents that the create-space form uses `kDefaultProfileId = "Default"` as a fallback while the success path treats only lowercase `"default"` as the default sentinel. Startup hydration in `maho_browser_main_extra_parts.cc` and the bridge's profile-path resolution use yet another shape on disk (`Default`).

This sentinel/path mismatch is real, but it is not the root cause of the most common reported create-space silent failure. It is normalized in a follow-up phase (Section 5, Phase 3) so the user-visible root cause in the sidebar reader is fixed first.

### 3.5 New-window browser-scoped seeding is ambiguous

When a new browser window opens, the bridge's `browser_active_space_[browser]` map for that window may not be seeded yet, so the very first `GetActiveSpaceId(browser)` read from the new window can return empty. The fix is straightforward, but it is intentionally deferred to a follow-up phase so the user-visible root fix in `maho_sidebar_view.cc` lands first.

---

## 4. Scope

### In scope

1. Convert the sidebar refresh read in `MahoSidebarView::RefreshFavoritesAndViewState()` to `bridge->GetActiveSpaceId(browser_)`.
2. Normalize the other latent global-getter reads in the same file to the browser-scoped overload where a `browser_` is available.
3. Make the post-create UI hand-off in `MahoSidebarCreateSpaceView::OnSpaceCreated()` deterministic: after `SwitchToSpace(browser_, new_space_id)`, schedule a sidebar refresh on the same browser so the favorites/view state model rebuilds against the new active space.
4. Add interactive coverage for the multi-window read path and for the create-space hand-off.
5. Leave room for a follow-up phase that normalizes the default-profile sentinel and an additional follow-up that resolves the new-window browser-scoped seeding.

### Out of scope

1. Switch-writer unification across command/footer/overlay/spaces-view, covered by `space-switcher-fix-plan-2026-05-23.md`.
2. Native create-space visual remediation (Profile row, theme block row, glyph cleanup), covered by `create-space-current-regressions-remediation-plan-2026-05-22.md`.
3. Theme payload validator changes, theme picker resurrection, or any WebUI theme-picker rework.
4. Any change to `maho_core_*` FFI surface.
5. Any GN edits.
6. Per-window `MahoSpaceThemeState` redesign.
7. Any cross-process or cross-profile sync rework.

---

## 5. Implementation Plan

The plan is split into a tight first phase that fixes the user-visible root causes, followed by smaller follow-up phases that close the consistency gaps without expanding scope.

### Phase 1 - Make `MahoSidebarView` read browser-scoped active-space state

**Files:**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`

**Required changes:**

1. In `RefreshFavoritesAndViewState()`, replace `bridge->GetActiveSpaceId()` with `bridge->GetActiveSpaceId(browser_)`. Keep the JSON-encoding step and the `ThreadPool::PostTaskAndReplyWithResult` shape unchanged.
2. In `UpdateSpacesPaneActions()`, replace the `bridge->GetActiveSpaceId().empty()` gating read with `bridge->GetActiveSpaceId(browser_).empty()`. The view always has a `browser_` here.
3. In `OpenSpaceConfigSurface()`, replace the gating read with the browser-scoped overload using `browser_`.
4. Confirm `MahoSidebarView` already holds `browser_` for all three callsites (it does, since it is constructed with a `Browser*`).

**Required invariant after Phase 1:**

Every active-space read inside `maho_sidebar_view.cc` is browser-scoped when a `Browser*` is available. There is no remaining production callsite in this file that reads the process-global `GetActiveSpaceId()` overload.

**QA scenario:**

- Tool: `grep`.
- Step: `grep -n "GetActiveSpaceId(" maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`.
- Expected result: every match passes `browser_` (or an explicit `Browser*`); no production call uses the no-argument overload.

### Phase 2 - Make the create-space post-create hand-off deterministic

**Files:**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`

**Required changes:**

1. In `OnSpaceCreated()`, after the existing `bridge->SwitchToSpace(browser_, new_space_id)` call, request a sidebar refresh on the same `browser_` so the favorites/view-state model is rebuilt against the newly active space. Use the existing sidebar refresh entry point that the rest of the codebase already uses post-switch (the same path that is invoked when `SwitchToSpace` triggers observer notifications). Do not introduce a new bridge method.
2. If `success` is true but `new_space_id` is empty, treat the result as a failure for UI purposes and stop. Do not call `RegisterSpace` or `SwitchToSpace` with an empty id.
3. Keep `RegisterSpace`, `SwitchToSpace(browser_, new_space_id)`, the icon propagation step, and the form-close behavior intact otherwise.
4. Do not add new direct `maho_core_*` calls. Do not bypass the bridge.

**Required invariant after Phase 2:**

After `OnSpaceCreated(true, non_empty_id, profile_id)` returns, the active space for `browser_` is the new space, the sidebar's favorites/view-state model has been scheduled for rebuild against that new space, and the form is dismissed. After `OnSpaceCreated(false, ...)` returns, no bridge state has been mutated.

**QA scenario:**

- Tool: `grep` plus targeted browser/interactive test.
- Step: `grep -n "RegisterSpace\|SwitchToSpace" maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc` and confirm the empty-id guard runs before either call. Then run the create-space hand-off interactive test from Phase 4.
- Expected result: no `RegisterSpace`/`SwitchToSpace` callsite is reachable with an empty id, and the interactive test sees the new space become active in the sidebar after a successful create.

### Phase 3 - Normalize the default-profile sentinel between create and startup

**Files:**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`
- `maho-chromium/browser/maho_browser_main_extra_parts.cc` (read-only verification, edit only if startup hydration is the divergent side)

**Required changes:**

1. Choose one canonical default-profile sentinel string. The recommended canonical value is lowercase `"default"`, matching the success path.
2. Update `kDefaultProfileId` (or equivalent) in the create-space view so the fallback uses the canonical sentinel.
3. Make the success-path mapping in `OnSpaceCreated` tolerant to legacy capitalized `"Default"` so prior persisted state still resolves.
4. Verify the on-disk profile-path mapping continues to resolve to `Default` (Chromium's existing on-disk profile directory). Do not rename the on-disk directory.
5. Confirm startup hydration in `maho_browser_main_extra_parts.cc` produces the same canonical sentinel for the seeded active space. Adjust only if it currently emits a different shape.

**Required invariant after Phase 3:**

The default-profile id used by create-space, by the success-path mapping, and by startup hydration is the same canonical string. The on-disk profile path remains `Default`.

**QA scenario:**

- Tool: `grep`.
- Step: `grep -n "Default\b\|\"default\"" maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc maho-chromium/browser/maho_browser_main_extra_parts.cc`.
- Expected result: every default-profile id literal is the canonical sentinel; the only `"Default"` capitalized occurrences are on-disk profile path strings or legacy-tolerant compares.

### Phase 4 - Resolve new-window browser-scoped active-space seeding

**Files:**

- `maho-chromium/browser/maho_space_profile_bridge.{h,cc}`
- `maho-chromium/browser/maho_browser_main_extra_parts.cc`

**Required changes:**

1. When a new browser is registered with the bridge (or first observed), seed `browser_active_space_[browser]` from the bridge-global fallback (`active_space_id_`) if it is non-empty and the per-browser entry is unset.
2. Keep the bridge-global fallback as the post-startup seed source. Do not introduce a new persisted per-window store as part of this pass.
3. Document the contract in the bridge header: the per-browser map is the canonical UI active-space state; the global value exists only as a seed and as a no-browser-context fallback.

**Required invariant after Phase 4:**

The first `GetActiveSpaceId(browser)` read from a freshly-opened browser window returns the seed value, not empty. Multi-window switching still preserves per-window isolation, since seeding only fills empty entries.

**QA scenario:**

- Tool: targeted browser/interactive test (see Phase 5).
- Step: open a second browser window and verify the sidebar in that window does not start empty before the user switches spaces.
- Expected result: the second window's sidebar reflects the bridge's active-space seed and the favorites/view-state model is non-empty.

### Phase 5 - Add regression coverage

**Files (expected, follow nearest existing test patterns):**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_edge_cases_interactive_test.cc`
- whichever existing test suite is closest to `maho_sidebar_create_space_view.cc`

**Required test additions:**

1. Multi-window read regression: open windows A and B, switch space in A, force a sidebar refresh in B, and assert B still reports its own active space, not A's.
2. Create-space hand-off regression: drive `OnCreatePressed` -> create succeeds -> assert the newly created space is the browser-scoped active space and the sidebar's favorites/view-state model is rebuilt for it.
3. Empty-id guard regression: simulate `OnSpaceCreated(true, "", profile_id)` and assert no `RegisterSpace`/`SwitchToSpace` is invoked.
4. Default-profile sentinel regression: drive create-space with the fallback profile and assert the resulting bridge state matches the canonical sentinel and the on-disk `Default` path.

**QA scenario:**

- Tool: targeted interactive tests.
- Step: run the four new tests against pre-fix and post-fix builds.
- Expected result: all four fail before the Phase 1-4 changes and pass after.

---

## 6. File-by-File Planned Changes

### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`

- `RefreshFavoritesAndViewState()`: switch to `bridge->GetActiveSpaceId(browser_)`.
- `UpdateSpacesPaneActions()`: switch the gating read to the browser-scoped overload.
- `OpenSpaceConfigSurface()`: switch the gating read to the browser-scoped overload.

### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`

- `OnSpaceCreated()`: add the empty-id guard, schedule a sidebar refresh against `browser_` after `SwitchToSpace(browser_, new_space_id)`, normalize the default-profile sentinel.
- Other functions (`OnCreatePressed`, `CreateSpaceOnPool`, icon propagation): unchanged except where Phase 3 explicitly requires the sentinel update.

### `maho-chromium/browser/maho_space_profile_bridge.{h,cc}` (Phase 4 only)

- Seed `browser_active_space_[browser]` from `active_space_id_` on first registration of a `Browser*`.
- Document the per-browser-vs-global contract in the header.

### `maho-chromium/browser/maho_browser_main_extra_parts.cc` (Phase 3-4 only)

- Confirm the canonical sentinel emitted at startup matches the canonical sentinel used by create-space. Adjust only if divergent.

### Files explicitly not targeted in this pass

- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_view.cc`
- `maho-chromium/browser/ui/views/spaces_overlay/maho_spaces_overlay_view.cc`
- Any `BUILD.gn` file
- Any Rust crate under `maho/crates/`

---

## 7. Verification Plan

### 7.1 Static verification

1. Run `lsp_diagnostics` on every changed file. Zero errors, zero new warnings on touched ranges.
2. Re-run the Phase 1 and Phase 3 grep checks and confirm zero remaining incorrect callsites.
3. Confirm no header changes leaked into files outside the listed scope.

### 7.2 Targeted Chromium build

After each phase that modifies C++:

```bash
autoninja -C out/Default chrome
```

Run from `chromium/src`. Exit code 0 required. If a narrower object compile is faster, build the touched sidebar object first and then the full target.

### 7.3 Interactive and regression coverage

Run the four new interactive tests added in Phase 5. All four must:

1. fail against the pre-fix build (or against a build with only some phases applied), and
2. pass against the build with all phases applied.

If any test only passes when other unrelated test suites also run, treat that as a flake signal and investigate before marking Phase 5 done.

### 7.4 Manual QA in a local build

1. Launch with the canonical wrapper:

    ```bash
    python3 maho-chromium/build/scripts/build_maho.py
    ```

2. Multi-window read: open two browser windows, switch space in window A, observe that window B's sidebar still highlights its own active space and that its favorites/view-state model has not jumped to A's space.
3. Create-space hand-off: open the native sidebar create form, create a new space using the fallback profile, observe that the sidebar moves to the new space and that the favorites/view-state model reflects it.
4. Empty-id guard: artificially force a successful-but-empty-id result in a debug build (or assert through a unit test) and confirm no bridge state mutates.
5. Sentinel normalization: confirm the new space is registered against `Default` on disk and that the bridge active-space id matches the canonical sentinel.
6. New-window seeding: open a new browser window and confirm the sidebar's active marker is non-empty before the user interacts.

### 7.5 Canonical full-build sanity (final pass only)

```bash
python3 maho-chromium/build/scripts/build_maho.py
```

If this build is blocked by an unrelated file outside the scope listed in Section 6, record that blocker separately and do not fold it into this remediation.

---

## 8. Out of Scope

The following items are intentionally deferred. They are real and tracked, but they are not part of this pass:

1. Unifying all space-switch writers behind a single bridge contract. Tracked by `space-switcher-fix-plan-2026-05-23.md`.
2. Native create-space visual remediation (Profile row, theme block row, glyph cleanup). Tracked by `create-space-current-regressions-remediation-plan-2026-05-22.md`.
3. Theme payload validator changes and any WebUI theme picker rework.
4. Per-window `MahoSpaceThemeState` redesign.
5. Any change to the `maho_core_*` FFI surface or to Rust crates under `maho/crates/`.
6. Any GN edits or `BUILD.gn` changes.
7. Surfacing user-visible error UX for core-null or FFI-null create failures (failure modes 1 and 2 in Section 3.2). Recommended as a separate UX plan once the silent-failure root cause is fixed.
8. Cross-process or cross-profile sync of active-space state.

---

## 9. Definition of Done

This remediation pass is done when all of the following are true:

1. Every active-space read in `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc` is browser-scoped when a `Browser*` is available.
2. `MahoSidebarCreateSpaceView::OnSpaceCreated` guards against empty ids, switches through the bridge with `browser_`, and triggers a sidebar refresh against the same browser.
3. The default-profile sentinel is canonical across create-space and startup hydration, while the on-disk profile path remains `Default`.
4. New browser windows seed their per-browser active-space entry from the bridge global on first registration.
5. The four new interactive regression tests pass against the post-fix build.
6. `python3 maho-chromium/build/scripts/build_maho.py` exits 0 with all changes applied.
7. The constraints in Section 2 still hold: no writer rework, no GN edits, no Rust core rewrite, no theme picker resurrection.

---

## 10. Recommended Execution Order

1. Phase 1, sidebar reader fix.
2. Phase 2, create-space hand-off determinism.
3. Phase 5 tests covering Phases 1 and 2 (run, watch them go green).
4. Phase 3, default-profile sentinel normalization.
5. Phase 4, new-window seeding.
6. Phase 5 tests covering Phases 3 and 4 (run, watch them go green).
7. Targeted Chromium build after each phase, full canonical build at the end.
8. Manual QA pass per Section 7.4.

This ordering keeps the user-visible root cause fixes ahead of the consistency cleanups, and keeps the verification artifacts grounded in real test runs rather than in claims.
