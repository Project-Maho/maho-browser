# 10. PAYG Credits Model

## Status

**Accepted**, 2026-06-28. Companion to ADR 0009 v2.

---

## Context

Not all Maho users fit a monthly subscription pattern. Power users who use AI sporadically (e.g., heavy research weeks followed by idle weeks) overpay on monthly plans, while casual users who occasionally exceed their Pro ceiling need a seamless overflow mechanism without upgrading tiers permanently.

PAYG (Pay-As-You-Go) credits address this gap: prepaid credit packs that never expire, consumed at a 30% markup over OpenRouter pass-through cost. This mirrors the prepaid-credit models of ChatGPT API, Anthropic API, and other developer platforms — proven to reduce churn from variable-usage users.

Critical distinction: **PAYG is NOT BYOK.** In PAYG mode, Maho still routes requests via its managed proxy using the Maho master OpenRouter key. The user pays Maho for credits; Maho pays OpenRouter. In BYOK mode, the user provides their own provider key and Maho routes with zero-retention pass-through at no cost to the user.

---

## Decision Summary

Credit packs sold via LemonSqueezy (variants 4001/4002/4003) at $10/$50/$100. Credits never expire, are non-refundable past 14-day cooling-off window, stack with subscription as overflow fallback OR function standalone. Same model allowlist as Max tier (mid-family + below). Same per-request caps as Max ($0.50 cost cap, max_tokens=8000). 30% markup applied at consumption time, not at purchase time — purchase grants face-value credit balance.

---

## Variant Mapping

| LS Variant ID | Pack Size (User Pays) | Face Value Credit | LS Fee (5% + $0.50) | Maho Net After LS | Notes |
|---------------|----------------------|-------------------|---------------------|-------------------|-------|
| 4001 | $10.00 | $10.00 | $1.00 | $9.00 | Starter pack |
| 4002 | $50.00 | $50.00 | $3.00 | $47.00 | "Best value" tag in pricing UI |
| 4003 | $100.00 | $100.00 | $5.50 | $94.50 | Power-user pack |

**Markup application**: The 30% markup is applied at consumption time, NOT at purchase. When a user buys a $10 pack, their balance shows $10.00. When they make a request that costs $0.10 at OpenRouter pass-through, their balance is deducted by $0.10 × 1.30 = $0.13. This means $10 of credit buys ~$7.69 of OpenRouter usage. The UX simplicity ("pay $10, see $10 balance") outweighs the slight mental-model complexity of consumption rates.

---

## Consumption Flow

The following sequence describes how a request is billed when PAYG credits are involved:

1. **User makes chat request** via Maho-managed proxy (billing-proxy CF Worker).

2. **Proxy authenticates** via `/auth/introspect` on the relay — confirms user identity and retrieves tier + subscription status.

3. **Proxy checks subscription ceiling**: If user has an active subscription (Free/Pro/Max) AND remaining budget within current billing period > estimated request cost → **consume from subscription budget** (50% markup applied via Layer 2 post-completion tracking). Flow ends normally.

4. **Subscription exhausted or no subscription**: Proxy calls `check_credit_fallback` — queries the relay's `/billing/credits_balance` endpoint for the user's current PAYG credit balance.
   - If `balance_usd` > estimated minimum request cost ($0.01) → **proceed under PAYG billing**. Reserve estimated cost from balance (see Atomicity §9). Route to OpenRouter. On completion, reconcile actual cost × 1.30 against reserved amount. Debit actual from balance, release excess reservation.
   - If `balance_usd` ≤ $0.01 (effectively zero) → **return 402 Payment Required** with response body containing:
     - `error`: "insufficient_credits"
     - `buy_url`: LemonSqueezy checkout link for credit packs
     - `balance_usd`: current balance (for client display)

5. **Client-side handling**: Maho browser UI displays a non-blocking notification: "Credits exhausted. [Buy more credits] or [Add your own API key (BYOK)]" with deep links to pricing page and BYOK settings respectively.

---

## Storage Spec

New table in the relay database (SQLite, managed by sea-orm migrations):

