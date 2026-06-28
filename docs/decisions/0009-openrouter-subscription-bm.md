# 9. OpenRouter Markup Subscription as Maho's Business Model

## Status

**Accepted**, 2026-06-28 (v2 — supersedes v1 from 2026-06-27). **Amended by ADR 0001a + ADR 0010.**

## Revision History

| Version | Date | Summary |
|---------|------|---------|
| v1 | 2026-06-27 | $0/$20/$200, 30% markup, Aside-parity pricing, frontier inclusion implicit |
| v2 | 2026-06-28 | $0/$5/$20, 50% subscription markup, frontier explicitly excluded from Maho-managed (BYOK only), PAYG credits added as orthogonal mode (see ADR 0010) |

---

Specifically amends the following ADR 0001 v3 decisions:
- "free for everyone" → Maho introduces a Free / Pro / Max consumer tier ladder
- "no payment processor" → LemonSqueezy adopted as Merchant of Record for subscription billing
- Option D ("Tiered Free / Pro / Enterprise, Deferred") was the option held open for exactly this case — see ADR 0001 lines 83-86

**Preserves** the following from ADR 0001 v3 and ADR 0008:
- No Enterprise tier at launch (3-tier consumer model only — Free / Pro / Max)
- No B2B sales motion, no SSO / audit_log / SOC2 / DPA / BAA scaffolding
- E2EE sync relay's "we never see your data" promise is preserved by **physically separating** the LLM proxy from the relay

Next scheduled review: **2027-03-01**, or sooner if any trigger fires (see Decision Drivers).

---

## Context

The v1 pricing ($0/$20/$200) was modeled on Aside's exact tier structure. Post-launch competitive analysis reveals this is mispositioned: ChatGPT Plus at $20/mo and Claude Pro at $20/mo offer frontier models with massive context windows. Maho cannot match frontier model access at $20 without catastrophic per-user losses (a single Opus 200k-context request can burn $3-5). Meanwhile, the $200 Max tier prices out the indie developer and prosumer TAM that Maho's privacy-first positioning naturally attracts.

The pivot to $5/$20 widens addressable market by ~10x (price-sensitive users who balk at $20 for a browser AI but will try $5), explicitly excludes frontier models from Maho-managed tiers (positioning Maho as "fast mid-tier AI bundled in your browser" rather than competing with dedicated coding agents), and adds PAYG credits as an orthogonal billing mode for variable-usage patterns.

---

## Decision Summary

Free $0 / Pro $5 / Max $20 subscriptions + PAYG credits + BYOK. Cheap-to-mid models only in Maho-managed. 50% subscription markup, 30% PAYG markup.

---

## Decision Drivers

- **Maho is already building the agent stack** — Swiftide multi-turn loop, BYOK key management, permission gates, SQLite history are shipped or near-shipped. Skipping monetization while paying for OpenRouter/LemonSqueezy/CF Workers operationally is unsustainable beyond founder funding.
- **Aside parity at lower price** — to compete on the onboarding axis, Maho needs a bundled-AI option that undercuts Aside ($20 vs $20) while targeting a wider TAM at $5.
- **Privacy positioning must be preserved** — the relay's E2EE promise is the strongest trust asset. LLM proxy must be physically separated from the relay.
- **No payment processor scaffolding required** by choosing a Merchant of Record (MoR) — LemonSqueezy handles tax, VAT, and most compliance automatically.
- **Frontier exclusion is a positioning choice** — Maho is a browser with bundled AI, not a coding agent. Mid-tier models serve 90%+ of browser AI use cases (summarization, quick answers, tab management) without the margin-destroying cost profile of frontier models.
- **Reversibility** — if the BM does not produce signal in 6 months, the architecture (CF Worker + LemonSqueezy + tiered schema) can be deprecated without unwinding the relay.

---

## Tier Table v2

