# Sidebar Tab and Folder Remaining Issues Remediation Plan

**Date:** 2026-05-21
**Branch:** main
**Scope:** Targeted closeout of the remaining tab and folder correctness issues across the Rust core (`maho/crates/maho-core/`) and the Chromium sidebar overlay (`maho-chromium/browser/ui/views/sidebar/` plus `maho-chromium/browser/ui/context_menu/`). This is an implementation plan only. No build, runtime, or manual QA commands are included here.

---

## Objective

Close out the small, named set of tab and folder defects that survive after the recent sidebar DnD and section refactors. The work is split between the Rust core (residency invariant and event-path cleanup) and the C++ sidebar (DnD asymmetry, cache pruning, missing folder-to-root surface, dead adapter state). Each issue listed below is grounded in code that already exists. This plan does not introduce new product features and does not redesign the planner or section policy.

---

## Grounded Issue Inventory

Each item is the basis for exactly one workstream entry below. Speculative issues are out of scope.

A. **Residency invariant still split across three Rust files.** The "favorites live in the favorites bucket, non-favorites live in `root_order`, never both" rule is enforced piecewise in `maho/crates/maho-core/src/space_manager.rs`, dispatched through `maho/crates/maho-core/src/maho_core.rs`, and applied by individual branches in `maho/crates/maho-core/src/event_dispatcher.rs`. The invariant needs a single, authoritative seam so each event path is forced through it.

B. **Dead direct `FavoriteTab` / `UnfavoriteTab` branches in `event_dispatcher.rs`.** These branches bypass the `MahoCore` wrapper that all other favorite-related paths use. They are unreachable in normal flow but still compile and still mutate state if hit, which is a foot-gun.

C. **Residency-unsafe `root_order` handling in event branches.** `MoveTab`, `MoveTabToSpace`, `ReopenLastClosed`, and `RemoveTabFromFolder` in `event_dispatcher.rs` need to consult favorite status before they push into or pull from `root_order`. Today they treat all tabs as `root_order` candidates, which can produce a tab that is both favorited and listed in `root_order`.

D. **`MoveFolderIntoFolder` ignores `space_id`.** The current implementation in `event_dispatcher.rs` mutates by `folder_id` alone, so a folder move can affect a folder with the same id in a different space. The branch must scope its mutation to the requested space.

E. **Asymmetric same-folder neighbor suppression in C++.** In `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` the same-folder neighbor suppression branch (around the `is_same_folder_neighbor` check) is asymmetric. The forward and backward neighbor cases do not behave the same way, so a same-folder no-op drop can still produce a visible reorder hint or dispatch.

F. **`PruneLocalTitleCache` ignores favorites.** The pruner in `maho_sidebar_tab_list_view.cc` (`PruneLocalTitleCache(model)`) walks only the non-favorite portion of the model. Favorites with locally-edited custom titles are never pruned, so stale local titles can leak across tab id reuse or favorite churn.

G. **Sidebar surface for `MoveFolderToRoot` is missing.** Rust already supports `MoveFolderToRoot` (see `maho/crates/maho-types/src/events/shell_event.rs` line 235 and the dispatcher branch at `maho/crates/maho-core/src/event_dispatcher.rs` line 748). The gap is on the C++ side: there is no shared DnD constant in `maho_sidebar_dnd_events.h`, no drop dispatch path in the sidebar views, and no folder context menu action in `maho_folder_context_menu.h/.cc` plus `maho_context_menu_ids.h`.

H. **Dead `tab_id_to_contents_` state in `maho_sidebar_state_adapter.h`.** The map is exposed via accessor and stored as `mutable std::map<std::string, content::WebContents*>`, but it is never populated by any caller. Either a real consumer needs to start populating it or the field plus accessor should be removed.

### Correction

The correction noted by the user is preserved here so this plan does not silently broaden scope:

> `maho/crates/maho-types/src/events/shell_event.rs` and the Rust event dispatcher already define `MoveFolderToRoot`. This plan does NOT add a new Rust shell event. Workstream 3 only closes the missing C++ sidebar surface (DnD constant, drop dispatch, folder context menu action, menu id).

---

## Workstreams

Four workstreams. They are scoped to the issue inventory above, in the same order. Each workstream lists exact file targets and the likely functions or methods to change. Any function name listed below is a candidate seam, not a contract: implementation is free to introduce a clearer helper if the listed function is not the right home.

### Workstream 1 — Rust residency invariant hardening

Covers issue A.

**Goal:** Make "favorites bucket xor `root_order`" an invariant enforced at one seam, then make every event path go through that seam.

**Target files:**

- `maho/crates/maho-core/src/space_manager.rs`
- `maho/crates/maho-core/src/maho_core.rs`
- `maho/crates/maho-core/src/event_dispatcher.rs`

