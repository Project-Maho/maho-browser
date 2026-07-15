# Maho Chromium Fork Incognito 모드 코드 분석 보고서

## 1. 문서 정보

| 항목 | 값 |
|---|---|
| 분석일 | 2026-07-13 (Asia/Seoul) |
| 대상 | 데스크톱 Chromium fork 및 Maho C++/Rust 브리지 |
| Maho 기준 브랜치/HEAD | `main` / `77c43532` |
| Chromium 기준 | `72f18f12ad47a`, Chromium `149.0.7812.0` |
| 분석 방식 | 현재 작업 트리 정적 분석 + 기존 단위 테스트 실행 |
| 결론 수준 | 코드 경로가 확정된 항목과 런타임 검증이 필요한 항목을 구분 |

> **스냅샷 주의:** 분석 당시 루트 작업 트리는 `git status --short` 기준 624개 항목이 표시되는 dirty 상태였다. 따라서 이 문서는 `77c43532` 커밋만의 감사가 아니라, 그 위에 존재하는 미커밋 변경과 `chromium/src`에 적용된 현재 오버레이를 포함한 스냅샷 분석이다. 기존 사용자 변경은 수정하지 않았다.

## 2. 요약 결론

Maho 데스크톱 앱은 Chromium의 Off-The-Record(OTR) 프로필, 메모리 전용 사이트 데이터 파티션, 세션 종료 시 OTR 프로필 폐기 구조를 그대로 사용한다. Maho의 전역 탭 레지스트리도 OTR 창을 제외하고, 최근 검색 저장과 확장 상태 브리지 일부에도 명시적 차단이 있다.

그러나 Maho가 추가한 프로세스 전역 기능 중 일부는 Chromium의 `Profile` 경계를 우회한다. 현재 코드 기준으로 다음 사항이 중요하다.

| 우선순위 | 판정 | 요약 |
|---|---|---|
| **P0 / Critical** | 확인됨 | MCP 브라우저 delegate가 모든 브라우저 창을 순회하면서 OTR 탭을 필터링하지 않는다. MCP 세션에 도달한 클라이언트는 Incognito 탭 목록·페이지 내용·스크린샷·콘솔·네트워크를 읽거나 탭을 조작할 수 있는 코드 경로를 가진다. |
| **P1 / High** | 확인됨 | AI의 탭/히스토리/북마크 선택 목록에는 OTR 차단이 있지만, `SubmitPrompt`, 현재 페이지 첨부, 브라우저 도구 실행과 페이지 컨텍스트 추출 경로에는 프로필 차단이 없다. Incognito 페이지 컨텍스트가 AI 런타임으로 전달될 수 있다. |
| **P1 / High** | 확인됨 | Incognito 창에도 Maho Sidebar/Spaces UI가 생성된다. OTR 프로필의 `GetPath()`가 원본 프로필 경로를 반환하므로 Space-프로필 매칭을 통과하고, 일반 프로필의 Space 이름·트리·즐겨찾기 등 전역 상태를 읽는다. |
| **P1 / High** | 확인됨 | Command Palette는 열린 탭만 OTR 프로필로 제한하지만, 일반 MahoCore의 Spaces·폴더·아카이브 탭·확장 목록을 Incognito 검색 컨텍스트에 그대로 넣는다. 일반 세션 메타데이터가 검색 결과에 노출될 수 있다. |
| **P2 / Medium** | 확인됨/정책 불일치 | Rust 검색은 Incognito에서 북마크를 제외하지만 C++이 Chromium 북마크 모델 결과를 다시 합친다. Chromium 기본 정책상 Incognito에서 북마크를 보는 것은 정상이나, 현재 Maho 테스트가 표현하는 “북마크 제외” 계약과는 충돌한다. |
| **P2 / Medium** | 잠재 결함 | 탭 프리뷰 캡처 코드는 OTR 가드 없이 JPEG를 MahoCore에 전달한다. 현재 생성 지점을 찾을 수 없어 실행 경로는 비활성으로 보이지만, 향후 활성화하면 private ID로 인식되지 않은 프리뷰가 디스크에 저장될 수 있다. |
| **P3 / Low** | 확인됨 | Incognito는 Chromium dark mode를 사용하지만 Maho의 프로세스 전역 Space tint가 계속 적용된다. 일반 창과의 시각적 구분이 약해질 수 있다. |

