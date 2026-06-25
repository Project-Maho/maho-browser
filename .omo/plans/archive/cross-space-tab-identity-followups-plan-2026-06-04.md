# Cross-Space Tab Identity — Follow-ups Plan

**Created:** 2026-06-04
**Predecessor:** `.sisyphus/plans/cross-space-tab-identity-fix-plan-2026-06-02.md`
**Target:**
- `maho/crates/maho-core/src/space_manager.rs` (test additions)
- `maho/crates/maho-core/src/maho_core.rs` (load_state test, R1 fix)
- `maho/crates/maho-core/src/event_dispatcher.rs` (R1, R3)
- `maho/crates/maho-core/src/tab_lifecycle.rs` (R2)
- `maho/crates/maho-core/tests/test_batch_1.rs` (test additions)
- `maho/crates/maho-types/tests/shared_contract_test.rs` (P4 JSON roundtrip)
- `maho-chromium/browser/maho_tab_registry.cc` (C5)
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc` (C6)
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` (C9 documentation)
- `maho-chromium/browser/ui/context_menu/maho_tab_context_menu.cc` (C9 documentation)

**Goal:** Close the seven concerns surfaced during the final code review of the cross-space tab leakage fix series. The user-reported bug is already resolved; this plan is hardening only. Each follow-up is independently small and reviewable. Net result: stronger regression coverage, cleaner invariants, and explicit documentation of the few places where current behavior is correct-by-accident.

---

## Predecessor recap

The cross-space tab leakage fix series shipped six layered changes:

- **P1** Removed `ParseTreeNode` `SetRestoredTabId` mutation (render path now read-only for live helpers).
- **P2** `MahoSpaceProfileBridge::SwitchToSpace(Browser*, ...)` confined `active_space_id_` global write to the no-browser overload.
- **P3** Added `has_been_announced` bit on `MahoTabIdHelper`; `DispatchCreateTabForContents` early-returns on already-announced tabs; `kReplaced` migrates the bit.
- **P4** `ShellEvent::CloseTab` gained `expected_space_id: Option<SpaceId>`; dispatcher validates against `tab.space_id`.
- **L3** `enforce_cross_space_tab_residency` runs at `load_state` time and from `enforce_residency_for_tab` at favorite/unfavorite time.
- **L4** `MahoSidebarFavoritesGridView::OnTilePressed` adds URL-based fallback + `SetRestoredTabId` after `OpenURL`.
- **Final fix** `RebuildRows` in `maho_sidebar_tab_list_view.cc` only triggers `BuildLiveStripFallbackTabListModel` when `active_space_id_.empty()` — closes the user-reported leak.

The final fix is the actual root cause of the user's symptom. The earlier layers are defense-in-depth.

---

## Current findings (concerns to close)

| ID | Layer | Location | Concern | Severity |
|---|---|---|---|---|
| R1 | Rust | `event_dispatcher.rs::CreateTab` re-announce branch | `apply_tab_residency` not called when re-announce-as-move triggers; favorited tab can be added to destination's `root_order` | Low (rare path) |
| R2 | Rust | `tab_lifecycle.rs::collect_child_ids` | No `visited` set; cycle in parent_id chain would infinite-loop | Near-zero (cycles structurally prevented) |
| R3 | Rust | `event_dispatcher.rs::CloseTab` descendant cleanup | `last_active_tab` of a descendant's space not cleared on close | Pre-existing, low |
| R4 | Rust | tests | Four missing tests: direct unit test for `enforce_cross_space_tab_residency`, `load_state` heal integration test, `CloseTab` `expected_space_id` JSON roundtrip, `ReopenLastClosed` deleted-space fallback | Medium (regression-protection gap) |
| C5 | C++ | `maho_tab_registry.cc::kReplaced` | `MigrateDispatchCache(old_id, new_id)` is correct-by-accident: `new_id` is captured *after* `SetRestoredTabId(old_id)`, so they coincide. Fragile if migration order changes. | Low (current behavior correct, future-fragile) |
| C6 | C++ | `maho_sidebar_favorites_grid_view.cc::OnTilePressed` URL fallback | `SetRestoredTabId` patches helper to favorite's `tab_id` without dispatching `close_tab` for the original UUID; orphan Rust record left in `tab_manager.tabs` | Low (cleaned up at next session restore) |
| C9 | C++ | All `close_tab` dispatch sites | `expected_space_id` populated only when `active_space_id` is non-empty; pre-bridge-init startup window dispatches without it | Very low (Rust treats `None` as legitimate "close from home space") |

