# Aside Followup Implementation Plan — Maho Browser

> **⚠️ SUPERSEDED (2026-06-27, same day)**: 본 plan은 v2.0인 `aside-followup-bm-redesign-2026-06-27.md`로 대체됨. 사용자가 8개 의사결정 (ACP 백지화, OpenRouter markup 구독 BM, 3-tier 가격 등)을 확정함에 따라 v1의 6-feature 범위는 BM 전환을 포함한 13-workstream 재설계로 확장됨. v1은 의사결정 진행 과정의 역사 보존 목적으로 유지.
>
> **⚠️ HISTORICAL PLANNING ARTIFACT (updated 2026-07)**: 이 문서는 역사 기록용입니다. 편집하지 말고 참조 용도로만 사용하세요. 현재 진실(current truth)은 아래 정본 문서에 있습니다:
> - `docs/competitive-analysis/aside-benchmark-status-2026-07.md`
> - `docs/competitive-analysis/aside-benchmark-evidence-2026-07.md`
> - `docs/decisions/0009-openrouter-subscription-bm.md` (v2)
> - `docs/decisions/0010-payg-credits-model.md`
>
> 본문에 남아 있는 Maho $20 / $200 가격과 과거 token cap 수치는 v1 잔재이며 현재 pricing으로 대체되었습니다: **Free $0 / Pro $5 / Max $20 구독 + PAYG 크레딧 $10 / $50 / $100 + BYOK (무료, 무제한, frontier는 BYOK 전용)**.

**작성일**: 2026-06-27
**버전**: 1.0 (SUPERSEDED)
**기반 문서**: `docs/competitive-analysis/aside-vs-maho.md` (2026-06-27)
**스코프**: §9.1 Immediate + §9.2 Short-term 6개 기능. §9.3 (모바일 셸 출하/Linux/감사)와 §9.4 (마케팅)는 별도 트랙.
**대상**: 향후 4-8주 (2 sprint) 실행 가능한 작업 분해
**언어**: 한국어 본문 + 영어 기술 용어

---

## §0 Meta

### 0.1 Re-baseline — 보고서 작성 후 코드 재조사로 확인된 사실

원본 보고서가 작성한 §9.1 권장 사항을 코드베이스에서 다시 확인한 결과, **세 가지 가정이 부분 또는 전체로 깨졌다**:

| 보고서 가정 | 코드 재조사 결과 | 영향 |
|---|---|---|
| "Tab Tidy 없음, AI 클러스터링 단계 추가 필요" | **`maho-core/src/tab_grouping.rs` (238줄) 완전 구현됨** — BGE embedding + cosine similarity + `cluster_tabs()`. `llm_manager.rs`에 `request_tidy_tabs`, `take_pending_tidy_tabs`, `TidyTabFolder` 타입까지 완성. C++/WebUI 연결만 부재. | Tab Tidy는 **S size** (Small) — UI 연결 작업으로 축소 |
| "BYOK 강제, zero-setup AI 갭 큼" | `llm_provider.rs` + `validate_api_key` (openai/anthropic) + `maho_ai_prefs.h` (`kProvider`/`kBaseUrl`/`kApiKey`) 인프라 완성. **트라이얼 키 또는 호스팅 프록시 옵션만 추가하면 됨** | Zero-setup도 **M size** (Medium) — 백엔드 변경 작음, UX 결정이 메인 |
| "ACP는 있지만 MCP는 별개, 진입 필요" | `maho-acp` crate (client/events/jsonrpc/process/session/transport/turn 7개 모듈) 구조 견고. **MCP 어댑터 추가가 깔끔할 듯**. | MCP는 **M size** — 새 어댑터 모듈, 인프라 재사용 |

### 0.2 결과: 우선순위 + Size

> **2026-06-27 의사결정 반영**:
> - **F1 구현 방식**: 옵션 A (LLM only, `request_tidy_tabs` 단독). `tab_grouping.rs` (embedding 기반)는 미사용 처리.
> - **Maho 타겟**: 혼합 전략 — power user 우선 + 일반 컨슈머 보조. F2는 P1로 강등하되 호스팅은 가볍게 (OpenRouter 무료 모델 한정).

| # | 기능 | Priority | Size | Sprint |
|---|---|---|---|---|
| F1 | Tab Tidy UI 연결 (LLM only) | **P0** | S (3-5d) | 1 |
| F2 | AI Zero-setup 트라이얼 (OpenRouter free models) | **P1** | M (5-7d) | 2 |
| F3 | MCP Host 어댑터 | **P1** | M (8-12d) | 2-3 |
| F4 | Routines (자동화 스케줄러) | **P1** | L (15-25d) | 3-4 |
| F5 | CLI (`maho-cli` bin) | **P2** | M (8-12d) | 4 |
| F6 | Password Manager 외부 연동 | **P2** | S (3-5d) | 4 |

> **총 예상 작업량**: ~6-9주 single engineer / ~3-5주 if parallelizable across 2 engineers
> **Sprint 1 P0 출하**: Tab Tidy → Aside의 가장 가시적 차별 무력화. F2는 P1으로 Sprint 2 진입.

### 0.3 Out-of-scope (별도 트랙)

- 모바일 셸 (`maho/ios-shell/`, `maho/android-shell/`) 출시 가속 → §9.3, 3-6개월 트랙
- Linux 데스크탑 출시 → §9.3
- Verifiable privacy 감사 → §9.3
- 마케팅 / 포지셔닝 → §9.4
- Mail-Routines 통합 (Routines 기본 완성 후 phase 2)