| Tier | Monthly Price | OR Budget (after 50% markup) | Per-Request Cost Cap | Per-Request max_tokens Cap | Model Family Access |
|------|--------------|------------------------------|---------------------|---------------------------|-------------------|
| Free | $0 | $0.50 | $0.05 | 200 | free-tier OSS models |
| Pro | $5 | ~$2.10 ($5 - LS fee - infra, remainder / 1.50) | $0.20 | 2,000 | + cheap-fast family (haiku/flash/mini/70B class) |
| Max | $20 | ~$13.30 ($20 - LS fee - infra, remainder / 1.50) | $0.50 | 8,000 | + mid family (sonnet/gemini-pro/gpt-mini-medium class) |
| BYOK | Free | Unlimited | None (user's key) | None | All models (user's provider) |
| PAYG | Credit-based | Consumed at 30% markup | $0.50 | 8,000 | Same as Max tier |

- **50% subscription markup**: user pays $X, Maho keeps margin after LS fee + infra, remainder funds OR usage at 1.50x pass-through.
- **30% PAYG markup**: credits consumed at OR cost × 1.30. Lower markup because one-time LS fee already eats into the purchase.
- **No automatic kill-switch**: If a user exhausts their subscription ceiling, PAYG credits kick in as fallback (if purchased). Otherwise, 402 until next billing cycle. Manual judgment for abuse (Q-5 decision preserved).

---

## Model Family Policy

Maho does NOT specify concrete model slugs in billing logic. Instead, the billing-proxy enforces access via a **family/tier abstraction**:

- **free-tier**: Models with $0 per-token cost on OpenRouter (community/free endpoints)
- **cheap-fast**: Models where typical request cost is $0.001–$0.05 (haiku-class, flash, mini, 70B-class open-source)
- **mid**: Models where typical request cost is $0.05–$0.50 (sonnet-class, gemini-pro, gpt-mini with medium reasoning)
- **frontier** (EXCLUDED from Maho-managed): Models where typical request cost >$0.50 or context-window cost can exceed $1/request (Opus, GPT-5 base, o3/o4 reasoning, gemini-ultra, all `:thinking`/`:reasoning-xhigh` variants)

Concrete model-to-family mapping lives in the billing-proxy's KV namespace `MODEL_ALLOWLIST`. This is reviewed monthly: prices are auto-fetched from the OpenRouter catalog and verified within ±30% of the declared tier band. Models that drift out of band are reclassified or removed.

See **Annex A** for the 2026-06-28 concrete slug snapshot.

---

## Frontier Exclusion Rationale

Frontier models (Claude Opus, GPT-5 base, o3, o4, Gemini 3 Ultra, all reasoning-premium variants) are explicitly excluded from all Maho-managed tiers (Free, Pro, Max, PAYG).

**Economic rationale**: A single Claude Opus request with 200k context window costs $3-5 at OpenRouter pass-through. At the Max tier ($20/mo, ~$13.30 OR budget), one heavy Opus session would consume 25-40% of a user's entire monthly allocation. This creates an impossible UX: users hit quota exhaustion mid-month from a single conversation, generating support load and churn.

**Positioning rationale**: Maho is a privacy-first browser with bundled AI assistance — summarization, quick Q&A, tab organization, form filling. These use cases are served excellently by mid-tier models (Sonnet, Gemini Pro, GPT-5 mini). Frontier models serve coding agents and research workflows that exceed Maho's core value proposition. Users who need frontier models are power users who already have provider accounts — BYOK serves them perfectly.

**UX rationale**: Excluding frontier prevents the "why is my quota gone" frustration loop. Users on $5 Pro get consistent, predictable AI access throughout the month with cheap-fast models. Users on $20 Max get mid-tier quality with comfortable headroom. No surprises.

**BYOK escape valve**: Power users who want Opus/GPT-5/o3 in Maho simply add their own API key. Maho routes via the same proxy with zero-retention but the cost is on the user's provider account. This preserves the privacy story while giving access to every model on the market.

---

## Q-2 Enforcement Addendum v2

Per-request enforcement uses the same three-layer model from v1, updated with v2 values:

### Layer 1: Pre-flight `max_tokens` Injection

Every request routed through the `billing-proxy` has its `max_tokens` parameter capped based on the user's tier and remaining balance:

| Tier | max_tokens cap | Per-request cost cap |
|------|---------------|---------------------|
| Free | 200 | $0.05 |
| Pro | 2,000 | $0.20 |
| Max | 8,000 | $0.50 |
| PAYG | 8,000 | $0.50 |

### Layer 2: Post-completion Cost Tracking

