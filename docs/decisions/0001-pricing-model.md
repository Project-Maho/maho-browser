# 1. Pricing Model for Maho Browser and Maho Mail

## Status

Accepted (revised v3), 2026-06-17. **Amended by ADR 0001a (2026-06-27).** Supersedes:
- v1 (Sync $5/mo + Stripe + Enterprise contact)
- v2 (BYOK-only individual + Enterprise contact, no processor)

Now: **free for everyone, no Enterprise tier, no payment processor (amended by ADR 0001a and ADR 0009).** See
also `docs/decisions/0008-no-enterprise-tier.md` for the Enterprise-removal
rationale.

Next scheduled review: **2027-01-15**, or sooner if any trigger fires.

## Context

This ADR has gone through three revisions on the same day:

- **v1** proposed Free + Sync $5/mo + Enterprise contact. Rejected because it
  forced a payment processor (Stripe / Paddle) onboarding for a tier whose
  cost basis is tiny and self-hostable.
- **v2** proposed Free for individuals + Enterprise contact. Rejected because
  the Enterprise tier was added without a concrete demand signal; it created
  an SSO / audit-log / SOC2 / DPA / BAA scaffolding burden ahead of any real
  customer.
- **v3 (this version)** drops Enterprise as well. Maho is free for everyone.
  No paid tier of any kind exists at launch.

The reasoning in one paragraph: per-user cost is small (BYOK eliminates AI
cost; the E2EE relay is cheap and self-hostable). Without per-user variable
cost, there is no individual paywall to justify. Without a B2B sales motion
or a signed pilot customer, there is no Enterprise tier to justify either.
Both paid layers were carried as defaults from the original plan and were
never actually validated.

## Decision Drivers

- AI cost is zero (BYOK only).
- Sync relay cost is small and self-hostable.
- No B2B sales motion exists yet; building Enterprise scaffolding ahead of
  one is premature.
- Operating budget is small enough that founder funding / sponsorships /
  search-default partnerships / future Pro tier can each carry it
  independently.
- Avoid premature commitment to any payment processor, sales hire, or
  compliance pipeline until a concrete signal forces the question.

## Considered Options

### Option A. Free for everyone, no paid tier (Chosen, v3)

- Maho is free for individuals and for organizations alike.
- No subscription, no payment processor, no Enterprise contact form, no
  Pro tier.
- Funding for ongoing operating cost comes from one or more of: founder,
  sponsorships, search default partnerships, grants, or a later opt-in tier
  introduced through a fresh ADR.
- Existing P2-6 Enterprise scaffolding (SSO, audit_log, Docker compose,
  DPA / BAA templates) is removed; see ADR 0008.

**Pros**
- Single message: "Maho is free." No tier ladder, no contact form, no card.
- Zero billing operations and zero compliance pipeline at launch.
- Trust narrative is internally consistent and the strongest it can be.

**Cons**
- No revenue at launch. Sustainability depends on founder cash and / or
  sponsorships and / or a future opt-in Pro tier.
- If a B2B customer arrives organically, we have to build Enterprise ops
  reactively rather than ahead of time.

### Option B. Free for individuals + Enterprise contact (v2, Rejected)

Rejected in v3. The Enterprise tier was carried over from the original plan
without a concrete customer signal. Building scaffolding (SSO / audit_log /
SOC2 prep / DPA / BAA) without a pilot is premature optimization.

### Option C. Free + paid Sync $5/mo (v1, Rejected)

Rejected in v2. Forces a payment processor onboarding to recover a cost that
is small and self-hostable; undercuts the trust narrative.

### Option D. Tiered Free / Pro / Enterprise (Deferred)

Possibly later. Out of scope for launch. Any new tier requires a fresh ADR
that explicitly addresses what changed in product or demand to justify it.

## Decision Outcome

**Chosen: Option A** — Maho is free for everyone, with no paid tier of any
kind at launch.

Rationale:

1. There is no individual variable cost (AI is BYOK; relay is E2EE and
   small).
2. There is no validated Enterprise demand. Building Enterprise ahead of
   one is premature.
3. Single-tier pricing makes the marketing message and the operations both
   trivially simple.
4. If sustainability ever requires revenue, that question gets a fresh ADR
   tied to actual numbers, not to plan defaults.

### Positive Consequences

- One sentence answer to "how much": "Free."
- No payment processor, no SSO, no audit log, no DPA / BAA legal review,
  no SOC2 pipeline at launch.
- Reduces external dependencies in the launch checklist to the bare
  minimum (TEAM_ID, Apple Developer Program, ed25519 release key, and
  community-driven items).
- The pricing page is one paragraph and one CTA.

### Negative Consequences

- No revenue at launch. Founder absorbs running costs, or the project
  relies on sponsorships / search-default partnerships / later opt-in
  Pro tier.
- A future paid tier will require a real ADR + processor / billing
  migration. We accept that delay rather than carry premature scaffolding.

## Review

Re-open on **2027-01-15**, or earlier if any of the following happen:

- Sync relay hosting cost grows materially (more than a small fraction of
  total operating budget), forcing a real funding question.
- A serious B2B customer arrives with a written ask (SSO, audit log,
  dedicated infrastructure) that justifies reopening the Enterprise tier.
- Maho cumulative individual user count creates real infrastructure
  pressure (e.g. global edge presence) that needs revenue.
- Major product change introduces a real per-user variable cost outside
  BYOK (e.g. server-side inference for free users — explicitly out of
  scope today).

## References

- `docs/competitive-analysis/genspark-vs-maho.md` §12.3, "Pricing Model Options"
- `docs/plans/wave-a-launch-blockers.md` §P0-3, "가격 모델 결정"
- `docs/decisions/0008-no-enterprise-tier.md`, Enterprise-removal rationale
- `docs/security/no-billing-policy.md`, the no-billing posture
- `website/src/pages/pricing.astro`, single-tier free message

---

*Revised 2026-06-17 (v3) — supersedes v1 (Stripe / Sync) and v2 (Enterprise contact).*
