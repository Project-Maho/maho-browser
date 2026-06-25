# Sidebar Folder/Tab Zen Alignment Parallel Plan

**Created:** 2026-05-26  
**Status:** Draft, pending plan review  
**Reference baseline:** Zen Browser `zen-browser/desktop@b94dd3b927b0419c47eb3b41a92dfc8a0b28ce19` (`dev`)  
**Scope:** Align local sidebar folder/tab logic with confirmed Zen behavior for folder lifecycle, nesting, ordering, persistence, folder/space conversion, and live-folder integration.  

---

## Behavior-First Constraint

This plan targets **observable Zen behavior**, not literal Gecko/JS implementation parity.

- If Maho can reproduce the same externally visible behavior with Rust/C++ contracts, that is acceptable.
- Zen-only mechanisms such as DOM sentinels or `_sessionRestoring` are treated as reference mechanisms, not mandatory literal ports, unless explicitly adopted.

---

## 0. Confirmed Ground Truth

This plan is grounded only in code paths already verified.

### 0.1 Zen-side behaviors we are targeting

1. **Empty folders persist**; Zen appears to achieve this via an empty-tab sentinel.
2. **Nested folders are supported** and bounded by `zen.folders.max-subfolders` (default `5`), with enforcement in creation/drag paths.
3. **Ordering is sibling/parent anchored**, not “tabs first, folders later” synthesis.
4. **Folder pinned semantics are immutable by type** (`pinned` getter always true, setter no-op).
5. **Folder creation pins incoming tabs** and excludes essential tabs.
6. **Folder → Space conversion exists** (`convertFolderToSpace`, `changeFolderToSpace`).
7. **Live folders share the same sidebar folder system** instead of living in a detached surface.
8. **Restore is guarded** (`_sessionRestoring`) and reconstructs folder position from `parentId` + sibling anchor data.

### 0.2 Confirmed local starting point

1. **Nested folders already exist locally** via `Folder.parent_folder_id`, `MoveFolderIntoFolder`, `ReorderFolder { parent_folder_id }`, `move_folder_in_space()`, and `get_subfolders()`.
2. **Empty folders do not persist today** because `move_tab_to_root()` deletes the source folder when `tab_ids` becomes empty.
3. **CreateFolder always creates at root** because `CreateFolder` lacks `parent_folder_id` and `create_folder()` hardcodes `parent_folder_id: None`.
4. **No confirmed depth limit exists** in core move/reorder or sidebar drag/drop logic.
5. **Folder pin is mutable** (`is_pinned`, `PinFolder`, `UnpinFolder`, folder context menu pin/unpin items).
6. **Folder → Space conversion is missing**; only `ExportSpaceIntoFolder` exists.
7. **Live folders are a separate WebUI surface** and are not part of `SidebarNodeKind` or the main sidebar tree query path.
8. **Sidebar tree is rebuilt from multiple flat queries** (`root_order`, folders, tabs) rather than from a single tree-shaped contract.

---

## 1. Goal

Produce a Zen-aligned sidebar folder/tab implementation without guessing, while preserving the parts of the current local model that already match the target.

Concretely, this plan aims to:

1. Preserve empty folders when the last tab leaves.
2. Keep existing nested-folder support, but make it Zen-compatible by adding create-time nesting and a max-depth guard.
3. Replace tabs-first/root-order synthesis with sibling/parent-aware ordering primitives.
4. Reconcile folder pin semantics with Zen’s type-level folder behavior.
5. Add folder-to-space conversion.
6. Pull live folders into the same sidebar tree contract instead of a detached-only surface.
7. Reduce adapter/FFI reconstruction drift by making the sidebar tree contract more explicit.

---

## 2. Out of Scope

These items are intentionally excluded unless new evidence is gathered:

1. Broad sidebar visual redesign unrelated to folder/tab logic.
2. Product-wide favorites/essentials redesign beyond **directly evidenced** creation-exclusion and tree-placement differences.
3. Sync-model redesign (`sync_models.rs`) — not part of the confirmed Zen comparison.
4. General split-view redesign outside folder/split interaction points already confirmed.
5. GN/build-file edits.
6. Making folder-depth limits user-configurable at runtime. This plan targets the **default parity behavior** first; a pref/config surface is separate work.

---

## 3. Parallel Execution Strategy

The work can be parallelized, but not from line 1. A small contract phase must land first.

### 3.1 Sequential foundation (must land first)

#### Foundation A — Preserve empty folders