The proxy listens to the final SSE chunk containing token counts, calculates the marked-up cost (50% for subscription, 30% for PAYG), and commits the amount back to the `UserUsage` Durable Object asynchronously.

### Layer 3: Hard-stop Thresholding

If cumulative usage in the `UserUsage` DO exceeds `subscription_ceiling_usd`, the next request checks for PAYG credit balance (see ADR 0010 §5). If credits available, consumption shifts to PAYG mode. If no credits, 402 Payment Required.

### KV `MODEL_ALLOWLIST` Monthly Review

- Prices auto-fetched from OpenRouter catalog API (`/api/v1/models`)
- Each model's median request cost compared against tier band thresholds
- Models drifting >±30% from their declared band are flagged for reclassification
- Review cadence: first Monday of each month
- Reviewer: billing-proxy maintainer (currently founder)

---

## Break-Even Math v2

### Per-User Unit Economics

**Pro $5/mo**:
- Revenue: $5.00
- LemonSqueezy fee (5% + $0.50): $0.75 (blended on $5)
- Infrastructure (CF Worker + relay share): $1.00
- OR budget ceiling: $2.10 (remaining $3.25 / 1.50 markup factor ≈ $2.17, rounded to $2.10)
- **Maho net margin: ~$1.15 per Pro user/month (23%)**

> Corrected math: $5.00 - $0.75 (LS) - $1.00 (infra) = $3.25 available. At 50% markup: OR budget = $3.25 / 1.50 = $2.17. Maho keeps $3.25 - $2.17 = $1.08 net from markup. Total net = $1.08 + (infra already deducted) → net after all costs = $5.00 - $0.75 - $1.00 - $2.17 = **$1.08 (21.6%)**

**Max $20/mo**:
- Revenue: $20.00
- LemonSqueezy fee: $1.50
- Infrastructure: $1.00
- OR budget ceiling: $11.67 ($17.50 / 1.50)
- **Maho net: $20 - $1.50 - $1.00 - $11.67 = $5.83 (29.2%)**

**PAYG $100 pack (one-time)**:
- Revenue: $100.00
- LemonSqueezy fee: $5.50
- Infrastructure: $1.00
- OR cost at 30% markup: $94.50 / 1.30 = $72.69 max OR spend
- **Maho net: $100 - $5.50 - $1.00 - $72.69 = $20.81 (20.8%)**

### Break-Even Target at $1,000/mo Operating Cost

| Segment | Count | Monthly Revenue | Maho Net |
|---------|-------|----------------|----------|
| Free | ~3,000 | $0 | $0 (cost: ~$150 in free-tier OR) |
| Pro | ~500 | $2,500 | ~$540 |
| Max | ~100 | $2,000 | ~$583 |
| PAYG | ~50 purchases | ~$1,500 | ~$312 |
| **Total** | | **~$6,000** | **~$1,285** net (covers $1,000 op + buffer) |

**Note**: Per-user margin is lower than v1 ($1.08/$5.83 vs $2.10/$46.20) but TAM is ~10x wider at the $5/$20 price band. The model bets on volume over per-user extraction.

---

## Risk Table v2

