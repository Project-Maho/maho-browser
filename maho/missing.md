# Missing Features (Arc Parity)

Arc 브라우저 대비 Maho 단축키 parity 현황.

## ✅ 완료 — 단축키 구현 완료

이전에 missing이었으나 현재 구현 완료된 항목.

| 단축키 | Arc 기능 | 구현 방식 |
|---|---|---|
| ⌘⌥O | Maho Mini → Open in Different Space | Space 선택 NSMenu 팝업 (MahoMiniWindowController) |
| ⌘⇧O | Organize Maho Mini Tabs | ATC 설정 안내 알럿 (기능 가이드 stub) |
| ⌘⌥G | Ask ChatGPT (Arc Max) | Coming Soon 알럿 (AI 인프라 미구현) |
| ⌃⌘J | Join Active Meeting | Coming Soon 알럿 (캘린더 연동 미구현) |

## 🔶 Stub 상태 — 기능 인프라 필요

단축키는 등록됐지만 실제 기능 구현이 필요한 항목.

| 단축키 | 기능 | 필요 작업 |
|---|---|---|
| ⌘⌥G | Ask AI | LLM 인프라 (Phase 0) + Command Bar AI 모드 (Phase 2) 구현 시 stub → 실제 연결 |
| ⌃⌘J | Join Meeting | 캘린더 API 연동 (Google Calendar / Apple Calendar) |
| ⌘⇧O | Organize Tabs | ATC rules 기반 자동 분류 로직 또는 LLM 기반 Tidy Tabs (Phase 5) |

## 현재 상태

- Maho Mini: `MahoMiniWindowController` — ⌘⌥N(열기), ⌘O(현재 Space에 열기), ⌘⌥O(다른 Space 선택) 모두 구현 완료
- Extensions: 설정 UI(`ExtensionsSettingsView`) 존재, ⌘E로 직접 열기 구현 완료
- AI: scaffold 플래그만 존재 — 아래 Maho AI 섹션 참조
- Calendar: notification 설정만 존재, 미팅 연동 없음

---

# Maho AI

Arc Max + Dia + Perplexity Comet + 기타 AI 브라우저 조사 기반 Maho AI 기능 명세 및 구현 계획.

## 경쟁 브라우저 AI 기능 분석

### 조사 대상

| 브라우저 | 개발사 | 포지셔닝 | 핵심 AI 패턴 |
|---|---|---|---|
| **Arc Max** | The Browser Company | AI 유틸리티 (opt-in 번들) | 5 Second Previews, Tidy Tab/Downloads, ChatGPT in Command Bar |
| **Dia** | The Browser Company (Arc 후속) | AI-first 브라우저 | 페이지별 Chat (⌘E), 인라인 텍스트 편집, 멀티탭 컨텍스트, Skills 시스템, 메모리 |
| **Perplexity Comet** | Perplexity | AI 검색 + 에이전틱 브라우저 | 우측 AI 사이드바, 페이지 인식 Q&A, 브라우저 제어 (탭 열기/닫기/그룹), Slash 단축키 |
| **Brave Leo** | Brave | 프라이버시 중심 AI | 사이드바 + 전체 페이지 + 주소창, **BYOM**, 임시 채팅, 탭 정리 |
| **Opera Aria** | Opera | 탭 인식 AI + MCP | 사이드바 + 탭 이동, Browser Connector (MCP), 외부 AI 연결 |
| **Opera Neon** | Opera | 에이전틱 브라우저 셸 | **브라우저 = MCP 서버**, 읽기/쓰기 권한 분리, 외부 AI가 브라우저 조작 |
| **Edge Copilot** | Microsoft | 엔터프라이즈 AI | 멀티탭 추론, 브라우저 히스토리 리콜, Copilot Vision, M365 연동 |
| **SigmaOS** | SigmaOS | 태스크 기반 AI | 인터랙티브 요약, 분할 화면 + AI, Ask Anything |
| **Sigma AI Browser** | Sigma | 로컬/오프라인 AI | 오프라인 LLM (Eclipse), 에이전트 모드, 프롬프트 인젝션 방어 |
| **Vivaldi** | Vivaldi | AI 비채택 (파워유저 도구) | AI 없음 — Quick Commands, Command Chains, Tab Stacking 등 |

### 크로스-브라우저 기능 매트릭스

| 기능 | Arc | Dia | Comet | Brave | Opera | Edge | 우선순위 |
|---|---|---|---|---|---|---|---|
| 페이지 요약 | ✅ (hover) | ✅ | ✅ | ✅ | ✅ | ✅ | 높음 |
| 페이지 Q&A | ❌ (제거) | ✅ | ✅ | ✅ | ✅ | ✅ | 높음 |
| 멀티탭 비교 | ❌ | ✅ (@멘션) | ✅ | ❌ | ✅ | ✅ | 중상 |
| AI 사이드바/패널 | ❌ | ✅ (Chat) | ✅ (Sidecar) | ✅ | ✅ | ✅ | 높음 |
| 커맨드바 AI | ✅ | ❌ | ❌ | ✅ (주소창) | ❌ | ❌ | 중 |
| 인라인 텍스트 편집 | ❌ | ✅ | ❌ | ❌ | ❌ | ❌ | 중 |
| 탭 자동 정리 | ✅ | ❌ | ✅ | ✅ | ❌ | ❌ | 중 |
| 탭 제목 정리 | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | 중하 |
| 다운로드 리네임 | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | 중하 |
| Skills/단축키 매크로 | ❌ | ✅ | ✅ (/ 단축키) | ❌ | ❌ | ❌ | 중 |
| 브라우저 에이전트 | ❌ | ❌ | ✅ (클릭/폼) | ❌ | ✅ (MCP) | ✅ (Vision) | 낮음 (V2) |
| BYOM (모델 선택) | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | 높음 |
| 오프라인/로컬 AI | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | 낮음 (V2) |
| 히스토리 리콜 | ❌ | ✅ (메모리) | ❌ | ✅ (메모리) | ❌ | ✅ | 낮음 (V2) |
| MCP 서버 | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | 중 (V2) |
| 음성 입력 | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ | 낮음 |

### 핵심 인사이트 (Maho에 반영할 것)

**1. 컨텍스트가 곧 제품이다**
- 모든 AI 브라우저가 "현재 페이지 + 열린 탭 + 히스토리"를 컨텍스트로 활용
- 단순 챗봇이 아닌, 브라우저 상태를 이해하는 AI가 차별점
- Maho 반영: LLM 호출 시 항상 현재 페이지 title/URL + 활성 탭 목록을 컨텍스트로 포함

**2. AI는 산문이 아니라 액션을 출력해야 한다**
- Comet: 탭 열기/닫기/그룹, 폼 채우기, 네비게이션
- Brave: 탭 정리/그룹화
- Opera Neon: MCP로 외부 AI가 브라우저 직접 조작
- Maho 반영: LLM 응답을 파싱하여 Core API 직접 호출 (이미 Hybrid 아키텍처에서 설계됨)

**3. 복수 진입점 제공**
- 모든 브라우저가 사이드바 + 주소창/커맨드바 + 컨텍스트 메뉴 중 2개 이상 지원
- Maho 반영: ① 커맨드바 AI 모드 (기존 계획) + ② AI 사이드 패널 (신규)

