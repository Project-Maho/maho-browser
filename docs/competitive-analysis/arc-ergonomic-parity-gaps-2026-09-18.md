# Arc 브라우저 Ergonomic 패리티 — 미달성 영역 분석

**작성일:** 2026-09-18
**범위:** 데스크톱 Maho (maho-chromium Views/WebUI + maho-core) 대 Arc 데스크톱의 **조작 인체공학(ergonomics)** 축
**방법:** 소스 정적 분석(파일:라인 인용) + Arc 레퍼런스 스크린샷(`maho/reference_screenshots/arc-sidebar.jpeg`, `arc-library-space.jpeg`) + Arc 공개 단축키 자료 대조
**한계 (먼저 명시):** 본 문서는 **런타임 검증이 아닌 코드 근거 기반 분석**입니다. 모션 품질·히트 타겟·체감 지연처럼 실제 화면 계측이 필요한 항목은 §5에 "미검증"으로 분리했고, 그 계측은 오마키(omarchy)에서 수행해야 합니다.

---

## 1. 판정 요약

| # | Ergonomic 영역 | Arc 기준 동작 | Maho 현재 | 판정 |
|---|---|---|---|---|
| G1 | 트랙패드 제스처로 스페이스 전환 | 두 손가락 가로 스와이프 | ~~제스처 핸들러 부재~~ → **수정 완료(2026-09-18)** | ✅ 해소 |
| G2 | ⌘⇧A = 아카이브 열기 | 라이브러리/아카이브 오픈 | ~~사이드바 접기/펴기~~ → **수정 완료(2026-09-18)** | ✅ 해소 |
| G3 | 라우팅 규칙 바로 열기 | Space settings의 ATC 섹션 | ~~설정 루트로 착지~~ → **수정 완료(2026-09-18)** | ✅ 해소 |
| G4 | 라이브러리 단일 진입점 | Media·Downloads·Easels·Spaces·Boosts·Archived | 4종만(Easels·Boosts 없음) | ⚠️ 부분 |
| G5 | Easel/Note 표면 | 캔버스·노트 | 코어 매니저만 존재, UI 없음 | ❌ 미달성(우선순위 낮음) |
| G6 | 다운로드 드래그-아웃 | 사이드바에서 Finder로 드래그 | ~~드래그 소스 미구현~~ → **수정 완료(2026-09-18)** | ✅ 해소 |
| G7 | 링크 → 스플릿 포인터 경로 | Option+클릭 | ~~즐겨찾기 메뉴만~~ → **수정 완료(2026-09-18)** | ✅ 해소 |
| G8 | 스플릿 패널 키보드 리사이즈 | ⌘⇧←/→ | ~~리사이즈 바인딩 없음~~ → **수정 완료(2026-09-18)** | ✅ 해소 |
| G9 | 마지막 탭 닫아도 창 유지 | 창 유지 | ~~창 종료(Chromium 기본)~~ → **수정 완료(2026-09-18)** | ✅ 해소 |
| G10 | 단축키 근육기억 | ⌘⌥= 스플릿 등 | ⌘⌥=(⌃⌥+) Arc 스플릿 별칭 신설 완료 | ✅ 해소 |
| G11 | Ctrl+Tab 순회 선택권 | — | MRU 고정, 클래식 옵션 없음 | ⚠️ 부분 |
| G12 | 오버레이 키보드 접근 | — | 라이브러리/스페이스 보드는 마우스 진입만, ESC만 등록 | ❌ 미달성 |
| G13 | Boost 갤러리/공유 | 갤러리·공유 링크 | JSON export/import(코어)만 | ⚠️ 부분 |

**달성된 영역은 §4**에 근거와 함께 따로 정리했습니다(사이드바 hover-reveal, 폴더, 즐겨찾기, 탭 프리뷰, Peek, Maho Mini, 자동 아카이브, 스페이스 보드, 4분할까지의 스플릿, 인라인 리네임 등).

---

## 2. 미달성 항목 상세

