# Sidebar Parity Follow-Ups & Retroactive Documentation Plan — 2026-06-07

**Status:** Draft (awaiting Momus review then phased execution)
**Owner:** Sisyphus
**Scope:** Sidebar scope-creep that landed in the same commit as `sidebar-typography-icon-zen-parity-plan-2026-06-07.md` (v2). Splits into (a) retroactive docs for orphan implementations, and (b) completion work for under-finished items.
**Reference parity plan:** `.sisyphus/plans/sidebar-typography-icon-zen-parity-plan-2026-06-07.md`

---

## 0. Context

The Zen-typography-parity v2 implementation commit bundled **7 distinct scope-creep changes** (items A–G in the parity-review code report). After mapping each to existing `.sisyphus/plans/*.md` files AND verifying actual implementation status against working tree, status breaks down as:

| Item | Description | Existing plan | Status |
|---|---|---|---|
| **A1** | `OnSpaceSwitched()` call site | `zen-parity-drag-image-controller-v2-2026-06-07.md` | **Already wired** at `maho_sidebar_view.cc:770` |
| **A2** | `MahoSidebarDragImageController` integration (cross-window image) | same plan | DRAFT plan, **needs A2 work** (drop `BuildCrossWindowDragImage`) |
| **A3** | Controller unit tests | same plan | **Already covered** — 11 TEST_F cases in `maho_sidebar_drag_image_controller_unittest.cc` |
| **B** | `is_drag_active_` flag | `sidebar-pinned-drop-zone-flicker-2026-06-07.md` §P1 | Implementation complete |
| **C** | 2-arg `SetRevealEmptyPinnedDropLaneCallback` | `sidebar-pinned-drop-zone-flicker-2026-06-07.md` §P0 | Implementation complete |
| **D** | `last_painted_*` repaint caching | `sidebar-pinned-drop-zone-flicker-2026-06-07.md` §P0 | Partially named (only `last_painted_drag_over_` explicit) |
| **E** | `EmptyFavoritesPlaceholderView` removal | `empty-favorites-ui-rework-google-seed-2026-06-05.md` §5.2 | Implementation complete BUT **regression**: `cancelable_task_tracker_.TryCancelAll()` removed from `Update()` |
| **F** | Favicon dominant-color + roving focus | **NONE — ORPHAN** | Implementation complete, no plan; UP/DOWN handling is a Maho-only extension to be stripped |
| **G** | Search pill compact variant removal | **NONE — ORPHAN** | Implementation complete, no plan |

This plan does three things:

1. **Retroactively documents** orphan items F and G as canonical specs (so future audits can point to a plan).
2. **Closes implementation gaps** in A and E, plus minor naming cleanup in D.
3. **Updates plan-status metadata** on the 3 covering plans (B/C/D, A, E) to mark their implementation landed.

---

## 1. Goal

Eliminate all "untracked" sidebar scope from the codebase. After this plan executes:
- Every sidebar source change has a corresponding plan reference.
- A's three gaps (dead `OnSpaceSwitched`, wrong cross-window image, missing tests) are closed.
- E's regression (lost `TryCancelAll()`) is restored.
- F and G have canonical "as-built" specs that match landed code.
- Plan files for A, B, C, D, E are updated with "Implementation landed YYYY-MM-DD; verified by …" headers.

**Non-goals**:
- Re-litigating any parity decision (those are settled in v2 parity plan).
- New features beyond closing the listed gaps.
- Rewriting any of the existing 5 covering plans — only metadata updates.

**Definition of Done**:
1. Phases F-RETRO and G-RETRO produce two new plan files in `.sisyphus/plans/`.
2. Phase A-COMPLETE closes all three A-item gaps with code + tests.
3. Phase E-FIX restores `TryCancelAll()` and adds a regression test.
4. Phase D-NAMING updates the flicker plan §P0 to enumerate `last_painted_hovered_` and `last_painted_active_` explicitly.
5. Phase META updates the 5 covering plans' headers to mark implementation status.
6. All changed code passes `lsp_diagnostics` clean and `python3 build/scripts/build_maho.py --skip-rust` exits 0.