---

## Target contract

After this plan lands, all of the following must hold:

1. **R1**: When `CreateTab` re-announces an existing tab to a different space, the destination's `root_order` correctly excludes the tab if it is favorited or in a folder. `apply_tab_residency` is called with the correct flags after the move.
2. **R2**: `collect_child_ids` is guarded by a `HashSet<TabId> visited` to prevent infinite loops even in pathological data.
3. **R3**: When `CloseTab` removes a descendant tab whose space has that tab as `last_active_tab`, the pointer is cleared.
4. **R4**: Four new tests pass:
   - `enforce_cross_space_tab_residency_removes_duplicate_from_wrong_space`
   - `load_state_heals_cross_space_duplicate_after_restore`
   - `close_tab_serde_roundtrip_with_expected_space_id`
   - `reopen_last_closed_falls_back_to_active_space_when_origin_deleted`
5. **C5**: `kReplaced` cache migration intent is explicit. Either (a) `new_id` is captured *before* `SetRestoredTabId` so the call is meaningful, or (b) a comment documents that `new_id == old_id` post-call and the migration is a no-op-by-design.
6. **C6**: The orphan Rust record left by `OnTilePressed` URL fallback is either prevented (by dispatching a `close_tab` for the original UUID before patching) or formally accepted as cleanup-deferred (with documentation). Decision depends on Rust core's `close_tab` handling of unknown IDs (audited in this plan).
7. **C9**: Confirmed that Rust core's `close_tab` handler treats `expected_space_id: None` as "close from `tab.space_id`" (i.e., no validation). Behavior is documented at the dispatch sites.
8. **All Rust workspace tests still pass** (target: ≥615 with the new tests).
9. **Chromium full build still succeeds.**
10. **No new `// nogncheck`, type-suppression, or test deletion.**

---

## Patch breakdown

The plan ships seven patches, each independently reviewable. They land in the order below, with verification gates between groups.

### Group A — Test gap (R4): regression-protection foundation

**Highest priority** — these tests catch any regression in the existing fix series, including future drift.

#### A1. Direct unit test for `enforce_cross_space_tab_residency`

**File:** `maho/crates/maho-core/src/space_manager.rs` (mod tests)

**Test:**
```rust
#[test]
fn enforce_cross_space_tab_residency_removes_duplicate_from_wrong_space() {
    let mut manager = SpaceManager::new();
    let space_a = SpaceId::new("a");
    let space_b = SpaceId::new("b");
    let tab_x = TabId::new("x");

    // Set up two spaces.
    manager.spaces.insert(space_a.clone(), Space::new_for_test("a"));
    manager.spaces.insert(space_b.clone(), Space::new_for_test("b"));

    // Force-inject the same tab_id into both spaces' tab_order/root_order
    // to simulate persisted cross-space duplication from before the
    // invariant was enforced.
    manager.spaces.get_mut(&space_a).unwrap().tab_order.push(tab_x.clone());
    manager.spaces.get_mut(&space_a).unwrap().root_order.push(RootItem::Tab(tab_x.clone()));
    manager.spaces.get_mut(&space_b).unwrap().tab_order.push(tab_x.clone());
    manager.spaces.get_mut(&space_b).unwrap().root_order.push(RootItem::Tab(tab_x.clone()));

    // Owner map says tab.space_id = a (ground truth).
    let mut owners = HashMap::new();
    owners.insert(tab_x.clone(), space_a.clone());

    manager.enforce_cross_space_tab_residency(&owners);

    // Space A retains the tab (its owning space).
    assert!(manager.spaces[&space_a].tab_order.contains(&tab_x));
    assert!(manager.spaces[&space_a].root_order.contains(&RootItem::Tab(tab_x.clone())));

    // Space B has the tab stripped from both lists.
    assert!(!manager.spaces[&space_b].tab_order.contains(&tab_x));
    assert!(!manager.spaces[&space_b].root_order.contains(&RootItem::Tab(tab_x.clone())));
}
```

