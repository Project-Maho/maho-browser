# No-Billing Policy

Date: 2026-06-17 (revised)
Replaces: `docs/security/billing-transparency.md` (retired)
Driver: `docs/decisions/0001-pricing-model.md` (v3) and
`docs/decisions/0008-no-enterprise-tier.md`

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
- Sync is end-to-end encrypted. The relay sees only encrypted blobs. You can
  self-host the relay per the open-source plan
  (`docs/decisions/0004-open-source-maho-core.md`).
- Account creation, sign-in, and sync continue to require an email address
  and a password / passphrase. Those are stored in the relay auth subsystem
  and are independent of any billing concept.

## If we ever introduce billing

The first such change will go through a published ADR amending or
superseding `docs/decisions/0001-pricing-model.md`. Existing free usage
will not be migrated to a paid tier without explicit consent. There will be
no backdoor activation of a payment processor.
