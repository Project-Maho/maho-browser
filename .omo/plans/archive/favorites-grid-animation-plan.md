# Favorites Grid Animation — Arc-Style FLIP Implementation Plan

**Created:** 2026-05-08
**Target:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc/.h`
**Goal:** Arc Browser 수준의 insertion/reorder/removal 애니메이션을 Chromium Views 네이티브로 구현

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│ MahoSidebarFavoritesGridView                                │
│                                                             │
│  ┌─────────────────────────────────────────────────┐        │
│  │ FavoritesAnimationController (NEW)              │        │
│  │                                                 │        │
│  │  views::BoundsAnimator  bounds_animator_        │        │
│  │  (use_transforms=true, 220ms, EASE_IN_OUT)     │        │
│  │                                                 │        │
│  │  Methods:                                       │        │
│  │   AnimateReorder(old_bounds, new_bounds, ids)   │        │
│  │   AnimateInsertion(tile, target_bounds)         │        │
│  │   AnimateRemoval(tile, callback)               │        │
│  │   AnimateDragPreview(preview_index)            │        │
│  │   CancelAll()                                  │        │
│  └─────────────────────────────────────────────────┘        │
│                                                             │
│  ┌──────┐ ┌──────┐ ┌──────┐  ... (kTileCount tiles)       │
│  │Tile 0│ │Tile 1│ │Tile 2│  SetPaintToLayer()            │
│  └──────┘ └──────┘ └──────┘                                │
└─────────────────────────────────────────────────────────────┘
```

---

## Dependency Graph (Parallel Execution Map)

```
[Stream A: Infrastructure]          [Stream B: Animation Logic]         [Stream C: Integration]
         │                                    │                                  │
    ┌────▼────┐                               │                                  │
    │ A1: Add │                               │                                  │
    │ layers  │                               │                                  │
    │ to tiles│                               │                                  │
    └────┬────┘                               │                                  │
         │                          ┌─────────▼──────────┐                       │
    ┌────▼─────────┐                │ B1: Create         │                       │
    │ A2: Add      │                │ AnimationController │                       │
    │ BoundsAnim-  │                │ class (header)      │                       │
    │ ator member  │                └─────────┬──────────┘                       │
    └────┬─────────┘                          │                                  │
         │                          ┌─────────▼──────────┐                       │
         │                          │ B2: Implement       │                       │
         │                          │ AnimateInsertion()  │◄──── Can start       │
         │                          └─────────┬──────────┘      independently    │
         │                          ┌─────────▼──────────┐                       │
         │                          │ B3: Implement       │                       │
         │                          │ AnimateRemoval()    │                       │
         │                          └─────────┬──────────┘                       │
         │                          ┌─────────▼──────────┐                       │
         │                          │ B4: Implement       │                       │
         │                          │ AnimateReorder()    │                       │
         │                          └─────────┬──────────┘                       │
         │                                    │                                  │
         └──────────────┬─────────────────────┘                                  │
                        │ (merge point)                                           │
              ┌─────────▼──────────┐                                             │
              │ C1: Integrate into │                                              │
              │ Update() method    │                                              │
              └─────────┬──────────┘                                             │
              ┌─────────▼──────────┐                                             │
              │ C2: Integrate into │                                              │
              │ OnDragUpdated()    │                                              │
              └─────────┬──────────┘                                             │
              ┌─────────▼──────────┐                                             │
              │ C3: Edge cases +   │                                              │
              │ tests              │                                              │
              └────────────────────┘
```

**Parallelizable pairs:**
- `A1 + B1` — 완전 독립 (header vs .cc)
- `A2 + B2 + B3` — A2는 member 추가, B2/B3는 standalone function body
- `B2 + B3 + B4` — 각각 독립적인 animation 함수 (서로 호출하지 않음)

---

## Stream A: Infrastructure (Tile Layer Setup)

### A1: Add PaintToLayer to Tile Views

**File:** `maho_sidebar_favorites_grid_view.cc` — constructor (line ~286-334)