**Edge cases also covered:**
- Tab in `tab_owners` but space doesn't exist: should silently do nothing (no panic).
- Tab not in `tab_owners`: retained everywhere (orphan retention is intentional for this function).
- Empty `tab_owners`: no changes to any space.

**Risk:** Adds Space test-helpers if not already present. Use existing patterns from `restore_spaces_*` tests in the same module.

#### A2. Integration test for `load_state` heal path

**File:** `maho/crates/maho-core/tests/test_batch_1.rs`

**Test:**
```rust
#[test]
fn load_state_heals_cross_space_duplicate_after_restore() {
    // Set up a temp LMDB and persist a state where the same tab_id appears
    // in both space A's and space B's tab_order. This mirrors pre-invariant
    // persisted corruption.
    let tmpdir = tempfile::tempdir().unwrap();
    let mut core = MahoCore::new();
    core.set_lmdb_path(tmpdir.path()).unwrap();

    let space_a = core.create_space("A", default_color(), profile_id());
    let space_b = core.create_space("B", default_color(), profile_id());
    let tab_id = create_tab_in(&mut core, &space_a.id);

    // Force-inject duplicate into space B's tab_order (bypass invariant) by
    // direct space_manager access (test helper acceptable).
    core.space_manager_mut().force_inject_tab_order_for_testing(&space_b.id, &tab_id);

    core.save_state().unwrap();

    // Now load fresh — the heal path should run.
    let mut core2 = MahoCore::new();
    core2.set_lmdb_path(tmpdir.path()).unwrap();
    core2.load_state().unwrap();

    let a_tabs = core2.space_manager().get_tab_order(&space_a.id);
    let b_tabs = core2.space_manager().get_tab_order(&space_b.id);

    assert!(a_tabs.contains(&tab_id),
        "tab must remain in its owning space (A) after load");
    assert!(!b_tabs.contains(&tab_id),
        "tab must be evicted from non-owning space (B) by enforce_cross_space_tab_residency");
}
```

**Risk:** Requires a `force_inject_tab_order_for_testing` helper on `SpaceManager` if one doesn't exist. Add it as `#[cfg(any(test, feature = "test-utils"))]` to keep production binary clean. If the test infrastructure makes LMDB setup heavy, this could be reformulated as a Rust-only test that bypasses `save_state`/`load_state` and just calls `restore_spaces` + `enforce_cross_space_tab_residency` directly.

**Fallback variant** if LMDB setup is too heavy:
```rust
#[test]
fn restore_spaces_then_enforce_residency_evicts_cross_space_duplicate() {
    let mut core = MahoCore::new();
    // ...build spaces with corrupted tab_order...
    let spaces_with_corruption = vec![/* ... */];
    core.space_manager_mut().restore_spaces(spaces_with_corruption, active_id);

    let owners: HashMap<_, _> = core.tab_manager().get_all_tabs()
        .into_iter().map(|t| (t.id.clone(), t.space_id.clone())).collect();
    core.space_manager_mut().enforce_cross_space_tab_residency(&owners);

    // Same assertions as above.
}
```

#### A3. JSON roundtrip test for `CloseTab.expected_space_id`

**File:** `maho/crates/maho-types/tests/shared_contract_test.rs`

**Test:**
```rust
#[test]
fn close_tab_serde_roundtrip_with_expected_space_id() {
    // With expected_space_id = Some
    let evt = ShellEvent::CloseTab {
        tab_id: TabId::new("t1"),
        expected_space_id: Some(SpaceId::new("space-a")),
    };
    let json = serde_json::to_string(&evt).unwrap();
    let parsed: ShellEvent = serde_json::from_str(&json).unwrap();
    assert!(matches!(parsed, ShellEvent::CloseTab { expected_space_id: Some(_), .. }));

    // With expected_space_id = None
    let evt2 = ShellEvent::CloseTab {
        tab_id: TabId::new("t2"),
        expected_space_id: None,
    };
    let json2 = serde_json::to_string(&evt2).unwrap();
    let parsed2: ShellEvent = serde_json::from_str(&json2).unwrap();
    assert!(matches!(parsed2, ShellEvent::CloseTab { expected_space_id: None, .. }));

    // Backward-compat: legacy JSON without the field.
    let legacy_json = r#"{"closeTab":{"tabId":"t3"}}"#;
    let parsed3: ShellEvent = serde_json::from_str(legacy_json).unwrap();
    assert!(matches!(parsed3, ShellEvent::CloseTab { expected_space_id: None, .. }));
}
```