**Why first:** It is the clearest Zen mismatch and changes the core invariant for later work.

**Files**
- `maho/crates/maho-core/src/space_manager.rs`

**Primary changes**
- Remove the auto-delete behavior in `move_tab_to_root()` when `folder.tab_ids.is_empty()`.
- Audit every consumer that may implicitly assume non-empty folders (`adapter`, `FFI view models`, `folder rendering`, `DnD`, persistence tests) and update them as needed.
- Re-verify explicit folder deletion paths still work normally.
- Add/update tests so empty folders survive tab removal and persistence round-trips.

**Verification**
- Rust unit tests around `move_tab_to_root()`.
- Restore/load test proving empty folder survives persisted state reload.
- Adapter/view regression proving zero-tab folders still serialize, hydrate, and render safely.

#### Foundation B — Create-time nesting + max-depth contract

**Why second:** All later folder-creation, DnD, and conversion work depends on a stable nesting contract.

**Files**
- `maho/crates/maho-types/src/events/shell_event.rs`
- `maho/crates/maho-core/src/space_manager.rs`
- `maho/crates/maho-types/src/folder.rs`

**Primary changes**
- Add `parent_folder_id: Option<FolderId>` to `CreateFolder` with backward-compatible defaulting.
- Update `create_folder()` to honor `parent_folder_id` instead of always creating at root.
- Add a core-enforced default nesting-depth limit of **5** to match Zen’s confirmed default behavior.
- Apply that same limit in both create-time and move/reorder paths.
- Add the confirmed create-time folder behavior: incoming tabs are pinned and essential-like tabs are excluded from folder creation paths where that concept exists locally.

**Verification**
- Create nested folder directly under parent.
- Reject create/move/reorder that would exceed depth 5.
- Existing root-folder creation still works unchanged.
- Folder creation tests prove incoming tabs follow the final agreed create-time residency rules.

After Foundation A and B, the rest can branch into parallel workstreams.

---

## 4. Parallel Workstreams

### Workstream 1 — Ordering and persistence contract

**Goal:** Move local ordering closer to Zen’s sibling/parent-anchored semantics.

**Files**
- `maho/crates/maho-types/src/space.rs`
- `maho/crates/maho-types/src/events/shell_event.rs`
- `maho/crates/maho-core/src/space_manager.rs`
- `maho/crates/maho-core/src/maho_core.rs`
- `maho/crates/maho-ffi/src/lib.rs`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`

**Primary changes**
- Audit and reshape tabs-first folder synthesis assumptions in `synthesize_root_order()`.
- Audit all reorder/move APIs that still depend on positional/index-heavy semantics and convert them toward sibling-anchor semantics (`before_*` / parent-aware insertions).
- Ensure restore/backfill logic preserves explicit sibling order rather than re-deriving a new tabs-first order.
- Reduce adapter-side reconstruction ambiguity by returning enough ordering data from core/FFI.
- Add a local restore-in-progress guard analogous to Zen’s `_sessionRestoring` where needed so restore-time reconstruction does not trigger normal runtime side effects.

**Notes**
- This does **not** require literal Gecko DOM storage. It requires preserving the same behavioral invariants in our Rust/C++ contracts.

**Parallel safety**
- Can run in parallel with Workstreams 2, 3, 4.
- Should finish before Workstream 5 final adapter cleanup.

**Verification**
- Root tab/folder ordering round-trip tests.
- Nested folder sibling ordering tests.
- Restore/backfill regression tests.

---

### Workstream 2 — Folder pin semantics alignment

**Goal:** Remove the local mutable folder-pin model that diverges from Zen.

**Files**
- `maho/crates/maho-types/src/folder.rs`
- `maho/crates/maho-types/src/events/shell_event.rs`
- `maho/crates/maho-core/src/space_manager.rs`
- `maho-chromium/browser/ui/context_menu/maho_folder_context_menu.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_models.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_section_policy.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drop_planner.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`

**Primary changes**
- Remove or deprecate `PinFolder` / `UnpinFolder` as user-visible mutable operations.
- Remove folder pin/unpin context menu entries.
- Stop treating folder pin as a mutable local toggle in sidebar section policy / drag planning.
- Rework any tree filtering logic that depends on folder pin as a separate section concept.
- Define persisted-data migration behavior for existing `is_pinned: true` folders before implementation (ignore on load vs normalize on write vs explicit migration).

**Parallel safety**
- Can run alongside Workstreams 1, 3, 4.
- Must be coordinated with Workstream 5 before final adapter/view cleanup, because tree-shape assumptions overlap.

**Verification**
- Folder context menu tests.
- Sidebar tree composition tests that currently depend on `folder_is_pinned` branching.

---

### Workstream 3 — Folder ↔ Space transitions

**Goal:** Add the missing Zen-style folder-to-space path and validate the existing opposite-direction path.

**Files**
- `maho/crates/maho-types/src/events/shell_event.rs`
- `maho/crates/maho-core/src/maho_core.rs`
- `maho/crates/maho-core/src/space_manager.rs`
- `maho-chromium/browser/ui/context_menu/maho_folder_context_menu.cc`
- `maho-chromium/browser/ui/context_menu/maho_space_context_menu.cc`

**Primary changes**
- Add `ConvertFolderToSpace` event and core handler.
- Create UI entry points in folder context menu.
- Re-evaluate `ExportSpaceIntoFolder` so ordering/folder state is preserved as much as current contracts allow.
- Handle nested-folder edge cases explicitly when promoting a folder into a space.
- Decide whether Zen’s `changeFolderToSpace` behavior needs a second local path or whether `ConvertFolderToSpace` alone is the intended first milestone.

**Parallel safety**
- Can run alongside Workstreams 1, 2, 4.
- Should consume the finalized nesting contract from Foundation B.

**Verification**
- Folder-to-space conversion tests.
- Empty-folder conversion behavior tests.
- Context-menu dispatch tests.

---

### Workstream 4 — Live folder convergence spike, then implementation

**Goal:** Move live folders from a detached-only WebUI surface into the same sidebar tree system Zen uses.

**Files**
- `maho/crates/maho-types/src/folder.rs`
- `maho/crates/maho-types/src/events/core_update.rs`
- `maho/crates/maho-types/src/traits/shell_renderer.rs`
- `maho/crates/maho-core/src/maho_core.rs`
- `maho/crates/maho-ffi/src/lib.rs`
- `maho-chromium/browser/ui/webui/maho_live_folders/maho_live_folders.mojom`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_models.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`