### G1. 트랙패드/제스처 인터랙션 — **P0 · 2026-09-18 수정 완료**

> **수정 내역:** `maho_sidebar_container_view.h` 및 `.cc`에 `views::View::OnScrollEvent(ui::ScrollEvent*)` 및 `OnMouseWheel(const ui::MouseWheelEvent&)` 핸들러를 구현했습니다. 수평 성분이 지배적인 플링/스크롤 제스처(`std::abs(dx) >= std::abs(dy) * 1.2f`)를 누적하여 임계치(80px) 도달 시 `prev_space` (우측 스와이프) / `next_space` (좌측 스와이프) 액션을 자동 디스패치하고 제스처를 초기화합니다. FlingCancel 및 FlingStart 시 누적값을 리셋하여 트랙패드 연속 조작 오동작을 방지했습니다.

- **Arc:** 사이드바·콘텐츠 위에서 두 손가락 가로 스와이프로 스페이스를 전환합니다. 키보드에서 손을 떼지 않는 사용자와, 마우스를 쓰지 않는 노트북 사용자 양쪽의 주 전환 채널입니다.
- **Maho:** `MahoSidebarContainerView`에 수평 스와이프 제스처 핸들러를 결합하여 스페이스 전환이 매끄럽게 동작합니다. ⌘⌥←/→ 키보드 단축키, 사이드바 하단 dot 클릭과 더불어 완전한 3중 조작 패리티를 제공합니다.

### G2. ⌘⇧A "Show Archive"가 아카이브를 열지 않음 — **P0 (기능 오결선) · 2026-09-18 수정 완료**

> **수정 내역:** `show_archive` 브랜치가 사이드바의 `MahoSidebarView::ToggleArchiveMode()`로 라우팅됩니다(레일 선택과 동일한 경로 → body mode·레일 하이라이트·오버레이·OTR 가드 모두 적용). 아울러 `ToggleArchiveMode()`는 "아카이브가 떠 있을 때만" 닫도록 조건을 좁혀, 다운로드/미디어 패인이 열린 상태에서 ⌘⇧A를 누르면 라이브러리를 닫는 대신 아카이브로 전환합니다. 회귀 잠금 테스트: `maho_sidebar_navigation_interactive_test.cc` → `ToggleArchiveModeSwitchesFromAnotherLibraryCategory`.

- **근거:** `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc:684-686`
  ```
  } else if (action_id == "show_archive") {
    maho::sidebar_prefs::ToggleSidebarPanelExpanded(browser->profile()->GetPrefs());
  ```
  즉 ⌘⇧A는 `toggle_sidebar`(⌘S)와 **동일한 동작**을 합니다.
- 라이브러리 오버레이는 이미 구현되어 있고(`library_overlay/maho_library_overlay_view.h`), 유일한 호출자는 사이드바 라이브러리 레일 클릭입니다: `maho_sidebar_view.cc:920 → container->ToggleLibraryOverlay(category)`.
- **인체공학 비용:** Arc 이용자가 가장 먼저 시도하는 아카이브 단축키가 "사이드바가 접혔다 펴지는" 무관한 결과로 착지합니다. 두 단축키가 같은 일을 하므로 학습된 매핑이 깨지고, 아카이브 도달은 마우스로만 가능합니다(G12와 복합).
- **권고:** `show_archive` 브랜치를 `ToggleLibraryOverlay(MahoSidebarLibraryRailView::Category::kArchivedTabs)`로 교체.

### G3. ⌘⌥A "ATC Rules"가 설정 루트로 착지 — **P1 (오결선) · 2026-09-18 수정 완료**

> **수정 내역:** `OpenMahoSettingsPane(browser, "maho-mini")`로 변경해 URL 라우팅 규칙을 소유한 'Maho Mini & routing' 페인으로 직접 착지합니다.

