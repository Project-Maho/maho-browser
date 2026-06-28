# Aside Follow-up: BM Redesign Executable Plan

**작성일**: 2026-06-27
**산출 경로**: hyperplan adversarial review (5명 적대팀, R1+R2 부분, R3 collapsed into R2 STANDS) → plan agent formalization
**기반 문서**:
- `docs/plans/aside-followup-bm-redesign-2026-06-27.md` (v2 plan)
- `docs/plans/aside-followup-decisions-locked-2026-06-27.md` (locked decisions, conflict 시 우선)

**상태**: 실행 가능 (사용자 게이트 5개 해소 시 Wave 0 즉시 착수 가능)

> 충돌 시 우선순위: `decisions-locked` > 본 문서 > `bm-redesign`(v2 plan)

---

## Context

**User Request**: Aside follow-up v2 BM redesign plan (Cloudflare Workers billing proxy + LemonSqueezy + OpenRouter + Swiftide agent cleanup)을 실행 가능한 작업 계획으로 공식화하되, 의존성 분석, 병렬화, TDD 지향, atomic commit 전략을 포함.

**Source of Truth (on conflict, locked-decisions wins)**:
- `docs/plans/aside-followup-bm-redesign-2026-06-27.md` (v2 plan)
- `docs/plans/aside-followup-decisions-locked-2026-06-27.md` (locked decisions)

**Current State Verified**:
- `maho-acp/` exists (22 files) — ready for cleanup
- `maho-acp-watchdog/` does NOT exist as separate directory (binary is inside maho-acp)
- `billing-proxy/` does NOT exist — new workspace to create
- `tab_grouping.rs` exists (238 LOC, referenced from `maho-core/src/lib.rs` and `maho-ffi/src/lib.rs`)
- ADR 0009 exists at `docs/decisions/0009-openrouter-subscription-bm.md`

---

## USER-INPUT GATES (RESOLVED 2026-06-27)

모든 게이트가 해소되어 Wave 0 즉시 착수 가능합니다.

| Gate | 결정 | 영향 |
|------|------|------|
| **Q-A** | **Keep 3 SKUs only** (Free / Pro $20 / Max $200) | BYOK variants 미추가. dev/agency 세그먼트 손실 감수. 단순성 우선. |
| **Q-B** | **Add top-up SKUs** ($10 / $50 / $100) | T1.2 LemonSqueezy product setup 에 within-tier USD ceiling top-up variant 추가. T1.5 quota 로직이 top-up 적립 처리. |
| **Q-C** | **DELETE in W0.1** | T0.4 가 T0.3 ACP cleanup PR 에 합쳐짐. `tab_grouping.rs` + `pub mod` + maho-ffi 참조 모두 제거. |
| **Q-D** | **ADR 0001a scoped amendment** | ADR 0001 유지, 범위 한정된 0001a 부속 수정 작성. "partially superseded" 문구 제거. |
| **Q-E** | **Approve 3-layer enforcement** | T4.3 가 Q-2 ADR addendum 작성: (1) max_tokens cap 주입, (2) post-completion running tally + hard-stop, (3) ADR 에 설계 명시. |

---

## Task Dependency Graph

