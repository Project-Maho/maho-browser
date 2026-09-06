# 브라우저 내장 메일 클라이언트 종합 코드 리뷰 보고서

- **대상**: `maho-chromium/browser/resources/maho_mail/` (React WebUI), `maho-chromium/browser/mail_helper/` (C++ 브라우저/헬퍼 프로세스), `maho/mail-core/` (Rust 코어)
- **범위**: 보안, 정확성, 메모리 안전성, 아키텍처 (테스트 커버리지는 부록 수준)
- **방식**: 3개 영역 병렬 심층 리뷰 + 주요 발견사항 원본 코드 교차검증 완료

---

## 1. 요약 (Executive Summary)

전체 아키텍처는 견고합니다. IMAP/SMTP·SQLCipher·MIME 파싱을 전용 샌드박스 헬퍼 프로세스로 격리하고, 렌더러에는 파일 경로 대신 일회용 토큰을 교환하며, Rust FFI 경계에 `catch_unwind`·포인터 provenance 관리·cbindgen ABI 동기화를 적용한 설계는 Chromium 멀티프로세스 보안 모델에 부합합니다.

그러나 **렌더러 → 브라우저 → 헬퍼 → Rust 로 이어지는 신뢰 경계마다 "격리 파이프라인을 우회하는 예외 경로"가 하나씩 존재**하며, 이것이 이번 리뷰의 핵심 패턴입니다:

| 계층 | 정상 경로 (안전) | 우회 경로 (취약) |
|---|---|---|
| WebUI | sandboxed iframe + CSP + DOMPurify로 본문 렌더링 | **인쇄 기능이 raw HTML을 특권 윈도우에 `document.write`** |
| C++ | `read_bridge`는 FFI 문자열 null 가드 적용 | **이벤트/신메일 콜백은 null 체크 없이 `std::string` 생성** |
| Rust | 대부분의 FFI 함수는 `catch_unwind` 보호 | **`MahoMailOAuthCancel`은 무보호 직접 실행** |

심각도 분포: **Critical 3, High 8, Medium 8, Low 4**

---

## 2. Critical 발견사항 (교차검증 완료)

### C-1. [WebUI] 인쇄 기능의 `chrome://` 특권 컨텍스트 XSS
**`react/components/layout/EmailContent.tsx:80-137, 1131-1138`**

`generatePrintHtml()`은 `email.body_html`을 sanitize 없이 템플릿 문자열에 삽입하고, `window.open("", "_blank")`으로 연 새 창(호출자의 `chrome://maho-mail` 출처를 상속)에 `document.write()`로 주입합니다. 악성 발신자가 `<script>`를 포함한 메일을 보내고 사용자가 인쇄를 누르면 **chrome:// 특권 컨텍스트에서 임의 스크립트가 실행되어 Mojo IPC를 직접 조작**할 수 있습니다. 본문 보기 경로의 sandbox+CSP 격리를 완전히 우회하는 경로입니다.

```tsx
const bodyContent = bodyHtml ? bodyHtml : `<pre ...>${safeHtml(...)}</pre>`; // bodyHtml 무방어
// ...
const printWindow = window.open("", "_blank");
printWindow.document.write(html); // chrome:// origin 상속
```

**수정**: `bodyHtml`에 `sanitizeEmailHtml`(DOMPurify) 강제 적용 + 인쇄 문서에 `<meta http-equiv="Content-Security-Content" content="script-src 'none'">` 주입. 근본적으로는 새 창 `document.write` 대신 `@media print` CSS 전환이 바람직합니다.

### C-2. [C++] Rust FFI 콜백의 null 포인터 역참조 → SIGSEGV
**`maho_mail_helper_main.cc:299-335`**

`MahoMailRegisterEventCallback`/`MahoMailRegisterNewMailCallback`에 등록된 람다가 `const char*` 인자를 null 체크 없이 `std::string` 생성자에 전달합니다. Rust 측이 `provider`/`reason`/`subject` 없이 이벤트를 발행하면 즉시 UB/크래시입니다. 같은 파일군의 `maho_mail_read_bridge.cc:59`는 `json ? std::string(json) : std::string()`로 방어하고 있어 **방어 패턴이 존재하나 일관되게 적용되지 않은 것**이 확인되었습니다.

**수정**: `auto SafeString = [](const char* s){ return s ? std::string(s) : std::string(); };` 헬퍼를 모든 FFI 콜백에 일괄 적용.

