# Sidebar Search Utility-Affordance Regression Remediation Plan (2026-05-23)

## Goal

Restore the sidebar search pill's trailing utility affordance (the address-bar-style utility icon) on cold start and during transient tab-activation states.

The regression is that `MahoSidebarSearchView::Update()` demotes `has_active_tab` from `true` to `false` whenever `TabStripModel::active_index() == TabStripModel::kNoTab`, even if the strip still contains tabs. That demoted value is fed straight into `CanShowUtilityAffordance()`, which then hides the utility icon slot. The visible symptom is that the address-bar-style utility icons disappear from the sidebar search pill.

The fix direction is narrow and surgical:

1. In `MahoSidebarSearchView::Update()`, treat only `!tsm || tsm->count() == 0` as a genuine zero-tab strip for demotion purposes.
2. Stop using `tsm->active_index() == TabStripModel::kNoTab` as a synonym for "the strip is empty". A transient `kNoTab` during activation is not the same as a strip with zero tabs.
3. Add or strengthen sidebar interactive tests that lock the demotion contract so the regression cannot return silently.
4. Leave the location-bar utility panel/icon code untouched. The bug is at the model-feeding layer, not at the panel/icon layer.

## Target contract

After this plan lands, the following must hold:

1. In `MahoSidebarSearchView::Update(const MahoSidebarSearchModel& model)`, the latch that protects `has_active_tab` from being demoted to `false` only releases (i.e. allows demotion) when `browser_` has no `TabStripModel` or `tab_strip_model()->count() == 0`.
2. `tab_strip_model()->active_index() == TabStripModel::kNoTab` no longer participates in the "strip is empty" decision inside `Update()`.
3. `CanShowUtilityAffordance()` and `UpdateUtilityAffordanceVisibility()` are not edited. Their behavior is exactly preserved; they just receive a non-spuriously-demoted `has_active_tab`.
4. `MahoSidebarStateAdapter::BuildSearchModel()` is not edited. Its existing semantics (`has_active_tab = true` only when `GetActiveWebContents(browser)` is non-null) remain the source of truth for "is there a real, committed active tab right now?".
5. The first prime branch in `Update()` (the live-tab-strip prime that uses `active_index != kNoTab && ContainsIndex(active_index)`) is not edited. That branch is intentionally permissive at first paint and is correct as written.
6. There exists at least one sidebar interactive test that fails today and passes after the fix, exercising the path "model briefly arrives with `has_active_tab=false` while `tsm->active_index() == kNoTab` and `tsm->count() > 0`".

## Explicit non-goals

1. No change to any location-bar utility-panel or utility-icon source. Out of scope:
   - `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.{cc,h}`
   - `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.{cc,h}`
   - `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.{cc,h}`
   These files render the icon and the panel; the regression is upstream of them.
2. No change to `MahoSidebarStateAdapter::BuildSearchModel()` semantics. Adapter contract stays: `has_active_tab=true` iff `GetActiveWebContents(browser) != nullptr`.
3. No change to `CanShowUtilityAffordance()`, `UpdateUtilityAffordanceVisibility()`, `UpdateIdleSurface()`, or the search-pill chrome wiring.
4. No change to the favorites pipeline, `OnThemeChanged` schedule logic, or `RefreshAll`/`ScheduleRefreshAll`.
5. No change to `MahoSidebarSearchModel` shape.
6. No commit, branch, or git ceremony is in scope for this plan.
7. No "while we're here" cleanup of unrelated branches in `Update()`, `BeginEditing`, or `EndEditing`.

## Current code findings

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.cc`

`MahoSidebarSearchView::Update(const MahoSidebarSearchModel& model)` runs two staged adjustments before assigning `model_`:

1. Lines ~280-289 (live-tab-strip prime): if the incoming model says no active tab but the live strip has a valid `active_index` that is `ContainsIndex`-true, force `patched.has_active_tab = true`. This handles cold start where the WebContents is not yet committed but the strip already has the tab. This branch is correct.
2. Lines ~296-304 (anti-demotion latch): if the previous model had `has_active_tab=true` but the new one says false, the latch keeps it `true` unless the strip is "empty". The current empty test is:
   ```cpp
   const bool strip_is_empty =
       !tsm || tsm->count() == 0 ||
       tsm->active_index() == TabStripModel::kNoTab;
   ```
   This is wrong. `active_index() == kNoTab` can fire transiently while the user closes a tab and the next active tab is being chosen, while `count()` is still > 0. When that happens the latch concludes "the strip is empty", releases, and `patched.has_active_tab` stays `false`. `model_` is then committed with `has_active_tab=false`, `UpdateIdleSurface()` runs, and `UpdateUtilityAffordanceVisibility()` hides `utility_icon_slot_`.

`CanShowUtilityAffordance()` returns `!is_editing_ && model_.has_active_tab`. It is correct, but it is at the mercy of whatever `Update()` puts into `model_.has_active_tab`. The bug is upstream of this method.

`UpdateIdleSurface()` and `UpdateSearchChrome()` consume `model_.has_active_tab` for other branching too (icon glyph color, secondary label visibility). Those uses are fine; they only need the value to be non-spurious.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`

