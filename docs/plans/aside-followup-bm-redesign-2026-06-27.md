# Aside Followup — BM Redesign Implementation Plan

**작성일**: 2026-06-27 (v2.0, full redesign)
**기반 문서**:
- `docs/competitive-analysis/aside-vs-maho.md` (분석 보고서)
- `docs/decisions/0009-openrouter-subscription-bm.md` (BM ADR, draft)
- (supersedes) `docs/plans/aside-followup-implementation-plan-2026-06-27.md` (v1)

**스코프**: BM 전환을 포함한 Aside 대응 전면 재설계
**전제**: 리소스 무한정 (병렬 fan-out 적극 활용), maho-acp 폐기, OpenRouter markup 월 구독 BM
**언어**: 한국어 본문 + 영어 기술 용어

---

## §0 Meta

### 0.1 이전 plan과의 차이

v1 plan은 "Aside 6개 기능 따라잡기"로 좁게 잡혔으나, 사용자 의사결정 (2026-06-27 8개 결정)에 의해 **BM 전환을 포함한 product 재설계 수준**으로 확장됨. v1 plan은 **superseded**.

### 0.2 락인된 8개 의사결정 (2026-06-27)

| # | 결정 | 값 | 영향 |
|---|---|---|---|
| D1 | Agent backend | **Swiftide 계속** (maho-agent) | ACP cleanup만 하면 됨 — backend 자체는 동작 중 |
| D2 | LLM proxy 위치 | **CF Workers + Rust WASM** (workers-rs) | 신규 인프라, relay와 분리 |
| D3 | 결제 provider | **LemonSqueezy** (MoR) | 세금/VAT 자동 처리, 단순 |
| D4 | OpenRouter 계약 | 개발은 standard, 런칭 전 enterprise | 협상 트랙 별도 |
| B1 | Tier 구조 | **Free / Pro / Max** (Aside parity) | 3-tier, Enterprise 제외 |
| B2 | Privacy 정책 | **Zero-retention proxy** | 가장 엄격, 마케팅 강함 |
| B3 | ADR 0001 supersede | **Sisyphus draft** (=ADR 0009) | 작성 완료, 사용자 검토 대기 |
| B4 | 이메일 인증 | **도입** | 가입 마찰 증가, abuse 차단 |

### 0.3 Workstream 구조 (re-baseline)

v1의 F1-F6 feature-centric 구조를 폐기하고, **workstream-centric** 구조로 재편. 13 workstream을 4 phase로 묶고, 리소스 무한정 가정 하에 최대 병렬화.

| Phase | Workstream | 우선순위 | 의존성 |
|---|---|---|---|
| 0 — Cleanup | W0.1 ACP 코드 제거 | P0 | 없음 (다른 모든 작업 차단 해소) |
| 1 — BM Foundation | W1.1 relay 스키마 확장 | P0 | 없음 |
| | W1.2 LemonSqueezy 통합 | P0 | W1.1 |
| | W1.3 CF Worker scaffold (workers-rs) | P0 | 없음 |
| | W1.4 OpenRouter proxy 구현 | P0 | W1.3 |
| | W1.5 Usage tracking + quota | P0 | W1.1, W1.3 |
| | W1.6 이메일 인증 | P1 | W1.1 |
| 2 — Agent Stack 마감 | W2.1 OpenRouter provider in SwiftideBackend | P0 | W1.4 |
| | W2.2 Browser tool ↔ Rust agent bridge | P1 | W0.1 완료 후 |
| | W2.3 Approval gate UI 왕복 | P1 | W2.2 |
| | W2.4 Session 목록 UI | P2 | W0.1 |
| 3 — Aside Parity 기능 | W3.1 Tab Tidy UI 연결 | P0 | W0.1 |
| | W3.2 Routines (자동화) | P1 | W2.1 |
| | W3.3 MCP host (옵션, 차후) | P2 | W2.1 |
| | W3.4 CLI (`maho-cli`) | P2 | W0.1 |
| 4 — 마케팅/법무 | W4.1 Pricing 페이지 재작성 | P0 | B1 확정 (✅) |
| | W4.2 Privacy / Terms 재작성 | P0 | B2 확정 (✅) |
| | W4.3 ADR 0009 finalize + publish | P0 | B3 (✅, draft 완료) |
| | W4.4 Demo 영상 / 비교 페이지 | P1 | W3.1, W2.3 완료 후 |

---

