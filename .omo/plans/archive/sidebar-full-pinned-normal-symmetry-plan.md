# Sidebar Full Pinned/Normal Symmetry Plan

## Goal

Make pinned and normal sidebar presentation for both tabs and folders fully symmetric.

Acceptance standard:

- pinned/normal must use the same actual render components
- pinned/normal must use the same build/render/data-resolution pipeline
- pinned/normal must use the same drag/drop transition pipeline
- pinned/normal may differ only by shared state values (`is_pinned`, `folder_is_pinned`)
- no visible UI difference may arise from section-specific upstream branching

## Current confirmed gaps

### A. Upstream data split

1. `maho_sidebar_state_adapter.cc`
   - `BuildTabListModel()` builds pinned and normal via separate FFI calls:
     - `maho_core_get_pinned_tabs`
     - `maho_core_get_space_tabs`
2. Folder partitioning is also section-built in adapter logic rather than from one unified source graph.

### B. Section layout/UI split

1. `maho_sidebar_tab_list_view.cc`
   - pinned section gets special separator handling
   - pinned section visibility is gated separately
   - empty pinned drop lane is reveal-only, normal has no equivalent mechanism

### C. Drag/drop transition split

1. Section bucket drops route through `ResolveDropPlan()` / `ExecuteDropPlan()`
2. Row-to-row / insertion-lane / folder-row drops bypass the shared planner prelude and dispatch shell events inline
3. Cross-section drag cases can miss `pin_tab` / `unpin_tab` / `pin_folder` / `unpin_folder`
4. Folder-row cross-section drops are a concrete known gap, not just a theoretical parity risk

### D. State encoding split

1. Drag payload carries both:
   - `origin`
   - `source_is_pinned`
2. Planner ORs both instead of using one canonical source of truth

### E. Test gaps

1. No test that visible favicon/icon parity survives pinned vs normal for the same logical tab
2. No strict structural parity test for tab/folder row view trees
3. No full coverage that all drag targets use identical planner semantics
4. No contract test that empty-lane / separator behavior is symmetric by design

## Non-goals

- broad sidebar redesign
- unrelated AI / build branch cleanup
- changing favorites semantics beyond what is required to preserve symmetry

## Locked implementation decisions

These decisions are part of the implementation contract for this pass.

### 1. Scope boundary

This plan covers sidebar tab/folder presentation, model preparation for sidebar rendering, and sidebar drag/drop behavior.

Explicitly out of scope for this pass unless required by the sidebar refactor:

- deeper pinned-tab lifecycle semantics outside sidebar rendering/dnd
- non-sidebar product behavior tied to pinning
- unrelated folder/context-menu bugs unless they block the symmetry refactor

### 2. Canonical architecture target

Pinned and normal must be projections of one prepared node graph.

That means:

- prepare tab nodes once
- prepare folder nodes once
- resolve favicon/tab-strip/live-tab data once
- project into pinned/normal sections only after preparation

The section split is allowed only as a projection/filter step over shared prepared nodes.

### 3. Empty section behavior decision

The end state will not use pinned-only reveal/hide behavior.

Implementation target:

- both sections use the same section container type
- both sections use the same empty-section mechanics
- both sections can expose the same generic empty drop target behavior
- any separator/boundary element must follow a generic section rule, not a pinned-only rule

### 4. Drag/drop unification strategy

All visible drop targets will share a two-stage pipeline:

1. resolve section transition plan from shared payload + target section
2. perform target-specific placement dispatch (`before` / `after` / `append` / `into-folder`) after the section transition prelude is resolved

This means row, insertion lane, folder row, and section target callbacks may still differ in placement details, but pin/unpin/folder-escape semantics must be computed by one shared planner path.

Required structural refactor:

- extract inline row/folder/insertion drop lambda logic into post-planner placement helpers
- run the shared section-transition planner before any target-specific placement dispatch
- keep section transition semantics centralized, with placement details layered after that prelude

### 5. Canonical pin-state input

For planning/execution logic, `source_is_pinned` is the canonical pin-state signal.

`origin` remains only for source-domain semantics such as favorites vs sidebar-section provenance.

Implementation rule:

- planner must not infer pin-state from `origin` once all callers provide `source_is_pinned`
- add assertions while migrating to catch disagreement between the two signals when both are present

### 6. Test-first lock points