`BuildSearchModel(Browser*)` sets `has_active_tab = true` exclusively inside the `if (content::WebContents* contents = GetActiveWebContents(browser))` block (line ~831). Independently, lines 85-89 use the same `active_index != kNoTab && ContainsIndex(active_index)` pattern to determine "do we have a valid active tab" before returning the WebContents. So during cold start, the adapter can correctly report `has_active_tab=false` because the WebContents is not yet available, even when the strip has tabs. That is the case the live-tab-strip prime in `Update()` is designed to mask, and the anti-demotion latch is designed to keep masked once the truth has been observed.

The state adapter is **not the bug**. The state adapter is honestly reporting "no committed contents yet". The sidebar search view's latch is supposed to protect against transient false demotions; today it does the opposite.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`

`MahoSidebarView::UpdateSearch()` simply delegates to `search_view_->Update(model)`. No interesting logic. This file is in scope only as a documented call path; no edit needed.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_edge_cases_interactive_test.cc`

The existing test `OnThemeChangedDoesNotHideUtilityAffordance` (line ~687) and `UtilityIconVisibleOnFirstPaintWithoutSeed` (line ~778) cover related but distinct paths:

- `OnThemeChanged…` covers the case where theme change-driven `RefreshAll` fires before the strip is ready and `has_active_tab` arrives false at first paint. That is fixed by switching to `ScheduleRefreshAll`. It does not exercise the demotion latch.
- `UtilityIconVisibleOnFirstPaintWithoutSeed` covers the cold-start path where `UpdateSearch()` is called from the constructor before async favorites refresh. Again, distinct from demotion.

Neither test exercises the case that the regression breaks: previously `has_active_tab=true`, then a refresh arrives with `has_active_tab=false` while `tsm->active_index() == kNoTab` but `tsm->count() > 0`. A new test is needed for that specific shape.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_actions_interactive_test.cc`

This file already exposes `search->utility_icon_slot_for_testing()` and uses `search->utility_icon_for_testing()`. Pattern at lines 434, 508, 544, 587, 647 etc. is well-established: get the search view, get the utility slot, assert visibility. We can either add a test here or in the edge-cases file. The edge-cases file is a better fit because demotion is an edge-case latch concern.

### Production-code bug summary

Single-call-site fix. Single boolean expression. The minimal patch removes one OR clause:

```cpp
// before
const bool strip_is_empty =
    !tsm || tsm->count() == 0 ||
    tsm->active_index() == TabStripModel::kNoTab;

// after
const bool strip_is_empty = !tsm || tsm->count() == 0;
```

Everything else is verification.

## File-by-file implementation plan

### File 1 (modified): `maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.cc`

Function: `MahoSidebarSearchView::Update(const MahoSidebarSearchModel& model)`.

Changes:

1. In the anti-demotion latch (the `if (model_.has_active_tab && !patched.has_active_tab && browser_)` block, currently lines ~296-304):
   - Replace the three-clause `strip_is_empty` definition with a two-clause definition: `const bool strip_is_empty = !tsm || tsm->count() == 0;`
   - Keep the surrounding control flow exactly: still grab `TabStripModel* tsm = browser_->tab_strip_model();`, still apply `if (!strip_is_empty) { patched.has_active_tab = true; }`.
   - Update the inline comment immediately above the latch to spell out the new contract: "Genuine zero-tab is `!tsm || tsm->count() == 0`. A transient `active_index() == kNoTab` while the strip still has tabs is **not** zero-tab; demoting on it would hide the utility affordance during normal tab-activation transitions."
2. Do not touch the live-tab-strip prime above the latch (lines ~280-289). Its use of `active_index != kNoTab && ContainsIndex(active_index)` is correct in that direction (it only **promotes** to `true`).
3. Do not touch `CanShowUtilityAffordance`, `UpdateUtilityAffordanceVisibility`, `UpdateIdleSurface`, `UpdateSearchChrome`, `BuildPrimaryText`, `BuildSecondaryText`, or any of the editing/widget code.
4. Do not add new fields, new helpers, new includes, or new headers. The fix is a one-expression replacement plus a comment refresh.

