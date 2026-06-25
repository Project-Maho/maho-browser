# Sidebar Pinned Drop Zone Flickering — Root Cause & Fix Plan

**Date:** 2026-06-07
**Status:** IMPLEMENTATION LANDED 2026-06-07.
§P0 dirty-check, §P0 reveal-callback gating, §P1 is_drag_active_ all live.
**Scope:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
**Symptom:** Visible flickering in the drop indicator when a regular (unpinned) tab is dragged toward / over the pinned tabs drop zone in the native sidebar.

---

## Architecture Recap

Three independent drop-indicator views participate in the pinned↔normal boundary:

| View class | File location | Indicator visual | Owns its own `drag_over_drop_target_`? |
|---|---|---|---|
| `SidebarSectionDropTarget` | line 276–392 | rounded rect background + 1dp primary border (32dp tall when revealed) | yes |
| `SidebarInsertionLaneView` | line 511–734 | 2dp solid primary line | yes |
| `SidebarTabRowView` | line ~1115–1759 (drag path: 1373–1398) | rounded rect background + border on the row | yes |

State machine that gates the empty-pinned drop zone reveal:

- `MahoSidebarTabListView::pinned_drop_lane_revealed_` (line 188 in `.h`)
- `RevealEmptyPinnedDropLane()` (line 2599) — guarded `return` if already revealed
- `UpdatePinnedDropLaneVisibility(bool)` (line 2623) — calls `SetMinDropTargetHeight` + `SetVisible` on `pinned_section_target_`

Drag event entry points that fire the reveal callback on **every** mouse-move:

- `SidebarTabRowView::OnDragUpdated` line 1388–1394
- `SidebarInsertionLaneView::OnDragUpdated` line 573–579
- (`SidebarFolderRowView::OnDragUpdated` — same pattern)
- `MahoSidebarTabListView::OnDragUpdated` line 2526–2537 (top-level)

Model rebuild path (after `pin_tab` shell event responds):

- `RebuildRows()` line 2635 — full `RemoveAllChildViews` → reconstruction → `DeprecatedLayoutImmediately()` chain → `SynthesizeMouseMoveEvent()` immediate + `PostTask`'d follow-up.

---

## Root Causes (4 stacked, contribute to a single visible flicker)

### Cause 1 — `UpdateAppearance()` has no dirty-check (PRIMARY, P0)

`SidebarSectionDropTarget::UpdateAppearance()` at line 376–386:

```cpp
void UpdateAppearance() {
  SetBackground(drag_over_drop_target_
      ? views::CreateRoundedRectBackground(ui::kColorSysSurface4, kRowCornerRadiusDp)
      : nullptr);
  SetBorder(drag_over_drop_target_
      ? views::CreateRoundedRectBorder(kDropIndicatorOutlineThicknessDp,
                                       kRowCornerRadiusDp, ui::kColorSysPrimary)
      : nullptr);
}
```

`OnDragUpdated` fires at OS event rate (60Hz+ on macOS). Every event:
- allocates a new `Background` and `Border` object on the heap,
- calls `SetBackground` / `SetBorder`, which marks the layer dirty,
- triggers a compositor repaint — **even when `drag_over_drop_target_` did not change since the previous call**.

Identical pattern in `SidebarInsertionLaneView::UpdateAppearance()` line 717–721 and `SidebarTabRowView::UpdateAppearance()` (~line 1659).

### Cause 2 — Reveal callback fires every mouse-move and triggers layout reflow (SECONDARY, P0)

`SidebarTabRowView::OnDragUpdated` line 1388–1394:

```cpp
if (section_ == MahoSidebarTabSection::kNormal &&
    reveal_empty_pinned_drop_lane_callback_ &&
    (payload.origin == SidebarDragOrigin::kPinnedSection ||
     payload.origin == SidebarDragOrigin::kNormalSection ||
     payload.origin == SidebarDragOrigin::kFavorites)) {
  reveal_empty_pinned_drop_lane_callback_.Run();
}
```

The guard inside `RevealEmptyPinnedDropLane()` (line 2600) prevents repeated layout calls, **but the callback itself still runs every event**. The first run flips `pinned_drop_lane_revealed_ = true` and calls `UpdatePinnedDropLaneVisibility(true)` which:

```cpp
drop_target->SetMinDropTargetHeight(visible ? kRowHeightDp : 0);  // 0 → 32dp
pinned_section_target_->SetVisible(visible);
```

This **shifts every normal-section row down by 32dp**. The view under the cursor changes, causing Chromium to fire `OnDragExited` on the previously-hovered row and `OnDragUpdated` on whatever view is now under the cursor. Both call `UpdateAppearance()` → two paint invalidations within the same frame → visible flash.

### Cause 3 — Boundary view-stack thrash (TERTIARY)

