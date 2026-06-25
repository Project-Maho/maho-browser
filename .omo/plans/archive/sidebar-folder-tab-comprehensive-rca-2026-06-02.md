# Sidebar Folder & Tab Structure — Comprehensive Root-Cause Report

**Date:** 2026-06-02
**Triggered by:** "폴더하고 탭 구조가 완전히 개판이 됐는데 아예 전체 구조부터 탐색해서 근본적인 원인 다 찾아서 고쳐야함"
**Method:** 2 parallel explore agents (`bg_740256cb`, `bg_18c34a7c`) + direct code reading + runtime LOG diagnostics

---

## TL;DR

Sidebar folder/tab structure has **5 distinct regressions** that landed together in the uncommitted working tree. None are caused by the hover popup feature itself — most predate it as "piggyback" changes. Fixes are ordered by severity in §F.

| # | Severity | Symptom | File:Line |
|---|---|---|---|
| **R1** | CRITICAL | 폴더 토글 클릭 무반응 (visual) — dispatch는 firing 되지만 rebuild가 반영 안 됨 | `tab_list_view.cc:3009` + `sidebar_view.cc:606` |
| **R2** | CRITICAL | 새로고침/재시작 후 복원된 탭들이 사이드바에서 사라짐 | `state_adapter.cc:340-352` (삭제됨) |
| **R3** | HIGH | 활성 탭의 URL이 사이드바에서 stale로 표시 | `state_adapter.cc::OverlayActiveTabFields` |
| **R4** | MEDIUM | 로딩 중 탭의 sync 아이콘 사라짐 (visual only) | `tab_list_view.cc:1108-1130` |
| **R5** | LOW | `SidebarSpaceHeaderRow`가 일반 탭 드래그를 pinned로 잘못 흡수 | `tab_list_view.cc:2228-2270` |

추가로 hover popup feature 자체에는 1건의 불완전 구현 (A1) — 본 사이클의 진단 모드라 원래 정상.

---

## A. 진단 LOG로 확정된 사실

`[MAHO TOGGLE DIAG]` LOG를 `ToggleFolderExpanded`에 추가하고 사용자가 클릭한 결과:

```
[MAHO TOGGLE DIAG] ToggleFolderExpanded fired folder_id=ef8dd213-345f-4207-b6e1-ed0b956258e1
[MAHO TOGGLE DIAG] DispatchShellEvent returned
[MAHO TOGGLE DIAG] ToggleFolderExpanded fired folder_id=8a2aa04b-d133-4d16-895c-e26220b15da0
[MAHO TOGGLE DIAG] DispatchShellEvent returned
... (16+ events captured)
```

**확정 사실:**
- ✅ Click → label_button_::NotifyClick → toggle_callback → `ToggleFolderExpanded` 정상 발화
- ✅ `active_space_id_` non-empty (SKIPPED 분기 안 탐)
- ✅ `DispatchShellEvent` 정상 리턴
- ❌ 시각적 변화 0
- ✅ 탭 추가는 정상 반영됨 (사용자 보고)

**결론:** 버그는 `DispatchShellEvent` **이후**의 sidebar refresh chain에 있음. Rust 토글, Mojo, 또는 visual rebuild 중 한 단계.

---

## B. 5개 회귀 상세

---

### R1 — 폴더 토글 시각 반영 안 됨

**위치:** `state_adapter.cc::DispatchShellEvent` (line ~538-553) → `bridge->NotifyChanged()` → `MahoSidebarView::OnSpaceProfileBridgeChanged()` (sidebar_view.cc:759) → `ScheduleRefreshAll()` (sidebar_view.cc:606)

**증거:**
- LOG: dispatch fire 확인됨
- 사용자 보고: 시각 변화 0
- 사용자 보고: 탭 추가는 정상 (= refresh chain 자체는 작동)

**가설 (확률 순):**

**B1.1 (가장 유력):** `ScheduleRefreshAll`이 in-flight일 때 toggle dispatch가 발생 → `refresh_scheduled_=true` 가드에 막혀 `needs_re_refresh_=true`만 set하고 return → 다음 round-trip에서 두 번째 refresh가 실행되어야 하지만 그 시점에 Rust state가 이미 정상화되어 있어 시각적으로는 "원위치"로 보임 (사용자가 "토글 안 됨"으로 인식).

**검증법:** 추가한 `[MAHO SBR DIAG]` LOG로 다음을 확인:
- `ScheduleRefreshAll entry refresh_scheduled_=1` (가드 hit) 발생 여부
- `OnFavoritesModelReady (async reply)` 발생 여부  
- `RefreshAll fired` 발생 여부

**B1.2:** Rust `space_manager.rs::toggle_folder_expanded` (line 1551)이 folder_id를 못 찾아서 silently no-op. 그러면 state 자체가 안 바뀜 → next rebuild에서 같은 is_expanded 값.

