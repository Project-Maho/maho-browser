# Maho Sync Relay External Security Audit, 2026

This document is the **Request-for-Proposal (RFP)** style scope for an external security audit of Maho's sync infrastructure. It is the artifact a security firm should read first when scoping the engagement.

This is **not** an audit report. No findings, no severity ratings, no certifications appear in this document. For prep materials handed to the auditor, see `docs/security/audit-package/README.md`.

## 1. Overview

Maho is preparing to launch a privacy-by-architecture browser and mail client. The product line includes a **sync relay** that holds end-to-end-encrypted blobs on behalf of users, plus a small amount of authentication metadata. Because Maho's marketing position is "trust by architecture, not by promise," we want a public third-party audit of the relay before launch.

The goal of the engagement is:

1. Verify the cryptographic claims hold up under hostile review.
2. Surface implementation-level vulnerabilities (auth, authz, parsing, race conditions, DoS).
3. Produce a report we can publish in full, CVE-style, alongside Maho's response and any mitigations.

We are not seeking a "passed / failed" badge. We are seeking written, specific findings that we can act on and publish.

## 2. In-Scope Components

### 2.1 `maho/relay/` (entire crate)

전체 relay 서버 구현. Rust crate, 별도 Cargo workspace.

- HTTP/WebSocket entry points.
- Connection handling, framing, rate limiting.
- Persistence layer (SQLite-backed).
- Background workers (cleanup, metrics).
- Configuration surface and operational defaults.
- Build artifacts and Docker image, if used in production deployment.

### 2.2 `maho/crates/maho-core/src/sync/`

Client-side sync logic that talks to the relay. Specifically:

- The end-to-end encryption layer (sealing and unsealing blobs).
- Key derivation from the user's passphrase (Argon2id, parameter selection).
- Session key handling and rotation.
- Conflict resolution and replay protection.

### 2.3 Authentication and Session Management

- JWT issuance, signing, and verification.
- Token lifetime and refresh flow.
- Login flow, including any password reset paths.
- "Logout from all devices" semantics.

### 2.4 Authorization (Room Ownership)

- The "room" concept (a sync namespace owned by an account).
- Access control: who can read, who can write, who can invite.
- Multi-device join flow and revocation.
- Cross-account isolation.

### 2.5 Argon2id KDF Parameters

We pick `m`, `t`, `p` parameters for password hashing and KDF. The auditor should challenge those choices specifically:

- Are the parameters strong enough against current GPU/ASIC attackers?
- Are they reasonable on the lowest-spec target device (mid-range mobile)?
- Is the salt scheme correct (per-user, sufficiently random, not reused)?

## 3. Out of Scope

The following are explicitly **not** part of this engagement:

- **Chromium itself.** Maho ships a Chromium fork for the browser; upstream Chromium has its own continuous audit posture and is out of scope here.
- **Third-party Rust dependencies.** Auditors should note any concerning transitive dependency, but full audit of those crates is out of scope.
- **The mail client's IMAP/SMTP layer.** Local-only, no relay involvement, separate audit if needed.
- **The macOS / iOS / Android shells.** Separate review surface; not part of the relay-focused engagement.
- **Marketing site, billing page, Stripe integration.** Billing is covered by `docs/security/billing-transparency.md` and Stripe's own SOC 2.
- **Physical security of the hosting provider.** Out of scope; covered by the provider's own SOC 2 / ISO 27001.

If the auditor finds something critical out of scope (e.g., an obvious flaw in a client shell), we want to hear about it informally, but it doesn't have to land in the formal report.

## 4. Engagement Preferences

### 4.1 Recommended Vendor: Cure53

We've shortlisted three firms. **Cure53** is our recommended primary, for these reasons:

- Open-source friendly. Many of their reports are public, and they're comfortable with us publishing the full deliverable.
- Reasonable pricing at our scope size.
- Track record on cryptographic protocol review (Mullvad, Signal-adjacent work).
- European entity, which simplifies the GDPR DPA conversation.

### 4.2 Alternates

- **Trail of Bits.** Strong on Rust and cryptography. Higher price band. We'd consider them if Cure53's calendar pushes past our launch window.
- **NCC Group.** Larger firm, broader practice. Good fallback if neither of the above can take the engagement on the right timeline.

We'll send the same scope document to all three and pick based on availability, price, and named-engineer fit.

## 5. Timeline

Target engagement window: **4 to 8 weeks** end-to-end.

| Week | Activity |
|------|----------|
| 1 | Kickoff, scope confirmation, environment access, threat-model walk-through |
| 2 to 4 | Active testing and code review |
| 5 to 6 | Maho-side mitigation of any high/critical findings, re-test |
| 7 to 8 | Final report writing and review |

We treat the inner mitigation/re-test cycle as part of the engagement, not as an afterthought. The report should reflect the post-mitigation state, with the original finding and the fix both documented.

## 6. Deliverables

The auditor should produce, at minimum:

1. **Final report (PDF).** Public-facing. Includes scope, methodology, findings, and Maho's response per finding. We will publish it under `docs/security/relay-audit-2026.pdf`.
2. **CVE-style finding list.** Each finding numbered (e.g., `MAHO-2026-001`), with severity (Critical / High / Medium / Low / Informational), description, reproduction steps, and recommended fix.
3. **Mitigation re-test confirmation.** For each finding that Maho fixes during the engagement, an explicit "verified mitigated" line in the final report.
4. **Methodology section.** What was looked at, what tools were used, what was *not* covered, and why. So readers can judge the audit's coverage honestly.
5. **Engineer-named cover letter (optional).** Signed by the lead consultant. Useful for procurement reviews on the Maho enterprise side.

## 7. Budget

**$30,000 to $80,000 USD** total engagement cost.

That range matches the Wave A planning band and matches typical quotes for a 4-to-8-week audit at this scope from each of the three firms above. We expect to land around the middle of that range with Cure53.

This budget covers:

- All consultant time across the engagement.
- Re-test of mitigations.
- Report production.

This budget does **not** cover:

- Bug bounty payouts (separate program, post-launch).
- Follow-up audits in subsequent years (separate engagement).
- Audit of any out-of-scope component listed in §3.

## 8. Logistics

- **Code access.** We'll provide read access to a private mirror of `maho/relay/` and `maho/crates/maho-core/src/sync/` at the engagement-start commit. The audit-package handoff (`docs/security/audit-package/README.md`) is the canonical entry point.
- **Environment.** We'll stand up a dedicated staging relay with synthetic accounts. No production data.
- **Communication.** Slack-shared channel for ongoing questions, weekly sync call, written status update each Friday.
- **Disclosure.** Findings are embargoed until the final report is published. We'll publish on a coordinated date once mitigations are merged.

---

*Draft v0.1, last updated 2026-06-17.*