| Task | Depends On | Reason | Blocks |
|------|------------|--------|--------|
| T0.1 Plan v2 inline corrections | None | Baseline corrections | All downstream |
| T0.2 ADR 0009 corrections | None | ADR must be correct before impl | T4.3 |
| T0.3 ACP cleanup | None | Unblocks workspace builds | T1.1, T2.1, T3.1, T3.4 |
| T0.4 `tab_grouping.rs` deletion (merged into T0.3) | None | Dead code per §5.1 | T3.1 |
| T1.1 Relay schema + `/auth/introspect` | T0.3 | Schema needed by proxy + LS | T1.2, T1.5, T1.6 |
| T1.2 LemonSqueezy integration | T1.1 | Needs user table + tier fields; SKU=3 + top-ups | T1.5 (webhook tier state) |
| T1.3 CF Worker scaffold | None | Independent infra | T1.4, T1.5 |
| T1.4 OpenRouter proxy | T1.3 | Needs Worker + Q-E 3-layer enforcement | T2.1 |
| T1.5 Usage tracking (Durable Object) | T1.1, T1.3 | Needs relay schema + Worker runtime + top-up logic | T1.4 (quota check) |
| T1.6 Email verification | T1.1 | Needs user table fields | None (parallel) |
| T2.1 SwiftideBackend MahoManaged mode | T0.3, T1.4 | Needs proxy endpoint | None |
| T3.1 Tab Tidy UI | T0.3, T0.4 | ACP/dead-code gone | T4.4 |
| T3.2 Routines (hardcoded recipes) | T2.1 | Needs OpenRouter provider | T4.4 |
| T3.4 CLI (maho-cli) | T0.3 | Clean workspace | None |
| T4.1 Pricing page | None | 3 SKUs locked + top-ups | T4.4 |
| T4.2 Privacy/Terms rewrite | None | B2 already locked | T4.4 |
| T4.3 ADR 0009 finalize + 0001a amend | T0.2 | Q-D=0001a + Q-E=3-layer addendum locked | None |
| T4.4 Demo scripts + comparison page | T3.1, T4.1 | Needs working features | None |
| T4.5 Revenue E2E integration test | T1.2, T1.4, T1.5 | Full billing path required | Launch gate |
| T-ARCH Architecture review (Q-4) | None | Parallel advisory track | Advisory only |

---

## Parallel Execution Graph

```
Wave 0 (Start immediately — all gates resolved):
├── T0.1: Plan v2 inline corrections [quick]
├── T0.2: ADR 0009 corrections [quick]
├── T0.3: ACP cleanup PR + tab_grouping.rs deletion (T0.4 merged) [small-impl]
├── T4.2: Privacy/Terms rewrite [writing]
└── T-ARCH: Architecture supplementary review [ultrabrain] (advisory, parallel)

Wave 1 (After Wave 0 completes — BM Foundation):
├── T1.1: Relay schema + /auth/introspect [deep]
├── T1.3: CF Worker scaffold [deep]
├── T3.1: Tab Tidy UI [small-impl]
└── T3.4: CLI maho-cli [unspecified-high] (P2, can slip)

Wave 2 (After T1.1 + T1.3 complete):
├── T1.2: LemonSqueezy integration (3 SKUs + top-ups) [deep]
├── T1.4: OpenRouter proxy (3-layer enforcement) [deep]
├── T1.5: Usage tracking DO (top-up logic) [deep]
├── T1.6: Email verification [small-impl]
└── T2.1: SwiftideBackend MahoManaged [small-impl]

Wave 3 (After Wave 2 completes):
├── T3.2: Routines hardcoded recipes [unspecified-high]
├── T4.1: Pricing page (3 SKUs + top-up section) [visual-engineering]
├── T4.3: ADR 0009 finalize + ADR 0001a amendment + Q-2 addendum [quick]
└── T4.5: Revenue E2E test [deep]

Wave 4 (After Wave 3 completes — Launch prep):
└── T4.4: Demo scripts + comparison page [writing + visual-engineering]

Critical Path: T0.3 → T1.1 → T1.4 → T2.1 → T3.2 → T4.4
Estimated Parallel Speedup: ~55% faster than sequential (6 weeks → achievable)
```

---

## Tasks

### T0.1: Plan v2 inline corrections
- **What**: Apply C-H1..C-H6 + D-2/D-3/D-6 + R-1/R-3/R-4 corrections to `docs/plans/aside-followup-bm-redesign-2026-06-27.md`. Remove `/v1/messages` endpoint, LS abstraction layer, AuthSource enum.
- **Delegation**: `quick`, skills `[]`
- **Depends**: None
- **Acceptance**: Markdown lint passes; diff verifiable