```rust
pub fn toggle_folder_expanded(&mut self, space_id: &SpaceId, folder_id: &FolderId) {
    if let Some(space) = self.spaces.get_mut(space_id) {
        if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
            folder.is_expanded = !folder.is_expanded;
        }
        // ← else: silently no-op
    }
}
```

**검증법:** Rust 측에 `eprintln!` 추가 또는 next round 후 `maho_core_get_sidebar_tree`로 직접 is_expanded 확인.

**Fix 방향:**
- B1.1이면: `ScheduleRefreshAll` 동기 fast-path 추가 (`needs_re_refresh_` 메커니즘 강화)
- B1.2이면: folder_id 매칭 디버깅 (UUID 형식 차이 등)

---

### R2 — 복원된 탭이 사이드바에서 사라짐 (CRITICAL)

**위치:** `maho_sidebar_state_adapter.cc::ParseTreeNode` — 탭 분기 안의 `SetRestoredTabId` 호출이 **삭제됨** (git diff 확인)

**삭제된 코드 (이전 버전):**
```cpp
if (contents && !node.tab_id.empty()) {
  if (auto* helper = MahoTabIdHelper::FromWebContents(contents);
      helper && helper->stable_tab_id() != node.tab_id) {
    helper->SetRestoredTabId(node.tab_id);
  }
}
```

**메커니즘:**
- 브라우저 재시작 시 `WebContents`는 다시 만들어지고 `MahoTabIdHelper`도 새 UUID 부여받음
- 하지만 core가 보관한 `tab_id`는 옛날 UUID
- `SetRestoredTabId`가 매핑을 갱신해서 둘을 다리 놓음
- **이 호출이 사라지면 `stable_id_map` lookup 실패 → `ResolveTabStripIndexForTabId` -1 반환 → `RebuildTreeNodes`에서 탭 행이 skip됨**

**영향:** 복원 후 탭이 사이드바에서 안 보임. 탭 strip(상단)에는 있는데 사이드바에는 없음.

**Fix:** `ParseTreeNode`의 탭 분기 (line ~340 근처)에 호출 복구.

---

### R3 — 활성 탭 URL이 stale 표시

**위치:** `maho_sidebar_state_adapter.cc::OverlayActiveTabFields` — git diff에서 다음 두 줄 삭제 확인:

**삭제된 코드:**
```cpp
const GURL& live_url = GetVisibleOrCommittedUrl(active_contents);
node.url = live_url.is_valid() ? live_url.spec() : std::string();
```

**메커니즘:**
- 활성 탭의 `node.url`은 core에서 마지막 저장된 값
- 사용자가 페이지를 navigate하면 live URL이 바뀜
- overlay가 사라지면 사이드바는 옛 URL 표시
- 사용자가 보는 페이지와 사이드바 row의 URL이 안 맞음

**Fix:** 두 줄 복구.

---

### R4 — 로딩 spinner 사라짐 (visual only)

**위치:** `maho_sidebar_tab_list_view.cc::SidebarTabRowView` constructor (line ~1108-1130)

**삭제된 코드:**
```cpp
loading_(node.is_loading),
...
if (loading_) {
  favicon_view_->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kSyncChromeRefreshIcon, ui::kColorSysOnSurfaceSubtle, kRowIconSizeDp));
} else {
  favicon_view_->SetImage(std::move(favicon));
}
```

**메커니즘:**
- 페이지 로딩 중일 때 favicon 자리에 sync 아이콘이 회전하던 것이 사라짐
- 기능 영향 없음, 시각적으로만 후퇴

**Fix:** 옵션 — 복구하거나 의도된 변경으로 받아들이기.

---

### R5 — SpaceHeaderRow가 의도치 않은 drop target

**위치:** `maho_sidebar_tab_list_view.cc::SidebarSpaceHeaderRow` (line ~2183-2316)

**메커니즘:**
- `SidebarSpaceHeaderRow`는 사이드바 상단 (top_region_)에 위치
- `CanDrop`에서 `kNormalSection`/`kPinnedSection` 둘 다 accept
- `GetDropCallback`이 **`PlanSectionDrop(kPinned, ...)`** 무조건 실행
- 사용자가 일반 탭을 사이드바 위쪽으로 드래그하면 의도치 않게 pinned로 옮겨짐

**Fix:** `CanDrop`을 더 엄격하게 (drag origin이 pinned section일 때만), 또는 헤더 row를 drop target에서 완전 제외.

---

## C. 추가 발견 (회귀는 아니지만 알아둘 것)

### C1. 진단 모드 — `OnMouseEntered`에서 `show_popup_callback_` 호출 비활성화 상태