## §1 TL;DR

**핵심 한 줄**: ACP는 이미 dead path여서 cleanup만 하면 끝, 진짜 새 작업은 **CF Workers + LemonSqueezy + relay 확장 + SwiftideBackend OpenRouter provider** 4축이다.

**6주 출시 가능 (리소스 무한정 가정)**:
- Week 1-2: Phase 0 + Phase 1 (BM 인프라) 병렬 착수
- Week 3-4: Phase 2 (agent stack 마감) + Phase 3 W3.1 (Tab Tidy)
- Week 5-6: Phase 3 잔여 + Phase 4 (마케팅/법무)

**즉시 다음 액션** (사용자 컨펌 시):
1. ADR 0009 finalize → 게시
2. Phase 0 ACP cleanup PR (single-day work)
3. W1.1, W1.3, W2.1, W3.1을 4개 병렬 PR로 fan-out

---

## §2 Phase 0 — ACP Cleanup

### 2.1 W0.1 — ACP 코드 제거

**목표**: ACP 관련 코드를 워크스페이스에서 완전 제거. `MahoUnifiedAgentAdapter`는 ACP를 안 쓰고 있으므로 동작 영향 없음.

**제거 대상**:

| 경로 | 처리 |
|---|---|
| `maho-chromium/browser/ai/maho_unified_agent_adapter.cc` | `#include "maho/browser/ai/maho_acp_event_decoder.h"` 라인 제거 (C-H1(a): 헤더 삭제 전 FIRST) |
| `maho/crates/maho-acp/` (전체) | **삭제** |
| `maho/crates/maho-ffi/src/acp.rs` | **삭제** |
| `maho/crates/maho-ffi/src/lib.rs` | `mod acp;` 라인 + 관련 export 제거 |
| `maho-chromium/browser/ai/maho_acp_event_decoder.h` | **삭제** |
| `maho-chromium/browser/ai/maho_acp_event_decoder.cc` | **삭제** |
| `maho-chromium/browser/ai/maho_acp_event_decoder_unittest.cc` | **삭제** |
| `maho-chromium/browser/ui/webui/maho_ai_prefs.h` | `kOpenCodeRuntimeEnabled`, `kRuntimeCommand` pref 제거 |
| `maho-chromium/browser/ai/BUILD.gn` | acp_event_decoder 빌드 target 제거 |
| `maho/Cargo.toml` workspace members | maho-acp 항목 제거 (C-H1(c,d): maho-acp-watchdog 미존재로 제외) |
| `.omo/plans/maho-ai-acp-migration-plan.md` | archive 디렉토리로 이동 (역사 보존) |
| `.omo/plans/opencode-*.md` (4개) | archive 디렉토리로 이동 |

**검증**:
- `cd maho && bun run typecheck` 통과 (cargo check workspace)
- `python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target chrome` 통과
- `cargo test -p maho-core -p maho-agent -p maho-ffi` 통과

**예상 작업량**: **0.5-1일** (single PR)

**위험**: 거의 없음. dead code 제거이므로 동작 변화 없음.

---

## §3 Phase 1 — BM Foundation

### 3.1 W1.1 — relay 스키마 + auth 확장

**파일**:
- `maho/relay/migration/m20260628_tier_fields.rs` — 신규 migration
- `maho/relay/entity/src/users.rs` — User 엔티티 확장
- `maho/relay/server/src/auth/routes.rs` — `/auth/introspect` 추가
- `maho/relay/server/src/lemonsqueezy/` — 신규 모듈

**스키마 변경** (migration):
```sql
ALTER TABLE users ADD COLUMN tier TEXT NOT NULL DEFAULT 'free';
ALTER TABLE users ADD COLUMN subscription_status TEXT;
ALTER TABLE users ADD COLUMN ls_customer_id TEXT UNIQUE;
ALTER TABLE users ADD COLUMN ls_subscription_id TEXT UNIQUE;
ALTER TABLE users ADD COLUMN email_verified_at TIMESTAMPTZ;
ALTER TABLE users ADD COLUMN email_verification_token_hash TEXT;
ALTER TABLE users ADD COLUMN tier_expires_at TIMESTAMPTZ;
CREATE INDEX idx_users_ls_customer ON users(ls_customer_id);

-- C-H5: webhook_events/processed_event_ids 테이블 추가 (중복 방지)
CREATE TABLE processed_event_ids (
    event_id TEXT PRIMARY KEY,
    processed_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```

