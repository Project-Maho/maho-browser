# Pinned Tab 2-Step Close + Cmd+Shift+T Behavior Plan — 2026-06-04

## Status
**Diagnosis: COMPLETE** (2 parallel explore agents)
**Implementation: NOT STARTED**

---

## Problem Statement

User wants asymmetric close behavior:

| Tab type | 1st close (Cmd+W / sidebar X / context menu) | 2nd close (on existing ghost) |
|----------|-----------------------------------------------|-------------------------------|
| **Pinned** | Kill `WebContents`, keep LMDB entry → renders as ghost in pinned section | Delete from LMDB entirely → vanishes from sidebar |
| **Normal** | Kill `WebContents` AND delete from LMDB → vanishes immediately | (n/a) |

`Cmd+Shift+T` (reopen last closed tab) should:
- For pinned **1st-closed** (ghost): NOT in `closed_stack`, user wakes by clicking the sidebar ghost row
- For pinned **2nd-closed** (deleted): in `closed_stack`, reopen via Cmd+Shift+T
- For normal closed: in `closed_stack`, reopen via Cmd+Shift+T

This creates a "sticky pinned tab" UX — pinned tabs are hard to lose accidentally; user must explicitly close twice.

---

## Existing Infrastructure (Already in Place)

The investigation revealed Maho **already implements 95% of the 1st-close logic**:

### 1. `PinnedCloseBehavior` enum — `maho-types/src/settings.rs:52`
```rust
pub enum PinnedCloseBehavior {
    Switch,            // just switch focus
    Reset,             // reset URL to pinned_url
    ResetSwitch,
    UnloadSwitch,      // suspend WebContents + switch focus  ← matches our 1st-close
    ResetUnloadSwitch,
    Close,             // actual full delete from LMDB
}
```

### 2. Existing gate — `maho_core.rs:2728-2797`
Already intercepts pinned tab close:
```rust
let behavior = self.settings_manager.get_settings().general.pinned_close_behavior.clone();
let is_pinned = self.tab_manager.get_tab(tab_id).map(|t| t.is_pinned).unwrap_or(false);

if is_pinned && !matches!(behavior, PinnedCloseBehavior::Close) {
    let needs_unload = matches!(behavior, PinnedCloseBehavior::UnloadSwitch | PinnedCloseBehavior::ResetUnloadSwitch);
    if needs_unload { self.tab_manager.suspend_tab(tab_id); }
    // ... reset/switch logic ...
    return vec![];  // <-- LMDB entry preserved
}
```

### 3. Pinned section ghost rendering — already works
- `Tab.is_pinned: true` + `state: Suspended { snapshot }` survives in `tab_manager`
- `space.root_order` position preserved on suspend
- `BuildTabListModel` (`state_adapter.cc:854-868`) routes by `node.is_pinned` flag → `pinned_tree`
- `RebuildTreeNodes` (`tab_list_view.cc:2835-2884`) sees `live_index < 0` → `SetSuspended(true)` → dimmed row in **pinned section**
- `IsGhostTab` filter only fires when both url AND title empty — Suspended snapshot has both → passes through

### 4. `WakeSuspendedTab` (Phase 5 from prior plan)
- Click ghost row → `ActivateTabById(tab_id)` → `live_index < 0` → `WakeSuspendedTab`
- `MahoTabIdHelper::SetPendingRestoredTabId(tab_id)` + `Navigate()` → new WebContents picks up old UUID
- Rust core's `CreateTab` handler at `event_dispatcher.rs:267` is idempotent on existing tab_id → returns existing tab → `is_pinned` flag preserved automatically

---

## Gaps (Implementation Required)

### Gap 1 — 2nd close path missing
**Location**: `maho_core.rs:2728-2797` (CloseTab handler)
**Current**: ghost (Suspended) pinned tab + close_tab event → **same `UnloadSwitch` branch fires again** → calls `suspend_tab` on already-Suspended tab (no-op) → returns empty → tab stuck as ghost forever.
**Need**: detect already-Suspended pinned tabs and fall through to full delete path.

### Gap 2 — Default `pinned_close_behavior` not `UnloadSwitch`
**Location**: settings init / default
**Current**: default may be `Switch` or `Close` — need to verify and set to `UnloadSwitch` as Maho default.