현재 상태는 “Chromium 기본 Incognito 저장소 격리는 유지되지만, Maho 프로세스 전역 기능까지 포함한 완전한 프라이버시 경계는 아니다”로 판정한다.

## 3. 범위와 판정 기준

### 포함 범위

- Chromium OTR 프로필 생성·파괴
- PrefService와 StoragePartition의 수명
- 히스토리·북마크·세션·다운로드 서비스 선택
- Maho 전역 탭/Space 모델과 FFI
- Command Palette
- Maho AI와 페이지 컨텍스트
- MCP 브라우저 자동화 delegate
- Sidebar/Spaces
- 확장 상태 브리지와 확장 사이드 패널
- 탭 프리뷰, 테마, 기존 테스트

### 제외 또는 미검증 범위

- 현재 소스 전체를 새로 빌드한 Maho 앱의 수동 E2E 실행
- OS 레벨 메모리, swap, crash dump, GPU 캐시, DNS 및 네트워크 사업자 로그
- AI 제공자/relay의 서버 측 보존 정책
- MCP 소켓의 실제 바인딩 주소, 인증·토큰 설정에 따른 외부 도달성 평가
- 모바일 네이티브 셸의 Private Tab UX 전체

### 심각도

- **P0 / Critical:** 권한을 가진 보조 기능이 OTR 페이지 내용 또는 조작 권한에 직접 도달한다.
- **P1 / High:** 일반 프로필 메타데이터나 페이지 컨텍스트가 OTR 경계를 넘거나, OTR 창에서 일반 상태를 변경할 수 있다.
- **P2 / Medium:** 계약 불일치, 잠재적 디스크 잔존, 또는 제한된 범위의 프라이버시 혼동이 있다.
- **P3 / Low:** 주로 시각적 신호나 방어 심층화 문제다.

## 4. Chromium 상류 동작

### 4.1 OTR 프로필 수명주기

Incognito 창 명령은 원본 프로필에서 primary OTR 프로필을 가져와 새 창을 연다.

```text
regular Profile
  -> GetPrimaryOTRProfile(create_if_needed=true)
  -> OffTheRecordProfileImpl
  -> Browser window / WebContents / in-memory StoragePartition
  -> 마지막 Incognito 창 종료
  -> ProfileDestroyer
  -> OTR KeyedService 및 StoragePartition 폐기
```

- 진입점: `chromium/src/chrome/browser/ui/browser_commands.cc:1131-1132`
- 생성 및 owner map 등록: `chromium/src/chrome/browser/profiles/profile_impl.cc:1001-1022`
- OTR PrefService 생성과 프로필 타입 설정: `chromium/src/chrome/browser/profiles/off_the_record_profile_impl.cc:157-189`
- 정책으로 Incognito가 비활성화된 경우의 `CHECK`: `chromium/src/chrome/browser/profiles/off_the_record_profile_impl.cc:191-200`
- KeyedService 및 StoragePartition 종료: `chromium/src/chrome/browser/profiles/off_the_record_profile_impl.cc:252-299`
- 마지막 Incognito 브라우저 종료 후 파괴 예약: `chromium/src/chrome/browser/ui/browser_manager_service.cc:83-130`
- owner map 제거 및 확장 session-only pref 정리: `chromium/src/chrome/browser/profiles/profile_impl.cc:1032-1046`

Maho의 `new_incognito` 액션도 별도 private 모델을 만들지 않고 Chromium의 동일 명령을 호출한다.

- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc:249-258`

### 4.2 저장소와 PrefService

OTR `BrowserContext`는 메모리 전용 StoragePartition만 허용하며, 그렇지 않으면 `CHECK`로 중단한다.

- `chromium/src/content/browser/browser_context.cc:130-152`

OTR prefs는 원본 프로필의 동기화 PrefService 위에 incognito overlay를 만들고, 명시된 persistent allowlist만 원본에 반영한다.

- `chromium/src/chrome/browser/prefs/pref_service_syncable_util.cc:20-27`

따라서 `profile->GetPrefs()`에 값을 쓴다는 사실만으로 디스크 유출이라고 단정할 수는 없다. 다만 allowlist, 별도 MahoCore 저장소, 프로세스 전역 singleton, 외부 AI/MCP 경로는 OTR PrefService의 보호 범위 밖이므로 각각 따로 감사해야 한다.

### 4.3 Chromium 서비스 선택은 “전부 별도”가 아니다

Chromium Incognito는 모든 서비스를 빈 인스턴스로 만드는 구조가 아니다. 일부 서비스는 원본 프로필로 redirect하되, 쓰기 진입점에서 차단한다.

| 서비스 | 상류 동작 | 근거 |
|---|---|---|
| HistoryService | 원본 프로필로 redirect 가능 | `chromium/src/chrome/browser/history/history_service_factory.cc:92-103` |
| 방문 기록 쓰기 | OTR이면 `HistoryTabHelper`가 서비스 반환을 거부 | `chromium/src/chrome/browser/history/history_tab_helper.cc:632-640` |
| BookmarkModel | 원본 프로필로 redirect | `chromium/src/chrome/browser/bookmarks/bookmark_model_factory.cc:80-94` |
| SessionService | Incognito에는 생성하지 않음 | `chromium/src/chrome/browser/sessions/session_service_factory.h:21-35` |
| DownloadHistory | OTR에서는 연결하지 않음 | `chromium/src/chrome/browser/download/download_core_service_impl.cc:71-78` |

이 구조 때문에 `BookmarkModelFactory::GetForBrowserContext(otr_profile)`은 일반 프로필 북마크를 반환한다. 이는 Chromium 기본 동작이며, Maho가 더 엄격한 정책을 원하면 호출 지점에서 별도 차단해야 한다.

또한 다운로드 기록은 남지 않더라도 사용자가 저장한 다운로드 파일 자체는 디스크에 남는다. 클립보드, 저장한 파일, 외부 서비스로 전송한 데이터도 Incognito 세션 종료 정리 대상이 아니다.

### 4.4 “디스크 쓰기 0”으로 해석하면 안 되는 이유

상류에는 CacheStorage, Cookie, FileSystem, IndexedDB, LocalStorage, ServiceWorker, session cookie, WebSQL 사용 후 일반 프로필 폴더 변경 여부를 비교하는 containment browser test가 있다. 그러나 현재 해당 본 테스트는 `DISABLED_StoringDataDoesNotModifyProfileFolder`로 비활성화되어 있고, 파일 allowlist도 허용한다.

- `chromium/src/chrome/browser/profiles/incognito_profile_containment_browsertest.cc:277-333`

따라서 본 보고서는 “핵심 사이트 데이터 파티션은 메모리 전용”이라고 판단하지만, “프로세스가 어떠한 파일도 쓰지 않는다”고 보증하지 않는다.

## 5. Maho 경계별 분석

### 5.1 정상적으로 차단되는 경로

#### 전역 탭 레지스트리

`MahoTabRegistry`는 normal browser type이면서 non-OTR인 브라우저에만 observer를 붙인다. 이 경계 덕분에 데스크톱 Incognito 탭의 create/update/close 이벤트는 일반적인 Maho Space/탭 영속화 경로로 들어가지 않는다.

- eligibility: `maho-chromium/browser/maho_tab_registry.cc:51-61`
- observer 부착: `maho-chromium/browser/maho_tab_registry.cc:535-544`

#### Command Palette의 현재 열린 탭과 검색어 저장

열린 탭 검색은 현재 `Profile*`가 같고 OTR 상태도 같은 브라우저만 사용한다. 또한 Incognito에서는 최근 검색어를 저장하지 않는다.

- 탭 소스 필터: `maho-chromium/browser/ui/views/command/maho_command_model.cc:87-94`
- OTR flag를 Rust FFI에 전달: `maho-chromium/browser/ui/views/command/maho_command_model.cc:450-459`, `:530-542`
- 검색어 저장 차단: `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc:1224-1251`, `:1303-1305`

#### 확장 상태 보조 브리지

Maho의 추가 확장 상태 브리지와 확장 side panel coordinator는 OTR 프로필 조회를 거부한다.

- `maho-chromium/browser/extensions/maho_extension_state_bridge.cc:39-49`
- `maho-chromium/browser/ui/views/side_panel/maho_extension_side_panel_coordinator.cc:23-33`

이는 Chromium 자체의 extension incognito 허용 정책을 대체하는 것이 아니라, Maho가 추가한 프로세스 전역 보조 상태를 OTR에서 차단하는 경계다.

#### Rust Private Tab 모델

Rust core에는 `Tab.is_private` 기반으로 히스토리·북마크 쓰기, 탭/Space 동기화, snapshot 영속화를 차단하는 별도 보호가 있다.

- 히스토리/북마크: `maho/crates/maho-core/src/maho_core.rs:2270-2352`
- sync: `maho/crates/maho-core/src/maho_core.rs:7125-7143`, `:7547-7572`, `:8126-8159`
- 탭/Space 저장: `maho/crates/maho-core/src/maho_core.rs:1440-1464`

다만 이것을 데스크톱 Chromium OTR의 1차 방어로 보면 안 된다. 데스크톱 OTR 탭은 `MahoTabRegistry` 단계에서 core에 들어가지 않으며, Command Palette FFI가 만든 임시 `TabViewModel`은 `is_private: false`로 설정한다. 이 경로의 프라이버시는 별도 `is_incognito` context flag에 의존한다.

- `maho/crates/maho-ffi/src/lib.rs:2084-2158`

### 5.2 F-01 — MCP가 OTR 탭을 열람·조작할 수 있음

**판정: P0 / Critical, 정적 코드 경로 확인됨**

`PostProfileInit`은 초기 non-OTR 프로필에서 한 번만 MCP delegate를 등록한다. 그러나 등록된 delegate는 process-global browser collection을 순회할 때 프로필이나 `IsOffTheRecord()`를 검사하지 않는다.

- process-global delegate 등록: `maho-chromium/browser/maho_browser_main_extra_parts.cc:1508-1523`
- 전체 탭 열거: `maho-chromium/browser/maho_browser_main_extra_parts.cc:341-365`
- active WebContents 선택: `maho-chromium/browser/maho_browser_main_extra_parts.cc:368-380`
- tab ID로 WebContents 선택: `maho-chromium/browser/maho_browser_main_extra_parts.cc:383-403`
- 활성 브라우저의 프로필 선택: `maho-chromium/browser/maho_browser_main_extra_parts.cc:684-694`
- 새 탭 생성 시 첫 활성 브라우저 프로필 사용: `maho-chromium/browser/maho_browser_main_extra_parts.cc:629-657`

이 delegate의 다른 메서드는 선택된 WebContents를 이용해 페이지 컨텍스트·접근성 snapshot·스크린샷·console/network 로그를 수집하거나 click/type/navigate/close를 수행한다. 앞의 공통 선택 함수에 OTR 가드가 없으므로 모든 후속 기능이 같은 영향을 받는다.

공격 가능 범위는 MCP 서버의 실제 인증과 네트워크 노출 설정에 따라 달라진다. 그러나 인증된 MCP 클라이언트가 존재한다는 전제에서 OTR을 보호하는 코드 경계는 현재 없다.

**권고:** MCP 정책을 “OTR 완전 제외”로 명시하고, 목록 열거·active 선택·ID lookup·create/close/navigation 모두에서 동일한 중앙 helper로 차단한다. 요청된 tab ID가 OTR이면 `not allowed in off-the-record context` 오류를 반환해야 한다. 단순히 탭 목록에서 숨기는 것만으로는 ID 직접 조회를 막지 못한다.

### 5.3 F-02 — AI 현재 페이지 컨텍스트 경로가 OTR을 차단하지 않음

**판정: P1 / High, 정적 코드 경로 확인됨**

보안 helper는 OTR 프로필을 AI attachment에 부적합하다고 정의한다.

- `maho-chromium/browser/ai/maho_ai_security_utils.h:31-38`

이 helper는 열린 탭 목록, 히스토리 검색, 북마크 검색에는 적용된다.

- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc:743-789`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc:792-858`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc:861-884`

