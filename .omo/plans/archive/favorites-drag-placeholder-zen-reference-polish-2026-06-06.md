# Favorites Drag Placeholder — Zen-Reference Polish

**Generated:** 2026-06-06
**Owner:** sidebar / favorites grid
**Reference:** [zen-browser/desktop](https://github.com/zen-browser/desktop) — `src/zen/workspaces/ZenWorkspaceIcons.mjs::initDragAndDrop`
**Files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.{h,cc}`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` (drag source)
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_util.{h,cc}` (drag image helper)

---

## Reference behavior — Zen

Zen workspace icons reorder uses raw mouse events (no native HTML5 D&D):

```js
// ZenWorkspaceIcons.mjs lines 32-86 (excerpted)
const mouseMoveHandler = (moveEvent) => {
  if (Math.abs(moveEvent[clientPos] - dragStart) > 5) {
    this.isReorderMode = true;
  }
  if (this.isReorderMode) {
    const tabs = [...this.children];
    const mouse = moveEvent[clientPos];
    for (const tab of tabs) {
      if (tab === draggedTab) continue;
      const rect = tab.getBoundingClientRect();
      if (mouse > rect.top && mouse < rect.bottom) {
        if (mouse < rect.top + rect.height / 2) {
          this.insertBefore(draggedTab, tab);
        } else {
          this.insertBefore(draggedTab, tab.nextSibling);
        }
      }
    }
  }
};
```

**핵심 패턴**:
1. dragged element 자체를 DOM `insertBefore` 로 cursor 위치에 *즉시* 이동 (no animation lag)
2. OS drag image **사용 안 함** — DOM element 가 cursor 따라가서 시각 단서가 됨
3. 다른 children 의 시각 자리 변동은 CSS transition (frame-rate, native ease)
4. Drop indicator border 같은 외곽선 **없음** — element 위치 자체가 indicator

---

## Our current state

Native Chromium views D&D 사용. drag 시 OS drag image (source view 의 thumbnail) 가 cursor 따라감. 별도로 favorites grid 안에 placeholder tile 표시.

**관찰된 문제** (사용자 보고):
1. cursor 이동 시 placeholder 가 즉시 안 따라감 (180ms transform animation lag)
2. OS drag image 가 source row 의 thumbnail (텍스트 "dev community" 포함) 그대로 — cursor 옆에 잔재처럼 보임
3. Drop indicator 박스 (`kColorSysPrimary` border) 가 grid 전체 둘러쌈 — Zen 처럼 placeholder 만 시각 단서로 충분

---

## Plan

### Step 1 — Placeholder cursor follow latency 0

**위치**: `maho_sidebar_favorites_grid_view.cc::AnimateDragPreview` (line 1613-1700) 의 placeholder branch (line 1636-1649).

**변경**: placeholder tile (`i == favorite_count_`) 의 transform 만 instant set (no animation). 다른 tile 들의 슬라이드 애니메이션 (180ms) 은 그대로.

```cpp
// AnimateDragPreview, after computing `target_slot` for placeholder tile:
if (placeholder_active_ && i == favorite_count_) {
  const float dx = static_cast<float>(target.x() - current_origin.x());
  const float dy = static_cast<float>(target.y() - current_origin.y());
  gfx::Transform offset;
  offset.Translate(dx, dy);
  // Instant — placeholder MUST follow cursor without lag.
  tile_views_[i]->layer()->GetAnimator()->StopAnimating();
  tile_views_[i]->layer()->SetTransform(offset);
  continue;  // skip the AnimationBuilder block below
}
// existing AnimationBuilder block for non-placeholder tiles
```

**효과**: cursor 이동 시 placeholder 가 frame-by-frame 즉시 따라감. 다른 tile 들은 부드럽게 양보.

**검증**: 기존 unittest (`PlaceholderMovesOnPreviewIndexChange`) 가 transform 변화는 verify 하지만 animation duration 은 안 봄 — pass 그대로. 추가로 `tile_views_[favorite_count_]->layer()->GetTargetTransform()` 이 즉시 일치하는지 verify 하는 테스트 권장.

### Step 2 — OS drag image 단순화 (favicon-only)

**위치**: drag source. sidebar tab row 의 drag start. 보통 `WriteDragDataForView` 또는 비슷한 메서드. 후보 파일:
- `maho_sidebar_tab_list_view.cc` 안 `SidebarTabRowView::WriteDragDataForView` (또는 비슷)
- `maho_sidebar_drag_util.{h,cc}`

**현재 동작 추정**: drag image = source view 의 자체 capture (`CreateSidebarDragImage(sender)` 또는 비슷). 큰 row thumbnail + label text 포함.

**변경**: drag image 를 favicon-only ImageSkia 로 set. Chromium views 의 `OSExchangeData::SetDragImage(...)` API 사용:

```cpp
// In source's WriteDragDataForView(...):
gfx::ImageSkia favicon_image = ResolveFaviconImageSkiaForTab(tab);
const int kDragImageSizeDp = 32;  // small icon
gfx::ImageSkia scaled = gfx::ImageSkiaOperations::CreateResizedImage(
    favicon_image, skia::ImageOperations::RESIZE_BEST,
    gfx::Size(kDragImageSizeDp, kDragImageSizeDp));
data->provider().SetDragImage(scaled, gfx::Vector2d(kDragImageSizeDp / 2, kDragImageSizeDp / 2));
```

**효과**: cursor 옆에 작은 favicon 만 표시. text 잔재 없음.

**위험**: 모든 sidebar drag 의 image 가 favicon-only 가 됨 (탭 영역 안 reorder 도 포함). 사용자 의도가 이거면 OK. 만약 favorites grid 진입 시에만 변경 원한다면 Chromium SetDragImage 는 drag 시작 시만 가능 — runtime 변경 어려움.

**대안**: drag image 를 transparent (1x1 투명 이미지) 로 만들고 grid 안 placeholder 가 시각 단서 역할. cursor 옆에 아무것도 안 보임. Zen 의 "DOM element 가 cursor 따라감" 패턴과 비슷한 효과.

```cpp
gfx::ImageSkia transparent = gfx::ImageSkia::CreateFrom1xBitmap(
    SkBitmap{});  // empty 1x1
data->provider().SetDragImage(transparent, gfx::Vector2d(0, 0));
```

권장: **transparent drag image** + **grid 안 placeholder 가 cursor 따라감** (Step 1 의 instant transform). Zen 패턴과 가장 가까움.

### Step 3 — Drop indicator 박스 제거

**위치**: `maho_sidebar_favorites_grid_view.cc::OnDragUpdated` (line 1056-1063 부근).

**현재 코드**:
```cpp
if (drag_accepted_tab_id_.empty() && (favorite_count_ > 0 || placeholder_active_)) {
  SetBorder(views::CreateSolidSidedBorder(
      gfx::Insets::TLBR(kDropIndicatorThicknessDp, kDropIndicatorInsetDp,
                        kDropIndicatorThicknessDp, kDropIndicatorInsetDp),
      ui::kColorSysPrimary));
}
```

**변경**: 이 SetBorder 호출 제거. Zen 처럼 placeholder + sliding tiles 만 시각 단서로 사용.

```cpp
// SetBorder(...kColorSysPrimary) 호출 제거
// 다른 SetBorder(nullptr) 호출은 그대로 (drag exit 시 정리)
```

**효과**: drag 중 grid 외곽 박스 없음. placeholder 가 어디 들어갈지 직접 보여줌.

**부작용**: drop target 자체 시각 단서 약해짐. 다만 favorites grid 자체에 들어왔다는 단서는 placeholder 활성화 (i.e., grid 가 N+1 layout 으로 reveal) 자체. 충분.

### Step 4 — 시각 polish

#### 4.1 Placeholder의 dim border (이미 적용됨)

`ActivateDragPlaceholder` (line 1283-1285) 가 favicon dominant color 의 alpha 128 (50%) 로 border 설정. Zen 의 dragged element 시각 강조와 비슷.

#### 4.2 다른 tile 들의 슬라이드 duration

현재 180ms ease-in-out. Zen 의 CSS transition 속도와 비교: Zen 은 `--zen-tab-transition-duration: 0.15s` (추정). 우리 180ms ≈ 비슷. 그대로.

만약 사용자가 더 빠른 걸 원하면 `kFavoriteTileSpacingDp` 근처 상수에 `kPreviewSlideDuration = 120ms` 정도로.

### Step 5 — 단위테스트 / 검증

**기존 통과 테스트** (보존):
- `PlaceholderActivatesOnExternalDragEnter` ✅
- `PlaceholderMovesOnPreviewIndexChange` ✅ (transform 변화 verify 추가 필요)
- `PlaceholderDeactivatesOnExternalDragExit` ✅

**추가 권장**:
- `PlaceholderInstantlyFollowsCursorWithoutAnimation` — placeholder layer 의 GetAnimator()->is_animating() 가 false 인지 verify
- `DropIndicatorBorderRemoved` — `OnDragUpdated` 후 grid 의 border 가 nullptr 인지 verify

### Step 6 — 시각 QA

- 일반 탭을 favorites 위로 drag → cursor 따라 placeholder *즉시* 이동 (animation lag 0)
- cursor 옆 OS drag image: transparent 또는 favicon-only (텍스트 잔재 없음)
- grid 외곽 박스 없음 (placeholder + sliding tiles 만 시각 단서)
- drop → 자연 전환

---

## 위험 / 고려사항

1. **OS drag image 변경의 platform 차이**: macOS Aura DnD 가 transparent image 정상 처리하는지 검증 필요. Linux/Windows 도. 만약 platform-specific issue 시 favicon-only 로 fallback.

2. **drag source 가 favorites tile 인 경우** (internal reorder): 우리 placeholder 메커니즘은 `is_external` 일 때만. internal reorder 는 기존 `AnimateDragPreview` (placeholder 없음) 그대로. drag image 도 별개.

3. **사용자 expectations**: Zen 사용자는 cursor 즉시 따라감 + drag image 없는 정도를 기대. 우리 fix 후도 OS-level drag image 가 transparent 되면 macOS 의 OS-level "drag in progress" 시각 단서가 약해질 수 있음 — UX 검증 필요.

4. **internal reorder 의 source tile opacity 0**: 기존 `OnDragStarted` 가 source tile opacity 0 으로 만듦. Zen 의 "dragged element 가 시각 단서" 와 정반대. internal reorder 도 placeholder 식으로 통합할지는 별도 결정.

---

## 단계별 작업 순서

1. **Step 1** (placeholder instant transform) — 가장 단순, 즉시 효과
2. **Step 3** (drop indicator border 제거) — 한 줄 변경
3. **Step 2** (drag image transparent) — drag source 코드 검토 + SetDragImage 변경
4. **Step 5** 단위테스트 추가
5. **Step 6** 시각 QA — 사용자 검증

**예상 작업 규모**: ~50 라인 변경 (Step 1: 5줄, Step 2: 10줄, Step 3: 5줄, Step 5: 30줄). 시각 QA 0.5 day.

**단계별 효과**:
- Step 1 만 적용: cursor 따라감 lag 사라짐 (가장 큰 개선)
- Step 1 + Step 3: 외곽 박스 없음, placeholder 만 시각 단서
- Step 1 + Step 2 + Step 3: Zen 동급 polish 달성