### Gap 3 — Double-dispatch from sidebar + kRemoved
**Location**: `maho_tab_registry.cc:269-291` (kRemoved observer)
**Current flow** for live pinned tab close via sidebar X:
1. `CloseTabById` → `DispatchShellEvent("close_tab")` [#1] → Rust suspends pinned tab
2. `model->CloseWebContentsAt(...)` → kRemoved fires
3. `DispatchCloseTabForContents` → `DispatchShellEventLocal("close_tab")` [#2] → Rust sees Suspended pinned tab → after Gap 1 fix, this would delete it immediately!
**Need**: kRemoved observer must NOT fire `close_tab` when removed tab was already suspended (i.e., already handled by sidebar/context-menu close).

### Gap 4 — Bulk close paths skip pin check
**Location**: `tab_lifecycle.rs:1068` (CloseOtherTabs / CloseTabsToRight / CloseTabsToLeft)
**Current**: bulk close iterates all tabs without checking `is_pinned` → would mass-delete pinned tabs.
**Need**: filter `!t.is_pinned` in bulk close methods (preserve pinned tabs from bulk operations).

### Gap 5 — `WakeSuspendedTab` is_pinned preservation (verification)
**Location**: `WakeSuspendedTab` in `tab_list_view.cc:2991`
**Current**: pre-seeds UUID via `SetPendingRestoredTabId`, calls `Navigate(NEW_FOREGROUND_TAB)`. New WebContents picks up old UUID. Rust core's `CreateTab` handler returns existing tab on UUID match (idempotent).
**Risk**: new WebContents is created in tab strip via `Navigate` — Chromium may not auto-pin it on creation. Visual state in tab strip might show as unpinned until next sidebar refresh.
**Need**: verify or explicitly call `tab_strip_model->SetTabPinned(new_idx, true)` after wake when restored tab's `is_pinned == true`.

---

## Concrete Implementation

### Change 1 — Add 2nd-close fall-through in Rust gate

**File**: `maho/crates/maho-core/src/maho_core.rs:2728-2797`

```rust
let behavior = self.settings_manager.get_settings().general.pinned_close_behavior.clone();
let tab_opt = self.tab_manager.get_tab(tab_id);
let is_pinned = tab_opt.map(|t| t.is_pinned).unwrap_or(false);
let already_suspended = tab_opt
    .map(|t| matches!(t.state, TabLifecycleState::Suspended { .. }))
    .unwrap_or(false);

if is_pinned && !matches!(behavior, PinnedCloseBehavior::Close) {
    if already_suspended {
        // 2nd close on a ghost pinned tab → fall through to full delete
        // (don't return; let the normal close_tab path execute below)
    } else {
        // 1st close: suspend + keep in LMDB
        let needs_unload = matches!(
            behavior,
            PinnedCloseBehavior::UnloadSwitch | PinnedCloseBehavior::ResetUnloadSwitch
        );
        if needs_unload {
            self.tab_manager.suspend_tab(tab_id);
        }
        // ... existing reset/switch logic ...
        return vec![];
    }
}

// Normal/full-delete path proceeds: tab_mgr.close_tab(...) etc.
```

### Change 2 — Set default `pinned_close_behavior = UnloadSwitch`

**File**: `maho/crates/maho-types/src/settings.rs` (Default impl) OR `maho-core` settings init

Verify and set:
```rust
impl Default for GeneralSettings {
    fn default() -> Self {
        Self {
            // ...
            pinned_close_behavior: PinnedCloseBehavior::UnloadSwitch,
        }
    }
}
```

If user has previously set explicit value, respect their choice (no migration override).

### Change 3 — Suppress duplicate close_tab from kRemoved for pinned tabs

**File**: `maho-chromium/browser/maho_tab_registry.cc` — `OnTabStripModelChanged(kRemoved)` at line 269

Strategy: query the WebContents' tab strip index BEFORE removal completes. Since `kRemoved` fires AFTER removal, we don't have the pinned state. Alternative: query Rust core for the tab's `is_pinned` flag using the `MahoTabIdHelper::stable_tab_id()` of the removed contents.

```cpp
} else if (change.type() == TabStripModelChange::kRemoved) {
    if (browser_shutdown::IsTryingToQuit()) { /* existing guard */ return; }

    const auto* remove = change.GetRemove();
    if (!remove) return;

    for (const auto& removed : remove->contents) {
        nav_observers_.erase(removed.contents);
        if (!TabRemoveReasonUtils::WillDeleteTab(removed.remove_reason)) continue;

        auto* helper = MahoTabIdHelper::FromWebContents(removed.contents);
        if (!helper || helper->stable_tab_id().empty()) continue;
        const std::string& tab_id = helper->stable_tab_id();

        // If this tab is already known to Rust core as pinned, the sidebar/context-menu
        // close path has already dispatched close_tab (which suspended it). Suppress the
        // duplicate dispatch from kRemoved so it doesn't immediately escalate to 2nd-close.
        if (IsCorePinnedTab(tab_id)) {
            registry_->EraseDispatchCache(tab_id);
            continue;
        }

        registry_->EraseDispatchCache(tab_id);
        DispatchCloseTabForContents(removed.contents);
    }
}
```

Helper `IsCorePinnedTab(tab_id)` queries `maho_core_get_tab_view_models` and checks `is_pinned`. Rust-side cheap (HashMap lookup).

**Alternative**: Add a flag to `MahoTabIdHelper` like `is_being_pinned_close` set by sidebar's `CloseTab` before invoking `CloseWebContentsAt`. The kRemoved observer reads the flag and skips.

**Decision**: Go with first approach (query Rust). Cleaner, no new flag plumbing.

### Change 4 — Bulk close pinned protection

**File**: `maho/crates/maho-core/src/tab_lifecycle.rs` — `close_other_tabs`, `close_tabs_to_right`, `close_tabs_to_left`

Filter:
```rust
pub fn close_other_tabs(&mut self, space_id: &SpaceId, keeper_tab_id: &TabId) -> Vec<TabId> {
    let to_close: Vec<TabId> = self.tabs.values()
        .filter(|t| t.space_id == *space_id
                  && t.id != *keeper_tab_id
                  && !t.is_pinned)        // ← preserve pinned
        .map(|t| t.id.clone())
        .collect();
    // ... existing close logic ...
}
```

Same filter for `close_tabs_to_right` and `close_tabs_to_left`.

### Change 5 — `WakeSuspendedTab` re-pin verification

**File**: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc:2991`

After `Navigate(&params)`, find the newly created tab in tab strip and re-pin if needed:
```cpp
void MahoSidebarTabListView::WakeSuspendedTab(const std::string& tab_id) {
    auto persisted = QueryTabFromCore(tab_id);
    if (!persisted) return;

    MahoTabIdHelper::SetPendingRestoredTabId(tab_id);

    NavigateParams params(browser_, GURL(persisted->url),
                          ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);

    // If the persisted tab was pinned, re-pin in the new WebContents' tab strip slot
    if (persisted->is_pinned && params.navigated_or_inserted_contents) {
        TabStripModel* model = browser_->tab_strip_model();
        int new_idx = model->GetIndexOfWebContents(params.navigated_or_inserted_contents);
        if (new_idx != TabStripModel::kNoTab && !model->IsTabPinned(new_idx)) {
            model->SetTabPinned(new_idx, true);
        }
    }
}
```

**Note**: extend `PersistedTabInfo` struct (line 2939) to include `is_pinned`.

---

## Acceptance Criteria

1. **Pinned 1st close keeps tab visible**: Pin a tab, navigate to a URL, Cmd+W → WebContents gone (tab strip count drops by 1), but **sidebar pinned section still shows the row as a dimmed ghost**.

2. **Ghost wake restores pinned status**: Click the dimmed pinned ghost in sidebar → new WebContents loads the URL → tab strip shows it as **pinned** (not as a regular tab).

3. **Pinned 2nd close removes from LMDB**: After 1st close (ghost state), Cmd+W on the ghost (or X click on sidebar ghost) → row disappears from sidebar pinned section. LMDB no longer has the entry. `closed_stack` has it.

4. **Cmd+Shift+T after 2nd close restores tab**: From state #3, Cmd+Shift+T → tab reopens with same URL, **pinned**. UUID preserved (verify via stderr log).

5. **Normal tab close is one-shot**: Open `https://example.com` → Cmd+W → tab disappears immediately from sidebar AND tab strip. Cmd+Shift+T reopens it as normal (not pinned).

6. **Bulk close preserves pinned**: Open 3 normal tabs + 2 pinned tabs. Right-click on a normal tab → "Close other tabs". The 2 pinned tabs remain (alive); other normal tabs gone.

7. **No double-close race**: Pin a tab, Cmd+W rapidly twice in succession (within 100ms). Result: tab is in ghost state (1 → ghost), not deleted (would require 2 separate close events with sidebar refresh in between).

8. **Cycle restart preserves ghost**: Pin a tab, 1st close (now ghost), Cmd+Q, relaunch. Sidebar shows the same pinned ghost. Click → wakes correctly.

---

## Risks & Mitigation

- **Default setting backward compat**: Existing users with `pinned_close_behavior = Switch` (or `Close`) will see different behavior. Mitigation: only override default when value is `IsDefaultValue()`. Migration: leave existing user choice as-is.

- **`IsCorePinnedTab` FFI cost in `kRemoved`**: Called for every removed tab (which can be many in bulk close). Each call serializes JSON. Mitigation: keep a small in-process cache of `is_pinned` keyed by tab_id, populated when sidebar refreshes. Or accept the cost — bulk close is rare.

- **Race on rapid double-close**: User Cmd+W → close_tab dispatched (#1, suspend) → sidebar refresh in flight → user Cmd+W again before refresh → close_tab dispatched (#2). Rust side sees Suspended → falls through to delete. **Result**: 2 rapid Cmd+Ws = ghost + delete = effectively skipping ghost. Acceptable behavior (user did press close twice).

- **`SetTabPinned` after Navigate timing**: `Navigate(NEW_FOREGROUND_TAB)` returns synchronously with `navigated_or_inserted_contents` set in most cases, but for some navigation types it's async. Mitigation: if `navigated_or_inserted_contents` is null, observe `OnTabStripModelChanged(kInserted)` and pin retroactively using a pending-pin map keyed by tab_id.

- **`closed_stack` size limit**: 25-deep VecDeque. Pinned 2nd-closes go on stack like normal. If user 2nd-closes more than 25 pinned tabs without Cmd+Shift+T, oldest gets evicted. Acceptable.

- **`reset_pinned_tab` interaction**: Existing `ResetPinnedTab` shell event resets pinned tab URL. Should still work — operates on existing pinned tab regardless of suspended state.

---

## Implementation Order (3-step rollout)

1. **Step 1 (~30min)**: Change 1 (Rust 2nd-close gate) + Change 2 (default UnloadSwitch). Compile-only verify.

2. **Step 2 (~1h)**: Change 3 (kRemoved suppression via Rust query) + Change 4 (bulk close protection) + Change 5 (WakeSuspendedTab re-pin). Build + manual QA on acceptance criteria 1-3.

3. **Step 3 (~30min)**: Verify acceptance criteria 4-8 with multi-cycle test. Address any edge cases discovered.

**Total estimated effort**: 2 hours including build cycles.

---

## Files Touched

| Phase | File | Change |
|-------|------|--------|
| 1 | `maho/crates/maho-core/src/maho_core.rs` | CloseTab handler 2740: add `already_suspended` check, fall through |
| 2 | `maho/crates/maho-types/src/settings.rs` | Default `pinned_close_behavior = UnloadSwitch` |
| 3 | `maho-chromium/browser/maho_tab_registry.cc` | kRemoved observer: `IsCorePinnedTab` check, skip dispatch |
| 4 | `maho/crates/maho-core/src/tab_lifecycle.rs` | `close_other/right/left_tabs`: add `!t.is_pinned` filter |
| 5 | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` | `WakeSuspendedTab` + `PersistedTabInfo`: re-pin if persisted is_pinned |

---

## Existing Mechanisms (DO NOT TOUCH)

- `MahoTabRegistry::OnTabStripModelChanged(kRemoved)` shutdown guard via `IsTryingToQuit()` — already in place
- `WakeSuspendedTab` UUID preservation via `SetPendingRestoredTabId` — works for pinned ghosts (verified)
- `chromium_src/.../session_startup_pref.cc` `LAST` patch (Phase 2 of prior plan) — required for ghost rows to survive restart
- `MahoSidebarTabRowView::SetSuspended` color-based dim (no layer) — fixed in prior cycle
- `space.root_order` positioning — preserved on suspend, no changes needed
- `IsGhostTab(url, title)` filter — only filters fully-empty url+title; safe for our snapshot-based suspended tabs

These compose with the 2-step close mechanism. The pinned ghost rendering invariants are already correct; we only need to gate the close_tab event flow.