**현재 의도된 진단 상태.** 이 보고서 작성 시점에는 popup이 일부러 꺼져있음. 토글 fix 완료 후 복구 예정.

```cpp
void SidebarFolderRowView::OnMouseEntered(const ui::MouseEvent& event) {
  views::View::OnMouseEntered(event);
  UpdateAppearance();
  // show_popup_callback_.Run() 의도적으로 제거됨 (진단)
}
```

복구 시 한 줄 추가:
```cpp
if (show_popup_callback_ && !children_snapshot_.empty()) {
  show_popup_callback_.Run();
}
```

### C2. `folder_is_pinned` 기본값 fix는 working tree에 적용됨

`ParseTreeNode`의 `node.folder_is_pinned = pinned.value_or(false);` — 이전엔 `value_or(true)`로 모든 folder가 pinned section에 끌려갔음. **현재 fix됨.**

### C3. `IsSidebarIneligibleUrl`의 `&& !is_active` 조건 제거

`state_adapter.cc:320` 변경. 활성 탭이 ineligible URL (예: `chrome://newtab`)이어도 사이드바에서 제외하도록 조건 강화. 의도된 변경으로 보임.

### C4. `cycle_space_cache_` (state_adapter.cc:639-642, 911-918, 990-1004)

`BuildViewStateModel` 1회 호출 안에서 footer/tablist 빌드가 같은 space state를 재사용하도록 캐싱. `mutable unique_ptr` 패턴이라 단일 호출 lifetime 안에서만 살아있음. 안전하지만 fragile (재진입 발생 시 캐시 깨짐).

### C5. ScrollView 래핑 (sidebar_view.cc:215-221)

`MahoSidebarTabListView`가 이제 `views::ScrollView` 안에 들어있음. `ClipHeightTo`로 viewport 정의됨. `views::ScrollView::Viewport`는 mouse event 가로채지 않으므로 click routing에 영향 없음.

### C6. SidebarAutoScroller — 드래그 자동 스크롤 시스템

`ForwardDragToAutoScroller` / `ForwardDragEnded` 호출이 다수 drop handler에 삽입됨. 모두 `OnDragUpdated`/`GetDropCallback`/`OnDragExited` 내부 — **mouse press/release에는 영향 없음.** 단순 click과 무관.

---

## D. 데이터 흐름 무결성 검증 (Rust → C++ → UI)

### Rust 측

**`event_dispatcher.rs:763-775`** — `ShellEvent::ToggleFolderExpanded` handler:
```rust
ShellEvent::ToggleFolderExpanded { space_id, folder_id } => {
    space_mgr.toggle_folder_expanded(&space_id, &folder_id);
    vec![CoreUpdate::FolderUpdated {
        folder_id,
        changes: FolderUpdate {
            name: None,
            is_expanded: None,    // ← intentional: state read fresh from get_sidebar_tree
            is_pinned: None,
        },
    }]
}
```

**`space_manager.rs:1551-1557`** — toggle 구현:
```rust
pub fn toggle_folder_expanded(&mut self, space_id: &SpaceId, folder_id: &FolderId) {
    if let Some(space) = self.spaces.get_mut(space_id) {
        if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
            folder.is_expanded = !folder.is_expanded;
        }
    }
}
```

**평가:** 단순하지만 정확. `space_id` 또는 `folder_id` 매칭 실패 시 silently no-op (R1.B1.2 가설 위험). 그러나 데이터 흐름 자체는 sound.

### C++ 측 — JSON 파싱

**`state_adapter.cc:377-378`:**
```cpp
std::optional<bool> expanded = dict->FindBool("isExpanded");
node.is_expanded = expanded.value_or(true);
```

Rust `serde(rename_all = "camelCase")` + `is_expanded: bool` (no `skip_serializing_if`)로 항상 JSON에 `"isExpanded"` 포함. `value_or(true)`는 fallback으로만 작동, 실제 사용 안 됨. **정상.**

### C++ 측 — RebuildTreeNodes 재귀

**`tab_list_view.cc:2795-2799`:**
```cpp
if (node.is_expanded && !node.children.empty()) {
  RebuildTreeNodes(parent, node.children, section, browser, node.folder_id);
}
```

`is_expanded` true이고 자식 있을 때만 inline 재귀. **정상.**

### Bridge → Refresh

**`state_adapter.cc:550-553`:**
```cpp
std::string updates = maho::core::HandleEvent(core, json.c_str());
if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->NotifyChanged();
}
```

매 dispatch마다 unconditionally `NotifyChanged()`. **정상.**

**`sidebar_view.cc:759-765`:**
```cpp
void MahoSidebarView::OnSpaceProfileBridgeChanged() {
  if (footer_view_) UpdateFooter(state_adapter_.BuildFooterModel(browser_));
  UpdateSpacesPaneActions();
  ScheduleRefreshAll();
}
```