| ID | Risk | Likelihood | Impact | Status | Mitigation |
|----|------|-----------|--------|--------|------------|
| R-1 | Auth cache TTL stale token allows post-cancellation usage | Medium | Low | MITIGATED | TTL ≤5s or no cache in v1 |
| R-2 | CF Worker cold start latency on first request | Low | Low | MITIGATED | Workers have near-zero cold start; Rust WASM is fast |
| R-3 | LemonSqueezy acquisition by Stripe → discontinuation | Low | High | PARTIAL | Direct integration (no abstraction layer) favors speed; migration plan documented |
| R-4 | OpenRouter outage blocks all Maho-managed users | Medium | High | PARTIAL | BYOK fallback exists; users can add own key anytime |
| R-5 | Free tier abuse (bot signups) | High | Medium | MITIGATED | Email verification + $0.05 per-request cap + 200 max_tokens |
| R-6 | Single model price spike (OR repricing) | Medium | Medium | MITIGATED | Monthly KV allowlist review; auto-alert on >30% drift |
| R-7 | Streaming SSE cost calculation inaccuracy | Medium | Low | MITIGATED | Post-completion reconciliation; bounded overshoot tolerance |
| R-8 | Privacy perception damage from LLM proxy | Medium | High | MITIGATED | Physical separation from relay; zero-retention policy; BYOK always available |
| R-9 | Durable Object consistency under high concurrency | Low | Medium | MITIGATED | DO single-writer model; alarm-based reset |
| R-10 | LemonSqueezy webhook delivery failure | Medium | Medium | MITIGATED | Idempotent processing; `processed_event_ids` table; retry reconciliation |
| R-11 | User confusion between subscription/BYOK/PAYG modes | Medium | Medium | PARTIAL | Clear pricing page; CLI `maho balance` command; in-app mode indicator |
| R-12 | Markup too high at 50% for price-sensitive users | Medium | Medium | PARTIAL | 50% markup is invisible to user (they see flat $5/$20); competitive with ChatGPT Plus value |
| R-13 | Volume dependency — need 500+ paid users for op break-even | High | High | PARTIAL | Lower entry price ($5) → wider TAM; marketing emphasizes Aside-parity at 1/4 price; PAYG adds non-subscription revenue |
| R-14 | OpenRouter price changes erode margin | Medium | High | MITIGATED | Monthly KV allowlist review; auto-fetch OR catalog prices; alert if model band drift >30%; can reclassify models across tiers |

---

## Monthly Health Check

Targets updated for v2 pricing (Q-5 no-auto-kill-switch preserved):

| Metric | Month 3 Target | Month 6 Target | Kill-Switch Trigger (Manual) |
|--------|---------------|---------------|------------------------------|
| Pro active subscriptions | ≥ 200 | ≥ 1,000 | — |
| Max active subscriptions | ≥ 20 | ≥ 100 | — |
| PAYG monthly purchase count | ≥ 20 | ≥ 100 | — |
| Pro → Max conversion | — | > 5% within 60 days | — |
| Free → Paid conversion | > 2% within 30 days | > 3% within 30 days | — |
| Churn (paid users) | < 12% monthly | < 10% monthly | — |
| 90-day rolling op cost vs revenue | — | — | If op cost > revenue for 90 days → raise prices or remove free-tier |

**No automatic kill-switch** (Q-5 preserved). The 90-day rolling trigger is a manual decision point requiring founder review, not an automated action.

---

## Related ADRs

- **ADR 0001** (v3): amended via ADR 0001a (which scopes the "free for everyone" and "no payment processor" portions). Further amended by this ADR v2 (pricing values) and ADR 0010 (PAYG mode).
- **ADR 0008** (no Enterprise tier): **preserved** — this ADR introduces consumer tiers only.
- **ADR 0010** (PAYG credits model): companion ADR defining credit lifecycle, variant mapping, consumption flow, and storage spec. See `docs/decisions/0010-payg-credits-model.md`.
- **ADR 0002** (on-device LLM library): unaffected.
- **ADR 0003** (realtime voice provider): unaffected.
- **ADR 0004** (open-source maho-core): unaffected.
- **ADR 0005** (MCP curation policy): unaffected.
- **ADR 0006** (i18n architecture): related — tier names and pricing page need locale handling.

---

## Addendum: Three-Layer Quota Enforcement (Q-E/Q-2) — Updated for v2

Because pre-flight precise-USD enforcement is physically impossible due to non-deterministic LLM tokens and streaming latency, Maho implements a robust **three-layer enforcement model**:

1. **Layer 1: Pre-flight `max_tokens` Injection**:
   Every request routed through the `billing-proxy` has its `max_tokens` parameter capped based on the user's tier and remaining balance:
   - Free Tier: capped to `200` tokens per request.
   - Pro Tier: capped to `2,000` tokens per request.
   - Max Tier: capped to `8,000` tokens per request.
   - PAYG (standalone or overflow): capped to `8,000` tokens per request.

2. **Layer 2: Post-completion Cost Tracking**:
   The proxy listens to the final SSE chunk containing token counts (or counts them manually in the streaming consumer), calculates the marked-up cost (50% for subscription consumption, 30% for PAYG consumption), and commits the amount back to the `UserUsage` Durable Object asynchronously.