**What:** 각 FavoriteTileButton에 compositor layer를 추가하여 transform 기반 animation이 가능하도록.

**Changes:**
```cpp
// In constructor loop, after creating tile:
tile->SetPaintToLayer();
tile->layer()->SetFillsBoundsOpaquely(false);
```

**Validation:** 빌드 성공 + 기존 레이아웃 동일 (layer만 추가, visual 차이 없음)

---

### A2: Add BoundsAnimator Member + Snapshot Array

**File:** `maho_sidebar_favorites_grid_view.h`

**What:**
```cpp
#include "ui/views/animation/bounds_animator.h"
#include "ui/views/animation/bounds_animator_observer.h"

// In private section:
views::BoundsAnimator bounds_animator_{this, /*use_transforms=*/true};
std::array<gfx::Rect, kTileCount> snapshot_bounds_ = {};
std::array<std::string, kTileCount> snapshot_tab_ids_ = {};
bool animation_in_progress_ = false;
```

**File:** `maho_sidebar_favorites_grid_view.cc` — constructor tail

**What:**
```cpp
bounds_animator_.SetAnimationDuration(base::Milliseconds(220));
bounds_animator_.set_tween_type(gfx::Tween::EASE_IN_OUT);
```

**Validation:** 빌드 성공, 동작 변화 없음

---

## Stream B: Animation Logic (Controller Functions)

### B1: Animation Controller — Class Skeleton

이 프로젝트는 별도 클래스 추출 대신 `MahoSidebarFavoritesGridView` 내부에 private methods로 구현.
(타일 배열과 BoundsAnimator에 직접 접근해야 하므로)

**File:** `maho_sidebar_favorites_grid_view.h` — private section 추가

```cpp
// Animation helpers
void SnapshotCurrentBounds();
void AnimateReorder();
void AnimateInsertion(size_t tile_index);
void AnimateRemoval(size_t tile_index, base::OnceClosure on_complete);
void AnimateDragPreview(int preview_index, const std::string& dragged_tab_id);
void CancelAnimations();

// Helpers
int FindSnapshotIndexForTabId(const std::string& tab_id) const;
bool IsNewItem(const std::string& tab_id) const;
bool IsRemovedItem(size_t old_index) const;
```

**Validation:** 컴파일 성공 (빈 함수 body로)

---

### B2: AnimateInsertion() — Scale-In + Fade

**File:** `maho_sidebar_favorites_grid_view.cc`

**Logic:**
1. Target tile의 layer를 opacity=0, scale=0.7로 초기화
2. `views::AnimationBuilder`로 opacity=1, scale=1로 220ms ease-out
3. 동시에 다른 tiles는 `BoundsAnimator`로 새 위치로 슬라이드

```cpp
void MahoSidebarFavoritesGridView::AnimateInsertion(size_t tile_index) {
  if (tile_index >= kTileCount || !tile_views_[tile_index]) return;

  views::View* tile = tile_views_[tile_index];
  if (!tile->layer()) return;

  // Initial state: invisible + scaled down
  tile->layer()->SetOpacity(0.0f);
  gfx::Transform scale_from;
  const gfx::Rect bounds = tile->bounds();
  const float center_x = bounds.width() / 2.0f;
  const float center_y = bounds.height() / 2.0f;
  scale_from.Translate(center_x, center_y);
  scale_from.Scale(0.7f, 0.7f);
  scale_from.Translate(-center_x, -center_y);
  tile->layer()->SetTransform(scale_from);

  // Animate to final state
  views::AnimationBuilder()
      .Once()
      .SetDuration(base::Milliseconds(220))
      .SetOpacity(tile->layer(), 1.0f, gfx::Tween::EASE_OUT)
      .SetTransform(tile->layer(), gfx::Transform(), gfx::Tween::EASE_OUT);
}
```

**Depends on:** A1 (tiles must have layers)
**Validation:** 새 favorite 추가 시 시각적으로 scale-in 확인

---

### B3: AnimateRemoval() — Scale-Out + Fade + Callback

**File:** `maho_sidebar_favorites_grid_view.cc`

