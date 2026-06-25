# Sidebar Folder & Tab Structure — Final Remediation Plan

**Date:** 2026-06-02 (revised after Oracle review)
**Predecessor:** `.sisyphus/plans/sidebar-folder-tab-comprehensive-rca-2026-06-02.md`
**Oracle review:** Validated R3, R4, R5. **Disputed R1 hypotheses, REJECTED R2 fix as dead code, identified CRITICAL new issue N1 (FFI thread race).**

---

## 1. Background

Oracle review revealed:
- **R1** (toggle visual): RCA's proposed fix is blind speculation. Must block on diagnostic LOG capture first. NEW suspicion: N1 thread race may cause intermittent state corruption.
- **R2** (lost tabs): RCA's proposed fix `SetRestoredTabId` reinsertion is **dead code** — the inner `if` condition (`helper->stable_tab_id() != node.tab_id`) is always false by construction. Real bug is in `tab_registry.cc::Bootstrap()` + `has_been_announced_` permanently swallowing restored tabs when space_id is empty at boot.
- **R3, R4, R5**: Confirmed. R5 fix should remove ALL drop overrides, not just `CanDrop`.
- **N1 (NEW, CRITICAL)**: `BuildFavoritesModelOnBackground` runs on `base::ThreadPool` and calls `maho_core_get_favorite_tabs` while UI thread can simultaneously run `HandleEvent` on the same `Rc<RefCell<>>`-backed core. This is `!Send`/`!Sync` violation.

---

## 2. Tasks

### Task F3 — Validate R1 root cause via diagnostic LOG (BLOCKING)

**Action:** Build with existing `[MAHO TOGGLE DIAG]` + `[MAHO SBR DIAG]` logs, click folder, capture output.

**Steps:**
1. `python3 maho-chromium/build/scripts/build_maho.py`
2. Launch from terminal: `chromium/src/out/Default/Maho.app/Contents/MacOS/Maho 2>&1 | grep "MAHO"`
3. Click any folder once (single click)
4. Inspect captured LOG output

**Decision matrix:**
| LOG pattern | Diagnosis | Next |
|---|---|---|
| `coalesced (needs_re_refresh_=true)` appears | Coalesce race (B1.1) | Apply F-R1-A: remove ScheduleRefreshAll indirection |
| All 3 SBR DIAG lines fire but no visual change | Read returned stale or rebuild ignores it | Add JSON dump LOG inside `BuildTabListModel`, grep `"isExpanded"` |
| No `OnFavoritesModelReady` after `ScheduleRefreshAll entry` | ThreadPool stuck or WeakPtr invalid | Likely **N1 manifesting** — proceed to F4 |
| All 3 fire AND visual changes | R1 already fixed by side effects | Skip F-R1-* fixes |

**Pass criteria:** Scenario identified.

---

### Task F1 — Fix R3 (active tab URL stale)

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`
**Function:** `OverlayActiveTabFields`
**Where:** Inside `if (active_contents)` block, BEFORE `node.is_loading = active_contents->IsLoading();`

**Insert:**
```cpp
const GURL& live_url = GetVisibleOrCommittedUrl(active_contents);
node.url = live_url.is_valid() ? live_url.spec() : std::string();
```

**Verification:**
- Build passes
- Open YouTube → click into a video (SPA pushState navigation) → sidebar URL reflects video URL not homepage

---

### Task F2 — Fix R5 (SpaceHeaderRow drop semantics)

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
**Class:** `SidebarSpaceHeaderRow` (line ~2228)

**Action:** Remove ALL drop-related overrides. Keep only `Configure()`, layout, labels.

**Remove these methods entirely:**
- `GetDropFormats`
- `AreDropTypesRequired`
- `CanDrop`
- `OnDragUpdated`
- `OnDragExited`
- `GetDropCallback`

**Remove member:**
- `bool drag_over_ = false;` (and any `UpdateAppearance()` referencing it)

**Verification:**
- Build passes
- Drag normal tab to space header → no highlight, no drop accept, tab returns to origin

---

### Task F-R1-A — Apply if F3 confirms coalesce race (B1.1)

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
**Function:** `OnSpaceProfileBridgeChanged`

**Replace:**
```cpp
void MahoSidebarView::OnSpaceProfileBridgeChanged() {
  if (footer_view_) UpdateFooter(state_adapter_.BuildFooterModel(browser_));
  UpdateSpacesPaneActions();
  ScheduleRefreshAll();
}
```

**With:**
```cpp
void MahoSidebarView::OnSpaceProfileBridgeChanged() {
  if (footer_view_) UpdateFooter(state_adapter_.BuildFooterModel(browser_));
  UpdateSpacesPaneActions();
  if (refresh_scheduled_) {
    needs_re_refresh_ = true;
  } else {
    RefreshAll();
  }
}
```

Bypasses async favorites round-trip when the change is state-driven (toggle, etc.) where favorites need not be re-fetched. `RefreshAll` reads sidebar tree synchronously via `BuildViewStateModel`.

**Verification:** Folder toggle visually responds within one frame.

---

### Task F4 — Fix N1 (FFI thread race) — apply regardless of F3 outcome

**Severity:** CRITICAL latent race. Even if R1 has another cause, this race is real.

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc:619-633`

