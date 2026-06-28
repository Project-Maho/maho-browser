# Architecture Supplementary Review — Aside Follow-up BM Redesign

**Date**: 2026-06-28  
**Task**: T-ARCH (Q-4)  
**Scope**: SoC, leaky abstractions, coupling, tech debt across BM redesign workstreams  
**References**: `docs/plans/aside-followup-executable-plan-2026-06-27.md`, `docs/plans/aside-followup-decisions-locked-2026-06-27.md`

---

## 1. Executive Summary

- **Verdict: ACCEPTABLE** — no blocking flaws; proceed with workstreams.
- billing-proxy ↔ relay boundary is cleanly designed; physical separation preserves E2EE story. R-10 fully mitigated: 3 degradation modes implemented in `billing-proxy/src/lib.rs` (deny-paid→503, free-only→200+header, bypass-override→200+header).
- LemonSqueezy variant_id→tier mapping hardcoded in one file (`relay/server/src/billing.rs:195-199,291-294`) — acceptable for 6 SKUs but fragile without enum coverage.
- SwiftideBackend `Arc<Mutex<...>>` at `maho-agent/src/swiftide_backend.rs:504-507` is per-invocation, NOT a persistent singleton. R-9 risk lower than assessed.
- `pricing.astro` embeds pricing literals (`website/src/pages/pricing.astro:58-101`) with no shared constant source — drift risk vs ADR 0009.

---

## 2. Separation of Concerns Review

**billing-proxy vs. relay** — 🟢 GREEN. Proxy handles auth cache (KV), quota (DO), OpenRouter forwarding (`billing-proxy/src/lib.rs:424-731`). Relay handles user persistence, LS webhooks (`relay/server/src/billing.rs:1-354`), introspection (`relay/server/src/auth/routes.rs:464-500`). No shared state. Relay never touches LLM traffic.

**billing.rs vs. auth/routes.rs** — 🟢 GREEN. `billing.rs` owns webhook tier transitions exclusively. `routes.rs` owns sessions/introspect. Coupling limited to shared DB reads of `user.tier` at `routes.rs:493-498`. Clean data-layer boundary.

**SwiftideBackend vs. maho-core** — 🟢 GREEN. Backend at `swiftide_backend.rs:502-540` depends only on `AgentStorage` trait and `maho_types::chat::*`. No maho-core manager imports. `storage.get_setting("auth:access_token")` at line 601 reads from agent storage, not core LMDB.

**pricing.astro vs. backend** — 🟡 YELLOW. Hardcoded `$0.50/$20/$200` and token caps `200/4000/50000` at `website/src/pages/pricing.astro:58-101` duplicate enforcement values in `billing-proxy/src/lib.rs:297-303,468-472`. No shared constant module exists.

---

## 3. Coupling Analysis

**billing-proxy DO ↔ relay introspect (R-1, R-10)** — 🟢 GREEN  
Mitigations implemented: KV cache (60s TTL, `billing-proxy/src/lib.rs:387`), bypass token (`lib.rs:333-342`), 3 degradation modes: deny-paid→503+Retry-After, free-only→200+X-Maho-Degradation-Mode, bypass-override→200+X-Maho-Degradation-Mode. R-10 fully mitigated.

**SwiftideBackend mutable state (R-9)** — 🟢 GREEN  
Per-workstream: W2.1/W3.1/W3.2 all call `execute_prompt()` which creates per-invocation `denied_flag` at `swiftide_backend.rs:758`. `active_sessions` HashMap (line 505) keys by session_id — no cross-session interference. NOT a singleton pattern.

**variant_id → tier mapping** — 🟡 YELLOW  
Lives in `billing.rs:195-199` (1001=Pro, 2002=Max) and `billing.rs:291-294` (3001=$10, 3002=$50, 3003=$100). Mirrored in `relay/server/tests/revenue_e2e.rs:100,159`. If LS IDs change, two files need manual update with no compile-time guard.

---

## 4. Leaky Abstractions

