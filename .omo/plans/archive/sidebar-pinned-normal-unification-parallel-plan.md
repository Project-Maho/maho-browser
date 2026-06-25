# Sidebar Pinned/Normal Unification Parallel Execution Plan

## Goal
Refactor the native sidebar so pinned and normal tabs and folders use one shared implementation path, with only section policy differences remaining. Remove accidental divergence while preserving behavior.

## Problem Statement
The current implementation shares some row and view code, but it still splits pinned and normal behavior across model containers, adapter inputs, section targets, reveal behavior, drag origin encoding, and drop dispatch. That split makes consistency drift likely and inflates the DnD test matrix.

## Non-Goals
* No redesign of sidebar UX beyond consistency fixes.
* No Rust or FFI contract rewrite in the first pass unless C++ canonicalization proves insufficient.
* No favorites redesign beyond the policy integration needed for unified drop planning.

## Success Criteria
* Pinned and normal tabs and folders render through the same row and build path.
* DnD structural decisions come from one planner.
* Section-specific behavior is isolated to policy, including pin and unpin, lane reveal, and section placement.
* No duplicate pinned and normal flat fallback builders remain.
* Existing sidebar behavior remains covered by unit, browser, and interactive tests.
* New matrix gaps are covered: normal to favorite, pinned to favorite, favorite to normal and pinned final state, cross-folder child tab final state, folder-child to pinned final state, and nested folder final-order verification.
* No measurable sidebar rebuild latency regression from the canonical model and projection layer. This is a sanity check, not a full perf project.

## Known Current Issues To Address During Refactor
* `GroupSplitTabs` is invoked twice in the adapter path.
* Flat fallback code duplicates pinned and normal row building.
* `SidebarFolderRowView` carries section semantics inconsistently relative to `folder_is_pinned`.
* Empty pinned lane logic is pinned-only and should stay policy, not structural duplication.
* Red and green test execution is currently blocked by unrelated branch compile errors outside sidebar scope. Sidebar work must not hide that fact.

## Execution Model
The plan uses serial gates to freeze contracts, then parallel workstreams only after interfaces are real, compilable artifacts.

## Global Constraints
* Each workstream must land as an independently revertable change set or equivalent isolated patch.
* No workstream may depend on another stream being irreversible before Gate 2 integration passes.
* Do not mix unrelated cleanup into Gate 0 or the early test-freeze gates.
* No touched test may rely on soft skip or fake coverage patterns.

## Rollback and Isolation Constraints
* Every workstream must be reversible without blocking the others.
* If one stream fails late, the earlier streams must remain shippable or revertable on their own.
* Interface Freeze artifacts must not require a broad rewrite to back out.
* Planner and policy extraction must stay in dedicated files so Workstream B can be reverted independently of Workstream C.
* Render unification must stay in the tab list view files so Workstream C can be reverted without deleting planner and policy code.

## Gate 0, Restore Runnable Test Environment and Fixture Readiness
Objective: Make sidebar test binaries runnable and confirm the current fixtures can express the required red tests.

Scope:
* Fix unrelated branch compile blockers that currently prevent `maho_sidebar_dnd_dispatch_unittests`, `maho_sidebar_dnd_browser_tests`, and `interactive_ui_tests` from building and running.
* Verify the existing sidebar test helpers and fixtures can express:
  * favorites-grid drops
  * cross-folder state assertions
  * folder-child to pinned assertions
  * nested-folder final-order verification
* If a small amount of test-helper prep is needed to express those tests, do it here without changing production behavior.

Exit criteria:
* Dedicated sidebar unit and browser binaries build.
* `interactive_ui_tests` builds with sidebar filters usable.
* Fixture readiness is explicitly verified so the red tests are known to be expressible before Gate 1 depends on them.
* Any remaining non-sidebar breakage is documented explicitly if not fixed.

## Gate 1, Freeze Behavior and Add Red Tests
Objective: Lock target behavior before implementation changes.

Scope:
* Add missing dispatch tests in `maho_sidebar_dnd_dispatch_unittest.cc`.
* Add missing final-state tests in `maho_sidebar_contract_test.cc` and `maho_sidebar_dnd_browser_test.cc`.
* Remove fake coverage and soft skip patterns in touched sidebar test files.

Required red tests:
* `NormalTabDropOnFavoritesGridDispatchesFavoriteTab`
* `PinnedTabDropOnFavoritesGridDispatchesUnpinThenFavoriteTab`
* Strengthen favorite to normal dispatch to assert exactly one event and no spurious `unpin_tab`.
* `FolderChildTabDropOnDifferentFolderMiddleZoneDispatchesMoveTabToFolder`
* `FavoriteDropToNormalRemovesItemFromFavoritesInViewState`
* `FavoriteDropToPinnedRemovesFromFavoritesAndAppearsInPinnedTree`
* `FolderChildTabDropToOtherFolderAppearsInTargetFolderInViewState`
* `FolderChildTabDropToPinnedRemovedFromFolderAndAppearsInPinnedTree`
* `LiveNestedFolderReorderFinalOrderVerified`