**Risk:** None. Pure value test.

#### A4. ReopenLastClosed deleted-space fallback test

**File:** `maho/crates/maho-core/tests/test_batch_1.rs`

**Test:**
```rust
#[test]
fn reopen_last_closed_falls_back_to_active_space_when_origin_deleted() {
    let mut core = MahoCore::new();
    let space_a = core.create_space("A", default_color(), profile_id());
    let space_b = core.create_space("B", default_color(), profile_id());

    let tab_id = create_tab_in(&mut core, &space_a.id);
    core.handle_event(ShellEvent::CloseTab {
        tab_id: tab_id.clone(),
        expected_space_id: None,
    });

    // Switch active to B, then delete A.
    core.handle_event(ShellEvent::ActivateSpace { space_id: space_b.id.clone() });
    core.delete_space(&space_a.id);

    // Reopen — should fall back to B (active), not crash on dangling A.
    core.handle_event(ShellEvent::ReopenLastClosed);

    let reopened = core.tab_manager().get_tab(&tab_id).expect("tab should be reopened");
    assert_eq!(reopened.space_id, space_b.id,
        "reopened tab must be reassigned to active space when origin space was deleted");
    assert!(core.space_manager().get_tab_order(&space_b.id).contains(&tab_id),
        "tab must appear in active space's tab_order");
}
```

**Risk:** None. Existing event_dispatcher logic already handles this case (lines 536–563); this is just the missing test.

---

### Group B — Code hardening

#### R1. CreateTab re-announce branch calls `apply_tab_residency`

**File:** `maho/crates/maho-core/src/event_dispatcher.rs`

**Current code (around line 272–278):**
```rust
if let Some(existing) = tab_mgr.get_tab(&explicit_id).cloned() {
    if existing.space_id != space_id {
        let source = existing.space_id.clone();
        tab_mgr.move_tab(&explicit_id, space_id.clone(), 0);
        space_mgr.remove_tab_from_space(&source, &explicit_id);
    }
    existing
} else {
    ...
}
```

**Patched code:**
```rust
if let Some(existing) = tab_mgr.get_tab(&explicit_id).cloned() {
    if existing.space_id != space_id {
        let source = existing.space_id.clone();
        tab_mgr.move_tab(&explicit_id, space_id.clone(), 0);
        space_mgr.remove_tab_from_space(&source, &explicit_id);
        // Also enforce favorite/folder residency at the destination so a
        // re-announced favorite doesn't end up in the destination's
        // root_order via the unconditional add_tab_to_space below.
        let is_in_folder = space_mgr.is_tab_in_folder(&space_id, &explicit_id);
        space_mgr.apply_tab_residency(&space_id, &explicit_id, existing.is_favorite, is_in_folder);
    }
    existing
} else {
    ...
}
```

**Why:** The `add_tab_to_space` call at line 285 unconditionally adds the tab to the destination's `root_order`. If the tab is favorited, this violates the "favorites stay out of root_order" invariant. `apply_tab_residency` strips the tab from `root_order` if it is favorited or in a folder.

**Risk:** Low. `apply_tab_residency` is idempotent and the trigger condition is narrow (existing tab + different space). All existing tests should remain green.

**Verification:** `bun run test`. Add a focused test if missing:
```rust
#[test]
fn create_tab_reannounce_favorited_does_not_add_to_destination_root_order() {
    // Create T in S_A, favorite it. Re-announce CreateTab with S_B.
    // Assert: T moved to S_B, but T NOT in S_B.root_order (favorite invariant).
}
```

#### R2. `collect_child_ids` cycle guard

**File:** `maho/crates/maho-core/src/tab_lifecycle.rs`