반면 실제 prompt와 페이지 본문을 제출하는 경로에는 동일한 프로필 검사가 없다.

- initial prompt가 자동으로 browser context 첨부를 요청: `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc:702-705`
- `SubmitPrompt`에 profile gate 없음: `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc:931-969`
- Current Page/Open Tab attachment 추출: `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc:971-1040`
- legacy `attach_browser_context` 추출: `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc:1043-1054`
- extractor는 URL scheme만 검사하고 browser profile은 검사하지 않음: `maho-chromium/browser/ai/maho_ai_page_context_extractor.cc:297-356`
- browser tool executor도 같은 extractor를 직접 호출: `maho-chromium/browser/ai/maho_browser_tool_executor.cc:71-157`
- chat orchestration이 매 요청에서 페이지 컨텍스트 추출: `maho-chromium/browser/ai/maho_chat_orchestration_service.cc:270-314`

Command Palette에서도 `ai` 모드 진입과 AI panel 표시를 OTR에서 차단하지 않는다.

- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc:933-942`, `:978-984`

이 항목은 “AI가 모든 Incognito 페이지를 자동 전송한다”는 뜻은 아니다. 사용자가 AI 패널·첨부·도구를 실행하거나 initial prompt 경로를 사용할 때, OTR 여부와 무관하게 현재 페이지를 추출해 런타임 adapter로 넘길 수 있다는 뜻이다.

**권고:** UI 목록이 아니라 최종 데이터 경계에서 차단한다. `SubmitPrompt`, `MahoAiPageContextExtractor`, `MahoBrowserToolExecutor`, `MahoChatOrchestrationService` 모두가 OTR 프로필이면 fail-closed하도록 방어를 중복 적용해야 한다. 일반 텍스트 채팅을 Incognito에서 허용할지, AI 전체를 비활성화할지는 제품 정책으로 별도 결정할 수 있지만 페이지 컨텍스트 첨부는 기본 차단이 안전하다.

### 5.4 F-03 — Incognito Sidebar가 일반 Space 상태와 연결됨

**판정: P1 / High, 데이터 경로 확인됨; 최종 화면 동작은 런타임 재현 필요**

현재 적용된 `BrowserView`는 normal browser type이면 OTR 여부와 관계없이 Maho Sidebar를 생성한다.

- `chromium/src/chrome/browser/ui/views/frame/browser_view.cc:1020-1035`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc:52-74`