**Likely functions and methods to change:**

- `space_manager.rs`
  - Introduce a single residency helper that, given a space and a tab id, can: classify the tab as favorite or non-favorite, insert it into the correct bucket at a given index, remove it from any bucket, and assert that the tab is not present in both buckets after the operation.
  - Likely seams: existing favorite-mutating helpers (`set_favorite_*`, `add_favorite_*`, `remove_favorite_*`), and existing root-order helpers (`insert_root_*`, `remove_root_*`, `reorder_root_*`). Introduce a thin wrapper that all callers must use, or fold these helpers into one residency-aware API.
- `maho_core.rs`
  - Route every favorite or root-order mutation that is currently called from `event_dispatcher.rs` through the new residency helper. The wrapper functions exposed on `MahoCore` for favorites and root order should not allow the caller to pick the wrong bucket.
- `event_dispatcher.rs`
  - Replace direct calls into the older bucket-specific helpers with calls to the new residency-safe API exposed by `MahoCore`. No business logic moves into the dispatcher in this pass.

**Exit condition for this workstream:** It is no longer possible for a single event path to put a tab into both `root_order` and the favorites bucket without the residency helper rejecting or reconciling it.

---

### Workstream 2 — Rust event-path cleanup

Covers issues B, C, and D.

**Goal:** Remove the dead favorite branches in the dispatcher, make the four named tab-move event branches residency-safe, and scope `MoveFolderIntoFolder` to its `space_id`.

**Target file:**

- `maho/crates/maho-core/src/event_dispatcher.rs`

**Likely functions and methods to change:**

- The dispatcher arm at `ShellEvent::FavoriteTab { tab_id }` near line 342.
- The dispatcher arm at `ShellEvent::UnfavoriteTab { tab_id }` near line 353.
- The dispatcher arm at `ShellEvent::MoveTab { .. }` near line 388.
- The dispatcher arm at `ShellEvent::MoveTabToSpace { .. }` near line 412.
- The dispatcher arm at `ShellEvent::ReopenLastClosed` near line 481.
- The dispatcher arm at `ShellEvent::RemoveTabFromFolder { .. }` near line 676.
- The dispatcher arm at `ShellEvent::MoveFolderIntoFolder { .. }` near line 733.

**Per-arm work:**

- B (`FavoriteTab`, `UnfavoriteTab`): Delete the direct branches in `event_dispatcher.rs`. The MahoCore wrapper path is the single supported route for favorite state changes. If a caller still needs a dispatcher-level handler, route it through the new residency helper from Workstream 1.
- C (`MoveTab`, `MoveTabToSpace`, `ReopenLastClosed`, `RemoveTabFromFolder`): For each branch, look up favorite status before computing a `root_order` index. If the tab is favorited, do not insert it into `root_order` and do not assume it lives in `root_order` for purposes of removal. Use the Workstream 1 residency helper so the bucket choice is centralized.
- D (`MoveFolderIntoFolder`): Take the `space_id` field that is already on the event payload and apply the mutation only to that space's folder tree. No matching by `folder_id` alone across spaces.

**Exit condition for this workstream:** The dispatcher has no direct favorite-state mutation, all four named tab-move branches are residency-safe, and `MoveFolderIntoFolder` is space-scoped.

---

### Workstream 3 — C++ sidebar DnD and menu fixes

Covers issues E and G.

**Goal:** Make same-folder neighbor suppression symmetric in the tab list view, and add the missing C++ surface for `MoveFolderToRoot`. No new Rust event is added in this workstream.