**Current code (line 179–198):**
```rust
pub fn collect_child_ids(&self, parent_id: &TabId) -> Vec<TabId> {
    let mut result = Vec::new();
    let mut queue = vec![parent_id.clone()];
    while let Some(current_parent) = queue.pop() {
        let children: Vec<TabId> = self.tabs.values()
            .filter(|t| t.parent_id.as_ref() == Some(&current_parent))
            .map(|t| t.id.clone())
            .collect();
        for child in children {
            queue.push(child.clone());
            result.push(child);
        }
    }
    result
}
```

**Patched code:**
```rust
pub fn collect_child_ids(&self, parent_id: &TabId) -> Vec<TabId> {
    let mut result = Vec::new();
    let mut queue = vec![parent_id.clone()];
    let mut visited: std::collections::HashSet<TabId> = std::collections::HashSet::new();
    visited.insert(parent_id.clone());
    while let Some(current_parent) = queue.pop() {
        let children: Vec<TabId> = self.tabs.values()
            .filter(|t| t.parent_id.as_ref() == Some(&current_parent))
            .map(|t| t.id.clone())
            .collect();
        for child in children {
            if !visited.insert(child.clone()) {
                continue;
            }
            queue.push(child.clone());
            result.push(child);
        }
    }
    result
}
```

**Why:** `set_tab_parent` prevents cycle creation, but defense-in-depth is cheap here. A corrupt persisted state with a cycle would otherwise hang the close path. The visited set is `O(N)` memory but `N` is bounded by the tab count in the parent's subtree.

**Risk:** Behavior unchanged for acyclic data (the only case in normal operation). Correctness preserved.

**Verification:** Add a test:
```rust
#[test]
fn collect_child_ids_terminates_on_cycle() {
    let mut mgr = TabLifecycleManager::new(...);
    // Force-inject T1.parent_id = T2 and T2.parent_id = T1 (bypass set_tab_parent guards).
    // Assert: collect_child_ids(T1) terminates and returns at most {T2}.
}
```

#### R3. `last_active_tab` cleanup for closed descendants

**File:** `maho/crates/maho-core/src/event_dispatcher.rs`

**Current code (around line 320–322):**
```rust
for (sid, cid) in &descendant_spaces {
    space_mgr.remove_tab_from_space(sid, cid);
}
```

**Patched code:**
```rust
for (sid, cid) in &descendant_spaces {
    space_mgr.remove_tab_from_space(sid, cid);
    if space_mgr.get_last_active_tab(sid) == Some(cid) {
        space_mgr.set_last_active_tab(sid, None);
    }
}
```

**Why:** Without this, a space whose `last_active_tab` was a descendant of a closed parent retains a dangling pointer. The next "restore last active" lookup would return a tab_id that no longer exists.

**Risk:** Pre-existing condition was simply that the dangling pointer wasn't observable in any user flow. Adding the cleanup makes behavior strictly more correct.

**Verification:** Add a test:
```rust
#[test]
fn close_tab_clears_last_active_tab_for_descendants() {
    // T_parent in S, T_child with parent_id=T_parent in S.
    // set_last_active_tab(S, T_child).
    // CloseTab(T_parent).
    // Assert: get_last_active_tab(S) == None.
}
```

---

### Group C — C++ documentation / explicit-intent

#### C5. `kReplaced` cache migration ordering

**File:** `maho-chromium/browser/maho_tab_registry.cc` (around line 360–376)

**Current code (paraphrased):**
```cpp
const std::string old_id = old_helper->stable_tab_id();
if (old_helper->stable_tab_id() != new_helper->stable_tab_id()) {
    new_helper->SetRestoredTabId(old_helper->stable_tab_id());
}
const std::string new_id = new_helper->stable_tab_id();  // ← captured AFTER SetRestoredTabId
new_helper->set_has_been_announced(old_helper->has_been_announced());
registry_->MigrateDispatchCache(old_id, new_id);  // ← new_id == old_id, so no-op
```