---

## §1 TL;DR

**3문장 요약**:
1. Aside의 핵심 차별 6개 중 **2개(Tab Tidy 백엔드, Multi-provider BYOK 인프라)는 Maho에 이미 거의 구현되어 있음** — UI 연결과 마케팅 마무리만 필요.
2. 새로 만들 핵심은 **MCP Host (F3) + Routines (F4) + CLI (F5)** — agentic 브라우저 표준 UX 3종.
3. 2 sprint (4주) 안에 **Aside의 두 onboarding 차별을 무력화**하고, 4 sprint (8주) 안에 **agentic 기능 3종 추가** 가능.

**즉시 다음 액션**:
- 본 plan 검토 → 우선순위 확정
- Sprint 1 작업 분담 결정 (Tab Tidy + Zero-setup, 둘 다 단일 엔지니어 5-8일)
- F4 Routines 세부 스펙은 Sprint 1 종료 시점에 별도 design doc 작성

---

## §2 보고서 → 실행 매핑 (전체 12개 권장 사항)

| 보고서 §9 항목 | 본 plan 매핑 | 비고 |
|---|---|---|
| 9.1 #1 AI zero-setup | **F2** | 백엔드 인프라 있음, 트라이얼 키 / 프록시 옵션 추가 |
| 9.1 #2 Tab Tidy | **F1** | Backend ✅ 완성, UI 연결만 |
| 9.1 #3 Password Manager 마케팅 | 부분 F6 + 마케팅 트랙 | 외부 매니저 연동만 엔지니어링 |
| 9.2 #4 MCP Host | **F3** | `maho-acp` 위에 어댑터 |
| 9.2 #5 Routines | **F4** | 신규 crate + WebUI |
| 9.2 #6 CLI | **F5** | 신규 bin crate |
| 9.3 #7 iOS/Android 가속 | OOS | 별도 트랙 |
| 9.3 #8 Linux | OOS | Updater 설계상 가능, 별도 plan |
| 9.3 #9 Verifiable privacy | OOS | sync relay 감사 트랙 |
| 9.4 #10-12 마케팅 | OOS | Eng plan에서 제외 |

---

## §3 F1 — Tab Tidy UI 연결 (LLM only)

### 3.1 Goal

`maho-core::llm_manager::request_tidy_tabs` 를 사용자 trigger 가능한 UI 패스로 연결한다. **확정 구현 방식: 옵션 A (LLM only)** — `tab_grouping::cluster_tabs` (embedding 기반)는 미사용 처리.

**Success Criteria**:
- 사이드바 상단 또는 space header에 "Tidy" 버튼 노출
- 클릭 시 LLM 호출 → folder 제안 → 사용자 confirm 모달
- 확인 시 제안된 folder 구조로 자동 그룹화
- Aside의 `aside.verticalTabsTidy`와 기능 동등

### 3.1.1 구현 방식 결정 근거 (2026-06-27)

옵션 A (LLM only) 선택 이유:
- Aside와 동일 방식 → 사용자 경험 parity 명확
- 단일 system만 유지 → 코드 복잡도 최소
- 폴더명 품질이 결정적 가치 → LLM이 우월
- BYOK 사용자가 기본 타겟이라 LLM 호출 비용 사용자 부담 = 자연스러움

옵션 B (hybrid) 미선택 이유:
- 두 시스템 모두 유지 비용 큼
- 단일 사용자 액션이 두 단계로 쪼개지면 디버깅 어려움
- LLM 호출 1회 비용/지연은 수용 가능 (수 초)

옵션 C (둘 다 노출) 미선택 이유:
- UX 의사결정을 사용자에게 전가 → "Quick vs Smart" 선택 피로
- 마케팅 메시지가 분산됨

### 3.1.2 `tab_grouping.rs` 처리 — 삭제

- **현재 상태**: `cluster_tabs` 호출처 0 (dead code)
- **삭제 결정 근거**:
  - 옵션 A 채택으로 embedding clustering은 사용 안 함
  - 옵션 B (하이브리드), 옵션 C (듀얼 노출) 모두 명시적으로 거부
  - 다른 잠재 use case (유사 탭 추천, 검색 ranking, suggestion 통합) 모두 **계획 없음**
  - 그냥 남겨두면 AI slop / 기술 부채 누적
- **액션**:
  - `maho/crates/maho-core/src/tab_grouping.rs` 삭제 (238줄)
  - `maho/crates/maho-core/src/lib.rs` 에서 `pub mod tab_grouping;` 라인 제거
  - `cargo check -p maho-core` 통과 확인
- **보존되는 인프라**: `crate::embedding` 모듈은 `memory_manager.rs`, `pattern_manager.rs`, `maho_core.rs` 3곳에서 활발히 사용 중이므로 그대로 유지

### 3.2 현재 상태

