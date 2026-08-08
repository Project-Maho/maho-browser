# 데스크톱 Maho AI 에이전트 테스트 커버리지 및 우선순위 백로그

## 범위

이 문서는 데스크톱 `chrome://maho-ai` 패널과 이를 뒷받침하는 C++, Rust, WebUI 경로의 테스트 커버리지를 다룹니다. 모바일 WebView, iOS, Android, Maho Mail의 AI 테스트는 범위에 포함하지 않습니다. Maho는 비공개 독점 소프트웨어이며, 아래 테스트와 항목은 내부 빌드 및 배포 환경을 전제로 합니다.

## 현재 검증 계층

| 계층 | 현재 검증 위치 | 검증 범위 |
|---|---|---|
| WebUI 상태 및 부팅 | `browser/resources/maho_ai/react/__tests__/store.test.ts`, `quick-submit-crash.test.ts`, `ingest-ask-maho.test.ts` | 스토어 동작, 초기 부팅, 첨부, 빠른 제출 안정성 |
| 네이티브 패널 수명주기 | `browser/ui/views/side_panel/maho_ai_side_panel_interactive_uitest.cc` | 열기, 닫기, 재열기, OTR 거부, 인그레스 큐, 소비자 재바인딩, 팝업 및 사이드바 대체 경로 |
| C++ 에이전트 수명주기 | `browser/ai/maho_unified_agent_adapter_lifecycle_unittest.cc` | 스트림 수명주기, 오래된 콜백 세대, 취소, 승인, 임대 콜백, OSCrypt 준비 상태 |
| 페이지 핸들러 및 컨텍스트 | `browser/ui/webui/maho_ai/maho_ai_page_handler_unittest.cc` 및 출력 비식별화 테스트 | 프롬프트와 컨텍스트 전달, 민감한 출력 보호 |
| 도구 및 보안 경계 | 브라우저 도구, MCP 임대, 방화벽, OTR 브라우저 테스트, Rust 형식 승인 테스트 | 정책, 개인정보, 도구 접근 제어 |
| 공급자 라우팅 | `maho/crates/maho-agent/tests/provider_routing_test.rs` | 보안 콜백의 공급자 계열 해석 |

## 우선순위 백로그

### P0. 결정론적 mock LLM 데스크톱 E2E

현재 `maho-chromium/build/scripts/e2e_chromium_ai.js`는 WebUI 마운트와 응답 여부를 확인하지만, 실제 호스팅된 패널에서 제공자 없는 스트리밍 대화 경로를 검증하지 않습니다. 하위 LLM 클라이언트 테스트의 HTTP 모킹도 사용자 경로 전체를 대체하지 못합니다. **mock LLM E2E는 P0입니다.**

**영향 위치**

* 테스트 진입점: `maho-chromium/build/scripts/e2e_chromium_ai.js` 또는 이를 호출하는 데스크톱 AI E2E 대상
* WebUI 경로: `browser/resources/maho_ai/react/`
* 패널 경로: `browser/ui/views/side_panel/maho_ai_side_panel_interactive_uitest.cc`
* 자격 증명 및 어댑터 경로: `browser/ai/maho_unified_agent_adapter_lifecycle_unittest.cc`

**구현 항목**

* 테스트 프로필에만 연결되는 로컬 SSE 호환 OpenAI 및 Anthropic mock 엔드포인트를 추가합니다.
* 테스트 전용 암호화 자격 증명을 준비하고, 실제 `chrome://maho-ai` 패널이 이 엔드포인트로 요청하도록 구성합니다.
* 일반 메시지와 Analyze, Explain, Summarize 빠른 작업을 모두 실행합니다.

**수용 시나리오**

1. 패널에서 일반 메시지를 제출하면 mock 서버가 기대한 제공자, 모델, 현재 컨텍스트를 포함한 요청을 받습니다.
2. 스트리밍 청크가 실제 DOM 응답으로 표시됩니다.
3. Analyze, Explain, Summarize 각각이 동일한 스트리밍 경로에서 올바른 의도와 컨텍스트로 완료됩니다.
4. 자격 증명 준비 전 생성된 세션도 준비 완료 후 일반 메시지와 빠른 작업을 정상 전송합니다.
5. 서버 오류는 패널 오류 UI로 표시됩니다.
6. 패널 닫기와 재열기, 브라우저 재시작 뒤에도 정의된 자격 증명 상태가 유지됩니다.
7. 키를 지운 뒤에는 어떤 LLM 요청도 전송되지 않습니다.

### P0. 실제 C++ 대 Rust ABI 통합 스모크 테스트