OTR 프로필의 `GetPath()`는 원본 프로필과 동일한 경로를 반환한다.

- `chromium/src/chrome/browser/profiles/off_the_record_profile_impl.cc:324-331`

Maho Space bridge는 profile path basename으로 Space 소유 프로필을 비교한다. 따라서 OTR 창은 원본 프로필의 Space와 일치하며 browser-scoped active Space를 상속한다.

- 조회 시 매칭: `maho-chromium/browser/maho_space_profile_bridge.cc:190-219`
- 새 browser seed: `maho-chromium/browser/maho_space_profile_bridge.cc:270-292`

Sidebar adapter는 이 Space ID로 process-global MahoCore의 V2 상태를 가져오고, Space 이름·아이콘·persisted tree를 구성한 뒤 현재 Incognito tab strip의 active state를 overlay한다.

- tree와 live tab overlay: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc:1024-1149`
- global core V2 state fetch: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc:1214-1247`

Space activation은 전역 core 상태를 변경하고, 연결된 프로필이 다르면 profile switch도 수행할 수 있다.

- `maho-chromium/browser/maho_space_profile_bridge.cc:92-122`
- Command Palette의 Space action: `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc:356-363`

결과적으로 일반 세션의 Space 이름, 폴더/탭 제목, 즐겨찾기 같은 메타데이터가 Incognito UI에 나타날 수 있고, OTR 창에서 일반 Space 상태를 변경할 가능성이 있다. 정확한 행 병합 및 클릭 결과는 현재 소스를 빌드한 앱에서 E2E로 확인해야 한다.

**권고:** 두 정책 중 하나를 명시해야 한다.

1. Incognito에서는 Maho Sidebar를 생성하지 않는다.
2. Sidebar를 유지하되 MahoCore Space 상태를 전혀 읽지 않는 OTR 전용 모델을 사용하고, 해당 OTR 창의 live tab strip만 표시한다.

두 번째 정책을 선택하면 Space 전환·즐겨찾기·아카이브·sync·profile switch 액션도 OTR에서 비활성화해야 한다.

### 5.5 F-04 — Command Palette의 전역 메타데이터 혼합

**판정: P1 / High, 정적 코드 경로 확인됨**

C++은 열린 탭을 현재 OTR 프로필로 제한하고 `is_incognito`를 전달한다. 그러나 Rust core는 `external_tabs` 외의 소스를 process-global MahoCore 상태에서 항상 구성한다.

