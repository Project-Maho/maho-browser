# Sidebar Pinned Drop Zone Flicker — Deeper Fix Plan (v2)

**Date:** 2026-06-08
**Supersedes:** Visual issues unaddressed by `sidebar-pinned-drop-zone-flicker-2026-06-07.md` (P0/P1 paint optimizations)
**Scope:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc/.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc/.h`
- Existing unit tests in `maho_sidebar_dnd_dispatch_unittest.cc`
- New tests (5 unit + 1 interactive)

---

## Why v1 Was Insufficient

The v1 plan addressed paint-level optimizations (UpdateAppearance dirty-check, callback gating, layout coalescing, SynthesizeMouseMoveEvent suppression). All P0/P1 items were correctly implemented. **However** v1 missed two deeper structural causes:

### Bug A — `Update()` resets state and rebuilds views mid-drag
`MahoSidebarTabListView::Update()` at line 2508 unconditionally sets `pinned_drop_lane_revealed_ = false` then calls `RebuildRows()` (line 2536), which destroys ALL child views (rows, lanes, drop targets) and recreates them. This is triggered by ANY TabStrip event during drag (favicon loading on a background tab, navigation, pin state changes) via the `OnTabStripModelChanged → ScheduleRefreshAll → RefreshAll → Update` chain.

**Primary harm is NOT the bool reset** — it is `RebuildRows()` tearing down live drop targets out from under the OS-level drag controller. On macOS this often papers over; on Linux/Win it's a `raw_ptr` UAF risk. v1's `is_drag_active_` flag only guarded `SynthesizeMouseMoveEvent` inside `RebuildRows`, not the tear-down itself.

### Bug B — Parent's `OnDragExited` fires when cursor enters child views (Chromium hit-test quirk)
When the cursor moves from `MahoSidebarTabListView`'s background area into a child `SidebarTabRowView` (which has its own `CanDrop()=true`), Chromium fires `OnDragExited` on the parent. The current code at line 2625–2628 calls `HideEmptyPinnedDropLane()` immediately, which:
1. Hides the pinned drop zone (`SetVisible(false)` + `InvalidateLayout()`)
2. The child's subsequent `OnDragUpdated` re-reveals it (because `pinned_drop_lane_revealed_` was just reset)
3. Net visible result: rapid hide → show cycle → **flicker**

Existence of `SidebarDropForwardingView` (line 207–250) acknowledges this problem partially, but only for `SidebarSectionDropTarget`, not for tab/folder rows.

### Framing corrections from Oracle (worth recording)
1. `GetDropCallback` does **not** fire during inter-child cursor transitions — only on actual drop. Earlier framing claiming it as a "flap source" was wrong. Real flaps only come from `OnDragExited` paths.
2. `MahoSidebarFavoritesGridView::OnDragExited` (line 1079) **also** collapses `drag_reveal_active_` — it just doesn't visibly bug because favorites tiles aren't independent drop targets. Don't blindly copy "the favorites pattern" — copy the *principle* (decouple reveal lifetime from `OnDragExited`).
3. External URL drags (from another window) have **no source `OnDragDone`**. End-of-session detection must include a sidebar-level fallback for "cursor walked out without dropping".

---

## Architectural Decision

**Make `is_drag_active_` strictly monotonic per OS drag session.** Set on first drag-target signal, cleared **only** on authoritative session end. With this invariant:
- `Update()` can simply skip-and-coalesce while the flag is true.
- `OnDragExited` no longer needs to hide the lane (session-end does).
- v1's `SynthesizeMouseMoveEvent` guard becomes more reliable.

### State Machine

| Transition | Trigger | Notes |
|---|---|---|
| `false → true` | `ForwardDragToAutoScroller(child, event)` — first `OnDragUpdated` of the session | Single set point; idempotent. |
| `true → false` | `OnDragSessionEnded()` — **only** path | Called from: (1) `MahoSidebarView::OnDragEnded` reached via `NotifySidebarDragEnded` from any source row's `OnDragDone`; (2) the top-level `MahoSidebarTabListView::GetDropCallback` lambda after drop logic; (3) `MahoSidebarView::OnDragExited` deferred-task fallback for external-drag-walks-out. |
| Never cleared by | `OnDragExited` of any view; `ForwardDragEnded` (becomes autoscroller-only) | This is the whole point — these flap on parent/child boundaries. |

### Update() Deferral Strategy: Single-Slot Coalesce

Store the *latest* `MahoSidebarTabListModel` in `pending_model_`, set `has_pending_update_ = true`, return early. Apply once on `OnDragSessionEnded()`. Memory-bounded (single struct slot), latest-wins, no UAF risk.

Rejected alternatives:
- **Queue all updates**: unbounded memory; all but last are stale.
- **Preserve flag only, allow RebuildRows**: doesn't solve view-tear-down UAF.
- **Drop entirely**: UI stays stale until next post-drag event — bad UX.

---

## Concrete Changes

### File: `maho_sidebar_tab_list_view.h`

```cpp
public:
  // Authoritative end-of-drag-session hook. Called from:
  // - MahoSidebarView::OnDragEnded (drag source's OnDragDone path)
  // - MahoSidebarTabListView::GetDropCallback (drop completion)
  // - MahoSidebarView::OnDragExited deferred fallback (external drags)
  // Idempotent: safe to call multiple times.
  void OnDragSessionEnded();