**`/auth/introspect` 엔드포인트**:
- 입력: `Authorization: Bearer <opaque_token>`
- 인증: 내부 서비스만 호출 가능 (별도 service token 또는 IP allowlist)
- 출력: `{ user_id, tier, subscription_status, email_verified, expires_at }`
- 캐시: 5s (또는 캐시 제거, R-1 / R-11 완화)

**예상 작업량**: **2-3일**

---

### 3.2 W1.2 — LemonSqueezy 통합

**파일**:
- `maho/relay/server/src/lemonsqueezy/webhook.rs` — webhook handler
- `maho/relay/server/src/lemonsqueezy/client.rs` — LS API client (subscription 조회/취소)
- `maho/relay/server/src/lemonsqueezy/signature.rs` — HMAC SHA256 webhook 검증

**핸들링할 webhook 이벤트** (C-H5):
- `order_created` — 첫 구독 → users.tier 업데이트, ls_customer_id/ls_subscription_id 기록
- `subscription_created` — tier 활성화
- `subscription_updated` — tier 변경 (Pro ↔ Max)
- `subscription_cancelled` — tier_expires_at = current_period_end
- `subscription_resumed` — tier_expires_at NULL
- `subscription_expired` — tier = 'free'
- `subscription_payment_failed` — subscription_status = 'past_due'
- `subscription_payment_recovered` — subscription_status 복구 (C-H5)

**Checkout 흐름**:
- 클라이언트 → 웹사이트 `account/upgrade` → LemonSqueezy hosted checkout URL 리다이렉트
- 결제 완료 후 webhook으로 tier 업데이트
- 사용자에게 이메일 confirmation (LS 자체 발송)

**보안 및 아키텍처**:
- Webhook signature 검증 (X-Signature 헤더, HMAC-SHA256, signing_secret 환경변수)
- Webhook idempotency (event_id 중복 시 `processed_event_ids` 확인 후 skip)
- **NO abstraction layer**: 복잡한 추상화 레이어를 두지 않고 직관적으로 구현하되(R-3 scrapped per insight bundle), 향후 Paddle/Stripe 전환을 고려한 직접 웹훅 핸들러 로직으로 간결화.

**예상 작업량**: **3-4일**

---

### 3.3 W1.3 — CF Worker scaffold (workers-rs)

**디렉토리**: `maho/billing-proxy/` — **별도 Cargo workspace** (C-H6: root/relay workspace 와 분리하여 `libsqlite3-sys` 충돌 방지)

**Cargo.toml**:
```toml
[workspace] # C-H6: 별도 workspace 선언 필수
resolver = "2"
members = ["."]

[package]
name = "maho-billing-proxy"
edition = "2021"

[lib]
crate-type = ["cdylib"]

[dependencies]
worker = "0.6" # D-6: current stable
worker-macros = "0.6"
serde = { version = "1", features = ["derive"] }
serde_json = "1"
# reqwest 비활성화, worker::Fetch 사용 (D-6)
```

**최소 라우터**:
```rust
#[event(fetch)]
async fn fetch(req: Request, env: Env, _ctx: Context) -> Result<Response> {
    Router::new()
        .get("/health", |_, _| Response::ok("ok"))
        .post_async("/v1/chat/completions", chat_completions)
        // post_async("/v1/messages") 엔드포인트 삭제 (hard constraint)
        .run(req, env).await
}
```

**Cloudflare 리소스**:
- KV namespace `MAHO_AUTH_CACHE` — auth introspect 결과 캐시 (5s 또는 제거, R-1)
- Durable Object `MAHO_USAGE_DO` — 사용량 집계용 (D1 SQLite/KV 대비 강한 일관성)
- Secret `OPENROUTER_API_KEY` — master key
- Secret `RELAY_INTROSPECT_TOKEN` — relay 호출용 service token

**Wrangler 설정**:
- `wrangler.toml` — production/staging 환경 분리
- CI: GitHub Actions에서 `wrangler deploy --env staging` → `--env production`

**예상 작업량**: **2-3일**

---

### 3.4 W1.4 — OpenRouter proxy 구현

**파일**: `maho/billing-proxy/src/proxy.rs`