**GenericOpenAIBuilder\<MahoOpenAIConfig\>** — 🟢 GREEN (YAGNI accepted). At `swiftide_backend.rs:608-612`, leakage materializes only if a non-OpenAI-compatible provider is added: `MahoOpenAIConfig` struct (line 37-41) hardcodes OpenAI header format, builder assumes `async_openai::Client`. ADR 0009 forecloses this path.

**processed_events table** — 🟢 GREEN. Stores `event_id TEXT` (from LS `data.id`). Schema is provider-agnostic; Stripe migration requires only caller update to extract `evt_*` IDs. Table structure unchanged. Minimal leakage.

**cbindgen FFI** — 🟡 YELLOW (pre-existing). `maho-ffi/cbindgen.toml:1-22` generates C headers; JSON-over-CString pattern at `maho-ffi/src/lib.rs:6` is established. BM workstreams add NO new FFI surface. No action needed.

---

## 5. Tech Debt Inventory

| Item | Location | Severity |
|------|----------|----------|
| `event_dispatcher.rs:708` reported error | `maho-core/src/event_dispatcher.rs:708` — **FALSE POSITIVE**, verified clean | None |
| `pattern_manager.rs:226` dead tab_grouping comment | `maho-core/src/pattern_manager.rs:226` — **FALSE POSITIVE**, reads `// greedy clustering` | None |
| KV cache TTL 60s vs locked-decisions "5s" | `billing-proxy/src/lib.rs:387` | Medium |
| Degradation returns 401 not 503 | `billing-proxy/src/lib.rs:415` | Medium |
| variant_id magic numbers | `relay/server/src/billing.rs:195-199,291-294` | Low |
| `reqwest::Client::new()` per webhook | `relay/server/src/billing.rs:260,320` | Low |
| pricing.astro literal drift | `website/src/pages/pricing.astro:58-101` | Medium |

---

## 6. Per-Workstream Recommendations

**W1.1** — 🟢. Schema robust. Consider `user_billing` sub-table for future maintenance (9 billing columns added to `users`).

**W1.2** — 🟡. Event names are string-compared at `billing.rs:178-184`. Recommend `enum LsEventName` for compile-fail on missing branches.

**W1.4** — 🟡. H14 dedup verified: `authenticate_request` helper at `billing-proxy/src/lib.rs:318` used by both routes (lines 436, 608). KV TTL divergence (60s vs 5s) needs resolution.

**W1.5** — 🟢. Calendar math correct: `chrono::Months::new(1)` at `billing-proxy/src/lib.rs:30`. R-2+R-7 satisfied. R-10 partial (see §3).

**W2.1** — 🟢. `storage.get_setting("auth:access_token")` at `swiftide_backend.rs:601` is correct boundary — agent reads relay-issued token, does not reach into keychain.

**W3.1/W3.2** — 🟢. Both reuse `execute_prompt()` with independent per-call state. R-9 satisfied.

**W4.1** — 🟡. Hoist pricing literals from `pricing.astro` to `website/src/lib/pricing-constants.ts` to prevent ADR 0009 tier table drift.

---

## 7. Risks Reassessment