**Patched code (Option A — capture before mutation):**
```cpp
const std::string old_id = old_helper->stable_tab_id();
const std::string new_id_pre_swap = new_helper->stable_tab_id();
if (old_id != new_id_pre_swap) {
    new_helper->SetRestoredTabId(old_id);
}
new_helper->set_has_been_announced(old_helper->has_been_announced());
// Migrate the cache from the new helper's pre-swap UUID to the old (now-canonical) UUID.
registry_->MigrateDispatchCache(new_id_pre_swap, old_id);
```

**Patched code (Option B — comment only):**
```cpp
const std::string old_id = old_helper->stable_tab_id();
if (old_helper->stable_tab_id() != new_helper->stable_tab_id()) {
    new_helper->SetRestoredTabId(old_helper->stable_tab_id());
}
const std::string new_id = new_helper->stable_tab_id();
new_helper->set_has_been_announced(old_helper->has_been_announced());
// At this point new_id == old_id (SetRestoredTabId above), so this call is a
// no-op-by-design. The cache entry under old_id is preserved in-place. Future
// edits that change SetRestoredTabId ordering must also update this site.
registry_->MigrateDispatchCache(old_id, new_id);
```

**Recommended:** Option A. It makes the migration intent explicit and removes the order-dependent fragility.

**Risk:** Low. Behavior change: now actively migrates the cache from the new helper's original (random) UUID to the old (carried) UUID. Currently the cache key was already the old UUID due to dispatch ordering, so the migration is effectively a rename of the same key. Need to verify no caller stores a cache entry under the new helper's pre-swap UUID. Likely safe because the new helper is fresh and no dispatches happen against it before this swap.

**Verification:** `build_maho.py` + manual smoke test of tab discard.

#### C6. URL fallback orphan record

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc::OnTilePressed` (path 1b, around line 1421–1424)

**Decision required first:** audit Rust core's `close_tab` handler behavior for an unknown `tab_id`. Two possible code-shapes depending on outcome:

**Case 1 — Rust silently ignores `close_tab` for unknown ID:**
The orphan Rust record is harmless. It will be cleaned up at next session restore (which rebuilds tab state from the C++ session). Add a comment at path 1b:
```cpp
// Path (1b) re-binds the live helper to the favorite's tab_id. The previous
// random helper UUID is now orphaned in the Rust core's tab_manager, but
// will be cleaned up at next session restore. close_tab for an unknown
// tab_id is a no-op in core, so there is no risk of corrupted state.
```

**Case 2 — Rust treats unknown `tab_id` as an error or has side-effects:**
Dispatch a `close_tab` for the original UUID before patching:
```cpp
// Dispatch close_tab for the original UUID so the orphan Rust record is
// cleaned up; then re-bind the helper to the favorite's tab_id.
const std::string original_id = helper->stable_tab_id();
maho::DispatchShellEvent("close_tab", {{"tab_id", original_id}});
helper->SetRestoredTabId(tab_id);
```

**Audit step (part of this plan):** Read `event_dispatcher.rs::CloseTab` and `maho_core.rs:close_tab` to determine which case applies. Document the finding and apply the corresponding patch.

**Risk:** Case 2 has higher risk because it dispatches a synthetic `close_tab` to clean up state. Need to confirm the synthetic close doesn't trigger any UI flicker or duplicate-event handling.

**Verification:** Manual test of the rare URL-fallback scenario: artificially desync the helper UUID and the favorite tab_id, click the favorite tile, verify the live tab activates and no orphan state remains.

#### C9. `expected_space_id` documentation at dispatch sites

**Files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc::CloseTab` and `CloseTabById`
- `maho-chromium/browser/ui/context_menu/maho_tab_context_menu.cc::ExecuteCommand IDC_MAHO_TAB_CLOSE`

**Audit step:** Read `event_dispatcher.rs::CloseTab` handler to confirm `expected_space_id: None` semantics.

**Expected outcome:** `None` is treated as "no validation; close from `tab.space_id`". Confirmed by the `if let Some(expected_sid) = expected_space_id` guard at line 292 — when `None`, validation is skipped entirely.

**Patch:** Add a comment at each dispatch site:
```cpp
// expected_space_id is intentionally omitted when active_space_id is empty
// (bridge not yet initialized — startup window). Rust core's CloseTab handler
// treats None as "no validation"; the close is processed against tab.space_id
// directly, which is the correct fallback during startup before the bridge
// has a space context.
```

