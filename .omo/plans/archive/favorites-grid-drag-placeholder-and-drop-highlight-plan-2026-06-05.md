# Favorites Grid Drag/Drop UX — 미해결 Bug 1·2 구현 계획

**Generated:** 2026-06-05
**Owner:** sidebar / favorites grid
**Files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.{h,cc}`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_models.h` (참고)

---

## Context

여러 차례 fix 를 거친 후에도 두 시각 버그가 남아 있다.

**Bug 1 — drag-time placeholder 부재**: 사용자가 일반 sidebar 탭을 favorites 영역 위로 드래그하는 순간 (drop 전), 들어갈 슬롯에 favorite tile 모양의 placeholder 가 즉시 등장해야 한다. 현재는 `AnimateDragPreview` 가 기존 favorite 들을 옆으로 미는 슬라이드만 하고, 드래그 중인 탭이 어디에 들어갈지 placeholder 로 보여주지 않는다.

**Bug 2 — drop 후 잠시 후 drop 표시 박스 재등장**: 사용자 보고: "drop 직후 잠시 후 박스가 다시 나타남". `MahoSidebarFavoritesGridView::OnDragUpdated` 의 `SetBorder(...kColorSysPrimary)` 가 drag accept 후에도 어딘가에서 다시 호출되는 비정상 path 가 있다.

---

## Bug 2 — drop 후 박스 잔재 (이번 세션 적용 완료, 검증 대기)

### Root cause

`drag_accepted_tab_id_` 가 `OnTileDragDone` 에서 일찍 clear 되고, 그 후 `OnDragUpdated` 가 한 번 더 호출되면 `SetBorder` 가드가 무력해져 박스가 재출현한다. Oracle 분석 결과 `FavoriteTileButton::OnDragDone` 이 `NotifySidebarDragEnded` 와 동등한 cleanup chain 을 호출하지 않아 `OnDragEnded` 가 production 에서 호출되지 않는 것이 근본 원인이었다.

### 적용 fix (이미 코드에 들어감)

| 위치 | 변경 |
|---|---|
| `FavoriteTileButton::OnDragDone()` (line ~361) | 끝에 `if (drop_target_) drop_target_->OnDragEnded();` 호출 추가 |
| `MahoSidebarView::OnDragExited()` (line 830) | `if (!favorites_view_->IsDropAccepted())` 가드로 `CollapseFromDrag` skip |
| `MahoSidebarFavoritesGridView` (header) | public `IsDropAccepted()` accessor 추가 |
| `MarkDropAccepted` (line ~1357) | `drag_reveal_active_ = false` 추가 |
| `Update()` 끝 (line ~809) | `pending_internal_drop_reorder_` consume 강화 — reorder 가 land 했을 때만 소비 |
| `OnTileDragDone` (line ~1369) | `drag_accepted_tab_id_.clear()` 제거 — `OnDragEnded` 가 유일한 reset 지점 |
| `OnDragUpdated` 시작 (line ~996) | `if (!drag_accepted_tab_id_.empty()) return kMove;` 가드 |
| `OnDragUpdated` 의 SetBorder 호출 (line ~1058) | `if (drag_accepted_tab_id_.empty())` 추가 가드 |
| `OnDragEnded` (line ~1141) | `drag_accepted_tab_id_.clear()` 추가 |

### 검증 시나리오

1. favorite 끼리 reorder → drag 중 박스 보임 (정상) → drop → 박스 즉시 사라지고 재등장 없음
2. 일반 탭을 favorites 로 drag → drop → 박스 즉시 사라지고 재등장 없음
3. drag 중 영역 밖으로 나갔다 다시 들어와도 박스 정상 동작

### Backup plan (위 fix 로 부족한 경우)

`drag_session_consumed_` sticky flag 추가:
- `MarkDropAccepted` 에서 `drag_session_consumed_ = true`
- `OnDragUpdated` 시작 가드에 추가
- 다음 drag 시작 검출 (예: 첫 OnDragUpdated 가 `drag_over_drop_target_=false` 상태에서 호출) 시 `drag_session_consumed_ = false`

---

## Bug 1 — drag-time favorite-shaped placeholder (큰 신규 작업)

### 의도