**4. BYOM (Bring Your Own Model)이 파워유저 차별점**
- Brave Leo가 유일하게 커스텀 모델 연결 지원
- Maho 반영: Provider 프로토콜 추상화 (이미 설계됨) + 로컬 Ollama 지원

**5. 권한 모델이 중요**
- Dia: 채팅 시작 시 다른 탭/쓰기 접근 없음, 명시적 승인 필요
- Opera Neon: 읽기/쓰기 도구 권한 분리
- Maho 반영: AI가 상태 변경 시 사용자 확인 UI (특히 Tidy Tabs 폴더 생성)

## 기존 인프라 현황

| 항목 | 상태 | 위치 |
|---|---|---|
| MaxSettings 플래그 | ✅ scaffold 존재 (`enabled`, `pagePreviews`, `tidyTabTitles`, `tidyDownloads`, `tidyTabs`, `aiCommandBar`, `instantLinks`) | `Settings.swift` (macOS/iOS 양쪽) |
| aiModeEnabled | ⚠️ 선언만 있고 미사용 (dead code) | `CommandBarViewController.swift` |
| PageSummarizer | ✅ iOS only, NLTokenizer 기반 추출적 요약 (LLM 아님) | `ios-shell/Support/PageSummarizer.swift` |
| iOS 요약 UI | ✅ SummaryOverlayView, PinchSummaryView, browseForMe | `ios-shell/Views/` |
| Rust HTTP 클라이언트 | ❌ 없음 (reqwest/hyper/ureq 미포함) | — |
| 프롬프트 템플릿 / AI 설정 | ❌ 없음 | — |

## 아키텍처 결정: Hybrid (Option C)

Core가 "무엇을" 결정, Shell이 "어떻게" 실행. 기존 ShellEvent ↔ CoreUpdate 패턴과 동일.

```
┌─────────────────────────────────────────────────────────────┐
│ Core (Rust)                                                 │
│                                                             │
│  비즈니스 로직이 AI 필요 판단                                │
│  → CoreUpdate::RequestLLMCompletion { request_id, prompt }  │
│                                                             │
│  Shell에서 결과 수신                                         │
│  ← ShellEvent::LLMResult { request_id, result }            │
│                                                             │
│  결과 파싱 → 상태 변경 (탭 리네임, 폴더 생성 등)            │
│  → 일반 CoreUpdate 발행 (TabUpdated, FolderCreated 등)      │
└─────────────────────────────────────────────────────────────┘
                          ↕ FFI (JSON)
┌─────────────────────────────────────────────────────────────┐
│ Shell (Swift)                                               │
│                                                             │
│  LLMService: URLSession + AsyncThrowingStream               │
│  APIKeyStore: 로컬 암호화 파일 (SecureLocalStore)              │
│  SSE 파싱, 스트리밍 UI 표시                                  │
│                                                             │
│  CoreUpdate 수신 → LLM 호출 → ShellEvent로 결과 전달       │
│  예외: ChatGPT in Command Bar는 스트리밍 직접 표시          │
│       (최종 결과만 Core에 전달)                              │
└─────────────────────────────────────────────────────────────┘
```

**이유**:
- Rust에 HTTP 클라이언트 추가 불필요 (reqwest 0개 의존성)
- 스트리밍이 Swift AsyncThrowingStream에서 네이티브 (FFI 콜백 지옥 회피)
- 비즈니스 로직(프롬프트 구성, 결과 파싱, 상태 변경)은 Core에서 테스트 가능
- 기존 앱의 모든 것과 동일한 이벤트 패턴

## 공통 인프라 (Phase 0) — 예상 1-2일

모든 AI 기능의 선행 조건. 6개 컴포넌트.

### 0-1. LLMService (Swift Shell)

**파일**: `macos-shell/Sources/AI/LLMService.swift` (신규)

```swift
protocol LLMProvider: Sendable {
    func streamChat(messages: [LLMMessage]) -> AsyncThrowingStream<String, Error>
    func completeChat(messages: [LLMMessage]) async throws -> String
}

final class LLMService {
    static let shared = LLMService()
    private var provider: LLMProvider?

    func configure(provider: LLMProvider) { ... }
    func streamChat(messages: [LLMMessage]) -> AsyncThrowingStream<String, Error> { ... }
    func completeChat(messages: [LLMMessage]) async throws -> String { ... }
}
```

