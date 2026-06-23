# Vendor Risk Register

This document tracks third-party vendors, suppliers, and library dependencies utilized by the Maho Browser Agent, assessing their security postures and outlining organizational contingency plans under SOC2 Security Criterion **CC9.1** (Risk Management).

**Risk score scale** (1-5):
- **1 — Minimal**: Self-contained MIT/Apache lib, no network/data access, replaceable
- **2 — Low**: Active maintenance + small attack surface + standard license
- **3 — Medium**: External service or wide attack surface; loss of vendor affects operations
- **4 — High**: Sensitive data flows through vendor; switching cost is non-trivial
- **5 — Critical**: Single-vendor lock-in for sensitive workflow; outage = product outage

---

## 1. Cloud & LLM Service Providers

These external services process user prompts and require strict vendor evaluation:

| Vendor | Service | Risk Score | SOC2 Status | DPA Signed | DPA Expiry | Last Review | Next Review | Owner | Subprocessors | Contingency |
|---|---|---|---|---|---|---|---|---|---|---|
| **OpenAI LLC** | GPT-4o inference | 4 | SOC2 Type II (Attested) | 2026-01-10 | 2027-01-10 | 2026-05-15 | 2026-11-15 | vCISO | Microsoft Azure (US-East-2) | Fail-over to Anthropic / local llama.cpp |
| **Anthropic PBC** | Claude 3.5 Sonnet | 4 | SOC2 Type II (Attested) | 2026-02-12 | 2027-02-12 | 2026-05-15 | 2026-11-15 | vCISO | AWS (us-west-2), GCP | Fail-over to OpenAI / private API |

**Escalation contact**: security@maho.dev → vCISO → CTO. P1 SLA: 4h ack, 24h triage.

---

## 2. Library & Code Dependencies (Supply Chain)

These third-party open-source libraries are integrated directly into our compiled binaries. They are scanned daily via Dependabot and `cargo audit` checks:

| Dependency | Purpose | Risk Score | License | SOC2 Scope | CVE Response SLA | Contingency Plan |
|---|---|---|---|---|---|---|
| **swiftide-core / swiftide-agents** | Agent architecture executor | 3 | MIT | Vendor Risk Only | Critical: 24h, High: 7d | In-house fork if abandoned |
| **async-openai / async-anthropic** | Client bindings | 2 | MIT | Vendor Risk Only | Critical: 24h, High: 7d | Replace with direct reqwest wrappers |
| **secrecy / zeroize** | Memory safety wrappers | 1 | MIT | Vendor Risk Only | Critical: 24h, High: 7d | Custom zeroize wrapper |
| **reqwest / hyper** | HTTP client libraries | 2 | Apache-2.0 / MIT | Vendor Risk Only | Critical: 24h, High: 7d | Latest patch; strict CVE SLA |
| **rmcp** | MCP client protocol | 3 | MIT | Vendor Risk Only | Critical: 24h, High: 7d | Custom MCP impl if abandoned |

---

## 3. Security Exceptions & Accepted Risks

The following CVEs/advisories have been evaluated and accepted as exceptions by the Security Lead. Each exception includes a **re-evaluation trigger** to prevent indefinite suppression:

| Advisory ID | Crate / Library | Severity | Rationale for Acceptance | Re-evaluation Trigger | Owner | First Suppressed |
|---|---|---|---|---|---|---|
| **RUSTSEC-2026-0041** | `lz4_flex` (v0.9.5) | High (8.2) | Pulled via `diamond-types` as dev-dependency of `maho-bench`. NOT in production browser releases. | Quarterly review OR upgrade of `diamond-types` to a version using `lz4_flex >=0.11.6` | vCISO | 2026-06-23 |
| **RUSTSEC-2026-0104** | `rustls-webpki` (v0.101.7) | High | Reachable panic in CRL parsing. Maho Agent does not process CRLs directly; TLS pre-validated certs only. | Next `rustls` minor version OR quarterly review | vCISO | 2026-06-23 |
| **RUSTSEC-2026-0098** | `rustls-webpki` (v0.101.7) | Medium | Name constraints incorrectly accepted. Low risk in desktop runtime context. | Next `rustls-webpki` patch OR quarterly review | vCISO | 2026-06-23 |
| **RUSTSEC-2026-0099** | `rustls-webpki` (v0.101.7) | Medium | Wildcard cert name constraints incorrectly accepted. Low risk in desktop runtime context. | Next `rustls-webpki` patch OR quarterly review | vCISO | 2026-06-23 |
| **N/A — orphan** | `reqwest` (v0.11.27) | N/A | Transitive dep of legacy `async-openai` (v0.14.3) + `reqwest-eventsource`. No workspace member directly depends; resolves through dep tree. NOT a CVE; tracked here for hygiene. | When async-openai 0.14.x is removed from dep tree (next major dep cleanup) | Eng | 2026-06-23 |

**Indefinite suppression is FORBIDDEN.** Each exception must be re-evaluated quarterly OR when upstream releases a fixed version, whichever comes first. The vCISO maintains a calendar reminder for the next review date.

---

## 4. Review & Verification Cadence

1. **Annual Assessments**: vCISO requests updated SOC2 Type II reports from OpenAI and Anthropic every 12 months.
2. **Automated Scanning Checks**: CVE flags from Dependabot / `bun run security-audit` block releases unless documented in §3 above with active re-evaluation trigger.
3. **Contingency Drills**: Engineering conducts semi-annual simulation testing of provider switching via settings UI.
4. **Quarterly CVE Exception Review**: vCISO reviews all entries in §3 every 90 days. Stale entries (no re-evaluation OR fix in 180 days) trigger CTO escalation.
5. **Subprocessor Changes**: When OpenAI or Anthropic announces new subprocessors, the vCISO must update §1 within 30 days.

---

## 5. New Vendor Onboarding Checklist

Before adding any new vendor (cloud service or critical library):
- [ ] SOC2 Type II report requested (or equivalent — ISO 27001, FedRAMP)
- [ ] DPA signed (for data-processor vendors)
- [ ] Subprocessor disclosure obtained
- [ ] License compatibility check (MIT/Apache OK; AGPL/copyleft requires CTO approval)
- [ ] Risk score assigned (table above scale)
- [ ] Contingency plan documented
- [ ] Initial review date set (within 90 days of onboarding)