**플로우**:
1. 요청 수신: `POST /v1/chat/completions` with `Authorization: Bearer <user_token>`
2. Auth check: KV 캐시 hit → 즉시 사용. miss → relay `/auth/introspect` 호출 → KV 캐시 (5s TTL 또는 제거 - R-1 완화)
3. Quota check: Durable Object `UserUsage::<user_id>` → 이번 달 누적 USD 비용 조회 → tier limit (Pro $15.40 / Max $153.80) + top-up 한도 비교
4. 모델 access check: 요청 모델이 tier 허용 모델 목록에 있는지 확인
5. OpenRouter 호출: master key로 stream 요청 (Pre-flight로 tier별 max_tokens cap 주입: Free ~$0.05, Pro ~$0.50, Max ~$5 - R-1 세부완화)
6. SSE 스트리밍 forwarding: backpressure 유지
7. 응답 완료 후: OpenRouter post-completion cost tally 계산 → Durable Object 업데이트 (비차단)
8. **Zero-retention**: prompt / response 본문 절대 저장 안 함

**모델 접근 매트릭스**:
```rust
fn allowed_models(tier: &str) -> &[&str] {
    match tier {
        "free" => &["google/gemma-3-27b-it:free", "meta-llama/llama-3.1-8b-instruct:free", ...],
        "pro" => /* free + */ &["openai/gpt-4o", "anthropic/claude-3.5-haiku", ...],
        "max" => /* pro + */ &["openai/gpt-4-turbo", "anthropic/claude-3-opus", ...],
        _ => &[],
    }
}
```

**오류 처리**:
- 429 (quota 초과): `{ error: "quota_exceeded", upgrade_url: "..." }`
- 401 (auth 실패): `{ error: "invalid_token" }`
- 403 (모델 미허용 또는 단일 요청 ceiling 초과): `{ error: "model_not_in_tier_or_ceiling_exceeded", upgrade_url: "..." }`
- 502 (OpenRouter 실패): exponential backoff retry (max 2회) → 그 후 사용자에게 전달
- 503 (circuit breaker): 분당 OpenRouter 실패율 >20% 시 임시 차단

**예상 작업량**: **4-5일**

---

### 3.5 W1.5 — Usage tracking + quota

**아키텍처 선택**: Durable Object per-user

**이유**:
- KV는 eventually consistent (60s) — quota 결정에 부적합 (Max 사용자 초과 사용 유발)
- D1은 SQLite at edge, write throughput 제약
- Durable Object는 강한 일관성 + 자동 분산 + write-throughput 최적

**`UserUsage` Durable Object**:
```rust
struct UserUsage {
    user_id: String,
    current_period_start: DateTime<Utc>,
    cost_usd_accumulated: f64, // C-H3: USD quota 집계
    base_ceiling_usd: f64,     // Pro $15.40, Max $153.80
    topup_ceiling_usd: f64,    // Q-B: 추가 구매 top-up 한도
    next_reset_at: DateTime<Utc>,
}

impl DurableObject for UserUsage {
    async fn fetch(&mut self, req: Request) -> Result<Response> {
        // POST /increment → USD 비용 누적 + 한도 체크
        // POST /topup → topup_ceiling_usd 추가 적립 (Q-B)
        // GET /summary → 현재 사용량 + base/topup 한도 반환
        // alarm callback → 월 갱신 (C-H4: Alarms API 자체 스케줄링)
    }
}
```

**월 갱신** (C-H4): CF Cron Trigger 대신 **Cloudflare Alarms API** (`setAlarm()`)를 사용해 유저별 subscription anniversary 시점에 개별 reset 수행 (R-2 완화). 만약 알람 누락 시 읽기 시점에 lazy reset 수행 (R-7 완화).

**클라이언트 표시**: AI 패널에 "Pro: $12.45 / $15.40 USD" 형식 inline 표시 (`/v1/usage/me` 엔드포인트로 조회)

**예상 작업량**: **3-4일**

---

### 3.6 W1.6 — 이메일 인증

**파일**:
- `maho/relay/server/src/auth/email_verify.rs` — verification flow
- `maho/relay/server/src/email/postmark.rs` — Postmark client (또는 Resend)

**플로우**:
1. signup 시 verification token 생성 → users.email_verification_token_hash 저장
2. Postmark API로 verification email 발송 (template: "Welcome to Maho — confirm your email")
3. 사용자가 이메일의 링크 클릭 → `GET /auth/verify-email?token=...` → users.email_verified_at = now()
4. 미인증 계정: LLM proxy 접근 시 403 (`/auth/introspect`에서 email_verified=false 반환 시 Worker가 거절)

