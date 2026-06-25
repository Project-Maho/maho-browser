# Cmd+W on Favorite — Fix Plan (Pinned Pattern Parity)

**Date:** 2026-06-08
**Scope:** Make `Cmd+W` on a favorite tab suspend the live `WebContents` only, while preserving the favorite entry in the sidebar (Arc parity).
**Effort:** ~3–5 hours

---

## Bug Summary

Pressing `Cmd+W` on a favorite (essentials) tab currently **destroys the favorite entry** from the sidebar. Expected behavior: the live `WebContents` should be unloaded/suspended while the favorite remains pinned in the grid, ready to be re-awakened on next click.

This is a **data-loss bug** — favorites are persistent user-curated entities. Removal must require an explicit "Remove from Favorites" action (which already exists as `IDC_MAHO_FAV_REMOVE` in `maho_favorites_context_menu.cc:41`).

---

## Root Cause (Verified)

The `Tab` entity in `maho-core` carries both `is_pinned` and `is_favorite` flags. Pinned tabs are protected from destructive close via two coordinated guards:

1. **Rust core** — `maho_core.rs:2924` `ShellEvent::CloseTab` checks `is_pinned && behavior != Close` → suspends instead of closing → returns `vec![]`
2. **C++ tab registry** — `maho_tab_registry.cc:334` `IsCorePinnedTab(tab_id)` check suppresses the duplicate `close_tab` that would otherwise fire from `TabStripModelChange::kRemoved`

**Neither guard exists for `is_favorite`.** The fix is to add equivalent guards.

### Current Cmd+W → Favorite Deletion Trace

```
Cmd+W (IDC_CLOSE_TAB)
  → chrome::CloseTab()
  → TabStripModel::CloseWebContentsAt()
  → TabStripModelChange::kRemoved fires
  → MahoTabRegistry::StripObserver::OnTabStripModelChanged()    [maho_tab_registry.cc:302]
  → IsCorePinnedTab() == false (favorite is NOT pinned)         [line 334 — no IsCoreFavoriteTab check]
  → DispatchCloseTabForContents()                                [line 340]
  → DispatchShellEventLocal("close_tab", {tab_id})               [line 156]
  → MahoCore::handle_event(ShellEvent::CloseTab)
  → maho_core.rs:2894 — is_pinned=false → no early-return        [no is_favorite guard]
  → event_dispatcher.rs:332 — tab_mgr.close_tab(&tab_id)
  → tab_lifecycle.rs:162 — self.tabs.remove(tab_id)              [Tab record DESTROYED]
  → Sidebar rebuilds favorites from get_favorite_tabs() → tile gone
```

### What Pinned Does Differently

```
Cmd+W (IDC_CLOSE_TAB)
  → ... same path through CloseWebContentsAt ...
  → kRemoved fires
  → IsCorePinnedTab() == true → continue (suppress duplicate)    [maho_tab_registry.cc:334]
  → (close_tab event was already dispatched by sidebar/context menu earlier)
  → maho_core.rs:2924 — is_pinned=true && behavior != Close
  → suspend_tab(tab_id) + focus switch
  → return vec![]                                                  [Tab record SURVIVES]
```

---

## Fix Design — Mirror Pinned Pattern Exactly

### Change 1: `maho/crates/maho-core/src/maho_core.rs`

In `handle_event_inner` for `ShellEvent::CloseTab` (line ~2894), AFTER the existing `is_pinned` guard block (which ends around line 2985 with the pinned suspend/reset/focus-switch logic and `return vec![]`), add a parallel `is_favorite` guard:

```rust
// After the pinned guard's return vec![]:

// Favorite guard — mirror PinnedCloseBehavior::UnloadSwitch.
// First close on an active favorite: suspend WebContents only, keep
// favorite entry intact. Second close (when already Suspended) falls
// through to full delete, matching the pinned 2nd-close pattern.
let tab_opt = self.tab_manager.get_tab(tab_id);
let is_favorite = tab_opt.map(|t| t.is_favorite).unwrap_or(false);
let already_suspended = tab_opt
    .map(|t| matches!(t.state, TabLifecycleState::Suspended { .. }))
    .unwrap_or(false);

if is_favorite && !already_suspended {
    let space_id = match tab_opt.map(|t| t.space_id.clone()) {
        Some(sid) => sid,
        None => return vec![],
    };

    self.tab_manager.suspend_tab(tab_id);

    // Focus-switch if the favorite was the active tab (mirrors pinned UnloadSwitch).
    let currently_active =
        self.space_manager.get_last_active_tab(&space_id).cloned();
    if currently_active.as_ref() == Some(tab_id) {
        let next_active = self
            .space_manager
            .get_tab_order(&space_id)
            .into_iter()
            .filter(|id| id != tab_id)
            .find(|id| {
                self.tab_manager
                    .get_tab(id)
                    .map(|t| !matches!(t.state, TabLifecycleState::Archived { .. }))
                    .unwrap_or(false)
            })
            .cloned();
        self.space_manager.set_last_active_tab(&space_id, next_active);
    }

    self.persist_lmdb_now();
    return vec![CoreUpdate::TabSuspended { tab_id: tab_id.clone() }];
}

// Fall-through: 2nd close on suspended favorite → full delete via dispatcher
```

**Notes:**
- The 2nd-close fallthrough is intentional — matches pinned semantics. After Cmd+W twice in a row, the favorite is fully deleted. This is the user's escape hatch when they really do want it gone (alternative to the explicit "Remove from Favorites" menu action).
- `CoreUpdate::TabSuspended` may need adding to the `core_update.rs` enum if not present. If a closer match exists (e.g., `TabStateChanged`), use that.
- `persist_lmdb_now()` should run identically to the pinned path so the suspended state survives a restart.

### Change 2: `maho-chromium/browser/maho_tab_registry.cc`

Add `IsCoreFavoriteActiveTab` helper next to existing `IsCorePinnedTab` (lines 53–83):

```cpp
bool IsCoreFavoriteActiveTab(const std::string& tab_id) {
  if (tab_id.empty()) return false;
  auto* core = MahoCoreHolder::Get();
  if (!core) return false;

  std::string space_id_json = "null";
  // Use same pattern as IsCorePinnedTab to query favorites.
  char* json = maho_core_get_favorite_tabs(core, space_id_json.c_str());
  if (!json) return false;

  std::string out(json);
  maho_string_free(json);

  std::optional<base::Value> parsed = base::JSONReader::Read(out);
  if (!parsed || !parsed->is_list()) return false;

  for (const auto& item : parsed->GetList()) {
    if (!item.is_dict()) continue;
    const std::string* id = item.GetDict().FindString("tab_id");
    if (id && *id == tab_id) {
      // Favorite present in the grid AND not yet suspended/archived.
      // We want to suppress the duplicate close_tab dispatch ONLY when the
      // tab is still in the live state — i.e., only on the FIRST Cmd+W.
      // The Rust core's get_favorite_tabs filters out Archived but not
      // Suspended, so we additionally check via tab_state if exposed.
      // For now, presence in the favorites list = is_favorite=true → skip.
      return true;
    }
  }
  return false;
}
```

Then in `OnTabStripModelChanged` at line 334:

```cpp
// Existing:
if (IsCorePinnedTab(tab_id)) {
  registry_->EraseDispatchCache(tab_id);
  continue;
}

// ADD parallel block:
if (IsCoreFavoriteActiveTab(tab_id)) {
  registry_->EraseDispatchCache(tab_id);
  continue;  // suspended-favorite path — Rust core already handled it
}
```

**Caveat on the active-vs-suspended distinction:** the C++ side currently can't easily distinguish "favorite active" from "favorite already suspended" without a richer FFI query. Two options:

**Option 2A (simpler, recommended):** Suppress on ANY favorite. Then the 2nd-close path becomes: user clicks the suspended favorite tile to wake it → state = Active → user presses Cmd+W → suspends again. The user can never fully delete via Cmd+W. Trade-off: removes the "double-tap to delete" escape hatch. To delete, user must use the explicit "Remove from Favorites" menu.

**Option 2B (matches pinned exactly):** Add a new FFI `maho_core_is_favorite_tab_active(core, tab_id) -> bool` that returns `true` only when `is_favorite && state == Active`. Suppress dispatch only when active. 2nd close on a suspended favorite falls through and triggers full delete via Rust path. More work, but closer to pinned parity.

**Recommendation: 2A.** Cleaner mental model ("Cmd+W never destroys a favorite — explicit action required") and simpler code. The 2nd-close-deletes pattern is a confusing affordance that few users would discover. The explicit "Remove from Favorites" menu is discoverable and explicit.

If we go with 2A, also remove the 2nd-close fallthrough in Change 1 — keep the favorite suspended forever until explicit unfavorite.

### Change 3 (only if Option 2A): Simplify Change 1's fallthrough

If 2A, replace the 2nd-close fallthrough comment with a permanent guard:

```rust
if is_favorite {
    // Cmd+W on a favorite NEVER destroys it. Suspend the WebContents.
    // To delete, use Remove from Favorites menu (IDC_MAHO_FAV_REMOVE).
    let tab_opt = self.tab_manager.get_tab(tab_id);
    let already_suspended = tab_opt
        .map(|t| matches!(t.state, TabLifecycleState::Suspended { .. }))
        .unwrap_or(false);
    if !already_suspended {
        // ... suspend + focus-switch logic ...
    }
    return vec![];  // Always early-return; never delete via close_tab
}
```

---

## Test Plan

### New unit tests (in `maho/crates/maho-core/src/maho_core.rs` test module, alongside existing `CloseTab` tests at lines 512, 527)

```rust
#[test]
fn close_tab_on_active_favorite_suspends_keeping_favorite() {
    let mut core = make_test_core();
    let tab_id = create_test_tab(&mut core, "https://example.com");
    core.handle_event(ShellEvent::FavoriteTab { tab_id: tab_id.clone() });

    let updates = core.handle_event(ShellEvent::CloseTab {
        tab_id: tab_id.clone(),
        expected_space_id: None,
    });

    let tab = core.get_tab(&tab_id).expect("tab record must survive");
    assert!(tab.is_favorite, "favorite flag must be preserved");
    assert!(matches!(tab.state, TabLifecycleState::Suspended { .. }),
            "tab must be in Suspended state, not Active");
    // No TabClosed update should be emitted for the suspended favorite
    assert!(!updates.iter().any(|u| matches!(u, CoreUpdate::TabClosed { .. })));
}

#[test]
fn close_tab_on_suspended_favorite_remains_suspended() {
    // Option 2A behavior: 2nd Cmd+W is also a no-op
    let mut core = make_test_core();
    let tab_id = create_test_tab(&mut core, "https://example.com");
    core.handle_event(ShellEvent::FavoriteTab { tab_id: tab_id.clone() });
    core.handle_event(ShellEvent::CloseTab {
        tab_id: tab_id.clone(),
        expected_space_id: None,
    });
    // 2nd close
    let updates = core.handle_event(ShellEvent::CloseTab {
        tab_id: tab_id.clone(),
        expected_space_id: None,
    });

    let tab = core.get_tab(&tab_id).expect("favorite must still exist");
    assert!(tab.is_favorite);
    assert!(!updates.iter().any(|u| matches!(u, CoreUpdate::TabClosed { .. })));
}

#[test]
fn unfavorite_tab_after_close_then_close_actually_deletes() {
    // The escape hatch: explicit unfavorite, then Cmd+W deletes.
    let mut core = make_test_core();
    let tab_id = create_test_tab(&mut core, "https://example.com");
    core.handle_event(ShellEvent::FavoriteTab { tab_id: tab_id.clone() });
    core.handle_event(ShellEvent::UnfavoriteTab { tab_id: tab_id.clone() });

    let updates = core.handle_event(ShellEvent::CloseTab {
        tab_id: tab_id.clone(),
        expected_space_id: None,
    });

    assert!(core.get_tab(&tab_id).is_none(), "tab must be deleted now");
    assert!(updates.iter().any(|u| matches!(u, CoreUpdate::TabClosed { .. })));
}
```

