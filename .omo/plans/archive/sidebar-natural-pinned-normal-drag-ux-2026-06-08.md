# Sidebar Pinned↔Normal Drag — Natural UX Implementation Plan

**Date:** 2026-06-08
**Scope:** All phases (P0 + P1 + cheap P2) implemented together in a single coordinated effort
**Predecessor:** `sidebar-pinned-drop-zone-flicker-deeper-fix-2026-06-08.md` (flicker eliminated; this plan adds polish on top)
**Effort:** ~20–32 hours total

---

## Why This Plan

The flicker fix made `is_drag_active_` monotonic and added `OnDragSessionEnded()`. The drag no longer breaks visually. But it still feels **abrupt**: source tab stays 100% opaque (looks duplicated), drop zone snaps in instantly, rows below jump 36dp without animation, drag image is static even when crossing section boundaries, dropped tab appears with no settle.

Goal: make pinned↔normal drag feel like Arc Browser — smooth, anticipatable, with clear cause-and-effect. All building blocks already exist in the codebase via the favorites grid (`views::AnimationBuilder`, `gfx::Tween`, `MahoSidebarDragImageController`); tab list just doesn't use them.

---

## Files Modified (all phases)

| File | Phase | Purpose |
|---|---|---|
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` | 1.1, 1.2, 1.3, 2.2, 3.1, 3.2 | Source fade, drop zone animation, row shift, drop settle, spring delay, a11y |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h` | 1.2, 2.2, 3.1 | Layer state, pending settle id, timer member |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_image_controller.cc` | 2.1 | Add kInPinnedZone detection + image swap |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_image_controller.h` | 2.1 | Extend Zone enum |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_image_factory.cc` | 2.1 | Add `BuildPinnedRowDragImage()` |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_image_factory.h` | 2.1 | Declare `BuildPinnedRowDragImage()` |

No GN file changes. No upstream Chromium file changes.

---

## Reference Patterns (READ BEFORE IMPLEMENTING)

`maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc`:

| Pattern | Line | Use For |
|---|---|---|
| `AnimateReorder()` (220ms `EASE_IN_OUT`, transform translate) | 1589–1629 | Phase 1.3 row shift |
| `AnimateInsertion()` (220ms `EASE_OUT`, opacity 0→1 + scale 0.7→1) | 1631–1656 | Phase 2.2 drop settle |
| `AnimateRemoval()` (180ms `EASE_IN`, opacity 1→0 + scale 1→0.7) | 1658–1701 | Phase 1.2 drop zone collapse |
| `AnimateDragPreview()` (180ms `EASE_IN_OUT`, transform translate) | 1703–1789 | Reference only |
| Source tile opacity hide: `layer()->SetOpacity(0.f)` | `WriteDragDataForView` 234–238 | Phase 1.1 |
| Source tile opacity restore: `layer()->SetOpacity(1.f)` | `OnDragDone` 273–277 | Phase 1.1 |
| Layer setup: `tile->SetPaintToLayer()` + `SetFillsBoundsOpaquely(false)` | 481–482 | All phases |
| Constants: `kReorderDuration`, `kInsertionDuration`, etc. | 111–115 | Use for consistency |
| Header import: `#include "ui/views/animation/animation_builder.h"` | 72 | Add to tab_list_view.cc |

**Use IDENTICAL patterns. Do NOT invent new animation primitives.** Match the favorites grid durations and easings exactly.

---

# PHASE 1 — Required (P0): ~6–10 hours

The "80% of perceived naturalness" tier. All three items use existing primitives, ship together.

## 1.1 Source Tab Row Fade

**Symptom:** Drag a tab — source row stays at 100% opacity. Floating drag image looks like a duplicate. Most jarring UX gap.

**Implementation:**

### `tab_list_view.cc` — `SidebarTabRowView` constructor (~line 1180)

Add at end of constructor:

```cpp
SetPaintToLayer();
layer()->SetFillsBoundsOpaquely(false);
```

### `tab_list_view.cc` — `SidebarTabRowView::WriteDragDataForView()` (~line 1680)

After existing drag image setup, BEFORE the function ends:

```cpp
if (layer()) {
  views::AnimationBuilder()
      .Once()
      .SetDuration(base::Milliseconds(120))
      .SetOpacity(layer(), 0.4f, gfx::Tween::EASE_OUT);
}
```

### `tab_list_view.cc` — `SidebarTabRowView::OnDragDone()` (search for or add this override)

```cpp
void SidebarTabRowView::OnDragDone() {
  if (layer()) {
    views::AnimationBuilder()
        .Once()
        .SetDuration(base::Milliseconds(120))
        .SetOpacity(layer(), 1.0f, gfx::Tween::EASE_OUT);
  }
  // Existing OnDragDone logic (e.g., NotifySidebarDragEnded) goes after.
}
```

### Same treatment for `SidebarFolderRowView` (~line 1900+)

Add `SetPaintToLayer()` + `SetFillsBoundsOpaquely(false)` in constructor; add opacity animation in its `WriteDragDataForView` and `OnDragDone`.

### `tab_list_view.cc` — header includes

Add at top alongside other view includes:

```cpp
#include "ui/views/animation/animation_builder.h"
```

**Why 0.4f, not 0.0f:** Tab rows have visible text titles. Users glance at them mid-drag to confirm identity. Favorites tiles are favicon-only, so they can fully hide. Match Arc Browser, not Chrome tab strip.

**Risks:**
- Layer is permanent for the row's lifetime — that's correct, no leak.
- `SetFillsBoundsOpaquely(false)` is critical for the rounded background to composite correctly with the layer.
- Verify hover restyle (`UpdateAppearance`) doesn't reset opacity.

## 1.2 Empty Pinned Drop Zone — Animated Reveal

**Symptom:** When the empty pinned drop zone reveals, it appears as an instant 36dp box with a primary border. Snaps in.

**Implementation:**

### `tab_list_view.cc` — `SidebarSectionDropTarget` constructor (~line 290)

```cpp
SetPaintToLayer();
layer()->SetFillsBoundsOpaquely(false);
layer()->SetOpacity(0.0f);
```

### `tab_list_view.cc` — `MahoSidebarTabListView::UpdatePinnedDropLaneVisibility(bool visible)` (~line 2821)

Replace the body. Pseudocode:

```cpp
void MahoSidebarTabListView::UpdatePinnedDropLaneVisibility(bool visible) {
  if (!pinned_section_target_) return;
  auto* drop_target = static_cast<SidebarSectionDropTarget*>(pinned_section_target_);

  if (visible) {
    // 1. Layout allocates space immediately (so row shift in 1.3 can capture old positions).
    drop_target->SetMinDropTargetHeight(kRowHeightDp);
    drop_target->SetVisible(true);
    if (pinned_separator_ && !pinned_separator_->GetVisible()) {
      pinned_separator_->SetVisible(true);
    }
    InvalidateLayout();

    // 2. Start with scale-Y(0) from top origin + opacity 0.
    auto* layer = drop_target->layer();
    gfx::Transform t;
    t.Scale(1.0f, 0.0f);
    layer->SetTransform(t);
    layer->SetOpacity(0.0f);

    // 3. Animate to identity over 180ms EASE_OUT.
    views::AnimationBuilder()
        .Once()
        .SetDuration(base::Milliseconds(180))
        .SetTransform(layer, gfx::Transform(), gfx::Tween::EASE_OUT)
        .SetOpacity(layer, 1.0f, gfx::Tween::EASE_OUT);
  } else {
    // Collapse: animate out, then SetVisible(false) to deallocate.
    auto* layer = drop_target->layer();
    gfx::Transform t;
    t.Scale(1.0f, 0.0f);

    views::AnimationBuilder()
        .OnEnded(base::BindOnce(
            [](base::WeakPtr<MahoSidebarTabListView> self) {
              if (!self) return;
              auto* dt = static_cast<SidebarSectionDropTarget*>(
                  self->pinned_section_target_);
              if (!dt) return;
              dt->SetMinDropTargetHeight(0);
              dt->SetVisible(false);
              self->InvalidateLayout();
            },
            weak_factory_.GetWeakPtr()))
        .Once()
        .SetDuration(base::Milliseconds(140))
        .SetTransform(layer, t, gfx::Tween::EASE_IN)
        .SetOpacity(layer, 0.0f, gfx::Tween::EASE_IN);
  }
}
```