`ScheduleRefreshAll` 항상 호출. 그러나 **이 안에서 `refresh_scheduled_` 가드로 코어 발이 묶일 수 있음 (R1.B1.1 가설)**.

---

## E. 진단 LOG로 다음 검증 필요한 것

이미 추가됨 (재빌드 필요):
- `[MAHO SBR DIAG] ScheduleRefreshAll entry refresh_scheduled_=...`
- `[MAHO SBR DIAG] coalesced (needs_re_refresh_=true)`
- `[MAHO SBR DIAG] OnFavoritesModelReady (async reply)`
- `[MAHO SBR DIAG] RefreshAll fired`

빌드 후 사용자가 폴더 클릭하면 다음 시나리오 중 하나로 분기:

**시나리오 X (B1.1 확정):**
```
ScheduleRefreshAll entry refresh_scheduled_=1
coalesced (needs_re_refresh_=true)
```
이후 `RefreshAll fired` 1번만 발생 → 다음 round trip에 needs_re_refresh로 한 번 더 fire되지만 그 시점에 state는 이미 안정화됨.

**시나리오 Y (rebuild는 발생하지만 visual 안 바뀜):**
```
ScheduleRefreshAll entry refresh_scheduled_=0
OnFavoritesModelReady (async reply)
RefreshAll fired
```
이러면 rebuild는 정상 → 문제는 그 안의 `BuildTabListModel`에서 stale data 읽음 → R1.B1.2 (Rust 측 토글 누락) 의심.

**시나리오 Z (rebuild 자체 발생 안 함):**
```
ScheduleRefreshAll entry refresh_scheduled_=...
(이후 OnFavoritesModelReady가 안 옴)
```
ThreadPool task가 invoke 안 됨 → 별도 인프라 이슈.

---

## F. 수정 계획 (영향 큰 순)

| # | Fix | 파일 | 라인 | 시간 |
|---|---|---|---|---|
| **F1** | R2 — `SetRestoredTabId` 호출 복구 | `state_adapter.cc::ParseTreeNode` 탭 분기 | ~340 | 10분 |
| **F2** | R3 — `OverlayActiveTabFields` URL overlay 복구 | `state_adapter.cc::OverlayActiveTabFields` | - | 5분 |
| **F3** | R1 — 진단 LOG 빌드 후 시나리오 확정 → 그 결과에 맞춰 fix | sidebar_view.cc + state_adapter.cc | 다양 | 30분~ |
| **F4** | C1 — `OnMouseEntered`에 `show_popup_callback_.Run()` 복구 | `tab_list_view.cc:1890` | 1줄 추가 | 1분 |
| **F5** | R5 — `SidebarSpaceHeaderRow::CanDrop` 엄격화 또는 drop target 제외 | `tab_list_view.cc:2228-2270` | - | 15분 |
| **F6** | R4 — 로딩 spinner 복구 (선택) | `tab_list_view.cc:1108-1130` | - | 10분 |

**즉시 실행 권장 순서:**
1. **F3 우선** — R1이 가장 critical UX 이슈 → 진단 LOG 빌드 후 시나리오 확정
2. **F1, F2 동시** — 별개 회귀, 빠르게 fix
3. **F4** — popup 복구 (사이클 마무리)
4. **F5, F6** — polish

---

## G. 참고 증거 파일

각 finding의 file:line 증거는 본문에 인라인 표기. 추가 검증이 필요한 경우:

- `bg_18c34a7c` (bridge → refresh path 분석): "ScheduleRefreshAll가 in-flight일 때 coalesce" 가설 도출
- `bg_740256cb` (full pipeline 분석): R2/R3/R5 git diff 기반 회귀 5건 도출
- 진단 LOG (`[MAHO TOGGLE DIAG]`): toggle dispatch가 정상 firing 확정
- `git log`: 최근 commit 모두 "backup: snapshot..." → 이 회귀들이 working tree commit 안 된 채로 누적된 상태

---

## H. 진단 모드 정리 (작업 완료 후)

다음 LOG들은 진단용이므로 작업 후 모두 제거:

```cpp
// tab_list_view.cc:3010
LOG(ERROR) << "[MAHO TOGGLE DIAG] ToggleFolderExpanded fired ...";

// sidebar_view.cc:606
LOG(ERROR) << "[MAHO SBR DIAG] ScheduleRefreshAll entry ...";
LOG(ERROR) << "[MAHO SBR DIAG] coalesced ...";
LOG(ERROR) << "[MAHO SBR DIAG] OnFavoritesModelReady ...";
LOG(ERROR) << "[MAHO SBR DIAG] RefreshAll fired";
```

`grep -n "MAHO TOGGLE DIAG\|MAHO SBR DIAG"`로 일괄 제거 가능.