### C++ side verification

Run existing `maho_sidebar_dnd_dispatch_unittest.cc` and any tab registry tests. None should regress because we're adding a new branch, not modifying existing branches.

### Manual QA

1. Add a tab to favorites (drag to favorites grid)
2. Press `Cmd+W` while focused on the favorite tab
3. Expected: live tab content unloads, favorite tile remains in grid (possibly dimmed/sleeping)
4. Click the favorite tile → tab content reloads (existing wake-from-suspended path)
5. Right-click favorite tile → "Remove from Favorites" → tile disappears
6. Verify pinned tabs still work as before (no regression)

---

## Files Modified

| File | Lines | Purpose |
|---|---|---|
| `maho/crates/maho-core/src/maho_core.rs` | ~2985+ | Add `is_favorite` guard in `ShellEvent::CloseTab` arm |
| `maho-chromium/browser/maho_tab_registry.cc` | 53–83, 334 | Add `IsCoreFavoriteActiveTab` helper, suppress duplicate dispatch |
| `maho/crates/maho-core/src/maho_core.rs` (test module) | end | 3 new unit tests |

If `CoreUpdate::TabSuspended` doesn't exist, add to:
| File | Purpose |
|---|---|
| `maho/crates/maho-types/src/events/core_update.rs` | New enum variant `TabSuspended { tab_id }` |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` | Handle new update variant (likely just triggers favorites refresh) |

---

## Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| FFI cache miss on `IsCoreFavoriteActiveTab` (if the JSON parse is slow per-event) | Low | Pinned has same pattern with no observed perf issue. If needed, add a lightweight `maho_core_is_favorite(tab_id)` FFI later. |
| 2nd-close fallthrough (Option B with delete) confuses users | Medium | Choose Option 2A (never delete via Cmd+W) — cleaner UX. |
| User expects Cmd+W on favorite to behave like Chrome (just close) | Low | This bug literally has data-loss; the fix is correct. Document in release notes. |
| `CoreUpdate::TabSuspended` not yet wired through C++ adapter | Medium | Verify in adapter; if not handled, fall back to triggering general favorites refresh. |
| Pinned + favorite (tab is both) order-of-checks matters | Low | Pinned check fires first; favorite-only check is unreachable for that case. Existing pinned semantics win, which is correct. |

---

## Recommendation

**Implement with Option 2A** (Cmd+W on favorite never destroys). Cleanest semantic, minimal C++ changes, matches the user's expressed mental model ("favorites are persistent, only explicit action removes them"). The "double-tap delete" escape hatch from pinned isn't worth replicating for favorites because:

1. Favorites are user-curated and emotionally "mine" — destruction should require explicit confirmation
2. The "Remove from Favorites" menu is discoverable
3. Pinned tabs without a `pinned_url` could be ambiguous (open + recreate); favorites are unambiguous (always have URL)

---

## Acceptance Criteria

1. ✅ Cmd+W on an active favorite tab → live WebContents suspends, favorite tile stays in sidebar grid
2. ✅ Click suspended favorite tile → tab reloads (existing path, no regression)
3. ✅ Right-click favorite → "Remove from Favorites" → tile disappears (existing path)
4. ✅ Cmd+W on a non-favorite, non-pinned tab → tab fully closes (existing behavior, no regression)
5. ✅ Cmd+W on a pinned tab → suspends per existing PinnedCloseBehavior (no regression)
6. ✅ All 3 new unit tests pass
7. ✅ All existing tests pass (`bun run test` from `maho/`)
8. ✅ Restart-survival: suspend a favorite via Cmd+W, restart browser → favorite reappears suspended