Constraints for this file:

1. The diff must be confined to the latch block and its preceding comment. No drive-by formatting changes elsewhere in the file.
2. The semantics must be: "demote `has_active_tab` to `false` only when there is no `TabStripModel` or it holds zero tabs". Anything else is a regression.

### File 2 (modified): `maho-chromium/browser/ui/views/sidebar/maho_sidebar_edge_cases_interactive_test.cc`

Add one new `IN_PROC_BROWSER_TEST_F` next to the existing utility-affordance edge-case tests (after `OnThemeChangedDoesNotHideUtilityAffordance`, before `FavoritesIsolatedToActiveSpaceAfterSpaceSwitch` is fine, or grouped with `UtilityIconVisibleOnFirstPaintWithoutSeed`).

Test name (suggested): `UtilityAffordanceSurvivesTransientActiveIndexNoTab`.

Behavioral shape:

1. `SeedProfile(2);` to get a stable tab-strip baseline with at least one tab.
2. `ResolveViews();` then capture `MahoSidebarSearchView* search = sidebar_->search_view_for_testing();` and `views::View* utility_slot = search->utility_icon_slot_for_testing();`.
3. Wait via `base::test::RunUntil` for `utility_slot->GetVisible()` to be true, asserting baseline.
4. Synthesize the regression's preconditions inside the search view by feeding two models in sequence through `search->Update(...)` (or whatever the existing test seam is — the file already builds search models inline; reuse that pattern):
   - First model: `has_active_tab=true`, valid `current_url`, normal `display_mode`. This establishes the latch's "previously true" state.
   - Second model: `has_active_tab=false`. Before invoking the second `Update()`, do not modify the actual `TabStripModel`. The browser still has tabs (`count() > 0`), but in many tab-activation transitions `active_index()` momentarily reads `kNoTab`. Since the test fixture cannot trivially force a transient `kNoTab`, prefer one of these equivalent options:
     - Option A: directly test the latch by ensuring the live strip currently has `count() > 0` and `active_index() == kNoTab` is *not* required — the latch must hold open whenever `count() > 0`, regardless of `active_index`. Assert that after the false-arriving second update, `utility_slot->GetVisible()` is still `true`.
     - Option B: detach the active WebContents in a way that produces `active_index() == kNoTab` momentarily but `count() > 0` (closing the active tab via `tab_strip_model()->CloseWebContentsAt(active_index, 0)` while a second tab exists). Choose Option A for simplicity unless Option B is needed for full coverage.
5. Assert `utility_slot->GetVisible()` remains `true` after the demotion-shaped update.
6. Add a clear failure message that names the regression: `"Utility slot must remain visible when has_active_tab=false arrives but the tab strip still has tabs; demoting on transient kNoTab is the regression."`

Constraints for this file:

1. Do not modify any existing test body.
2. Do not modify `SeedProfile`, `ResolveViews`, or any helper.
3. Do not introduce new headers beyond what is needed by the new test (likely none; the file already includes all needed search-view headers).
4. The new test must fail today against the current production code and pass after File 1 is fixed.

### File 3 (modified): `maho-chromium/browser/ui/views/sidebar/maho_sidebar_actions_interactive_test.cc`

Add one defensive assertion-style test next to the existing utility-icon visibility tests (around lines 434-690), or, if the demotion-latch concept is genuinely owned by the edge-cases test file, leave this file unchanged and document the decision (see "Modified vs intentionally not modified" below).

Recommended approach for this file:

1. Keep the existing utility-icon tests untouched.
2. Add one targeted regression test, name suggestion: `UtilitySlotStaysVisibleAcrossDemotionAttempt`. It should:
   - Build a fresh `MahoSidebarSearchView` via the same construction path the file already uses for action tests.
   - Send a primer `MahoSidebarSearchModel` with `has_active_tab=true` through `search->Update(...)`.
   - Send a follow-up `MahoSidebarSearchModel` with `has_active_tab=false` through `search->Update(...)` while the underlying browser's `tab_strip_model()->count() > 0`.
   - `EXPECT_TRUE(search->utility_icon_slot_for_testing()->GetVisible())`.
3. This test is a unit-level corollary to the more end-to-end edge-cases test. If both feel duplicative, prefer keeping only the edge-cases test (File 2) and leave this file unchanged. Make that call after writing File 2 and observing whether it adequately covers the latch.