**Logic:**
1. Target tile의 layer를 현재 상태에서 opacity=0, scale=0.7로 180ms ease-in
2. 완료 시 callback 호출 → visibility false + transform reset

```cpp
void MahoSidebarFavoritesGridView::AnimateRemoval(
    size_t tile_index,
    base::OnceClosure on_complete) {
  if (tile_index >= kTileCount || !tile_views_[tile_index]) {
    if (on_complete) std::move(on_complete).Run();
    return;
  }

  views::View* tile = tile_views_[tile_index];
  if (!tile->layer() || !tile->GetVisible()) {
    if (on_complete) std::move(on_complete).Run();
    return;
  }

  gfx::Transform scale_to;
  const gfx::Rect bounds = tile->bounds();
  const float cx = bounds.width() / 2.0f;
  const float cy = bounds.height() / 2.0f;
  scale_to.Translate(cx, cy);
  scale_to.Scale(0.7f, 0.7f);
  scale_to.Translate(-cx, -cy);

  views::AnimationBuilder()
      .Once()
      .SetDuration(base::Milliseconds(180))
      .SetOpacity(tile->layer(), 0.0f, gfx::Tween::EASE_IN)
      .SetTransform(tile->layer(), scale_to, gfx::Tween::EASE_IN)
      .OnEnded(base::BindOnce(
          [](views::View* v, base::OnceClosure cb) {
            v->SetVisible(false);
            v->layer()->SetOpacity(1.0f);
            v->layer()->SetTransform(gfx::Transform());
            if (cb) std::move(cb).Run();
          },
          base::Unretained(tile), std::move(on_complete)));
}
```

**Depends on:** A1 (tiles must have layers)
**Validation:** favorite 제거 시 scale-out 후 사라짐

---

### B4: AnimateReorder() — FLIP Pattern (Core)

**File:** `maho_sidebar_favorites_grid_view.cc`

**Logic:**
1. `snapshot_bounds_` / `snapshot_tab_ids_`에 이전 상태가 저장된 상태
2. 현재 layout이 이미 새 위치로 배치된 상태 (layout rebuild 완료 후 호출)
3. 각 tile에 대해: old bounds → new bounds mapping 후 BoundsAnimator로 animate

```cpp
void MahoSidebarFavoritesGridView::SnapshotCurrentBounds() {
  for (size_t i = 0; i < kTileCount; ++i) {
    if (tile_views_[i] && tile_views_[i]->GetVisible()) {
      snapshot_bounds_[i] = tile_views_[i]->bounds();
      snapshot_tab_ids_[i] = tile_tab_ids_[i];
    } else {
      snapshot_bounds_[i] = gfx::Rect();
      snapshot_tab_ids_[i].clear();
    }
  }
}

int MahoSidebarFavoritesGridView::FindSnapshotIndexForTabId(
    const std::string& tab_id) const {
  if (tab_id.empty()) return -1;
  for (size_t i = 0; i < kTileCount; ++i) {
    if (snapshot_tab_ids_[i] == tab_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void MahoSidebarFavoritesGridView::AnimateReorder() {
  bounds_animator_.Cancel();

  for (size_t i = 0; i < kTileCount; ++i) {
    if (!tile_views_[i] || !tile_views_[i]->GetVisible()) continue;
    if (tile_tab_ids_[i].empty()) continue;

    const int old_index = FindSnapshotIndexForTabId(tile_tab_ids_[i]);
    if (old_index < 0) continue;  // New item — handled by AnimateInsertion

    const gfx::Rect old_bounds = snapshot_bounds_[old_index];
    const gfx::Rect new_bounds = tile_views_[i]->bounds();

    if (old_bounds.IsEmpty() || old_bounds == new_bounds) continue;

    // INVERT: place tile at old position
    tile_views_[i]->SetBoundsRect(old_bounds);
    // PLAY: animate to new position
    bounds_animator_.AnimateViewTo(tile_views_[i], new_bounds);
  }
}
```