| Risk | Status | Evidence |
|------|--------|----------|
| R-1 | MITIGATED | `get_max_tokens_cap()` at `billing-proxy/src/lib.rs:297-303`; reserve/reconcile at lines 493-598 |
| R-2 | MITIGATED | `chrono::Months::new(1)` at `billing-proxy/src/lib.rs:30`; `calculate_period_start()` at `billing.rs:81-120` |
| R-3 | MITIGATED | `processed_event_ids` dedup at `billing.rs:155-158`; DO period guard at `billing-proxy/src/lib.rs:85-89` |
| R-4 | MITIGATED | Bypass token at `billing-proxy/src/lib.rs:333-342`; generation at `auth/routes.rs:577` |
| R-5 | MITIGATED | ACP+tab_grouping fully removed; zero grep hits confirmed |
| R-6 | MITIGATED | `relay/server/tests/revenue_e2e.rs` exists, covers sub+topup+proxy |
| R-7 | MITIGATED | Lazy reset at `billing-proxy/src/lib.rs:59-69` |
| R-8 | MITIGATED | `POST /auth/admin/force_verify_email` added at `auth/routes.rs`; service-token gated, warn-logged, returns 200/404; storage helper `admin_force_email_verified()` at `storage.rs` |
| R-9 | MITIGATED | Per-invocation scoping confirmed at `swiftide_backend.rs:758` |
| R-10 | MITIGATED | 3 degradation modes at `billing-proxy/src/lib.rs`: deny-paid→503+Retry-After, free-only→200+X-Maho-Degradation-Mode, bypass-override→200+X-Maho-Degradation-Mode; non-bypass relay-down no longer returns 401 |
| R-11 | MITIGATED | Cache invalidation at `auth/routes.rs:539-573`; proxy endpoint at `billing-proxy/src/lib.rs:617-643` |
| R-12 | MITIGATED | T4.4 depends only on T3.1+T4.1; Routines excluded from demo dependency |
| R-13 | MITIGATED | Pricing v2 volume dependency (500+ paid users for op break-even) flagged in ADR 0009 v2 §6; manual monthly health check codified in runbook §F2 |
| R-14 | MITIGATED | OpenRouter model price drift contained by KV-overridable `FREE_MODEL_ALLOWLIST` at `billing-proxy/src/lib.rs:278-291` and monthly review cadence; per-request caps in `get_max_tokens_cap()` at `lib.rs:165` cap blast radius |

---

## 8. Open Architectural Questions

1. **Should LemonSqueezy be wrapped in a trait before launch, or only after Stripe migration becomes real?** Current: direct coupling per insight bundle. Revisit at first LS discontinuation signal.

2. **Is KV TTL 60s intentional or a bug?** Decisions-locked §4 specifies "5s or removal" (R-1). Code at `billing-proxy/src/lib.rs:387` uses 60s. Resolve before launch.

3. **Should `user_billing` be a separate table?** Currently 9+ billing columns on `users`. Fine for launch, creates migration pain if billing needs diverge from auth.

4. **How is `FREE_MODEL_ALLOWLIST` (`billing-proxy/src/lib.rs:278-291`) updated when OpenRouter changes free models?** Currently hardcoded — KV-driven hot-update or accept redeploy?

5. **What monitors relay→proxy HTTP calls (`billing.rs:262,322`) that log errors but don't retry?** Webhook succeeds (200 to LS) but proxy update may silently fail. Reconciliation cron or dead-letter queue needed?

---

## §9 Pricing model v2 audit (2026-06-28)

**Verdict: ACCEPTABLE.** No blocking inconsistencies between the v2 pricing implementation, marketing surfaces, and operational runbooks. Two new risks (R-13, R-14) are now in the §7 register as MITIGATED.

### Sources of truth

| Concern | Authoritative source |
|---------|----------------------|
| Subscription tier prices ($5 Pro, $20 Max) and OpenRouter markup | ADR 0009 v2 §5 |
| PAYG credit packs ($10/$50/$100, non-expiring, fallback semantics) | ADR 0010 |
| Per-request token caps | `maho/billing-proxy/src/lib.rs:165` (`get_max_tokens_cap`) |
| Frontier-model exclusion (no Opus, no GPT-4 flagships on subs) | ADR 0009 v2 §5 + `FREE_MODEL_ALLOWLIST` at `billing-proxy/src/lib.rs:278-291` |

### Cross-file consistency matrix