Before or alongside refactor work, add failing tests for these exact invariants:

- pinned vs normal tab rows have identical child hierarchy and icon slot behavior
- pinned vs normal folder rows have identical child hierarchy
- equivalent cross-section drops produce identical planner transitions regardless of visible target type
- empty section structure is generic rather than pinned-special-cased
- same logical tab rendered through pinned vs normal receives the same favicon outcome

## Required end state

### 1. Single source model path

Refactor sidebar state production so pinned/normal views are derived from one shared tab/folder model pipeline, then partitioned only by `is_pinned` / `folder_is_pinned` state.

Target properties:

- one shared node construction path for tabs
- one shared node construction path for folders
- no pinned-only favicon resolution path
- no pinned-only folder render preparation path

### 2. Single section render path

Keep `SidebarTabRowView` and `SidebarFolderRowView` shared, and remove all section-specific visible behavior from the render/build layer except behavior that is expressed as a generic state-driven rule applicable to either section.

Target properties:

- no pinned-only separator rule unless the same rule is genericized
- no pinned-only reveal/hide lifecycle in the container layer
- no section-specific row layout differences

### 3. Single DnD semantics path

All drag destinations must resolve transitions through the same planner path.

Target properties:

- section target drops
- row-to-row drops
- insertion-lane drops
- folder-row drops

must all end in the same plan execution machinery, with target section and source pin-state handled once.

### 4. Single pin-state source of truth

Choose one canonical pin-state signal in drag payload / planner inputs and remove redundant derivation.

### 5. Symmetry-first tests

Add tests that fail on any future pinned/normal divergence in:

- favicon/icon visibility
- row hierarchy structure
- folder row structure
- drag transition semantics
- empty section behavior

## Execution plan

### Phase 0 — Freeze acceptance contract

Files:

- `.sisyphus/plans/sidebar-full-pinned-normal-symmetry-plan.md`

Actions:

1. Treat any section-specific visible behavior as a bug unless it can be expressed as generic state-driven behavior reusable by both sections.
2. Treat any separate pinned-vs-normal upstream fetch/transform branch as unfinished.
3. Treat tabs and folders identically under the same symmetry contract.

Exit criteria:

- agreement that the plan goal is full symmetry, not “shared row class only”

### Phase 1 — Unify upstream state production

Primary files:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_models.h`
- related Rust/FFI surface if required by current adapter contract

Actions:

1. Treat the current separate FFI fetches as fixed inputs for the first pass, and introduce a shared post-fetch preparation/enrichment layer as the primary implementation path.
2. Prepare tab nodes once after fetch, stamp shared state once, then partition only at the final section projection boundary.
3. Do the same for folders so folder nodes are produced once and projected by `folder_is_pinned`.
4. Ensure favicon/tab-strip/live-tab resolution runs in the shared preparation layer before section projection, so pinned and normal consume identical prepared node data.
5. Remove any accidental dependence on build order between pinned and normal sections.
6. Escalate to Rust/FFI contract changes only if local shared preparation cannot satisfy the acceptance criteria.

Parallelizable sub-work:

- 1A: build shared post-fetch tab preparation layer
- 1B: build shared post-fetch folder preparation layer
- 1C: move favicon/tab-strip/live-tab enrichment ahead of projection

Exit criteria:

- pinned/normal nodes come from the same preparation path
- section split happens only by shared state fields
- favicon/live-tab enrichment is completed before section projection

### Phase 2 — Unify section/container rendering behavior

Primary files:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h`

Actions:

1. Remove pinned-only container lifecycle behavior (`pinned_drop_lane_revealed_`, pinned-only visibility toggles, pinned-only separator handling) or convert it into a generic section behavior available to either section through the same API.
2. Ensure `RebuildRows()` and `RebuildTreeSection()` do not apply visible behavior based on `kPinned` vs `kNormal` other than consuming projected nodes.
3. Keep row classes shared and ensure `UpdateAppearance()` stays section-agnostic for both tab and folder rows.
4. Normalize empty-section rendering so the section container contract is identical for pinned and normal.

Parallelizable sub-work:

- 2A: section container / separator / empty-lane normalization
- 2B: tab row visual parity assertions
- 2C: folder row visual parity assertions

Exit criteria:

- no pinned-only visible container behavior remains
- same hierarchy shape rules apply to both sections
- tab and folder `UpdateAppearance()` logic remains section-agnostic