**구현 상세**:
- `OpenAIProvider`: URLSession.shared.bytes(for:) + SSE 파싱
- `AnthropicProvider`: 동일 패턴, x-api-key 헤더 + anthropic-version
- SSE 파서: 바이트 버퍼 → UTF-8 변환 → `\n\n` 분할 → `data:` 라인 파싱
- 취소: `continuation.onTermination`으로 idempotent 취소
- 참고 구현: [Easydict ClaudeService](https://github.com/tisfeng/Easydict) — `URLSession.shared.bytes(for:)` + 바이트 버퍼 SSE 파싱

### 0-2. APIKeyStore (Swift Shell)

**파일**: `macos-shell/Sources/AI/APIKeyStore.swift`

```swift
final class APIKeyStore {
    static let shared = APIKeyStore()
    private let keyPrefix = "apikey."
    private let store = SecureLocalStore.shared

    func save(key: String, for provider: String) -> Bool { ... }
    func get(for provider: String) -> String? { ... }
    func delete(for provider: String) -> Bool { ... }
    func hasKey(for provider: String) -> Bool { ... }
}
```

**구현 상세**:
- `SecureLocalStore`에 위임 — macOS Keychain 미사용
- 키 네이밍: `apikey.{provider}` 형식
- API 키는 Core에 절대 전달하지 않음 — Shell에서만 사용

**SecureLocalStore** (`macos-shell/Sources/AI/SecureLocalStore.swift`):
- 저장 위치: `~/Library/Application Support/Maho/secure_store.enc`
- 암호화: AES-256-GCM (CryptoKit)
- 키 파생: HKDF-SHA256, 하드웨어 UUID 기반 (IOKit `IOPlatformUUID`)
- 전체 스토어를 하나의 암호화 파일로 관리 (JSON → encrypt → write)
- 스레드 안전: DispatchQueue 직렬화

### 0-3. Core LLM 이벤트 타입 (Rust)

**파일**: `crates/maho-types/src/events/shell_event.rs` + `core_update.rs`

```rust
// 새 CoreUpdate 변형
CoreUpdate::RequestLLMCompletion {
    request_id: String,
    request_type: LLMRequestType,  // TidyTabTitle, TidyDownload, TidyTabs, PagePreview
    prompt: String,
    context: serde_json::Value,    // 기능별 메타데이터 (tab_id, download_id 등)
}

// 새 ShellEvent 변형
ShellEvent::LLMResult {
    request_id: String,
    result: String,  // LLM 원시 응답
}

ShellEvent::LLMError {
    request_id: String,
    error: String,
}
```

**Shell 핸들링 플로우**:
1. `CoreUpdate::RequestLLMCompletion` 수신
2. `LLMService.completeChat(prompt)` 호출
3. 결과를 `ShellEvent::LLMResult { request_id, result }` 로 Core에 전달
4. Core가 결과 파싱 → 상태 변경 → 일반 CoreUpdate 발행

### 0-4. Core LLM 요청 매니저 (Rust)

**파일**: `crates/maho-core/src/llm_manager.rs` (신규)

```rust
pub struct LLMManager {
    pending_requests: HashMap<String, LLMRequest>,
}

impl LLMManager {
    pub fn request_tidy_title(&mut self, tab_id: &TabId, title: &str, url: &str) -> CoreUpdate { ... }
    pub fn request_tidy_download(&mut self, download_id: &str, filename: &str, url: &str) -> CoreUpdate { ... }
    pub fn request_tidy_tabs(&mut self, tabs: Vec<TabSummary>) -> CoreUpdate { ... }
    pub fn request_page_preview(&mut self, url: &str) -> CoreUpdate { ... }
    pub fn handle_result(&mut self, request_id: &str, result: &str) -> Vec<CoreUpdate> { ... }
}
```

**역할**:
- 프롬프트 템플릿 관리 (기능별)
- request_id 생성 및 pending 추적
- LLM 응답 파싱 (JSON, plain text)
- 파싱 결과를 실제 상태 변경 CoreUpdate로 변환

### 0-5. Settings UI (Swift Shell)

**파일**: `macos-shell/Sources/Settings/AISettingsView.swift` (신규)

**위치**: `SettingsWindowController.swift`의 tabs 배열에 추가

```swift
(AISettingsViewController(), "Maho AI", "sparkles")
```

**UI 구성**:
- Provider 선택: OpenAI / Anthropic / Custom (드롭다운)
- API Key 입력 (SecureTextField → APIKeyStore → SecureLocalStore에 암호화 저장)
- 모델 선택 (gpt-4o, claude-sonnet 등)
- 기능별 토글: MaxSettings 플래그 연결
  - ☑ Tidy Tab Titles (`tidyTabTitles`)
  - ☑ Tidy Downloads (`tidyDownloads`)
  - ☑ Tidy Tabs (`tidyTabs`)
  - ☑ AI Command Bar (`aiCommandBar`)
  - ☑ Page Previews (`pagePreviews`)
- 기존 `MaxSettingsUpdate` 구조 재사용

### 0-6. 실패 모드 설계 (공통)

**핵심 원칙**: AI 실패 시 graceful no-op. UI를 절대 블로킹하지 않음.

| 기능 | 실패 시 동작 |
|---|---|
| Tidy Tab Titles | 원래 제목 유지 |
| Tidy Downloads | 원래 파일명 유지 |
| Tidy Tabs | 정리 안 됨, 토스트 알림 |
| 5 Second Previews | 프리뷰 표시 안 됨 |
| ChatGPT in Command Bar | 에러 메시지 인라인 표시 |

**Rate Limiting**:
- 5 Second Previews: Shell에서 300ms 디바운스 + URL별 캐시 (Core)
- 동시 요청 제한: 최대 3개

---

## Phase 1: Tidy Tab Titles — 예상 2-4시간

가장 간단한 기능. LLM 인프라 검증용.

### 변경 파일 목록

| 레이어 | 파일 | 변경 내용 |
|---|---|---|
| Core 모델 | `crates/maho-types/src/tab.rs` | `custom_title: Option<String>` 필드 추가 |
| Core 로직 | `crates/maho-core/src/tab_lifecycle.rs` | `pin_tab()` 에서 `settings.max.tidy_tab_titles` 체크 → `LLMManager.request_tidy_title()` 호출 |
| Core 로직 | `crates/maho-core/src/llm_manager.rs` | `handle_result()` 에서 TidyTabTitle 응답 → `tab.custom_title = Some(result)` + `CoreUpdate::TabUpdated` |
| Core VM | `crates/maho-core/src/maho_core.rs` | `TabViewModel` 에 `custom_title` 필드 추가, 표시 제목 = `custom_title.unwrap_or(title)` |
| Shell 모델 | `macos-shell/Sources/Models/ViewModels.swift` | `TabViewModel.customTitle: String?` 추가 |
| Shell UI | `macos-shell/Sources/Sidebar/SidebarTabCellView.swift` | 표시 로직: `tab.customTitle ?? tab.title` |
| Shell UI | `macos-shell/Sources/Sidebar/FavoritesBarView.swift` | 동일 표시 로직 |

### 프롬프트 템플릿

```
You are a tab title shortener. Given a web page title and URL, return ONLY a short 1-3 word title.

Title: {title}
URL: {url}

Rules:
- Remove site names, separators (-, |, ·), breadcrumbs
- Keep the most distinctive part
- Max 3 words
- Return ONLY the shortened title, nothing else

Examples:
- "Settings - Account - GitHub" → "Account"
- "Pull requests · user/repo" → "Pull Requests"
- "Amazon.com: Wireless Mouse - Electronics" → "Wireless Mouse"
```

### 플로우

```
1. 사용자가 탭 우클릭 → "Pin"
2. Shell: ShellEvent::PinTab { tab_id }
3. Core event_dispatcher: tab_lifecycle.pin_tab() 호출
4. Core pin_tab(): settings.max.tidy_tab_titles 체크
   → true: llm_manager.request_tidy_title(tab_id, title, url)
   → CoreUpdate::RequestLLMCompletion 반환
5. Shell: LLMService.completeChat(prompt) 호출 (non-streaming, 간단한 응답)
6. Shell: ShellEvent::LLMResult { request_id, "Account" }
7. Core llm_manager.handle_result(): tab.custom_title = Some("Account")
   → CoreUpdate::TabUpdated { changes: { custom_title: Some("Account") } }
8. Shell: SidebarTabCellView가 customTitle 표시
```

---

## Phase 2: ChatGPT in Command Bar — 예상 4-6시간

사용자 체감 임팩트 최대. **스트리밍은 Shell에서 직접 처리** (예외적으로 Core를 거치지 않음).

### 변경 파일 목록

| 레이어 | 파일 | 변경 내용 |
|---|---|---|
| Shell UI | `CommandBarViewController.swift` | AI 모드 활성화 — `aiModeEnabled` 연결, Tab 키 전환 (Site Search 패턴 재사용), 스트리밍 응답 표시 영역 |
| Shell UI | `CommandBarWindowController.swift` | AI 응답 패널 높이 조절, ⌘⌥G 단축키 → AI 모드 직접 진입 |
| Shell UI | `macos-shell/Sources/AI/AIResponseView.swift` (신규) | 스트리밍 텍스트 표시 NSView (NSTextView 기반, 마크다운 렌더링) |
| Shell 로직 | `macos-shell/Sources/AI/CommandBarAIHandler.swift` (신규) | 질문 → LLMService.streamChat() → AIResponseView 업데이트 |
| Shell 메뉴 | `AppDelegate.swift` | ⌘⌥G 메뉴 아이템 등록 → `askChatGPT` 액션 |

### UI 동작

```
1. ⌘⌥G 또는 커맨드바에서 "AI" 입력 후 Tab
2. 커맨드바 플레이스홀더: "Ask Maho AI..."
3. 질문 입력 → Enter
4. 커맨드바 아래 AIResponseView 확장 (최대 400px 높이)
5. 스트리밍 텍스트 실시간 표시
6. 완료 시:
   - "Copy" 버튼 → 클립보드
   - "Open as Tab" 버튼 → 새 탭에 렌더링
   - Esc → 닫기
```

### Site Search 패턴 재사용

현재 커맨드바에 이미 구현된 Site Search 모드 전환 패턴:
- `siteSearchMode` 변수 + Tab 키 전환
- 플레이스홀더 텍스트 변경
- 결과 영역 재활용

AI 모드도 동일한 패턴:
- `aiModeEnabled` 변수 (이미 선언됨) 활성화
- Tab 키 전환 조건 추가
- 결과 영역을 AIResponseView로 교체

---

## Phase 3: Tidy Downloads — 예상 2-3시간

### 변경 파일 목록

| 레이어 | 파일 | 변경 내용 |
|---|---|---|
| Core 모델 | `crates/maho-types/src/download.rs` (또는 해당 구조체) | `original_filename: Option<String>` 필드 추가 |
| Core 로직 | `crates/maho-core/src/download_manager.rs` (또는 해당) | downloadCompleted 시 `settings.max.tidy_downloads` 체크 → `llm_manager.request_tidy_download()` |
| Core 로직 | `crates/maho-core/src/llm_manager.rs` | TidyDownload 응답 → `download.original_filename = Some(old)`, 새 이름 적용 → `CoreUpdate::DownloadRenamed` |
| Shell VM | `ViewModels.swift` | `DownloadViewModel.originalFilename: String?` 추가 |
| Shell UI | `DownloadPopoverView.swift` | 원본 이름 복원 버튼 (클릭 → `ShellEvent::RestoreDownloadName`) |
| Shell 로직 | `AppDelegate.swift` or `DownloadPopoverView.swift` | 실제 파일 rename: `FileManager.default.moveItem()` |

### 훅 포인트

```
CoreUpdate.downloadCompleted(downloadId:)
→ Core에서 settings.max.tidy_downloads 체크
→ CoreUpdate::RequestLLMCompletion { request_type: TidyDownload, context: { download_id, filename, url } }
→ Shell: LLMService.completeChat()
→ Shell: ShellEvent::LLMResult
→ Core: 새 이름 적용 + CoreUpdate::DownloadRenamed { download_id, old_name, new_name }
→ Shell: FileManager.moveItem(at: oldPath, to: newPath)
→ Shell: DownloadPopoverView.reloadDownloads()
```

### 프롬프트 템플릿

```
Rename this downloaded file to be human-readable. Return ONLY the new filename with extension.

Original: {filename}
Source URL: {url}
Page Title: {page_title}

Rules:
- Keep the file extension
- Use descriptive words, not codes/hashes
- Max 50 characters
- Use spaces, not underscores or dashes
- Return ONLY the filename, nothing else

Examples:
- "IMG_20240315_142356.jpg" from photos page → "Beach Sunset March 2024.jpg"
- "document(3).pdf" from invoice page → "Invoice March 2024.pdf"
```

---

## Phase 4: 5 Second Previews — 예상 4-6시간

### 변경 파일 목록

| 레이어 | 파일 | 변경 내용 |
|---|---|---|
| Shell JS | `macos-shell/Sources/AI/LinkHoverScript.js` (신규) | WKUserScript — 링크 hover 감지 → `window.webkit.messageHandlers.linkHover.postMessage()` |
| Shell WK | `WebViewManager.swift` | `WKUserContentController.addUserScript()` + `addScriptMessageHandler()` |
| Shell UI | `macos-shell/Sources/AI/AIPreviewPopover.swift` (신규) | NSPopover — 요약 카드 표시 (TabPreviewPopover 패턴 재사용) |
| Shell 로직 | `macos-shell/Sources/AI/LinkPreviewHandler.swift` (신규) | hover 메시지 수신 → 디바운스(300ms) → Shift 키 체크 → Core에 요청 |
| Core 로직 | `llm_manager.rs` | `request_page_preview()` + URL별 캐시 (HashMap) |

### JS Injection 스크립트

```javascript
// LinkHoverScript.js — WKUserScript (document end)
(function() {
    let hoverTimeout = null;
    document.addEventListener('mouseover', function(e) {
        const link = e.target.closest('a[href]');
        if (!link) return;
        clearTimeout(hoverTimeout);
        hoverTimeout = setTimeout(function() {
            window.webkit.messageHandlers.linkHover.postMessage({
                url: link.href,
                text: link.textContent.substring(0, 200),
                rect: JSON.stringify(link.getBoundingClientRect())
            });
        }, 300);
    });
    document.addEventListener('mouseout', function(e) {
        if (e.target.closest('a[href]')) {
            clearTimeout(hoverTimeout);
            window.webkit.messageHandlers.linkHover.postMessage({ cancelled: true });
        }
    });
})();
```

### 플로우

```
1. WKUserScript가 페이지 로드 시 inject
2. 사용자가 링크에 hover (300ms 디바운스)
3. JS → messageHandler → LinkPreviewHandler
4. Shell: NSEvent.modifierFlags에서 Shift 키 체크
   (특정 도메인: Google/DuckDuckGo/Bing/X/HN은 Shift 불필요)
5. Shell: ShellEvent → Core llm_manager.request_page_preview(url)
6. Core: 캐시 확인 → 없으면 CoreUpdate::RequestLLMCompletion
7. Shell: URL fetch (URLSession) → 텍스트 추출 → LLMService.completeChat()
8. Shell: ShellEvent::LLMResult → Core: 캐시 저장 + CoreUpdate::PagePreviewReady
9. Shell: AIPreviewPopover 표시 (NSPopover, link 위치 기준)
```

---

## Phase 5: Tidy Tabs — 예상 4-6시간

### 변경 파일 목록

| 레이어 | 파일 | 변경 내용 |
|---|---|---|
| Shell UI | `SidebarController.swift` | 빗자루 버튼 추가 (`setupNavigationRow` 또는 section header) |
| Core 로직 | `llm_manager.rs` | `request_tidy_tabs()` — today tabs 수집 → 그룹핑 프롬프트 |
| Core 로직 | `llm_manager.rs` | `handle_result()` TidyTabs — JSON 파싱 → `create_folder()` + `move_tab_to_folder()` 호출 |
| Core 로직 | `space_manager.rs` | 기존 API 재사용 (`create_folder`, `add_tab_to_folder`) |

### 트리거 조건

```
today_tabs = get_today_tabs(space_id)  // active, unpinned, unfavorited
if today_tabs.len() >= 6 {
    // 빗자루 버튼 활성화 (badge 표시)
}
```

### 프롬프트 템플릿

```json
{
  "instruction": "Group these browser tabs into 2-4 folders. Return JSON only.",
  "tabs": [
    {"id": "tab_1", "title": "React Hooks Guide", "url": "https://react.dev/..."},
    {"id": "tab_2", "title": "GitHub - myrepo", "url": "https://github.com/..."},
    ...
  ],
  "response_format": {
    "folders": [
      {"name": "Development", "tab_ids": ["tab_1", "tab_2"]},
      {"name": "Shopping", "tab_ids": ["tab_5", "tab_6"]}
    ]
  }
}
```

### Core 파싱 + 실행

```rust
// llm_manager.rs — handle_result() for TidyTabs
fn handle_tidy_tabs_result(&self, result: &str, space_id: &SpaceId) -> Vec<CoreUpdate> {
    let response: TidyTabsResponse = serde_json::from_str(result)?;
    let mut updates = vec![];
    for folder in response.folders {
        // 실제 tab_id 존재 검증
        let valid_ids: Vec<_> = folder.tab_ids.iter()
            .filter(|id| self.tab_manager.get_tab(id).is_some())
            .collect();
        if valid_ids.is_empty() { continue; }

        let created = space_manager.create_folder(space_id, &folder.name, false);
        if let Some(folder_vm) = created {
            for tab_id in valid_ids {
                space_manager.add_tab_to_folder(space_id, &folder_vm.id, tab_id);
            }
            updates.push(CoreUpdate::FolderCreated { folder: folder_vm });
        }
    }
    updates
}
```

---

## Phase 6: AI 사이드 패널 (페이지 Q&A + 멀티탭) — 예상 6-8시간

경쟁 분석에서 **모든 AI 브라우저가 제공하는 핵심 기능**. Arc Max에는 없었지만 Dia/Comet/Brave/Opera/Edge 전부 지원.

### 변경 파일 목록

| 레이어 | 파일 | 변경 내용 |
|---|---|---|
| Shell UI | `macos-shell/Sources/AI/AISidePanel.swift` (신규) | 우측 슬라이드 패널 (NSSplitViewItem 또는 NSPanel) |
| Shell UI | `macos-shell/Sources/AI/AIChatView.swift` (신규) | 채팅 UI (NSScrollView + 메시지 셀) |
| Shell UI | `MainWindowController.swift` | 패널 토글 (⌘E), 패널 show/hide 로직 |
| Shell 로직 | `macos-shell/Sources/AI/PageContextExtractor.swift` (신규) | 현재 페이지 DOM 텍스트 추출 (JS injection → document.body.innerText) |
| Shell 로직 | `macos-shell/Sources/AI/AIChatHandler.swift` (신규) | 메시지 관리 + LLMService 호출 + 스트리밍 표시 |
| Core 로직 | `llm_manager.rs` | 페이지 컨텍스트 포함 프롬프트 구성 |

### UX 동작

```
1. ⌘E 또는 툴바 AI 아이콘 클릭
2. 우측에 300px 패널 슬라이드 인
3. 현재 페이지 title + URL 자동 표시
4. 질문 입력 → 현재 페이지 텍스트를 컨텍스트로 포함하여 LLM 호출
5. 스트리밍 응답 표시
6. 다른 탭으로 전환 시 컨텍스트 자동 업데이트 (옵션)
7. "@탭이름" 으로 다른 탭 컨텍스트 추가 (V2)
```

### Dia/Comet 참고 UX

- **Dia**: ⌘E로 Chat 열기, 페이지 + 다른 탭 + 히스토리 컨텍스트, @멘션으로 탭 참조
- **Comet**: 우측 Sidecar 패널, SSE 스트림 + WebSocket (채팅과 브라우저 액션 분리)
- **Brave**: 사이드바 + 전체 페이지 (`brave://leo-ai`) + 주소창

---

## Phase 7 (V2): 추가 기능 로드맵

경쟁 분석에서 도출한 V2 기능. Phase 0-6 완료 후 진행.

### 7-1. BYOM (Bring Your Own Model)
- Brave Leo에서 영감
- Settings UI에서 Custom Provider 추가: Ollama (로컬), LM Studio, 커스텀 OpenAI-compatible endpoint
- `LLMProvider` 프로토콜에 `OllamaProvider` 구현 추가
- 예상: 2-3시간

### 7-2. Skills/매크로 시스템
- Dia Skills + Comet Slash 단축키에서 영감
- 사용자 정의 프롬프트 템플릿 (`/fact-check`, `/translate`, `/summarize` 등)
- 커맨드바에서 `/` 입력 시 스킬 목록 표시
- Core에 `skills_manager.rs` 추가 (JSON 저장)
- 예상: 4-6시간

### 7-3. 인라인 텍스트 편집
- Dia "in-line copy editor"에서 영감
- WKWebView 텍스트 필드 감지 (JS injection)
- 선택 텍스트 → AI 리라이트/개선 제안 → 팝오버 표시
- 예상: 6-8시간

### 7-4. 브라우저 MCP 서버
- Opera Neon에서 영감
- Maho를 MCP 서버로 노출 → 외부 AI (Claude, ChatGPT 등)가 탭/페이지/히스토리 접근
- 읽기/쓰기 권한 분리
- 예상: 1-2주

### 7-5. 멀티탭 비교/추론
- Edge Copilot + Dia @멘션에서 영감
- "이 3개 탭 비교해줘" → 각 탭 텍스트 추출 → LLM에 비교 프롬프트
- 테이블/차트 형태 응답
- 예상: 4-6시간

---

## 구현 순서 요약 (업데이트)

| Phase | 기능 | 예상 시간 | 의존성 | 참고 브라우저 |
|---|---|---|---|---|
| **0** | **공통 LLM 인프라** | 1-2일 | 없음 | 전체 |
| **1** | Tidy Tab Titles | 2-4시간 | Phase 0 | Arc |
| **2** | ChatGPT in Command Bar | 4-6시간 | Phase 0 | Arc, Brave |
| **3** | Tidy Downloads | 2-3시간 | Phase 0 | Arc |
| **4** | 5 Second Previews | 4-6시간 | Phase 0 | Arc |
| **5** | Tidy Tabs | 4-6시간 | Phase 0 | Arc, Brave, Comet |
| **6** | **AI 사이드 패널** | 6-8시간 | Phase 0 | **Dia, Comet, Brave, Opera, Edge** |
| — | **V1 총계** | **~4-6일** | — | — |
| 7-1 | BYOM | 2-3시간 | Phase 0 | Brave Leo |
| 7-2 | Skills/매크로 | 4-6시간 | Phase 2, 6 | Dia, Comet |
| 7-3 | 인라인 텍스트 편집 | 6-8시간 | Phase 0 | Dia |
| 7-4 | 브라우저 MCP 서버 | 1-2주 | Phase 6 | Opera Neon |
| 7-5 | 멀티탭 비교 | 4-6시간 | Phase 6 | Edge, Dia |

## 신규 파일 목록

```
macos-shell/Sources/AI/
├── LLMService.swift           # LLM API 클라이언트 (URLSession + SSE)
├── OpenAIProvider.swift        # OpenAI chat completions
├── AnthropicProvider.swift     # Anthropic messages API
├── OllamaProvider.swift        # Ollama 로컬 모델 (V2 BYOM)
├── SecureLocalStore.swift      # AES-256-GCM 로컬 암호화 저장소
├── APIKeyStore.swift           # SecureLocalStore 기반 API 키 관리
├── CommandBarAIHandler.swift    # 커맨드바 AI 모드 로직
├── AIResponseView.swift        # 스트리밍 응답 표시 NSView
├── AISidePanel.swift           # AI 사이드 패널 컨테이너
├── AIChatView.swift            # 채팅 UI (메시지 셀)
├── AIChatHandler.swift         # 채팅 로직 + 스트리밍
├── PageContextExtractor.swift  # 페이지 DOM 텍스트 추출
├── AIPreviewPopover.swift      # 5 Second Previews 팝오버
├── LinkPreviewHandler.swift    # hover 메시지 핸들러
└── LinkHoverScript.js          # WKUserScript

macos-shell/Sources/Settings/
└── AISettingsView.swift        # Maho AI 설정 패널

crates/maho-core/src/
├── llm_manager.rs              # LLM 요청/응답 관리자
└── skills_manager.rs           # Skills/매크로 시스템 (V2)
```

## 참고 구현 (OSS)

| 패턴 | 참고 레포 | 핵심 파일 |
|---|---|---|
| SSE 스트리밍 | [Easydict](https://github.com/tisfeng/Easydict) | `ClaudeService.swift` — URLSession.bytes + 바이트 버퍼 파싱 |
| Provider 추상화 | [Osaurus](https://github.com/osaurus-ai/osaurus) | `ChatEngineProtocol.swift` — protocol + actor 라우터 |
| 로컬 암호화 저장 | 자체 구현 | `SecureLocalStore.swift` — CryptoKit AES-256-GCM + HKDF, Keychain 미사용 |
| 토큰 트래킹 | [Osaurus](https://github.com/osaurus-ai/osaurus) | `ChatEngine.swift` — wrapStreamWithLogging + estimateInputTokens |

---

# UI 설계

## 디자인 토큰 (기존 앱에서 추출)

### 색상

```
배경:
  windowBackground:       rgb(16, 15, 21)    alpha 1.0
  sidebarBackground:      rgb(27, 24, 40)    alpha 1.0
  contentBackground:       rgb(28, 27, 45)    alpha 1.0
  cardBackground:          rgb(42, 39, 61)    alpha 1.0
  commandBarBackground:    rgb(15, 14, 20)    alpha 0.95

인터랙티브:
  sidebarHover:            white alpha 0.06
  sidebarSelected:         white alpha 0.105
  searchFieldBackground:   rgb(43, 40, 63)    alpha 0.90
  selectionHighlight:      rgb(99, 102, 241)  alpha 1.0   (= accentBlue)

텍스트:
  primaryText:             rgb(243, 243, 247)
  secondaryText:           rgb(188, 188, 202)
  tertiaryText:            rgb(143, 143, 159)
  commandBarSectionHeader: white alpha 0.5  (dark) / black alpha 0.45 (light)

액센트:
  accentBlue:              rgb(99, 102, 241)
  accentBlueBright:        rgb(129, 140, 248)
  folderIconColor:         rgb(96, 165, 250)
  dangerRed:               rgb(239, 68, 68)
  successGreen:            rgb(34, 197, 94)
```

### 타이포그래피

```
커맨드바:
  검색 필드:     SF Pro 17pt regular
  섹션 헤더:     SF Pro 11pt semibold
  제안 제목:     SF Pro 13pt semibold
  제안 부제:     SF Pro 11pt regular
  숏컷 배지:     SF Pro 10pt medium

사이드바:
  탭 제목:       SF Pro 12pt regular
  섹션 제목:     SF Pro 11pt medium
  폴더 제목:     SF Pro 12pt medium
  검색 필:      SF Pro 12pt regular

설정:
  섹션 헤더:     SF Pro 13pt semibold
  본문:          SF Pro 12pt regular
  라벨:          SF Pro 12pt regular

프리뷰:
  제목:          SF Pro 12pt medium
  URL:           SF Pro 10pt regular
```

### 치수

```
메인 윈도우:     1200×800 (min 600×400)
사이드바:        222px (min 180, max 400)
타이틀바:        72px
커맨드바:        600×400, radius 12, border 1
커맨드바 행:     44px (제안), 28px (섹션 헤더)
사이드바 행:     28px (탭), 28px (폴더), 14px (비폴더 섹션), 12px (구분자)
탭 프리뷰:       300×230, radius 4 (이미지)
설정 윈도우:     800×600
설정 카드:       padding 16, radius 10
```

---

## 1. 메인 윈도우 레이아웃 (현재 + AI 추가)

```
┌──────────────────────────────────────────────────────────────────────────┐
│  ● ● ●  (traffic lights)                    타이틀바 72px               │
├────────────┬─────────────────────────────────────────────┬───────────────┤
│            │                                             │              │
│  사이드바   │              웹 콘텐츠                       │  AI 사이드    │
│  222px     │              (WKWebView)                    │  패널        │
│            │                                             │  320px       │
│ ┌────────┐ │                                             │              │
│ │◀ ▶ ↻ 🤖│ │                                             │ ┌──────────┐ │
│ └────────┘ │                                             │ │ 현재 페이지│ │
│ ┌────────┐ │                                             │ │ title.com │ │
│ │🔍 검색  │ │                                             │ ├──────────┤ │
│ └────────┘ │                                             │ │          │ │
│            │                                             │ │  채팅    │ │
│ ★ Favorites│                                             │ │  메시지   │ │
│ ┌──┬──┬──┐ │                                             │ │  영역    │ │
│ │  │  │  │ │                                             │ │          │ │
│ └──┴──┴──┘ │                                             │ │          │ │
│            │                                             │ │          │ │
│ 📌 Pinned  │                                             │ │          │ │
│  ├ Tab 1   │                                             │ │          │ │
│  ├ Tab 2   │                                             │ │          │ │
│            │                                             │ ├──────────┤ │
│ ── 구분선 ──│                                             │ │💬 질문... │ │
│            │                                             │ │  [Send]  │ │
│ □ Normal   │                                             │ └──────────┘ │
│  ├ Tab 3   │                                             │              │
│  ├ Tab 4   │                                             │              │
│            │                                             │              │
│ + New Tab  │                                             │              │
│ 🌐 Space   │                                             │              │
├────────────┴─────────────────────────────────────────────┴───────────────┤
│                           (상태바 / 없음)                                │
└──────────────────────────────────────────────────────────────────────────┘

AI 사이드 패널 토글: ⌘E 또는 네비게이션 바의 🤖 버튼
패널 숨김 시: 웹 콘텐츠가 전체 너비 차지 (현재와 동일)
```

---

## 2. AI 사이드 패널 상세 (Phase 6)

```
┌─────────────────────────── 320px ───────────────────────────┐
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │ 8px inset
│  │  🤖 Maho AI          현재 페이지 │ ✕ │               │   │
│  │  ───────────────────────────────────────              │   │
│  │  📄 React Hooks Guide — react.dev                    │   │ 컨텍스트 표시
│  └──────────────────────────────────────────────────────┘   │ 44px 높이
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                                                      │   │
│  │  🧑 이 페이지의 핵심 내용을 요약해줘                    │   │ 유저 메시지
│  │                                            12:34 PM  │   │ cardBg
│  │                                                      │   │ radius 10
│  ├──────────────────────────────────────────────────────┤   │ padding 12
│  │                                                      │   │
│  │  🤖 이 페이지는 React Hooks에 대해 설명합니다.        │   │ AI 메시지
│  │                                                      │   │ contentBg
│  │  **주요 내용:**                                       │   │ radius 10
│  │  1. useState — 상태 관리                              │   │
│  │  2. useEffect — 사이드 이펙트                         │   │
│  │  3. useContext — 전역 상태                            │   │
│  │                                                      │   │
│  │  이 가이드는 함수형 컴포넌트에서의...                   │   │
│  │                                            12:34 PM  │   │
│  │                                                      │   │
│  │                                                      │   │
│  │  (빈 공간 — 메시지 스크롤 영역)                       │   │
│  │                                                      │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  💬 Ask about this page...                     ▲    │   │ 입력 필드
│  │                                                      │   │ height 40
│  └──────────────────────────────────────────────────────┘   │ radius 8
│                                                              │ 8px inset
└──────────────────────────────────────────────────────────────┘

치수:
  패널 너비: 320px (NSSplitViewItem)
  헤더: 44px (컨텍스트 표시)
  메시지 영역: flexible (NSScrollView)
  입력 영역: 40px + 8px bottom padding
  메시지 버블 radius: 10
  메시지 padding: 12
  메시지 간격: 8
  유저 메시지 배경: cardBackground
  AI 메시지 배경: contentBackground
  폰트: 13pt regular (메시지), 11pt regular (타임스탬프), 12pt medium (헤더)
```

---

## 3. 커맨드바 AI 모드 (Phase 2)

```
┌───────────────────────── 600px ──────────────────────────┐
│                                                           │
│  🤖  AI │ Ask Maho AI...                                 │  모드 표시
│  ─────────────────────────────────────────────────────── │  (Site Search와 동일 패턴)
│                                                           │
│  ┌─────────────────────────────────────────────────────┐ │
│  │                                                     │ │
│  │  🤖 React의 useEffect cleanup 함수에 대해            │ │  스트리밍
│  │  설명해줄게요.                                       │ │  응답 영역
│  │                                                     │ │
│  │  `useEffect`에서 반환하는 함수는 cleanup             │ │  AIResponseView
│  │  함수로, 컴포넌트가 unmount되거나 의존성이            │ │  (NSTextView)
│  │  변경될 때 호출됩니다.                               │ │
│  │                                                     │ │  maxHeight: 368
│  │  ```javascript                                      │ │  (420 - 52)
│  │  useEffect(() => {                                  │ │
│  │    const sub = subscribe();                         │ │
│  │    return () => sub.unsubscribe();                  │ │
│  │  }, []);                                            │ │
│  │  ```                                                │ │
│  │                                                     │ │
│  │  ▌ (커서 깜빡 — 스트리밍 중)                         │ │
│  │                                                     │ │
│  ├─────────────────────────────────────────────────────┤ │
│  │  [📋 Copy]    [🔗 Open as Tab]              [Esc]  │ │  액션 바
│  └─────────────────────────────────────────────────────┘ │  height 36
│                                                           │
└───────────────────────────────────────────────────────────┘

모드 전환 플로우:
┌─────────────────────────────────────────────────────────┐
│  🔍 │ Search tabs and actions...                        │  기본 모드
│  ─────────────────────────────────────────────────────── │
│  ● Recent Searches                                      │
│  ├─ react hooks             ⌘1                         │
│  ├─ github settings         ⌘2                         │
│  ● Open Tabs                                            │
│  ├─ 📄 GitHub Dashboard     ⌘T                         │
│  ├─ 📄 Stack Overflow       ⌘T                         │
│  ● Actions                                              │
│  ├─ 🤖 Ask Maho AI         ⌘⌥G    ← 여기서 Tab       │
│  ├─ 📤 Share Page           ⌘⇧S                        │
└─────────────────────────────────────────────────────────┘
        │
        │ "AI" 입력 후 Tab 또는 ⌘⌥G
        ▼
┌─────────────────────────────────────────────────────────┐
│  🤖  AI │ Ask Maho AI...                                │  AI 모드
│  ─────────────────────────────────────────────────────── │
│                                                         │
│  (AI 모드 - 질문 대기 중)                                │
│                                                         │
│  💡 Try: "이 페이지 요약해줘"                            │  힌트 텍스트
│  💡 Try: "React vs Vue 비교해줘"                         │  secondaryText
│  💡 Try: "이 코드 설명해줘"                              │  13pt regular
│                                                         │
│  Esc to go back                                         │
└─────────────────────────────────────────────────────────┘

치수:
  모드 인디케이터: accentBlue 배경, white 텍스트, radius 4, padding 4/8
  AI 모드 전환: Tab 키 (siteSearchMode와 동일 패턴)
  응답 영역: NSTextView, maxHeight 368px
  액션 바: height 36, 버튼 간격 8
  Copy/Open 버튼: bezelStyle .rounded, controlSize .small, 10pt font
```

---

## 4. 5 Second Previews 팝오버 (Phase 4)

```
                    ┌─────── link text ────────┐
                    │  Understanding React...  │
                    └────────────┬─────────────┘
                                 │ Shift + hover (300ms 디바운스)
                                 ▼
                    ┌───────────────────────────┐
                    │                           │  AIPreviewPopover
                    │  📄 Understanding React   │  NSPopover
                    │     Hooks in 2026         │
                    │                           │  width: 280
                    │  ─────────────────────── │  height: ~140
                    │                           │  behavior: .semitransient
                    │  이 문서는 React 18의      │
                    │  새로운 Hooks 패턴과       │  요약 텍스트
                    │  성능 최적화 전략에 대해   │  13pt regular
                    │  설명합니다. Server        │  primaryText
                    │  Components와의 통합...   │
                    │                           │
                    │  react.dev                │  URL
                    │                           │  10pt regular
                    └───────────────────────────┘  secondaryText 0.7

치수:
  팝오버 크기: 280 × auto (최소 100, 최대 180)
  padding: 12 (all sides)
  제목: 13pt semibold, primaryText, maxLines 2
  구분선: commandBarDivider, height 1, top/bottom margin 8
  요약: 13pt regular, primaryText, maxLines 5
  URL: 10pt regular, secondaryText alpha 0.7
  preferredEdge: .maxY (링크 아래)
  로딩 상태: NSProgressIndicator (spinning, 16x16, 중앙)

패턴 참고: TabPreviewPopover (300×230, semitransient, 동일한 show/cancel 패턴)
```

---

## 5. Tidy Tab Titles 인디케이터 (Phase 1)

```
사이드바 탭 행 (핀 후):

변경 전:
  ├─ 🌐 Settings - Account - GitHub              28px

변경 중 (LLM 호출):
  ├─ 🌐 Settings - Account - GitHub  ⟳           28px
                                     ^스피너 10x10

변경 후:
  ├─ 🌐 Account                      ✨           28px
                                     ^AI 배지 (1초 후 페이드 아웃)

AI 배지:
  "✨" 아이콘 or "AI" 텍스트
  8pt medium, accentBlueBright
  alpha 0 → 1 → 0 (0.3s fadeIn, 2s hold, 0.5s fadeOut)

customTitle 표시 로직:
  displayTitle = tab.customTitle ?? (tab.title.isEmpty ? tab.url : tab.title)
```

---

## 6. Tidy Downloads UI (Phase 3)

```
다운로드 팝오버 행 (리네임 후):

┌──────────────────────────────────── 340px ───────────────────────────┐
│                                                                      │
│  📄 Invoice March 2024.pdf                    [Open] [Remove]       │  64px 행
│  ■■■■■■■■■■■■■■■■■■■■ 100%                                         │
│  Complete · AI renamed from "document(3).pdf"                       │  AI 안내
│             ↑ 클릭 시 원래 이름 복원                                 │  10pt regular
│                                                                      │  accentBlueBright
└──────────────────────────────────────────────────────────────────────┘

원래 이름 복원 플로우:
  1. "AI renamed from ..." 텍스트 클릭
  2. FileManager.moveItem(newPath → oldPath)
  3. DownloadViewModel.filename 복원
  4. 텍스트 사라짐

AI renamed 텍스트:
  font: 10pt regular
  color: accentBlueBright
  underline on hover (NSCursor.pointingHand)
```

---

## 7. Tidy Tabs 확인 UI (Phase 5)

```
사이드바 빗자루 버튼 클릭 시:

┌───────────────────────────── 300px ──────────────────────────────┐
│                                                                   │
│  🧹 Tidy Tabs                                    [Cancel] [Apply]│  NSPopover
│  ─────────────────────────────────────────────────────────────── │  behavior:
│                                                                   │  .semitransient
│  AI가 8개 탭을 3개 폴더로 정리합니다:                              │  width: 300
│                                                                   │  height: auto
│  📁 Development (3 tabs)                                         │
│     ├─ React Hooks Guide                                         │
│     ├─ GitHub - myrepo                                           │
│     ├─ Stack Overflow                                            │
│                                                                   │
│  📁 Shopping (2 tabs)                                            │
│     ├─ Amazon - Wireless Mouse                                   │
│     ├─ Best Buy - Monitors                                       │
│                                                                   │
│  📁 Reading (3 tabs)                                             │
│     ├─ Medium - AI in 2026                                       │
│     ├─ HN - Show HN                                             │
│     ├─ Blog - Design Systems                                    │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘

빗자루 버튼:
  위치: SidebarController.setupNavigationRow (네비게이션 버튼 옆)
  아이콘: "sparkles" (SF Symbol) 또는 커스텀 빗자루
  크기: 20×20 (makeNavigationButton 패턴)
  활성 조건: today_tabs.count >= 6
  비활성 시: alpha 0.3

확인 팝오버:
  폴더 아이콘: folderIconColor (rgb 96, 165, 250)
  폴더 이름: 12pt medium, primaryText
  탭 이름: 11pt regular, secondaryText
  Cancel: bezelStyle .rounded, controlSize .small
  Apply: bezelStyle .rounded, accentBlue 배경, white 텍스트

로딩 상태:
  버튼 클릭 → 팝오버에 스피너 + "AI가 탭을 분석하고 있습니다..."
  13pt regular, secondaryText
```

---

## 8. AI 설정 패널 (Phase 0-5)

```
┌─────────────────────── 800×600 설정 윈도우 ───────────────────────────┐
│  [General] [Appearance] [Privacy] ... [✨ Maho AI] [Shortcuts] ...   │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────────────────── card radius 10 ──────────────────────┐│
│  │                                                                  ││
│  │  AI Provider                                                     ││  padding 16
│  │  ┌──────────────────────────────────────────────────────┐       ││
│  │  │ OpenAI                                           ▼  │       ││  드롭다운
│  │  └──────────────────────────────────────────────────────┘       ││
│  │                                                                  ││
│  │  API Key                                                         ││
│  │  ┌──────────────────────────────────────────────────────┐       ││
│  │  │ ••••••••••••••••••••••sk-proj...                     │       ││  SecureTextField
│  │  └──────────────────────────────────────────────────────┘       ││
│  │                                                                  ││
│  │  Model                                                           ││
│  │  ┌──────────────────────────────────────────────────────┐       ││
│  │  │ gpt-4o                                           ▼  │       ││  드롭다운
│  │  └──────────────────────────────────────────────────────┘       ││
│  │                                                                  ││
│  └──────────────────────────────────────────────────────────────────┘│
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────────┐│
│  │                                                                  ││
│  │  AI Features                                     spacing 12     ││
│  │                                                                  ││
│  │  ☑ Tidy Tab Titles     탭 핀 시 제목 자동 정리                   ││  라벨 width 140
│  │  ☑ Tidy Downloads      다운로드 파일명 자동 정리                  ││
│  │  ☑ Tidy Tabs           탭 자동 폴더 정리                         ││  12pt regular
│  │  ☑ AI Command Bar      커맨드바에서 AI 질문                      ││  secondaryText (설명)
│  │  ☑ Page Previews       링크 AI 프리뷰                            ││
│  │  ☑ AI Side Panel       사이드 패널 AI 채팅                       ││
│  │                                                                  ││
│  └──────────────────────────────────────────────────────────────────┘│
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────────┐│
│  │                                                                  ││
│  │  Custom Endpoint (Advanced)                                      ││
│  │  ┌──────────────────────────────────────────────────────┐       ││
│  │  │ http://localhost:11434/v1   (Ollama)                 │       ││  V2 BYOM
│  │  └──────────────────────────────────────────────────────┘       ││
│  │                                                                  ││
│  └──────────────────────────────────────────────────────────────────┘│
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘

설정 저장 플로우:
  Provider/Key/Model 변경 → Shell-only 저장 (APIKeyStore → SecureLocalStore 암호화 파일 + UserDefaults)
  Feature 토글 변경 → MaxSettingsUpdate → Core 저장 (기존 패턴)
```

---

## 9. 스트리밍 상태 인디케이터 (공통)

```
로딩/스트리밍 중:

  ┌────────────────────────────────────────────┐
  │  ⟳ Thinking...                            │   상태 텍스트
  │                                            │   13pt regular
  │  ▌                                         │   커서 깜빡
  │  (스트리밍 텍스트가 여기에 나타남)           │   0.5s interval blink
  └────────────────────────────────────────────┘

에러 상태:

  ┌────────────────────────────────────────────┐
  │  ⚠️ AI 요청에 실패했습니다.                 │   dangerRed
  │  API 키를 확인하거나 다시 시도해주세요.      │   13pt regular
  │                                [Retry]     │   secondaryText
  └────────────────────────────────────────────┘

API 키 미설정:

  ┌────────────────────────────────────────────┐
  │  🔑 AI 기능을 사용하려면 API 키가            │   secondaryText
  │  필요합니다.                                │   13pt regular
  │                          [Open Settings]   │   accentBlue
  └────────────────────────────────────────────┘
```

## 참고 자료

- Arc Max: https://arc.net/max
- Arc Help Center: https://resources.arc.net/hc/en-us/articles/19335160678679
- Dia Browser: https://www.diabrowser.com/
- Dia Getting Started: https://www.diabrowser.com/getting-started
- Dia Security: https://diabrowser.com/security
- Dia Skills: https://www.diabrowser.com/skills
- Perplexity Comet: https://www.perplexity.ai/comet/
- Comet Help Center: https://comet-help.perplexity.ai/en/
- Comet 리버스 엔지니어링: https://labs.zenity.io/p/perplexity-comet-a-reversing-story
- Brave Leo: https://support.brave.app/hc/en-us/articles/20958609786637
- Opera Aria: https://www.opera.com/features/browser-ai
- Opera Neon MCP: https://blogs.opera.com/news/2026/03/opera-neon-adds-mcp-connector-to-the-browser/
- Opera Browser Connector: https://blogs.opera.com/news/2026/04/opera-new-browser-connector-brings-claude-and-chatgpt-into-the-browser/
- Edge Copilot: https://www.microsoft.com/en-us/edge/features/copilot
- SigmaOS: https://sigmaos.io/
- Sigma AI Browser: https://sigmabrowser.com/