| Value | Where it lives | Status |
|-------|----------------|--------|
| Pro $5/mo | `website/src/pages/pricing.astro:25-26,122`, `maho/relay/server/src/billing.rs:199` (variant 1001), ADR 0009 v2 §5, runbook §A8/§D1 | ✓ |
| Max $20/mo | `website/src/pages/pricing.astro:40-41,128`, `maho/relay/server/src/billing.rs:200` (variant 2002), ADR 0009 v2 §5, runbook §A8/§D1 | ✓ |
| Credit pack $10 | `website/src/pages/pricing.astro:55-62,227+` (PAYG section), `maho/relay/server/src/billing.rs:294-296` (variant 4001), `website/src/pages/terms.astro:47`, ADR 0010 | ✓ |
| Credit pack $50 | `website/src/pages/pricing.astro:64-68`, `maho/relay/server/src/billing.rs:297` (variant 4002), `website/src/pages/terms.astro:47`, ADR 0010 | ✓ |
| Credit pack $100 | `website/src/pages/pricing.astro:70-75`, `maho/relay/server/src/billing.rs:298` (variant 4003), `website/src/pages/terms.astro:47`, ADR 0010 | ✓ |
| Free per-request cap $0.05 / 200 tokens | `maho/billing-proxy/src/lib.rs:165` + tests at `lib.rs:1218-1220,1416`, ADR 0009 v2 §5 | ✓ |
| Pro per-request cap $0.20 / 2000 tokens | `maho/billing-proxy/src/lib.rs:165` + tests at `lib.rs:1224-1226,1417`, ADR 0009 v2 §5 | ✓ |
| Max per-request cap $0.50 / 8000 tokens | `maho/billing-proxy/src/lib.rs:165` + tests at `lib.rs:1230-1232,1418`, ADR 0009 v2 §5 | ✓ |
| PAYG per-request cap (matches Max ceiling) | `maho/billing-proxy/src/lib.rs:1419,1439`, ADR 0010 | ✓ |

### Risks specific to v2

- **R-13: Volume dependency.** v2 economics require ~500+ paid users (Pro+Max mix) for OpenRouter spend to clear ops overhead at the new $5/$20 price points. Flagged in ADR 0009 v2; mitigation is the monthly manual health check in runbook §F2 (MRR vs OR spend, churn, free→pro conversion). No automated kill-switch by design (Plan §6 Q-5). Status: MITIGATED.
- **R-14: OpenRouter model price drift.** A unilateral provider price change on any allowlisted model could compress margin below the markup target. Mitigation: `FREE_MODEL_ALLOWLIST` at `billing-proxy/src/lib.rs:278-291` is KV-overridable for hot-update without redeploy, and the per-request token cap (`get_max_tokens_cap`, `lib.rs:165`) bounds the cost of any single regression. Monthly review of OR public price list is part of the §F2 health pass. Status: MITIGATED.

### Cross-doc inconsistency detection (launch gate)

Run before launch to confirm v1 prices and pre-v2 vocabulary are absent from in-scope content:

```bash
# Old subscription prices (v1) must not appear in launch-gated surfaces.
# Build the regex from variables so this audit file itself does not match.
DOLLAR='$'
OLD="${DOLLAR}15/mo|${DOLLAR}99/mo|${DOLLAR}200/mo"
grep -rE "$OLD" website/src/ maho/billing-proxy/src/ maho/relay/server/src/ docs/demos/ docs/operations/manual-test-runbook.md
# Expected: 0 hits

# "partially superseded" should remain only in ADR 0001a's intentional historical record.
grep -rn "partially superseded" website/src/ maho/billing-proxy/src/ maho/relay/server/src/ docs/operations/manual-test-runbook.md docs/demos/
# Expected: 0 hits

# Top-up vocabulary (replaced by "credit pack") should be gone from active surfaces.
grep -rni "top-up\|topup" website/src/pages/pricing.astro website/src/pages/terms.astro docs/operations/manual-test-runbook.md docs/demos/03-tier-upgrade.md
# Expected: 0 hits
```

If all three return 0 hits, pricing v2 consistency is locked. Any hit is a P0 launch blocker.

### Tech debt added by v2 (informational, not blocking)

| Item | Location | Severity |
|------|----------|----------|
| Variant ID map duplicated between subscription branch (`billing.rs:199-200`) and PAYG branch (`billing.rs:294-298`) | `maho/relay/server/src/billing.rs` | Low |
| Legacy variant 3001/3002/3003 still aliased to PAYG amounts at `billing.rs:296-298` for back-compat | `maho/relay/server/src/billing.rs` | Low; clean up after 1 billing cycle |
| Pricing literals still duplicated between `pricing.astro` and `billing-proxy/src/lib.rs` | covered by §5 row "pricing.astro literal drift"; v2 does not make this worse but does not fix it | Medium (pre-existing) |

---

**End of review.**