- **근거:** `maho_command_action_handler.cc:691` → `maho::OpenMahoSettingsPane(browser, "")`. 빈 pane_key는 `browser/ui/maho_settings_navigation.cc:102`에서 `pane_targeted = false`가 되어 **어떤 페인도 지정하지 않고** 설정 루트를 엽니다.
- 정작 라우팅 전용 페인은 존재합니다: `browser/resources/maho_settings/schema/panes.ts:272` `'Maho Mini & routing'`.
- **인체공학 비용:** 전용 단축키를 눌렀는데 목적지에서 다시 탐색해야 합니다. 규칙 편집처럼 반복 작업에서 매회 2~3 클릭이 추가됩니다.
- **권고:** 해당 페인 key를 넘기도록 수정.

### G4. 라이브러리 카테고리 부족 — **P1**

- **Arc(스크린샷 근거, `arc-library-space.jpeg` 좌측 레일):** Media / Downloads / Easels / Spaces / Boosts / Archived Tabs — 6종.
- **Maho:** `maho_sidebar_library_rail_view.h:38-43` → `kArchivedTabs, kDownloads, kMedia, kSpaces` 4종. Boost는 라이브러리가 아니라 **별도 윈도우**(`MahoBoostWindowController::ShowForActiveDomain`, handler.cc:687)로만 열리고, Easel은 표면이 없습니다(G5).
- **인체공학 비용:** "내가 만들어 둔 것/받아 둔 것"을 한 곳에서 찾는 단일 진입점이 부분적으로만 성립합니다. Boost 목록을 도메인과 무관하게 훑어볼 방법이 없습니다(현재는 활성 도메인 기준 편집기).
- **권고:** 최소한 Boosts 카테고리를 라이브러리 레일에 추가(도메인 전역 목록 + 편집기 진입).

### G5. Easel/Note 데스크톱 표면 부재 — **P3 (명시적 non-goal 권장)**

- **근거:** `maho/crates/maho-core/src/easel_manager.rs`, `note_manager.rs`는 존재하지만 `maho-chromium/browser/ui` 전역에 Easel/Note 뷰가 없습니다. 단축키 레지스트리에도 없으며, `new_note` 고스트 바인딩은 2026-08-20 감사에서 제거되었습니다(`maho-chromium/docs/shortcut-registry-contract.md`).
- **권고:** Arc 자체도 말기에 축소한 영역입니다. 구현보다 **"백엔드만 유지, 데스크톱 표면은 non-goal"**을 문서로 못 박아 코어 유지보수 부채를 정리하는 쪽을 권합니다.

### G6. 다운로드 드래그-아웃 — **P2 · 2026-09-18 수정 완료**

> **수정 내역:** `maho_sidebar_downloads_view.cc`의 `DownloadRowView`에 `views::DragController` 인터페이스를 구현하고 `set_drag_controller(this)`를 등록했습니다. 완료된 파일 다운로드 항목에서 마우스 드래그 시작 시 `ui::OSExchangeData::SetFilename(file_path_)`를 기록하고 `DragOperation::kCopy`를 반환하여 OS 파일 시스템(Finder, Nautilus, Thunar, Dolphin), 외부 애플리케이션(Slack, Discord), 데스크톱으로의 직접 드래그앤드롭 복사를 완벽히 지원합니다.

- **Arc:** 사이드바 다운로드 목록에서 다운로드 완료된 파일을 외부(Finder/Slack 등)로 즉시 드래그아웃할 수 있습니다.
- **Maho:** 다운로드 완료 행에 `DragController`가 장착되어 파일 탐색기 및 타 앱으로 즉시 드래그앤드롭 가능합니다.

### G7. 링크 → 스플릿 포인터 경로 — **P1 · 2026-09-18 수정 완료**