3. **Layer 3: Hard-stop Thresholding + PAYG Fallback**:
   If the cumulative usage in the `UserUsage` DO exceeds `subscription_ceiling_usd`, the proxy checks PAYG credit balance. If credits > 0, request proceeds under PAYG billing. If credits = 0, 402 Payment Required with link to purchase credits or wait for next billing cycle.

### KV `MODEL_ALLOWLIST` Review Process (Monthly)

- Auto-fetch model catalog from OpenRouter API
- Compare each model's median per-request cost against tier band thresholds:
  - free-tier: $0/token (community endpoints)
  - cheap-fast: $0.001–$0.05 per typical request
  - mid: $0.05–$0.50 per typical request
  - frontier (excluded): >$0.50 per typical request
- Flag models drifting >±30% from declared band
- Reclassify or remove flagged models
- Update KV `MODEL_ALLOWLIST` with new mapping
- Log change in `docs/operations/model-allowlist-changelog.md`

---

## Annex A: Concrete Model Slugs as of 2026-06-28

> **Subject to monthly review via billing-proxy KV `MODEL_ALLOWLIST`.** These are the launch-day slugs; they will drift as OpenRouter adds/removes/reprices models.

### Free Tier (available to all users)

| Slug | Notes |
|------|-------|
| `google/gemini-3-flash-lite:free` | Google free-tier endpoint |
| `meta-llama/llama-4-scout-17b-16e:free` | Meta free-tier MoE |
| `mistralai/mistral-small-3.2-24b:free` | Mistral free-tier |
| `qwen/qwen-3-32b:free` | Alibaba free-tier |

### Pro $5 Tier (adds to Free)

| Slug | Family | Notes |
|------|--------|-------|
| `google/gemini-3.5-flash` | cheap-fast | Google's fast mid-tier |
| `anthropic/claude-haiku-4.5` | cheap-fast | Anthropic's fast model |
| `openai/gpt-5-nano` | cheap-fast | OpenAI's smallest |
| `openai/gpt-5-mini` | cheap-fast | Low reasoning mode |
| `meta-llama/llama-4-maverick` | cheap-fast | Meta's flagship open |
| `meta-llama/llama-4-70b-instruct` | cheap-fast | Meta 70B class |
| `deepseek/deepseek-v3.5` | cheap-fast | DeepSeek flagship |
| `z-ai/glm-4.7-flash` | cheap-fast | Zhipu fast model |

### Max $20 Tier (adds to Pro)

| Slug | Family | Notes |
|------|--------|-------|
| `google/gemini-3.5-pro` | mid | Google's pro tier |
| `anthropic/claude-sonnet-4.7` | mid | Anthropic's mid-tier |
| `openai/gpt-5-mini` | mid | Medium reasoning mode |
| `qwen/qwen-3-72b-instruct` | mid | Alibaba 72B |
| `mistralai/mistral-large-3` | mid | Mistral's large model |
| `x-ai/grok-4-mini` | mid | xAI's mid-tier |

### BYOK Only (Excluded from ALL Maho-Managed Tiers)

| Slug Pattern | Reason |
|-------------|--------|
| `anthropic/claude-opus-*` | Frontier — $3-5/heavy request |
| `openai/gpt-5` (base) | Frontier — high per-token cost |
| `openai/o3*` | Reasoning — unpredictable cost |
| `openai/o4*` | Reasoning — unpredictable cost |
| `google/gemini-3-ultra` | Frontier — highest Google tier |
| `*:thinking` / `*:reasoning-xhigh` | Reasoning premium — unbounded cost |

---

## Triggers for Re-Review

This ADR should be reopened earlier than 2027-03-01 if any of the following fires:

1. **Operating margin sustained negative over 60 days** — current pricing does not cover OpenRouter pass-through cost.
2. **LemonSqueezy discontinuation announcement** — Stripe consolidation forces a provider migration.
3. **Aside or comparable competitor cuts price by >40%** — pricing rebalance required.
4. **Free tier abuse exceeds 5% of total traffic** — quota or auth model needs revision.
5. **A B2B / Team customer signal arrives** — Enterprise tier discussion (ADR 0008 supersede) needs to start.
6. **PAYG revenue exceeds 40% of total** — may indicate subscription pricing is wrong; consider tier rebalancing.