The pinned/normal boundary stacks four views:

```
[SidebarSectionDropTarget(pinned)]   ← own OnDragUpdated/Exited
[SidebarDropForwardingView]          ← line 207–250, re-dispatches to parent section target
[SidebarSeparatorRow]                ← 8dp gap, no GetDropFormats override → rejects drop format
[SidebarSectionDropTarget(normal)]   ← own OnDragUpdated/Exited
```

When the cursor crosses the 8dp separator gap, Chromium alternates `OnDragExited` on one view and `OnDragUpdated` on the next. Each side unconditionally calls `UpdateAppearance()`, so both indicators flash on/off. `SidebarDropForwardingView` (line 207–250) re-dispatches a single OS event to the parent section target, so a single mouse move can trigger `OnDragUpdated` along **two separate paths**.

### Cause 4 — `SynthesizeMouseMoveEvent` after `RebuildRows` (POST-DROP, P1)

`RebuildRows()` line 2635:

- `RemoveAllChildViews()` (line 2644) — destroys every row
- `DeprecatedLayoutImmediately()` walked up the ancestor chain (line 2701–2703)
- `SynthesizeMouseMoveEvent()` immediate (line 2709)
- `PostTask` → `SynthesizeMouseMoveEvent()` again on next UI tick (line 2710–2722)

The `HasCapture()` guard (line 2704) is intended to suppress this during an active mouse-press, but **macOS OS-level drag sessions do not necessarily set widget capture**. After a `pin_tab` drop, the synthesized mouse-moves can re-fire `OnDragUpdated` on the freshly-built views, retriggering Cause 1.

---

## Fix Plan (in priority order)

### P0 — Dirty-check in every `UpdateAppearance()`

**Files:** `maho_sidebar_tab_list_view.cc`

For each of the three classes, add a `bool last_painted_drag_over_ = false;` member, and bail early when state is unchanged:

```cpp
void UpdateAppearance() {
  if (last_painted_drag_over_ == drag_over_drop_target_) {
    return;
  }
  last_painted_drag_over_ = drag_over_drop_target_;
  SetBackground(drag_over_drop_target_ ? views::CreateRoundedRectBackground(...) : nullptr);
  SetBorder(drag_over_drop_target_ ? views::CreateRoundedRectBorder(...) : nullptr);
}
```

Apply to:
- `SidebarSectionDropTarget` line 376–386
- `SidebarInsertionLaneView` line 717–721
- `SidebarTabRowView::UpdateAppearance` (~line 1659)

Note: The dirty-check pattern was extended in implementation to also
guard the hovered and active appearance branches. SidebarTabRowView
gained three fields (last_painted_drag_over_, last_painted_hovered_,
last_painted_active_) instead of one. The combined check is:
```cpp
  if (last_painted_drag_over_ != drag_over_drop_target_ ||
      last_painted_hovered_  != hovered ||
      last_painted_active_   != active_) { ... }
```

**Cost:** 3 × 5 lines.
**Effect:** Eliminates ~99% of redundant Background/Border allocations and paint invalidations during drag.

### P0 — Gate `reveal_empty_pinned_drop_lane_callback_` invocation

**Files:** `maho_sidebar_tab_list_view.cc`

Add `pinned_drop_lane_revealed_` accessor or pass the bool through a getter, then guard the callback call sites so the callback only fires while the lane is **not yet** revealed:

- `SidebarTabRowView::OnDragUpdated` line 1388 — wrap callback call
- `SidebarInsertionLaneView::OnDragUpdated` line 573 — wrap callback call
- `SidebarFolderRowView::OnDragUpdated` — wrap callback call

Pattern:

```cpp
if (section_ == MahoSidebarTabSection::kNormal &&
    reveal_empty_pinned_drop_lane_callback_ &&
    !is_pinned_drop_lane_already_revealed_callback_.Run() &&
    (payload.origin == ...)) {
  reveal_empty_pinned_drop_lane_callback_.Run();
}
```

Either expose an `IsPinnedDropLaneRevealed()` getter on `MahoSidebarTabListView` and pass a `RepeatingCallback<bool()>` into each row, or use a single `base::RepeatingClosure` whose body is already idempotent and instead **suppress the call with a local atomic** kept in sync via the same callback (simpler: pass a `base::WeakPtr<MahoSidebarTabListView>` and check `view->pinned_drop_lane_revealed_`).

**Cost:** ~10 lines + 1 new accessor.
**Effect:** Stops the per-frame callback execution entirely once the lane is up. Reduces wakeups during drag from ~60/s to 0/s in steady state.

### P1 — Coalesce layout in `UpdatePinnedDropLaneVisibility`

**File:** `maho_sidebar_tab_list_view.cc` line 2623