---

## 2. Phase Breakdown

### Phase F-RETRO — Retroactive Plan + UP/DOWN Strip: Favicon Dominant Color + Roving Focus

**Action**: 
1. Create `.sisyphus/plans/favorites-dominant-color-and-roving-focus-RETROACTIVE-2026-06-07.md` (documentation).
2. Strip VKEY_UP / VKEY_DOWN cases from `HandleGridKeyFromTile` (Zen-parity strict, per Q3 resolution).

**Scope of new plan** (documenting what landed):

1. **Dominant color extraction & cache** in `maho_sidebar_favorites_grid_view.cc`:
   - File-scope free function `ExtractFaviconDominantColor(const ui::ImageModel&) -> std::optional<SkColor>`. Implementation uses `color_utils::CalculateKMeanColorOfBitmap()` (verified — not a stub). Returns `nullopt` for empty/non-image models.
   - Member field `favicon_dominant_color_cache_` (`base::flat_map<GURL, SkColor>`). Keyed by tile URL. Populated in `Update()` when `live_tab_favicons` returns a non-empty image, and in `OnFaviconLoaded()` after async favicon arrival.
   - Public API: `GetFaviconDominantColor(size_t index) const -> std::optional<SkColor>` reads from cache.

2. **Active-tile border glow** consuming the cache:
   - When `tile_is_active_[i] == true`, `FavoriteTileButton::SetActiveBorderColor(*GetFaviconDominantColor(i))` paints the dominant-color border. Falls back to `ui::kColorSysSurfaceVariant` when no color is cached.
   - Test hook `SetTileActiveBorderColorForTesting(size_t, std::optional<SkColor>)` for unit tests.

3. **Drag placeholder dominant-color border** in `ActivateDragPlaceholder()`:
   - Extracts dominant color from `drag_placeholder_icon_` and applies a 50% alpha version as the placeholder tile's border (Zen-style ghost preview).

4. **Roving keyboard focus** for the favorites grid (LEFT/RIGHT/HOME/END/SPACE/RETURN — Zen parity via Firefox upstream `#advanceFocus`):
   - All tiles use `FocusBehavior::ACCESSIBLE_ONLY` after `Update()` (no individual tile is a tab stop).
   - Member field `roving_focus_index_` (size_t).
   - Methods: `PromoteRovingFocus(size_t)`, `AdvanceRovingFocus(int delta)`, `HandleGridKeyFromTile(ui::KeyboardCode)`, `GetFirstVisibleTileIndex() -> size_t`.
   - VKEY_LEFT/RIGHT/HOME/END/SPACE/RETURN handled. **VKEY_UP/DOWN explicitly NOT handled** (parity with Zen — Firefox upstream `#advanceFocus` does not row-jump on UP/DOWN; it walks `ariaFocusableItems` linearly. Adding column-step UP/DOWN was a Maho over-extension stripped per Q3 resolution.)
   - Test hooks: `roving_focus_index_for_testing()`, `HandleGridKeyFromTileForTesting(KeyboardCode)`.