private:
  // Pending Update() captured during an active drag session.
  // Single-slot coalesce: only the latest model is preserved.
  MahoSidebarTabListModel pending_model_;
  raw_ptr<Browser> pending_browser_ = nullptr;
  bool has_pending_update_ = false;
```

### File: `maho_sidebar_tab_list_view.cc`

**1. Update() — defer during active drag** (line 2504–2540 region):

```cpp
void MahoSidebarTabListView::Update(const MahoSidebarTabListModel& model,
                                    Browser* browser) {
  if (is_drag_active_) {
    pending_model_ = model;          // overwrite; only latest matters
    pending_browser_ = browser;
    has_pending_update_ = true;
    return;                          // do NOT touch active_space_id_,
                                     // last_model_, pinned_drop_lane_revealed_,
                                     // or call RebuildRows().
  }
  // ... existing body unchanged from line 2505 onward ...
  browser_ = browser;
  active_space_id_ = model.active_space_id;
  pinned_drop_lane_revealed_ = false;
  PruneLocalTitleCache(model);
  // ...
  RebuildRows(model, browser);
}
```

**2. OnDragExited — remove hide call** (line 2625–2628):

```cpp
void MahoSidebarTabListView::OnDragExited() {
  // Intentionally do NOT call HideEmptyPinnedDropLane() here. Chromium fires
  // this when the cursor enters a child drop target (row/lane), causing
  // visible flicker. The lane is hidden by OnDragSessionEnded() at
  // authoritative session end.
  // Also intentionally do NOT call ForwardDragEnded(this) — that flag is
  // now monotonic per session, cleared only by OnDragSessionEnded.
}
```

**3. New OnDragSessionEnded** — appended after `HideEmptyPinnedDropLane`:

```cpp
void MahoSidebarTabListView::OnDragSessionEnded() {
  // Idempotent: safe to call multiple times within a session-end cascade.
  is_drag_active_ = false;
  HideEmptyPinnedDropLane();
  if (has_pending_update_) {
    has_pending_update_ = false;
    auto model = std::move(pending_model_);
    pending_model_ = MahoSidebarTabListModel{};   // explicit clear
    Browser* b = pending_browser_;
    pending_browser_ = nullptr;
    Update(model, b);   // recursion-safe: is_drag_active_ now false
  }
}
```

**4. ForwardDragEnded — make autoscroller-only** (line 2340–2348):

```cpp
void ForwardDragEnded(views::View* child) {
  for (views::View* v = child; v; v = v->parent()) {
    if (auto* list = views::AsViewClass<MahoSidebarTabListView>(v)) {
      list->OnAutoScrollDragEnded();
      // Intentionally NOT clearing is_drag_active_ here — that flag tracks
      // the OS drag session, not per-target enter/exit. Cleared in
      // OnDragSessionEnded() only.
      return;
    }
  }
}
```

**5. Top-level GetDropCallback — call OnDragSessionEnded after drop logic** (line 2569+):

```cpp
return base::BindOnce(
    [](base::WeakPtr<MahoSidebarTabListView> self,
       Browser* browser, std::string space_id,
       base::RepeatingCallback<void(const std::string&)>
           favorites_drop_accepted,
       const ui::DropTargetEvent& event,
       ui::mojom::DragOperation& output_drag_op,
       std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
      // ... existing drop logic ...
      if (self) self->OnDragSessionEnded();
    },
    weak_factory_.GetWeakPtr(), browser_, active_space_id_,
    favorites_drop_accepted_callback_);