Constraints for this file:

1. Do not modify any existing test body.
2. Do not modify shared helpers, fixtures, or imports beyond what the new test requires.
3. If you decide to skip this file, document the skip in the plan's "Modified vs intentionally not modified" section as a deliberate choice, not an oversight.

### File 4 (intentionally not modified): `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`

Reasoning:

1. `BuildSearchModel()` correctly reports `has_active_tab = true` only when `GetActiveWebContents(browser) != nullptr`. That is the right contract for the adapter.
2. Loosening that contract (e.g. promoting on `active_index != kNoTab && ContainsIndex(active_index)`) would make the adapter overlap with `Update()`'s live-tab-strip prime and diffuse responsibility.
3. The regression is the latch's release condition, not the adapter's truth function.

Therefore: do not edit. Document the decision in the plan and in the new tests' comments so future readers understand why the latch lives where it lives.

### File 5 (intentionally not modified): `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`

Reasoning:

1. `MahoSidebarView::UpdateSearch()` is a pure forwarder to `search_view_->Update(model)`.
2. There is no decision logic here that affects the regression.
3. Adding logic here would couple the parent view to tab-strip semantics that already belong in the adapter or the search view.

Therefore: do not edit. Cite this file only as part of the call graph.

### Files 6-N (intentionally not modified): location-bar utility panel/icon files

Out of scope:

- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.{cc,h}`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.{cc,h}`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.{cc,h}`

Reasoning:

1. The icon view handles its own visibility via `SetAffordanceVisible` / `SetHoverVisible`, which are driven by the search view based on `CanShowUtilityAffordance()`.
2. The panel view and panel provider are model builders for the popup; they have no input into the sidebar's "should I show the icon" decision.
3. Editing these files would be a category mismatch: the regression is in the sidebar's model latch, not in the icon's render path or the panel's model construction.

Therefore: do not edit. If a reviewer suggests touching them, push back and point to this section.

## Modified vs intentionally not modified — summary table

| File | Status | Why |
|------|--------|-----|
| `maho_sidebar_search_view.cc` | Modified | Single-line fix in `Update()` latch |
| `maho_sidebar_edge_cases_interactive_test.cc` | Modified | New regression test for the demotion latch |
| `maho_sidebar_actions_interactive_test.cc` | Modified (optional) | Optional unit-level corollary test; skip if the edge-cases test fully covers |
| `maho_sidebar_state_adapter.cc` | Not modified | Adapter contract is correct; not the source of the bug |
| `maho_sidebar_view.cc` | Not modified | Pure forwarder to search view |
| `maho_location_bar_utility_icon_view.{cc,h}` | Not modified | Render path only; downstream of the bug |
| `maho_location_bar_utility_panel_view.{cc,h}` | Not modified | Popup view; unrelated to demotion |
| `maho_location_bar_utility_panel_provider.{cc,h}` | Not modified | Model builder for popup; unrelated to demotion |

## Verification plan

### Code-level verification

1. Run `lsp_diagnostics` on `maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.cc` after the edit. Expect zero new errors and zero new warnings.
2. Run `lsp_diagnostics` on `maho-chromium/browser/ui/views/sidebar/maho_sidebar_edge_cases_interactive_test.cc` after the edit. Expect zero new errors and zero new warnings.
3. If File 3 is touched, run `lsp_diagnostics` on `maho-chromium/browser/ui/views/sidebar/maho_sidebar_actions_interactive_test.cc` too.
4. Visually re-read the diff to confirm:
   - The deleted clause is exactly `tsm->active_index() == TabStripModel::kNoTab`.
   - No other clauses changed.
   - Comment above the latch reflects the new semantics.
5. Re-read the new test(s) to confirm:
   - They start from an authentic `has_active_tab=true` baseline.
   - They send a follow-up `has_active_tab=false` model while `tsm->count() > 0`.
   - They assert `utility_slot->GetVisible() == true` after the second update.
   - The failure message names the regression so future readers understand the lock.

### Test-level verification

1. Run the new sidebar interactive tests:
   - `MahoSidebarEdgeCasesInteractiveTest.UtilityAffordanceSurvivesTransientActiveIndexNoTab` (or the chosen name).
   - If File 3 is touched: the new actions test.
   These must fail against the unfixed production code (sanity check before applying File 1) and pass after File 1.
2. Run the surrounding suites as a regression sweep:
   - `MahoSidebarEdgeCasesInteractiveTest.*` — confirm `OnThemeChangedDoesNotHideUtilityAffordance` and `UtilityIconVisibleOnFirstPaintWithoutSeed` still pass.
   - `MahoSidebarActionsInteractiveTest.*` — confirm existing utility-icon visibility tests are unaffected.
3. Run any other sidebar interactive tests in the same target so unrelated paths get exercised.

### Runtime / manual verification

1. Build via `python3 maho-chromium/build/scripts/build_maho.py` (canonical build).
2. Launch the browser into a normal session with at least two tabs.
3. Confirm the sidebar search pill renders the trailing utility icon at startup (this is the scenario the user originally reported).
4. Switch active tabs by clicking adjacent tabs in the sidebar list. Confirm the utility icon does not flicker out during the transition.
5. Close the active tab while another tab remains. Confirm the utility icon remains visible across the close.
6. Open and close the utility panel via clicking the icon and the trailing inset. Confirm both hit zones still trigger the panel as before (regression sanity for `ShouldToggleUtilityPanelForEvent` is implicit but worth a manual smoke).
7. Close all tabs to drive the strip to a true zero-tab state. Confirm the utility icon hides as it should — this validates the *kept* portion of the latch (`!tsm || tsm->count() == 0`).
8. Reopen a tab. Confirm the utility icon comes back.
9. Trigger a theme change. Confirm the utility icon does not flicker.
10. Spot-check the address bar/utility icon clicks open the existing panel content. No visual regressions in the panel itself.

## Risks

1. **Hidden coupling on `active_index() == kNoTab`**: there may be other call sites that conflate "transient kNoTab" with "zero tabs". The fix here is local to the search view's latch; no audit of other call sites is in scope. If a reviewer asks for a wider audit, that is a follow-up plan, not this one.
2. **Test reproducibility**: forcing `active_index() == kNoTab` while `count() > 0` is timing-sensitive. The recommended Option A in File 2 sidesteps that by directly verifying the latch behavior at the model layer rather than the strip layer. If Option A is judged too unit-y, Option B (close active tab while another exists) is acceptable but flakier.
3. **Latch over-permissiveness**: removing one demotion path means the latch will keep `has_active_tab=true` longer in unusual scenarios. The only path to demotion is now `!tsm || tsm->count() == 0`, which is exactly the user-visible "no tabs at all" case. That is the documented intent.
4. **No state-adapter loosening**: someone may push back with "why not also make the adapter promote on `ContainsIndex(active_index)`?". The answer is that the search view's prime branch already handles that, and adding it to the adapter would diffuse responsibility. Resist that scope creep.
5. **GN regen / build flow**: per project AGENTS.md, do not edit any GN file to chase the build. The fix in this plan is C++ only; if the build fails at a GN regen step, stop and report.

## Suggested implementation order

1. Read the current `Update()` body in `maho_sidebar_search_view.cc` end-to-end one more time and confirm the fix is the one-clause removal described in File 1.
2. Add the new edge-case test (File 2) **first**. Run it against the unfixed code and confirm it fails. This locks the contract.
3. Apply the one-clause fix in File 1.
4. Re-run the new test. Confirm it passes.
5. Optionally add the actions-test corollary (File 3). Decide based on whether the edge-cases test alone covers the latch convincingly.
6. Run `lsp_diagnostics` on all modified files.
7. Run the local sidebar interactive test target as a regression sweep.
8. Run the canonical build and walk the manual verification steps.

## Done definition

Work is done only when all of the following are true:

1. `MahoSidebarSearchView::Update()`'s anti-demotion latch uses `const bool strip_is_empty = !tsm || tsm->count() == 0;` and nothing more.
2. The clause `tsm->active_index() == TabStripModel::kNoTab` no longer appears inside the latch.
3. The comment above the latch explains the new contract.
4. A new sidebar interactive test fails against the pre-fix code and passes against the post-fix code, exercising "previously `has_active_tab=true`, then `has_active_tab=false` arrives while `tsm->count() > 0`".
5. All previously passing sidebar interactive tests still pass.
6. `lsp_diagnostics` is clean on every modified file.
7. Manual verification confirms the utility icon is visible on startup with tabs, survives tab activation transitions, survives closing the active tab while others remain, and correctly hides only when the strip is genuinely empty.
8. No file outside the modified set in the table above is touched. Specifically: `maho_sidebar_state_adapter.cc`, `maho_sidebar_view.cc`, and the location-bar utility panel/icon files are byte-for-byte unchanged.