| 레이어 | 상태 | 증거 |
|---|---|---|
| ~~Embedding clustering~~ | ⏸️ 보류 (옵션 A 채택으로 미사용) | `tab_grouping.rs` 238줄, 호출처 0 |
| LLM 기반 tidy 요청 | ✅ 완성 | `llm_manager.rs:request_tidy_tabs`, `take_pending_tidy_tabs`, `TidyTabFolder` |
| FFI 노출 | ⚠️ 확인 필요 | `maho-ffi` 통해 C++에서 호출 가능한지 grep |
| C++ tools registry 연결 | ❌ 부재 | `grep tidy maho-chromium/browser/` 결과 0 |
| WebUI trigger | ❌ 부재 | `grep tidy maho-chromium/browser/resources/` 결과 0 |
| Sidebar 버튼 | ❌ 부재 | sidebar 디렉토리에 tidy 키워드 없음 |

### 3.3 작업 분해

#### F1.1 FFI 확인 + 누락 시 추가 (0.5-1d)

- 파일: `maho/crates/maho-ffi/src/lib.rs`
- 액션: `request_tidy_tabs(space_id, tab_infos)`, `take_pending_tidy_tabs()` FFI 노출 확인
- 누락 시 export 추가, header 재생성
- 검증: `cargo check -p maho-ffi`

#### F1.2 C++ 브리지 (1-2d)

- 신규 파일:
  - `maho-chromium/browser/ai/maho_tab_tidy_service.h/cc` — service 객체
  - service: `RequestTidy(space_id, tabs) -> future<vector<TidyFolder>>`
- 기존 파일 연결:
  - `maho-chromium/browser/ai/maho_browser_tool_registry.cc` — tidy 도구 등록 (agent에서도 호출 가능하도록)
- 검증: GN target 빌드 통과

#### F1.3 Mojo IDL + WebUI 핸들러 (1d)

- 신규 파일:
  - `maho-chromium/browser/ui/webui/maho_sidebar/maho_sidebar.mojom` 에 `RequestTidy()` + `TidySuggestion` 메시지 추가 (또는 신규 mojom)
  - `maho-chromium/browser/ui/webui/maho_sidebar/maho_sidebar_page_handler.cc` 에 핸들러
- 검증: mojom 컴파일 통과

#### F1.4 사이드바 Tidy 버튼 + 모달 (1-2d)

- 기존 파일 수정:
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.h/cc` 또는 space header view 에 "Tidy" 아이콘 버튼 추가
  - 클릭 시 mojom 호출
- 신규 모달:
  - `maho-chromium/browser/resources/sidebar/tidy_suggestion_modal.ts` — 제안된 폴더 리스트, 폴더명 인라인 편집, "Apply" / "Cancel"
- 아이콘: `maho/assets/icons/sf-to-lucide-mapping.json` 에서 Lucide `wand-2` 또는 `sparkles`
- 검증: 수동 QA — 5-20 탭 열고 Tidy 클릭 → 폴더 제안 → 적용

#### F1.5 적용 로직 (folder 자동 생성) (0.5-1d)

- 기존 파일 수정:
  - `maho-core/src/space_manager.rs` 또는 `sidebar_folder_manager` — `apply_tidy_suggestion(space_id, folders)` 추가
  - 기존 `create_folder` API 재사용
- 검증: integration test 작성 (mock LLM 응답 → 폴더 생성 확인)

#### F1.6 마무리

- Telemetry: `Maho.Tidy.RequestCount`, `Maho.Tidy.SuggestionApplied`, `Maho.Tidy.LatencyMs`
- A/B flag: `kTabTidyEnabled` pref (default off → 검증 후 on)
- 사용자 설정: `chrome://maho-settings#ai` 에 "Enable Smart Tab Tidy" 토글

### 3.4 의존성

- 사용자가 AI provider 설정 완료 상태여야 함 (F2 와 자연스럽게 시너지)
- BGE embedding 모델 로드 가능해야 함 → `crate::embedding::embed` 확인

### 3.5 위험

| 위험 | 완화 |
|---|---|
| Embedding 모델이 사용자 디바이스에 없는 경우 | 모델 자동 다운로드 OR LLM-only fallback (embedding 생략, LLM에 raw title/URL 보내 클러스터링 요청) |
| 100+ 탭에서 클러스터링 O(n²) 비용 | 현재 코드 주석에 "<100 tabs" 명시 → UI에 "Best for under 100 tabs" 노출 또는 sampling |
| LLM 응답 시간 길어 사용자 이탈 | 로딩 indicator + cancel 버튼 + 5초 후 progressive update |

### 3.6 출하 후 메트릭

- WAU 중 Tidy 사용자 % → 목표 5% (week 4)
- Tidy 제안 수락률 → 목표 70%+
- 마케팅: 데모 영상 1편 (사이드바 어수선 → Tidy 클릭 → 정리됨, 15초)

---

## §4 F2 — AI Zero-setup 트라이얼 옵션 (Lightweight, P1)

### 4.1 Goal

신규 사용자(특히 컨슈머 페르소나)가 API 키 없이도 AI 사이드패널을 **맛볼 수 있는** lightweight 옵션을 추가한다. **풀 기능 보장 아님** — 어디까지나 "써보기" 경험.

**Success Criteria**:
- 사용자가 Maho 첫 실행 시 AI 패널 활성화 시도 → "Try it / Use my API key" 두 옵션
- 트라이얼 선택 시 OpenRouter **무료 모델만** (gemma-3, llama-3-8b 등) 사용 가능
- 트라이얼 한도 도달 시 BYOK 안내 UI
- **Maho LLM 비용 0** — OpenRouter 무료 tier만 사용

### 4.1.1 우선순위 P1 결정 근거 (2026-06-27)

