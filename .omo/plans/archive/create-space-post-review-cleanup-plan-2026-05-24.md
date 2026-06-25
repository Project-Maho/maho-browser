# Create Space Post-Review Cleanup Plan (Rewritten)

**Created:** 2026-05-24  
**Status:** Draft, rewritten after state review  
**Primary scope:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc/.h`  
**Secondary scope:** `maho-chromium/browser/maho_space_profile_bridge.cc/.h`, `maho-chromium/browser/maho_browser_main_extra_parts.cc`, `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc/.h`, `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`

---

## 1. Objective

Finish the post-review cleanup for the create-space / space-switch remediation by addressing the blockers that still remain after the latest implementation review.

The latest review confirmed that some of the cleanup work has already landed correctly:

1. `MahoSpaceProfileBridge::ProfileBasenameForId` exists and centralizes profile-path construction.
2. Both production call sites now route through the helper:
   - `maho_sidebar_create_space_view.cc`
   - `maho_browser_main_extra_parts.cc`
3. `maho_sidebar_view.cc` still reads browser-scoped active-space state through `GetActiveSpaceId(browser_)`.
4. Additional tests for helper validation and no-mutation behavior were added.

However, the same review also concluded that the cleanup still cannot be signed off because:

1. The working tree is still contaminated by many out-of-scope changes outside the cleanup allowlist.
2. `maho_sidebar_create_space_view.cc/.h` still carry the large UI-heavy version instead of being trimmed back to remediation-only changes.
3. One newly added parity test is effectively tautological, and two older weak test patterns remain in the same test file.

This rewritten plan therefore focuses only on the remaining cleanup work. It does **not** restate already-landed work as if it were still pending implementation.

---

## 2. Non-Negotiable Constraints

1. **Working tree isolation remains the first gate.** No cleanup sign-off is possible until the diff is reduced to the cleanup allowlist.
2. **Allowlist-only cleanup.** Only the files in Section 4 may remain in the cleanup diff.
3. **No GN edits.** Any `BUILD.gn` changes remain out of scope.
4. **No Rust core rewrites.** No edits under `maho/crates/`.
5. **No unrelated feature work.** Command palette, AI panel, location bar, WebUI, and unrelated sidebar follow-ons remain out of scope.
6. **No architecture redesign.** Preserve the current bridge contract and current remediation direction.
7. **Preserve already-landed correctness fixes.** Do not regress:
   - browser-scoped sidebar reads
   - empty-id guard
   - browser-scoped `SwitchToSpace`
   - helper-based profile path validation
8. **This plan excludes build/run content by request.** Build commands, binary launch, and manual QA execution are intentionally omitted from this rewritten version.

---

## 3. Reviewed Current State

### 3.1 Already landed and should be preserved

The following pieces are already present in the code and should now be treated as **preserve + verify**, not fresh implementation work:

- `MahoSpaceProfileBridge::ProfileBasenameForId(std::string_view)` exists.
- `kMahoProfileIdMaxLen = 64` exists.
- The helper trims whitespace, handles `default` / `Default`, rejects separators, rejects `.` / `..`, rejects `..` substrings, rejects non-ASCII / non-printable bytes, and returns `std::nullopt` on invalid ids.
- `maho_sidebar_create_space_view.cc::OnSpaceCreated(...)` now uses the helper and aborts cleanly on invalid profile ids without mutating bridge state.
- `maho_browser_main_extra_parts.cc::PopulateBridgeFromCore(...)` now uses the helper and skips invalid profile ids.
- `maho_sidebar_view.cc` still uses `GetActiveSpaceId(browser_)` in the relevant reader callsites.

### 3.2 Remaining blocker: working tree contamination

The latest review found that the cleanup branch still contains many out-of-scope modifications outside the intended 8-file allowlist. That means the cleanup is not yet isolated as a reviewable unit.

This is not a cosmetic issue. It prevents the cleanup from being judged as a coherent implementation of the saved plan.

### 3.3 Remaining blocker: create-space view not trimmed

The latest review also confirmed that `maho_sidebar_create_space_view.cc/.h` still expose the large UI-heavy form implementation, including theme/profile/icon-picker infrastructure that was explicitly supposed to be trimmed out of this cleanup pass.

The current cleanup target is narrower:

- keep the remediation-only logic,
- keep the helper usage,
- revert the unrelated UI rework back to baseline.

### 3.4 Remaining blocker: test quality gaps

Although helper-related tests were added, the latest review found three unresolved test-quality issues in `maho_sidebar_spaces_interactive_test.cc`:

1. `StartupHydrationParity` is currently a self-comparison and does not meaningfully prove parity between two production paths.
2. `FooterActionOpensSpacesSurface` still relies on a weak `A || B` assertion.
3. `SpaceDotSwitchesTabList` still has a soft-skip early return pattern.

These must be fixed in the cleanup pass.

---

## 4. Cleanup Scope (File Allowlist)

This cleanup pass must end with diffs only in the following files:

1. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
2. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.h`
3. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`
4. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.h`
5. `maho-chromium/browser/maho_space_profile_bridge.cc`
6. `maho-chromium/browser/maho_space_profile_bridge.h`
7. `maho-chromium/browser/maho_browser_main_extra_parts.cc`
8. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`

Any diff outside this list is out of scope for this cleanup rewrite.

---

## 5. Rewritten Implementation Plan

### Phase 0 — Isolate the cleanup diff

**Purpose:** Reduce the branch to the cleanup allowlist before any further cleanup judgment or follow-up edits.

**Required steps:**

1. Inspect the current working tree and list every modified or untracked path.
2. Move every non-allowlist change out of this cleanup branch via branch split, stash, or revert.
3. Keep the cleanup branch focused only on the 8 allowlist files.
4. Re-check the resulting diff until every remaining changed path is inside the allowlist.

**Required invariant after Phase 0:**

`git diff --name-only` returns only files from Section 4.

**Notes from latest review:**

This phase is still not done and is the highest-priority remaining blocker.

---

### Phase 1 — Trim `maho_sidebar_create_space_view.cc/.h` back to remediation-only scope

**Purpose:** Remove the scope creep that caused the latest review failure.

**Files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.h`