5. **F-STRIP code change** (in same commit as F-RETRO doc):
   - **NOTE**: The actual code is an if/else chain, not a switch statement. Two locations need editing:

   **Location 1**: `maho_sidebar_favorites_grid_view.cc:171-173` — pre-filter that routes keys to `HandleGridKeyFromTile`:
   ```cpp
   // BEFORE:
   if (key == ui::VKEY_LEFT || key == ui::VKEY_RIGHT ||
       key == ui::VKEY_UP || key == ui::VKEY_DOWN ||
       key == ui::VKEY_HOME || key == ui::VKEY_END) {

   // AFTER (remove UP/DOWN from accept list):
   if (key == ui::VKEY_LEFT || key == ui::VKEY_RIGHT ||
       key == ui::VKEY_HOME || key == ui::VKEY_END) {
   ```

   **Location 2**: `maho_sidebar_favorites_grid_view.cc:1818` inside `HandleGridKeyFromTile`:
   ```cpp
   // BEFORE:
   const bool forward = (key == ui::VKEY_RIGHT || key == ui::VKEY_DOWN);

   // AFTER (remove VKEY_DOWN from forward; LEFT remains backward):
   const bool forward = (key == ui::VKEY_RIGHT);
   ```

   - Add a 3-line comment above the line-1818 assignment:
     ```cpp
     // Zen parity: zen-browser/desktop @ 965bb9391 does not handle ArrowUp/Down
     // for essentials grid keyboard nav; Firefox upstream #advanceFocus walks
     // ariaFocusableItems linearly. UP/DOWN handling stripped 2026-06-07.
     ```
   - Audit existing test cases: `grep -n "VKEY_UP\|VKEY_DOWN" maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc` — if any test asserts UP/DOWN behavior, delete or rewrite to VKEY_RIGHT/LEFT.
   - Add a positive test asserting UP/DOWN keys are NOT consumed by `HandleGridKeyFromTile` (focus stays on current tile).

5. **Test coverage** (already landed in `maho_sidebar_dnd_dispatch_unittest.cc`):
   - `FavoriteActive_BorderDefaultsToSurfaceVariantWithoutColor`
   - `FavoriteActive_BorderUsesInjectedDominantColor`
   - `FavoriteActive_PlusFocus_FocusBorderWinsOverActiveColor`
   - `FavoriteActive_PlusHover_FillBrightensWithoutBorderChange`
   - `FavoriteActive_TogglesBackToIdle`
   - `RovingTabStop_NoTileIsTabStopAfterUpdate`
   - `RovingTabStop_FocusUpdatesBorderAndTracksRovingIndex`
   - `RovingTabStop_ArrowKeyAdvancesFocusToNextTile`
   - `RovingTabStop_HomeKeyJumpsToFirstTile`
   - `RovingTabStop_NonVisibleTileCannotBeTabStop`
   - `FavoriteTileButtonDoesNotStealFocusOnPress`