- Maho 타겟: power user + 일반 컨슈머 혼합 → 트라이얼 nice-to-have지만 필수 아님
- BYOK 자체는 power user에게는 마찰 아님 → P0 가치 부족
- OpenRouter 무료 tier 의존으로 **호스팅 비용 0 + 운영 부담 최소** → P1 가치 충분
- Sprint 1은 F1 (P0)만 집중, F2는 Sprint 2 진입

### 4.2 현재 상태

| 레이어 | 상태 |
|---|---|
| Multi-provider abstraction | ✅ `LLMProvider` trait, openai/anthropic 지원 |
| BYOK key storage | ✅ `maho_ai_prefs.h` (`kApiKey`, `kBaseUrl`, `kProvider`) |
| 트라이얼 provider | ❌ 부재 |
| 트라이얼 한도 추적 | ❌ 부재 |
| Onboarding UI | ⚠️ 일부 — welcome flow 존재하지만 AI 패널 첫-사용 분기 없음 |

### 4.3 확정 사항 (2026-06-27)

1. **호스팅 모델**: **(a) OpenRouter 무료 모델 한정 프록시** — Maho-managed OpenRouter key, 무료 모델만 라우팅 (gemma-3, llama-3-8b 류). Paid 모델은 BYOK 필수.
2. **트라이얼 한도**: **50 메시지 / 30일** (Maho-managed device ID 기반). 사용자가 일찍 가치 체험할 만큼은 충분, abuse 동기는 약함.
3. **Privacy 메시지**: 트라이얼 사용 시 OpenRouter로 데이터 전송됨 명시. BYOK 전환 시 사용자 endpoint로만 통신.

### 4.4 작업 분해

#### F2.1 호스팅 인프라 (3-5d, 결정 (a)일 때)

- 새 서비스: `maho-trial-proxy` (Cloudflare Worker / Fly.io / Vercel Edge)
  - 입력: 사용자 디바이스 ID + 메시지
  - 처리: rate limit check (KV store) → OpenRouter API 호출 → stream back
  - 출력: SSE stream
- 검증: device-level rate limit 동작, abuse detection
- **선결 조건**: OpenRouter 계정 + 예산 결정

#### F2.2 Rust trial provider (1-2d)

- 신규 파일: `maho/crates/maho-core/src/llm_provider_trial.rs`
  - `TrialProvider` implements `LLMProvider`
  - endpoint: Maho hosting URL
  - device ID 자동 생성 + persist
  - 한도 초과 시 명확한 에러
- `llm_manager.rs`에 등록

#### F2.3 Onboarding UI (2d)

- 기존 welcome flow 확장:
  - `maho-chromium/browser/ui/webui/maho_welcome/` (확인 필요)
  - "Set up AI assistant" step 추가: 라디오 — "Try free (30 messages)" / "Use my own API key (BYOK)"
- AI 패널 첫 사용 시:
  - `maho-chromium/browser/ui/webui/maho_ai/app/initial_setup.tsx` (또는 기존 component) 분기 추가

#### F2.4 한도 도달 UX (1d)

- 패널 내 상태 표시: "Trial: 12/30 messages left"
- 0/30 도달 시 modal: "Set up your own key for unlimited" + Anthropic/OpenAI/OpenRouter 링크
- 기존 settings page에서 언제든 BYOK 전환 가능

#### F2.5 검증 + 안전 장치

- Rate limit 우회 시도 차단 (디바이스 ID 변조)
- 비용 monitoring dashboard (호스팅 시)
- Privacy policy 업데이트

### 4.5 의존성

- F2.1이 호스팅 결정에 종속 — 의사결정 회의 1회 필요

### 4.6 위험

| 위험 | 완화 |
|---|---|
| 호스팅 비용 폭발 | Hard rate limit + circuit breaker + 비용 알람 |
| 트라이얼 악용 (multiple device ID) | 30 메시지는 충분히 작아 큰 abuse 동기 부족; 필요시 IP rate limit 추가 |
| BYOK 사용자 이탈 (트라이얼만 쓰고 떠남) | 한도 도달 UX에서 BYOK 가치 명확히 (e.g. 더 좋은 모델, unlimited) |

### 4.7 메트릭

- AI 패널 첫 사용 시 트라이얼 vs BYOK 선택 비율
- 트라이얼 → BYOK 전환율 (목표 30%+)
- AI 패널 first-use → second-use 비율 (활성화 지표)

---

## §5 F3 — MCP Host 어댑터

### 5.1 Goal

Maho가 **Model Context Protocol (MCP) host로 동작** — 외부 MCP 서버(파일시스템, GitHub, Slack 등 도구)가 Maho agent에 연결되어 도구로 사용 가능. Aside의 1.0 핵심 차별 무력화.

**Success Criteria**:
- 사용자가 `chrome://maho-settings#ai/mcp` 에서 MCP 서버 추가/제거
- 추가된 서버의 도구가 `MahoBrowserToolRegistry`에 자동 등록
- Agent turn 실행 시 MCP 도구 호출 가능
- 표준 MCP 서버 (e.g. `@modelcontextprotocol/server-filesystem`) 즉시 동작

### 5.2 현재 상태

