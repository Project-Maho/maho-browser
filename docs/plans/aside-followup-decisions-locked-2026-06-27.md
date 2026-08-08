# Aside Followup — Decisions Locked-In (2026-06-27)

> **⚠️ HISTORICAL PLANNING ARTIFACT (updated 2026-07)**: 이 문서는 역사 기록용 의사결정 산출물입니다. 편집하지 말고 참조 용도로만 사용하세요. 현재 진실(current truth)은 아래 정본 문서에 있습니다:
> - `docs/competitive-analysis/aside-benchmark-status-2026-07.md`
> - `docs/competitive-analysis/aside-benchmark-evidence-2026-07.md`
> - `docs/decisions/0009-openrouter-subscription-bm.md` (v2)
> - `docs/decisions/0010-payg-credits-model.md`
>
> 본문 §2 등에 남아 있는 Maho $20 / $200 가격과 과거 token cap 수치는 v1 잔재이며 현재 pricing으로 대체되었습니다: **Free $0 / Pro $5 / Max $20 구독 + PAYG 크레딧 $10 / $50 / $100 + BYOK (무료, 무제한, frontier는 BYOK 전용)**. 아래 "Revision 2026-06-28" 노트가 이 전환을 부분적으로 기록합니다.

**상태**: 의사결정 단계 종료, 실행 단계 진입 준비
**참조**: `aside-followup-bm-redesign-2026-06-27.md` (v2.0 plan), `docs/decisions/0009-openrouter-subscription-bm.md` (ADR)
**검토 단계**: hyperplan adversarial review (5명 적대팀, 19 high-confidence findings 추출, plan agent 실행 계획 산출)
**의사결정자**: user
**기록자**: Sisyphus

본 문서는 v2.0 plan과 ADR 0009의 실행 가능 상태를 정의합니다. 본 문서가 v2.0 plan과 충돌하면 **본 문서가 우선**합니다. 코드 작업 진행하면서 v2.0 plan은 inline 으로 수정됩니다.

---

## Revision 2026-06-28 — Q-3 / Q-5 / B1 reopened by user

After v1 launch prep was complete, user re-evaluated unit economics vs Aside competitive positioning and decided:
- Q-3 reopened: pricing changed from $20/$200 to $5/$20 (see ADR 0009 v2)
- Q-5 reopened: subscription markup increased from 30% to 50%; PAYG markup at 30%; no auto-kill-switch unchanged
- B1 reopened: 3-tier subscription (Free/Pro/Max) augmented with 2 orthogonal billing modes (PAYG credits, BYOK) (see ADR 0010)

Net cascade: 11 file changes across backend (relay + billing-proxy), frontend (pricing.astro, compare/aside.astro, terms.astro), CLI (`maho balance`, `maho buy-credits`), and docs (demos, runbook, audit).

Original v1 (2026-06-27) decisions preserved below for historical traceability.

---

## §1 락인된 의사결정 13개

### 기술 결정 (D1-D4)

| 코드 | 결정 | 영향 |
|---|---|---|
| **D1** | Agent backend = **Swiftide (maho-agent, in-process)** | ACP cleanup 후 그대로 사용 |
| **D2** | LLM proxy = **Cloudflare Workers + Rust WASM (workers-rs)** | 별도 `maho/billing-proxy/` Cargo workspace |
| **D3** | 결제 = **LemonSqueezy (MoR)** | Stripe acquisition risk 인지 |
| **D4** | OpenRouter = **standard API로 개발, 런칭 전 enterprise 협상** | enterprise 트랙 별도 |

### 비즈니스 결정 (B1-B4)

| 코드 | 결정 | 영향 |
|---|---|---|
| **B1** | Tier = **Free / Pro / Max** (3-tier, Enterprise 제외) ~~Aside parity only~~ + PAYG + BYOK orthogonal (v2, see ADR 0009 v2 + ADR 0010) | Aside parity + variable-usage coverage |
| **B2** | Privacy = **Zero-retention proxy** | 마케팅 메시지 강함 |
| **B3** | ADR 0009 = **Accepted** (Sisyphus draft → 사용자 검토 완료) | ADR 0001 부분 supersede |
| **B4** | 이메일 인증 = **도입** (Postmark / Resend) | abuse 방지 |

### 적대팀 검토 후 결정 (Q1-Q5)

| 코드 | 결정 | 영향 |
|---|---|---|
| **Q-1** | MVP signal gate = **NO** (6주 plan 바로 시작) | ⚠️ adversarial 강한 권고 override |
| **Q-2** | Quota 단위 = **USD with per-request cost ceiling** | C-H3 ($57k drain) 해결 |
| **Q-3** | 가격 노출 = ~~$20/$200 즉시 공개~~ → $5/$20 (v2, see ADR 0009 v2 §4) | pricing.astro 재작성 |
| **Q-4** | 아키텍처 보충 리뷰 = **YES (병렬 트랙)** | architect findings 갭 복구 |
| **Q-5** | Kill-switch 자동 트리거 = **없음 (수동 판단)** + ~~markup 30%~~ → 50% subscription / 30% PAYG (v2) | sunk cost risk 자기 인지 + 마진 개선 |