Exit criteria:
* New tests compile.
* At least the known behavior gaps fail red for the right reason.
* No newly added test uses soft skip or fake coverage.

## Interface Freeze, Short Serial Design Checkpoint
Objective: Freeze the minimal interfaces that let streams run independently.

Required artifacts:
1. Canonical sidebar tree and view-model source declaration.
2. `SectionKind` and section policy boundary declaration.
3. Unified `SidebarDragPayload` signature with `source_is_pinned`.
4. `DropPlan` declaration split into:
   * structural operation
   * pin transition
   * favorite transition
5. Projection helper declarations for pinned and normal views over a canonical source.
6. Planner and policy extraction declared in dedicated files, for example `maho_sidebar_drop_planner.h/.cc` and `maho_sidebar_section_policy.h/.cc`.

Exit criteria:
* Headers and signatures for the frozen interfaces exist.
* Stub implementations compile, even if behavior is not complete yet.
* The freeze produces compilable headers, stubs, and signatures, not just conceptual agreement.
* Ownership boundaries are explicit: planner and policy files belong to Workstream B, while tab list render files belong to Workstream C.
* Each downstream stream knows which files and interfaces it owns.

## Parallel Workstream A, Canonical Model and Adapter Canonicalization
Primary files:
* `maho_sidebar_state_models.h`
* `maho_sidebar_state_adapter.cc`

Goal:
Introduce a canonical root sidebar model and projection helpers while preserving current external behavior.

Tasks:
* Add a canonical root tree and model representation.
* Build pinned and normal projections from that canonical source for transition compatibility.
* Remove duplicate `GroupSplitTabs` invocation.
* Add invariants ensuring section flags match node pin state.

Constraints:
* Keep existing downstream consumers compiling via transitional adapters and projections.
* Do not remove old fields until Gate 2 integration proves consumers are migrated.

Exit criteria:
* The adapter produces one canonical source of truth plus compatibility projections.
* Existing tests for state shape still pass or are updated intentionally.

## Parallel Workstream B, Drop Planner and Section Policy Extraction
Primary files:
* New dedicated planner and policy files created during Interface Freeze, such as:
  * `maho_sidebar_drop_planner.h/.cc`
  * `maho_sidebar_section_policy.h/.cc`
* Minimal integration touchpoints in `maho_sidebar_tab_list_view.cc/.h` only where required to wire the new interfaces.

Goal:
Move structural DnD logic into one planner and isolate section differences to policy.

Tasks:
* Introduce `DropPlan`.
* Separate structural operations from pin and favorite transitions.
* Implement `ResolveDropPlan(...)`.
* Implement planner execution helpers and policy mapping.
* Normalize favorites interactions into the same transition model.

Constraints:
* Workstream B owns planner and policy files, and it must not become a second owner of the whole tab list render path.
* Do not change visible behavior beyond fixing known gaps caught by tests.
* Keep current test hooks usable during migration.

Exit criteria:
* One planner decides move and reorder semantics.
* Section-specific differences live in policy mapping only.
* Known favorites and pinned transition bugs are resolved through planner and policy, not ad hoc conditionals.

## Parallel Workstream C, View and Render Path Unification
Primary files:
* `maho_sidebar_tab_list_view.cc`
* `maho_sidebar_tab_list_view.h`

Hard dependency:
* Workstream C starts only after Workstream A has landed the canonical projections and interfaces it depends on.

Goal:
Collapse duplicated pinned and normal render and build logic into shared helpers.

Tasks:
* Replace duplicated flat fallback loops with one helper.
* Make `RebuildRows` and `RebuildTreeSection` flow use shared per-section helpers over the canonical projections.
* Add DCHECKs that section placement matches node pin state for tabs and folders.
* Clean up dead or misleading section-specific parameters if no longer needed.

Constraints:
* Preserve row ordering: pinned section, separator, new tab button, normal section.
* Preserve pinned empty-lane reveal UX as a policy feature.
* Workstream C owns render-path cleanup, and it should consume planner and policy interfaces rather than redefine them.

Exit criteria:
* No duplicated pinned versus normal flat row builder remains.
* Row construction is shared and section differences are parameterized.

## Parallel Workstream D, Test and Verification Hardening
Primary files:
* Sidebar DnD, unit, browser, and contract tests
* Any touched interactive tests

Goal:
Keep the refactor honest and reduce future drift.