`MahoUnifiedAgentAdapterLifecycleTest`는 가짜 FFI vtable을 사용합니다. 따라서 실제 `maho_agent_create_session_leased`와 `maho_agent_send_message_leased`의 ABI 레이아웃, 소유권 콜백, 보안 키 할당 및 해제, Rust 취소 동작은 검증되지 않습니다. **실제 C++ 대 Rust ABI 커버리지는 P0입니다.**

**영향 위치**

* 기존 C++ 수명주기 테스트: `browser/ai/maho_unified_agent_adapter_lifecycle_unittest.cc`
* 실제 FFI 진입점: `maho_agent_create_session_leased`, `maho_agent_send_message_leased`
* Rust 공급자 라우팅 테스트: `maho/crates/maho-agent/tests/provider_routing_test.rs`
* 연결 대상: Chromium이 사용하는 실제 `maho-core` 정적 라이브러리

**구현 항목**

* 실제 정적 라이브러리를 링크하는 좁은 C++ 통합 테스트 대상을 만듭니다.
* 네트워크 없는 결정론적 mock 제공자를 사용해 FFI 경계를 넘는 한 개의 임대 세션을 실행합니다.

**수용 시나리오**

1. C++가 실제 ABI로 임대 세션을 만들고 메시지를 전송하면 Rust가 스트리밍 콜백을 반환합니다.
2. 임대 콜백 해제가 정확히 한 번 수행되며, 해제 뒤 콜백이 재사용되지 않습니다.
3. C++ 취소 요청이 Rust 작업을 중단하고 이후 스트리밍을 막습니다.
4. 보안 키 할당과 free 콜백이 짝을 이루며, 누수와 이중 해제가 없습니다.

### P1. 현재 페이지 빠른 작업의 의미론적 종단 간 검증

빠른 작업 칩은 일반 프롬프트 제출 경로를 통해 현재 페이지 컨텍스트를 붙입니다. 현재 테스트는 DOM 클릭에서 의도와 컨텍스트가 어댑터 응답까지 이어지는 흐름, 비동기 추출 중 탐색 또는 재로드를 검증하지 않습니다.

**영향 위치**

* WebUI 빠른 작업: `browser/resources/maho_ai/react/`
* 페이지 핸들러: `browser/ui/webui/maho_ai/maho_ai_page_handler_unittest.cc`
* 패널 인그레스 재바인딩: `browser/ui/views/side_panel/maho_ai_side_panel_interactive_uitest.cc`

**수용 시나리오**

1. 지연된 렌더러 컨텍스트 추출 중 빠른 작업을 제출한 뒤 탐색하면, 요청은 캡처된 문서만 사용하거나 결정론적으로 거부합니다.
2. 새 페이지가 이전 요청의 컨텍스트로 잘못 귀속되지 않습니다.
3. 보류 중인 인그레스 요청이 있는 상태에서 패널을 재로드하거나 재바인딩해도 결과가 유실되거나 중복 전송되지 않습니다.

### P1. 브라우저 수준 자격 증명 수명주기

기존 어댑터 회귀 테스트는 이미 존재하는 세션에서 OSCrypt가 준비되는 경우를 다룹니다. 재시작, 키 교체, 관리형 인증 만료, 설정 변경 뒤 OTR 전환과 같은 브라우저 수준 상태는 검증되지 않습니다.

**영향 위치**

* 어댑터 준비 상태: `browser/ai/maho_unified_agent_adapter_lifecycle_unittest.cc`
* 패널과 프로필 경로: `browser/ui/views/side_panel/maho_ai_side_panel_interactive_uitest.cc`
* 일반 및 빠른 작업 인그레스: `browser/resources/maho_ai/react/`

**수용 시나리오**

1. 암호화된 BYOK 자격 증명이 재시작 뒤 복원되고 일반 프로필 경계 밖으로 노출되지 않습니다.
2. 활성 세션 중 키를 교체하면 다음 일반 메시지와 빠른 작업이 모두 새 자격 증명 상태를 사용합니다.
3. 세션 생성 뒤 설정을 저장해도 상태가 일관되게 갱신됩니다.
4. 관리형 인증이 만료되면 비밀값 없이 오류 상태가 표시됩니다.
5. 설정 변경 뒤 OTR로 전환하면 일반 프로필 자격 증명이 OTR 요청에 사용되지 않습니다.

### P1. 네이티브 패널 제스처, 준비 상태, 포커스, 마이크 라우팅