```sql
CREATE TABLE credits (
    user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    balance_usd REAL NOT NULL DEFAULT 0.0,
    lifetime_purchased_usd REAL NOT NULL DEFAULT 0.0,
    last_purchase_at INTEGER,  -- unix timestamp (seconds)
    last_consumption_at INTEGER  -- unix timestamp (seconds)
);
```

**Migration file**: `maho/relay/migration/src/m20260628_000006_add_payg_credits.rs`

**Entity file**: `maho/relay/entity/src/credits.rs`

**Notes**:
- `balance_usd` stores face-value balance (what user sees), not OR-equivalent balance.
- `lifetime_purchased_usd` is monotonically increasing; used for analytics (LTV calculation, upgrade candidate detection).
- `last_purchase_at` and `last_consumption_at` are nullable (NULL = never purchased / never consumed).
- No `expires_at` column — credits never expire by design.

---

## Webhook Handling

When LemonSqueezy fires an `order_created` event with a variant ID matching 4001, 4002, or 4003:

1. **Idempotency check**: Look up `order_id` in `processed_event_ids` table. If exists, return 200 OK (already processed).

2. **User lookup**: Extract `custom_data.user_id` from the webhook payload (set during checkout via LemonSqueezy custom checkout fields).

3. **Credit increment**: 
   ```
   credits.balance_usd += face_value  (NOT LS net — Maho absorbs LS fee)
   credits.lifetime_purchased_usd += face_value
   credits.last_purchase_at = now()
   ```
   - $10 pack → +$10.00 to balance
   - $50 pack → +$50.00 to balance  
   - $100 pack → +$100.00 to balance

4. **Record event**: Insert `order_id` into `processed_event_ids` with timestamp.

5. **Notify billing-proxy**: POST to `/billing/credits_update` with `{ user_id, new_balance_usd }` so the proxy's cache (if any) is invalidated.

6. **User notification**: Trigger in-app notification (via relay WebSocket) confirming credit purchase: "Added $X to your Maho AI credits. Current balance: $Y."

**Why face value, not LS net?** Simplicity. The user pays $10 and sees $10 in their balance. Maho eats the LS fee (~$1 on $10) as cost of acquisition. This matches every prepaid-credit product users have encountered (App Store, Steam Wallet, etc.).

---

## Refund Policy

- **14-day cooling-off window** after each individual pack purchase (not from account creation).
- Refund amount = MIN(pack_face_value, current_unconsumed_balance) at refund-request time.
  - Example: User buys $100 pack, uses $30 worth of credits in 10 days, requests refund → refund = $70 (unconsumed balance).
  - Example: User buys $100 pack, uses $95 in 13 days, requests refund → refund = $5 (unconsumed balance).
- **After 14 days**: non-refundable. Credits never expire, so "I forgot I had them" is not a valid refund reason.
- **Refund execution**: LemonSqueezy refund API → deduct refunded amount from `balance_usd` → relay confirms via webhook `order_refunded`.
- **Admin trail**: All refund requests and decisions logged in `admin_audit_log` with reason, timestamp, and acting admin.

---

## Atomicity

PAYG credit consumption uses the same `reserve_before_call` pattern as subscription billing (ADR 0009 v2, Q-2 enforcement Layer 3):