- MahoCore가 Spaces·아카이브·북마크·closed tabs·확장·폴더를 구성: `maho/crates/maho-core/src/maho_core.rs:2165-2236`
- Spaces 검색은 OTR 조건 없음: `maho/crates/maho-core/src/command_bar.rs:440-463`
- 폴더 검색은 OTR 조건 없음: `maho/crates/maho-core/src/command_bar.rs:465-488`
- 아카이브 탭 검색은 OTR 조건 없음: `maho/crates/maho-core/src/command_bar.rs:490-513`
- 확장 목록과 action 검색도 OTR 조건 없음: `maho/crates/maho-core/src/command_bar.rs:579-629`
- 북마크·closed tabs·history만 명시적으로 제외: `maho/crates/maho-core/src/command_bar.rs:515-577`, `:631-660`
- AI intent suggestion 제외: `maho/crates/maho-core/src/command_bar.rs:393-412`

즉 현재 테스트가 검사하는 history/bookmark는 숨겨지지만, 일반 프로필의 Space 이름, 폴더 이름, 아카이브 탭 제목/URL, 설치 확장 이름은 Incognito 검색 결과에 나타날 수 있다. `open_space:*` action은 실제 일반 core Space를 활성화한다.

**권고:** `is_incognito`일 때 SearchContext 자체를 최소화한다. 허용 소스를 `external_tabs`, URL navigation, web search 및 안전한 local window action의 allowlist로 제한한다. 결과 단계에서 필터링하면 이미 favicon 조회나 보조 처리로 데이터가 전달될 수 있으므로, source 수집 전에 비워야 한다.

### 5.6 F-05 — C++ 북마크 보강이 Rust Incognito 정책을 무효화

**판정: P2 / Medium, 정책 계약 불일치**

Rust engine은 `is_incognito`일 때 bookmarks를 결과에서 제외한다. 그러나 C++은 현재 browser profile로 Chromium BookmarkModel을 조회한다. 상류 factory가 OTR을 원본 프로필로 redirect하므로 일반 북마크가 반환된다.

- Chromium bookmark 수집: `maho-chromium/browser/ui/views/command/maho_command_model.cc:365-387`
- Rust 검색과 함께 전달: `maho-chromium/browser/ui/views/command/maho_command_model.cc:450-459`
- core 결과에 bookmark가 없으면 Chromium 북마크 전체를 append: `maho-chromium/browser/ui/views/command/maho_command_model.cc:549-559`, `:649-653`
- Rust의 bookmark 차단: `maho/crates/maho-core/src/command_bar.rs:515-545`
- 상류 redirect: `chromium/src/chrome/browser/bookmarks/bookmark_model_factory.cc:80-94`

이것은 Chromium 기본 UX와 일치할 수 있지만, Maho 테스트와 core API가 표현하는 “Incognito 검색에서는 bookmark 제외” 동작과는 일치하지 않는다.

**권고:** 먼저 제품 계약을 결정한다.

- **엄격 격리:** Incognito이면 `BuildChromiumBookmarkSuggestions`를 호출하지 않는다.
- **Chromium 호환:** Incognito에서 북마크 표시를 공식 정책으로 문서화하고 Rust 차단 및 테스트를 그 정책에 맞게 변경한다.

어느 경우든 C++과 Rust가 서로 다른 정책을 적용하는 상태는 제거해야 한다.

### 5.7 F-06 — 탭 프리뷰 캡처의 잠재적 디스크 잔존

**판정: P2 / Medium 잠재 결함, 현재 실행 경로는 비활성으로 보임**

`MahoTabPreviewCapture`는 모든 browser collection을 관찰하도록 작성되어 있고 OTR 필터 없이 이전 active tab의 화면을 JPEG로 core에 전달한다.

- collection observer: `maho-chromium/browser/maho_tab_preview_capture.cc:84-99`, `:117-140`
- JPEG를 core로 전달: `maho-chromium/browser/maho_tab_preview_capture.cc:142-183`

core의 preview 저장은 `private_ids`에 포함된 ID만 제외한다. 데스크톱 OTR 탭은 core tab manager에 등록되지 않으므로, 프리뷰만 들어오면 해당 ID가 private set에 없을 수 있다.

- `maho/crates/maho-core/src/tab_preview_manager.rs:87-100`
- `maho/crates/maho-core/src/maho_core.rs:1440-1456`