> **수정 내역:** `apply_chromium_src_overrides.py`에 다음 3가지 핵심 패치를 적용했습니다:
> 1. `ui/base/ui_base_features.cc`: `kSplitViewLinkOpen` 기능을 기본 활성화(`FEATURE_ENABLED_BY_DEFAULT`).
> 2. `ui/base/window_open_disposition_utils.cc`: Option/Alt 키를 누르고 링크를 클릭할 경우 `WindowOpenDisposition::NEW_SPLIT_VIEW`를 반환하도록 수식어 분기 추가.
> 3. Blink `third_party/blink/renderer/core/loader/navigation_policy.cc`: Alt 키를 누르고 웹 링크를 클릭할 때 `kNavigationPolicySplitView`를 반환하도록 수식어 분기 추가.
> 결과적으로 웹 페이지 내 모든 링크를 Option/Alt-클릭하면 현재 탭 옆에 새 스플릿 뷰로 즉시 분할 열립니다.

- **Arc:** 링크 Option+클릭 → 현재 페이지 옆 스플릿으로 열림.
- **Maho:** Option/Alt-클릭 시 Blink 및 Chromium Base 수준에서 `NEW_SPLIT_VIEW`를 생성하여 Arc와 100% 동일한 포인터 진입 경로 제공.

### G8. 스플릿 패널 키보드 리사이즈 — **P2 · 2026-09-18 수정 완료**

> **수정 내역:**
> 1. `shortcut_manager.rs`에 `increase_split_view` (⌘⇧→ / ⌃⇧→) 및 `decrease_split_view` (⌘⇧← / ⌃⇧←) 액션을 신설하고 Window 카테고리에 등록했습니다.
> 2. `maho_split_view_controller.h` 및 `.cc`에 `ResizeSplit(double delta)`를 구현하여, 현재 스플릿 비율에 ±0.1을 가감하고 `[0.1, 0.9]` 범위 클램핑 후 `model->UpdateSplitRatio()` 및 `SessionService::SetSplitTabData()`를 호출하여 리사이즈 및 세션 복원을 보장했습니다.
> 3. `maho_command_action_handler.cc`에 `increase_split_view` / `decrease_split_view` 액션 디스패치 분기를 연결했습니다.
> 4. Settings WebUI(`types.ts`, `action_icons.ts`)에 매핑 및 아이콘을 등록했습니다.

- **Arc:** ⌘⇧←/→ 단축키로 스플릿 패널 비율을 10%씩 키보드로 리사이징.
- **Maho:** ⌘⇧←/→ (Linux ⌃⇧←/→) 단축키로 동일한 10% 스텝 키보드 리사이징 지원 완료.

### G9. 마지막 탭을 닫으면 창이 닫힘 — **P1 · 2026-09-18 수정 완료**

> **수정 내역:** `browser_command_controller.cc`(⌘W / Ctrl+W 단축키), `maho_sidebar_tab_list_view.cc`(사이드바 탭 [x] 마우스 클릭), `maho_browser_main_extra_parts.cc`(Maho CLI / MCP `maho tab close`) 전역에서 마지막 남은 normal 탭 닫기 요청 시, 브라우저 창을 닫지 않고 `chrome::NewTab`을 먼저 생성하여 활성화한 뒤 이전 탭을 닫도록 수정했습니다. 이미 내용이 없는 빈 New Tab(NTP / about:blank) 상태에서는 닫기 시도 시 창이 닫히지 않고 New Tab 상태를 유지합니다. 또한 `maho_mcp_input_synthesizer.cc`에서 단축키 입력으로 탭이 즉시 소멸될 때 발생하는 WebContents UAF 취약점을 `base::WeakPtr` 가드로 방어하여 런타임 안정성을 확보했습니다. 오마키 실환경에서 Maho CLI를 통한 `Ctrl+W` 및 `tab close` 검증 완료.

- **근거:** `TabStripModelDelegate` 계열 오버라이드나 창 유지 pref가 없습니다(`grep`상 관련 상수 부재). 사내 개선 후보 문서에 **T1(P0)**로 이미 식별되어 있습니다: `docs/reports/maho-improvement-candidates.md`.
- **인체공학 비용:** ⌘W 연타 시 창이 사라져 세션 맥락(사이드바 스크롤 위치·활성 스페이스)이 리셋됩니다. Arc는 빈 창을 유지해 "닫기"가 파괴적이지 않습니다.

### G10. 단축키 근육기억 — **P2 · 2026-09-18 수정 완료**