### Phase 3 — Route every drop path through one planner

Primary files:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drop_planner.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drop_planner.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_section_policy.cc`

Actions:

1. Extract row-to-row drop lambda placement logic into post-planner placement helpers.
2. Extract insertion-lane drop lambda placement logic into post-planner placement helpers.
3. Extract folder-row drop lambda placement logic into post-planner placement helpers.
4. Make row-to-row drops use the same planner path as section-target drops.
5. Make insertion-lane drops use the same planner path as section-target drops.
6. Make folder-row drops use the same planner path as section-target drops.
7. Ensure all cross-section transitions produce the same pin/unpin behavior no matter which visible target receives the drop.
8. Explicitly cover the known folder-row cross-section pin/unpin gap.
9. Preserve no-op semantics for same-section root drops.
10. Keep target-specific placement dispatch separate from section transition planning, but require every path to execute the same section-transition prelude first.

Parallelizable sub-work:

- 3A: tab row drop planner routing
- 3B: insertion lane planner routing
- 3C: folder row planner routing

Exit criteria:

- every visible drop path resolves through the same planning/execution code
- no target-specific pin/unpin drift remains
- row/insertion/folder/section targets agree on equivalent cross-section outcomes

### Phase 4 — Collapse redundant pin-state signaling

Primary files:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_models.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drop_planner.cc`

Actions:

1. Pick a single canonical pin-state input for drag planning.
2. Remove or demote redundant fields once all callers use the same contract.
3. Add defensive assertions that source pin-state and target section stay coherent.
4. Preserve `origin` only for non-pin provenance distinctions such as favorites.

Exit criteria:

- planner does not rely on dual source pin-state encodings

### Phase 5 — Lock with tests before/alongside refactor

Primary files:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_contract_test.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_planner_unittest.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_interactive_test.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_browser_test.cc`

Required tests:

1. Tab row structural parity test
   - same logical node rendered as pinned vs normal produces same row hierarchy / child classes / icon slot behavior
2. Folder row structural parity test
   - same logical folder node rendered as pinned vs normal produces same hierarchy
3. Favicon parity test
   - same tab data rendered in pinned vs normal yields same favicon visibility/result
4. DnD parity tests
   - row drop, insertion lane drop, folder drop, section drop all produce identical planner transitions for equivalent source/target states
   - explicitly include folder-row cross-section pin/unpin cases
5. Empty-section contract test
   - section empty-state behavior is symmetric by construction
6. Planner migration assertion tests
   - planner reads canonical `source_is_pinned` and ignores `origin` for pin-state after migration

Exit criteria:

- a future pinned-only visual/data path regression fails tests immediately

### Phase 6 — Verification and proof

Verification steps:

1. changed-file diagnostics clean where meaningful
2. relevant sidebar unit tests pass
3. relevant interactive/browser tests pass or fail only for pre-existing unrelated reasons
4. canonical build re-run once unrelated branch blockers are isolated
5. final code review pass confirms:
   - same components
   - same upstream path
   - same planner path
   - only state values differ

## Suggested implementation order

1. write failing parity tests for row structure, favicon parity, and DnD target equivalence
2. unify upstream node preparation so favicon/live-tab enrichment happens before projection
3. collapse canonical pin-state handling in payload/planner callers
4. route row/insertion/folder drops through the shared section-transition planner
5. normalize section container and empty-section behavior
6. run full sidebar verification

## Risks

1. Rust/FFI contract changes may be needed for true single-source state production
2. empty pinned lane behavior may currently hide assumptions in contract tests
3. stale build blockers outside sidebar can mask actual verification results
4. unifying drop semantics can accidentally change placement behavior if section transition and placement dispatch are not separated cleanly
5. superficial symmetry can regress later unless structural parity tests assert hierarchy, not just flags
6. row/folder/insertion callbacks currently bypass the planner entirely, so Phase 3 is a decomposition refactor, not a small wiring tweak

## Done definition

The work is done only when all of the following are true:

1. pinned and normal tabs are rendered from the same prepared node path
2. pinned and normal folders are rendered from the same prepared node path
3. every visible drop path shares the same planner/executor semantics
4. no pinned-only visible container/render behavior remains
5. tests explicitly prove pinned/normal parity for tabs and folders
6. the only remaining distinction is shared model state (`is_pinned`, `folder_is_pinned`)