Tasks:
* Expand planner and policy tests once new abstractions exist.
* Add explicit invariants for pinned and normal projection correctness.
* Strengthen any ambiguous nested-folder and cross-folder assertions touched by the refactor.
* Ensure no touched test relies on soft skip behavior.

Constraints:
* This stream begins in Gate 1 and continues in parallel once interfaces freeze.
* Do not rewrite unrelated sidebar tests.

Exit criteria:
* Test coverage cleanly splits into structural planner tests and section policy tests.
* Final-state tests cover all transitions the planner and policy claim to support.

## Gate 2, Integration
Objective: Connect adapter canonicalization, planner and policy, and the shared render path.

Tasks:
* Wire canonical model projections into the tab list view.
* Switch drag payload usage to unified source and pin-state semantics.
* Route all drop-capable surfaces through shared planner execution.
* Remove obsolete branches made redundant by the new planner and policy structure.

Exit criteria:
* Tests from Gate 1 and Workstream D pass against the integrated code.
* No duplicate legacy path remains on the hot path.
* The integration patch remains independently revertable if a later gate fails.

## Gate 3, Cleanup and Removal
Objective: Remove transitional scaffolding and dead paths.

Tasks:
* Remove legacy pinned and normal compatibility fields once consumers are migrated.
* Delete dead enums or branches such as unused drop-zone states if they are still unused.
* Remove obsolete constructor parameters or add DCHECK-backed invariants where parameters remain.
* Simplify tests that were only needed during migration.

Exit criteria:
* No stale compatibility path remains without an active consumer.
* Final code reflects one implementation path plus policy.

## Gate 4, Validation and Review
Objective: Prove the refactor is correct and regression-safe.

Validation steps:
* Build and run dedicated sidebar unit tests.
* Build and run dedicated sidebar browser tests.
* Build and run filtered `interactive_ui_tests` for sidebar suites.
* Re-run relevant Rust and FFI-facing checks if any adapter contract changed across the boundary.
* Perform a sidebar rebuild-latency and rebuild-cost sanity check, for example compare rebuild or update timing before and after on representative seeded data, or add a focused debug timing harness if that is the least invasive option.
* Request Oracle review on the final implementation.

Evidence required:
* Sidebar unit tests are green.
* Sidebar browser and interactive tests are green.
* No new diagnostics appear in touched files beyond the known environment baseline.
* No meaningful rebuild-latency or rebuild-cost regression comes from the new canonical and projection layer.
* Oracle review finds no remaining architectural or test-gap issue in the changed scope.

## Dependency Graph
* Gate 0 -> Gate 1 -> Interface Freeze.
* Interface Freeze unlocks A, B, and D.
* A -> C is a hard dependency.
* B runs in parallel with A after Interface Freeze because planner and policy ownership is isolated to dedicated files.
* D starts in Gate 1 and continues in parallel with A, B, and C.
* Gate 2 depends on A + B + C.
* Gate 3 depends on Gate 2 stabilization.
* Gate 4 depends on Gate 3.

## Recommended Merge Order
1. Gate 0 fixes.
2. Gate 1 red tests.
3. Interface Freeze artifacts, headers and stubs compiling.
4. Workstream A minimal canonical scaffolding.
5. Workstream B planner and policy scaffolding in dedicated files.
6. Workstream C render unification on top of A outputs and B interfaces.
7. Gate 2 integration.
8. Workstream D final hardening.
9. Gate 3 cleanup.
10. Gate 4 validation and Oracle review.

## Risks and Mitigations
* Risk: adapter change breaks too many consumers.
  * Mitigation: compatibility projections remain until integration is complete.
* Risk: planner abstraction becomes too broad and hides behavior differences.
  * Mitigation: explicit `DropPlan` fields for structural versus policy transitions.
* Risk: build and test environment stays blocked by unrelated branch issues.
  * Mitigation: Gate 0 is explicit and cannot be skipped silently.
* Risk: flat fallback and tree path drift during migration.
  * Mitigation: unify through helpers before deleting compatibility fields.
* Risk: favorites semantics become second-class during pinned and normal unification.
  * Mitigation: include favorite transitions in planner design from day one.
* Risk: B and C churn from shared-file edits.
  * Mitigation: planner and policy extraction to dedicated files during Interface Freeze, plus strict file ownership boundaries.
* Risk: integration reveals the planner or model design was flawed.
  * Mitigation: each stream lands as independently revertable patches until Gate 2 passes.

## Deliverables
* Reviewed plan markdown in `.sisyphus/plans/`.
* Updated sidebar tests covering the missing DnD matrix.
* Unified sidebar implementation with policy-isolated pinned and normal behavior.
* Validation evidence and Oracle review at completion.
