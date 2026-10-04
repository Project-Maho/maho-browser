# Chromium Sidebar Drag-and-Drop Reference

**Created:** 2026-05-02  
**Status:** Active reference for Chromium sidebar drag-and-drop behavior and gap analysis  
**Implementation repo:** `maho-chromium/` relative to the workspace root

---

## 1. Purpose

This document is the durable reference for Chromium sidebar drag-and-drop behavior.

It captures:

1. the current product wiring status in Chromium native sidebar Views;
2. the current core capability in `maho-core`;
3. the historical macOS AX E2E limitations;
4. the Swift source-of-truth behaviors that Chromium is expected to match;
5. the implementation and testing framing that should be reused in future work.

This document should be treated as the canonical reference before making further sidebar DnD changes.

---

## 2. Source-of-truth hierarchy

### 2.1 Swift reference

The Swift/AppKit UI tests are the behavioral source of truth for the user-facing drag-and-drop UX that already exists in the macOS shell:

- `macos-shell/UITests/SidebarDnDTests.swift`

Relevant covered Swift scenarios include:

- favorite reorder;
- tab to pinned section;
- tab to folder;
- root regular-tab reorder;
- folder reorder;
- tab out of folder.

Swift does **not** fully define the broader Chromium matrix for every nested folder case. Some of the requested matrix is therefore a product extension beyond the currently explicit Swift tests.

### 2.2 Chromium product wiring surface

The primary Chromium native sidebar DnD implementation lives here:

- `browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- `browser/ui/views/sidebar/maho_sidebar_tab_list_view.h`
- `browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc`
- `browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`

### 2.3 Core capability surface

The underlying event and ordering capabilities live here:

- `crates/maho-core/src/event_dispatcher.rs`
- `crates/maho-core/src/space_manager.rs`
- `crates/maho-core/tests/test_batch_1.rs`

### 2.4 Historical AX verification surface (unavailable)

The former native macOS AX harness file is unavailable. Its historical command
is intentionally omitted so this document cannot be mistaken for an executable
runbook. The current desktop lifecycle is documented in
`docs/chromium-desktop-e2e.md`.

It is no longer a shipped executable surface. References below to AX runs or
observations are retained as historical context only and must not be presented
as current executable evidence. Desktop E2E/검증은 omarchy(100.91.254.71)에서 실행하며,
참고 문서는 `docs/chromium-desktop-e2e.md`이다. (VM 기반 라이프사이클과 런북은 2026-09-18
완전 삭제되었다.)

---

## 3. Important framing: product gaps vs AX gaps

There are two different classes of failure in current Chromium sidebar DnD work.

### 3.1 AX testability gaps

These are historical cases where product wiring existed but the native macOS
accessibility tree did not make every folder-heavy interaction easy to verify.
The former AX harness is unavailable, so these observations are context rather
than a current executable verification layer.

Examples:

- historical folder-target drag paths were hard to observe reliably;
- some folder-heavy drops still need stronger non-AX verification;
- archived observations did not establish full folder parity.

### 3.2 Product wiring gaps

These are cases where `maho-core` supports the operation, but the Chromium native sidebar Views layer does not dispatch the needed shell event yet.

Examples:

- none of the currently verified root or nested folder reorder paths listed below should be treated as missing wiring;
- any new folder DnD case should be checked against the Views code and unit dispatch tests before calling it a gap.

Do not confuse these. AX stabilization does not fix missing product wiring, and AX weakness does not imply the Views layer is missing the path.
---

## 4. Current capability matrix

### Legend

- **Core:** `maho-core` event and state support exists.
- **Chromium Views:** native sidebar Views wiring exists.
- **Historical AX evidence:** archived observations from the unavailable harness.
- **Status:** overall current state.

### 4.1 Finalized lane/body DnD contract

The current sidebar DnD model is lane and body based, not row hover based. The finalized interaction contract is:

- default before and after lanes reorder only;
- tab on tab body routes to split view;
- tab on folder body moves into that folder;
- folder on folder body moves into that folder;
- folder on tab body is invalid and a no-op;
- mixed root ordering is supported through the unified root-order contract;
- append-after-last is explicit and tested.

### 4.2 Current capability matrix

| Interaction | Core | Chromium Views | Historical AX evidence | Status |
| --- | --- | --- | --- | --- |
| Root tab reorder | Yes | Yes | Weak archived evidence | Product wired; mixed root ordering uses the unified root-order contract |
| Folder sibling reorder at root | Yes | Yes | Weak archived evidence | Product wired; append-after-last is explicit and tested |
| Tab: pinned ↔ normal | Yes | Yes | Historical no-op observation | Product wired; current E2E verification is pending |
| Folder: pinned ↔ normal | Yes | Yes | No reliable archived path | Product wired; current E2E verification is pending |
| Tab ↔ favorites | Yes | Yes | Historical no-op observation | Product wired; runtime behavior needs current verification |
| Tab -> folder | Yes | Yes | Weak archived evidence | Product wired |
| Folder -> folder | Yes | Yes | Weak archived evidence | Product wired |
| Folder reorder inside folder | Yes | Yes | Weak archived evidence | Product wired; current E2E verification is pending |
| Tab reorder inside folder | Yes | Yes | Weak archived evidence | Product wired; current E2E verification is pending |
| Tab out of folder to root | Yes | Yes | Weak archived evidence | Product wired; current E2E verification is pending |

---

## 5. Evidence by interaction class

## 5.1 Root tab reorder

### Swift expectation

`SidebarDnDTests.swift` includes `testDragTabBetweenRegularTabsReorders()`.

Expected end-state:

- second regular root tab moves above first;
- both frames change;
- order reverses visually.

### Chromium evidence

`SidebarTabRowView::GetDropCallback()` dispatches:

- `DispatchShellEvent("reorder_tab", {"tab_id", ..., "before_tab_id", ...})`

### Core evidence

- `space_manager.rs::reorder_tab(...)`
- `event_dispatcher.rs` handles `ShellEvent::ReorderTab`

### Current status

Product wiring exists. Archived AX observations classified this as a **no-op**
in Chromium runtime, but the unavailable harness cannot provide current proof.
The remaining question is runtime behavior correctness versus gesture delivery,
not missing core support.

---

## 5.2 Folder sibling reorder at root

### Swift expectation

`SidebarDnDTests.swift` includes `testDragFolderToReorder()`.

Expected end-state:

- second folder moves above first;
- both folders move from original Y positions.

### Chromium evidence

`browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` dispatches `reorder_folder` for root folder drops using `before_folder_id`.

`browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc` covers both root folder reorder directions.

### Core evidence

- `ShellEvent::ReorderFolder`
- `ShellEvent::MoveFolderToRoot`
- `space_manager.rs::reorder_folder(...)`
- `space_manager.rs::move_folder_to_root(...)`

### Current status

This path is wired in Chromium Views. The remaining weakness is AX-based proof, not missing dispatch wiring.

---

## 5.3 Pinned / normal / favorites transfers

### Tab pinned ↔ normal

#### Chromium evidence

`SidebarSectionDropTarget::GetDropCallback()` dispatches:

- `pin_tab`
- `unpin_tab`

#### Core evidence

- `ShellEvent::PinTab`
- `ShellEvent::UnpinTab`

#### Status

Product wired.

### Folder pinned ↔ normal

In the Zen-aligned model, folders are structurally (type-)pinned. There is no user-facing toggle that flips a folder between pinned and normal, and no shell event dispatch path exists for that transition.

Folder drop behavior at section boundaries:

- folder dropped on the normal section: invalid, no-op;
- folder dropped on the favorites grid: invalid, no-op (favorites accept tab payloads only);
- folder dropped on the pinned section: valid, handled as a regular folder drop with no extra pin/unpin transition or step.

### Tab ↔ favorites

#### Chromium evidence

`maho_sidebar_favorites_grid_view.cc`:

- external tab to favorites -> `favorite_tab`
- favorite reorder -> `reorder_favorite`
- dragging favorites back to tab sections/root can trigger `unfavorite_tab`

#### Core evidence

- favorite/unfavorite support exists and seeded support-contract validation exists

#### Status

Product wired. Archived AX observations recorded favorite reorder as a runtime
no-op; current verification requires the supported non-AX layers.

### Folder ↔ favorites

Favorites grid accepts only tab payloads:

- `payload.node_kind == SidebarNodeKind::kTab`

Status:

- **not a supported interaction class** in current product model.

---

## 5.4 Tab into folder

### Swift expectation

`testDragTabToFolderDoesNotCrash()` expects:

- dragged tab appears inside `Work` folder children;
- folder remains visible;
- folder can be expanded if needed.

### Chromium evidence

`SidebarFolderRowView::GetDropCallback()` dispatches:

- `move_tab_to_folder`

### Core evidence

- `ShellEvent::MoveTabToFolder`
- `space_manager.rs::add_tab_to_folder(...)`

### Status

Product wired. Archived AX evidence was unreliable because folder rows were not
stably exposed; the unavailable harness is not a current verification surface.

---

## 5.5 Folder into folder

### Chromium evidence

`SidebarFolderRowView::GetDropCallback()` dispatches:

- `move_folder_into_folder`

### Core evidence

- `ShellEvent::MoveFolderIntoFolder`
- `event_dispatcher.rs` -> `space_mgr.move_folder(...)`
- core tests cover moving folder into folder and back to root

### Status

Product wired. AX validation remains weak/blocking.

---

## 5.6 Folder reorder inside folder

### Core evidence

Nested folder reorder is structurally supported because folders carry:

- `parent_folder_id`
- `before_folder_id`

and `space_manager.rs::reorder_folder(...)` supports parent-aware reorder.

### Chromium evidence

`browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` dispatches `reorder_folder` for same-parent nested folder drops using `parent_folder_id` and `before_folder_id`.

`browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc` covers same-parent nested folder reorder in both directions.

### Status

Product wired in Chromium Views. AX verification remains weaker than the dispatch proof.

---

## 5.7 Tab reorder inside folder

### Core evidence

- `ShellEvent::ReorderTabInFolder`
- `space_manager.rs::reorder_tab_in_folder(...)`

### Chromium evidence

`browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` dispatches `reorder_tab_in_folder` for folder-scoped tab drops.

`browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc` covers same-folder tab reorder before and after the target row.

### Status

Product wired in Chromium Views. AX verification remains weaker than the dispatch proof.

---

## 5.8 Tab out of folder back to root

### Swift expectation

`testDragTabOutOfFolderRemovesFromFolder()` expects:

- dragged child tab disappears from folder children;
- same tab appears at root regular section.

### Core evidence

- `ShellEvent::RemoveTabFromFolder`
- `space_manager.rs::remove_tab_from_folder(...)`

### Chromium evidence

`browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` dispatches `move_tab_to_root` when a folder child tab is dropped on a root tab.

`browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc` covers moving a tab out of a folder to a root-before-tab target.

### Status

Core and Chromium Views both cover the path. AX verification is still weaker than the dispatch proof.

---

## 6. Pinned vs normal parity for folder-heavy operations

The tree model itself supports pinned and normal sections in parallel:

- `MahoSidebarTabListModel` contains `pinned_tree` and `normal_tree`
- folders carry `folder_is_pinned`
- `state_adapter.cc` builds trees by `fi.is_pinned`

That means folder-heavy operations are representable in both sections.

However:

- the wired folder-heavy interactions now include `tab -> folder`, `folder -> folder`, root folder reorder, same-parent nested folder reorder, folder-scoped tab reorder, and tab-out-of-folder-to-root dispatch;
- current end-to-end proof remains pending for folder-heavy parity because the historical AX harness is unavailable.

Practical status:

- **3 and 4**: model parity exists; product wiring exists.
- **5 and 6**: product wiring exists, but AX proof is still weaker than the Views dispatch evidence.
- **tab out of folder**: product wiring exists, but archived AX evidence is not authoritative for the full folder-heavy path.

---

## 7. Current testing strategy recommendation

Do **not** rely on native macOS AX E2E as the only verification layer for folder-heavy DnD.

Use three layers:

### 7.1 Core tests

Use / extend `maho-core` tests for:

- folder reorder;
- move folder into folder;
- move folder to root;
- tab reorder in folder;
- remove tab from folder.

### 7.2 Chromium native Views drag/drop tests

Preferred verification for product wiring:

- synthetic `OSExchangeData` carrying `SidebarDragPayload`
- direct exercise of `GetDropCallback()` paths
- assert the correct shell event dispatch / resulting model refresh path

This is now the main non-AX verification layer for current Chromium sidebar DnD and should remain the primary proof surface for product wiring.

### 7.3 Supported desktop E2E and artifacts

Desktop E2E/검증은 omarchy(100.91.254.71)에서 실행한다(VM 기반 macOS 라이프사이클은
2026-09-18 완전 삭제됨). Treat historical AX observations only as diagnostic
context; the former harness is unavailable and must not be invoked or cited as
current evidence.

Do not make folder-heavy parity depend on the unavailable AX surface.

---

## 8. Files to inspect before changing DnD behavior

### Swift reference

- `macos-shell/UITests/SidebarDnDTests.swift`

### Chromium native Views implementation

- `browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- `browser/ui/views/sidebar/maho_sidebar_tab_list_view.h`
- `browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc`
- `browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`
- `browser/ui/views/sidebar/maho_sidebar_state_models.h`