**Provider 선택**: **Postmark** 권장 (transactional 이메일 deliverability 최고, 월 100개 무료)

**예상 작업량**: **2일**

---

## §4 Phase 2 — Agent Stack 마감

### 4.1 W2.1 — OpenRouter provider in SwiftideBackend

**파일**: `maho/crates/maho-agent/src/swiftide_backend.rs`

**현재**: OpenAI / Anthropic 직접 지원
**추가**: OpenRouter provider — 단순히 새 LLM provider 추가가 아니라 **Maho 호스팅 모드**를 추가하는 것이 핵심:
- BYOK 모드: 사용자 OpenAI/Anthropic 키로 직접
- **Maho 모드 (신규)**: endpoint = `https://billing-proxy.maho.app/v1`, api_key = 사용자 Bearer token (relay에서 발급)

**구현 세부사항** (C-H2):
- `OpenAICompatibleProvider` 대신 `swiftide_integrations::openai::GenericOpenAIBuilder::<MahoOpenAIConfig>`를 사용하도록 변경 (위치: `maho/crates/maho-agent/src/swiftide_backend.rs:623`).
- **NO AuthSource enum**: 복잡한 `AuthSource` 열거형을 도입하지 않고, 단순하게 `base_url`과 `api_key` 필드로만 구성정보를 로드 및 설정하도록 설계 간소화.

**예상 작업량**: **2일**

---

### 4.2 W2.2 — Browser tool ↔ Rust agent bridge [Post-Launch로 이동 - D-3 적용]

**상태**: MVP launch 스레드에서 제외하여 post-launch로 연기.

**문제**: C++의 `MahoBrowserToolExecutor`와 Rust의 `SwiftideBackend` tool loop 간 FFI 브릿지 단절 해결 필요.

**예상 작업량**: **5-6일**

---

### 4.3 W2.3 — Approval gate UI 왕복 [Post-Launch로 이동 - D-2 적용]

**상태**: MVP launch 스레드에서 제외하여 post-launch로 연기.

**문제**: C++ → WebUI → 사용자 → C++ 간 Mojo event 및 permission decision 구현 완료 필요.

**예상 작업량**: **3-4일**
4. WebUI → C++ adapter: Mojo call `RespondToApproval(decision)`
5. C++ adapter → Rust: `PermissionDecision` (Allow / Deny / AllowAlways) 반환

**예상 작업량**: **3-4일**

---

### 4.4 W2.4 — Session 목록 UI

**파일**: `maho-chromium/browser/ui/webui/maho_ai/sessions/`

**구현**:
- AI 패널 상단에 "Sessions" dropdown
- 과거 세션 목록 (제목, 시작 시각, 메시지 수, 모델)
- 클릭 → 해당 세션 복원
- 검색 / 삭제 / pin

**현재 상태**: `kSessionListJson`, `kSessionPersistenceEnabled` pref 있음. `AgentStorage` (maho-agent crate)에 SQLite 저장 동작 중. UI만 부재.

**예상 작업량**: **3일**

---

## §5 Phase 3 — Aside Parity 기능

### 5.1 W3.1 — Tab Tidy UI 연결 (옵션 A, LLM only)

(v1 plan §3 그대로 — 락인 결정 변경 없음)

핵심:
- `request_tidy_tabs` (LLM)만 사용. `tab_grouping.rs` (embedding) **삭제** (v1에서 결정).
- 사이드바 상단에 "Tidy" 버튼 (Lucide `wand-2`)
- Mojo `RequestTidy()` → `TidySuggestion` 모달 → folder 적용

**예상 작업량**: **3-5일**

---

### 5.2 W3.2 — Routines (자동화 스케줄러)

**디렉토리**: `maho/crates/maho-routines/`

**구현 범위 (v1)**: cron trigger only. inbox event는 phase 2 (Maho Mail 통합 시).

**핵심 모듈**:
- `types.rs` — `Routine { id, name, prompt, schedule, last_run, ... }`
- `scheduler.rs` — tokio task, 1분 간격 due check
- `executor.rs` — agent session start → result capture
- `history.rs` — LMDB 저장

**WebUI**: `chrome://maho-routines`

**예상 작업량**: **15-20일** (큰 작업)

---

### 5.3 W3.3 — MCP host (옵션, 차후)

D1에서 옵션 A (Swiftide only)를 선택했으므로 MCP는 **별도 phase**. `rmcp` crate가 maho-agent Cargo.toml에 이미 있으므로 가벼운 통합은 가능하나, Aside 출시 대응에는 W3.1/W3.2 우선.