### T0.2: ADR 0009 inline corrections
- **What**: USD ceiling tables, 30% markup, kill-switch removed, per-request caps, Status → Accepted
- **Delegation**: `quick`, skills `[]`
- **Depends**: None
- **Acceptance**: Status="Accepted"; tier table has USD columns

### T0.3: ACP Cleanup PR (includes T0.4 tab_grouping.rs deletion)
- **What**: Remove ACP code per corrected order (C-H1(a) include removal FIRST). Skip non-existent watchdog dir. **Includes `tab_grouping.rs` deletion** per Q-C=DELETE — delete file, remove `pub mod tab_grouping;` from `maho-core/src/lib.rs`, clean FFI references in `maho-ffi/src/lib.rs` (lines 6316-6325). R-5 mitigation: FULL build (no `--skip-rust`).
- **Delegation**: `small-impl`, skills `["git-master"]`
- **Depends**: None
- **Acceptance**: `bun run typecheck`+`bun run test`+`build_maho.py` (full link) all pass; no "maho-acp" or "tab_grouping" grep hits; ACP migration plans moved to `docs/archive/`

### T1.1: Relay Schema + `/auth/introspect`
- **What**: Migration (tier/subscription/email/processed_events fields). `/auth/introspect` endpoint (service-token-gated). `period_start_at` for R-3 mitigation. Anniversary-keyed reset for R-2. TDD: tests first.
- **Risk mitigations**: R-2 (anniversary keying), R-3 (period_start_at guard), R-10 (degradation-compatible response shape), R-11 (cache invalidation hook on verify webhook)
- **Delegation**: `deep`, skills `[]`
- **Depends**: T0.3
- **Acceptance**: `cd maho/relay && cargo test && cargo clippy -- -D warnings`; `/auth/introspect` returns `{ user_id, tier, subscription_status, email_verified, expires_at, period_start_at }`

### T1.2: LemonSqueezy Integration
- **What**: Webhook handler for 8 events (`order_created`, `subscription_created/updated/cancelled/resumed/expired`, `subscription_payment_failed`, `subscription_payment_recovered`). HMAC verification. Tier state machine with period_start_at guard. **NO abstraction layer** (R-3 scrapped per insight bundle). **3 SKUs locked** (Q-A=Keep 3 SKUs) + **top-up SKUs $10/$50/$100** (Q-B=Add top-ups). Top-up purchase event handler adds to DO USD ceiling within current period.
- **Risk mitigations**: R-2 (anniversary reset), R-3 (period guard drops stale replays), R-6 (creates path T4.5 tests)
- **Delegation**: `deep`, skills `[]`
- **Depends**: T1.1
- **Acceptance**: All 8 webhook events + top-up purchase event → correct tier/ceiling transitions; HMAC-SHA256 verified; idempotent via `processed_event_ids`; period guard drops old-period events

### T1.3: CF Worker Scaffold (billing-proxy)
- **What**: Create `maho/billing-proxy/` as **SEPARATE Cargo workspace** (C-H6 — NOT in root/relay workspace). workers-rs current stable (D-6). `worker::Fetch` instead of reqwest. Minimal `/health` + `/v1/chat/completions` stub. DO + KV bindings. `wrangler.toml` with staging/production envs. **NO `/v1/messages`** (deleted per hard constraint).
- **Delegation**: `deep`, skills `[]`
- **Depends**: None
- **Acceptance**: Isolated workspace (not in root/relay Cargo.toml); `cd maho/billing-proxy && cargo check` passes; wrangler dev validates; `UserUsage` DO class + `MAHO_AUTH_CACHE` KV namespace declared

