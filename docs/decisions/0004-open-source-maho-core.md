# 0004. Open-Source License for maho-core and Public Crates

> **⚠️ SUPERSEDED / REVERSED (2026-07)**: This ADR is no longer in force. Maho is **proprietary and closed-source**. There is no public source repository for the Maho codebase, no Apache 2.0 (or any OSI) license grant on `maho-core` or any other crate, and no open-source marketing claim. Do NOT rely on this ADR for any current license, contribution, or positioning decision, and do NOT introduce "open source", "auditable source", "view source on GitHub", or public-source-repo language anywhere based on it. Trust positioning now rests on **trust by architecture**: self-hostable sync relay, zero-retention LLM proxy, end-to-end encryption, and locally verifiable behavior. The body below is retained unchanged for historical audit only. See root `AGENTS.md` (LICENSING & DISTRIBUTION) for the current posture.

Date: 2026-06-17
Status: Superseded / Reversed (2026-07) — was: Accepted (2026-06-17)

## Context and Problem Statement

Maho's strategic position is "trust by transparency": the user's data is end-to-end encrypted on the relay, the AI integration is auditable end-to-end, and the agentic protocol (ACP) is an open standard rather than a proprietary lock-in. Genspark, the headline competitor, ships as an opaque binary. The cleanest way to make our position credible (and the cleanest way to recruit external contributors and partner browsers) is to publish the Rust core under a real open-source license.

This decision was substantively analyzed in `docs/plans/wave-c-differentiation.md` P2-2 (sub-task C-2.1). It calls for an ADR before the GitHub repo split, before any contribution agreement is drafted, and before the marketing announcement (sub-task C-2.7). The license choice has long-lived consequences for patent posture, fork dynamics, downstream packaging, and our ability to upstream changes from `maho-extension` and `mail/` later.

The question this ADR answers is twofold:

1. **Which crates are in scope for open-sourcing?**
2. **Which OSI-approved license do we publish them under?**

## Decision Drivers

- **Trust by transparency.**
  The marketing position ("Maho is auditable; Genspark is a black box") is only as strong as the published source surface. Token open-sourcing of a single crate would be obviously cosmetic.
- **Patent protection for contributors and the project.**
  We expect contributions from independent engineers, partner browsers, and possibly large companies. A license without an explicit patent grant exposes both the contributor and us to patent assertion.
- **ACP standard credibility.**
  Wave C P2-1 turns ACP into a public standard with a reference implementation. The reference implementation must be under a license that other browser vendors (Brave, Arc-successors, Dia) can adopt without a legal review that takes longer than the implementation.
- **Compatibility with closed shells.**
  The macOS, iOS, and Android shells, plus the Chromium overlay, link against the public crates. The license must not force the shells to also become open-source. (This rules out GPL-3.0 and AGPL.)
- **Compatibility with Chromium's BSD-3 license boundary.**
  The Chromium overlay (`maho-chromium/`) consumes a prebuilt static library produced from the public crates. The public-crate license must permit static linking into a BSD-3-licensed binary without imposing reciprocal obligations on the binary.
- **Community familiarity.**
  A license that 90% of Rust ecosystem contributors recognize on sight reduces friction. Apache 2.0 and MIT pass this test trivially; MPL-2.0 and the various weak-copyleft licenses pass it less reliably.
- **Defensibility against hostile forks.**
  The brand "Maho" must remain ours. Source openness is a feature; brand dilution is not.

## Considered Options

- **Option A: Apache License 2.0.**
  Permissive, with an explicit patent grant and a patent-termination clause. The de-facto standard for organizationally-backed Rust projects (tokio, hyper, candle, axum).
- **Option B: MIT License.**
  Maximally permissive. Short, well-understood, but no explicit patent grant.
- **Option C: MPL-2.0 (Mozilla Public License 2.0).**
  File-level weak copyleft. Contributors must publish modifications to MPL-licensed *files*, but downstream users can combine MPL files with proprietary code.
- **Option D: GPL-3.0.**
  Strong copyleft. Linking GPL-3.0 code into the closed shells would require those shells to also be GPL-3.0. Disqualifying for our shipping model.
- **Option E: AGPL-3.0.**
  GPL-3.0 plus a network-use clause. Even more aggressive than GPL-3.0. Disqualifying for the same reasons, plus extra hostility to enterprise adoption.

## Decision Outcome

**Chosen option: Apache License 2.0 for the public crates.**

**In-scope crates** (publish under Apache 2.0):

- `maho/crates/maho-core`
- `maho/crates/maho-types`
- `maho/crates/maho-storage`
- `maho/crates/maho-engine`
- `maho/crates/maho-ffi`
- `maho/crates/maho-jni`
- `maho/crates/maho-bench`
- `maho/crates/maho-acp`