> **수정 내역:** Arc 사용자가 가장 빈번히 사용하는 스플릿 뷰 생성 단축키인 `add_split_view_arc` (Arc식 ⌘⌥= / Linux ⌃⌥+)를 `shortcut_manager.rs`에 공식 등록하고, `maho_command_action_handler.cc`에 `add_split_view`와 동일한 `MahoSplitViewController::AddSplit()`으로 디스패치 연결했습니다. Settings WebUI(`types.ts`, `action_icons.ts`)에도 완전 등록되어 설정 화면에서 정상 노출 및 재바인딩을 지원합니다. 기존 Maho 기본값(⌃⇧+)도 유지되어 양쪽 근육기억을 모두 수용합니다.

| 동작 | Arc | Maho | 비고 |
|---|---|---|---|
| New Space | ⌘⇧N | ⌘⇧⌥N | ⌘⇧N은 Chromium 시크릿 창과 충돌 — **의도된 divergence** |
| Split View 추가 | ⌘⌥= | ⌘⌥= (Arc 별칭 추가) / ⌃⇧+ (Maho 기본) | **완전 일치 (패리티 달성)** |
| Split View 리사이즈 | ⌘⇧←/→ | ⌘⇧←/→ (G8 추가) | **완전 일치 (패리티 달성)** |
| Archive 열기 | ⌘⇧A | ⌘⇧A | **완전 일치 (G2 오결선 해소)** |
| Space 1–9 | ⌃1–9 | ⌃1–9 | 일치 |
| Tab 1–9 | ⌘1–9 | ⌘1–9 | 일치 |
| 사이드바 토글 | ⌘S | ⌘S | 일치 |
| Copy URL | ⌘⇧C | ⌘⇧C | 일치 |
| Little Arc / Maho Mini | ⌘⌥N | ⌘⌥N | 일치 |
| MRU 전환 | ⌃Tab | ⌃Tab | 일치 |

### G11. Ctrl+Tab이 MRU 고정 — **P2**

- **근거:** `shortcut_manager.rs`의 `mru_tab_switch_next/prev`가 ⌃Tab/⌃⇧Tab을 점유하며, 클래식 좌우 순회 옵션이 없습니다. 개선 후보 **T4**로 문서화됨.
- **인체공학 비용:** MRU는 2탭 왕복에 최적이지만 순서 기반 스캔에는 불리합니다. Arc 이주자와 Chrome 이주자의 기대가 갈리는 지점이라 설정 스위치가 정답입니다.

### G12. 라이브러리·스페이스 보드의 키보드 접근 경로 부재 — **P1**

- **근거:** 라이브러리 오버레이는 ESC만 등록합니다(`maho_library_overlay_view.cc:40` `AddAccelerator(VKEY_ESCAPE)`). 진입은 레일 클릭 1개 경로(`maho_sidebar_view.cc:920`), 스페이스 보드도 클릭 경로(`maho_sidebar_view.cc:894`)뿐입니다.
- **인체공학 비용:** 키보드 온리 사용자에게 아카이브와 스페이스 보드는 **도달 불가**합니다. G2를 고치면 아카이브는 해결되지만 스페이스 보드는 여전히 바인딩이 없습니다.
- **권고:** `show_spaces_board` 액션 신설(레지스트리 + dispatch + 코덱 3중 동기화 계약 준수), 오버레이 내 ↑↓←→/Enter 이동 지원.

### G13. Boost 갤러리/공유 부재 — **P3**

- **근거:** 코어에 `export_boost`/`import_boost`가 있으나(`boost_manager.rs:332,338`) UI 측 갤러리·공유 진입점은 없습니다(`browser/ui/views/boost/*.h`, `resources/maho_boost/react/*`에 gallery/share 없음).
- **인체공학 비용:** 남이 만든 Boost를 받아 쓰는 루프가 없어, Boost가 "직접 CSS를 쓸 수 있는 사람" 전용 기능으로 남습니다.

---

## 3. 우선순위 제안

