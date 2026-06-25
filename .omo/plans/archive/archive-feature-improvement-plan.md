# Archive Feature Improvement Plan

**Revised:** Oracle review incorporated. Original 3 bugs → 4 correctness issues + 3 UX items.

## Context
Tab archive 리뷰 결과 4개 정확성 문제 + 3개 UX 개선 식별. Oracle 리뷰에서 live-space 오염 이슈 추가 발견, favorites 수정 범위 확대, 삭제 UI 진단 보완.

## Phase 1: Correctness (Must Fix)

### 1.1 복원/재오픈 탭 timestamp 미리셋 → 즉시 재아카이브
**Files:** `maho/crates/maho-core/src/tab_lifecycle.rs`
**Problem:** `restore_archived_tab()`과 `reopen_last_closed()` 모두 `state_timestamps`만 리셋하고 `creation_timestamps`는 안 함. Today Tab 경로가 `creation_timestamps` 기준이라, 복원된 탭이 다음 tick에서 즉시 재아카이브됨.
**Fix:**
```rust
/// 공통 헬퍼: 탭이 active pool에 재진입할 때 호출
fn reset_active_timestamps(&mut self, tab_id: &TabId) {
    let now = now_millis();
    self.state_timestamps.insert(tab_id.clone(), now);
    self.creation_timestamps.insert(tab_id.clone(), now);
}
```
- `restore_archived_tab()` — 기존 `state_timestamps.insert` 대신 `reset_active_timestamps` 호출
- `reopen_last_closed()` — 동일 적용 (현재 `creation_timestamps` 리셋 누락)
**Verification:** Unit test — 아카이브 복원 후 tick 실행, Active 유지 확인. 동일하게 reopen 후 확인.
**Risk:** `creation_timestamps` 리셋 시 `close_tabs_to_left/right` 정렬 의미 변경됨 (같은 map 사용). 현재는 수용 가능하나, 추후 today-tab 나이 vs 삽입순서 분리 필요할 수 있음.

### 1.2 Lifecycle 면제 중앙화 (favorites + pinned)
**File:** `maho/crates/maho-core/src/tab_lifecycle.rs`
**Problem:** 원래 플랜은 Active→Frozen 분기에만 `!is_favorite` 추가했으나, 이미 Frozen 상태인 탭이 이후 favorite 되어도 Suspended→Archived로 진행됨. 모든 자동 전환 분기에 일관된 면제 필요.
**Fix:**
```rust
fn is_auto_lifecycle_exempt(tab: &Tab) -> bool {
    tab.is_pinned || tab.is_favorite
}
```
`tick_with_settings()`의 3개 분기 모두에 적용:
- Active → Frozen: `&& !is_auto_lifecycle_exempt(tab)` (기존: `!is_pinned`)
- Frozen → Suspended: `&& !is_auto_lifecycle_exempt(tab)` (기존: 체크 없음)
- Suspended → Archived: `&& !is_auto_lifecycle_exempt(tab)` (기존: 체크 없음)

Today Tab 경로는 이미 `!t.is_pinned && !t.is_favorite` 체크하므로 변경 불필요.
**Verification:** Unit test — favorite 탭을 Frozen 상태에서 favorite 후 tick, Suspended 전환 안 됨 확인.
**Note:** memory pressure 경로(`handle_memory_pressure`)는 의도적으로 면제 안 함 — 시스템 리소스 위기 시에는 favorite도 suspend 가능.

### 1.3 아카이브 탭 live-space 구조 오염
**Files:** `maho/crates/maho-core/src/tab_lifecycle.rs`, `maho/crates/maho-core/src/space_manager.rs`
**Problem (Oracle 신규 발견):**
- `get_space_tabs()`, `get_pinned_tabs()`, `get_favorite_tabs()`, `count_favorite_tabs_in_spaces()` 가 `Archived` 상태 필터링 안 함
- 아카이브된 탭이 일반 사이드바에 ghost row로 노출 가능
- 아카이브된 favorite가 `MAX_FAVORITES` 카운트에 포함 → 실제 사용 가능 슬롯 줄어듦
- `delete_archived_tab()`이 SpaceManager의 `tab_order`/`root_order`/folder 참조 정리 안 함 → stale ID 잔류
**Fix (2파트):**

**A) Getter 필터링:**
```rust
// get_space_tabs — archived 제외
pub fn get_tabs_by_space(&self, space_id: &SpaceId) -> Vec<&Tab> {
    self.tabs.values()
        .filter(|t| t.space_id == *space_id && !matches!(t.state, TabLifecycleState::Archived { .. }))
        .collect()
}

// get_pinned_tabs, get_favorite_tabs, get_today_tabs, count_favorite_tabs_in_spaces
// 모두 동일하게 Archived 상태 필터 추가
```