### T1.4: OpenRouter Proxy Implementation
- **What**: Auth check (KV cache → relay fallback → degradation) → quota check (DO) → model matrix check → OpenRouter call (master key) → SSE streaming → post-completion cost tally → DO update. Zero-retention (never store prompt/response). **Q-E=Approved 3-layer enforcement baked in.**
- **Risk mitigations**:
  - **R-1 three-layer (Q-E locked)**: (1) max_tokens cap per tier ($0.05→~200tok / $0.50→~4000tok / $5→~50000tok) injected on every request; (2) post-completion running tally + hard-stop on next request after `cost_usd_accumulated >= (base_ceiling + topup_ceiling)`; (3) ADR addendum written in T4.3 documents this physically-bounded design
  - R-4 bypass token: Accept `X-Maho-Verify-Bypass` header (single-use 5-min expiry), bypasses KV cache
  - R-7 lazy DO reset: Every request checks `now() >= next_reset_at`, resets if past due
  - R-10 degradation modes when relay unreachable: (a) deny-all-paid with 503+retry-after, (b) allow-Free-only with telemetry, (c) bypass-token overrides relay check; all with dashboard telemetry
  - Hardcoded model matrix + KV emergency override
- **Delegation**: `deep`, skills `[]`
- **Depends**: T1.3
- **Acceptance**: SSE streams OpenRouter response; degradation modes verified; zero-retention grep clean; all 7 unit test categories pass

### T1.5: Usage Tracking (Durable Object)
- **What**: `UserUsage` Durable Object. **MUST be DO, not KV** (hard constraint — KV 60s edge propagation lets Max users overshoot $50+ ceiling). Uses Alarms API for self-scheduled monthly reset (C-H4, NOT Cron Trigger). Reset cycle keyed to per-user subscription anniversary (R-2). Lazy reset fallback on read (R-7). **Top-up ceiling increment (Q-B)**: top-up purchase webhook adds USD to current-period ceiling (e.g., Pro $15.40 + $50 top-up → effective $65.40 this period; top-up does NOT roll over to next period).
- **Delegation**: `deep`, skills `[]`
- **Depends**: T1.1 (user anniversary from relay), T1.3 (Worker runtime)
- **Acceptance**: DO stores `{user_id, current_period_start, cost_usd_accumulated, base_ceiling_usd, topup_ceiling_usd, next_reset_at}`; increment returns `{allowed, remaining_usd}`; alarm schedules at anniversary; lazy reset works when alarm missed; top-up event increments `topup_ceiling_usd`

### T1.6: Email Verification
- **What**: Token gen → email send (**Postmark vs Resend decided Week 1** per insight bundle) → verify endpoint → bypass token issuance (R-4)
- **Risk mitigations**: R-4 (verify endpoint returns single-use 5-min bypass token), R-8 (admin reset endpoint for support tickets), R-11 (KV cache invalidation hook on verify)
- **Delegation**: `small-impl`, skills `[]`
- **Depends**: T1.1
- **Acceptance**: Signup → token → email → verify → `verify_bypass_token` returned (single-use, 5-min); `/auth/introspect` reflects `email_verified: true`