그러나 전체 저장소 검색 결과 constructor 정의 외에 `MahoTabPreviewCapture` 인스턴스를 만드는 코드가 없었다. 현재 `g_instance`는 생성되지 않는 것으로 보이며, 따라서 현 빌드에서 실제 캡처가 발생한다고 판정하지 않는다.

**권고:** 기능 활성화 전에 C++ capture 진입점에서 `web_contents->GetBrowserContext()->IsOffTheRecord()`를 차단하고, Rust `update_tab_preview`도 존재하지 않는 tab ID 또는 private tab ID를 거부하도록 방어한다. 비활성 코드에도 회귀 테스트를 추가해야 한다.

### 5.8 F-07 — Space tint가 Incognito에도 적용

**판정: P3 / Low**

Incognito 창은 dark `ColorProviderKey`를 사용하지만, Maho color mixer는 key에서 Incognito 여부를 구분할 수 없어 process-global Space theme tint를 계속 적용한다. 코드에도 TODO가 남아 있다.

- `maho-chromium/browser/ui/theme/maho_color_mixer.cc:470-476`
- `maho-chromium/browser/ui/theme/maho_color_mixer.cc:527-539`

기능적 데이터 유출은 아니지만, 일반 Space 색상이 Incognito 창에 보이면 사용자가 현재 프라이버시 모드를 잘못 인지할 수 있다.

## 6. 동작 매트릭스

| 영역 | 일반 창 | Incognito 현재 상태 | 판정 |
|---|---|---|---|
| Chromium cookies/site data | 디스크 기반 | OTR memory partition | 정상 |
| Chromium 방문 기록 쓰기 | 저장 | `HistoryTabHelper`에서 차단 | 정상 |
| Chromium 북마크 읽기 | 원본 모델 | 원본 모델 공유 | Chromium 기본 동작 |
| Chromium session restore | 사용 | 서비스 없음 | 정상 |
| 다운로드 기록 | 저장 | history 미연결 | 정상, 파일 자체는 남음 |
| Maho global tab registry | 등록 | observer 미부착 | 정상 |
| Maho recent command query | 저장 | 저장 차단 | 정상 |
| Command Palette live tabs | 같은 프로필 탭 | 같은 OTR 프로필 탭만 | 정상 |
| Command Palette history/closed | 표시 | 숨김 | 정상 |
| Command Palette bookmarks | 표시 | Rust는 숨기나 C++이 재삽입 | 계약 불일치 |
| Command Palette Space/folder/archive/extension | 표시 | 일반 core 상태 표시 가능 | 경계 누락 |
| Maho extension state bridge | 사용 | 조회 거부 | 정상 |
| Maho Sidebar/Spaces | global core 상태 | 동일 global core 상태 + OTR live overlay | 경계 누락 |
| Maho AI picker | 표시 | tabs/history/bookmarks 빈 목록 | 부분 정상 |
| Maho AI page context/tools | 페이지 추출 가능 | OTR profile gate 없음 | 경계 누락 |
| MCP browser automation | 전체 창 제어 | OTR 탭도 대상 가능 | Critical 경계 누락 |
| Tab preview capture | 코드 존재 | OTR guard 없음, 현재 미생성 | 잠재 결함 |

## 7. 테스트 결과와 공백

### 실행한 테스트

1. Rust Incognito 검색 테스트

   ```bash
   cd maho
   cargo test -p maho-core incognito -- --nocapture
   ```

   결과: 성공. Incognito 관련 3개 테스트가 통과했다.

   - history/bookmark ranking 제외: `maho/crates/maho-core/tests/ranking_regression.rs:378-409`
   - CommandBarQuery history/bookmark 제외: `maho/crates/maho-core/tests/test_batch_1.rs:1073-1095`
   - SearchHistory 빈 결과: `maho/crates/maho-core/tests/test_batch_1.rs:1917-1952`

   기존 경고로 `sync_manager.rs`의 unused `mut`, `boost_manager.rs` 테스트의 non-snake-case 경고가 있었으나 테스트 실패는 없었다.