```

Note: only the **top-level** `MahoSidebarTabListView::GetDropCallback` needs this. Per-section / per-row drop callbacks need not — they're terminated by either the source's `OnDragDone` path (which reaches `MahoSidebarView::OnDragEnded`) or by their own callback's drop logic having no drag continuation.

### File: `maho_sidebar_view.cc/.h`

**1. OnDragEnded — call into tab list** (line 855):

```cpp
void MahoSidebarView::OnDragEnded() {
  if (favorites_view_) favorites_view_->OnDragEnded();
  if (drag_image_controller_) drag_image_controller_->OnDragEnded();
  if (tab_list_view_) tab_list_view_->OnDragSessionEnded();   // NEW
}
```

**2. OnDragExited — deferred fallback for external-drag-walks-out** (around line 846):

```cpp
void MahoSidebarView::OnDragExited() {
  if (favorites_view_) {
    favorites_view_->OnDragExited();
    if (!favorites_view_->IsDropAccepted()) favorites_view_->CollapseFromDrag();
  }
  // Deferred check for external drag walks-out. If a drag-target child
  // re-fires OnDragUpdated before this task runs, is_drag_active_ stays
  // true and we keep the lane visible. Otherwise we end the session.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoSidebarView::MaybeEndDragSessionIfCursorOutside,
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarView::MaybeEndDragSessionIfCursorOutside() {
  if (!tab_list_view_) return;
  // Hit-test current cursor screen position against widget bounds.
  views::Widget* widget = GetWidget();
  if (!widget) return;
  const gfx::Point cursor =
      display::Screen::GetScreen()->GetCursorScreenPoint();
  const gfx::Rect widget_bounds = widget->GetWindowBoundsInScreen();
  if (!widget_bounds.Contains(cursor)) {
    tab_list_view_->OnDragSessionEnded();
  }
}
```

Add to header: `void MaybeEndDragSessionIfCursorOutside();` (private).

---

## Interaction With v1 P0/P1 Fix

**Strictly compatible — strengthens the prior fix.**

| v1 fix | Interaction |
|---|---|
| `UpdateAppearance()` dirty-check (3 classes) | Unchanged. Orthogonal. |
| Reveal callback gating (`!IsPinnedDropLaneRevealed()`) | Now near-trivially a no-op during drag — the flag stays `true` for the whole session, so callbacks early-exit. |
| `UpdatePinnedDropLaneVisibility` coalesce | Unchanged. Still useful when the lane truly toggles on session start/end. |
| `SynthesizeMouseMoveEvent` `is_drag_active_` guard | More reliable now that `is_drag_active_` is monotonic. No flaps in mid-drag. |

---

## Test Strategy

### Existing Tests — Update Required

| Test | File | Line | Change |
|---|---|---|---|
| `EmptyPinnedLaneHidesAfterDragExit` | `maho_sidebar_dnd_dispatch_unittest.cc` | 1204 | After `OnDragExited()`, lane will **stay visible** (correct new behavior). Test must call `list.OnDragSessionEnded()` before asserting `is_pinned_drop_lane_revealed_for_testing() == false`. Rename to `EmptyPinnedLaneHidesAfterSessionEnd`. |
| `UpdateResetsRevealedPinnedDropLane` | `maho_sidebar_dnd_dispatch_unittest.cc` | 1349 | If test sets `is_drag_active_ = false` first then calls `Update`, behavior unchanged. If test simulates active drag, must use new pending-update path. Verify which behavior is intended; likely needs a sibling test for "Update during drag is deferred". |
| `UpdateWithNewPinnedContentClearsRevealFlag` | `maho_sidebar_dnd_dispatch_unittest.cc` | 1377 | Same as above — verify drag-active state, may need to split into two tests. |
| `DragTabToPinnedSection` | `maho_sidebar_dnd_interactive_test.cc` | 55 | Should pass unchanged. Drop completion path now also calls `OnDragSessionEnded()`. Verify `is_drag_active_` is `false` after drop. |
| `DragCancelRestoresState` | `maho_sidebar_dnd_interactive_test.cc` | 416 | Should pass unchanged. ESC cancel path triggers `OnDragDone` → `MahoSidebarView::OnDragEnded` → `OnDragSessionEnded`. |
| Other 8 lane-related tests | `maho_sidebar_dnd_dispatch_unittest.cc` | 1090–1377 | Audit each: most just exercise `RevealEmptyPinnedDropLane` while not in a drag, so unchanged. |

### New Tests — Required

In `maho_sidebar_dnd_dispatch_unittest.cc`:

1. **`UpdateDeferredDuringDragAppliedOnSessionEnd`**
   - Set `is_drag_active_ = true` via simulated `OnDragUpdated`.
   - Call `Update(model_v1)`. Assert `last_model_` unchanged, no `RebuildRows`.
   - Call `Update(model_v2)`. Assert `pending_model_` reflects v2 (latest wins).
   - Call `OnDragSessionEnded()`. Assert UI reflects v2, `pending_model_` cleared, `is_drag_active_ == false`.

2. **`DropLaneSurvivesParentChildTransition`**
   - Reveal lane via `OnDragUpdated` on the tab list.
   - Fire a child row's `OnDragExited()` immediately followed by the same row's `OnDragUpdated()`.
   - Assert lane stayed `revealed == true` across the transition.

3. **`IsDragActiveMonotonicAcrossSiblingTransitions`**
   - Fire two adjacent rows' `OnDragExited`/`OnDragUpdated` interleaved (4 events total).
   - Assert `is_drag_active()` returns `true` continuously throughout.
   - Call `OnDragSessionEnded()`. Assert `false`.

4. **`OnDragSessionEndedIsIdempotent`**
   - Call twice in succession. Second is a no-op (no crash, no double-Update).

5. **`UpdateDuringDragDoesNotMutateBrowserOrSpaceId`**
   - Set `is_drag_active_ = true`. Capture initial `browser_` and `active_space_id_`.
   - Call `Update(model_with_different_space)`.
   - Assert `browser_` and `active_space_id_` unchanged. Assert `pending_browser_` and pending model captured.

In `maho_sidebar_dnd_interactive_test.cc`:

6. **`PinnedLaneSurvivesFaviconLoadDuringDrag`**
   - Start a drag session.
   - Synthetically fire `OnTabChangedAt` 3 times (simulates favicon loads).
   - During each, verify lane stays revealed.
   - Complete drop. Verify pending model applied at session end.

---

## Risk Assessment

| Risk | Likelihood | Severity | Mitigation |
|---|---|---|---|
| External drag walks out → lane stuck visible | Medium | Low | Deferred PostTask hit-test in `MahoSidebarView::OnDragExited`. Self-heals on next `Update()` regardless. |
| `OnDragDone` not called for some drag-source corner case (capture lost) | Low | Medium | If observed in QA: add sentinel watchdog timer (~30s `is_drag_active_` auto-reset). Don't add preemptively. |
| Deferred `Update()` causes user-visible staleness during drag | Low | Low | Drag durations measured in seconds. Only one model snapshot replayed at end. Worst case: stale favicon for ≤ drag duration. Acceptable. |
| Existing tests break (intentional regressions) | High | None | `EmptyPinnedLaneHidesAfterDragExit` and 1–2 others encode the buggy invariant. Update is mechanical. Land test updates in same commit. |
| `pending_model_` value-type lifetime issues | Low | High (UAF) | `MahoSidebarTabListModel` must be a value type (verify struct definition has no embedded `raw_ptr` to short-lived objects). If it does, switch to `std::optional<MahoSidebarTabListModel>`. |
| `MahoSidebarView::OnDragExited` deferred PostTask races with new drag start | Low | Low | Inside `MaybeEndDragSessionIfCursorOutside`, hit-test cursor against widget bounds. If cursor is back inside, the new `OnDragUpdated` already re-set `is_drag_active_ = true`, so `OnDragSessionEnded` would clear it incorrectly. **Add an additional check**: don't end session if the new model `is_drag_active_` is already `true` (i.e., a child re-entered). Practical implementation: check `tab_list_view_->is_drag_active() && cursor_inside`. |

---

## Implementation Order (3 Commits)

This grouping enables `git bisect` to isolate any regression:

### Commit 1: Refactor `is_drag_active_` to be monotonic
- `ForwardDragEnded` no longer touches the flag (autoscroller-only).
- Add `OnDragSessionEnded()` method (initially: just clear flag and call `HideEmptyPinnedDropLane`, no pending-update logic yet).
- Wire `MahoSidebarView::OnDragEnded` to call `tab_list_view_->OnDragSessionEnded()`.
- Wire top-level `MahoSidebarTabListView::GetDropCallback` lambda to call `OnDragSessionEnded()`.
- Add deferred fallback in `MahoSidebarView::OnDragExited` (the `MaybeEndDragSessionIfCursorOutside` helper).
- Run all tests. Most pass without change. Update `EmptyPinnedLaneHidesAfterDragExit` test.

### Commit 2: `Update()` deferral
- Add `pending_model_`, `pending_browser_`, `has_pending_update_` members.
- Modify `Update()` to early-return-and-stash when `is_drag_active_`.
- Modify `OnDragSessionEnded()` to apply pending update.
- Add new tests #1, #5 from §Test Strategy.

### Commit 3: Decouple drop-lane lifetime from `OnDragExited`
- Remove `HideEmptyPinnedDropLane()` call from `MahoSidebarTabListView::OnDragExited()`.
- Add new tests #2, #3, #4, #6 from §Test Strategy.

Each commit independently revertable. If the third commit breaks something specific (e.g., interactive QA shows lane stays after a true exit), revert just that commit and keep the deferral fix.

---

## Effort Estimate

| Phase | Effort |
|---|---|
| Code changes (3 files, ~120 lines) | 3 hours |
| Existing test fix-ups (5–7 tests) | 3 hours |
| New tests (5 unit + 1 interactive) | 4 hours |
| Manual QA (pin/unpin, favicon flood, ESC cancel, external URL drop, multi-monitor) | 2 hours |
| **Total** | **~12 hours / 1.5 days** |

---

## Out of Scope

- `MahoSidebarFavoritesGridView::OnDragExited` collapse path (line 1079) shares the same anti-pattern but doesn't visibly bug because favorites tiles aren't independent drop targets. Clean up in a follow-up if needed.
- Bonus issue from the analysis: `ActivateDragPlaceholder` calling `RebuildGridRows` mid-drag without preceding `CancelAnimations()` (favorites-only, separate stutter bug). Track separately.
- Watchdog timer for `is_drag_active_` cleanup if `OnDragDone` is missed. Add only if real-world QA reveals the case.

---

## Acceptance Criteria

1. ✅ Drag a regular tab toward an empty pinned section. Drop indicator appears smoothly without flicker as cursor crosses tab/row boundaries.
2. ✅ During drag, simulate a favicon load on a background tab (e.g., open a URL in another tab during drag, or use test hook). Drop indicator stays visible; no view tear-down occurs.
3. ✅ Drop the tab in the pinned drop zone. Tab is pinned. Drop zone collapses. UI reflects post-drop model state.
4. ✅ ESC during drag → drop zone collapses, no stale state.
5. ✅ External URL drop into normal section → drop zone behaves correctly, no leak.
6. ✅ All existing tests in `maho_sidebar_dnd_dispatch_unittest.cc` and `maho_sidebar_dnd_interactive_test.cc` pass (after updates listed in §Test Strategy).
7. ✅ All 6 new tests pass.