---

## §2 락인된 markup % + 사용자 가용 LLM 비용

| Tier | 월 가격 | OpenRouter 실비 한도 | Maho 순 마진 (수수료 차감 전) |
|---|---|---|---|
| Free | $0 | $0 (무료 모델만 — OpenRouter 무료 tier 라우팅) | $0 |
| Pro | $20 | ~$15.40 ($20 / 1.30) | ~$4.60 |
| Max | $200 | ~$153.80 ($200 / 1.30) | ~$46.20 |

**수수료 차감 후 실 마진** (Pro 기준):
- $20 사용자 결제
- LemonSqueezy 5% + $0.50 = $1.50
- CF Worker / relay 운영 분담 ~$1
- OpenRouter 실비 $15.40
- **Maho 순 = $20 - $1.50 - $1 - $15.40 = ~$2.10 / Pro 사용자 / 월**
- **Break-even: ~480 Pro 사용자 / 월 = $1,000 운영비 흡수**

**Cost ceiling 적용** (per-request):
- Free tier: 단일 요청 최대 $0.05 (소형 응답만 통과)
- Pro tier: 단일 요청 최대 $0.50 (Claude Sonnet 보통 사용 OK, Opus 큰 컨텍스트 차단)
- Max tier: 단일 요청 최대 $5 (Opus + 큰 컨텍스트도 OK, 단 1요청당)

> 위 수치들은 OpenRouter 실비 모니터링 후 30일 시점에 재조정 합니다 (수동 점검).

---

## §3 Hard Constraints 적용 체크리스트 (v2.0 plan 정정)

코드 작업 시점에 적용. 각 항목은 v2.0 plan 의 해당 § 와 함께 정정.

- [ ] **C-H1(a)**: `maho-chromium/browser/ai/maho_unified_agent_adapter.cc:27`의 `#include "maho/browser/ai/maho_acp_event_decoder.h"` 제거를 W0.1 **첫 단계**로 (헤더 파일 삭제 전)
- [ ] **C-H1(c,d)**: W0.1에서 `maho/crates/maho-acp-watchdog/` 삭제 단계 제거 (디렉토리 미존재). Cargo.toml workspace members 에서 `maho-acp-watchdog` 제거 단계도 제거 (안 들어있음)
- [ ] **C-H2**: §4.1의 `OpenAICompatibleProvider` 명시를 `swiftide_integrations::openai::GenericOpenAIBuilder::<MahoOpenAIConfig>` 로 교체. 위치: `maho/crates/maho-agent/src/swiftide_backend.rs:623`
- [ ] **C-H3**: §3.4 (proxy) + §3.5 (UserUsage) — quota 단위를 **USD with per-request cost ceiling** 으로 통일. tier 테이블에서 "메시지 수" 컬럼 제거, "USD 한도" 컬럼 추가
- [ ] **C-H4**: §3.5 — "CF Cron Trigger → 모든 DO reset" 제거. **Cloudflare Alarms API** (`setAlarm()`) 로 각 `UserUsage` DO 가 monthly reset 자체 스케줄링
- [ ] **C-H5**: §3.1 migration 에 `webhook_events` 또는 `processed_event_ids` 테이블 추가. §3.2 webhook 이벤트 리스트에 `subscription_payment_recovered` 추가
- [ ] **C-H6**: §3.3 — billing-proxy를 **별도 Cargo workspace** (`maho/billing-proxy/Cargo.toml`에 `[workspace]` section). 절대 root / relay workspace 에 넣지 말 것 (libsqlite3-sys 충돌)

---

## §4 추가 정정 (decisions converged)

- [ ] **D-2 적용**: W2.3 (Approval gate UI 왕복) 을 P1 Phase 2 → **post-launch** 로 이동
- [ ] **D-3 적용**: W2.2 (Browser tool ↔ Rust agent bridge) 를 P1 Phase 2 → **post-launch** 로 이동
- [ ] **D-6 적용**: workers-rs 버전을 `0.5` 핀에서 "current stable" 로. reqwest 0.12 대신 `worker::Fetch` 사용
- [ ] **R-3 적용**: LemonSqueezy webhook handler 를 abstraction layer 로 캡슐화 (Paddle/Stripe 전환 가능하도록)
- [ ] **R-4 적용**: §3.3 의 "D1 OR DO" 대안 텍스트 제거, DO + KV 락인
- [ ] **R-1 mitigation**: §3.4 의 auth cache TTL = 5초 (또는 cache 자체 제거) — v1에서는 후자 권장

---

## §5 Q-1 = NO 후속 영향 (MVP gate 스킵)

원래 plan agent 산출물의 **Wave 0 (User decisions)** → **Wave 5 (Phase C only if GO)** 구조는 폐기. 다음과 같이 적용:

- ❌ Task 4-9 (Signal MVP: LemonSqueezy Payment Link, minimal Worker, integration test, 2주 측정, GO/NO-GO) → **스킵**
- ✅ Wave 1 의 Task 1-7 중 **Task 1 (plan 정정), Task 2 (ADR 정정), Task 3 (ACP cleanup), Task 6 (SwiftideBackend MahoManaged), Task 7 (/auth/introspect)** → 그대로 실행
- ✅ Wave 5 의 Task 11-13 (Full relay + billing-proxy + pricing pages) → **GO 대기 없이 바로 실행**
- ✅ Q-4 → 별도 architecture-review 병렬 트랙 추가 (아래)

새 sprint 구조 (v2.0 plan §7 sequencing 일부 수정):

| Sprint | 주차 | 주요 작업 |
|---|---|---|
| 0.5 (병렬) | W0 (1-3일) | Task 1 (plan 정정) + Task 2 (ADR 정정) + Task 3 (ACP cleanup) + 아키텍처 보충 리뷰 (Q-4) |
| 1 | W1-2 | W1.1 relay schema + W1.3 CF Worker scaffold + W2.1 OpenRouter provider + W3.1 Tab Tidy |
| 2 | W3-4 | W1.2 LemonSqueezy + W1.4 OpenRouter proxy + W1.5 Usage tracking + W1.6 이메일 인증 |
| 3 | W5-6 | W3.2 Routines + W4.1 Pricing + W4.2 Privacy/Terms + W4.3 ADR finalize |
| 4 | W7-8 | W3.4 CLI + W3.3 MCP host (옵션) + W4.4 demo / 통합 QA |
| post-launch | W9+ | W2.2 Browser tool bridge + W2.3 Approval UI + W2.4 Session UI |

> W2.2 / W2.3 / W2.4 는 BM revenue 와 무관하므로 launch 후 진행.

---

## §6 아키텍처 보충 리뷰 (Q-4 병렬 트랙) — 새 워크스트림

원래 plan 의 architect 적대팀 멤버가 R1 findings 전송 손실. 그 갭을 채우기 위해 아래 트랙을 W0 시점 병렬 실행:

- **워크스트림**: ultrabrain category subagent 1명
- **소요**: 1-2일
- **출력**: separation of concerns / leaky abstraction / coupling / 기술 부채 누적 관점에서의 plan 검토 결과
- **결과 반영**: v2.0 plan 의 §3-§7 에 inline 추가 코멘트 / 정정
- **차단 관계**: 다른 워크스트림과 무관 (병렬 실행)

---

## §7 ⚠️ Risk Register (사용자 명시 동의 후 진행)

본 plan 은 다음 위험을 **사용자가 인지하고 진행**합니다:

1. **MVP 검증 없이 BM 빌드** (Q-1 NO override): 6주 인프라 구축 후 0 paying user 가능성 존재
2. **자동 kill-switch 없음** (Q-5): 매월 수동 점검 필요. founder cash 소모에 대한 객관적 trigger 없음
3. **30% markup** (Q-5): Pro 사용자당 순 마진 ~$2.10 → break-even ~480 Pro 사용자 필요
4. **adversarial team R1 손실** (architect): Q-4 보충 트랙으로 완화 중

권장 monthly health 체크 (수동):
- Pro 활성 구독 수 (목표: month 3 ≥ 100, month 6 ≥ 500)
- 월 OpenRouter 실비 vs MRR (목표: 마진 > $0)
- Churn rate (목표: < 15%)
- Free → Pro conversion rate (목표: > 2%)

위 수치는 자동 trigger 아니지만, 매월 review 시 "이 BM 그대로 갈지" 질문에 객관적 데이터 제공.

---

## §8 즉시 다음 액션 (Wave 0 즉시 실행 가능)

병렬 실행:
1. **Task 1 — plan v2.0 inline 정정** (C-H1~C-H6, D-2, D-3, D-6, R-1, R-3, R-4)
2. **Task 2 — ADR 0009 inline 정정** (tier table USD ceiling, markup 30%, kill-switch 언어 제거 / "수동 판단" 명시, Status Draft → Accepted)
3. **Task 3 — ACP cleanup PR** (corrected 순서)
4. **Q-4 — 아키텍처 보충 리뷰 트랙 spawn** (ultrabrain subagent)

이후 Wave 1 (W1.1, W1.3, W2.1, W3.1 등) 순차/병렬 진입.

---

## §9 Frontier model exclusion (v2)

Maho-managed Free/Pro/Max/PAYG tiers exclude frontier models (Claude Opus, GPT-5 base, o3, o4, gemini-3-ultra, all reasoning premium variants). Rationale: ADR 0009 v2 §7. Frontier accessible only via BYOK.

Model allowlist concrete slugs live in `MODEL_ALLOWLIST` KV namespace (billing-proxy), subject to monthly review per ADR 0009 v2 §8.

---

**문서 끝.**