### Core capability

- `crates/maho-core/src/event_dispatcher.rs`
- `crates/maho-core/src/space_manager.rs`
- `crates/maho-core/tests/test_batch_1.rs`

### Current E2E and support-contract surface

- `docs/chromium-desktop-e2e.md`
- `browser/ui/webui/maho_test/maho_test_ui.cc`

---

## 9. Decision record

The correct framing for future work is:

1. keep Swift as the behavior reference where it already defines the interaction;
2. use Chromium native sidebar Views as the actual implementation target;
3. treat `maho-core` as the source of event/state capability;
4. separate AX limitations from missing product wiring;
5. preserve current Chromium Views wiring and invest further in proof surfaces only where AX remains too weak.

---

## 10. Bottom line

At the time of writing:

- Chromium already wires **tab pinned/normal transfers**, **folder pinned/normal transfers**, **tab into folder**, **folder into folder**, **root folder reorder**, **same-parent nested folder reorder**, **folder-scoped tab reorder**, **tab out of folder to root**, and **favorite drag paths**;
- archived AX observations are non-authoritative context, while native dispatch tests and omarchy desktop E2E are the current proof surfaces;
- the live macOS accessibility tree is still the limiting factor for strong end-to-end proof on folder-heavy flows.

Any future sidebar DnD implementation or review should begin from this document.