**예상 작업량**: **8-12일** (차후)

---

### 5.4 W3.4 — CLI (`maho-cli`)

(v1 plan §7 그대로)

**예상 작업량**: **8-12일**

---

## §6 Phase 4 — 마케팅 / 법무

### 6.1 W4.1 — Pricing 페이지 재작성

**파일**: `website/src/pages/pricing.astro`

**변경**:
- 현재 "Free forever, BYOK only" 메시지 → Free / Pro / Max 3-tier table
- 각 tier의 quota, model 접근, feature 차이
- BYOK는 "Free + Use your own key" 옵션으로 명시 (계속 무료)
- LemonSqueezy checkout 링크 (CTA 버튼)
- Trust 메시지: "Sync is E2EE. AI is zero-retention. Neither is logged or sold."

**예상 작업량**: **2일**

---

### 6.2 W4.2 — Privacy / Terms 재작성

**파일**:
- `website/src/pages/privacy.astro`
- `website/src/pages/terms.astro`

**Privacy 변경**:
- "Sync is E2EE — we cannot decrypt your data" (기존)
- **신규**: "LLM traffic goes through Maho's zero-retention proxy. We do not store prompts or responses. We log only: model used, token count, timestamp, for billing purposes."
- OpenRouter sub-processor 공개 (OpenRouter DPA 검토 결과 반영)
- GDPR right-to-erasure: 사용자 요청 시 users + usage rows 모두 삭제

**Terms 변경**:
- Subscription billing terms (자동 갱신, 환불 정책)
- Acceptable use policy (abuse / prohibited content)
- Tier 변경 / 취소 권리

**예상 작업량**: **3-4일** (법무 검토 포함 시 추가)

---

### 6.3 W4.3 — ADR 0009 finalize + publish

**현재 상태**: Draft 작성 완료 (`docs/decisions/0009-openrouter-subscription-bm.md`)

**남은 작업**:
- 사용자 검토 + 수정 요청 반영
- "Draft" → "Accepted" 상태 변경
- ADR 0001 헤더에 "Partially superseded by ADR 0009" 메모 추가
- 팀 합의 표명 (있다면 commit message 또는 별도 PR 설명에)

**예상 작업량**: **0.5일**

---

### 6.4 W4.4 — Demo 영상 / 비교 페이지

**산출물**:
- Demo 영상 3편 (각 30-60초):
  1. **Zero-setup AI**: 다운로드 → 가입 → AI 패널 즉시 동작 (vs Aside parity)
  2. **Tab Tidy**: 어수선한 사이드바 → Tidy 클릭 → 정리됨
  3. **Cross-platform**: macOS + iOS + Android에서 같은 sync (Aside 못함)
- 비교 페이지: `website/src/pages/compare/aside.astro` (보고서 §6 기반)

**예상 작업량**: **5-7일**

---

## §7 Sequencing — 6주 일정 (리소스 무한정)

### Week 1
- W0.1 ACP cleanup ← 1일 완료
- W1.1 relay 스키마 ← 시작 (~3일)
- W1.3 CF Worker scaffold ← 시작 (~3일)
- W2.1 OpenRouter provider ← 시작 (~2일)
- W3.1 Tab Tidy UI ← 시작 (~5일)
- W4.3 ADR 0009 finalize ← 시작 (사용자 검토 의존)

### Week 2
- W1.2 LemonSqueezy 통합 ← 시작 (W1.1 완료 후, ~4일)
- W1.4 OpenRouter proxy ← 시작 (W1.3 완료 후, ~5일)
- W1.5 Usage tracking ← 시작 (W1.1+W1.3 완료 후, ~4일)
- W1.6 이메일 인증 ← 시작 (W1.1 완료 후, ~2일)
- W2.2 Browser tool bridge ← 시작 (W0.1 완료 후, ~6일)
- W2.4 Session UI ← 시작 (~3일)
- W3.1 Tab Tidy ← 완료

### Week 3
- W1 전체 (BM Foundation) 완료 목표
- W2.3 Approval gate UI ← 시작 (W2.2 완료 후, ~4일)
- W3.2 Routines ← 시작 (~15-20일)
- W4.1 Pricing 페이지 ← 시작 (~2일)
- W4.2 Privacy/Terms ← 시작 (~4일)

