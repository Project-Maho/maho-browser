# No-Billing Policy

> **SUPERSEDED / REVERSED (2026-07).** This policy no longer reflects how Maho
> works. It is reversed by `docs/decisions/0009-openrouter-subscription-bm.md`
> (ADR 0009 v2) and `docs/decisions/0010-payg-credits-model.md` (ADR 0010).
> Maho now offers **optional managed AI billing**: a Free tier ($0), Pro ($5),
> and Max ($20), plus pay-as-you-go credit packs ($10 / $50 / $100). BYOK
> stays free and unlimited, and frontier models remain BYOK-only. The claim
> below that "Maho does not bill anyone" is **no longer true**. Keep this file
> for historical context only. Do not cite it as current policy; see the two
> ADRs above for the canonical billing model.

Date: 2026-06-17 (revised)
Status: Superseded / Reversed (2026-07) by ADR 0009 v2 and ADR 0010
Replaces: `docs/security/billing-transparency.md` (retired)
Driver: `docs/decisions/0001-pricing-model.md` (v3) and
`docs/decisions/0008-no-enterprise-tier.md`

---

**The sections below are historical.** They described Maho's stance before
managed AI billing existed. They must not be cited as current policy.

## Position

Maho does not bill anyone.

There is no subscription, no Enterprise tier, no payment processor. We do not
collect your card number, billing address, or any payment-related data,
because we do not need any.

## What we do not collect

- Credit card numbers (none)
- Billing addresses (none)
- Payment-method tokens (none)
- Stripe / Paddle / Lemon Squeezy / etc. customer IDs (we do not use a
  payment processor)
- Refund / dispute history (no charges, no disputes)
- Recurring billing schedule (nothing recurs)
- Enterprise contract metadata (no Enterprise tier)

## What stays the same as the rest of the product

- AI is BYOK: prompts go to the provider whose key you configured. Maho is
  not in the data path beyond launching the OpenCode subprocess. See
  `docs/onboarding/local-ai-with-opencode.md`.
- Sync is end-to-end encrypted. The relay sees only encrypted blobs. The
  relay is self-hostable, so you can run your own instance and verify its
  behavior locally. Trust here rests on architecture (zero-retention,
  end-to-end encryption, self-hostable relay), not on a public source
  license: Maho is proprietary / closed-source.
- Account creation, sign-in, and sync continue to require an email address
  and a password / passphrase. Those are stored in the relay auth subsystem
  and are independent of any billing concept.

## If we ever introduce billing

The first such change will go through a published ADR amending or
superseding `docs/decisions/0001-pricing-model.md`. Existing free usage
will not be migrated to a paid tier without explicit consent. There will be
no backdoor activation of a payment processor.