cursor 가 favorites grid 위로 올라온 순간, 드래그 중인 탭이 들어갈 자리에 **다른 favorite 들과 동일한 모양의 placeholder tile** 이 즉시 등장해야 한다. drop 안 하고 빠져나가면 placeholder 사라짐. drop 하면 자연 전환.

### Root cause

현재 `AnimateDragPreview` (line ~1610) 는 기존 N tile 들을 N 슬롯 layout 의 다른 슬롯으로 미는 transform 슬라이드만 한다. 드래그 중인 외부 탭이 들어갈 자리는 layout 에 새로 등장하지 않아 사용자에게 미리보기 되지 않는다.

### 구현 방안

#### 1. 새 멤버 (header)

```cpp
// MahoSidebarFavoritesGridView header private 섹션:
int drag_placeholder_index_ = -1;        // 현재 placeholder 가 그려지는 슬롯 인덱스
ui::ImageModel drag_placeholder_icon_;   // 드래그 중인 탭의 favicon
std::u16string drag_placeholder_text_;   // 표시 라벨
bool placeholder_active_ = false;        // 활성 상태
```

#### 2. Helper method 추가

```cpp
// 드래그 진입 시 placeholder 활성화
void ActivateDragPlaceholder(const SidebarDragPayload& payload, int slot);

// 드래그 위치 변경 시 placeholder 슬롯 이동
void MoveDragPlaceholder(int new_slot);

// 드래그 떠남 또는 drop 시 placeholder 비활성화
void DeactivateDragPlaceholder();
```

#### 3. `OnDragUpdated` 변경

```cpp
int OnDragUpdated(const ui::DropTargetEvent& event) {
  if (!drag_accepted_tab_id_.empty()) return kMove;
  // ... payload check ...

  const bool is_external = (payload.origin != SidebarDragOrigin::kFavorites);

  if (is_external && favorite_count_ < kTileCount) {
    if (!placeholder_active_) {
      ActivateDragPlaceholder(payload, preview_index);
    } else if (drag_placeholder_index_ != preview_index) {
      MoveDragPlaceholder(preview_index);
    }
  }

  // 기존 AnimateDragPreview 호출은 유지 (placeholder 포함 N+1 layout 기준 슬라이드)
  // ...
}
```

#### 4. Layout 재계산

`AnimateDragPreview` 와 `RebuildGridRows` 가 placeholder 활성 시 `favorite_count_ + 1` 슬롯 기준으로 layout 계산하도록:

```cpp
size_t effective_count = placeholder_active_
    ? std::min(favorite_count_ + 1, kTileCount)
    : favorite_count_;
RebuildGridRows(effective_count);
```

#### 5. Placeholder tile 의 시각

- `tile_views_[favorite_count_]` (또는 다음 invisible 슬롯) 을 빌려서:
  - `SetVisible(true)`, `SetEnabled(false)` (클릭 막음)
  - 그 tile 의 favicon `ImageView` 에 `drag_placeholder_icon_` set
  - tile 의 `SetActiveBorderColor` 로 dim 한 border (또는 dashed) 적용 — "임시" 시각
- 또는 별도 `views::View` 를 일회성으로 만들어 `content_view_` 에 add (RebuildGridRows 와 충돌 위험)

권장: 기존 invisible tile 슬롯 재활용 + active border color 로 dim 처리.

#### 6. Drag favicon 추출

`SidebarDragPayload` 에 favicon 정보가 없을 수 있다 — payload 는 tab_id 만 들고 있다. favicon 은 `MahoTabIdHelper::FromWebContents` 등으로 source tab 의 WebContents 에서 가져와야 한다.

```cpp
ui::ImageModel ResolveFaviconForTabId(const std::string& tab_id);
```

`browser_->tab_strip_model()` 순회하며 `MahoTabIdHelper::stable_tab_id()` 매칭 → `TabUIHelper::From(tab_iface)->GetFavicon()`. 이미 `Update()` 안에 비슷한 로직 있음 (line ~580-605) — 헬퍼로 추출.

#### 7. `OnDragExited` / drop 처리

```cpp
void OnDragExited() {
  if (!drag_accepted_tab_id_.empty()) {
    DeactivateDragPlaceholder();
    ResetDropIndicator();
    // ... layer reset
    return;
  }
  DeactivateDragPlaceholder();
  // ... 기존 normal path
}
```