**Risk:** None. Documentation only.

---

## Sequencing & verification gates

The follow-ups land in this order. Each group must pass its gate before the next is applied.

| # | Group | Patches | Gate |
|---|---|---|---|
| 1 | Group A — Tests | A1, A2, A3, A4 | `bun run test` passes; ≥615 total tests; new tests pass |
| 2 | Group B — Hardening | R1, R2, R3 | `bun run test` passes; new R1/R2/R3 tests pass |
| 3 | Group C5 | kReplaced ordering | `build_maho.py` exit 0; manual tab-discard smoke |
| 4 | Group C6 audit | Read Rust close_tab handler | Document findings, decide Case 1 vs Case 2 |
| 5 | Group C6 patch | Comment or close_tab dispatch | `build_maho.py` exit 0; manual URL-fallback smoke |
| 6 | Group C9 | Documentation comments | `build_maho.py` exit 0 (no behavioral change) |

If any gate fails, halt and revert the failing group. Do not stack failures.

---

## Out of scope

This plan does **not**:

- Reduce favorite cross-space scope (favorites grid stays profile-wide; that's an explicit product decision).
- Touch `MahoSpaceProfileBridge::SetLastActiveTab`/`GetLastActiveTab` wiring (separate UX feature).
- Change `BuildLiveStripFallbackTabListModel` itself — only the gate that decides when to call it (already done).
- Add new shell events or FFI signatures.
- Change persistence format. `expected_space_id` field is already deployed as backward-compat optional; no migration needed.
- Address pre-existing concerns unrelated to the cross-space fix series (e.g., `MahoTabIdHelper`'s pending-restored-tab-id slot leak risk).

---

## Rollback

Each patch is small and revertable as a single commit:

- **Group A**: revert each test commit. Tests are pure additions; no production code change.
- **R1**: remove the `apply_tab_residency` call in the re-announce branch. Behavior reverts to "leak favorite into destination root_order on re-announce" (rare path).
- **R2**: remove the `visited` set. Cycle protection reverts to "structurally guaranteed by `set_tab_parent`".
- **R3**: remove the `last_active_tab` cleanup. Reverts to "dangling pointer for closed descendants" (unobservable).
- **C5 (Option A)**: revert to the `new_id` capture-after-mutation order. Cache migration becomes accidental no-op again.
- **C6**: remove comment or remove synthetic close_tab dispatch. Orphan Rust records reappear (harmless if Case 1).
- **C9**: remove documentation comments. No behavioral change.

If a regression is reported, bisect order: C6 → C5 → R3 → R2 → R1 → A4 → A3 → A2 → A1.

---

## Open questions (must resolve during implementation)

1. **C6 case decision:** Audit `event_dispatcher.rs::CloseTab` and `tab_lifecycle.rs::close_tab` to determine whether unknown tab_id triggers any side effect. The answer determines Case 1 (comment) vs Case 2 (synthetic close_tab dispatch).
2. **A2 LMDB test feasibility:** If LMDB setup in tests is too heavy, fall back to the simpler "restore_spaces + enforce" variant. Both validate the same invariant.
3. **R1 test coverage:** A focused test for re-announce-as-move with a favorited tab is missing from the existing suite. Add one alongside the patch.
4. **Test helpers required:** `Space::new_for_test`, `force_inject_tab_order_for_testing` (on `SpaceManager`), `space_manager_mut`/`tab_manager` accessors. Confirm these exist or add behind `#[cfg(any(test, feature = "test-utils"))]`.

---

## Definition of done

- All seven concerns addressed (4 tests + R1 + R2 + R3 + C5 + C6 audit & patch + C9 documentation).
- All Rust workspace tests pass at ≥615.
- Chromium full build passes.
- No new test ignored, deleted, or `#[ignore]`-marked.
- `kReplaced` cache migration intent is explicit (Option A capture-before, OR Option B documented).
- C6 outcome is documented inline at the call site.
- C9 dispatch sites have inline comments confirming the bridge-init-window fallback is intentional and safe.
- The user-reported bug (cross-space favorite leakage into other spaces' regular tab list) remains resolved.

When all of the above hold, the cross-space tab identity workstream is complete.
