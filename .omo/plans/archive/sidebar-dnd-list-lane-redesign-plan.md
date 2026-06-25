# Sidebar Drag and Drop List Lane Redesign Plan

**Status:** Approved implementation plan

**Goal:** Redesign the sidebar drag and drop model so the sidebar behaves like a true ordered list with lane-aware drop behavior, while preserving the finalized UX rules and making the implementation easy to split across multiple engineers.

## Finalized UX Spec

The end state is fixed and must not drift.

1. Default before and after lanes are reorder only.
2. Tab on tab row body means split view.
3. Tab on folder row body means move into folder.
4. Folder on folder row body means move into folder.
5. Folder on tab row body is invalid and must be a no op.

### Oracle corrections that are part of the approved spec

1. Folder row tab payloads must branch by lane versus body, not always move into folder.
2. Tab row lanes must accept folder payloads, while tab row body rejects folder payloads.
3. The current backend primitives cannot express mixed root ordering and need a unified insertion contract.
4. Last item after lane append semantics must be designed and tested, not assumed.

### UX interpretation notes

The redesign is not a return to the old row hover model as an acceptable final state. The behavior target is a lane aware list with clear insertion semantics, body semantics, and explicit no op handling for invalid combinations.

## Current Architecture Diagnosis

The current stack has a structural mismatch between view intent and backend shape.

### View layer

`maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` currently owns the tab row rendering and drag and drop affordances. The view can describe row body versus lane drops, but it depends on a model shape that is already split by type.

`maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` translates sidebar state from core into Chromium view data. The adapter currently emits root folders first and root tabs later, which means the UI receives two groups instead of one mixed ordered list.

### Core layer

`maho/crates/maho-core/src/space_manager.rs` and `maho/crates/maho-core/src/event_dispatcher.rs` are the key backend surfaces for ordering and event propagation. Today they do not expose a unified insertion contract that can represent a single ordered root sequence containing both tabs and folders with stable insertion points.

### Diagnosis summary

The present architecture can describe items, but it cannot yet express the exact ordering contract that the redesigned list needs. That is the critical blocker for a correct implementation. Any view layer refactor that assumes a true interleaved root list before backend support lands will be incomplete or wrong.

## Critical Blocker

### Mixed root ordering

The state adapter emits root folders first and root tabs later. The desired UX depends on a true simple list where tabs and folders can be interleaved at the root level. Without backend and model support for mixed root ordering, drop math, insertion placement, and last item after lane append behavior cannot be implemented safely.

### Required backend outcome

Introduce a unified insertion contract in core and propagate it through the event path so the Chromium adapter can represent a single ordered root list rather than two separate root groups.

## Workstreams

The workstreams are ordered by dependency. Items marked parallel can proceed together once their gate is open.

### Workstream 1. Backend ordering contract

**Dependency:** none, this is the primary gate

**Purpose:** Define and implement the unified insertion contract needed to represent mixed root ordering.

**Why it blocks others:** All row body versus lane decisions depend on a backend model that can store a single interleaved order and can tell the UI where a move lands.

**Concrete file targets**

- `maho/crates/maho-core/src/space_manager.rs`
- `maho/crates/maho-core/src/event_dispatcher.rs`
- `maho/crates/maho-types/src/events/shell_event.rs`
- Related core tests under `maho/crates/maho-core/tests/` or adjacent module tests, depending on where ordering logic already lives

**Implementation focus**

- Add a unified insertion contract for root items.
- Make root ordering stable across tabs and folders.
- Encode enough insertion metadata for after lane and body based moves.
- Define behavior for appending after the last item in a lane.

**Starting implementation guidance**

- In `space_manager.rs`, the current root ordering is split across `space.tab_order` for tabs and `space.folders` for folders. Start by identifying the smallest representation that can preserve a single root-level order across both item kinds.
- In `event_dispatcher.rs` and `shell_event.rs`, the current API is `reorder_tab(before_tab_id)`, `move_tab_to_root(before_tab_id)`, `reorder_folder(parent_folder_id, before_folder_id)`, and `move_folder_to_root(before_folder_id)`. These are insufficient for mixed root ordering because they can only reference a sibling of the same kind. Add a new insertion contract that can target any root item or an append position.
- The new contract must be able to express at least these three cases explicitly: insert before a root tab, insert before a root folder, append after the last visible root item.
- Keep nested folder semantics separate from root mixed ordering. The first milestone is correctness at the root list; nested folder reorder may continue to use the existing same-parent folder primitives until the root contract is stable.