**Target files:**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_events.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drop_planner.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drop_planner.h`
- `maho-chromium/browser/ui/context_menu/maho_context_menu_ids.h`
- `maho-chromium/browser/ui/context_menu/maho_folder_context_menu.h`
- `maho-chromium/browser/ui/context_menu/maho_folder_context_menu.cc`

**Likely functions and methods to change:**

- E, same-folder neighbor suppression
  - In `maho_sidebar_tab_list_view.cc`, the block around the `is_same_folder_neighbor` computation (around line 1360). Make the forward-neighbor and backward-neighbor cases mirror each other: same predicate, same suppression, same drop indicator reset. The fix lives entirely inside whatever method owns that neighbor decision; do not change unrelated drop planning code in this pass.
- G, `MoveFolderToRoot` C++ surface
  - `maho_sidebar_dnd_events.h`: add a shared DnD constant for the "promote folder to root" intent, consistent with the existing constants used for folder-into-folder and folder reorder.
  - `maho_sidebar_drop_planner.{h,cc}`: extend the planner so a folder dragged from inside a parent folder, dropped into a root insertion lane (or onto a root drop zone that is not another folder body), produces the new "folder to root" plan. Reuse existing zone classification helpers; do not redesign zone detection.
  - Sidebar views drop dispatch path: at the call site that consumes the planner output and dispatches `MoveFolderIntoFolder` and folder-reorder events today, add the dispatch for the existing Rust `MoveFolderToRoot` shell event when the planner emits the new intent.
  - `maho_context_menu_ids.h`: add a stable command id for the new folder context menu entry.
  - `maho_folder_context_menu.h`/`.cc`: add a "Move folder to root" entry, gated to the case where the folder has a parent folder (so it is hidden for already-root folders). Wire the command id through the menu builder and through the existing `ExecuteCommand` dispatch so it emits the existing Rust `MoveFolderToRoot` shell event.

**Exit condition for this workstream:** Same-folder neighbor suppression is symmetric, and a folder can be promoted to root via both DnD and the folder context menu without any new Rust event.

---

### Workstream 4 — C++ cache and model cleanup, plus tests

Covers issues F and H, plus the test coverage that protects the changes from Workstreams 1 through 3.

**Goal:** Make `PruneLocalTitleCache` cover favorites, retire the dead `tab_id_to_contents_` adapter state unless a real consumer is identified, and lock the new contracts behind tests.

**Target files:**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`
- Test files listed under "Tests and Contracts" below.

**Likely functions and methods to change:**

- F, `PruneLocalTitleCache`
  - `MahoSidebarTabListView::PruneLocalTitleCache(...)` near line 2687 of `maho_sidebar_tab_list_view.cc`. Walk both the non-favorite tab set and the favorites bucket of the supplied model when computing the live id set. Any tab id present in either bucket is "live"; everything else is prunable. The single call site at line 2086 does not need to change beyond passing the same model.
- H, dead `tab_id_to_contents_`
  - In `maho_sidebar_state_adapter.h`, the accessor at line 78 and the field at line 85. First confirm there is no caller that already populates the map elsewhere in `maho_sidebar_state_adapter.cc` or in the broader sidebar views. If no real producer or consumer exists, remove the field, the accessor, and any include now made unused. If a real consumer is identified, instead wire a single producer at the appropriate adapter ingress and document that producer at the field declaration. Default outcome for this pass is removal.

**Exit condition for this workstream:** The local title cache no longer leaks across favorite churn, the state adapter no longer carries a dead map, and the new behaviors from all four workstreams are covered by tests.

---

## File Targets at a Glance

Rust:

- `maho/crates/maho-core/src/space_manager.rs` — residency helper.
- `maho/crates/maho-core/src/maho_core.rs` — single seam for event paths.
- `maho/crates/maho-core/src/event_dispatcher.rs` — branch cleanup and residency-safe routing.
- `maho/crates/maho-types/src/events/shell_event.rs` — read-only reference; not modified by this plan.

C++ sidebar:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_events.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drop_planner.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drop_planner.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`

C++ context menu:

- `maho-chromium/browser/ui/context_menu/maho_context_menu_ids.h`
- `maho-chromium/browser/ui/context_menu/maho_folder_context_menu.h`
- `maho-chromium/browser/ui/context_menu/maho_folder_context_menu.cc`

---

## Sequencing

The workstreams must run in this order. Out-of-order work either re-introduces residency violations or piles cleanup on top of a moving target.

1. **Workstream 1 — Rust residency invariant hardening.** Establishes the one seam every other Rust change depends on.
2. **Workstream 2 — Rust event-path cleanup.** Routes the named branches through the new seam, deletes the dead favorite branches, and scopes `MoveFolderIntoFolder` to its space.
3. **Workstream 3 — C++ sidebar DnD and menu fixes.** Symmetric same-folder suppression first (small, isolated), then the `MoveFolderToRoot` C++ surface (DnD, then context menu). The context menu work depends on the DnD planner change because both share the new shared DnD constant.
4. **Workstream 4 — C++ cache and model cleanup, plus tests.** Pruner fix and adapter dead-code removal can run in parallel within this workstream. Tests come last so they assert the final shape, but tests for each workstream may be added incrementally as that workstream lands.

Within each workstream, prefer the smallest possible commits. Do not bundle Rust and C++ changes into one commit.

---

## Tests and Contracts

This section names where coverage should land. It is a target list, not a compile or runtime command list.

**Rust unit and integration tests:**

- `maho/crates/maho-core/src/space_manager.rs` (or its existing test module): residency helper invariant test. A tab placed in favorites is not in `root_order`, and vice versa. Both buckets cannot be mutated independently for the same tab.
- `maho/crates/maho-core/src/event_dispatcher.rs` (or its existing test module):
  - `MoveTab` for a favorited tab does not touch `root_order`.
  - `MoveTab` for a non-favorited tab updates `root_order` correctly.
  - `MoveTabToSpace` preserves residency on the destination space.
  - `ReopenLastClosed` of a previously-favorited tab restores it into the favorites bucket and not into `root_order`.
  - `RemoveTabFromFolder` for a favorited tab does not insert it into `root_order`.
  - `MoveFolderIntoFolder` does not affect a folder with the same id in another space.
  - The (now removed) `FavoriteTab` / `UnfavoriteTab` direct branches have no remaining call sites; an assertion test or a `#[deny]` build configuration is acceptable as the protection.