**Plan file structure** (for the retroactive doc):
- §1 Goal (eliminate untracked dominant-color + roving-focus code)
- §2 Files touched (favorites_grid_view.cc/h)
- §3 As-built API surface
- §4 Test coverage (list of 11 tests above)
- §5 Known limitations (multi-row UP/DOWN not test-covered)
- §6 Reference: `favorite-active-glow-and-favicon-source-fix-plan-2026-06-03.md` (which excluded dominant color as non-goal #1; this plan is the resolution)
- §7 Status: Implementation landed 2026-06-07; documentation 2026-06-07

**Follow-up work to capture in plan**:
- N/A — F-RETRO + F-STRIP closes all F gaps in this commit (multi-row tests no longer needed since multi-row behavior is removed for Zen parity).

**Verification**:
- Plan file exists in `.sisyphus/plans/`.
- Cross-references to `favorite-active-glow-and-favicon-source-fix-plan-2026-06-03.md` are bidirectional (also amend that plan's "Non-goals" section to point to this retroactive plan).

**Effort**: 30 min (writing) + 20 min (cross-link amendment) + 20 min (F-STRIP code + test audit) = **70 min total**.

---

### Phase G-RETRO — Retroactive Plan: Search Pill Compact Variant Removal

**Action**: Create `.sisyphus/plans/sidebar-search-pill-compact-variant-removal-RETROACTIVE-2026-06-07.md`.

**Scope of new plan** (documenting what landed):

1. **Removed tokens** from `maho_sidebar_layout_tokens.h`:
   - `kSearchPillCompactHeightDp` (was 35dp)
   - `kSearchPillCompactCornerRadiusDp` (was 18dp)

2. **Removed local constants** from `maho_sidebar_search_view.cc`:
   - `kSearchPillTrailingInsetDp` (was 15dp)
   - `kSearchPillLeadingInsetDp` (was 13dp)

3. **Unified `MahoSidebarSearchView::UpdateSearchChrome`**:
   - Both placeholder and active modes now use `kSearchPillCornerRadiusDp` (16dp) and `kSearchPillHeightDp` (38dp).
   - Insets unified to `kSearchCardLeadingInsetDp` / `kSearchCardTrailingInsetDp`.
   - Visual distinction between placeholder vs active modes is now color-only (border + label color), not dimensional.

4. **Context-menu callback change** (related but separate):
   - `ShowContextMenuForViewImpl` now binds `FocusCommandSurface` instead of `BeginEditing` to the address-bar-context-menu callback.
   - Right-click → "Search" now opens the command palette overlay (not the in-pill textfield).
   - `BeginEditing` is still called from `OnIdleButtonPressed` (left-click).

**Rationale to capture** (per Zen parity intent):
- Zen has a single pill geometry — `--urlbar-height: 38px`, `--urlbar-container-height: 48px` default. No compact variant exists. The compact variant in Maho was a pre-Zen-parity design artifact; removing it aligns with Zen's unified pill structure (parity §4C in the v2 plan).
- The `FocusCommandSurface` change is a separate UX decision — opening the command palette on right-click is more discoverable than entering inline edit mode. **This decision should be reviewed for intent**.

**Test coverage**: Currently zero. The plan will identify two test gaps:
1. No test asserts the unified pill geometry across placeholder ↔ active transition.
2. No test asserts the context menu now opens the command palette (vs the textfield).

**Follow-up work to capture in plan**:
- Add minimal unit test for the geometry unification (placeholder pill height = active pill height).
- Add interactive test or document deliberate non-test (e.g., "covered by manual QA") for the `FocusCommandSurface` context menu binding.

**Verification**:
- Plan file exists.
- Search the codebase for any external references to `kSearchPillCompactHeightDp` / `kSearchPillCompactCornerRadiusDp` to confirm full removal: `grep -rn "kSearchPillCompact" maho-chromium/ chromium/src/maho/`.

**Effort**: 25 min.

---

### Phase A-COMPLETE — Close DragImageController Gaps

**Status of covering plan**: `zen-parity-drag-image-controller-v2-2026-06-07.md` is **DRAFT** with three gaps in the landed implementation:

#### Gap A1 — `OnSpaceSwitched()` is dead code — **CLOSED (verification only)**

**Symptom (claimed)**: `MahoSidebarDragImageController::OnSpaceSwitched()` is defined but has zero call sites.

**Verification result**: **Already wired.** Direct grep confirmed:
```
maho_sidebar_view.cc:768  void MahoSidebarView::OnSpaceProfileBridgeChanged() {
maho_sidebar_view.cc:770    drag_image_controller_->OnSpaceSwitched();
```

**Action**: None. The earlier scope-creep audit (`bg_1f82aa83`) flagged this as missing, but Momus parity-gate review confirmed the wiring exists at line 770. No code change needed in this phase.

**Effort**: 0 min (verification step only).

#### Gap A2 — Cross-Window Image Uses Favicon Instead of Page Thumbnail

**Symptom**: `BuildCrossWindowDragImage` is called with `source_->favicon` as the `page_thumbnail` argument. The function renders a 16dp favicon stretched across a 200×130 canvas.

**Resolution (per Q1 = Option 1)**: Drop the cross-window thumbnail feature entirely.

**Action**:
1. In `MahoSidebarDragImageController::OnDragUpdated`, replace any `Zone::kCrossWindowOut` branch that selected `BuildCrossWindowDragImage` with `BuildTabStackDragImage` (regular tab-stack image looks adequate when dragged out of window). Or, if the simpler path is to just stop swapping when crossing the window boundary, remove the zone-detection branch and let the original `Widget::SetDragImage` from `WriteDragDataForView` persist.
2. Delete `BuildCrossWindowDragImage` from `maho_sidebar_drag_image_factory.cc/h` (verify no other callers via `grep -rn "BuildCrossWindowDragImage" maho-chromium/`).
3. Remove `Zone::kCrossWindowOut` from the `Zone` enum if it has no remaining behavior. If it still influences any state machine, keep the enum value but document that the rendered image is unchanged.
4. Add a 2-line comment in the controller noting: "Cross-window drag image is intentionally NOT swapped (Zen parity: Zen relies on the OS-native drag image when crossing windows). See `sidebar-scope-creep-followups-and-retroactive-2026-06-07.md` Phase A-COMPLETE A2."

**Rationale**: Zen does not implement cross-window drag image swapping — when the user drags out of the Zen sidebar, the OS-native drag image (Firefox-generated favicon + URL strip) is used. Maho matching that behavior is consistent with the "TRUE Zen parity" mandate.

**Verification**:
- `grep -rn "BuildCrossWindowDragImage" maho-chromium/` returns 0 hits after change.
- Manual QA: drag a tab from sidebar out of the Maho window onto another app or desktop. Drag image should be the OS-native fallback, not stretched-favicon.

**Effort**: 45 min (delete factory function + cleanup zone enum + manual QA).

#### Gap A3 — Zero Unit Tests for Controller — **CLOSED (verification only)**

**Symptom (claimed)**: `MahoSidebarDragImageController` has no tests.

**Verification result**: **Already covered.** Direct check confirmed:
```
maho_sidebar_drag_image_controller_unittest.cc  9420 bytes  11 TEST_F cases
```

**Action**: None. The earlier scope-creep audit (`bg_1f82aa83`) flagged this as missing, but Momus parity-gate review confirmed the file exists with 11 test cases.

**Optional follow-up** (not required by this plan): If A2's removal of `BuildCrossWindowDragImage` invalidates any existing test, update or remove the affected test in the same commit as A2.

**Effort**: 0 min (verification step only); +0–10 min if A2 invalidates a test.

#### Gap A summary

Total Phase A-COMPLETE effort: 0 + 45 + 0 = **45 min** (A1 and A3 already closed; only A2 needs work).

After Phase A-COMPLETE lands, update `zen-parity-drag-image-controller-v2-2026-06-07.md` header from "DRAFT" to "Implementation landed 2026-06-07; gaps closed by sidebar-scope-creep-followups-and-retroactive-2026-06-07.md".

---

### Phase E-FIX — Restore `cancelable_task_tracker_.TryCancelAll()` Regression

**Symptom**: `MahoSidebarFavoritesGridView::Update(model)` previously called `cancelable_task_tracker_.TryCancelAll()` at the top to cancel any in-flight async favicon load. The diff replaced this with `DeactivateDragPlaceholder(); ClearStaleTileLayerState();` — but did NOT preserve the cancel.

**Risk**: Stale `OnFaviconLoaded` callbacks can fire against a new model — the favicon for tile index `i` from the old model may arrive after `Update()` has bound tile `i` to a different favorite. Visual: wrong favicon flashes on tile, then corrects.

**Action**:
1. Open `maho_sidebar_favorites_grid_view.cc`.
2. At the top of `MahoSidebarFavoritesGridView::Update(...)`, before any model-mutating logic, add:
   ```cpp
   // Cancel any in-flight async favicon loads so stale callbacks cannot
   // arrive and bind a wrong favicon to a tile that is now a different
   // favorite. (Restored 2026-06-07; was inadvertently dropped during
   // the EmptyFavoritesPlaceholderView removal in
   // empty-favorites-ui-rework-google-seed-2026-06-05.md.)
   cancelable_task_tracker_.TryCancelAll();
   ```
3. Add a regression test in `maho_sidebar_dnd_dispatch_unittest.cc`:
   - `Update_CancelsInFlightFaviconLoads`
   - Setup: install a favicon-service test fixture that defers callbacks; trigger one favicon load; call `Update()` with a different model; verify the deferred callback is cancelled before it can fire.

**Verification**:
- New test passes.
- Manual QA: rapidly switch spaces with favorites that have distinct favicons; verify no flash of wrong favicon on tiles.

**Effort**: 30 min (1-line fix + 1 test).

---

### Phase D-NAMING — Amend Flicker Plan to Enumerate All `last_painted_*` Fields

**Symptom**: `sidebar-pinned-drop-zone-flicker-2026-06-07.md` §P0 names only `last_painted_drag_over_`. The implementation also added `last_painted_hovered_` and `last_painted_active_` to `SidebarTabRowView::UpdateAppearance` for the hovered/active appearance branches.

**Action**: Edit `sidebar-pinned-drop-zone-flicker-2026-06-07.md` §P0 to add:
```
Note: The dirty-check pattern was extended in implementation to also
guard the hovered and active appearance branches. SidebarTabRowView
gained three fields (last_painted_drag_over_, last_painted_hovered_,
last_painted_active_) instead of one. The combined check is:
  if (last_painted_drag_over_ != drag_over_drop_target_ ||
      last_painted_hovered_  != hovered ||
      last_painted_active_   != active_) { ... }
```

**Verification**:
- Plan file diff shows the note added.
- `grep -n "last_painted_" maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h` returns 3 hits in `SidebarTabRowView`.

**Effort**: 10 min.

---

### Phase META — Update Plan-Status Headers on 5 Covering Plans

**Action**: Add an "Implementation status" line to the top of each plan file that landed code:

1. `zen-parity-drag-image-controller-v2-2026-06-07.md` —
   ```
   Status: DRAFT → IMPLEMENTATION LANDED 2026-06-07.
   Gap closure tracked in: sidebar-scope-creep-followups-and-retroactive-2026-06-07.md (Phase A-COMPLETE).
   ```

2. `sidebar-pinned-drop-zone-flicker-2026-06-07.md` —
   ```
   Status: IMPLEMENTATION LANDED 2026-06-07.
   §P0 dirty-check, §P0 reveal-callback gating, §P1 is_drag_active_ all live.
   ```

3. `empty-favorites-ui-rework-google-seed-2026-06-05.md` —
   ```
   Status: §5.2 (EmptyFavoritesPlaceholderView removal) IMPLEMENTATION LANDED 2026-06-07.
   Note: a `cancelable_task_tracker_.TryCancelAll()` regression was introduced and fixed in
   sidebar-scope-creep-followups-and-retroactive-2026-06-07.md (Phase E-FIX).
   ```

4. `favorite-active-glow-and-favicon-source-fix-plan-2026-06-03.md` —
   ```
   Status: PLAN-ONLY. The dominant-color path explicitly excluded as non-goal #1
   was implemented in a separate scope; see
   favorites-dominant-color-and-roving-focus-RETROACTIVE-2026-06-07.md.
   ```

5. `sidebar-typography-icon-zen-parity-plan-2026-06-07.md` —
   ```
   Status: IMPLEMENTATION LANDED 2026-06-07.
   All 16 audit deltas + 4 structural items at Zen-exact values; Momus parity-gate PASSED.
   ```

**Verification**:
- All 5 files show updated status header.

**Effort**: 15 min total (3 min per file).

---

## 3. Sequencing & Throughput

Phases are mostly independent. Recommended order:

1. **F-RETRO + G-RETRO** in parallel (writing tasks, no code conflicts) — 50 min combined.
2. **E-FIX** (one-line restore + test) — 30 min.
3. **D-NAMING** (plan amendment) — 10 min.
4. **META** (5 plan status updates) — 15 min.
5. **A-COMPLETE** (3 gap closures, longest) — sequential A1 → A2 → A3, ~2h 45m.

**Total estimate: ~5 hours focused work** (most of it in A-COMPLETE).

If A2's Option 2 (real thumbnail capture) is chosen, total balloons to ~7 hours.

---

## 4. Verification Matrix

| Phase | Compile | LSP | Tests | Plan file present | Cross-refs |
|---|---|---|---|---|---|
| F-RETRO | n/a | n/a | n/a (existing tests) | ✅ | ↔ favorite-active-glow plan |
| G-RETRO | n/a | n/a | n/a | ✅ | (standalone) |
| A-COMPLETE A1 | ✅ | ✅ | manual QA | n/a | drag-controller v2 plan updated |
| A-COMPLETE A2 (opt 1) | ✅ | ✅ | manual QA | n/a | factory header updated |
| A-COMPLETE A3 | ✅ | ✅ | new unit_tests pass | n/a | new test file in BUILD.gn |
| E-FIX | ✅ | ✅ | new test passes | n/a | empty-favorites plan updated |
| D-NAMING | n/a | n/a | n/a | flicker plan amended | (in-place edit) |
| META | n/a | n/a | n/a | 5 plan headers updated | (cross-links) |

**Pre-merge checklist**:
- `python3 maho-chromium/build/scripts/build_maho.py --skip-rust` exits 0.
- `python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests` exits 0.
- `out/Default/unit_tests --gtest_filter="MahoSidebar*"` all pass.
- All 5 covering plan files have updated status headers.
- 2 new retroactive plan files (F-RETRO, G-RETRO) exist in `.sisyphus/plans/`.

---

## 5. Risk Register

| ID | Risk | Likelihood | Mitigation |
|---|---|---|---|
| FU1 | A1 space-switch event identification wrong; calling `OnSpaceSwitched()` from wrong observer leads to double-fire or miss | Medium | Read `maho_sidebar_view.cc` carefully; trace observer chain; add a `DCHECK_CALLED_ON_VALID_SEQUENCE` if uncertain |
| FU2 | A2 Option 1 (drop cross-window image) regresses UX for power users | Low | Native OS drag image is acceptable; can add Option 2 in a later plan if QA flags |
| FU3 | A3 zone-detection extraction breaks production behavior | Low | Refactor first, run full sidebar interactive tests; only then add unit tests |
| FU4 | E-FIX regression test is flaky (timing-dependent) | Medium | Use `base::test::TaskEnvironment` deterministic time; do not use real clocks |
| FU5 | D-NAMING amendment misnames a field | Low | Verify against actual `tab_list_view.h` field names before committing |
| FU6 | META status updates accidentally rewrite plan content | Low | Use targeted `edit` tool calls, append-only |
| FU7 | F-RETRO documentation diverges from code if F implementation changes mid-plan | Low | Lock the plan to current `git diff` HEAD; if code changes, amend plan in same PR |
| FU8 | G-RETRO `FocusCommandSurface` intent unclear; design may have wanted `BeginEditing` retained on right-click | Medium | Flag as Open Question in G-RETRO; require user/design call before merging |

---

## 6. Open Questions — RESOLVED 2026-06-07

All four questions answered by user; plan locked.

| # | Question | Answer |
|---|---|---|
| Q1 (A2) | cross-window drag image strategy | **Option 1 — DROP the feature.** Native OS drag image is acceptable when crossing windows. Remove `BuildCrossWindowDragImage` from factory and replace call sites with `BuildTabStackDragImage`. |
| Q2 (G) | `FocusCommandSurface` vs `BeginEditing` on right-click | **Keep current behavior** (`FocusCommandSurface` opens command palette). Document as intentional UX choice in G-RETRO. |
| Q3 (F) | multi-row UP/DOWN roving focus | **REMOVE.** Verified via Zen source audit (librarian `bg_bcf78bb1`): Zen does NOT have grid-aware keyboard navigation. Firefox upstream `#advanceFocus` is linear `±1` only — UP/DOWN walks `ariaFocusableItems` flatly with no row-jumping. Maho's column-step UP/DOWN is a Maho-only extension that violates strict parity. Strip UP/DOWN handling from `HandleGridKeyFromTile`; keep LEFT/RIGHT/HOME/END/SPACE/RETURN only. |
| Q4 (E) | `TryCancelAll()` restore location | **Restore exactly at original position** (top of `Update()`) to minimize regression risk. |

### Resulting plan amendments

- **A2**: Phase A-COMPLETE A2 commits to Option 1. Estimated effort: **45 min** (was conditional on choice).
- **G**: G-RETRO documents `FocusCommandSurface` as intentional. No code change.
- **F**: Phase F-RETRO is no longer pure retroactive — it now includes a small **F-STRIP** sub-phase:
  - Strip VKEY_UP / VKEY_DOWN cases from `HandleGridKeyFromTile` in `maho_sidebar_favorites_grid_view.cc`.
  - Update or remove the related test cases that assumed multi-row navigation. Audit existing tests in `maho_sidebar_dnd_dispatch_unittest.cc`:
    - `RovingTabStop_ArrowKeyAdvancesFocusToNextTile` — keep (uses VKEY_RIGHT).
    - `RovingTabStop_HomeKeyJumpsToFirstTile` — keep.
    - Any test exercising VKEY_UP/DOWN — delete or rewrite as VKEY_RIGHT/LEFT (verify via grep first).
  - Add 1 line comment in `HandleGridKeyFromTile` referencing this plan and the Zen audit (`bg_bcf78bb1`).
  - **F-RETRO doc** captures: dominant color is full retroactive parity match; LEFT/RIGHT/HOME/END roving is parity match (Zen via Firefox upstream); UP/DOWN was an over-extension and was removed.
  - **Effort**: F-RETRO doc 30 min + F-STRIP code 20 min = **50 min** total.
- **E**: One-line restore at top of `Update()` exactly per `empty-favorites-ui-rework-google-seed-2026-06-05.md` original.

### Updated total effort estimate

- F-RETRO + F-STRIP: 70 min
- G-RETRO: 25 min
- A-COMPLETE A1: 0 min (already closed; verified)
- A-COMPLETE A2 (Option 1 confirmed): 45 min
- A-COMPLETE A3: 0 min (already closed; verified)
- E-FIX: 30 min
- D-NAMING: 10 min
- META: 15 min

**Total: ~3 hours focused work** (A1 and A3 already wired/tested per Momus verification; only A2 remains as DragImageController code change).

---

## 7. Out-of-Scope

- Re-litigating any decision in v2 parity plan.
- Adding visual QA infrastructure (separate concern).
- Performance profiling of the new `last_painted_*` caching (assumed positive impact).
- WebUI tests (sidebar is C++ Views; no WebUI side here).
- macOS-specific drag image rendering bugs (separate plan).
- Cross-platform builds (Linux/Windows verification deferred to standard CI).

---

## 8. References

### Existing covering plans
- `.sisyphus/plans/zen-parity-drag-image-controller-v2-2026-06-07.md` — covers item A
- `.sisyphus/plans/sidebar-pinned-drop-zone-flicker-2026-06-07.md` — covers items B, C, D
- `.sisyphus/plans/empty-favorites-ui-rework-google-seed-2026-06-05.md` — covers item E
- `.sisyphus/plans/favorite-active-glow-and-favicon-source-fix-plan-2026-06-03.md` — adjacent (excludes F)

### Parent parity plan
- `.sisyphus/plans/sidebar-typography-icon-zen-parity-plan-2026-06-07.md`

### Files modified by Phase A-COMPLETE / E-FIX
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_image_controller.cc/h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_image_factory.cc/h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc/h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_image_controller_unittest.cc` (new)
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc`
- `maho-chromium/browser/ui/views/sidebar/BUILD.gn`

### Files created by F-RETRO / G-RETRO
- `.sisyphus/plans/favorites-dominant-color-and-roving-focus-RETROACTIVE-2026-06-07.md`
- `.sisyphus/plans/sidebar-search-pill-compact-variant-removal-RETROACTIVE-2026-06-07.md`

---

## 9. Approval Gate

Before Phase A-COMPLETE execution begins, this plan should be reviewed by Momus with focus on:
- Phase A-COMPLETE option selection (A2 Option 1/2/3)
- E-FIX regression test design
- Whether D-NAMING and META amendments are sufficient or if more documentation is needed

```
task(subagent_type="momus", prompt=".sisyphus/plans/sidebar-scope-creep-followups-and-retroactive-2026-06-07.md", load_skills=[], run_in_background=false)
```

After Momus approval, execute in order: F-RETRO + G-RETRO → E-FIX → D-NAMING → META → A-COMPLETE.