**Depends on:** A2 (BoundsAnimator member)
**Validation:** reorder 후 타일들이 이전 위치에서 새 위치로 슬라이드

---

### B5: AnimateDragPreview() — Live DnD Repositioning

**File:** `maho_sidebar_favorites_grid_view.cc`

**Logic:**
DnD 진행 중, 드래그 위치에 따라 나머지 타일들이 "빈자리를 만들며" 슬라이드.

```cpp
void MahoSidebarFavoritesGridView::AnimateDragPreview(
    int preview_index,
    const std::string& dragged_tab_id) {
  const size_t visible_count = GetVisibleFavoriteCount();
  if (visible_count == 0) return;

  // Calculate hypothetical bounds with a gap at preview_index
  const int item_count = static_cast<int>(visible_count);
  const auto ideal_bounds = GetTileBoundsForCount(item_count);

  int slot = 0;
  for (size_t i = 0; i < kTileCount; ++i) {
    if (!tile_views_[i] || !tile_views_[i]->GetVisible()) continue;
    if (tile_tab_ids_[i] == dragged_tab_id) {
      // Dragged tile stays hidden or at drag image position
      continue;
    }

    // Skip the preview_index slot (that's where dragged item would go)
    if (slot == preview_index) slot++;

    const gfx::Rect target = ideal_bounds[slot];
    if (tile_views_[i]->bounds() != target) {
      bounds_animator_.AnimateViewTo(tile_views_[i], target);
    }
    slot++;
  }
}
```

**Depends on:** A2 + B4's helpers (`GetTileBoundsForCount` already exists)
**Validation:** 드래그 중 타일들이 부드럽게 재배치

---

## Stream C: Integration into Existing Methods

### C1: Integrate into Update()

**File:** `maho_sidebar_favorites_grid_view.cc` — `Update()` method

**Current flow:**
```
Update() → RebuildGridRows() → set visibility/data → PreferredSizeChanged()
```

**New flow:**
```
Update() → SnapshotCurrentBounds()
         → RebuildGridRows()
         → set visibility/data
         → PreferredSizeChanged()
         → Detect changes:
             - New items → AnimateInsertion()
             - Reordered items → AnimateReorder()
             - Removed items → AnimateRemoval() (deferred visibility)
```

```cpp
void MahoSidebarFavoritesGridView::Update(
    const MahoSidebarFavoritesModel& model) {
  // === SNAPSHOT (FIRST) ===
  SnapshotCurrentBounds();
  const std::array<std::string, kTileCount> old_tab_ids = tile_tab_ids_;
  const size_t old_count = favorite_count_;

  // === LAYOUT REBUILD (LAST) ===
  favorite_count_ = std::min(model.items.size(), tile_views_.size());
  const bool has_items = !model.items.empty();
  const size_t visible_item_count = favorite_count_;
  RebuildGridRows(visible_item_count);
  content_view_->SetVisible(has_items);
  UpdateContainerOwnedPreferredHeight(visible_item_count);
  SetVisible(has_items || drag_reveal_active_);

  if (!has_items) {
    for (size_t i = 0; i < tile_views_.size(); ++i) {
      tile_views_[i]->SetVisible(false);
      tile_urls_[i] = GURL();
      tile_tab_ids_[i].clear();
    }
    return;
  }

  // ... existing tile data assignment ...
  const auto specs = BuildTileSpecs(model);
  // (assign tile_urls_, tile_tab_ids_, visibility, icons, etc.)

  // === ANIMATE (INVERT + PLAY) ===
  if (old_count == 0) return;  // First load — no animation

  bool has_reorder = false;
  for (size_t i = 0; i < kTileCount; ++i) {
    if (!tile_views_[i]->GetVisible() || tile_tab_ids_[i].empty()) continue;

    const int old_idx = FindSnapshotIndexForTabId(tile_tab_ids_[i]);
    if (old_idx < 0) {
      // NEW item
      AnimateInsertion(i);
    } else if (snapshot_bounds_[old_idx] != tile_views_[i]->bounds()) {
      has_reorder = true;
    }
  }

  if (has_reorder) {
    AnimateReorder();
  }

  // Removal: handled before layout rebuild (detect items in old but not in new)
  // Note: Removal requires deferred-delete pattern — see C1-REMOVAL below
}
```