1. **Reserve**: Before forwarding to OpenRouter, estimate maximum possible cost (model's max per-token price × max_tokens cap × 1.30 markup). Deduct estimated amount from `balance_usd` atomically.

2. **Execute**: Forward request to OpenRouter. Stream response.

3. **Reconcile**: On stream completion, calculate actual cost from token counts × model price × 1.30. Compare to reserved amount.
   - If actual < reserved → credit back the difference to `balance_usd`.
   - If actual > reserved (shouldn't happen with proper max_tokens capping, but edge case) → debit the additional amount. If balance goes negative, log as incident (bounded by max_tokens cap, max overshoot ~$0.10).

4. **Failure handling**: If OpenRouter returns an error (5xx, timeout), release full reservation back to `balance_usd`. User is not charged for failed requests.

**Insufficient balance detection**: If estimated reservation exceeds `balance_usd`, return 402 immediately (deterministic, no partial execution). This is checked BEFORE the OpenRouter call, ensuring users never see a request start and then fail mid-stream due to credit exhaustion.

---

## Telemetry

Track the following metrics per user, per month:

| Metric | Purpose |
|--------|---------|
| `subscription_consumption_usd` | How much of their subscription ceiling they use |
| `payg_consumption_usd` | How much PAYG credit they burn |
| `payg_purchase_count` | Number of pack purchases this month |
| `subscription_overflow_events` | Times subscription ceiling was hit and PAYG kicked in |

**Upgrade signals**:
- High `payg_consumption_usd` with no subscription → candidate for Max subscription pitch ("Subscribe to Max for $20/mo and save vs PAYG")
- High `subscription_overflow_events` on Pro → candidate for Max upgrade pitch ("You've exceeded Pro ceiling X times this month")
- High `subscription_overflow_events` on Max → no higher tier exists; surface BYOK pitch ("Add your own API key for unlimited usage")

---

## Risk Register (PAYG-Specific)

| ID | Risk | Likelihood | Impact | Status | Mitigation |
|----|------|-----------|--------|--------|------------|
| R-15 | Refund attack: buy $100, burn $80 in 14 days, request refund for full amount | Medium | Low | MITIGATED | Refund capped at unconsumed balance; worst case Maho is still positive (~$13 net from the $80 consumed portion per 30% markup math) |
| R-16 | Credit balance UI confusion: users don't understand consumption rate vs face value | Medium | Medium | PARTIAL | Pricing page explains "30% platform fee on usage"; CLI `maho balance` shows both face-value balance AND estimated-requests-remaining; in-app tooltip on balance |
| R-17 | Pack-size price anchoring: wrong pack sizes lead to suboptimal conversion | Low | Low | PARTIAL | A/B test pack sizes post-launch ($5/$25/$50 vs $10/$50/$100); current sizes mirror common prepaid patterns |
| R-18 | Credit hoarding: users buy large packs and never use them (dead balance) | Low | Positive | N/A | This is revenue-positive for Maho (cash collected, no OR cost incurred). No mitigation needed — it's a feature, not a bug |
| R-19 | PAYG cannibalizes subscription revenue | Medium | Medium | PARTIAL | PAYG markup (30%) is LOWER margin than subscription (50%); subscription is strictly better value for consistent users; pricing page calculator shows break-even point |

---

## Implementation References

| Component | Path | Notes |
|-----------|------|-------|
| Migration | `maho/relay/migration/src/m20260628_000006_add_payg_credits.rs` | Creates `credits` table |
| Entity | `maho/relay/entity/src/credits.rs` | sea-orm entity for credits |
| Billing handler | `maho/relay/src/billing.rs` → `handle_credit_purchase()` | Webhook → balance increment |
| Balance query | `maho/relay/src/billing.rs` → `get_credit_balance()` | Used by billing-proxy via `/billing/credits_balance` |
| Proxy fallback | `billing-proxy/src/authenticate_request.rs` → `check_credit_fallback()` | Subscription exhausted → PAYG check |
| Consumption | `billing-proxy/src/consume.rs` → `reserve_and_consume_credit()` | Atomic reserve-execute-reconcile |
| CLI command | `maho/crates/maho-core/src/commands/balance.rs` | `maho balance` shows credit + subscription status |
| CLI command | `maho/crates/maho-core/src/commands/buy_credits.rs` | `maho buy-credits` opens LS checkout |

---

## Related ADRs

- **ADR 0009 v2** (parent): defines the subscription tiers, model family policy, and markup structure that PAYG operates within. See `docs/decisions/0009-openrouter-subscription-bm.md`.
- **ADR 0001a** (umbrella amendment): scoped amendment to ADR 0001 v3, further amended by ADR 0009 v2 and this ADR. See `docs/decisions/0001a-scoped-amendment-to-0001.md`.
- **ADR 0008** (no Enterprise): preserved — PAYG is a consumer billing mode, not an enterprise feature.