| 레이어 | 상태 |
|---|---|
| ACP (Agent Client Protocol) | ✅ `maho-acp` crate 풀 구현 (jsonrpc/transport/session) |
| Browser tool registry | ✅ `maho_browser_tool_registry.cc` |
| MCP 어댑터 | ❌ 부재 |
| MCP 서버 process 관리 | ❌ 부재 (acp `process.rs` 재사용 가능) |
| 설정 UI | ❌ 부재 |

### 5.3 작업 분해

#### F3.1 신규 crate `maho-mcp` (3-5d)

- 위치: `maho/crates/maho-mcp/`
- 의존: `maho-acp` (transport, jsonrpc 재사용)
- 모듈:
  - `client.rs` — MCP client (JSON-RPC over stdio, MCP spec)
  - `server_config.rs` — 서버 설정 직렬화 (name, command, args, env)
  - `tool_bridge.rs` — MCP tool → Maho tool 변환
  - `registry.rs` — 활성 MCP 서버 관리
- 참고 구현: Anthropic의 [TypeScript MCP SDK](https://github.com/modelcontextprotocol/typescript-sdk) 또는 Python SDK 의 와이어 포맷
- 단위 테스트: mock MCP server stub로 tool list + call

#### F3.2 C++ 통합 (3-4d)

- 신규 파일:
  - `maho-chromium/browser/ai/maho_mcp_manager.h/cc` — singleton, 사용자 설정 로드 + 서버 spawn
  - `maho-chromium/browser/ai/maho_mcp_tool_adapter.h/cc` — MCP tool → `MahoBrowserToolRegistry` 등록
- 기존 파일:
  - `maho_browser_tool_registry.cc` — MCP-sourced 도구 등록 진입점 추가
  - `maho_unified_agent_adapter.cc` — agent turn 시 MCP 도구 노출

#### F3.3 설정 WebUI (2-3d)

- 신규 페이지: `chrome://maho-settings#ai/mcp` (또는 별도 `chrome://maho-mcp`)
- UI:
  - 활성 서버 리스트 (name, status, tool count)
  - "Add server" 모달: command + args + env + verify 버튼
  - 미리 정의된 popular MCP servers 추천 (`filesystem`, `github`, `puppeteer`)
- 신규 파일:
  - `maho-chromium/browser/resources/maho_settings/sections/mcp/` (디렉토리)
  - `maho_settings.mojom` 에 `ListMcpServers`, `AddMcpServer`, `RemoveMcpServer` 추가

#### F3.4 보안 / Approval

- 새 도구 등록 시 사용자 명시 승인 (한 번)
- agent가 MCP 도구 호출 시 `MahoUnifiedAgentAdapter::RespondToApproval` 활용 (기존 인프라)
- 위험 도구 (filesystem 쓰기, shell exec) 표시

#### F3.5 검증

- 표준 MCP 서버 3개로 e2e 테스트:
  - `npx -y @modelcontextprotocol/server-filesystem ~/Documents`
  - `npx -y @modelcontextprotocol/server-github` (PAT 필요)
  - `uvx mcp-server-time`
- agent에게 "list files in ~/Documents" 요청 → 도구 호출 + 결과 표시

### 5.4 위험

| 위험 | 완화 |
|---|---|
| MCP spec이 빠르게 변함 | semver pinning + 잘 테스트된 서버 추천 리스트만 |
| 사용자가 악의적 MCP 서버 추가 | 첫 추가 시 명확한 경고 + approval policy 적용 |
| 프로세스 관리 누수 (zombie processes) | `maho-acp/process.rs` 의 reap 로직 재사용 |

### 5.5 메트릭

- MCP 활성화 사용자 %, 평균 등록 서버 수
- agent turn 중 MCP 도구 호출 비율
- 마케팅: "Connect Maho to any MCP server. Aside doesn't have this depth." (verify Aside MCP spec)

---

## §6 F4 — Routines (자동화 스케줄러)

### 6.1 Goal

사용자가 **반복 작업을 cron-style 또는 event-trigger 방식으로 예약**하고 agent에게 자동 실행시킨다. Aside의 1.0 핵심 차별.

**Success Criteria**:
- 사용자가 `chrome://maho-routines` 에서 routine 정의 (자연어 prompt + schedule)
- 백그라운드에서 정시 / 이벤트 트리거 시 agent 자동 실행
- 결과 알림 + 히스토리 보관
- Aside의 free 3 / pro unlimited 와 동등

### 6.2 현재 상태

**완전 부재**. `suggestion_manager.rs`에 "Resume your X routine?" 텍스트만 있음 (실제 routine 시스템 아님).

### 6.3 결정 필요 사항

1. **Trigger 종류 v1**:
   - (a) Cron schedule (e.g. 매일 9시, 매주 월요일)
   - (b) Inbox / 이메일 도착 (Maho Mail 필요 — phase 2)
   - (c) Push notification heartbeat (browser open 시 wake) — Aside 방식
   - **권장 v1**: (a) cron만. (b), (c)는 phase 2.

2. **실행 컨텍스트**:
   - Browser foreground 필요 vs background daemon?
   - **권장**: foreground 우선 (browser running 중). Background daemon은 macOS launchd / Windows scheduled task 별도 구현 trans-OS 복잡도 큼 → phase 2.

3. **저장 위치**: routine 정의 + 실행 히스토리 어디에?
   - 권장: `maho-storage` LMDB에 새 namespace `routines/`

### 6.4 작업 분해

#### F4.1 신규 crate `maho-routines` (5-7d)

- 위치: `maho/crates/maho-routines/`
- 모듈:
  - `types.rs` — `Routine { id, name, prompt, schedule, last_run, last_result_id }`, `Schedule` enum (Cron, Manual)
  - `scheduler.rs` — tokio task가 1분 간격으로 due routine 체크
  - `executor.rs` — agent session start + result capture
  - `history.rs` — 실행 히스토리 LMDB 저장
  - `lib.rs` — public API
- 의존: `maho-storage`, `maho-acp` (executor 가 agent 호출)
- 단위 테스트: mock scheduler, manual trigger

#### F4.2 maho-core 통합 (1-2d)

- `maho_core.rs`에 `RoutinesManager` 인스턴스 추가
- 시작 시 `scheduler::start()` 호출
- pause/resume on app sleep/wake

#### F4.3 FFI + C++ 브리지 (2d)

- `maho-ffi` export: `list_routines`, `create_routine`, `update_routine`, `delete_routine`, `run_routine_now`, `list_routine_history`
- `maho-chromium/browser/routines/maho_routines_service.cc` 서비스 객체
- 결과 알림: 기존 desktop notification 인프라 활용

#### F4.4 WebUI `chrome://maho-routines` (4-6d)

- 디렉토리: `maho-chromium/browser/ui/webui/maho_routines/`
- mojom: `maho_routines.mojom` (`ListRoutines`, `CreateRoutine`, `RunRoutine`, `RoutineUpdated` event)
- React UI:
  - Routine 리스트 (name, last run, status, next run)
  - "New routine" 모달: name + prompt textarea + cron 빌더 (visual)
  - Routine 상세: 히스토리, prompt 편집, 비활성화
- 신규 페이지 등록: `maho-chromium/browser/ui/webui/maho_webui_browser_resource_provider.cc`

#### F4.5 알림 + 히스토리 (1-2d)

- 작업 완료 시 desktop notification (성공/실패 분기)
- 클릭 시 → `chrome://maho-routines` 상세
- 히스토리 보존 정책 (마지막 30개 / 30일)

#### F4.6 한도 정책 (Aside parity, 0.5d)

- Free tier 무관 — Maho는 무제한이지만, 추후 가격 정책 시 적용 가능한 hook 남겨둠

### 6.5 의존성

- F3 (MCP Host) 와 시너지 — MCP 도구가 routine에서도 사용 가능해야 의미 큼
- ACP runtime이 안정적으로 실행 가능해야 함 (`maho-acp-watchdog`)

### 6.6 위험

| 위험 | 완화 |
|---|---|
| Background 실행 중 사용자 PC sleep / lid close | foreground 우선 정책 + 다음 wake 시 catch-up 옵션 |
| 실패 routine 무한 재시도 | exponential backoff + max 3 retry |
| 데이터 변형 작업 (e.g. "delete emails") 실수 | RoutineKind 분류 + 위험 routine 사전 review-before-action 강제 |
| 의도하지 않은 API 비용 | 사용자 LLM provider — Maho 호스팅 안 함 (trial provider는 routines 비허용) |

### 6.7 메트릭

- Routine 생성 사용자 %
- 평균 routine 수 / 사용자
- 실행 성공률
- 마케팅: "Set it and forget it" 데모 (e.g. 매일 아침 GitHub PR 요약)

---

## §7 F5 — CLI (`maho-cli`)

### 7.1 Goal

터미널에서 `maho` 커맨드로 agent session을 시작하고 결과를 받는다. 개발자 페르소나 어필.

**Success Criteria**:
- `maho run "summarize my GitHub issues"` 동작 (Maho running 중)
- `maho repl` 양방향 세션
- macOS Homebrew tap 배포
- Aside CLI와 기능 동등

### 7.2 작업 분해

#### F5.1 신규 bin crate `maho-cli` (5-7d)

- 위치: `maho/crates/maho-cli/`
- 의존: `maho-acp` client, `clap`
- 서브커맨드:
  - `maho run <prompt>` — 단발 실행 (running browser에 IPC)
  - `maho repl` — 양방향
  - `maho routines list/run/create` — Routine 관리
  - `maho mcp list/add/remove` — MCP 서버 관리
- IPC 방식:
  - macOS: unix socket `~/Library/Application Support/Maho/cli.sock`
  - Windows: named pipe
  - browser 시작 시 socket listener spawn (`maho_chromium/browser/cli_bridge/`)

#### F5.2 Browser 측 IPC 서버 (2-3d)

- 신규 파일: `maho-chromium/browser/cli_bridge/maho_cli_bridge.h/cc`
- socket listen + ACP 메시지 brokering
- 권한: 같은 user 만 접근

#### F5.3 패키징

- Homebrew tap: `homebrew-maho` repo
- 또는 `Cargo install` (간단)
- 추후: `npm install -g maho-cli` (Node wrapper)

#### F5.4 문서

- `website/src/content/docs/cli.mdx`
- README 예제 (3-5개 use case)

### 7.3 의존성

- F3 (MCP) 가 있으면 CLI에서 MCP 도구 자동 활용 가능 → 더 강력
- F4 (Routines) 가 CLI에서 관리되면 power user UX 완성

### 7.4 위험

| 위험 | 완화 |
|---|---|
| Browser 안 켜져 있을 때 CLI 동작 안 함 | CLI에서 browser 자동 launch 옵션 (`--auto-launch`) |
| 보안: 다른 user / process가 socket 접근 | macOS는 file perm 0600, Windows는 SECURITY_ATTRIBUTES |

---

## §8 F6 — Password Manager 외부 연동

### 8.1 Goal

`maho-core::password_manager` 가 외부 manager(1Password CLI, Bitwarden CLI)와 연동. Aside의 "external password manager connection" parity.

### 8.2 작업 분해

#### F6.1 1Password CLI adapter (1-2d)

- 신규 모듈: `maho/crates/maho-core/src/password_external/op_cli.rs`
- `op item get <id> --vault X --format json` 파싱
- secret 캐시 (메모리 only, 5분 TTL)

#### F6.2 Bitwarden CLI adapter (1-2d)

- 신규 모듈: `bw_cli.rs`
- `bw get item <id>` 파싱
- 동일 캐시 정책

#### F6.3 설정 UI (1d)

- `chrome://maho-settings#passwords` 에 "External password manager" 섹션
- "1Password" / "Bitwarden" / "None" 라디오
- CLI path / vault 선택

### 8.3 위험

- CLI 미설치 / 인증 안 됨 시 명확한 에러 UX
- 캐시 leak 방지 (메모리 zeroize on drop)

---

## §9 Sequencing / Gantt

### 9.1 Sprint 분할 (2주 sprint 가정) — 2026-06-27 의사결정 반영

#### Sprint 1 (Week 1-2) — Tab Tidy 출하

- F1 Tab Tidy UI 연결 (3-5d) ← **P0, 옵션 A**
- 남는 capacity: `maho-acp-watchdog` 디렉토리 audit + F3/F4 design doc 작성
- **Sprint 1 종료 시점 데모 가능**: "Maho도 Aside처럼 Tab Tidy 됨"

#### Sprint 2 (Week 3-4) — Zero-setup + MCP 진입

- F2 AI Zero-setup (5-7d, OpenRouter 무료 모델 한정) ← P1
- F3 MCP Host 시작 — `maho-mcp` crate skeleton + 기본 client (5d) ← P1
- **Sprint 2 종료 시점 데모 가능**: "API key 없이도 Maho AI 써볼 수 있음" + MCP 첫 서버 연결

#### Sprint 3 (Week 5-6) — MCP 완성 + Routines 시작

- F3 MCP 완성 — C++ 통합 + WebUI 설정 (8d)
- F4 Routines crate skeleton + scheduler 기본 동작 (7d) ← P1

#### Sprint 4 (Week 7-8) — Routines 완성 + CLI/Password

- F4 Routines 완성 — WebUI + 알림 + 히스토리
- F5 CLI 시작 (병렬) ← P2
- F6 Password 외부 연동 (병렬) ← P2
- 통합 테스트 + 마케팅 자산 (데모 영상 3개: Tidy, Routines, MCP)

### 9.2 병렬화 옵션 (2 engineers)

| Engineer A (browser) | Engineer B (Rust core) |
|---|---|
| Sprint 1: F1 UI 연결 | Sprint 1: F2 trial provider Rust 측 |
| Sprint 2: F3 C++ + UI | Sprint 2: F3 `maho-mcp` crate, F4 `maho-routines` crate |
| Sprint 3: F4 WebUI | Sprint 3: F4 scheduler + executor + F6 외부 매니저 |
| Sprint 4: F5 IPC bridge | Sprint 4: F5 CLI bin crate |

**병렬 시 총 기간**: ~4-5 weeks

### 9.3 의존성 그래프

```
F1 (Tab Tidy)     ── 독립
F2 (Zero-setup)   ── 독립 (hosting decision needed)
F3 (MCP)          ── 의존: maho-acp (✅ 있음)
F4 (Routines)     ── 의존: maho-acp, F3 (시너지)
F5 (CLI)          ── 의존: F3 + F4 (시너지)
F6 (External PM)  ── 독립
```

### 9.4 ~~중요 의사결정 포인트 (Plan 시작 전 필수)~~ — 2026-06-27 모두 확정 ✅

1. ~~**F2 호스팅 모델**~~ — **결정**: OpenRouter 무료 모델 한정 프록시 (§4.3)
2. **F4 v1 trigger 범위** — cron만 vs cron+event → §6.3 (Sprint 3 시작 시 확정)
3. **Sprint 분담** — single vs parallel (engineer 수) → 리소스 확정 후
4. **MCP 사용자 경험** — power-user only 설정 vs onboarding featured? → Sprint 2 진입 시 확정

남은 결정 (2-4번)은 해당 sprint 시작 시점에 확정 가능 — plan 차단 요인 아님.

---

## §10 Risk Register

| 위험 | 확률 | 영향 | 완화 | Owner |
|---|---|---|---|---|
| F2 호스팅 비용 통제 실패 | 중 | 고 | hard rate limit + 비용 알람 + circuit breaker | F2 owner |
| MCP 스펙 변경 (breaking) | 중 | 중 | semver pin + 추천 서버 리스트 | F3 owner |
| Routines background 실행 cross-platform 복잡도 | 고 | 중 | foreground-only v1, daemon은 phase 2 | F4 owner |
| Tab Tidy LLM 응답 quality 낮음 | 중 | 저 | prompt iteration + 사용자 폴더명 인라인 편집 | F1 owner |
| CLI socket 권한 / 보안 | 저 | 고 | file perm 0600 + 같은 user 검증 + security review | F5 owner |
| Aside가 동일 기능 빠르게 추가 | 고 | 중 | Maho 차별축 (모바일, ad-block, Spaces) 마케팅 병행 | PM |
| `maho-acp-watchdog` 미완성 (현재 디렉토리 비어있음) | 고 | 고 | F3/F4 시작 전 watchdog 우선 안정화 | Eng Lead |

> **⚠️ Critical**: `maho/crates/maho-acp-watchdog/` 디렉토리가 비어 있음 (확인 필요). F3+F4 모두 watchdog 안정성에 의존하므로 **Sprint 1에 watchdog 상태 audit 필수**.

---

## §11 Success Metrics (Post-Launch)

### 11.1 사용자 활성화

| 지표 | 4주 목표 | 8주 목표 |
|---|---|---|
| WAU 중 AI 패널 사용자 % | 40% | 55% |
| Tab Tidy 사용자 % | 5% | 12% |
| Trial → BYOK 전환율 | 25% | 35% |
| MCP 서버 등록 사용자 % | 3% | 8% |
| Routine 생성 사용자 % | 5% | 10% |
| CLI 설치 (Homebrew) | 100 | 500 |

### 11.2 품질

| 지표 | 목표 |
|---|---|
| Tab Tidy 제안 수락률 | 70%+ |
| Routine 실행 성공률 | 90%+ |
| MCP tool 호출 성공률 | 95%+ |
| Trial API 가용성 | 99.5%+ |

### 11.3 마케팅

- 데모 영상 3편 (Tidy, Routines, MCP)
- 비교 페이지 업데이트: `aside-vs-maho.md` → website `/compare/aside`
- Reddit, HN, Twitter 런칭 1회

---

## §12 Out-of-Scope (별도 트랙)

### 12.1 Mid-term (3-6 months)

- iOS / Android 셸 출하 가속 — Swift 셸 "reference-only" 탈출
- Linux 데스크탑 출시 (Updater 설계상 가능)
- Maho Mail ↔ Routines 통합 (inbox trigger)
- Verifiable privacy: sync relay 외부 감사
- Cloud handoff (Aside Pro feature) — long-running agent task offload

### 12.2 검토 후 결정

- iOS 셸의 ACP runtime — 현재 macOS/Win 위주 watchdog → iOS WKWebView 환경 적합성 검토
- Aside의 "Ultrabrowse" deep research 대응 — multi-source 자동 합성 모드 필요한지

### 12.3 마케팅 트랙 (별도 plan)

- "AI Browser, on every device" 포지셔닝 도큐먼트
- Aside vs Maho 비교 페이지 (`website/`)
- Demo video 시리즈

---

## §13 부록

### 13.1 코드베이스 출발점 (각 feature 시작 시 읽을 파일)

#### F1 Tab Tidy
- `maho/crates/maho-core/src/tab_grouping.rs`
- `maho/crates/maho-core/src/llm_manager.rs` (request_tidy_tabs)
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.h`
- `maho/crates/maho-ffi/src/lib.rs`

#### F2 Zero-setup
- `maho/crates/maho-core/src/llm_provider.rs`
- `maho/crates/maho-core/src/llm_manager.rs`
- `maho-chromium/browser/ui/webui/maho_ai_prefs.h`
- welcome flow 위치 (확인 필요)

#### F3 MCP Host
- `maho/crates/maho-acp/src/lib.rs` + 전체 디렉토리
- `maho-chromium/browser/ai/maho_browser_tool_registry.h`
- `maho-chromium/browser/ai/maho_unified_agent_adapter.h`

#### F4 Routines
- `maho/crates/maho-acp/src/client.rs`
- `maho/crates/maho-storage/` (LMDB 진입점)
- `maho-chromium/browser/ui/webui/` 새 entry 추가 방법: 다른 `maho_*` WebUI 참고

#### F5 CLI
- `maho/crates/maho-acp/src/transport.rs` (IPC 패턴 참고)

#### F6 Password External
- `maho/crates/maho-core/src/password_manager.rs`

### 13.2 사전 확인 작업 (Plan 시작 시 즉시)

1. **`maho-acp-watchdog` 디렉토리 상태 audit** — 비어 있는 이유 확인 (deprecated? 작업 중?)
2. **AGENTS.md (maho/crates/maho-acp/) 읽기** — ACP 설계 의도 파악
3. **`request_tidy_tabs` 호출 그래프 추적** — 어디서 호출되는지, FFI 노출 여부
4. **maho-welcome 현재 흐름 read** — F2 통합 위치 정확히 파악

### 13.3 참고 외부 자료

- MCP spec: [modelcontextprotocol.io](https://modelcontextprotocol.io)
- MCP TS SDK: [github.com/modelcontextprotocol/typescript-sdk](https://github.com/modelcontextprotocol/typescript-sdk)
- OpenRouter docs (F2 hosting option): [openrouter.ai/docs](https://openrouter.ai/docs)
- Aside docs (reference for parity): [docs.aside.com](https://docs.aside.com)
- 기반 분석 보고서: `docs/competitive-analysis/aside-vs-maho.md`

---

**문서 끝.**

**다음 액션**:
1. 본 plan PM/Eng Lead 리뷰
2. §9.4 4개 결정 확정
3. Sprint 1 작업 분담 → 시작
4. (선택) Momus agent로 plan QA — `momus` 에 본 파일 path 전달