현재 인터랙티브 UI 테스트는 인그레스 API를 직접 호출합니다. 사이드바 버튼 또는 명령 팔레트 제스처가 올바른 창의 패널을 열고, WebUI 로드를 기다리며, 작성자에 포커스하는지 검증하지 않습니다. `MahoAiSidePanelWebView::RequestMediaAccessPermission`에도 전용 테스트가 없습니다.

**영향 위치**

* 인터랙티브 UI 테스트: `browser/ui/views/side_panel/maho_ai_side_panel_interactive_uitest.cc`
* 미디어 권한 구현: `MahoAiSidePanelWebView::RequestMediaAccessPermission`

**수용 시나리오**

1. 사이드바 버튼과 명령 팔레트 모두 패널을 열고 작성자에 포커스를 둡니다.
2. Escape 동작은 정의된 패널 상태 전환을 수행합니다.
3. 두 브라우저 창에서 실행해도 요청과 포커스가 호출한 창에만 연결됩니다.
4. WebUI 로드 실패 시 요청이 잘못된 소비자에게 전달되지 않으며, 재시도 가능한 상태가 됩니다.
5. 마이크 권한 허용과 거부가 WebView에서 플랫폼 권한 경로로 정확히 전달됩니다.

### P2. 렌더링 기반 WebUI 상호작용 및 접근성

`compact-conversation-layout.test.ts`와 `composer-trigger.test.ts` 일부는 TSX 소스 문자열과 데이터 투영을 검사합니다. 승인 흐름은 Allow와 Deny 클릭을 검증하지 않으며, AI 전용 접근성 테스트도 없습니다.

**영향 위치**

* `browser/resources/maho_ai/react/__tests__/compact-conversation-layout.test.ts`
* `browser/resources/maho_ai/react/__tests__/composer-trigger.test.ts`
* `browser/resources/maho_ai/react/`의 승인, 작성자, 컨텍스트 선택기, 오류 UI

**수용 시나리오**

1. 소스 문자열 계약을 렌더링된 DOM 계약으로 교체합니다.
2. Allow와 Deny 클릭이 올바른 승인 결과를 전송합니다.
3. 오류의 재시도와 닫기가 사용자 상호작용으로 동작합니다.
4. 임대 해제와 강제 탈취 상태가 UI에 표시됩니다.
5. Shift+Enter, 붙여넣기, 파일 드롭, 컨텍스트 선택기 키보드 탐색이 동작합니다.
6. 스트리밍 업데이트의 `aria-live`, 모달 포커스 트랩, 키보드 제어, 런타임 오류 복구가 접근성 검증을 통과합니다.

### P2. 스트레스 및 시간 제한 경계

어댑터의 2 MB 스트림 누적 제한, 30초 권한 및 브라우저 도구 대기 제한, 보안 콜백 활동 중 키 삭제 경쟁 상태는 다뤄지지 않습니다. MCP 백엔드 임대는 검증되지만, 강제 탈취 뒤 UI 가시성은 확인되지 않습니다.

**영향 위치**

* C++ 어댑터 테스트: `browser/ai/maho_unified_agent_adapter_lifecycle_unittest.cc`
* MCP 임대 및 UI 상태: `browser/resources/maho_ai/react/`

**수용 시나리오**

1. 2 MB를 넘는 스트림 누적은 결정론적 오류로 종료되고 UI에 표시됩니다.
2. 가짜 시계로 30초 권한 및 브라우저 도구 대기 시간 초과를 재현하고, 후속 콜백이 결과를 변경하지 못하게 합니다.
3. 키 삭제와 보안 콜백이 동시에 발생해도 비밀값 접근, 누수, 충돌, 이중 해제가 없습니다.
4. MCP 임대가 강제 탈취되면 UI가 즉시 사용 불가 또는 오류 상태를 표시합니다.

## 권장 도입 순서

1. 재사용 가능한 로컬 mock LLM SSE fixture와 데스크톱 AI E2E 대상을 구축합니다.
2. 같은 fixture를 사용해 실제 C++ 대 Rust ABI 스모크 통합 대상을 추가합니다.
3. 현재 페이지 빠른 작업의 추출 및 탐색 경계, 자격 증명 재시작 및 키 교체 브라우저 테스트를 추가합니다.
4. 네이티브 패널의 제스처, 포커스, 다중 창, 준비 상태, 마이크 권한 인터랙티브 테스트를 확장합니다.
5. WebUI 소스 문자열 계약을 렌더링 DOM 및 접근성 상호작용 테스트로 교체합니다.
6. 스트리밍, 승인 및 도구 대기, 보안 키 무효화의 시간 제한과 스트레스 경계를 추가합니다.
