# 5. MCP Curation Policy

## Status

**SUPERSEDED**, 2026-06-18.

The curation list (`docs/mcp-curation-list.md`) and schema doc
(`docs/mcp-curation-schema.md`) that this ADR governed were deleted as DEAD
scaffolding (no Settings UI consumes the data; future Settings UI not on the
near-term roadmap). When MCP curation is needed for a real Settings UI in the
Chromium fork, a fresh ADR will be authored.

The original ADR text is preserved below for audit trail only.

---

## Status

Accepted, 2026-06-17.

Next scheduled review: **2026-12-17** (six months after acceptance, aligned with the second quarterly audit refresh).

## Context

Genspark publicly markets a catalog of more than 700 MCP (Model Context Protocol) servers. The marketing message is "more is better." In practice, most of the catalog is unaudited: tool inventories are not enumerated, permission classifications are inconsistent, and supply-chain provenance is opaque. A user installing an arbitrary MCP from that catalog has no Genspark-side guarantee about what tools the server exposes, what permissions those tools require, or whether the upstream repository was reviewed for malicious behavior.

Maho cannot win a quantity race against that catalog and should not try. Maho's positioning across Wave A and Wave B is "trust per entry, not entries per page." The MCP surface is a high-blast-radius attack vector — a malicious server can read the user's browsing context, exfiltrate cookies, write to the file system, or fire financial transactions. The pricing ADR (`docs/decisions/0001-pricing-model.md`) and Maho's trust-by-architecture posture (self-hostable sync relay, zero-retention LLM proxy, locally verifiable behavior) lean on the same trust positioning. An unaudited MCP catalog would contradict every other promise Maho makes about safety. (Note, 2026-07: an earlier draft of this paragraph cited a planned Wave C open-source ADR; Maho is proprietary / closed-source, so the trust argument rests on architecture, not on a public source license.)

This ADR records the policy decision before the Settings UI lands. The Settings UI work itself is tracked in Wave B P1-5 and is currently blocked on P1-4 settings pane infrastructure; the policy here governs what that UI MUST enforce when it ships.

## Decision Drivers

- **Safety budget**: every MCP we list at install time inherits Maho's reputation. One bad entry shipped at scale is worse than ten missing entries.
- **Operational realism**: a small curation team cannot audit 700 servers. We need a small, defensible set plus a clear pathway for community contributions.
- **Honesty**: users SHOULD be able to install any MCP they want, but Maho MUST be explicit about whether Maho has audited it.
- **Refresh discipline**: an audit is a snapshot. Without a refresh cadence, the list silently rots.
- **Future Settings UI contract**: the UI shipping in P1-5 (when unblocked) needs a stable schema and a stable policy to render against.

## Considered Options

### Option A. Full marketplace clone

Mirror the Genspark approach: every MCP we can find, listed with minimal metadata, opt-in install.

**Pros**
- Maximum surface area; nothing the user can find on Genspark is "missing."
- No curation overhead.

**Cons**
- We inherit every supply-chain risk in the catalog with zero added value.
- "Audited by Maho" becomes meaningless because everything is listed.
- Marketing cannot honestly claim a safety differentiator.

### Option B. No curation, allow only manual install

Ship no list. Users paste a repo URL or local path and Maho runs it after a generic warning.

**Pros**
- Zero ongoing operational cost.
- Maho takes no implicit position on any specific server.

**Cons**
- New users have nothing to start with. Activation drops.
- Genspark's "700+" marketing is unanswered.
- The generic warning becomes click-through noise; it does not actually inform the decision.

### Option C. Curated allowlist with audit metadata (Chosen)

Maho maintains a hand-audited list of approximately 30 MCP servers at launch, scaling to 50 within two refresh cycles. Each entry carries audit date, auditor, tool count, default permission tier, risk notes, and lifecycle status. Users can install any entry from the list with one click. Users can also install an arbitrary MCP, but the UI MUST show a confirmation modal explaining that the entry has no Maho audit record. The list is refreshed quarterly and accepts community contributions through a PR workflow.

**Pros**
- Concrete, defensible safety claim. "We audit every entry on this list" is a single sentence that maps to a single document.
- Operational cost is bounded. 30 to 50 entries quarterly is two reviewer-days per quarter.
- Community contributions scale audit throughput without diluting the safety claim.
- The list is transparent (lives in the public repo) and reviewable. Anyone can audit our auditing.
- Genspark's quantity story is countered with a quality story rather than ignored.

**Cons**
- Operational overhead, even if bounded.
- Some users will want servers we have not audited and will see the warning modal as friction.
- The first list is necessarily small relative to Genspark.

## Decision Outcome

**Chosen: Option C.**

Maho maintains a curated allowlist of 30 or more MCP servers in `docs/mcp-curation-list.md`, governed by the schema in `docs/mcp-curation-schema.md`. Inclusion in the list certifies that the entry has passed Maho's initial audit (tool inventory, permission classification, risk surface review). Exclusion does not imply danger; it means we have not yet audited the entry.

When the Settings UI lands (blocked on P1-4 settings pane infrastructure), it MUST:

1. Render only `Curated` entries in the default catalog view.
2. Allow the user to install any curated entry with one click, applying the entry's default `permissionTier`.
3. Allow the user to install an arbitrary MCP via a manual flow, but show a confirmation modal that names the missing audit record explicitly.
4. Display the audit date and auditor on every curated card.
5. Refuse to upgrade a `Destructive` entry to `Auto` from this surface; the user MUST grant per-call approval for destructive tools.

Operationally:

- The list is refreshed every 92 days. The Curation Team is responsible for the refresh.
- Any MCP that publishes a new minor or major version triggers an out-of-cycle re-audit before the list reflects the new version.
- Community audits land via PR. The reviewer signs off, sets `Audit By`, and merges.
- Emergency removal (CVE, supply-chain compromise) moves the entry to `Deprecated` immediately, with the reason recorded in the audit log.

### Positive Consequences

- Maho can credibly market "every MCP we recommend has a name on it" without a footnote.
- The curation list doubles as a recruiting and contribution funnel: external auditors get a visible byline.
- The schema is small enough that the future Settings UI does not depend on a service; it ships as a static JSON resource.
- The operational floor (30 entries) is small enough that one quarterly day-and-a-half pays for the entire list.

### Negative Consequences

- We are smaller than Genspark and visibly so. Users who count entries will count fewer.
- The community PR pathway requires reviewer bandwidth that is not strictly automatable.
- The list will lag fast-moving upstreams between refresh cycles. Users who want bleeding-edge versions will use the manual flow and see the warning.

## Review

This ADR will be re-opened on **2026-12-17**, or earlier if any of the following happen:

- The list grows beyond 100 entries and the quarterly cadence stops being feasible.
- An entry shipped as `Curated` is later found to have been compromised at audit time, calling the audit process itself into question.
- A second self-serve install path emerges (for example, a per-tool sub-allowlist) that this ADR does not anticipate.
- Genspark or another competitor adopts a comparable curation model, neutralizing the differentiator.

If none of those triggers fire, the December review is a confirmation, not a redesign.

## References

- `docs/mcp-curation-list.md` — the canonical list this policy governs.
- `docs/mcp-curation-schema.md` — field definitions and validation rules.
- `docs/plans/wave-b-genspark-gap.md` §P1-5 — the implementation plan this ADR unblocks at the policy layer.
- `docs/decisions/0001-pricing-model.md` — sibling trust-positioning ADR.

---

*Draft v0.1, last updated 2026-06-17.*