**Critical:**
- Set transform origin to **top edge** (default is center; you need to set the layer's anchor or use a separate transform composition). Confirm the API; if Views doesn't expose origin directly, scale from a wrapper view that's anchored top.
- Don't try to interpolate `min_height_` per frame — synchronous `InvalidateLayout()` is too expensive at 60Hz.
- Border (1dp primary) on `SidebarSectionDropTarget` may distort during scale. If visible distortion occurs, suppress border until animation completes via `OnEnded` callback that sets the border, or render border on a separate sibling layer.

## 1.3 Section Layout Shift — Coupled with 1.2

**Symptom:** When pinned drop zone reveals, all rows below jump 36dp instantly. Phase 1.2 alone makes this WORSE because the drop zone scales in but rows don't move with it.

**Implementation:** In `UpdatePinnedDropLaneVisibility()`, coordinate row translation with drop zone reveal.

```cpp
void MahoSidebarTabListView::UpdatePinnedDropLaneVisibility(bool visible) {
  if (!pinned_section_target_) return;

  // Snapshot positions of all views BELOW the pinned separator.
  std::vector<views::View*> views_to_shift;
  bool past_separator = false;
  for (views::View* child : tab_rows_->children()) {
    if (child == pinned_separator_) {
      past_separator = true;
      continue;
    }
    if (past_separator) {
      views_to_shift.push_back(child);
    }
  }

  // Ensure each shifted view has a layer for transform animation.
  for (views::View* v : views_to_shift) {
    if (!v->layer()) {
      v->SetPaintToLayer();
      v->layer()->SetFillsBoundsOpaquely(false);
    }
  }

  if (visible) {
    // ... [existing reveal logic from 1.2] ...

    // Each row starts at translateY(-kRowHeightDp) and animates to identity.
    for (views::View* v : views_to_shift) {
      gfx::Transform start;
      start.Translate(0, -kRowHeightDp);
      v->layer()->SetTransform(start);
    }

    views::AnimationBuilder builder;
    auto sequence = builder.Once().SetDuration(base::Milliseconds(180));
    for (views::View* v : views_to_shift) {
      sequence = sequence.SetTransform(v->layer(), gfx::Transform(),
                                       gfx::Tween::EASE_OUT);
    }
  } else {
    // Reverse: rows animate from identity back to translateY(-kRowHeightDp),
    // matching the drop-zone collapse from 1.2.
    views::AnimationBuilder builder;
    auto sequence = builder.Once().SetDuration(base::Milliseconds(140));
    for (views::View* v : views_to_shift) {
      gfx::Transform end;
      end.Translate(0, -kRowHeightDp);
      sequence = sequence.SetTransform(v->layer(), end, gfx::Tween::EASE_IN);
    }
  }
}
```

**Coupling rule:** 1.2 and 1.3 MUST land in the same commit. They are visually inseparable.

**Risks:**
- Don't translate `tab_rows_` itself (the scroll content). Translate each child's layer.
- After collapse animation, reset transforms to identity on all shifted views (`OnEnded` callback) — otherwise next reveal breaks.
- Lazy `SetPaintToLayer()` for normal-tree row views adds layer cost; consider permanent layer in `SidebarTabRowView` constructor (already done by 1.1) and `SidebarFolderRowView` (already done by 1.1) — leaves only `SidebarInsertionLaneView` and `SidebarSeparatorRow` to handle here.

---

# PHASE 2 — Required (P1): ~10–16 hours

## 2.1 Zone-Aware Drag Image (Single Biggest "Arc-Like" Win)

**Symptom:** When user drags a normal tab into the pinned area, the drag image stays as a full-row tab card. No visual hint that "this will become pinned".

**Implementation:**

### `maho_sidebar_drag_image_controller.h` — extend Zone enum

```cpp
enum class Zone {
  kInitial,
  kInTabZone,
  kInEssentialsZone,
  kInPinnedZone,    // NEW
  kCrossWindowOut
};
```

Add member:

```cpp
gfx::ImageSkia pinned_image_;
```

### `maho_sidebar_drag_image_controller.cc` — `DetectZone()` (line 63–83)

Add pinned detection. Walk hit_view's ancestors:

```cpp
Zone DragImageController::DetectZone(views::View* hit_view) const {
  // Existing kCrossWindowOut detection first.
  if (!hit_view) return Zone::kCrossWindowOut;

  // Existing kInEssentialsZone detection.
  for (views::View* v = hit_view; v; v = v->parent()) {
    if (views::AsViewClass<MahoSidebarFavoritesGridView>(v)) {
      return Zone::kInEssentialsZone;
    }
  }

  // NEW: kInPinnedZone detection.
  for (views::View* v = hit_view; v; v = v->parent()) {
    if (auto* drop_target = views::AsViewClass<SidebarSectionDropTarget>(v)) {
      if (drop_target->section_for_testing() == MahoSidebarTabSection::kPinned) {
        return Zone::kInPinnedZone;
      }
    }
    // Also check pinned section ancestors (rows inside pinned section).
    // Walk up looking for the section_container_ for kPinned.
  }

  return Zone::kInTabZone;
}
```

**Order matters**: `kCrossWindowOut > kInEssentialsZone > kInPinnedZone > kInTabZone`. Don't reorder existing zones.

### `maho_sidebar_drag_image_factory.h/.cc` — add `BuildPinnedRowDragImage()`

Mirror `BuildEssentialsDragImage()` (drag_image_factory.cc:115–161). Header:

```cpp
gfx::ImageSkia BuildPinnedRowDragImage(const TabSnapshot& snapshot,
                                        SkColor theme_accent);
```

Implementation:
- Same square card style as essentials, but sized to `kRowHeightDp` (~36dp).
- Just the favicon, centered.
- Theme accent border.
- No title text.

### `maho_sidebar_drag_image_controller.cc` — `RebuildImagesForCurrentTheme()`

Build `pinned_image_` from the same source snapshot:

```cpp
pinned_image_ = BuildPinnedRowDragImage(snapshot, theme_accent_);
```

### `maho_sidebar_drag_image_controller.cc` — `SwapImageForCurrentZone()` (line 103–133)

Add the kInPinnedZone case:

```cpp
case Zone::kInPinnedZone:
  if (widget_ && !pinned_image_.isNull()) {
    widget_->UpdateDragImage(pinned_image_, source_->original_offset);
  }
  break;
```

### `maho_sidebar_drag_image_controller.cc` — `TransitionTo()` (line 85–101)

Use existing 50ms revert timer pattern. Instant entry into kInPinnedZone, 50ms revert delay when leaving (matches existing essentials pattern):

```cpp
if (next_zone == Zone::kInPinnedZone || next_zone == Zone::kInEssentialsZone) {
  current_zone_ = next_zone;
  revert_timer_.Stop();
  SwapImageForCurrentZone();
} else if (current_zone_ == Zone::kInPinnedZone || current_zone_ == Zone::kInEssentialsZone) {
  // Defer revert by 50ms.
  revert_timer_.Start(...);
}
```

**Risks:**
- The drag image is OS-managed; you cannot animate within it. Only swap.
- `pinned_image_` must be ready BEFORE the cursor enters the zone. Build it in `RebuildImagesForCurrentTheme` at drag start.
- Don't try to morph mid-flight (Arc does this with WebKit; Views `UpdateDragImage` is a discrete swap, repeated calls flicker on macOS).

## 2.2 Drop Settle Animation

**Symptom:** After drop, `RebuildRows` blows away all child views. New tab appears instantly. No "landed here" feedback.

**Blocking constraint:** `RebuildRows` (tab_list_view.cc:2854) destroys the tab-id → view mapping. To animate "the dropped tab landed here", we need identity preservation across the rebuild.

**Implementation:**

### `tab_list_view.h` — add member

```cpp
private:
  std::string pending_drop_settle_tab_id_;
```

### `tab_list_view.cc` — top-level `MahoSidebarTabListView::GetDropCallback` (~line 2715)

Capture the dropped tab id BEFORE calling `OnDragSessionEnded()`:

```cpp
if (self) {
  if (payload.node_kind == SidebarNodeKind::kTab) {
    self->pending_drop_settle_tab_id_ = payload.node_id;
  }
  self->OnDragSessionEnded();
}
```

Same for `SidebarTabRowView::GetDropCallback` and `SidebarInsertionLaneView::GetDropCallback` paths that result in successful tab placement (pin/unpin/reorder).

### `tab_list_view.cc` — `RebuildTreeNodes` (~line 2986)

When creating a `SidebarTabRowView`, check if its tab_id matches the pending settle id:

```cpp
if (node.tab_id == pending_drop_settle_tab_id_) {
  row_view->layer()->SetOpacity(0.0f);
  gfx::Transform start;
  start.Scale(0.95f, 0.95f);
  row_view->layer()->SetTransform(start);

  views::AnimationBuilder()
      .Once()
      .SetDuration(base::Milliseconds(220))
      .SetOpacity(row_view->layer(), 1.0f, gfx::Tween::EASE_OUT)
      .SetTransform(row_view->layer(), gfx::Transform(), gfx::Tween::EASE_OUT);
}
```

### `tab_list_view.cc` — `RebuildRows` (~line 2854)

After RebuildRows completes its main work, clear the pending id:

```cpp
pending_drop_settle_tab_id_.clear();
```

### `tab_list_view.cc` — `SynthesizeMouseMoveEvent` defer (line 2919, 2931)

The synthetic mouse move triggers hover restyle which can clobber the settling layer. Defer it:

```cpp
const bool settle_pending = !pending_drop_settle_tab_id_.empty();
const auto delay = settle_pending ? base::Milliseconds(240) : base::TimeDelta();
base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
    FROM_HERE,
    base::BindOnce(
        [](base::WeakPtr<MahoSidebarTabListView> self) {
          if (!self || !self->GetWidget() || self->is_drag_active_) return;
          if (self->GetWidget()->IsVisible() && !self->GetWidget()->HasCapture()) {
            self->GetWidget()->SynthesizeMouseMoveEvent();
          }
        },
        weak_factory_.GetWeakPtr()),
    delay);
```

**Risks:**
- Dropped tab's row needs `SetPaintToLayer()` — already added by Phase 1.1 to `SidebarTabRowView` constructor.
- If the dropped tab doesn't appear in the new model (e.g., moved to another window), gracefully no-op. Just clear the pending id.
- Don't refactor `RebuildRows` to diff-based — too risky given the recent flicker fix.

---

# PHASE 3 — Cheap Items (P2): ~3–4 hours

These are low-cost additions that round out the experience. Heavy P2 items (will-pin silhouette, cursor magnetism, fly-back-to-source) are explicitly DEFERRED — see "Out of Scope" below.

## 3.1 Spring-Loading Delay on Empty Pinned Reveal

**Symptom:** Cursor swiping through normal section accidentally triggers the empty pinned reveal.

**Implementation:**

### `tab_list_view.h` — add timer member

```cpp
private:
  base::OneShotTimer pinned_lane_reveal_timer_;
```

### `tab_list_view.cc` — modify reveal callback wiring

In `SidebarTabRowView::OnDragUpdated` (~line 1420) and `SidebarInsertionLaneView::OnDragUpdated`, instead of calling `reveal_empty_pinned_drop_lane_callback_.Run()` directly:

```cpp
if (section_ == MahoSidebarTabSection::kNormal &&
    reveal_empty_pinned_drop_lane_callback_ &&
    is_pinned_drop_lane_already_revealed_callback_ &&
    !is_pinned_drop_lane_already_revealed_callback_.Run() &&
    /* origin matches */) {
  start_reveal_timer_callback_.Run();   // 250ms delay
}
```

Where `start_reveal_timer_callback_` is wired by `MahoSidebarTabListView::RebuildTreeNodes` to:

```cpp
base::BindRepeating(
    [](MahoSidebarTabListView* self) {
      if (self->pinned_lane_reveal_timer_.IsRunning()) return;
      self->pinned_lane_reveal_timer_.Start(
          FROM_HERE, base::Milliseconds(250),
          base::BindOnce(&MahoSidebarTabListView::RevealEmptyPinnedDropLane,
                         base::Unretained(self)));
    },
    base::Unretained(this));
```

### `tab_list_view.cc` — cancel timer on drag exit

In `SidebarTabRowView::OnDragExited` and `SidebarInsertionLaneView::OnDragExited`, call a parent helper that stops the timer:

```cpp
if (cancel_reveal_timer_callback_) cancel_reveal_timer_callback_.Run();
```

In `MahoSidebarTabListView::OnDragSessionEnded`, also stop the timer:

```cpp
pinned_lane_reveal_timer_.Stop();
```

## 3.2 A11y Announcement on Lane Reveal

**Symptom:** Screen reader users get no signal when the empty pinned drop zone reveals.

**Implementation:**

### `tab_list_view.cc` — `RevealEmptyPinnedDropLane()` (~line 2783)

After `pinned_drop_lane_revealed_ = true;`:

```cpp
if (pinned_section_target_) {
  pinned_section_target_->GetViewAccessibility().NotifyEvent(
      ax::mojom::Event::kExpandedChanged, /*send_native_event=*/true);
}
```

In `HideEmptyPinnedDropLane()`:

```cpp
if (pinned_section_target_) {
  pinned_section_target_->GetViewAccessibility().NotifyEvent(
      ax::mojom::Event::kExpandedChanged, /*send_native_event=*/true);
}
```

Add accessible name + role on `SidebarSectionDropTarget` if not already set:

```cpp
GetViewAccessibility().SetRole(ax::mojom::Role::kRegion);
GetViewAccessibility().SetName(u"Pinned tabs drop area");
```

---

# OUT OF SCOPE (Explicitly Deferred / Skipped)

| Item | Reason |
|---|---|
| Insertion lane animation (2dp line scale-in) | Precision-targeting UI; animation creates ambiguity at boundaries. Both Chrome tab strip and Arc keep this instant. |
| Phantom silhouette in destination section | High cost (~6–8h), low ROI. Drag image change of Phase 2.1 conveys ~70% of the same signal. |
| Cursor magnetism / snap | Overkill, breaks precision targeting. |
| Cancel "fly-back to source" animation | macOS `NSDraggingSession` owns this. Faking via Views post-drag widget is brittle and inconsistent with OS-native cancellation everywhere else in Chrome. |
| Refactor `RebuildRows` to diff-based reconciler | Too risky given recent flicker fix. Worth considering as a separate future project (~2–3 days) if you want full reorder/insert/exit animations. |

---

# Cross-Cutting Constraints

1. **Don't break existing tests.** After each phase, run:
   - `maho_sidebar_dnd_dispatch_unittest.cc`
   - `maho_sidebar_dnd_interactive_test.cc`
   - The 5 new tests added by the flicker fix
2. **Don't touch GN files.** If a GN issue appears, STOP and report.
3. **Don't modify upstream Chromium files** (`chromium/src/...`). Use `chromium_src/` override pattern only.
4. **Don't suppress type errors.** Never `static_cast` to silence warnings. Fix root cause.
5. **Layer cleanup invariant:** Permanent layers added in row constructors (Phase 1.1) persist for row lifetime — that's correct. Per-drag layers (e.g., transient lane animations) MUST be torn down in `OnDragSessionEnded()`. Accumulating layers across drags silently leaks GPU memory and degrades scroll performance.
6. **`is_drag_active_` invariants:** The flag is monotonic per OS drag session. Don't read `pinned_drop_lane_revealed_` outside drag for animation state. The mid-drag rebuild guard at `Update()` (line 2629) must continue to short-circuit any model-driven `Update` during animation.
7. **macOS DnD constraint:** Drag image is OS-owned surface. All swaps go through `widget_->UpdateDragImage()`. Do NOT animate the drag image itself via local layer transforms.
8. **Accessibility:** Layer opacity does not affect screen-reader announcements (good). Lane reveal/hide must fire `AXEvent::kExpandedChanged` (Phase 3.2).
9. **Comments:** Project policy says no unnecessary comments. The few comments needed for non-obvious DnD invariants (e.g., explaining `OnAutoScrollDragEnded()` placement) are acceptable. Match the existing comment style of `tab_list_view.cc`.
10. **Timing standardization:**
    - 120ms `EASE_OUT` — source row fade in/out
    - 140ms `EASE_IN` — drop zone collapse
    - 180ms `EASE_OUT` — drop zone reveal, row shift on reveal
    - 220ms `EASE_OUT` — drop settle animation
    - 250ms — spring-load delay
    - 50ms — drag image revert delay (existing, unchanged)

---

# Implementation Order (Single Coordinated Effort)

This is one delivery. Phases exist for risk control but should land together. Suggested commit order for clean bisect:

1. **Commit 1: Phase 1.1** — Source row fade. Add layer + opacity animation.
2. **Commit 2: Phase 1.2 + 1.3 (coupled)** — Drop zone reveal animation + row shift translate-Y. Same commit; visually inseparable.
3. **Commit 3: Phase 2.1** — Zone-aware drag image (kInPinnedZone + BuildPinnedRowDragImage).
4. **Commit 4: Phase 2.2** — Drop settle animation with `pending_drop_settle_tab_id_` plumbing.
5. **Commit 5: Phase 3.1** — Spring-load delay.
6. **Commit 6: Phase 3.2** — A11y announcements.

Each commit must build cleanly and pass existing tests. Phase 1.1 alone is shippable polish; Phases 1.2+1.3 must ship together; Phases 2.1, 2.2, 3.1, 3.2 are independent.

After each commit, run:

```bash
python3 maho-chromium/build/scripts/build_maho.py
```

from `/Users/indo/code/project/maho-workspace`. If build fails for reasons clearly related to your changes, fix; if unrelated (e.g., upstream files, GN), STOP and report.

---

# Acceptance Criteria

**Visual scenarios (manual QA after all phases):**

1. **Drag normal tab toward empty pinned section:**
   - Source row dims to 40% opacity (Phase 1.1)
   - Empty pinned drop zone fades + scales in from top edge over 180ms (Phase 1.2)
   - Normal-section rows below slide down 36dp smoothly over same 180ms, synchronized with drop zone reveal (Phase 1.3)
   - When cursor enters pinned section, drag image visibly changes shape to icon-only square (Phase 2.1)

2. **Drop completes:**
   - Deferred model update applies (existing flicker fix behavior)
   - Dropped tab fades + scales in at its landing spot in the pinned section over 220ms (Phase 2.2)
   - SynthesizeMouseMoveEvent is delayed 240ms so hover restyle doesn't clobber the settle (Phase 2.2)

3. **Drag and ESC cancel:**
   - Drop zone collapses smoothly (140ms EASE_IN, Phase 1.2)
   - Source row fades back to opacity 1.0 over 120ms (Phase 1.1)

4. **Cursor swiping through empty pinned area without intent:**
   - No spurious reveal — 250ms delay protects (Phase 3.1)

5. **Screen reader user (VoiceOver/NVDA):**
   - Lane reveal/hide announces as expand/collapse state change (Phase 3.2)

**Test scenarios (unit + interactive):**

- All existing `maho_sidebar_dnd_dispatch_unittest.cc` tests pass.
- All existing `maho_sidebar_dnd_interactive_test.cc` tests pass.
- The 5 unit tests + 1 interactive test added by the flicker fix continue to pass.
- Final build succeeds: `python3 maho-chromium/build/scripts/build_maho.py` exit 0.

---

# Effort Summary

| Phase | Duration | Items |
|---|---|---|
| Phase 1 | 6–10 h | Source fade (2h), drop zone reveal anim (3h), row shift coupled (3h), QA (2h) |
| Phase 2 | 10–16 h | Zone-aware drag image (6h), drop settle (6h), synth-mouse defer (1h), test verification (2h) |
| Phase 3 | 3–4 h | Spring-load delay (2h), A11y events (1h), verification (1h) |
| **Total** | **~20–32 h** | All phases together |

---

# Optional Future Work

1. **Refactor `RebuildRows` to diff-based reconciler** keyed by `tab_id`. Unlocks proper enter/exit/move animations without the brittle settle-id plumbing in Phase 2.2. Estimated 2–3 days. Worth it only if you also want intra-section reorder animations.

2. **Extract `AnimationBuilder` patterns into shared `MahoSidebarMotion` helper.** Currently durations and easings are duplicated between favorites grid and tab list. Centralize as constants module. Low priority unless adding a third drag/animation surface.