**Keep exactly these behavioral changes:**

1. Empty-id guard in `OnSpaceCreated`.
2. Helper-based profile-path resolution via `MahoSpaceProfileBridge::ProfileBasenameForId`.
3. No-mutation failure behavior on invalid helper result.
4. Browser-scoped `SwitchToSpace(browser_, new_space_id)`.
5. Post-switch sidebar refresh hand-off via `ScheduleRefreshAll()`.
6. Canonical lowercase `"default"` sentinel behavior, with legacy tolerance preserved through the helper path.

**Revert these categories back to baseline unless separately re-landed in their own plan:**

- Custom hero / decorative create-space header work.
- Expanded profile-row UI composition beyond what is required for the remediation.
- Icon picker UI plumbing.
- Theme preview tile / native theme selection state infrastructure.
- Extra layout restructuring and non-remediation label/affordance rewrites.
- Header members and methods that exist only to support the expanded UI-heavy version.

**Required invariant after Phase 1:**

`maho_sidebar_create_space_view.cc/.h` differ from baseline only by the narrow remediation behavior and the helper usage. No unrelated UI rework remains.

---

### Phase 2 — Preserve and re-verify helper centralization

**Purpose:** Treat the helper work as landed, but verify it stays intact after Phase 0 and Phase 1 cleanup.

**Files:**
- `maho-chromium/browser/maho_space_profile_bridge.h`
- `maho-chromium/browser/maho_space_profile_bridge.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`
- `maho-chromium/browser/maho_browser_main_extra_parts.cc`

**Status from latest review:** already landed.

**Required checks:**

1. `ProfileBasenameForId` remains declared on `MahoSpaceProfileBridge`.
2. `kMahoProfileIdMaxLen` remains present.
3. The helper remains the only C++ producer of `Default` / `MahoProfile_*` mapping.
4. `OnSpaceCreated(...)` still routes through the helper.
5. `PopulateBridgeFromCore(...)` still routes through the helper.
6. Invalid helper results still prevent bridge mutation in create-space flow.
7. Invalid helper results still skip hydration entries in startup population flow.

**Required invariant after Phase 2:**

The helper remains the single source of truth for profile-path construction and validation.

---

### Phase 3 — Preserve and re-verify the browser-scoped sidebar reader fix

**Purpose:** Confirm the previously landed reader fix remains intact after cleanup isolation.

**Files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.h`

**Status from latest review:** already landed.

**Required checks:**

1. `RefreshFavoritesAndViewState()` still reads `GetActiveSpaceId(browser_)`.
2. `UpdateSpacesPaneActions()` still reads `GetActiveSpaceId(browser_)`.
3. `OpenSpaceConfigSurface()` still reads `GetActiveSpaceId(browser_)`.
4. No unrelated drift remains in the header.

**Required invariant after Phase 3:**

The diff in `maho_sidebar_view.cc/.h` is limited to the original browser-scoped reader cleanup.

---

### Phase 4 — Finish the test cleanup

**Purpose:** Keep the already-landed helper/failure tests, and fix the test issues still called out by the latest review.

**Files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`

**Already landed and should be preserved:**

- `ProfileBasenameForIdValidation`
- `ExplicitFailurePathNoMutation`

**Required remaining changes:**

1. **Rewrite `StartupHydrationParity`**
   - It must stop being a self-comparison.
   - It must prove parity between the startup hydration path and the create-space success path, or else be replaced by a narrower but honest invariant test.