### T2.1: SwiftideBackend MahoManaged Mode
- **What**: Add `MahoManaged` auth source to SwiftideBackend. **NO `AuthSource` enum** — just store `base_url` + `api_key` (hard constraint per skeptic #7). Per C-H2: use `GenericOpenAIBuilder` (**accepted, not refactored** — YAGNI per insight bundle, ADR 0009 forecloses non-OpenAI-compat). Endpoint = `https://billing-proxy.maho.app/v1`, key = user Bearer token from relay.
- **Delegation**: `small-impl`, skills `[]`
- **Depends**: T0.3, T1.4
- **Acceptance**: MahoManaged endpoint config works; BYOK regression test still passes

### T3.1: Tab Tidy UI Connection
- **What**: Default-ON, **no A/B flag, no settings toggle** (hard constraint per skeptic #17). Sidebar "Tidy" button (Lucide `wand-2`). Mojo `RequestTidy()` → LLM call (via SwiftideBackend) → `TidySuggestion` modal → folder apply. LLM-only approach (embedding path deleted in T0.4).
- **Delegation**: `small-impl`, skills `["frontend-ui-ux"]`
- **Depends**: T0.3, T0.4
- **Acceptance**: Tidy button visible (Lucide wand-2); LLM round-trip → modal → folder apply works; default-on (no pref gates it); Chromium build passes

### T3.2: Routines (Hardcoded Recipes)
- **What**: Per insight bundle decision — **hardcoded recipes (3-5 dev-days)**, NOT programmable scheduler (15-20 dev-days). Preserves Max-tier value justification at 1/5 cost. 3-5 built-in templates (e.g., "Morning Briefing", "Close old tabs", "Summarize reading list") on simple cron. Max-tier gated.
- **Risk mitigations**: R-9 (document mutable state access pattern), R-12 (demos depend only on these committed features)
- **Delegation**: `unspecified-high`, skills `[]`
- **Depends**: T2.1
- **Acceptance**: 3-5 recipes run on schedule; results in `chrome://maho-routines` minimal UI; tier check enforced

### T3.4: CLI (maho-cli) — P2
- **What**: New crate. **Dev-segment acquisition surface** (insight bundle: KEEP per Stripe/Vercel/Cursor precedent for BYOK monetization). Connects to billing-proxy for MahoManaged users.
- **Delegation**: `unspecified-high`, skills `[]`
- **Depends**: T0.3
- **Acceptance**: `maho-cli` builds; `maho chat "hello"` sends through proxy or direct BYOK; auth flow works

### T4.1: Pricing Page Rewrite
- **What**: Rewrite `website/src/pages/pricing.astro` — **3-tier table (Q-A=Keep 3 SKUs)** with **USD ceiling framing** (not message count). BYOK = "Free + Use your own key" (footnote). **Top-up section (Q-B=Add top-ups)**: separate section showing "Pro/Max 사용자 — 한도 추가 구매 $10 / $50 / $100". LemonSqueezy checkout links for tier subscriptions + top-up one-time purchases. Trust message. Free tier framed as **"$0.50/mo USD ceiling on free models only"**.
- **Delegation**: `visual-engineering`, skills `["frontend-ui-ux", "copywriting"]`
- **Depends**: None
- **Acceptance**: `cd website && bun run build`; mobile responsive; checkout URLs functional for 3 tiers + 3 top-up SKUs

### T4.2: Privacy/Terms Rewrite
- **What**: Update `privacy.astro` and `terms.astro` — zero-retention proxy policy, OpenRouter sub-processor disclosure, GDPR right-to-erasure, subscription billing terms (auto-renewal, refunds), **top-up purchase terms (one-time, non-refundable beyond 14-day window, doesn't roll over)**, acceptable use policy.
- **Delegation**: `writing`, skills `["copywriting"]`
- **Depends**: None (B2 already locked)
- **Acceptance**: `cd website && bun run build`; required clauses present including top-up terms

### T4.3: ADR 0009 Finalize + ADR 0001a Amendment + Q-2 Addendum
- **What**:
  - Final ADR 0009 (Status → Accepted)
  - **Q-D = ADR 0001a scoped amendment**: write new `docs/decisions/0001a-scoped-amendment-to-0001.md` that explicitly scopes which sections of ADR 0001 are amended by 0009; ADR 0001 itself keeps "amended by 0001a" notation (NOT "partially superseded")
  - **Q-E = Q-2 ADR addendum**: append addendum to ADR 0009 documenting the 3-layer enforcement (pre-flight max_tokens cap + post-completion running tally + acceptance of bounded overshoot ≤ max_tokens × highest model price), explicitly noting that pre-flight exact-USD enforcement is physically impossible
- **Delegation**: `quick`, skills `[]`
- **Depends**: T0.2
- **Acceptance**: No contradictions between ADR 0001, 0001a, 0009; addendum documents pre-flight impossibility + three-layer mitigation

### T4.4: Demo Scripts + Comparison Page
- **What**: Demo videos (30-60s each) + `website/src/pages/compare/aside.astro`. Per insight bundle: decoupled from Routines slip (skeptic #15) + reordered to lead with cross-platform supporting clips (creative #2 — **BUT cross-platform parity ABANDONED for launch per architect**; iOS shell is reference-only per §9.1). Demos show: (1) Zero-setup AI, (2) Tab Tidy, (3) proxy tier upgrade flow.
- **Hard constraint**: NO claims about iOS/Android parity (creative #1 destroyed by architect).
- **Risk mitigation R-12**: Depend ONLY on launch-committed features.
- **Delegation**: `writing`, skills `["copywriting"]`
- **Depends**: T3.1, T4.1
- **Acceptance**: 3 demo scripts written (no cross-platform claims); comparison page accurate; `bun run build` passes

### T4.5: Revenue E2E Integration Test (LAUNCH GATE)
- **What**: Per risk R-6 (completely absent in v2): Single live E2E test using LemonSqueezy test mode + ephemeral test account + OpenRouter free model. Tests: LS checkout → webhook → tier set → proxy allow → usage decrement → ceiling enforcement → 429.
- **Delegation**: `deep`, skills `[]`
- **Depends**: T1.2, T1.4, T1.5
- **Acceptance**: Test passes in CI; repeatable; ephemeral accounts cleaned up; no state leakage

### T-ARCH: Architecture Supplementary Review (Q-4)
- **What**: Parallel advisory track. Review separation of concerns, leaky abstractions, coupling, tech debt accumulation. Output: inline comments/corrections to plan. Per locked-decisions §6.
- **Risk mitigation**: R-9 (mutable state access patterns documented per workstream)
- **Delegation**: `ultrabrain`, skills `[]`
- **Depends**: None (fully parallel)
- **Acceptance**: Written review document covering SoC, coupling, abstraction; per-workstream recommendations

---

## Risks → Mitigations (Adversarial Survivors)

| Risk | Source | Mitigation | Embedded In |
|------|--------|-----------|-------------|
| **R-1** USD cost ceiling pre-flight enforcement physically impossible | validator #6 + architect strengthened | Three-layer: max_tokens cap + post-completion tally + ADR addendum | T1.4, T4.3 |
| **R-2** DO calendar reset vs LS anniversary mismatch | validator #1 | DO reset keyed to per-user subscription anniversary | T1.1, T1.5 |
| **R-3** LS webhook replay of new old-period events | validator #2 | `period_start_at` guard in tier state | T1.1, T1.2 |
| **R-4** KV 5s vs edge 60s race | validator #3 + architect refinement | `verify_bypass_token` (single-use 5-min) | T1.6, T1.4 |
| **R-5** ACP FFI removal linker-only failure | validator #4 | Full link in CI (no `--skip-rust`), workspace check | T0.3 |
| **R-6** Revenue-path E2E test absent | validator #5 | New T4.5 workstream | T4.5 |
| **R-7** DO Alarm dropped → stuck at $0 | validator #7 + creative refinement | Lazy reset on read | T1.5 |
| **R-8** Email verify P1 yet gates proxy | skeptic #10 + architect STANDS | Admin reset path OR remove gate from proxy | T1.6 |
| **R-9** SwiftideBackend mutable singleton | architect #6 | Document ownership per workstream; Q-4 review | T-ARCH, T3.2 |
| **R-10** distributed monolith proxy↔relay | architect #1 | Three explicit degradation modes with telemetry | T1.1, T1.4 |
| **R-11** auth/introspect 60s cache vs verify | skeptic #13 | Explicit cache invalidation on email-verify webhook | T1.1, T1.6 |
| **R-12** W3.2 Routines slip blocks demos | skeptic #15 | Demos depend only on launch-committed features | T4.4 |

---

## Commit Strategy

Atomic commits per PR, one PR per task:

| PR | Branch | Commits |
|----|--------|---------|
| T0.1 | `fix/plan-v2-corrections` | 1: "docs: apply adversarial review corrections to v2 plan" |
| T0.2 | `fix/adr-0009-corrections` | 1: "docs: finalize ADR 0009 — USD ceiling, 30% markup, accepted" |
| T0.3 | `cleanup/remove-acp` | 2-3: (1) "refactor: remove ACP include from unified adapter" (2) "cleanup: delete maho-acp crate + FFI module + Chromium files" (3) "docs: archive ACP migration plans" |
| T0.4 | merged into T0.3 OR `cleanup/remove-tab-grouping` | 1: "cleanup: delete tab_grouping.rs (Spaces path foreclosed §5.1)" |
| T1.1 | `feat/relay-tier-schema` | 2: (1) "feat(relay): add tier/subscription migration + processed_events table" (2) "feat(relay): implement /auth/introspect endpoint" |
| T1.2 | `feat/relay-lemonsqueezy` | 2: (1) "feat(relay): LemonSqueezy webhook signature verification" (2) "feat(relay): tier state machine from LS webhook events" |
| T1.3 | `feat/billing-proxy-scaffold` | 1: "feat: scaffold billing-proxy CF Worker (separate workspace)" |
| T1.4 | `feat/billing-proxy-openrouter` | 3: (1) "feat(billing-proxy): auth + quota middleware" (2) "feat(billing-proxy): OpenRouter SSE streaming proxy" (3) "feat(billing-proxy): degradation modes + bypass token" |
| T1.5 | `feat/billing-proxy-usage-do` | 1: "feat(billing-proxy): UserUsage Durable Object with alarm reset" |
| T1.6 | `feat/relay-email-verify` | 1: "feat(relay): email verification flow + bypass token" |
| T2.1 | `feat/swiftide-maho-managed` | 1: "feat(maho-agent): add MahoManaged auth source to SwiftideBackend" |
| T3.1 | `feat/tab-tidy-ui` | 2: (1) "feat(chromium): Tab Tidy Mojo interface + handler" (2) "feat(chromium): Tab Tidy sidebar button + suggestion modal" |
| T3.2 | `feat/routines-recipes` | 2: (1) "feat(maho-core): hardcoded routine recipes + scheduler" (2) "feat(chromium): chrome://maho-routines minimal UI" |
| T3.4 | `feat/maho-cli` | 1: "feat: maho-cli crate (dev-segment acquisition)" |
| T4.1 | `feat/website-pricing` | 1: "feat(website): rewrite pricing page — 3-tier USD ceiling" |
| T4.2 | `feat/website-legal` | 1: "feat(website): rewrite privacy + terms for zero-retention proxy BM" |
| T4.3 | `docs/adr-finalize` | 2: (1) "docs: ADR 0001a scoped amendment" (2) "docs: ADR 0009 accepted + Q-2 enforcement addendum" |
| T4.4 | `feat/website-demos-comparison` | 1: "feat(website): demo scripts + aside comparison page" |
| T4.5 | `test/revenue-e2e` | 1: "test: revenue path E2E (LS test mode → proxy → usage)" |

**Merge order**: T0.x → T1.x → T2.x → T3.x → T4.x (respecting dependency graph within waves).

---

## Success Criteria (Launch Gate)

All must pass before public launch:

1. `cd maho && bun run typecheck` passes
2. `cd maho && bun run test` passes
3. `cd maho/relay && cargo test && cargo clippy -- -D warnings` passes
4. `cd maho/billing-proxy && cargo check` passes
5. `cd website && bun run build` passes
6. `python3 maho-chromium/build/scripts/build_maho.py` passes (full link)
7. T4.5 revenue E2E test passes
8. Zero grep hits for "maho-acp" or "tab_grouping" in workspace
9. ADR 0009 status = Accepted
10. Pricing page live with correct tier/pricing info

---

## Execution Instructions

모든 user gate 가 해소되어 즉시 Wave 0 착수 가능합니다.

1. **Wave 0**: Fire IN PARALLEL (no inter-dependencies):
   ```typescript
   task(category="quick", prompt="T0.1: Apply plan v2 inline corrections...")
   task(category="quick", prompt="T0.2: Apply ADR 0009 corrections...")
   task(category="small-impl", load_skills=["git-master"], prompt="T0.3: ACP cleanup PR + tab_grouping.rs deletion...")
   task(category="writing", load_skills=["copywriting"], prompt="T4.2: Privacy/Terms rewrite (incl. top-up terms)...")
   task(category="ultrabrain", prompt="T-ARCH: Architecture supplementary review...")
   ```

2. **Wave 1**: After Wave 0:
   ```typescript
   task(category="deep", prompt="T1.1: Relay schema + /auth/introspect...")
   task(category="deep", prompt="T1.3: CF Worker scaffold...")
   task(category="small-impl", load_skills=["frontend-ui-ux"], prompt="T3.1: Tab Tidy UI...")
   task(category="unspecified-high", prompt="T3.4: CLI maho-cli...")  // P2
   ```

3. **Wave 2**: After T1.1 + T1.3:
   ```typescript
   task(category="deep", prompt="T1.2: LemonSqueezy integration (3 SKUs + top-ups)...")
   task(category="deep", prompt="T1.4: OpenRouter proxy (3-layer enforcement)...")
   task(category="deep", prompt="T1.5: Usage tracking DO (top-up logic)...")
   task(category="small-impl", prompt="T1.6: Email verification...")
   task(category="small-impl", prompt="T2.1: SwiftideBackend MahoManaged...")
   ```

4. **Wave 3**: After Wave 2:
   ```typescript
   task(category="unspecified-high", prompt="T3.2: Routines hardcoded recipes...")
   task(category="visual-engineering", load_skills=["frontend-ui-ux", "copywriting"], prompt="T4.1: Pricing page (3 SKUs + top-up section)...")
   task(category="quick", prompt="T4.3: ADR 0001a amendment + ADR 0009 accepted + Q-2 addendum...")
   task(category="deep", prompt="T4.5: Revenue E2E test...")
   ```

5. **Wave 4**: After Wave 3:
   ```typescript
   task(category="writing", load_skills=["copywriting"], prompt="T4.4: Demo scripts + comparison page (no cross-platform parity claims)...")
   ```

6. **Final QA**: Run all 10 launch gate items.

---

## Adversarial Provenance

이 계획은 hyperplan adversarial review 의 산출물입니다:

- **Members spawned**: skeptic (`unspecified-low`), validator (`unspecified-high`), researcher (`deep`), architect (`ultrabrain`), creative (`artistry`)
- **Researcher**: DROPPED (R1 무응답, deep 카테고리 — hyperplan skill 명시적 허용)
- **Rounds executed**: R1 complete (4명 발견 40개), R2 partial (skeptic + architect cross-attack full STANDS markings 포함; validator + creative R2 무응답 — team-mode response failure)
- **R3 collapse**: R2 STANDS markings 을 defend/concede proxy 로 사용 (Q-4 architect-gap recovery 가 이번 restart 의 명시적 목적이었고 성공)
- **Findings survived/refined**: 34/40 (skeptic 17/18, validator 7/7, architect 5/7, creative 5/8)
- **Findings destroyed/conceded**: 6 (skeptic #6 DO reject, architect #3 GenericOpenAIBuilder leak, architect #7 LS abstraction theoretical, creative #1 cross-platform parity, creative #3 BGE Spaces, creative #5 magic-link)

본 문서의 모든 hard constraint / decision / risk / open question 은 위 adversarial 필터를 통과한 것만 반영됨.

---

**문서 끝.**