**B) Delete cleanup:**
```rust
pub fn delete_archived_tab(&mut self, tab_id: &TabId) -> bool {
    // ... 기존 tabs/timestamps 제거 ...
    // SpaceManager에서도 제거 (tab_order, root_order, folder 참조)
    // → space_manager.remove_tab(tab_id) 호출 필요
}
```
**Verification:**
- Unit test — 아카이브 탭이 `get_space_tabs()` 결과에 미포함 확인
- Unit test — 아카이브 탭 삭제 후 SpaceManager에 stale ID 없음 확인
- Unit test — 아카이브된 favorite가 `MAX_FAVORITES` 카운트에 미포함 확인

### 1.4 삭제 후 UI 즉시 반영
**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
**Problem:** 삭제 callback이 shell event만 dispatch. 기존 observer 경로(`DispatchShellEvent` → `NotifyChanged` → `RefreshAll`)가 있으나 비동기라 즉시 반영 안 될 수 있음.
**Fix (tactical):** delete callback에서 dispatch 후 `archive_view_->ReloadArchivedTabs()` 직접 호출. 기존 observer 경로의 후속 reload와 중복되지만 무해함.
```cpp
archive_view_->SetDeleteCallback(
    base::BindRepeating(
        [](base::WeakPtr<MahoSidebarView> self, const std::string& tab_id) {
            DispatchShellEvent("delete_archived_tab", {{"tab_id", tab_id}});
            if (self && self->archive_view_) {
                self->archive_view_->ReloadArchivedTabs();
            }
        }, weak_factory_.GetWeakPtr()));
```
**Verification:** Interactive test — archive mode에서 탭 삭제, 즉시 리스트에서 사라짐 확인.

---

## Phase 2: UX Improvements

### 2.1 Multi-restore (archive mode 유지)
**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
**Problem:** 복원 시 `ExitArchiveModeToTabs()` 즉시 호출 → 연속 복원 불가.
**Fix:** `ExitArchiveModeToTabs()` 제거. 복원 후 기존 observer refresh 경로로 row 제거. 1.4와 동일하게 즉시 `ReloadArchivedTabs()` 호출 추가.
```cpp
archive_view_->SetRestoreCallback(base::BindRepeating(
    [](base::WeakPtr<MahoSidebarView> self, const std::string& tab_id) {
        DispatchShellEvent("restore_archived_tab", {{"tab_id", tab_id}});
        if (self && self->archive_view_) {
            self->archive_view_->ReloadArchivedTabs();
        }
    }, weak_factory_.GetWeakPtr()));
```
**Verification:** Interactive test — 연속 3개 탭 복원, archive mode 유지 확인.

### 2.2 검색 디바운스
**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.cc`
**Problem:** 매 키입력마다 전체 리스트 재구성.
**Fix:** `base::OneShotTimer` 200ms 디바운스. 아카이브 목록이 실제로 크다고 확인된 후 적용해도 됨.
**Priority:** 낮음 — correctness 이후.

### 2.3 Archive cap (보류)
**Problem:** 아카이브 무제한 축적.
**Status:** 후순위. 구현 시 per-space eviction + SpaceManager cleanup 동반 필요.
**When:** 실제 사용 데이터에서 성능 문제 확인된 후.
**If implemented:**
- Per-space cap (전체 cap 아님)
- `archived_at` 기준 oldest eviction
- Eviction 시 SpaceManager `tab_order`/`root_order`/folder 참조도 정리

---

## Phase 3: Future (Out of scope)

- Cross-space archive 통합 검색
- List virtualization
- Sort options (title, domain, date)
- Batch select + restore/delete
- `creation_timestamps` 분리: today-tab 나이 vs 탭 삽입순서 (close_left/right용)

## Execution Order
1.1 → 1.2 → 1.3 → 1.4 → 2.1 → 2.2 → 2.3

## Risk Notes
- **`creation_timestamps` 오버로딩**: today-tab 나이 판단과 `close_tabs_to_left/right` 정렬이 같은 map 사용. 1.1 수정으로 복원 탭의 좌/우 정렬 위치 변경됨. 현재 수용 가능하나 Phase 3에서 분리 고려.
- **`tick_with_settings` 뺄셈**: `now - timestamp`이 `saturating_sub` 아님. 시계 역행 시 debug panic / release wrap 가능. 별도 수정 권장.
- **기존 아카이브된 favorites**: 1.2 적용 후에도 이미 아카이브된 favorite 탭은 정리 안 됨. 마이그레이션 필요 여부 판단.
- **2.1 multi-restore**: 복원 callback에 성공/실패 ack 없음. ReloadArchivedTabs()가 core 상태 반영하므로 실패 시 row 잔류로 자연 처리.