### C-3. [Rust] `MahoMailOAuthCancel`의 `catch_unwind` 누락 → FFI 경계 패닉 누출
**`src/ffi/onboarding_api.rs:308-319`**

다른 FFI 함수들은 `accept_read`/`ffi_bool` 래퍼로 패닉을 격리하지만, 이 함수만 Rust 코드를 직접 실행합니다. 내부 Mutex poisoning 등으로 패닉이 발생하면 `extern "C"` 경계를 넘는 언와인딩으로 프로세스 abort이 발생합니다.

**수정**: `std::panic::catch_unwind(...).unwrap_or(false)` 래핑.

---

## 3. High 발견사항

### WebUI
- **H-1. 초안 저장 시 첨부파일·수신확인 설정 유실** — `react/hooks/useComposeDraft.ts:92-103`. `attachmentsRef`/`requestReadReceiptRef`를 추적하면서 `buildRequest()`가 두 필드를 요청 페이로드에 포함하지 않음. 30초 자동저장마다 첨부파일이 백엔드 초안에서 소실되는 데이터 정합성 버그. (교차검증 완료)
- **H-2. 트래커 차단 우회** — `react/utils/trackerBlocker.ts:241-266`. "이미지 허용 + 트래커만 차단" 모드에서 50개 고정 도메인 목록에 없는 임의 도메인의 `<link>`, `<audio>`, `<embed>`, `<form action>` 외부 리소스가 통과. deprecated 래퍼(`wrapHtmlForDarkMode` 등)는 CSP/차단 로직 자체가 없음.

### C++
- **H-3. macOS 알림 권한 콜백의 시퀀스 위반 크래시** — `maho_mail_notification_permission_mac.mm:88-115`. `UNUserNotificationCenter` completion handler(임의 GCD 큐)에서 `base::BindPostTaskToCurrentDefault` 호출 → `DCHECK(HasCurrentDefault())` 실패.
- **H-4. DB 키 주입 레이스** — `maho_mail_helper_launcher.cc:309-325`. 핸드셰이크(`OnInitialized`) 완료 전 `InjectKeys` 발송 시 조기 `ready_ = true` 전이 + 이후 재주입으로 2중 초기화.
- **H-5. 평문 마스터 키의 메모리 장기 상주** — `maho_mail_helper_launcher.h:124-126`. SQLCipher/자격증명 키를 `std::string`에 영구 캐싱. AGENTS.md의 "zeroed after transmission" 규칙 위반. 코어 덤프/스왑 노출 위험.
- **H-6. Windows 첨부파일 임시 실행파일 삭제 실패** — `maho_mail_attachment_registry.cc:482, 62-67`. `FLAG_WIN_SHARE_DELETE` 누락 + 소멸자 본문에서 핸들 close 전 `DeleteFile` 태스크 포스팅 → 공유 위반으로 악성 실행 가능 임시 파일 잔류.

### Rust
- **H-7. `Drop` 구현체 내 `lock().unwrap()` → 이중 패닉 abort** — `src/ffi/ai_api.rs:870`, `src/ffi/compose_api.rs:326`. 언와인딩 중 `drop()`에서 poisoned mutex `.unwrap()` 시 fatal runtime abort. `state.rs`의 `poisoned.into_inner()` 패턴으로 통일 필요.
- **H-8. UUIDv4 슬라이싱 기반 AES-GCM nonce/PKCE 난수 생성** — `src/credentials.rs:89-94`, `src/oauth/mod.rs:77-83`. UUIDv4는 version/variant 고정 비트로 인해 96비트 nonce의 실질 엔트로피가 90비트로 저하. `rand = "0.8"` 의존성이 이미 있으므로 `rand::thread_rng().fill_bytes()`로 교체.

---

## 4. Medium 발견사항 (요약)