**Action (Option A — simpler, recommended):** Move `BuildFavoritesModelForSpaceIdJson` to UI thread.

**Replace:**
```cpp
void MahoSidebarView::RefreshFavoritesAndViewState() {
  std::string active_space_id_json;
  if (auto* bridge = MahoSpaceProfileBridge::GetInstance(); bridge) {
    const std::string& active_space_id = bridge->GetActiveSpaceId(browser_);
    if (!active_space_id.empty()) {
      active_space_id_json = base::GetQuotedJSONString(active_space_id);
    }
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarView::BuildFavoritesModelOnBackground,
                     active_space_id_json),
      base::BindOnce(&MahoSidebarView::OnFavoritesModelReady,
                     weak_factory_.GetWeakPtr()));
}
```

**With:**
```cpp
void MahoSidebarView::RefreshFavoritesAndViewState() {
  std::string active_space_id_json;
  if (auto* bridge = MahoSpaceProfileBridge::GetInstance(); bridge) {
    const std::string& active_space_id = bridge->GetActiveSpaceId(browser_);
    if (!active_space_id.empty()) {
      active_space_id_json = base::GetQuotedJSONString(active_space_id);
    }
  }
  // N1 fix: run synchronously on UI thread to avoid Rc<RefCell<>> race with HandleEvent.
  // Favorites read is small (fixed slot count); UI latency impact negligible.
  OnFavoritesModelReady(BuildFavoritesModelOnBackground(active_space_id_json));
}
```

**Verification:**
- Build passes
- Run app, perform many rapid sidebar actions (drag, toggle, tab adds) — no `BorrowMutError` panic in stderr
- Folder toggle and tab additions still update sidebar visually

---

### Task F6 — Restore popup show callback

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc:1890-1893`

**Replace:**
```cpp
void SidebarFolderRowView::OnMouseEntered(const ui::MouseEvent& event) {
  views::View::OnMouseEntered(event);
  UpdateAppearance();
}
```

**With:**
```cpp
void SidebarFolderRowView::OnMouseEntered(const ui::MouseEvent& event) {
  views::View::OnMouseEntered(event);
  UpdateAppearance();
  if (show_popup_callback_ && !children_snapshot_.empty()) {
    show_popup_callback_.Run();
  }
}
```

**Verification:**
- Hover non-empty folder → popup appears after 225ms with full tab list
- Hover empty folder → no popup

---

### Task F8 — Remove diagnostic LOGs

**Action:** After R1 confirmed working, remove all diagnostic logs.

**Find:**
```bash
grep -rn "MAHO TOGGLE DIAG\|MAHO SBR DIAG" maho-chromium/browser/ui/views/sidebar/
```

**Remove from:**
- `maho_sidebar_tab_list_view.cc::ToggleFolderExpanded` — restore to original 5-line body
- `maho_sidebar_view.cc::ScheduleRefreshAll`, `OnFavoritesModelReady`, `RefreshAll` — restore to original bodies

**Verification:** `grep` returns zero matches.

---

### Tasks DEFERRED (out of scope this cycle)

| ID | Task | Reason |
|---|---|---|
| **R2** | Lost tabs after restore | RCA's fix is dead code. Real bug is in `tab_registry.cc::Bootstrap` + `has_been_announced_`. Needs separate diagnostic round. **Tracked separately.** |
| **R4** | Loading spinner | Visual-only regression. Not blocking. Restore in follow-up if desired. |
| **N2** | `has_been_announced` retry on space_id hydration | Same root area as R2 — deferred. |
| **N3** | `expected_space_id` Rust deserializer parity | Verify in separate Rust pass. |
| **N4** | `IsSidebarIneligibleUrl` removal parity | Spot check during QA. |
| **N5** | `cycle_space_cache_` re-entrancy CHECK | Defensive, low priority. |
| **N6** | Space header cold-start flicker | Cosmetic, low priority. |

---

## 3. Execution Order

```
[F3] Diagnostic LOG capture (BLOCKING)
        │
        ├──[F1] R3 URL overlay (independent, parallel-safe)
        ├──[F2] R5 SpaceHeaderRow (independent, parallel-safe)
        └──[F4] N1 FFI thread race (independent, apply regardless)
        │
        ▼
   F3 outcome → matching R1 fix
        │
        ├──[F-R1-A] Coalesce race fix (if Scenario X)
        └──[manual debug] If Scenario Y/Z, escalate per Oracle
        │
        ▼
   [F6] Popup re-enable (after R1 confirmed)
        │
        ▼
   [F8] Diagnostic cleanup (last)
        │
        ▼
   Final build + Manual QA pass