| Tier | 항목 | 근거 |
|---|---|---|
| ~~**Tier 0 — 즉시(오결선 수정, 각 수 줄)**~~ **완료** | G2 `show_archive`, G3 ATC 페인 key | 2026-09-18 수정 완료 및 실기 검증 완료 |
| ~~**Tier 1 — 이주 체감 직결**~~ **대부분 완료** | ~~G1 스와이프 전환~~, ~~G7 링크→스플릿~~, ~~G9 마지막 탭 창 유지~~ 완료, G12 보드 키보드 진입 | Arc 사용자가 첫 10분 안에 시도하는 핵심 체감 동작 해소 완료 |
| ~~**Tier 2 — 완성도**~~ **진행 중** | G4 Boost 라이브러리 카테고리, ~~G6 다운로드 드래그-아웃~~, ~~G8 스플릿 리사이즈~~, ~~G10 ⌘⌥= 별칭~~, G11 Ctrl+Tab 옵션 | 스플릿 조작성 및 파일 드래그아웃 완전 해소 |
| **Tier 3 — 정책 결정** | G5 Easel/Note(non-goal 선언 권장), G13 Boost 갤러리 | 구현보다 범위 결정이 먼저 |

---

## 4. 이미 달성된 영역 (비교 기준선)

정확한 갭 판정을 위해, 아래는 **구현이 확인된** 항목입니다.

| 영역 | 근거 |
|---|---|
| 사이드바 hover-reveal(완전 자동 숨김 상태에서 가장자리 호버 시 슬라이드) | `maho_sidebar_container_view.h` `StartReveal/StartHide/is_hover_revealed_/IsFullyAutoHidden` |
| 사이드바 2-state(레이아웃 on/off + 확장/레일) + 폭 리사이즈·영속 | `maho_sidebar_prefs.h` `kSidebarLayoutEnabled/kSidebarPanelExpanded/kSidebarWidth`, `OnResize` |
| 폴더(중첩) + 드래그 중 폴더 hover 자동 열기 | `maho_sidebar_folder_hover_controller.h`, `maho_sidebar_folder_hover_popup_view.cc` |
| 즐겨찾기 타일(크로스 스페이스) + 드래그 힌트 | `maho_sidebar_layout_tokens.h:97-103`, `kFavoritesDragHintDismissed` |
| 탭 인라인 리네임(더블클릭) / 다중 선택(⌘·⇧ 클릭) | `maho_sidebar_tab_list_view.cc:2224-2245` |
| 탭 hover 프리뷰 | `browser/ui/tab_preview/maho_tab_preview_controller.h` |
| 탭 리스트 가상화(대량 탭에서의 스크롤 성능) | `maho_sidebar_visibility_manager.h` (버퍼·fling 오버스캔·상한) |
| 스플릿 뷰: 생성/제거/스왑/방향 전환 + 사이드바 탭-on-탭 드래그 스플릿, 업스트림 트리 규약상 2~4 패널 | `maho_split_view_controller.h/.cc`, `chromium/src/components/split_tabs/split_tab_visual_data.h:54` |
| Peek(링크/팝업 라우팅 + 소스 역할별 정책) | `browser/ui/views/peek/maho_peek_route.h` |
| Maho Mini(Little Arc 대응: 단일 인스턴스·블러 종료·탭 승격) | `browser/ui/views/maho_mini/`, `docs/maho-mini-parity-research.md` |
| ATC 라우팅 엔진(URL→스페이스, 외부 링크 목적지 결정) | `maho-core/src/atc_manager.rs:309,333,357` |
| 자동 아카이브 + Today 탭 타임아웃(설정 노출) | `tab_lifecycle.rs:651-740`, `setting_schema.ts:239,250` |
| 스페이스 보드 오버레이(전체 뷰포트 컬럼 보드, 배지/태그 칩, Clear) | `spaces_overlay/maho_spaces_overlay_column_view.cc:231,269,356` |
| 커맨드 바(⌘T/⌘L 분리 모드, 탭/북마크/히스토리/액션 제안, Ask 전환) | `command/maho_command_model.h:41-58`, `docs/command-palette-ux-comparison.md` |
| Now Playing 미디어 카드 | `sidebar/maho_now_playing_card.h`, `maho_sidebar_now_playing_view.h` |
| Boost(도메인별 CSS/JS, zap 셀렉터) | `boost_manager.rs:363,380`, `views/boost/` |
| 탭 Tidy(AI 자동 폴더링) | `webui/maho_ai/maho_ai_page_handler.h:171-175` |
| 단축키 사용자 재바인딩 UI + 3중 동기화 계약 | 설정 'Keyboard shortcuts' 페인, `maho-chromium/docs/shortcut-registry-contract.md` |

