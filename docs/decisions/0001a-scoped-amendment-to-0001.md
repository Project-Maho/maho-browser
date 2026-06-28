# 1a. Scoped Amendment to ADR 0001

## Status

**Accepted**, 2026-06-27. **Amends ADR 0001 v3.**

---

## Context

ADR 0001 v3 established a "free for everyone, no payment processor" model for Maho. With the adoption of ADR 0009, Maho introduces a paid consumer subscription tier (Pro & Max) backed by LemonSqueezy to compete with bundled AI models. This amendment explicitly scopes the changes to ADR 0001 v3.

---

## Amended Sections of ADR 0001 v3

1. **Section "Status"**:
   - Amended to note that the status is amended by ADR 0001a and ADR 0009.
2. **Section "Decision"**:
   - The decision "free for everyone" is amended to allow paid consumer tiers (Pro at $5/mo, Max at $20/mo) alongside the Free tier. See ADR 0009 v2 for v2 tier pricing.
   - The decision "no payment processor" is amended to register LemonSqueezy as the Merchant of Record for subscription collection and top-ups.
3. **Section "BYOK Rationale"**:
   - The promise of free, unlimited Bring-Your-Own-Key (BYOK) usage is fully preserved. The subscription tiers are strictly optional convenience layers with managed inference.

---

## Consequences

- Direct billing integrations are isolated to the payment processor helper endpoints on the relay and the `billing-proxy`.
- Sync relay's E2EE structure remains un-inspected and unaffected.

---

## Further Amendments

- Amended further by ADR 0009 v2 (2026-06-28) and ADR 0010 (2026-06-28).
- Net effect of v2 cascade: $0/$5/$20 subscription tiers (was $0/$20/$200) + PAYG credits + BYOK + frontier model exclusion from Maho-managed.
- Tier table moved to ADR 0009 v2 §5 (Annex A for concrete 2026-06-28 model slugs).