**Primary changes**
- As spike output, decide the minimum shared-model representation needed to distinguish manual vs live folders in the sidebar pipeline.
- Extend core updates / renderer-facing view models so live-folder state can flow through the standard sidebar pipeline.
- Add a live-folder node concept to the sidebar tree shape if the spike confirms that representation.
- Keep the existing WebUI as configuration/management surface if needed, but stop treating it as the only runtime representation.

**Parallel safety**
- This should start as a **design/contract spike** in parallel with Workstreams 1–3.
- The actual implementation should begin after Workstream 1 clarifies ordering contract shape.

**Verification**
- Live folder appears in sidebar tree queries.
- Existing live-folder WebUI remains functional for configuration.
- Live-folder restrictions (create/nesting/drop rules) are testable from the shared tree path.

---

### Workstream 5 — FFI / adapter / sidebar tree boundary cleanup

**Goal:** Stop rebuilding the sidebar from multiple disconnected flat queries when the target behavior is tree-like.

**Files**
- `maho/crates/maho-core/src/maho_core.rs`
- `maho/crates/maho-ffi/src/lib.rs`
- `maho-chromium/third_party/maho/maho_ffi.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_models.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drop_planner.cc`

**Primary changes**
- Replace or consolidate the flat multi-call query pattern into an explicit tree-oriented sidebar query boundary.
- Ensure the adapter no longer has to infer missing structure from separate root-order, folder, and tab payloads.
- Update drag payload / row building to understand the final tree contract, including live folders if Workstream 4 lands.

**Dependencies**
- Depends on the output shape from Workstreams 1 and 4.
- Must be coordinated with Workstream 2 because folder pin semantics affect sectioning.

**Verification**
- Adapter contract tests.
- Sidebar row rebuild tests.
- DnD dispatch/unit tests.

---

### Workstream 6 — Favorites / essentials narrow audit and correction

**Goal:** Limit changes to what Zen evidence actually supports.