**Excluded** (stay closed for now, or live under a different license boundary):

- `maho-chromium/`: bound by Chromium's license boundary; the Maho overlay code that depends on Chromium internals stays where it is.
- `maho/crates/maho-extension`: TBD; revisit after the Web Extensions surface stabilizes.
- `mail/`: Tauri-based, separate Cargo workspace, separate concern; not part of the Wave C P2-2 announcement.
- `website/`: proprietary marketing site; not source-relevant.

The license file is `LICENSE-APACHE-2.0`, mirrored at the repo root and at each crate root, per the standard Rust convention. The repo root also carries `NOTICE` listing third-party Apache 2.0 attributions inherited transitively (notably candle, per ADR 0002).

This decision ratifies the recommendation in `docs/plans/wave-c-differentiation.md` P2-2 sub-task C-2.1.

### Consequences

**Positive**

- Patent protection. Apache 2.0 §3 grants a license to any patent claim necessary to use the contribution, and §3 second sentence terminates that grant if the licensee files a patent suit against the project. Both directions matter.
- ACP credibility. Apache 2.0 is the license of choice for cross-vendor standards (Kubernetes, OpenTelemetry, Apache Arrow). Other browser vendors can adopt the reference implementation with a legal review measured in days, not months.
- Ecosystem compatibility. Apache 2.0 is one-way compatible with GPL-3.0+ (someone can sublicense Apache 2.0 code into a GPL-3.0+ project), and trivially compatible with MIT and BSD downstreams. Our public-crate consumers have no surprises.
- Community recruitment. The Rust ecosystem's "Apache 2.0 OR MIT" dual-license norm is the unofficial industry standard. Our single-license choice is intentionally simpler than dual-licensing because it removes the contributor's choice (and the maintainer's bookkeeping cost).
- Trust signal. The license aligns with the marketing claim. We can write "Maho is open-source under Apache 2.0" on the homepage without an asterisk.
- Chromium boundary safety. Apache 2.0 permits static linking into a BSD-3 binary; the Chromium overlay consuming the prebuilt static library is unaffected.

**Negative**

- **Forks are legally permitted.** A competitor could copy the entire maho-core surface and ship a competing browser.
  *Mitigation:* the brand "Maho" is trademarked separately. The trademark is not licensed under Apache 2.0; a fork cannot ship under the Maho name. Marketing leverage and ecosystem trust stay with the original.
- **Patent grant flows both ways.** Anyone receiving Apache 2.0 code from us also receives a patent license from us covering that code. We accept this; the patent-termination clause is the counter-pressure.
- **No copyleft.** A vendor can take our crates, modify them for a closed product, and never give back. Apache 2.0's contribution incentive is reputational and ecosystemic, not legal. We accept this trade because the alternatives (MPL-2.0, GPL-3.0) lose us either community familiarity or shell-compatibility.
- **Contribution agreement burden.** To preserve the patent grant in a clean form, we adopt a Developer Certificate of Origin (DCO) sign-off model rather than a heavyweight CLA. This is documented in the forthcoming `CONTRIBUTING.md` (sub-task C-2.4).
- **Secret-scrub burden one time.** Before the public push, the `maho/` workspace history must be scanned for accidentally-committed credentials and internal-only references. This is a separate effort (sub-task C-2.3); this ADR does not perform it.

## Pros and Cons of the Options

| Dimension | Apache 2.0 (chosen) | MIT | MPL-2.0 | GPL-3.0 | AGPL-3.0 |
|---|---|---|---|---|---|
| OSI-approved | Yes | Yes | Yes | Yes | Yes |
| Permissive vs copyleft | Permissive | Permissive | Weak (file-level) copyleft | Strong copyleft | Strong copyleft + network-use |
| Patent grant clause | Yes (explicit) | No | Yes (weaker than Apache) | Yes (GPL form) | Yes (GPL form) |
| Patent-termination clause | Yes | No | Yes | Yes | Yes |
| Closed-shell compatibility | Yes | Yes | Yes (file-level) | No (forces shell to GPL) | No (worse than GPL) |
| Chromium BSD-3 boundary | Compatible (static link OK) | Compatible | Compatible (file-level) | Incompatible | Incompatible |
| ACP cross-vendor adoption | Easy | Easy | Friction (file-level scope) | Hostile | Hostile |
| Rust ecosystem familiarity | Very high (industry default) | Very high | Medium | Medium | Low |
| Defends against hostile fork | Brand + trademark | Brand + trademark | License-level (file-level) | License-level (strong) | License-level (strongest) |
| Compatibility with our deps | Native (candle, tokio) | Native | Friction with Apache | Friction (one-way) | Friction (one-way) |
| Contribution model | DCO or CLA | DCO or CLA | DCO + scope rules | CLA usually required | CLA usually required |
| Verdict | **Chosen** | Rejected: no patent grant | Rejected: file-level scope confusion + Apache compatibility friction | Disqualified: forces closed shells open | Disqualified: even more aggressive than GPL |

The two real candidates are Apache 2.0 and MIT. MIT loses on the patent-grant axis. The patent grant is not a hypothetical concern: ACP is an interoperability standard, on-device LLM inference is an active patent-litigation surface, and the relay sync protocol intersects with the encrypted-messaging patent landscape. Apache 2.0's explicit patent grant plus termination clause is the conservative answer.

MPL-2.0 is the fallback if a board-level decision later requires file-level copyleft to keep modifications upstream. It is rejected today because (1) the file-level scope creates real ambiguity at FFI boundaries (does a Mojo IDL-generated stub count as "the same file"?), and (2) MPL-2.0's compatibility with Apache 2.0 is one-directional and asymmetric, which would complicate the candle dependency posture in ADR 0002.

## Migration Plan (separate effort)

This ADR does not perform the migration. The migration is sub-tasks C-2.2 through C-2.7 in `docs/plans/wave-c-differentiation.md`, executed after this ADR is accepted.

The required migration steps, captured here for cross-reference:

1. **Secret scan.** Run `gitleaks` (or equivalent) across the full history of the in-scope crates. Triage every hit. (Sub-task C-2.3.)
2. **Optional `git filter-branch`** (or `git filter-repo`) pass to remove any historical secret that scan turns up. This rewrites history; the rewritten history is what the public repo receives. The internal monorepo history is preserved separately.
3. **Per-crate `README.md`** in each in-scope crate root, written for an external reader who has never seen the workspace.
4. **Repo-root `README.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, `LICENSE-APACHE-2.0`, `NOTICE`.** The `CONTRIBUTING.md` adopts DCO sign-off and references the patent-grant flow.
5. **GitHub Actions CI** for the public repo: `cargo check --workspace`, `cargo test --workspace`, `cargo clippy -- -D warnings`, `cargo fmt --check`. Mirrors the `bun run typecheck` and `bun run test` proxies the internal repo uses.
6. **Trademark filing for "Maho"** if not already in motion. The trademark is the brand defense layer that complements the Apache 2.0 license. (Cross-cutting concern, see `docs/plans/wave-c-differentiation.md` §3.2.)
7. **Marketing announcement** (sub-task C-2.7). Blog post, HN submission, r/rust submission. References this ADR for the license rationale.

## Review Triggers

This ADR is reviewed if **any** of the following occurs.

1. **A board-level decision** to require copyleft on the public crates (for example to prevent a specific competitor from taking the code). Re-evaluate MPL-2.0.
2. **A patent assertion** against the project or against a contributor that the Apache 2.0 §3 termination clause has to be invoked against. Confirm the clause works as drafted; consider tightening the contribution agreement.
3. **The Chromium license boundary changes** (for example Chromium relicensing some component such that static-linking Apache 2.0 code becomes contentious). Re-evaluate the prebuilt-static-library boundary.
4. **A second Maho crate** outgrows the "stay closed" exclusion and becomes a candidate for open-sourcing. The default disposition is Apache 2.0 by extension, but the scope decision lives in a follow-up ADR.
5. **A trademark dispute** that proves the brand defense layer alone is insufficient. Re-evaluate whether a stronger license layer (MPL-2.0 file-level) would have helped.
6. **The Rust ecosystem norm shifts** away from Apache 2.0 (unlikely; documented for completeness).

The ADR is **not** reviewed on routine forks, routine third-party packaging, or routine downstream relicensing of derivative works that Apache 2.0 already permits.

## More Information

- Wave plan: `docs/plans/wave-c-differentiation.md` P2-2 (full sub-task list, acceptance criteria, dependencies)
- Related ADR: `docs/decisions/0002-on-device-llm-library.md` (candle is Apache 2.0; license compatibility was a decision driver there too)
- Related ADR: `docs/decisions/0001-pricing-model.md` (the open-source posture is part of the same trust-by-transparency story that the pricing ADR commits to)
- Workspace context: `AGENTS.md` (root) for the three Cargo workspaces (`maho/`, `maho/relay/`, `mail/`), only the first of which is in scope here
- Apache License 2.0 text: https://www.apache.org/licenses/LICENSE-2.0
- OSI license list: https://opensource.org/licenses
- Developer Certificate of Origin: https://developercertificate.org/
- Reference: tokio, hyper, candle, axum (Rust projects under Apache 2.0)

This ADR ratifies the open-sourcing recommendation in `docs/plans/wave-c-differentiation.md` P2-2 and locks the license choice. It does not perform the migration, draft the contribution agreement, or schedule the marketing announcement. Those are tracked as sub-tasks C-2.2 through C-2.7 in the wave plan.