2. C++ 확장 상태 OTR 테스트

   ```bash
   chromium/src/out/Default/unit_tests \
     --gtest_filter='MahoExtensionStateBridgeTest.FromProfileNullForIncognito' \
     --test-launcher-jobs=1
   ```

   결과: 성공, 1개 테스트 통과.

   - `maho-chromium/browser/extensions/maho_extension_state_bridge_unittest.cc:66-72`

   실행 중 macOS Objective-C class duplicate 경고가 있었지만 해당 테스트는 통과했다.

### 테스트 공백

Maho C++ 테스트 파일에서 Incognito/OTR 키워드를 가진 것은 확장 상태 브리지 테스트 하나뿐이었다. 다음 경계는 자동 테스트가 없다.

- MCP가 OTR 탭을 열거·조회·조작하지 못하는지
- AI `SubmitPrompt`/attachment/browser tool이 OTR 페이지를 거부하는지
- Incognito Sidebar가 일반 Space 상태를 읽지 않는지
- C++ Command Palette의 Chromium bookmark 보강이 Incognito 정책을 지키는지
- Spaces·folders·archived tabs·extensions가 Incognito 검색에서 제외되는지
- preview capture가 OTR WebContents를 거부하는지
- 마지막 Incognito 창 종료 후 Maho process-global cache에 OTR 식별자·URL·thumbnail이 남지 않는지

상류 containment browser test도 현재 disabled이므로, fork 기능을 포함하는 별도 통합 테스트가 필요하다.

## 8. 권고 구현 순서

### 1단계 — 출시 차단 수준 경계

1. MCP의 모든 WebContents 선택 경로에 central OTR deny helper 적용.
2. AI의 최종 page-context 추출 및 tool execution 경계에서 OTR fail-closed.
3. 위 두 영역에 OTR browser test 또는 delegate 단위 테스트 추가.

### 2단계 — Maho 전역 상태 분리

1. Incognito Sidebar 정책 결정: 미생성 또는 live OTR tabs 전용 모델.
2. Command Palette의 Incognito SearchContext를 allowlist 기반으로 재구성.
3. Space activate/profile switch/global archive/favorite action을 OTR에서 차단.
4. 북마크 표시 정책을 C++·Rust·테스트에서 하나로 통일.

### 3단계 — 방어 심층화

1. `MahoTabPreviewCapture`가 활성화되기 전에 C++/Rust 양쪽 가드 추가.
2. process-global cache와 shell event에 profile/OTR provenance를 명시적으로 포함.
3. Incognito 전용 시각 신호가 Space tint에 덮이지 않게 ColorProvider 경로 조정.
4. fork 전용 profile-directory containment test를 추가하고, MahoCore LMDB/SQLite·preview 저장소까지 snapshot 비교 범위에 포함.

## 9. 권장 중앙 정책 형태

분산된 `IsOffTheRecord()` 검사만 늘리면 새 기능이 같은 실수를 반복하기 쉽다. 최소한 C++ 기능 경계에서 다음 원칙을 공유하는 것이 적합하다.

```text
PrivateContextPolicy
  browser/profile 없음          -> deny
  profile->IsOffTheRecord()      -> deny by default
  명시적 OTR-safe 기능           -> local window state만 허용
  process-global MahoCore 조회   -> 별도 allowlist 없으면 deny
  외부 AI/MCP 전송 또는 조작     -> deny
  disk/sync/history/preview write -> deny
```

허용 예외는 “Incognito에서도 동작해야 한다”가 아니라 “어떤 데이터만 사용하며 세션 종료 후 어디에도 남지 않는가”를 테스트로 증명해야 한다.

## 10. 최종 판정

Chromium 자체의 OTR 저장소와 프로필 수명주기는 유지되고 있으며, Maho의 탭 레지스트리·최근 검색 저장·확장 보조 브리지에는 유효한 차단이 있다. 그러나 Maho가 추가한 MCP, AI context, Sidebar/Spaces, Command Palette의 process-global 데이터 소스는 동일한 경계를 일관되게 적용하지 않는다.

특히 MCP는 Incognito WebContents를 직접 선택하고 제어할 수 있는 경로이므로 가장 먼저 차단해야 한다. 그 다음으로 AI page context와 Sidebar/Command Palette의 일반 프로필 메타데이터 혼합을 정리해야 한다. 이 세 영역이 해결되고 fork 전용 E2E 격리 테스트가 추가되기 전까지는 Maho 전체 기능 기준의 Incognito 격리를 보장한다고 표현하기 어렵다.