**Executable QA scenario**

- Tool / command:
  - `cd /Users/indo/code/project/maho-workspace/maho && bun run test`
- Focused steps:
  1. Add or update core tests that cover mixed root ordering and append-after-last behavior.
  2. Run the full Rust workspace test command above.
  3. If runtime is too high during iteration, run the narrowest cargo test command for the touched module first, then rerun `bun run test` before handoff.
- Expected result:
  - New or updated core ordering tests pass.
  - No regression in existing space/folder/tab ordering tests.
  - The resulting contract can represent tab-before-folder, folder-before-tab, and append-after-last without ambiguity.

**Gate to unlock**

This workstream must be complete before the adapter can be made correct and before any view logic that depends on interleaved root items is finalized.

### Workstream 2. State adapter shape and mapping

**Dependency:** Workstream 1

**Purpose:** Update the adapter so Chromium receives a single ordered sidebar model that matches the backend contract.

**Concrete file targets**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`
- Adapter specific tests adjacent to that file, or `maho-chromium/browser/ui/views/sidebar/*test*` if that is where state adapter coverage already lives

**Implementation focus**

- Replace the folder first, tabs later assumption.
- Preserve mixed root order from core to UI.
- Ensure the adapter can identify row body versus lane targets using the unified model.
- Surface last item lane append behavior clearly enough for the view layer to render it consistently.

**Why this can run after the backend gate**

The adapter is the bridge between the new core ordering contract and the view layer. It cannot be correct until the root list shape exists in core.

**Executable QA scenario**

- Tool / command:
  - `PATH="/Users/indo/code/project/maho-workspace/chromium/src/third_party/depot_tools:$PATH" autoninja -C out/Default interactive_ui_tests`
  - `./out/Default/interactive_ui_tests --gtest_filter='MahoSidebarContractTest.*' --gtest_brief=1`
- Focused steps:
  1. Build the sidebar test target after adapter changes.
  2. Run the contract-focused interactive tests.
  3. Inspect any test fixtures or support-page JSON assertions that still assume folders-first, tabs-later root output.
- Expected result:
  - Contract tests pass without restoring the old folders-first adapter shape.
  - Mixed root ordering is visible in the emitted tree/contract model where expected.

### Workstream 3. Tab list view drag and drop semantics

**Dependency:** Workstreams 1 and 2

**Purpose:** Implement the finalized body and lane behavior in the tab list view.

**Concrete file targets**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- Existing or new tab list view tests, likely nearby sidebar view tests under `maho-chromium/browser/ui/views/sidebar/`

**Implementation focus**

- Use body versus lane as the first branch in drag target handling.
- Enforce the finalized payload matrix.
- Make invalid combinations a no op.
- Support after lane insertion, including the last item append case.
- Preserve split view behavior only for tab on tab row body.

**Required decision matrix in code**

| Drop target | Payload | Result |
|---|---|---|
| Tab row body | Tab | Split view |
| Tab row body | Folder | Reject, no op |
| Tab row lane | Tab | Reorder or lane insert as defined by the list contract |
| Tab row lane | Folder | Reorder or lane insert as defined by the list contract |
| Folder row body | Tab | Move into folder |
| Folder row body | Folder | Move into folder |
| Folder row lane | Tab | Reorder or lane insert as defined by the list contract |
| Folder row lane | Folder | Reorder or lane insert as defined by the list contract |

**Executable QA scenario**

- Tool / command:
  - `PATH="/Users/indo/code/project/maho-workspace/chromium/src/third_party/depot_tools:$PATH" autoninja -C out/Default interactive_ui_tests`
  - `./out/Default/interactive_ui_tests --gtest_filter='MahoSidebarDnDInteractiveTest.DragReordersRegularTabs:MahoSidebarDnDInteractiveTest.DragTabIntoFolder:MahoSidebarDnDInteractiveTest.DragTabToPinnedSection:MahoSidebarDnDInteractiveTest.DragUnpinTab' --gtest_brief=1`
- Focused steps:
  1. Rebuild the target after tab-row DnD changes.
  2. Run the focused interactive tests covering tab reorder, tab into folder, and pin/unpin.
  3. If split-view body handling changes, add a focused body-on-tab scenario and rerun the same target.
- Expected result:
  - Before/after lanes reorder rather than acting like generic row-hover zones.
  - Tab row body triggers split only for tab payloads.
  - Folder payload on tab row body is rejected and does not degrade into reorder or folder movement.

### Workstream 4. Folder row behavior and payload branching

**Dependency:** Workstreams 1 and 2, plus alignment with Workstream 3

**Purpose:** Ensure folder row handling branches correctly by lane versus body and does not collapse all tab payloads into folder moves.

**Concrete file targets**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- Any folder row helper or delegate file used by the list view, if the implementation is split out
- Folder row focused tests in the sidebar view test area

**Implementation focus**

- Branch tab payloads by lane versus body.
- Keep folder on folder body as move into folder.
- Keep folder on tab body invalid and rejected.
- Verify that body targets and lane targets produce different outcomes.

**Why this is separate from Workstream 3**

The Oracle correction makes this a distinct risk area. It deserves its own implementation and test ownership so the branching logic does not get flattened into a generic move handler.

**Executable QA scenario**

- Tool / command:
  - `PATH="/Users/indo/code/project/maho-workspace/chromium/src/third_party/depot_tools:$PATH" autoninja -C out/Default maho_sidebar_dnd_dispatch_unittest`
  - `./out/Default/maho_sidebar_dnd_dispatch_unittest --gtest_filter='SidebarDnDDispatchTest.*Folder*' --gtest_brief=1`
- Focused steps:
  1. Rebuild the unit test target after folder-row branching changes.
  2. Run the folder-focused dispatch tests.
  3. Add explicit lane-vs-body cases for tab payloads on folder rows and rerun the same target.
- Expected result:
  - Folder row body + tab moves into folder.
  - Folder row before/after lanes with tab payloads do not always collapse into move-into-folder.
  - Folder row before/after lanes with folder payloads reorder correctly.

### Workstream 5. Test and validation coverage

**Dependency:** Workstreams 1 through 4, but test scaffolding can start earlier

**Purpose:** Build coverage around the corrected behavior, especially the mixed root ordering blocker and the last item after lane append semantics.

**Concrete file targets**

- `maho/crates/maho-core/src/space_manager.rs` tests or adjacent core test modules
- `maho/crates/maho-core/src/event_dispatcher.rs` tests or adjacent event tests
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` tests
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` tests

**Implementation focus**

- Add tests for mixed root ordering preservation.
- Add tests for lane versus body routing.
- Add tests for invalid folder on tab body behavior.
- Add tests for last item after lane append semantics.
- Add tests that prove the old folder first, tabs later adapter shape is gone.

**Executable QA scenario**

- Tool / command:
  - `cd /Users/indo/code/project/maho-workspace/maho && bun run typecheck && bun run test`
  - `PATH="/Users/indo/code/project/maho-workspace/chromium/src/third_party/depot_tools:$PATH" autoninja -C out/Default maho_sidebar_dnd_dispatch_unittest maho_sidebar_dnd_browser_tests interactive_ui_tests`
  - `./out/Default/maho_sidebar_dnd_dispatch_unittest --gtest_brief=1`
  - `./out/Default/maho_sidebar_dnd_browser_tests --gtest_filter='MahoSidebarDnDBrowserTest.*' --gtest_brief=1`
  - `./out/Default/interactive_ui_tests --gtest_filter='MahoSidebarContractTest.*:MahoSidebarDnDInteractiveTest.*' --gtest_brief=1`
- Focused steps:
  1. Run Rust typecheck/test after backend changes are integrated.
  2. Build all relevant Chromium sidebar test targets.
  3. Run unit dispatch, browser-level, and interactive/contract suites.
  4. Confirm the new matrix is covered, especially invalid folder-on-tab-body and last-item append semantics.
- Expected result:
  - All touched Rust and Chromium sidebar suites pass.
  - No remaining tests encode the old row-hover semantics as the accepted end state.
  - The final matrix is executable and reproducible by another engineer.

## Parallel Execution Map

This map shows what can run concurrently and what is blocked.

### Phase A, blocked by backend and model changes

| Workstream | Can run now | Blocked by |
|---|---|---|
| 1. Backend ordering contract | Yes | None |
| 2. State adapter shape and mapping | No | Workstream 1 |
| 3. Tab list view drag and drop semantics | No | Workstreams 1 and 2 |
| 4. Folder row behavior and payload branching | No | Workstreams 1 and 2, plus the shared drag and drop contract |
| 5. Test and validation coverage, backend tests | Partially | Core shape for the ordering contract |

### Phase B, once backend ordering contract lands

| Workstream | Can run concurrently with | Blocked by |
|---|---|---|
| 2. State adapter shape and mapping | 5, backend focused tests | Workstream 1 only |
| 5. Backend focused tests | 2 | None after Workstream 1 exists |

### Phase C, once adapter shape is updated

| Workstream | Can run concurrently with | Blocked by |
|---|---|---|
| 3. Tab list view drag and drop semantics | 4 and 5, as long as test ownership is split cleanly | Workstreams 1 and 2 |
| 4. Folder row behavior and payload branching | 3 and 5 | Workstreams 1 and 2 |
| 5. UI tests for drag and drop | 3 and 4 | Workstreams 1 and 2 |

### Practical handoff rule

If a task needs to decide where a drop lands in a mixed root list, it is blocked until the backend ordering contract exists. If a task only adds assertions, fixtures, or coverage around the contract, it can start earlier as long as it does not lock in the old split list shape.

## Concrete File Targets by Engineer

### Engineer A, backend contract

- `maho/crates/maho-core/src/space_manager.rs`
- `maho/crates/maho-core/src/event_dispatcher.rs`
- Core tests near those modules

### Engineer B, adapter

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`
- Adapter tests near the sidebar view layer

### Engineer C, tab list drag and drop

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- Sidebar drag and drop view tests

### Engineer D, folder row branching and invalid cases

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- Any folder row helper split out of the list view
- Folder focused sidebar tests

### Engineer E, validation and coverage

- Core ordering tests
- Adapter mapping tests
- View level drag and drop tests
- Any new fixtures needed to reproduce last item after lane append behavior

## Workstream QA Checklist

Each workstream is not complete until its executable QA scenario has run and matched the expected result.

| Workstream | Verification command(s) | Expected proof |
|---|---|---|
| 1. Backend ordering contract | `cd /Users/indo/code/project/maho-workspace/maho && bun run test` | Mixed root ordering and append semantics are representable and tested in Rust |
| 2. State adapter shape and mapping | `PATH="/Users/indo/code/project/maho-workspace/chromium/src/third_party/depot_tools:$PATH" autoninja -C out/Default interactive_ui_tests` then `./out/Default/interactive_ui_tests --gtest_filter='MahoSidebarContractTest.*' --gtest_brief=1` | Contract output no longer hardcodes folders-first then tabs |
| 3. Tab list view drag and drop semantics | `./out/Default/interactive_ui_tests --gtest_filter='MahoSidebarDnDInteractiveTest.DragReordersRegularTabs:MahoSidebarDnDInteractiveTest.DragTabIntoFolder:MahoSidebarDnDInteractiveTest.DragTabToPinnedSection:MahoSidebarDnDInteractiveTest.DragUnpinTab' --gtest_brief=1` | Tab lanes reorder, tab body splits, folder-on-tab-body rejects |
| 4. Folder row behavior and payload branching | `./out/Default/maho_sidebar_dnd_dispatch_unittest --gtest_filter='SidebarDnDDispatchTest.*Folder*' --gtest_brief=1` | Folder row body vs lane routing is distinct and correct |
| 5. Validation and coverage | full Rust + Chromium command set in this plan | End-to-end matrix is covered with reproducible commands |

## Acceptance Criteria

1. The backend can represent mixed root ordering in a unified way.
2. The adapter no longer emits root folders first and root tabs later as the only available shape.
3. The UI uses the finalized drop matrix exactly as written in this plan.
4. Folder row tab payloads branch by lane versus body.
5. Tab row lanes accept folder payloads, while tab row body rejects folder payloads.
6. Folder on tab row body is a no op.
7. Last item after lane append semantics are explicitly implemented and tested.
8. Old row hover semantics are not treated as the final design.
9. Tests prove the mixed root ordering blocker is resolved before UI behavior depends on it.

## Risk List

1. **Mixed root ordering regression**. The adapter may still flatten or reorder root entries if the core contract is incomplete.
2. **Lane versus body confusion**. A single handler may accidentally treat body drops and lane drops the same way.
3. **Invalid payload acceptance**. Folder on tab body must stay a no op and not degrade into a move.
4. **Last item append ambiguity**. The final slot after a lane may behave differently depending on whether the code treats it as insert after or append to end.
5. **Test drift**. New tests may encode the old split list shape if fixtures are not updated carefully.
6. **Partial UI implementation**. View changes that land before backend support may mask bugs instead of fixing them.

## Test Matrix

| Case | Target | Payload | Expected result | Must be covered |
|---|---|---|---|---|
| 1 | Tab row body | Tab | Split view | Yes |
| 2 | Tab row body | Folder | No op | Yes |
| 3 | Tab row lane | Tab | Reorder or lane insert | Yes |
| 4 | Tab row lane | Folder | Reorder or lane insert | Yes |
| 5 | Folder row body | Tab | Move into folder | Yes |
| 6 | Folder row body | Folder | Move into folder | Yes |
| 7 | Folder row lane | Tab | Reorder or lane insert | Yes |
| 8 | Folder row lane | Folder | Reorder or lane insert | Yes |
| 9 | Last item after lane | Tab or folder depending on target | Append semantics behave correctly | Yes |
| 10 | Root ordering | Mixed folders and tabs | Preserved order in adapter and UI | Yes |

## Validation Sequence

1. Land the backend ordering contract and run `cd /Users/indo/code/project/maho-workspace/maho && bun run test`.
2. Rebuild Chromium sidebar test targets with `PATH="/Users/indo/code/project/maho-workspace/chromium/src/third_party/depot_tools:$PATH" autoninja -C out/Default maho_sidebar_dnd_dispatch_unittest maho_sidebar_dnd_browser_tests interactive_ui_tests`.
3. Verify the adapter contract with `./out/Default/interactive_ui_tests --gtest_filter='MahoSidebarContractTest.*' --gtest_brief=1`.
4. Verify folder-row lane vs body routing with `./out/Default/maho_sidebar_dnd_dispatch_unittest --gtest_filter='SidebarDnDDispatchTest.*Folder*' --gtest_brief=1`.
5. Verify tab-list interaction behavior with `./out/Default/interactive_ui_tests --gtest_filter='MahoSidebarDnDInteractiveTest.DragReordersRegularTabs:MahoSidebarDnDInteractiveTest.DragTabIntoFolder:MahoSidebarDnDInteractiveTest.DragTabToPinnedSection:MahoSidebarDnDInteractiveTest.DragUnpinTab' --gtest_brief=1`.
6. Verify browser-level sidebar DnD with `./out/Default/maho_sidebar_dnd_browser_tests --gtest_filter='MahoSidebarDnDBrowserTest.*' --gtest_brief=1`.
7. Run `cd /Users/indo/code/project/maho-workspace/maho && bun run typecheck && bun run test` as the final Rust verification sweep.
8. Confirm the final UX matrix against the plan and only then treat the work as complete.

## Notes for Implementation Handoff

- Keep the backend and adapter work isolated from the UI work until the unified ordering contract is real.
- Treat the mixed root ordering blocker as a hard gate, not a warning.
- Do not accept any final plan that preserves the old row hover semantics as the end state.
- The implementation should be split so one engineer can own the backend contract, one can own the adapter, and one or two can own the view and test work once the gate opens.