### Week 4
- W2 (Agent Stack) 완료 목표
- W3.2 Routines 계속
- W4 (마케팅/법무) 완료 목표
- **이 시점에서 Closed Beta 가능**

### Week 5
- W3.2 Routines 완료
- W3.4 CLI ← 시작 (~10일)
- W4.4 Demo 영상 / 비교 페이지 ← 시작
- 통합 QA pass 1

### Week 6
- W3.4 CLI 완료
- W4.4 완료
- 통합 QA pass 2
- **Public launch 준비 완료**

W3.3 MCP host는 launch 이후 phase 2.

---

## §8 Risk Register

| 위험 | 확률 | 영향 | 완화 | Owner |
|---|---|---|---|---|
| Cloudflare Worker WASM bundle 1MB 초과 | 중 | 중 | 무료 plan 1MB / 유료 10MB. 처음부터 유료 plan 가입. tree-shaking, optional features 정리 | W1.3 owner |
| LemonSqueezy → Stripe 인수 후 변경 | 저 | 고 | [scrapped] 추상화 레이어를 배제하고 직관적 결합으로 MVP 속도 우선. 전환 필요 시 수동 포팅. | W1.2 owner |
| OpenRouter 무료 모델 정책 변경 (가용성 / 한도) | 중 | 고 | Free tier 모델 목록을 config-driven으로 (worker secret 또는 KV). 정책 변경 시 신속 대응 | W1.4 owner |
| Durable Object cost 폭주 (per-user object) | 저 | 중 | usage 패턴 모니터링. 필요 시 D1 + sticky session 모델로 전환 | W1.5 owner |
| Email deliverability (Postmark 신뢰성) | 저 | 중 | Postmark의 transactional template + DKIM/SPF 설정. fallback으로 Resend 사용 | W1.6 owner |
| Tier quota 보정 (50/1500/15000) — 실제 사용 패턴과 미스매치 | 고 | 중 | 첫 30일 close monitoring → 60일 시점에 quota 재조정 ADR amendment | PM |
| Zero-retention 약속 위반 (실수로 prompt 저장) | 저 | 극대 | Worker 코드 PR 리뷰에 zero-retention checklist 추가. 외부 보안 감사 (런칭 후 60일) | Security |
| ADR 0001 supersede 공개 backlash | 중 | 중 | ADR 0009의 "decision drivers" + "considered options" 섹션을 공개 blog 포스트로 발행 | PM / Founder |
| Aside가 cross-platform 따라잡기 | 중 | 중 | Maho iOS/Android 셸 출시 가속이 별도 트랙 (§9 OOS). 마케팅 메시지로 우선권 유지 | Mobile team |
| LemonSqueezy의 EU VAT 처리 누락 | 저 | 고 | LS는 MoR로 자동 처리 — 약관 명시 확인 | Legal |
| `MahoBrowserToolExecutor` FFI 노출이 sequence checker / threading 위반 | 중 | 고 | 기존 `MahoAiPageContextExtractor` 패턴 참고. C++ 측 PostTask로 thread safety 보장 | W2.2 owner |

---

## §9 Out-of-Scope (별도 트랙)

### 9.1 Mid-term (3-6 months)
- iOS / Android 셸 출하 가속 (별도 plan)
- Linux 데스크탑 출시
- Maho Mail ↔ Routines 통합 (inbox trigger)
- MCP host (W3.3)
- Verifiable privacy: sync relay 외부 감사
- Enterprise tier (현재 ADR 0008로 차단됨, 별도 ADR로 재오픈 가능)

### 9.2 마케팅 (별도 트랙)
- "AI Browser, on every device" 메시지 캠페인
- Aside 비교 페이지 / SEO
- Press 아웃리치 (TechCrunch, MacStories, Verge)
- Reddit / HN / Twitter 런칭

---

## §10 Success Metrics

### 10.1 Launch 6주 후

| 지표 | 목표 |
|---|---|
| Maho-managed AI 사용자 % (vs BYOK) | 60% |
| Free → Pro 전환율 | 5% |
| Pro → Max 전환율 | 2% |
| MRR (Monthly Recurring Revenue) | $5K |
| 이메일 verification 완료율 | 80%+ |
| Tab Tidy 사용자 % | 12% |
| AI 패널 daily active user / DAU | 50%+ |

### 10.2 Launch 6개월 후