**Depends on:** A1, A2, B1-B4 all complete
**Validation:** Full flow test — add/remove/reorder favorites, all animate

---

### C1-REMOVAL: Deferred Removal Pattern

Removal은 까다로움 — 타일을 animate-out 하는 동안 layout에서 공간을 차지해야 함.

**Approach:** "ghost slot" 패턴
1. 삭제할 타일을 즉시 제거하지 않고 fade-out animation 시작
2. Animation 완료 시 실제로 visibility false + layout rebuild

```cpp
// Before RebuildGridRows in Update():
std::vector<size_t> removed_indices;
for (size_t i = 0; i < old_count; ++i) {
  if (old_tab_ids[i].empty()) continue;
  bool still_exists = std::any_of(
      model.items.begin(), model.items.end(),
      [&](const auto& item) { return item.tab_id == old_tab_ids[i]; });
  if (!still_exists) {
    removed_indices.push_back(i);
  }
}

// After layout rebuild, trigger removal animations
for (size_t idx : removed_indices) {
  AnimateRemoval(idx, base::BindOnce(
      &MahoSidebarFavoritesGridView::OnRemovalAnimationComplete,
      weak_factory_.GetWeakPtr()));
}
```

---

### C2: Integrate into OnDragUpdated()

**File:** `maho_sidebar_favorites_grid_view.cc` — `OnDragUpdated()` method

**What:** 드래그 중 `preview_index`가 변경될 때마다 `AnimateDragPreview()` 호출

```cpp
int MahoSidebarFavoritesGridView::OnDragUpdated(
    const ui::DropTargetEvent& event) {
  // ... existing payload validation ...

  drag_over_drop_target_ = true;
  // ... existing border/indicator logic ...

  // NEW: Compute preview index and animate
  const int preview_index = ResolveDropIndex(
      event.location(), payload.node_id,
      payload.origin == SidebarDragOrigin::kFavorites);
  if (preview_index != current_drag_preview_index_) {
    current_drag_preview_index_ = preview_index;
    AnimateDragPreview(preview_index, payload.node_id);
  }

  return static_cast<int>(ui::mojom::DragOperation::kMove);
}
```

**New member:** `int current_drag_preview_index_ = -1;`

**Depends on:** B5 complete + C1 tested
**Validation:** 드래그 중 타일 이동 확인

---

### C3: Edge Cases + Cleanup

**Items:**
1. `OnDragExited()` — cancel preview animations, restore original positions
2. `GetDropCallback()` — animation complete 후 실제 reorder 이벤트 발행
3. `CancelAnimations()` — 모든 pending animation 즉시 완료 (layout resize 시)
4. `OnBoundsChanged()` — resize 시 animation cancel 후 즉시 rebuild
5. Rapid-fire updates — animation 진행 중 새 Update() 호출 시 `bounds_animator_.Cancel()` 먼저 실행

```cpp
void MahoSidebarFavoritesGridView::CancelAnimations() {
  bounds_animator_.Complete();  // Teleport to target bounds
  animation_in_progress_ = false;
  current_drag_preview_index_ = -1;
}

void MahoSidebarFavoritesGridView::OnBoundsChanged(
    const gfx::Rect& previous_bounds) {
  CancelAnimations();  // NEW: cancel before resize rebuild
  views::View::OnBoundsChanged(previous_bounds);
  if (bounds().size() != previous_bounds.size()) {
    RebuildGridRows(favorite_count_);
    UpdateContainerOwnedPreferredHeight(favorite_count_);
  }
}
```

---

## Timing & Easing Constants