**Files**
- `maho/crates/maho-core/src/maho_core.rs`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc`

**Primary changes**
- Re-check folder creation exclusion rules against favorites/essentials behavior and implement the directly evidenced creation-time exclusion behavior.
- Confirm whether `apply_tab_residency()` and the separate favorites grid still fit after Workstreams 1 and 5.
- Do **not** broaden scope into unrelated favorites redesign unless new evidence appears.

**Parallel safety**
- Can run in parallel as a narrow audit while other workstreams proceed.
- Any implementation change should wait until Workstream 5 clarifies the final tree boundary.

**Verification**
- Residency tests.
- Folder creation exclusion tests.

---

## 5. File-by-File Critical Path Map

### Highest-contention files

These files are touched by multiple workstreams and should not be edited blindly in parallel.

| File | Why it is high contention | Workstreams touching it |
|---|---|---|
| `maho/crates/maho-core/src/space_manager.rs` | Empty-folder invariant, create-time nesting, depth checks, ordering, conversion edge cases all live here | Foundation A, Foundation B, 1, 3 |
| `maho/crates/maho-types/src/events/shell_event.rs` | All core contract expansions start here | Foundation B, 1, 2, 3 |
| `maho/crates/maho-ffi/src/lib.rs` | Final sidebar-tree boundary depends on all upstream contract choices | 1, 4, 5 |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` | Rebuild logic, sectioning, and live-folder/tree shape all converge here | 1, 2, 4, 5 |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` | Final row building and drag/drop semantics depend on final tree model | 4, 5 |

### Safe-to-parallel files

| File cluster | Can proceed mostly independently after foundations? |
|---|---|
| Folder/space context menus (`maho_folder_context_menu.cc`, `maho_space_context_menu.cc`) | Yes, after event contracts stabilize |
| Live-folder mojom + live-folder-specific view model work | Yes, as spike/contract work |
| Narrow favorites audit | Yes |
| Rust tests for empty folders / depth / create-time nesting | Yes |

---

## 6. Verification Plan

### 6.1 Rust-side verification

- `bun run test` from `maho/`
- Focused regression tests for:
  - empty folder preservation
  - nested folder creation
  - max depth = 5 enforcement
  - root/sibling ordering
  - folder-to-space conversion

### 6.2 Chromium/sidebar verification

- Sidebar adapter/unit tests for tree composition
- DnD tests covering nested folders and folder escape paths
- Context menu tests for new folder-to-space action and removed folder-pin actions
- If applicable, sidebar rendering tests for live-folder rows once unified

### 6.3 Persistence verification

- Restore with existing saved data that lacks new fields
- Verify `#[serde(default)]` / optional-field compatibility for any new type additions
- Verify empty-folder persistence across save/load

---

## 7. Risks and Guardrails

| Risk | Guardrail |
|---|---|
| Empty-folder preservation breaks assumptions in existing tests or adapter code | Land Foundation A first and update tests before broader refactors |
| Nested create + depth-limit changes introduce partial tree corruption | Enforce depth in core, not only in UI/drop planner |
| Folder pin semantic changes ripple through too many view assumptions | Keep Workstream 2 isolated and land it before Workstream 5 adapter cleanup |
| Existing persisted `is_pinned: true` folders become ambiguous after semantic change | Decide migration/normalization policy before landing Workstream 2 |
| Live-folder integration grows into an uncontrolled rewrite | Treat Workstream 4 as spike-first; do not start broad UI rewrites until contract is explicit |
| Ordering changes accidentally re-synthesize tabs-first behavior on restore | Add restore/backfill regression tests before refactoring adapter code |

---

## 8. Recommended Execution Order

1. **Foundation A** — preserve empty folders
2. **Foundation B** — add create-time nesting + depth limit 5
3. **Parallel start**
   - Workstream 1 — ordering/persistence
   - Workstream 2 — folder pin semantics
   - Workstream 3 — folder-to-space conversion
   - Workstream 4 spike — live-folder convergence contract
   - Workstream 6 — narrow favorites/essentials audit
4. **Workstream 5** — FFI/adapter/tree cleanup after contract outputs from 1/2/4
5. Final verification sweep across Rust + Chromium/sidebar tests

---

## 9. Definition of Done

This plan is complete only when all of the following are true:

1. Removing the last tab from a folder does **not** delete the folder.
2. New folders can be created directly under a parent folder.
3. Folder depth above the agreed default cap of 5 is rejected consistently in core and sidebar move paths.
4. Root and sibling ordering no longer depend on tabs-first synthetic rebuild behavior.
5. Folder pin/unpin is no longer modeled as a mutable local folder toggle.
6. Folder-to-space conversion exists end-to-end.
7. Live folders have a shared representation inside the sidebar tree path.
8. Sidebar tree/adaptation is driven by an explicit contract rather than multiple disconnected flat reconstructions.
9. Restore-time reconstruction has an explicit guard preventing normal runtime side effects from firing mid-restore.