```

**Recommended sequence:**
1. F1, F2, F4 in parallel (low risk, fast)
2. F3 build & capture LOG (blocking R1 fix)
3. F-R1-A (or escalate based on F3 outcome)
4. F6 popup restore
5. F8 LOG cleanup
6. Final build + manual QA

---

## 4. Manual QA Scenarios

| # | Scenario | Tool | Steps | Expected |
|---|---|---|---|---|
| 1 | Folder toggle (closed → open) | manual click | Single click on closed folder | Children appear inline within one frame |
| 2 | Folder toggle (open → closed) | manual click | Click expanded folder | Children disappear within one frame |
| 3 | URL overlay refresh (R3) | manual nav | Open YouTube, click into video | Sidebar URL reflects video URL not homepage |
| 4 | Hover popup non-empty | manual hover | Hover non-empty folder, wait 225ms | Popup with full tab list appears |
| 5 | Hover popup empty | manual hover | Hover empty folder | No popup |
| 6 | SpaceHeaderRow drop rejection (R5) | manual drag | Drag normal tab to space header | No highlight, drop rejected, tab returns to origin |
| 7 | Drag tab to folder | manual drag | Drag tab onto folder body | Tab enters folder, sidebar updates |
| 8 | No FFI panic (N1) | manual stress | Rapid drag + toggle + tab-add for ~30s | No `BorrowMutError` in stderr |
| 9 | No diagnostic LOG output | terminal | Launch with `2>&1 \| grep MAHO DIAG` | Zero output |
| 10 | Build clean | command | `python3 maho-chromium/build/scripts/build_maho.py` | Exit 0, dylib refreshed |

---

## 5. Risks & Open Decisions

| Item | Risk | Mitigation |
|---|---|---|
| F-R1-A might not be the right fix | F3 LOG capture confirms before applying | F3 is BLOCKING |
| F4 (N1) UI-thread FFI may add latency | Favorites read is small (fixed slot count, sub-ms typical) | Measure if jank reported |
| F6 popup re-enable may resurrect popup-related bugs | Popup feature recently fixed (Combo C) | Existing manual QA scenarios cover this |
| R2 deferral leaves data-loss risk for restored tabs | Real bug is elsewhere; RCA's fix is dead code | Tracked separately, needs its own diagnostic pass |

**Open decisions before execution:**
- ✅ R5: Option A — remove ALL drop overrides (per Oracle)
- ✅ R4: SKIP this cycle (visual only, low priority)
- ⚠️ R1 fix branch: depends on F3 outcome — DO NOT APPLY F-R1-A blindly

---

## 6. Out of Scope

- R2 / N2 (lost-tabs restore bug) — separate plan
- N3-N6 — follow-up polish
- Hover popup architectural redesign
- Auto-scroll / scroll wrap / space header refactoring