```cpp
void MahoSidebarTabListView::UpdatePinnedDropLaneVisibility(bool visible) {
  if (!pinned_section_target_) return;
  auto* drop_target = static_cast<SidebarSectionDropTarget*>(pinned_section_target_);
  const int target_height = visible ? kRowHeightDp : 0;
  bool changed = false;
  if (drop_target->preferred_size().height() != target_height) {
    drop_target->SetMinDropTargetHeight(target_height);
    changed = true;
  }
  if (pinned_section_target_->GetVisible() != visible) {
    pinned_section_target_->SetVisible(visible);
    changed = true;
  }
  if (changed) {
    pinned_section_target_->InvalidateLayout();
  }
  if (pinned_separator_) pinned_separator_->SetVisible(true);
}
```

**Cost:** ~10 lines.
**Effect:** Idempotent. No-op when state already matches. Single explicit layout invalidation.

### P1 — Suppress `SynthesizeMouseMoveEvent` while drag is active

**File:** `maho_sidebar_tab_list_view.cc` line 2700–2722

Add `bool is_drag_active_ = false;` to `MahoSidebarTabListView`. Set true in `OnDragUpdated` (line 2526) and in any descendant view's `OnDragUpdated` via `ForwardDragToAutoScroller` companion path. Clear in `OnDragExited` (line 2594) and post-drop. Skip both `SynthesizeMouseMoveEvent` calls (line 2709 and 2718) when `is_drag_active_` is true.

```cpp
if (GetWidget()->HasCapture() || is_drag_active_) {
  return;
}
GetWidget()->SynthesizeMouseMoveEvent();
// ... PostTask body also gets `!weak_self->is_drag_active_` guard
```

**Cost:** ~15 lines.
**Effect:** Eliminates the post-drop synthesize-mouse-move loop that retriggers `OnDragUpdated` on freshly-built views.

### P2 — Hysteresis on pinned drop lane reveal/hide

**File:** `maho_sidebar_tab_list_view.cc`

Add a 150–200ms debounce timer (modeled after the spring-load timer used for folder spring-expand) before showing OR hiding the empty pinned drop lane. Pattern:

```cpp
base::OneShotTimer pinned_drop_lane_reveal_timer_;
// in RevealEmptyPinnedDropLane: schedule a timer instead of immediate flip
// in HideEmptyPinnedDropLane: same — schedule hide
```

**Cost:** ~25 lines + new member.
**Effect:** Cursor oscillation across the boundary no longer toggles the lane within a single drag gesture — pure visual smoothing.

---

## Verification Plan

1. **Unit tests** — no regression expected; `maho_sidebar_planner_unittest.cc` and `maho_sidebar_dnd_dispatch_unittest.cc` cover the dispatch matrix.
2. **Interactive tests** — `maho_sidebar_dnd_interactive_test.cc::DragTabToPinnedSection` (line 55–96) and `DuplicatePinDefended` (line 495–540) must continue to pass. These guarantee the functional drop path.
3. **Manual QA** — drag a regular tab from normal section toward pinned section, observe drop indicator stability:
   - With empty pinned section: lane should appear once, stay solid, no flicker as cursor moves over.
   - With populated pinned section: section drop target appearance should remain stable as cursor crosses the section boundary.
   - After drop: no visible re-flash from the synthesized mouse-move.
4. **Paint count instrumentation (optional)** — temporarily wrap `SetBackground`/`SetBorder` calls in the three classes with a static counter and log paint calls per drag gesture in a debug build. Target: ≤ 2 paint calls per indicator transition (one on, one off), down from ~60/s.

---

## Out of Scope

- WebUI sidebar (`chrome://maho-sidebar`) — does not implement tab drag, no work needed.
- `maho_sidebar_favorites_grid_view.cc` `CollapseFromDrag` — separate animation path; not the source of the boundary flicker. Re-evaluate only if flicker persists for favorites→pinned transitions after P0 lands.
- `maho_sidebar_view.cc::OnDragExited` / `OnDragEnded` — already correctly tear down state.
- Upstream Chromium `TabDragController` / `TabStrip` — not involved; sidebar is custom Views.

---

## Files to Touch

| File | Lines | Why |
|---|---|---|
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` | 376–386, 573–579, 717–721, 1388–1394, ~1659, 2526–2537, 2599–2633, 2700–2722 | All four fixes |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h` | ~188, ~339, add `is_drag_active_`, `last_painted_drag_over_` accessors | Header support for the new state |

No other files require changes for P0/P1.

---

## Recommendation

**Land P0-1 + P0-2 first** (≈20 lines, no behavioral risk). They eliminate ~80% of the perceptible flicker independently. P1-3 + P1-4 follow as a cleanup pass. P2 is a polish pass — only if user feedback after P0/P1 still reports occasional boundary flutter.