```cpp
namespace {
// Animation durations (match Arc's feel: snappy but not abrupt)
constexpr base::TimeDelta kReorderDuration = base::Milliseconds(220);
constexpr base::TimeDelta kInsertionDuration = base::Milliseconds(220);
constexpr base::TimeDelta kRemovalDuration = base::Milliseconds(180);
constexpr base::TimeDelta kDragPreviewDuration = base::Milliseconds(180);

// Scale factors
constexpr float kInsertionStartScale = 0.7f;
constexpr float kRemovalEndScale = 0.7f;

// Easing
constexpr gfx::Tween::Type kReorderTween = gfx::Tween::EASE_IN_OUT;
constexpr gfx::Tween::Type kInsertionTween = gfx::Tween::EASE_OUT;
constexpr gfx::Tween::Type kRemovalTween = gfx::Tween::EASE_IN;
constexpr gfx::Tween::Type kDragPreviewTween = gfx::Tween::EASE_IN_OUT;
}  // namespace
```

---

## File Change Summary

| File | Changes | Stream |
|------|---------|--------|
| `maho_sidebar_favorites_grid_view.h` | +BoundsAnimator member, +snapshot arrays, +animation methods, +drag preview index | A2, B1 |
| `maho_sidebar_favorites_grid_view.cc` | +layer setup in ctor, +animation method bodies, +Update() rewrite, +OnDragUpdated() integration | A1, B2-B5, C1-C3 |
| `BUILD.gn` (sidebar) | +`"//ui/views/animation"` dep (if not already present) | A2 |

---

## Execution Plan (Optimal Parallel Schedule)

```
Time →  T0          T1          T2          T3          T4
        │           │           │           │           │
Agent1: [A1+A2: Infrastructure setup ──────]            │
        │           │           │           │           │
Agent2: [B1: Header declarations ──]        │           │
        │           [B2: AnimateInsertion ──]            │
        │           │           │           │           │
Agent3: │           [B3: AnimateRemoval ────]            │
        │           │           │           │           │
Agent4: │           [B4: AnimateReorder ────]            │
        │           │           │           │           │
        │           │           │     [MERGE: C1 Integration ─────]
        │           │           │           │           │
        │           │           │           [B5: DragPreview ──────]
        │           │           │           │           │
        │           │           │           │   [C2: DnD Integration]
        │           │           │           │           │
        │           │           │           │   [C3: Edge cases ───]
```

**Parallel slots:**
- **Slot 1 (T0):** A1+A2 (infra) | B1 (header) — 2 agents
- **Slot 2 (T1):** B2 | B3 | B4 — 3 agents (all independent function bodies)
- **Slot 3 (T2):** C1 (requires A+B merge) | B5 (independent)
- **Slot 4 (T3):** C2 + C3 (sequential, depends on C1+B5)

**Maximum parallelism: 3 agents simultaneously at Slot 2**

---

## Verification Checklist

- [ ] `autoninja -C out/Default chrome` — 빌드 성공
- [ ] 기존 favorites 표시 동작 유지 (regression 없음)
- [ ] Favorite 추가: scale-in + fade-in animation
- [ ] Favorite 제거: scale-out + fade-out, 나머지 슬라이드
- [ ] Favorite reorder (DnD): FLIP slide animation
- [ ] DnD 중: live preview (타일들이 gap 만들며 이동)
- [ ] Rapid update: animation 중 새 update 시 crash 없음
- [ ] Sidebar resize: animation cancel + 즉시 relayout
- [ ] Empty grid → drag reveal: 정상 동작 유지
- [ ] 기존 DnD dispatch 테스트 통과: `maho_sidebar_dnd_*_test.cc`

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Layer 메모리 증가 | kTileCount=9, 무시 가능. 필요시 on-demand layer creation |
| Animation 중 Update() 재진입 | `CancelAnimations()` 선행 호출로 state 정리 |
| Removal deferred-delete 중 dangling pointer | `weak_factory_` + bounds check in callback |
| BoundsAnimator + manual SetBoundsRect 충돌 | Animation 중에는 manual bounds 변경 금지, `IsAnimating()` guard |
| DnD preview + actual reorder 타이밍 | Drop 시 `CancelAnimations()` → 즉시 final layout → 새 AnimateReorder() |