2. **Strengthen `FooterActionOpensSpacesSurface`**
   - Remove the weak `navigated_to_spaces || popover_visible` assertion.
   - Assert one concrete expected behavior.

3. **Remove soft-skip behavior from `SpaceDotSwitchesTabList`**
   - Eliminate the warning-and-return path.
   - Make the test setup guarantee the needed fixture state, or fail loudly if that setup is missing.

4. **Keep non-default mapping coverage**
   - Ensure the helper test still explicitly covers non-default accepted ids such as `"alpha"` and `"Default-Like-But-Not"`.

5. **Keep invalid-id rejection coverage**
   - Ensure the helper test still explicitly covers:
     - whitespace-only input
     - separators
     - `.` / `..`
     - `..` substring cases
     - leading dot
     - non-ASCII
     - overlength ids

**Required invariant after Phase 4:**

The test file no longer contains the three review-caught weaknesses:
- no tautological parity test,
- no weak `A || B` assertion for the key behavior,
- no silent early-return skip for the space-dot switch path.

---

## 6. File-by-File Planned Changes

### `maho_sidebar_create_space_view.cc`
- Trim back to remediation-only behavior.
- Preserve helper usage and no-mutation failure behavior.
- Preserve post-switch sidebar refresh.

### `maho_sidebar_create_space_view.h`
- Remove header drift introduced solely by the expanded UI-heavy version.

### `maho_space_profile_bridge.h`
- Preserve `ProfileBasenameForId` declaration and `kMahoProfileIdMaxLen`.

### `maho_space_profile_bridge.cc`
- Preserve helper implementation exactly as the single source of truth for profile path resolution.

### `maho_browser_main_extra_parts.cc`
- Preserve helper-based startup hydration mapping and invalid-id skip behavior.

### `maho_sidebar_view.cc`
- Preserve browser-scoped active-space reads.

### `maho_sidebar_view.h`
- Revert any unrelated drift if present.

### `maho_sidebar_spaces_interactive_test.cc`
- Keep landed helper/failure coverage.
- Rewrite or strengthen the three tests still called out by the latest review.

---

## 7. Static Verification Plan (Build-Free)

This rewritten version intentionally excludes build/run/manual-QA content.

### 7.1 Scope verification
- `git diff --name-only` must be a subset of the Section 4 allowlist.

### 7.2 Diagnostics
- `lsp_diagnostics` must be clean for every allowlist file.

### 7.3 Helper invariant
- Search for `FilePath::FromASCII("MahoProfile_"` under `maho-chromium/browser/`.
- Expected result: the only production hit is inside `MahoSpaceProfileBridge::ProfileBasenameForId`.

### 7.4 Sidebar reader invariant
- Search for `GetActiveSpaceId(` in `maho_sidebar_view.cc`.
- Expected result: every production call passes `browser_`.

### 7.5 Test-source review
- Confirm that:
  - `StartupHydrationParity` is no longer tautological.
  - `FooterActionOpensSpacesSurface` no longer uses the weak disjunctive assertion.
  - `SpaceDotSwitchesTabList` no longer soft-skips.

---

## 8. Out of Scope

The following remain explicitly deferred:

1. Any GN file changes.
2. Any Rust crate changes.
3. Command palette changes.
4. AI panel changes.
5. Location bar changes.
6. Theme-picker WebUI changes.
7. Re-landing the trimmed UI rework from `maho_sidebar_create_space_view.cc` under a separate visual/UI-focused plan.
8. Architecture redesigns.
9. Runtime build execution and manual QA steps.

---

## 9. Definition of Done

This rewritten cleanup pass is done when all of the following are true:

1. The diff is isolated to the 8-file allowlist.
2. `maho_sidebar_create_space_view.cc/.h` contain only remediation-scoped changes plus helper usage.
3. `ProfileBasenameForId` remains the single source of truth for profile-path construction.
4. Both production call sites still route through the helper.
5. Browser-scoped sidebar reader fixes remain intact.
6. The helper validation and no-mutation tests remain present.
7. `StartupHydrationParity` is no longer tautological.
8. `FooterActionOpensSpacesSurface` no longer uses the weak disjunctive assertion.
9. `SpaceDotSwitchesTabList` no longer soft-skips.
10. `lsp_diagnostics` is clean for all allowlist files.
11. No out-of-scope GN, Rust, command, AI, location-bar, WebUI, or unrelated sidebar diff remains.

---

## 10. Recommended Execution Order

1. **Phase 0** — isolate the working tree to the allowlist.
2. **Phase 1** — trim `maho_sidebar_create_space_view.cc/.h`.
3. **Phase 2** — verify helper centralization survived trimming.
4. **Phase 3** — verify sidebar reader cleanup remained intact.
5. **Phase 4** — fix the remaining test-quality issues.
6. Run the static verification checklist in Section 7.
7. Check completion against Section 9.