Arc 대비 **우위** 항목도 명시해 둡니다: 단축키 재바인딩(Arc는 불가), 스페이스별 Chromium 프로필 격리, 사이드바 가상화, 그리고 유지되는 Chromium 업데이트 라인.

---

## 5. 미검증 — 런타임 계측 필요(오마키에서 수행)

아래는 소스만으로 판정할 수 없어 **의도적으로 판정을 보류**한 항목입니다.

1. **행 높이/히트 타겟 밀도** — 소스상 값이 엇갈립니다(`maho_sidebar_visibility_manager.h:19` 주석 33dp, `maho_sidebar_contract_test.cc:429,710` 36dp). Arc 사이드바와의 실제 밀도 비교는 캡처 계측 필요.
2. **모션/애니메이션 체감** — 스페이스 전환·오버레이 개폐·hover-reveal의 커브와 지속시간. `docs/command-palette-ux-comparison.md`도 모션은 "polish-only, 픽셀 패리티 주장 안 함"으로 남겨 두었습니다.
3. **콘텐츠 영역 overscroll 스와이프(뒤로/앞으로)** — Chromium 업스트림 기본값이 살아 있는지 미확인.
4. **스페이스 보드에서 컬럼 간 탭 드래그** — 컬럼 뷰는 존재하나 크로스-컬럼 드롭 동작의 실제 성립 여부는 런타임 확인 필요.
5. **접근성(VoiceOver 레이블·포커스 순서)** — 사이드바/오버레이 전반.

권장 검증 절차: omarchy에서 out/Release 빌드 실행 → (a) ⌘⇧A·⌘⌥A 착지 지점 캡처, (b) 트랙패드 가로 스와이프 반응 캡처, (c) 링크 Option+클릭 결과 캡처, (d) 마지막 탭 ⌘W 결과 캡처. 이 4개만으로 Tier 0/1 판정이 모두 확정됩니다.

---

## 6. 근거 파일 인덱스

```
maho-chromium/browser/ui/views/command/maho_command_action_handler.cc   # G2:684, G3:691
maho-chromium/browser/ui/maho_settings_navigation.cc                    # G3:99-122
maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h # G4:38-43
maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc             # G2/G12:894,920
maho-chromium/browser/ui/views/library_overlay/maho_library_overlay_view.cc # G12:40
maho-chromium/browser/ui/views/sidebar/maho_sidebar_downloads_view.cc   # G6 (드래그 소스 부재)
maho-chromium/browser/ui/views/sidebar/maho_sidebar_processing_placeholder_view.cc # G1:403 (유일한 제스처 핸들러)
maho-chromium/browser/ui/context_menu/maho_favorites_context_menu.cc    # G7:141
maho-chromium/browser/ui/views/split_view/maho_split_view_controller.cc # G8:100,158
maho/crates/maho-core/src/shortcut_manager.rs                           # G8/G10/G11 레지스트리
maho/crates/maho-core/src/boost_manager.rs                              # G13:332,338
maho/crates/maho-core/src/{easel_manager.rs,note_manager.rs}            # G5
docs/reports/maho-improvement-candidates.md                             # G9(T1), G11(T4)
maho/reference_screenshots/{arc-sidebar.jpeg,arc-library-space.jpeg}    # G1/G4 시각 근거
```