| 지표 | 목표 |
|---|---|
| MRR | $50K |
| Pro 사용자 수 | 1,500 |
| Max 사용자 수 | 100 |
| OpenRouter 비용 / 매출 비율 | <40% (gross margin 60%+) |
| 월간 churn | <8% |
| Free tier abuse 비율 | <2% |
| NPS | >40 |

### 10.3 품질

| 지표 | 목표 |
|---|---|
| Worker p99 latency (auth check + proxy 시작) | <200ms |
| LLM 응답 시간 (LLM 시간 제외, Maho 오버헤드) | <50ms |
| Webhook 처리 성공률 | 99.5%+ |
| Privacy 약속 위반 인시던트 | 0 |

---

## §11 ADR / 문서 변경 목록

- `docs/decisions/0009-openrouter-subscription-bm.md` — **신규** (draft 완료, 사용자 검토 대기)
- `docs/decisions/0001-pricing-model.md` — 헤더에 "Partially superseded by ADR 0009 (BM portion)" 메모 추가
- `docs/competitive-analysis/aside-vs-maho.md` — 변경 없음 (분석 보고서)
- `docs/plans/aside-followup-implementation-plan-2026-06-27.md` — **superseded** 마크 추가 (v1)
- `docs/plans/aside-followup-bm-redesign-2026-06-27.md` — **본 문서** (v2.0)
- `maho/relay/AGENTS.md` — anti-pattern 문구 ("No decrypting or inspecting payload contents") 명확화: relay는 변경 없음, LLM proxy는 별개 인프라
- `website/src/pages/privacy.astro` — zero-retention proxy 정책 추가
- `website/src/pages/terms.astro` — subscription terms 추가
- `website/src/pages/pricing.astro` — 3-tier table

---

## §12 즉시 다음 액션

1. **ADR 0009 사용자 검토** — `docs/decisions/0009-openrouter-subscription-bm.md` draft 확인
2. **W0.1 ACP cleanup PR 시작** — 차단 요인 없음
3. **W1.1, W1.3, W2.1, W3.1 fan-out** — 4개 병렬 PR
4. **W4.3 finalize** — ADR 0009 수정 반영 후 Accepted 상태로 변경

질문 / 결정 필요 시점:
- Week 2 종료 시점: tier quota 수치 최종 확정 (현재 잠정 500/1500/15000)
- Week 3 종료 시점: 이메일 provider 최종 확정 (Postmark vs Resend)
- Week 4 종료 시점: Routines v1 trigger 범위 (cron only vs cron+event)
- Week 5 종료 시점: MCP host 일정 결정 (post-launch phase 2 vs 동시 출시)

## §13 Architecture Supplementary Review (T-ARCH)

본 리뷰는 Q-4 의사결정에 따라 Separation of Concerns, Leaky Abstraction, Coupling, Tech Debt 관점에서 작성된 아키텍처 검토 내용입니다.

1. **Separation of Concerns (관심사 분리)**:
   - LLM Proxy (`billing-proxy`)를 기존 E2EE sync relay와 물리적으로 완전히 분리한 설계는 동기화 서버의 E2EE 비밀 보장 규칙을 수호하고 보안 및 컴플라이언스 범위를 단순화하는 데 크게 기여합니다.
   
2. **Leaky Abstraction (추상화 누수)**:
   - LemonSqueezy 결제 게이트웨이의 전용 추상화 레이어를 제거하여 단기적 MVP 개발 속도를 극대화했습니다. 다만, 이는 LemonSqueezy 플랫폼으로의 강한 결합을 발생시키며, 향후 Paddle 또는 Stripe Direct로의 전환 시 웹훅 시그니처 및 이벤트 매핑 로직의 수동 수정 부채가 됩니다.
   
3. **Coupling (결합도)**:
   - `AuthSource` enum의 확장을 우회하고 `base_url`과 `api_key`를 직접 주입받는 방식으로 `SwiftideBackend` 설계를 간소화함으로써 컴파일 복잡성과 런타임 매개변수 간의 강한 결합을 효과적으로 완화했습니다.
   
4. **Tech Debt Accumulation (기술 부채 누적)**:
   - Browser tool bridge(W2.2) 및 Approval UI(W2.3)와 같은 UX/비동기 오버헤드가 높은 연동 구조를 launch 이후(post-launch)로 연기함에 따라 MVP 단계의 배포 안정성을 확보하고 릴리즈 시점의 기술 부채 집중 현상을 적절히 분산했습니다.

---

**문서 끝.**
