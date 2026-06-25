# Empty Favorites UI Rework — google.com seed + section hide

**Generated:** 2026-06-05
**Status:** §5.2 (EmptyFavoritesPlaceholderView removal) IMPLEMENTATION LANDED 2026-06-07.
Note: a `cancelable_task_tracker_.TryCancelAll()` regression was introduced and fixed in
sidebar-scope-creep-followups-and-retroactive-2026-06-07.md (Phase E-FIX).
**Owner:** sidebar / favorites + spaces
**Files:**
- `maho/crates/maho-core/src/maho_core.rs` (first-run seed)
- `maho/crates/maho-core/src/space_manager.rs` (or `maho_core.rs::create_space`)
- `maho/crates/maho-core/src/tab_lifecycle.rs` (favorite tab creation)
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.{h,cc}` (empty-state hide)
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc` (parent)

---

## Goal

빈 favorite 영역의 카드 형 일러스트 onboarding (`EmptyFavoritesPlaceholderView`) 을 제거하고, 새 사용자/새 스페이스에 `google.com` favorite 을 기본 seed 로 한 개 자동 추가한다. favorite 가 0 이 되는 경우 (사용자가 google.com 까지 unfavorite) favorites section 을 완전히 숨긴다.

---

## Bug context

- 현재 빈 상태 UI (`EmptyFavoritesPlaceholderView`, line ~100) 는 carded onboarding (큰 별 badge + close × + "Drag to add Favorites" 카피 + description). drag-time placeholder 의 favorite-tile 시각 언어와 단절됨.
- `seed_first_run_tabs` (line 984) 는 기존에 NTP url 로 favorite/pinned 를 1개씩 만드는 로직이지만 `#[allow(dead_code)]` 로 표시됨 — 현재 first-run path 에서 호출되지 않음.
- `create_space` (line 3320) 는 새 스페이스 생성만 하고 favorites 자동 시드하지 않음.

---

## Plan

### Step 1 — Rust: 통합 helper 정의

위치: `maho-core/src/maho_core.rs` (impl MahoCore 안 private 메서드).

```rust
fn seed_default_favorite_for_space(&mut self, space_id: &SpaceId) {
    let google_url = maho_types::common::Url::new("https://www.google.com");
    let tab = self.tab_manager.create_tab(
        space_id.clone(),
        Some(google_url),
        Some("Google".to_string()),
        None,
    );
    self.space_manager
        .add_tab_to_space(space_id, tab.id.clone(), None);
    self.tab_manager.favorite_tab(&tab.id);
}
```

이 helper 를 두 호출 지점 (Step 2, Step 3) 에서 재사용.

### Step 2 — Rust: 새 스페이스 생성 시 google.com seed

위치: `maho-core/src/maho_core.rs::create_space` (line 3320).

```rust
pub fn create_space(&mut self, name: &str, color: SpaceColor, profile_id: ProfileId) -> Space {
    let space = self.space_manager.create_space(name, color, profile_id);
    let vm = self.space_manager.to_view_model(&space);
    self.seed_default_favorite_for_space(&space.id);  // ADD
    let update = CoreUpdate::SpaceCreated { space: vm };
    for listener in self.update_listeners.borrow().iter() {
        listener(update.clone());
    }
    space
}
```

검증:
- 새 스페이스 생성 후 그 스페이스의 favorites 에 google.com 1개 존재
- 기존 스페이스의 favorites 영향 없음
- 사용자가 unfavorite 해도 다음 스페이스 생성 시 다시 시드됨 (의도)

### Step 3 — Rust: 앱 최초 시작 시 default space 에 google.com seed (storage-aware)

코드 사실:
- `MahoCore::new()` 가 매 실행 호출되며 in-memory default space "Personal" 생성
- 재실행 시 `MahoCore::load_state()` 가 storage 의 spaces 로 in-memory 데이터를 덮어씀
- 따라서 **"앱 최초 시작" 의 정확한 신호**: `load_state()` 안에서 `tabs_data.is_none() && spaces_data.is_none()` (storage 가 비어있음)

위치: `maho-core/src/maho_core.rs::load_state` (line 1109).

```rust
pub fn load_state(&mut self) -> Result<(), PersistenceError> {
    let lmdb = ...;
    let tabs_data = lmdb.get("tabs").map_err(...)?;
    let spaces_data = lmdb.get("spaces").map_err(...)?;
    let profiles_data = lmdb.get("profiles").map_err(...)?;

    let is_first_run = tabs_data.is_none() && spaces_data.is_none();

    // ... 기존 restore 로직 그대로 ...

    if is_first_run {
        let active_id = self.get_active_space_id().clone();
        self.seed_default_favorite_for_space(&active_id);
    }
    Ok(())
}
```

검증:
- storage 비어있는 첫 실행: default space "Personal" 의 favorites 에 google.com 1개
- 재실행: `is_first_run = false` → seed 안 됨, 기존 favorites 보존
- storage 없이 (unittest 등) `MahoCore::new()` 만 호출 시: seed 안 됨 (load_state 미호출). 의도된 동작 — 통합테스트는 명시적으로 seed helper 호출.

### Step 4 — Rust: 기존 user 영향 / migration 정책

`is_first_run` 가드가 storage 데이터 부재로 검출 → 기존 user 의 storage 에는 데이터 있으니 자동 false. **기존 user 영향 0**. 별도 settings flag 불필요.

