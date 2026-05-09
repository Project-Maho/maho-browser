# Chromium Sidebar Drag-and-Drop Reference

**Created:** 2026-05-02  
**Status:** Active reference for Chromium sidebar drag-and-drop behavior and gap analysis  
**Implementation repo:** `/Users/indo/code/project/maho-workspace/maho-chromium/`

---

## 1. Purpose

This document is the durable reference for Chromium sidebar drag-and-drop behavior.

It captures:

1. the current product wiring status in Chromium native sidebar Views;
2. the current core capability in `maho-core`;
3. the current macOS AX E2E limitations;
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

### 2.4 Current AX verification surface

The current native macOS AX harness lives here:

- `build/scripts/e2e_chromium_sidebar.py`
- `docs/chromium-desktop-e2e.md`

That harness can still validate the broader desktop launch flow and some non-folder drag outcomes. It should not be treated as authoritative for folder-heavy parity, because the live AX tree does not yet provide a strong enough verification path for every folder interaction.

---

## 3. Important framing: product gaps vs AX gaps

There are two different classes of failure in current Chromium sidebar DnD work.

### 3.1 AX testability gaps

These are cases where the product wiring exists, but the native macOS accessibility tree still does not make every folder-heavy interaction easy to verify from the current AX harness.

Examples:

- folder-target drag paths can be hard to observe reliably in live Chromium AX;
- some folder-heavy drops still need stronger non-AX verification;
- the harness can confirm launches and some support-contract checks, but not full folder parity.

### 3.2 Product wiring gaps

These are cases where `maho-core` supports the operation, but the Chromium native sidebar Views layer does not dispatch the needed shell event yet.

Examples:

- none of the currently verified root or nested folder reorder paths listed below should be treated as missing wiring;
- any new folder DnD case should be checked against the Views code and unit dispatch tests before calling it a gap.

Do not confuse these. AX stabilization does not fix missing product wiring, and AX weakness does not imply the Views layer is missing the path.
---

## 4. Current capability matrix\n\n### Legend\n\n- **Core:** `maho-core` event and state support exists.\n- **Chromium Views:** native sidebar Views wiring exists.\n- **AX E2E:** current macOS AX harness can sometimes exercise it, but may not prove it cleanly.\n- **Status:** overall current state.\n\n### 4.1 Finalized lane/body DnD contract\n\nThe current sidebar DnD model is lane and body based, not row hover based. The finalized interaction contract is:\n\n- default before and after lanes reorder only;\n- tab on tab body routes to split view;\n- tab on folder body moves into that folder;\n- folder on folder body moves into that folder;\n- folder on tab body is invalid and a no-op;\n- mixed root ordering is supported through the unified root-order contract;\n- append-after-last is explicit and tested.\n\n### 4.2 Current capability matrix\n\n| Interaction | Core | Chromium Views | AX E2E | Status |\n| --- | --- | --- | --- | --- |\n| Root tab reorder | Yes | Yes | Weak, can be hard to prove from current AX runs | Product wired, mixed root ordering uses the unified root-order contract |\n| Folder sibling reorder at root | Yes | Yes | Weak, currently better proven by unit dispatch than AX runs | Product wired, append-after-last is explicit and tested |\n| Tab: pinned ↔ normal | Yes | Yes | Weak, no-op observed in current harness runs | Product wired, verification is still weak |\n| Folder: pinned ↔ normal | Yes | Yes | No reliable AX path | Product wired, verification is still weak |\n| Tab ↔ favorites | Yes | Yes | Favorite reorder no-op observed in current harness, support-contract exists | Product wired, runtime behavior still needs stronger verification |\n| Tab -> folder | Yes | Yes | Folder AX is weak | Product wired |\n| Folder -> folder | Yes | Yes | Folder AX is weak | Product wired |\n| Folder reorder inside folder | Yes | Yes | Weak, currently better proven by unit dispatch than AX runs | Product wired, AX verification remains weak |\n| Tab reorder inside folder | Yes | Yes | Weak, currently better proven by unit dispatch than AX runs | Product wired, AX verification remains weak |\n| Tab out of folder to root | Yes | Yes | Weak, current AX runs do not fully prove it | Product wired, AX verification remains weak |\n\n---

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

Product wiring exists. Current AX harness runs classify this as a **no-op** in Chromium runtime, so the remaining question is runtime behavior correctness vs DnD gesture delivery, not missing core support.

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

#### Chromium evidence

`SidebarSectionDropTarget::GetDropCallback()` dispatches:

- `pin_folder`
- `unpin_folder`

#### Core evidence

- `ShellEvent::PinFolder`
- `ShellEvent::UnpinFolder`

#### Status

Product wired.

### Tab ↔ favorites

#### Chromium evidence

`maho_sidebar_favorites_grid_view.cc`:

- external tab to favorites -> `favorite_tab`
- favorite reorder -> `reorder_favorite`
- dragging favorites back to tab sections/root can trigger `unfavorite_tab`

#### Core evidence

- favorite/unfavorite support exists and seeded support-contract validation exists

#### Status

Product wired, but current AX harness has observed favorite reorder as a runtime no-op. This needs stronger non-AX verification.

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

Product wired. Current AX harness cannot verify reliably because folder rows are not stably exposed.

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
- AX verification is still the weak link for folder-heavy parity, so these paths should be described as product wired but not fully proven by the current harness.

Practical status:

- **3 and 4**: model parity exists; product wiring exists.
- **5 and 6**: product wiring exists, but AX proof is still weaker than the Views dispatch evidence.
- **tab out of folder**: product wiring exists, but the current AX harness is not authoritative for the full folder-heavy path.

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

### 7.3 AX / artifact E2E

Keep the current AX harness for:

- favorites baseline;
- support-contract checks;
- selected smoke validation where AX is stable.

Do not force folder-heavy parity to depend on AX stability.

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

- `build/scripts/e2e_chromium_sidebar.py`
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
- the current AX harness can still run useful checks, but it should not be treated as authoritative for folder-heavy parity when the dispatch proof is the stronger evidence;
- the live macOS accessibility tree is still the limiting factor for strong end-to-end proof on folder-heavy flows.

Any future sidebar DnD implementation or review should begin from this document.