| # | 계층 | 위치 | 내용 |
|---|---|---|---|
| M-1 | WebUI | `bundle_react.mjs:47-49` | esbuild 배너가 Trusted Types pass-through default policy(`createHTML: s=>s`)를 등록해 Chromium WebUI의 DOM XSS 방어 무력화 |
| M-2 | WebUI | `react/utils/attachmentSafety.ts:89-106` | 확장자 추출 시 공백 미처리(`report.exe ` 우회) + 매크로 확장자(`.docm`/`.xlsm`/`.msix` 등) 목록 누락 |
| M-3 | WebUI | `react/store.ts`, `react/tauri_mocks_*.ts` | 어디서도 import되지 않는 레거시 Tauri 잔재 dead code 7개 파일 |
| M-4 | C++ | `maho_mail_arbitration_service.cc:14-61` | OTP 추출이 본문 첫 4~8자리 숫자를 무조건 선택(주문번호 오인) + 도메인 매칭에 PSL(eTLD+1) 미적용으로 `evil.co.uk` 계열 오리진 격리 우회 |
| M-5 | C++ | `maho_mail_helper_sandbox_linux.cc:47-79` | `/lib`, `/usr/lib` 전체 재귀 동기 탐색으로 헬퍼 시작 지연 + `/usr/lib64` 누락 |
| M-6 | C++ | `maho_mail_helper_main.cc:454-457` | `PrepareShutdown`이 Rust 드레인 없이 즉시 콜백 반환 → IMAP IDLE/DB 트랜잭션 미정리 상태로 강제 종료 |
| M-7 | Rust | `src/sync.rs:600-625` | `uid IN (?,...)` 가변 파라미터 무제한 → 대량 동기화 시 `SQLITE_MAX_VARIABLE_NUMBER` 초과. 500개 청킹 필요 |
| M-8 | Rust | `src/ffi/calendar_api.rs:484-496` | `escape_ics_text`가 `\r` 미처리 → iCalendar CRLF 인젝션으로 임의 `ATTENDEE`/`METHOD` 속성 삽입 가능 |
| M-9 | Rust | `src/sync.rs:442` | `Transaction::new_unchecked`로 컴파일 타임 트랜잭션 안전성 우회. `&mut Connection` 시그니처로 변경 권장 |
| M-10 | Rust | `src/oauth/loopback.rs:80-160` | Linux 고정 FD 리스너를 복수 OAuth 스레드가 공유 `accept()` → state 불일치 시 상대방 플로우까지 400으로 파괴 |

## 5. Low 발견사항 (요약)

- **L-1** (WebUI) `react/tsconfig.json` `"strict": false` + Mojo 래퍼의 `Promise<any>`/`as unknown as T` 남용
- **L-2** (C++) `maho_mail_helper_sandbox_linux.cc:193-207` OAuth 비활성 시 FD 정리가 실패로 간주되어 헬퍼 구동 차단 가능
- **L-3** (Rust) `src/oauth/loopback.rs:275-295` HTTP 요청 수신 루프의 O(N²) `windows(4)` 스캔 (64KB 상한으로 영향 제한적)
- **L-4** (Rust) `src/account.rs:320-330` 토큰 갱신 실패 시 제공자 에러 바디 원문 로깅 → 민감정보 포함 가능성

---

## 6. 최우선 조치 Top 5

| 순위 | 심각도 | 대상 | 조치 |
|---|---|---|---|
| 1 | Critical | `EmailContent.tsx` 인쇄 경로 | DOMPurify + `script-src 'none'` CSP 적용, 장기적으로 `@media print` 전환 |
| 2 | Critical | `maho_mail_helper_main.cc` FFI 콜백 | SafeString 널 가드 일괄 적용 (read_bridge 패턴 확산) |
| 3 | Critical | `onboarding_api.rs` | `MahoMailOAuthCancel`에 `catch_unwind` 적용 + FFI 무보호 함수 전수조사 |
| 4 | High | `useComposeDraft.ts` | `buildRequest`에 `attachments`/`read_receipt` 매핑 추가 |
| 5 | High | `credentials.rs` / `oauth/mod.rs` | UUID 난수 → `rand::thread_rng().fill_bytes()` 교체 |

---

## 7. 종합 평가

**강점**: 프로세스 격리 설계, 토큰 기반 첨부파일 레지스트리, 거부 사유 코드(`mail_*`) 계약의 C++/Rust/문서 간 일관성, 크래시 루프 백오프, UIDVALIDITY 에포크 워터마크 등 라이프사이클 복원력이 체계적입니다.

**개선 방향**: 각 계층에 이미 존재하는 방어 패턴(iframe sandbox, SafeString, catch_unwind, poisoned 회수)이 **새로 추가된 경로에 일관 적용되지 않는 것**이 반복 패턴입니다. "새 기능 추가 시 신뢰 경계 체크리스트"(sanitize 여부, null 가드 여부, unwind 보호 여부)를 CI lint 또는 리뷰 체크리스트로 제도화하면 동류 결함의 재발을 막을 수 있습니다.
