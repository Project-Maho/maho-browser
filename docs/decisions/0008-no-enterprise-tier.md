# 0008. No Enterprise Tier at Launch

Date: 2026-06-17
Status: Accepted
Related: `docs/decisions/0001-pricing-model.md` (v3 reflects this decision)

## Context and Problem Statement

The original wave plan (`docs/plans/wave-d-enterprise-mature.md`) carried an
Enterprise tier (P2-6) as a launch-relevant work item: SSO (SAML / OIDC /
SCIM), audit log infrastructure, on-prem Docker compose, DPA / BAA templates,
SOC2 audit pipeline, dedicated relay deployments, support SLAs.

On review the Enterprise tier was rejected because it was inherited from the
plan template, not derived from a customer or revenue signal. Specifically:

1. There is no signed pilot customer asking for SSO, audit log, or on-prem
   today.
2. The Enterprise scaffolding requires a real legal investment (DPA / BAA
   review), a sales motion (lead handling, demo, contract negotiation), and
   eventually a compliance pipeline (SOC2 Type I / II). None of those costs
   are justified ahead of a concrete customer.
3. The previous ADR (0001 v2) used "Enterprise carries the revenue" as a
   sustainability argument while shipping zero Enterprise machinery and zero
   sales process. That argument is fictional until at least one Enterprise
   contract closes.
4. Maho's current operating cost is small enough to run without Enterprise
   revenue. The trust-by-architecture positioning is also stronger when the
   only product story is "free, BYOK, open source, self-hostable" with no
   B2B price wall on the side.

## Decision Drivers

- No validated B2B demand at launch.
- Enterprise tier creates compliance / legal / sales overhead that scales
  with seriousness, not with intent.
- Removing the tier simplifies the marketing message to one sentence.
- Existing relay code (auth, sync, updates) already supports the only thing
  individuals actually need; SSO / audit log / on-prem are extra work
  layered on top.
- A future Enterprise tier can be reintroduced via a fresh ADR the moment a
  signed customer creates the demand.

## Considered Options

### A. Drop Enterprise entirely (Chosen)

- Delete the P2-6 scaffolding (relay/sso/, audit_log.rs, audit_log entity,
  audit_log migration, enterprise integration tests, Docker compose,
  enterprise-deployment.md, DPA / BAA templates).
- Remove Enterprise from the pricing page.
- Update ADR 0001 to "free for everyone."
- Mark P2-6 as DROPPED in the wave doc with this ADR as the rationale.

### B. Keep scaffolding but hide from marketing (Rejected)

- Considered briefly. Rejected because dead-ish code accumulates without
  a maintainer, the SeaORM migration in particular is a real DB schema
  change that has to be tested even if unused, and the DPA / BAA templates
  invite "did legal review them?" questions whose honest answer is no.

### C. Keep Enterprise in the plan as currently scaffolded (Rejected, prior state)

- Rejected per the reasoning above.

### D. Reintroduce Enterprise later (Deferred)

- Always available. A future ADR can resurrect P2-6 once a real customer
  signs.

## Decision Outcome

Adopt **Option A** — drop Enterprise.

### Consequences

Positive:

- Zero compliance / legal / sales pipeline at launch.
- Zero new SeaORM tables and zero new migrations to maintain.
- The marketing message collapses to one sentence: "Maho is free."
- Reduces dependency on `Sales/Legal infra = NOT_READY`; that external
  blocker is no longer relevant.

Negative:

- No B2B revenue path at launch. A future Enterprise customer will need to
  wait for the scaffolding to be rebuilt under a fresh ADR (estimated 1-2
  weeks of work).
- We accept the cost of "build it later when we actually have a buyer"
  versus "carry it speculatively now."

## Review Triggers

- A serious B2B customer arrives with a written ask for SSO, audit log,
  dedicated relay, on-prem deployment, DPA, BAA, or contractual SLAs.
- Sync relay hosting cost grows enough that B2B revenue starts looking
  necessary, not optional.
- Maho team grows past the point where founder-led B2B handling is the
  bottleneck.

## More Information

- Removed code:
  - `maho/relay/server/src/sso/` (mod, saml, oidc, scim)
  - `maho/relay/server/src/audit_log.rs`
  - `maho/relay/entity/src/audit_log.rs`
  - `maho/relay/migration/src/m20260617_000004_create_audit_log.rs`
  - `maho/relay/server/tests/enterprise_sso.rs`
  - `maho/relay/docker-compose.enterprise.yml`
- Removed docs:
  - `docs/enterprise-deployment.md`
  - `docs/enterprise/dpa-template.md`
  - `docs/enterprise/baa-template.md`
- Updated:
  - `docs/decisions/0001-pricing-model.md` (v3, Enterprise-free)
  - `docs/security/no-billing-policy.md` (removes Enterprise section)
  - `website/src/pages/pricing.astro` (single-tier free)
  - `docs/plans/wave-d-enterprise-mature.md` (P2-6 marked DROPPED)