**C++ unit tests:**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc`:
  - Symmetric same-folder neighbor suppression: forward-neighbor and backward-neighbor same-folder lane drops both produce the same no-op (or both produce the same `ReorderTabInFolder`), and never produce an asymmetric mix.
  - New folder-to-root DnD path: a nested folder dragged to a root insertion lane dispatches `MoveFolderToRoot`. A root folder dragged in the same way does not.
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_planner_unittest.cc`:
  - Drop planner emits the new "folder to root" intent for the correct zones and does not emit it for folder-into-folder middle zones.
- `maho-chromium/browser/ui/context_menu/maho_folder_context_menu_unittest.cc`:
  - "Move folder to root" entry is present for nested folders, hidden for already-root folders, and dispatches `MoveFolderToRoot` when invoked.

**C++ contract and interactive tests:**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_contract_test.cc`:
  - Local title cache pruning includes favorites: a renamed favorite that is later removed has its local title pruned and does not leak across tab id reuse.
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_interactive_test.cc`:
  - A live drag of a nested folder into a root insertion lane promotes the folder to root.
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_actions_interactive_test.cc`:
  - Folder context menu "Move folder to root" promotes the folder to root.

**Adapter cleanup test posture:**

- If `tab_id_to_contents_` is removed, no new test is required; the removal is covered by existing build and unit-test coverage of the adapter.
- If a real consumer is identified and the field is kept, add a unit test that asserts the producer populates the map for every reachable tab on the relevant adapter ingress.

---

## Atomic Commit Strategy

One concern per commit. Do not mix Rust and C++. Do not mix workstreams.

Suggested commit shape:

1. `maho-core: introduce residency helper and route favorites/root-order through it` (Workstream 1, no behavior change visible to callers other than the new seam).
2. `maho-core: route MoveTab through residency helper` (Workstream 2, tab-move arm).
3. `maho-core: route MoveTabToSpace through residency helper` (Workstream 2, cross-space arm).
4. `maho-core: route ReopenLastClosed through residency helper` (Workstream 2).
5. `maho-core: route RemoveTabFromFolder through residency helper` (Workstream 2).
6. `maho-core: scope MoveFolderIntoFolder to space_id` (Workstream 2, issue D).
7. `maho-core: remove dead FavoriteTab/UnfavoriteTab dispatcher branches` (Workstream 2, issue B).
8. `sidebar: make same-folder neighbor suppression symmetric` (Workstream 3, issue E).
9. `sidebar: add shared MoveFolderToRoot DnD constant and planner intent` (Workstream 3, issue G, planner half).
10. `sidebar: dispatch MoveFolderToRoot from drop dispatch` (Workstream 3, issue G, dispatch half).
11. `sidebar: add Move folder to root context menu entry` (Workstream 3, issue G, menu half).
12. `sidebar: prune local title cache across favorites` (Workstream 4, issue F).
13. `sidebar: remove dead tab_id_to_contents_ adapter state` (Workstream 4, issue H, default outcome).
14. `sidebar/maho-core: tests for residency, folder-to-root, neighbor symmetry, and pruner` (Workstream 4, tests).

If a single test file ends up covering more than one issue, split that test commit by issue, not by file.

---

## Non-Goals

This pass is intentionally narrow. The following are explicitly out of scope and must not be folded in unless a new reproducer makes them necessary:

- No broad rewrite of the sidebar planner (`maho_sidebar_drop_planner.{h,cc}`) beyond adding the new "folder to root" intent and using the new shared DnD constant.
- No broad rewrite of the section policy (`maho_sidebar_section_policy.{h,cc}`).
- No new Rust shell events. `MoveFolderToRoot` already exists in `maho/crates/maho-types/src/events/shell_event.rs` and the dispatcher; this plan only closes the C++ side.
- No changes to favorites grid layout, favicon handling, or tab preview surfaces.
- No animation, motion, or visual polish work.
- No cross-window or cross-profile changes.

If a new reproducer surfaces during implementation that genuinely requires expanding scope, capture it as a follow-up plan rather than widening this one.