drop callback 안에서는 `MarkDropAccepted` 후 `DeactivateDragPlaceholder` 호출 — 이후 backend 의 `Update(model)` 이 새 favorite 으로 자연 전환.

#### 8. `DeactivateDragPlaceholder` 구현

```cpp
void DeactivateDragPlaceholder() {
  if (!placeholder_active_) return;
  placeholder_active_ = false;
  drag_placeholder_index_ = -1;
  // tile_views_[favorite_count_] (placeholder slot) 를 invisible 복원
  if (favorite_count_ < kTileCount && tile_views_[favorite_count_]) {
    tile_views_[favorite_count_]->SetVisible(false);
    tile_views_[favorite_count_]->SetEnabled(false);
  }
  RebuildGridRows(favorite_count_);
}
```

### 위험 / 고려사항

1. **`tile_views_[favorite_count_]` 이미 model items 의 일부일 수 있음**: `Update()` 가 전체 spec 을 적용하므로 placeholder 가 `favorite_count_` 인덱스를 차지하면 충돌. → `Update(model)` 호출 중에는 placeholder 비활성화 보장 필요.

2. **AnimateDragPreview 의 layout 계산 일관성**: placeholder 활성 시 layout 의 `effective_count` 사용 일관 필요. 혼란 시 transform 계산 오류 → 시각 jank.

3. **drop 직후 placeholder → 실제 tile 전환의 매끄러움**: `MarkDropAccepted` → backend → `Update(model)` 사이 시간 갭 동안 placeholder 가 살아있어야 시각 끊김 없음. timing 검증 필요.

4. **drop cancel (ESC)**: drag 도중 사용자가 ESC 누르면 `OnDragExited` 또는 `OnDragEnded` 가 호출되어야 placeholder 해제. 현재 platform 동작 확인 필요.

5. **빈 favorites grid (count=0)**: 현재 `EmptyFavoritesPlaceholderView` 가 별도 표시. drag-time placeholder 와의 충돌 처리 — placeholder 활성 시 empty card 숨기기.

### 검증 시나리오

1. 일반 탭을 favorites 영역 위로 드래그 → cursor 진입 즉시 placeholder tile 이 들어갈 슬롯에 favicon + 둥근 카드 모양으로 등장
2. cursor 를 favorites 영역 안에서 다른 슬롯으로 이동 → placeholder 가 새 슬롯으로 이동, 다른 favorite 들이 자연스럽게 슬라이드
3. drop 안 하고 영역 밖으로 빠져나감 → placeholder 사라짐, 기존 favorites 가 N 슬롯 layout 으로 복원
4. drop → placeholder 가 그대로 model 업데이트로 정상 favorite tile 로 전환 (시각 끊김 없음)
5. 빈 favorites grid 일 때 → empty card 숨겨지고 placeholder 만 표시

### 단계별 작업

1. **Step 1**: `ResolveFaviconForTabId` 헬퍼 추출 (현재 Update() 안 로직 → static / private method)
2. **Step 2**: `drag_placeholder_*` 멤버 + `ActivateDragPlaceholder` / `MoveDragPlaceholder` / `DeactivateDragPlaceholder` 구현
3. **Step 3**: `OnDragUpdated` 에 placeholder 활성/이동 로직 통합
4. **Step 4**: `RebuildGridRows` / `AnimateDragPreview` 의 `effective_count` 일관 적용
5. **Step 5**: `OnDragExited`, `Update`, drop callback 에 placeholder 정리 로직
6. **Step 6**: 시각 QA + tweak (border color, transition timing 등)

### 단위 테스트 추가

- `MahoSidebarFavoritesGridView::PlaceholderActivatesOnExternalDragEnter`
- `MahoSidebarFavoritesGridView::PlaceholderMovesOnPreviewIndexChange`
- `MahoSidebarFavoritesGridView::PlaceholderDeactivatesOnExternalDragExit`
- `MahoSidebarFavoritesGridView::PlaceholderTransitionsOnInternalReorder` (placeholder 비활성 보장)

### 작업 규모 추정

코드 변경 ~150-200 라인. 단위 테스트 추가 ~80 라인. 시각 QA 포함 0.5-1 day.