검증:
- 기존 user 가 favorites 모두 unfavorite 한 채로 앱 재시작 → google.com 자동 추가 안 됨
- 기존 user 가 새 스페이스 생성 → 그 스페이스에 google.com 1개 (Step 2 효과)

### Step 5 — C++: empty-state UI 제거

위치: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.{h,cc}`

#### 5.1 `Update(model)` (cc:641) 의 visibility 로직 변경

```cpp
// 기존:
content_view_->SetVisible(has_items);
empty_state_view_->SetVisible(!has_items);

// 변경:
const bool grid_visible = has_items || placeholder_active_;
content_view_->SetVisible(grid_visible);
empty_state_view_->SetVisible(false);  // 항상 hide
SetVisible(grid_visible);  // grid 자체도 hide
```

#### 5.2 `EmptyFavoritesPlaceholderView` 처리

옵션:
- (A) 코드 자체 보존 (dead) — 향후 재활용 여지
- (B) 클래스 + `empty_state_view_` 멤버 + 모든 사용처 제거 (clean)

권장: **(B)** clean-up. dead code 정리.

영향 받는 라인:
- Class definition: cc:100-228 (`EmptyFavoritesPlaceholderView` + `BEGIN_METADATA`)
- Member: header `empty_state_view_`, `empty_state_card_`
- ctor: `empty_state_view_ = shell_view->AddChildView(...)` + 그 안 layout / card 초기화
- `UpdateEmptyStateHighlight`: `empty_state_card_` 사용 — 이 메서드 호출처 모두 제거 또는 no-op 화
- `Update(model)` 안 `empty_state_view_->SetVisible` 라인

#### 5.3 `RevealForDrag` / `CollapseFromDrag` (cc:1118+)

빈 grid drag 진입 시 reveal — drag-time placeholder 메커니즘이 이미 처리. 단순화 가능:

```cpp
void RevealForDrag() {
  if (drag_reveal_active_ || favorite_count_ > 0) return;
  drag_reveal_active_ = true;
  SetVisible(true);
  // empty card 관련 SetPreferredSize 제거 — placeholder 가 height 계산 처리
}
```

#### 5.4 `MahoSidebarView::OnDragExited` (view.cc:830)

`CollapseFromDrag` 가 height 0 으로 줄이는 효과 검증. drag accept 시 skip 가드 (이미 적용) 외에 추가 변경 불필요.

### Step 6 — 단위/E2E 테스트

추가:
- `MahoCore::CreateSpaceSeedsGoogleFavorite` (Rust) — 새 스페이스 생성 시 favorites 에 google.com 1개 확인
- `MahoCore::CreateSpaceFavoriteIsRemovable` (Rust) — 사용자가 unfavorite 가능
- `MahoCore::FirstRunSeedsGoogleFavoriteOnDefaultSpace` (Rust) — `load_state` 의 `is_first_run` 분기 검증 (storage 비어있을 때만 seed)
- `MahoCore::SecondRunDoesNotReseed` (Rust) — 재실행 시 seed 안 됨
- `MahoSidebarFavoritesGridView::HidesWhenZeroFavorites` (C++) — favorite_count_ == 0 시 grid SetVisible(false)
- `MahoSidebarFavoritesGridView::RevealsOnDragEntry` (C++) — drag 진입 시 reveal

### Step 7 — 시각 QA

- 첫 실행 시 google.com favorite 1개 보임 (favicon 정상 로딩)
- 새 스페이스 만들면 그 스페이스에서 google.com favorite 1개
- google.com 을 unfavorite → favorites section 자체 사라짐
- drag 진입 → grid reveal + placeholder
- drop → google.com (또는 다른 url) 추가 → grid 정상 표시

---

## 위험 / 고려사항

1. **google.com favicon 미리 로딩**: 첫 실행 시 google.com 페이지 방문 안 했어도 favicon 표시되어야 자연스러움. `FaviconServiceFactory` 로 직접 google.com favicon 조회 → 캐시 미스 시 globe icon fallback. 첫 화면에서 globe 보이고 페이지 로딩 후 갱신.
2. **여러 스페이스 동시 새 생성**: 각 스페이스마다 독립 google.com tab — 동일 url 의 별도 tab 들. 의도된 동작.
3. **테마/locale**: google.com 은 사용자 locale 따라 redirect. 의도와 무관, OK.
4. **세션 복원 후 빈 상태**: 세션 복원 시 새 스페이스 안 만듬 — 기존 데이터 그대로. 영향 없음.
5. **EmptyFavoritesPlaceholderView 제거 시 기존 unittest 영향**: 검색 + 정리.
6. **Storage 없이 (unittest) `MahoCore::new()` 만 호출**: `load_state` 미호출이라 first-run seed 안 됨. 통합테스트는 `with_lmdb_storage(...)` + `load_state()` 사용해야 검증됨.

---

## 단계별 작업 순서

1. Rust Step 1 (helper 정의)
2. Rust Step 2 (`create_space` 호출) → cargo test
3. Rust Step 3 (`load_state` 의 first-run 가드)
4. Rust Step 4 (migration 검증 — 별도 코드 변경 없음, 테스트만)
5. C++ Step 5 (visibility 로직 + EmptyFavoritesPlaceholderView 제거)
6. Step 6 단위테스트 추가
7. 빌드 + Step 7 시각 QA

작업 규모 추정: Rust ~30 라인, C++ ~80 라인 (제거 위주), 테스트 ~60 라인. 시각 QA 포함 0.5 day.
