# SOC2 Type 1 → Type 2 Readiness Implementation Plan

**Created:** 2026-06-23
**Owner:** Engineering (technical controls) + Leadership (org controls)
**Status:** Planning
**Scope:** Maho Browser agent runtime + BYOK secret handling + supporting infrastructure
**Reference:** AICPA TSP 100-2017 Trust Services Criteria

---

## 1. Goal & Scope

### Goal
Achieve SOC2 Type 1 attestation within 6 months and Type 2 within 12-18 months for the Maho Browser agent + BYOK subsystem.

### Trust Service Categories
- **Security (Common Criteria CC1-CC9)** — REQUIRED, primary focus
- **Confidentiality** — RECOMMENDED for BYOK (user API keys = customer confidential data)
- *Not in scope*: Availability, Processing Integrity, Privacy (defer to later cycle)

### What's IN scope for this plan
- `maho/crates/maho-agent/` Rust runtime
- BYOK secret handling (iOS Keychain, Android KeyStore, Desktop OSCrypt)
- Permission gating + audit logging
- Cross-platform FFI surface (`maho-ffi`, `maho-jni`)
- Desktop Chromium overlay (`maho_unified_agent_adapter`)
- Mobile shells (AgentScreen.kt, AgentView.swift)

### What's OUT of scope
- Browser core (`maho-core`) — separate SOC2 cycle
- Sync relay (`maho/relay/`) — separate SOC2 cycle, has its own threat model
- Chromium upstream (vendor-managed)
- LLM provider APIs (OpenAI, Anthropic) — vendor risk only, not our controls

---

## 2. Current State (Phase 0: Self-Assessment)

### Existing controls (DONE)
| Control | Implementation | Evidence |
|---|---|---|
| CC6.1 Access | `PermissionGatedTool` default-deny, `denied_flag` short-circuit | `swiftide_backend.rs:95-141` |
| CC6.1 Session isolation | UUID-based session identity | `AgentScreen.kt:222`, `AgentView.swift:54` |
| CC6.6 At-rest (iOS) | Keychain w/ `kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly` | `MahoBridge+Agent.swift:82` |
| CC6.6 At-rest (Android) | AndroidKeyStore AES-256-GCM | `MahoBridge.kt:870-895` |
| CC6.6 At-rest (Desktop) | OSCrypt-encrypted prefs (read scaffold) | `maho_unified_agent_adapter.cc:209-258` |
| CC6.7 Transit | reqwest default TLS via system CA | dependency |
| CC6.8 Memory hygiene | `SecretString` + `Zeroizing` + `memset_s` + volatile loops | `swiftide_backend.rs:53-83`, `MahoBridge+Agent.swift:194-200` |
| CC7.1 Deny audit | `tracing::warn` on permission deny | `swiftide_backend.rs:130-134` |
| CC7.1 Poison audit | `tracing::error` + `mutex_type` field | `swiftide_backend.rs:23-32` |
| CC8.1 Change mgmt | 10-round oracle/agent review chain | This commit |
| CC8.1 Testing | 9/9 maho-agent tests | `cargo test -p maho-agent` |

### Gaps (TODO)
| Gap ID | Control | Effort | Owner |
|---|---|---|---|
| G1 | CC7.1 — Tool ALLOW path audit log | 1h | Eng |
| G2 | CC7.1 — Session start/end audit log | 1h | Eng |
| G3 | CC7.1 — BYOK key access audit | 2h | Eng |
| G4 | CC7.2 — Dependency vulnerability scanning (`cargo audit`, `bun audit`, Dependabot) | 3h | Eng/DevOps |
| G5 | CC7.2 — Rate limiting + cost monitoring (or document accepted risk) | 4h | Eng |
| G6 | CC7.4 — Incident response procedure | 2h doc | Eng + Sec lead |
| G7 | CC7.4 — Crash dump scrubbing for secrets | 4h | Eng |
| G8 | CC6.6 — SQLite conversation DB encryption-at-rest decision (encrypt or document accepted risk) | 6h or doc | Eng |
| G9 | CC6.8 — Desktop BYOK settings UI (write side for OSCrypt) | 8-12h | Eng |
| G10 | CC8.1 — SECURITY.md vulnerability disclosure policy | 1h | Eng |
| G11 | CC8.1 — CODEOWNERS + branch protection enforcement | 2h | DevOps |
| G12 | CC9.1 — Risk register + vendor risk review (OpenAI, Anthropic, swiftide) | 4h doc | Sec lead |
| G13 | CC8.1 — Release notes + change log discipline | 1h policy | Eng |
| G14 | CC6.8 — In-memory residency hardening (subprocess helper) — OPTIONAL, defer | 1-2d | Eng |
| G15 | CC2.2 — Internal threat model document with control mapping | 4h doc | Eng + Sec lead |
| G16 | CC4.1 — Continuous monitoring dashboard (Sentry, Grafana, etc.) | 1d setup | DevOps |
| G17 | CC1.x — Background screening policy for production access | Org | Leadership |
| G18 | CC1.x — Vendor risk management procedure | Org doc | Sec lead |

**Total engineering effort**: ~40-60 hours (5-8 days focused work)
**Total documentation effort**: ~15-20 hours
**Total org/process effort**: 4-6 weeks elapsed (involves people decisions)

---

## 3. Phase 1: Engineering Controls (4-6 weeks)

### 1.1 Audit logging completion (G1-G3) — 4 hours

**G1: Tool ALLOW path audit**

Add `tracing::info!` in `PermissionGatedTool::invoke` AFTER allow decision, BEFORE inner_tool.invoke:

```rust
if decision == PermissionDecision::Allow {
    tracing::info!(
        target = "maho_agent::audit",
        tool = %self.name(),
        session_id = %context.session_id().unwrap_or_default(),
        "Tool invocation allowed; proceeding",
    );
}
```

**G2: Session start/end audit**

In `SwiftideBackend::run_turn`:
```rust
tracing::info!(
    target = "maho_agent::audit",
    session_id = %session_id,
    event = "session_start",
);
// ... existing logic ...
tracing::info!(
    target = "maho_agent::audit",
    session_id = %session_id,
    event = "session_end",
    duration_ms = elapsed.as_millis(),
);
```

**G3: BYOK key access audit**

In `MutexAgentStorage::get_byok_key` and equivalent FFI paths:
```rust
tracing::info!(
    target = "maho_agent::audit",
    event = "byok_key_access",
    provider = %provider,
    // do NOT log the key itself
);
```

**Acceptance criteria**:
- `tracing::info!` calls with `target = "maho_agent::audit"` exist for: session lifecycle, tool decisions, BYOK access
- No sensitive data in audit logs (verified by grep)
- Audit logs routed to dedicated stream (separate from debug logs)

### 1.2 Dependency vulnerability scanning (G4) — 3 hours

**Actions:**
1. Add `bun run audit` step to root `package.json` (proxy to `cargo audit` for Rust + `bun audit` for web)
2. Add CI job: GitHub Actions workflow `.github/workflows/security-audit.yml`
3. Enable Dependabot for `maho/`, `maho-chromium/`, `mail/`, `website/` workspaces
4. Document allowed CVE exception process

**Acceptance criteria**:
- `bun run security-audit` exits non-zero on known vulns
- CI fails PR if vuln introduced
- Documented process for exceptions

### 1.3 Compliance tests (G6) — 1 hour

Add integration tests proving fail-closed behavior:
- `test_agent_fails_without_byok_key` — `allow_insecure_key_storage=false`, no callback → expects `ExecutionError`
- `test_agent_denies_unauthorized_tool` — tool not in allowlist → `PermissionDenied`
- `test_secure_storage_callback_invoked` — verifies callback path is exercised

Already covered by `test_agent_permission_deny`; expand for full evidence.

### 1.4 Crash dump scrubbing (G7) — 4 hours

For Chromium overlay: ensure `Box<dyn ChatCompletion>` and `SecretString` are wiped during `RtlCaptureStackBackTrace` / breakpad / crashpad processing. Most Chromium secrets are already scrubbed; verify our `MahoOpenAIConfig` is included.

**Actions:**
- Mark `MahoOpenAIConfig` fields as `// SAFETY: contains BYOK plaintext, exclude from crash dumps`
- Add to Chromium's `SensitiveData` registry if applicable
- Verify with crash dump test on debug build

### 1.5 Desktop BYOK settings UI (G9) — 8-12 hours

**Scope:**
- Mojo IDL extension: `SetBYOKKey(provider, key)` / `ClearBYOKKey(provider)` in `maho_settings.mojom`
- C++ page handler: `OSCrypt::EncryptString` + base64 encode + `prefs_->SetString(...)`
- React UI: BYOK section in `chrome://maho-settings/ai` with masked input + Save/Clear buttons
- Verification: round-trip test (set → get → decrypt → matches input)

**Out of scope:**
- Key rotation UI (separate phase)
- Multi-key per provider (single key only for v1)

### 1.6 Vulnerability disclosure (G10) — 1 hour

Create `SECURITY.md` at repo root:
- How to report (security@maho.dev)
- Response SLA (72 hours acknowledge, 90 days fix)
- Scope (in-scope vs out-of-scope)
- Hall of fame / bounty (optional)

### 1.7 Branch protection + CODEOWNERS (G11) — 2 hours

- `.github/CODEOWNERS` mapping for agent/security-critical paths
- Branch protection rules (require review, require CI, no force-push)
- Documented in `docs/development/code-review-policy.md`

---

## 4. Phase 2: Documentation (2-3 weeks)

### 2.1 Threat model + Control mapping (G15) — 4 hours

Create `docs/security/agent-threat-model.md`:
- Asset inventory (BYOK keys, conversation history, agent execution context)
- Attacker scenarios (malware, memory disclosure, network MITM, supply chain)
- Control mapping (each TSC criterion → code location → test evidence)
- Acknowledged residual risks (e.g., in-memory plaintext window, equivalent to other browser secrets)

### 2.2 Incident response procedure (G6) — 2 hours

Create `docs/security/incident-response-byok.md`:
- Detection (logs, monitoring, user reports)
- Triage (severity classification)
- Containment (key revocation procedure)
- Eradication (remove malicious code, patch)
- Recovery (notify users, reissue keys)
- Lessons learned (post-mortem template)

### 2.3 Key rotation procedure — 1 hour

Create `docs/security/byok-key-rotation.md`:
- User-side: how to rotate via Settings UI
- Engineering: code paths that respect rotation
- Compliance: rotation SLA per provider policy

### 2.4 Vendor risk register (G12) — 4 hours

Create `docs/security/vendor-risk-register.md`:
- LLM providers: OpenAI, Anthropic (SOC2 reports requested, DPA reviewed)
- Library vendors: swiftide, rmcp, async-openai, async-anthropic, reqwest, hyper, secrecy, zeroize
- Each vendor: SOC2 status, license, last review date, contingency plan

### 2.5 Release notes + change log policy (G13) — 1 hour

Establish:
- `CHANGELOG.md` with Keep-a-Changelog format
- Security entries tagged with `[SECURITY]`
- Breaking changes flagged
- Per-release security review sign-off

### 2.6 Cross-references

Update:
- `docs/architecture/native-vs-web-boundary.md` — add SOC2 control references
- `docs/architecture/streaming-event-protocol.md` — add audit log envelope spec
- `AGENTS.md` files — add security review checkpoint to commit checklist

---

## 5. Phase 3: Organization Processes (4-6 weeks — leadership-owned)

### 3.1 Security org (Leadership)
- Designate security lead (could be CTO or hire CSO)
- Define security review approval matrix
- Establish quarterly security review cadence

### 3.2 Access management
- Production access ACLs (who can deploy, who can read prod logs)
- Periodic access reviews (quarterly)
- Offboarding checklist
- 2FA enforcement on critical accounts (GitHub, npm, registries)

### 3.3 Background screening (G17)
- Policy for production-access engineers
- Vendor: Checkr / HireRight / equivalent
- Document in `docs/policies/background-screening.md`

### 3.4 Vendor risk management (G18)
- New vendor onboarding checklist (SOC2 report request, DPA, ToS review)
- Annual vendor review
- Vendor offboarding (key revocation, contract closure)

### 3.5 Training program
- Annual security awareness training (engineers + non-engineers)
- Code review security checklist
- Onboarding security module

---

## 6. Phase 4: Continuous Monitoring (G16) — 6+ months ongoing

### 4.1 Logging & telemetry
- Centralized log aggregation (Datadog, Grafana, Splunk)
- Structured `tracing` outputs from all agent paths
- Anomaly detection (cost spikes, unusual tool patterns)
- Alert routing (PagerDuty, Slack)

### 4.2 Vulnerability management
- Weekly dependency scan review
- CVE response SLA tracking
- Pen test cadence (annual minimum)

### 4.3 Change management evidence
- Git history as primary evidence
- PR template with security checklist
- Approval audit trail

### 4.4 Periodic reviews
- Quarterly access reviews
- Quarterly risk register update
- Annual policy review

---

## 7. Phase 5: Audit Preparation (4-6 weeks pre-audit)

### 5.1 Self-assessment
- Internal Type 1 dry-run
- Gap remediation sprint
- Evidence packaging

### 5.2 Auditor selection
- Big 4 (Deloitte, PwC, EY, KPMG) — premium pricing, market signal
- Boutique (Schellman, A-LIGN, BARR Advisory) — better pricing, faster
- Request RFP from 2-3 vendors
- Reference checks

### 5.3 Pre-audit prep
- Document collection (policies, procedures, evidence)
- Walkthrough rehearsal
- Q&A bench for engineers

---

## 8. Phase 6: External Audit (3-6 months)

### 6.1 Type 1 audit (point-in-time)
- Auditor kickoff
- Control walkthroughs
- Evidence sampling
- Report draft
- Management response
- Final report

### 6.2 Type 2 audit (operating effectiveness over 6+ months)
- Sustained control operation evidence
- Continuous monitoring evidence
- Sampling over the audit period
- Final report

---

## 9. Timeline Estimate

| Phase | Duration | Cumulative |
|---|---|---|
| Phase 0 (scoping done) | 0 | 0 |
| Phase 1 (eng controls) | 4-6 weeks | 6 weeks |
| Phase 2 (docs) | 2-3 weeks (parallel with P1) | 6 weeks |
| Phase 3 (org) | 4-6 weeks (parallel) | 8 weeks |
| Phase 4 (monitoring start) | ongoing from week 6 | 8 weeks + |
| Phase 5 (audit prep) | 4-6 weeks | 14 weeks |
| Phase 6 Type 1 audit | 8-12 weeks | 26 weeks (~6 months) |
| Phase 6 Type 2 audit | 6 months after Type 1 | 12 months |

**Type 1 attestation: 6 months**
**Type 2 attestation: 12-18 months**

---

## 10. Effort & Cost Estimate

### Engineering effort
- Phase 1 code work: 40-60 hours (1-2 engineers, 4-6 weeks)
- Phase 2 documentation: 15-20 hours
- Phase 4 monitoring setup: 2-3 days
- Phase 5 audit support: 2-3 weeks of engineer Q&A

### External costs
- Auditor (Type 1): $20K-$60K depending on vendor
- Auditor (Type 2): $40K-$120K
- Pen test (annual): $15K-$40K
- Compliance tooling (Vanta, Drata, Tugboat): $10K-$25K/year
- Background screening: $50-$100 per hire

### Total first-year compliance budget
- Engineering: ~$50K-$100K opportunity cost
- External: $50K-$150K
- Total: **$100K-$250K**

---

## 11. Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Auditor finds material gap during fieldwork | Medium | High | Phase 5 dry run + gap remediation sprint |
| Vendor (OpenAI) won't share SOC2 report under NDA | Low | Medium | Use public SOC2 status page / Drata Trust Center |
| Engineering bandwidth diverted from product | High | Medium | Dedicated 0.5 FTE for 3 months |
| New regulation (EU AI Act, US AI EO) adds requirements mid-audit | Medium | High | Quarterly regulatory review, scope flex |
| Critical vuln found in `swiftide` or `reqwest` during audit | Medium | High | Dependabot + monthly review + escalation playbook |
| Insider threat from production-access engineer | Low | High | Background screening + access reviews + 2FA |

---

## 12. Owner Matrix

| Workstream | Primary | Secondary | Sign-off |
|---|---|---|---|
| Phase 1 code work | Senior eng | Eng team | Tech lead |
| Phase 2 docs | Senior eng + Sec lead | All eng | CTO |
| Phase 3 org process | CTO/COO | Sec lead | CEO |
| Phase 4 monitoring | DevOps | Sec lead | CTO |
| Phase 5 audit prep | Sec lead | All eng | CTO + CEO |
| Phase 6 external audit | Sec lead | All eng | CEO |

---

## 13. This Session's Immediate Actions (Engineering-owned, executable now)

Cherry-pick from the gap list — what we can do in the next 1-2 sessions:

### Session A (4-6 hours)
- G1 + G2 + G3: Audit logging (4h)
- G10: SECURITY.md (1h)
- G15: Threat model + control mapping draft (4h, can spread across sessions)
- G13: CHANGELOG.md initial entry (30min)

### Session B (4-6 hours)
- G4: Dependency scanning CI (3h)
- G6: Compliance test additions (1h)
- G9 desktop BYOK settings UI scaffolding (start, 4h)

### Session C+ (multi-session)
- G9 completion (8-12h)
- G7: Crash dump scrubbing
- G16: Monitoring dashboard

Remaining items are **leadership-owned** (Phases 3-6) and require people + budget decisions outside engineering's autonomy.

---

## 14. Success Criteria

### Phase 1-2 complete when
- All gaps G1-G15 closed with code + docs
- Internal threat model peer-reviewed
- 100% of in-scope code paths have audit logging

### Type 1 attestation when
- External auditor issues unqualified Type 1 report
- All identified control deficiencies remediated
- Management assertion signed

### Type 2 attestation when
- 6+ months of operating effectiveness evidence
- External auditor issues unqualified Type 2 report
- Continuous monitoring functioning at audit close

---

## 15. References

- AICPA TSP Section 100 (2017 Trust Services Criteria)
- AICPA SOC 2 Reporting Guide (2022)
- NIST 800-53 (control mappings)
- ISO 27001:2022 (parallel standard)
- Vanta / Drata SOC2 readiness guides
- Existing Maho docs: `docs/architecture/native-vs-web-boundary.md`, `docs/architecture/streaming-event-protocol.md`

---

## 16. Leadership Decisions (FINAL)

Made 2026-06-23. These decisions drive scope, timeline, and budget below.

| # | Decision | Choice | Implication |
|---|---|---|---|
| 1 | External auditor | **Big 4 (Deloitte/PwC/EY/KPMG)** | Premium pricing $40K-60K Type 1; market signal valued over cost |
| 2 | Compliance tooling | **In-house** | No Vanta/Drata. Engineering builds evidence automation via GitHub Actions + Terraform + custom scripts. ~1-2 FTE-months additional eng effort across Phases 1-4. **Note**: Big 4 auditors typically expect a tool — clarify with auditor during RFP whether in-house evidence is acceptable. Risk: may need to retrofit tooling under audit timeline pressure. |
| 3 | Type 1 vs Type 2 | **Type 1 only first cycle** | 6-month timeline. $25K-50K total external cost. Phase 6 Type 2 work deferred to next cycle (year 2). |
| 4 | TSC scope | **Security + Confidentiality** | Adds C1.1-C1.2 controls. Confidential data lifecycle (BYOK keys, conversation history): classification, retention, secure disposal. +10-15% audit scope. |
| 5 | Sec lead | **Fractional vCISO** | $40K-100K/year. Hire week 1-2 of Phase 1. Critical path for Phase 5 audit prep + Phase 6 audit support. |
| 6 | Background screening | **Checkr** | $35-100/check. Onboard before Phase 3.3 background-screening policy goes live. |
| 7 | Audit period start | **After Phase 1+2 complete (~week 6-8)** | Type 1 observation point is end of Phase 2. Auditor fieldwork begins Phase 5 (~week 10-14). Type 1 report by ~month 6. |

### Decision-driven adjustments to plan

**Phase 0 — IMMEDIATE (week 0-1, this week)**
- Hire fractional vCISO (Decision #5). Interview 3 candidates. ~$300/hour, expect 20-40 hours/month.
- Begin Checkr vendor onboarding (Decision #6). MSA + DPA review.
- Request RFPs from all 4 Big 4 firms (Decision #1). Compare bids in week 4-5.

**Phase 1 — Engineering controls (week 1-6)**
Adjusted from base plan to accommodate in-house tooling (Decision #2):
- **G1-G3** (audit logging): output to structured JSON, designated `target = "maho_agent::audit"` filter
- **NEW G19**: In-house evidence pipeline — GitHub Actions workflow that exports SOC2-relevant evidence (PR reviews, CI passes, dep scans, access logs) to S3 bucket weekly. Effort: 16-24 hours.
- **NEW G20**: Audit log aggregator — central log store (S3 + Athena, or simple Postgres) for `maho_agent::audit` target. Effort: 8-16 hours.
- **NEW G21**: Confidentiality TSC controls (Decision #4):
  - C1.1 — Data classification labels for BYOK keys, conversation history, agent execution metadata
  - C1.2 — Retention policy + secure disposal (e.g., automatic conversation deletion after N days, key zeroization on user deletion). Effort: 12-16 hours.

**Phase 2 — Documentation (week 2-5, parallel with P1)**
- **NEW G22**: Data classification policy doc (Decision #4). Effort: 3-4 hours.
- **NEW G23**: Data retention + deletion policy doc. Effort: 2-3 hours.
- All other docs as planned.

**Phase 3 — Organization (week 1-8, leadership-owned)**
- Fractional vCISO drives this phase per Decision #5.
- Checkr integration for new-hire screening + retroactive screening for production-access engineers (Decision #6).

**Phase 4 — Continuous monitoring (start week 6, ongoing)**
- In-house dashboard (Decision #2): Grafana + Loki or DataDog or AWS CloudWatch.
- Manual evidence collection cadence (quarterly access reviews, monthly risk register update).
- **Risk**: in-house evidence pipeline must be production-ready BEFORE Phase 6 audit fieldwork begins.

**Phase 5 — Audit prep (week 8-12)**
- Self-assessment using AICPA self-attestation worksheets
- Mock audit interviews with vCISO + CTO + senior eng
- Evidence packaging for Big 4 auditor (Decision #1) — Big 4 has rigorous evidence standards; may require additional formatting beyond what in-house tooling produces.

**Phase 6 — External Type 1 audit (week 12-26)**
- Big 4 fieldwork: ~4-8 weeks
- Findings + remediation + report: ~4 weeks
- Type 1 attestation delivered ~month 6

### Updated timeline (decisions applied)

| Week | Milestone |
|---|---|
| 0-1 | Hire vCISO, Checkr onboarding, Big 4 RFP launch |
| 1-6 | Phase 1 engineering controls (G1-G21) |
| 2-5 | Phase 2 documentation (G15, G22, G23, etc.) |
| 1-8 | Phase 3 org processes |
| 6+ | Phase 4 continuous monitoring begins |
| 8-12 | Phase 5 audit prep |
| 12-26 | Phase 6 Big 4 Type 1 audit |
| **~Month 6** | **Type 1 attestation issued** |

### Updated budget (Year 1, Type 1-only cycle)

| Item | Cost |
|---|---|
| Fractional vCISO (12 months × $5K avg) | $60K |
| Big 4 Type 1 audit | $40K-60K |
| Annual pen test (recommended pre-audit) | $20K-30K |
| Checkr background screening (5-10 engineers × $80 avg) | $400-$800 |
| In-house tooling infra (S3, Grafana, Postgres) | $1K-3K/year |
| Engineering opportunity cost (40-60h × $200/h) | $8K-12K |
| Documentation + policy templates (templates from IAPP, AICPA) | $1K-2K |
| **Total Year 1** | **$130K-170K** |

(Note: Big 4 + in-house tooling combo is ~$70K-120K MORE than boutique + Vanta combo. Trade-off accepted per Decision #1, #2.)

### Risks introduced by decision combination

**R1 (HIGH)**: Big 4 + in-house tooling mismatch
- Big 4 auditors typically expect evidence in standardized formats (Vanta exports, Drata reports). In-house evidence may require additional auditor-side processing time → potential audit delays.
- Mitigation: confirm acceptable evidence formats during RFP. Reserve 2 weeks contingency in Phase 5.

**R2 (MEDIUM)**: Confidentiality TSC + in-house tooling
- Confidentiality controls (data classification, retention) typically rely on tooling tags. In-house implementation must replicate this manually.
- Mitigation: invest in G21 (12-16h) properly; consider lightweight tagging via existing tracing infrastructure.

**R3 (MEDIUM)**: Fractional vCISO bandwidth
- 20-40 hours/month may be insufficient during Phase 5-6 audit prep + fieldwork.
- Mitigation: scope vCISO contract to include "audit period coverage" with surge availability.

**R4 (LOW)**: Type 1-only signals weakness to enterprise customers
- Some enterprise procurement requires Type 2.
- Mitigation: state in trust center that Type 2 audit period began [date], Type 2 attestation expected by [date+12 months]. Soft commit to Type 2 in cycle 2.

---

## 17. Immediate Next Actions (Week 0-1)

### Engineering-executable RIGHT NOW (no leadership input needed)

These can begin this session or next session, before vCISO hired or auditor selected:

| Task | Effort | Blocks audit? |
|---|---|---|
| G1+G2+G3: Audit logging (deny + allow + session + BYOK access) with `target = "maho_agent::audit"` | 4h | YES — CC7.1 direct violation |
| G10: `SECURITY.md` vulnerability disclosure policy | 1h | YES — CC8.1 evidence |
| G13: `CHANGELOG.md` initial structured entry for agent feature | 30min | NO — but Phase 5 needs it |
| G15: Threat model draft (`docs/security/agent-threat-model.md`) | 4h | YES — CC2.2 + audit walkthrough |
| G22: Data classification policy doc draft | 3h | YES — C1.1 (Confidentiality TSC) |
| G23: Data retention + deletion policy doc | 2h | YES — C1.2 |
| G4: `cargo audit` + `bun audit` + Dependabot wiring | 3h | YES — CC7.2 |
| G6: Compliance test additions to integration_tests.rs | 1h | NO — but Phase 5 evidence |

**Total session-executable engineering**: ~18-20 hours over 3-4 sessions

### Requires leadership/HR action (start parallel)

| Task | Owner | Timeline |
|---|---|---|
| Interview + hire fractional vCISO | CTO/CEO | Week 0-2 |
| Sign Big 4 RFP (engage Deloitte/PwC/EY/KPMG) | CFO/CTO | Week 0 send, Week 4 select |
| Checkr account setup + MSA | HR/CFO | Week 1-2 |
| Define security review approval matrix | CTO + vCISO | Week 2-3 |

### Recommended this-week execution order

**Day 1** (this session if user agrees):
- G1+G2+G3 audit logging
- G10 SECURITY.md
- G13 CHANGELOG.md
- G4 dep scanning CI

**Day 2** (next session):
- G15 threat model draft
- G22 data classification policy
- G23 retention policy
- G6 compliance test additions

**Day 3-4** (leadership in parallel):
- vCISO interviews
- Big 4 RFPs sent
- Checkr onboarding starts

### Success criteria for week 1
- [ ] All engineering quick wins (G1-G4, G6, G10, G13) committed
- [ ] At least 2 vCISO candidates interviewed
- [ ] Big 4 RFPs sent to all 4 firms
- [ ] Checkr MSA in legal review
- [ ] vCISO selected by week 2

---

## 18. Plan Status

**Created**: 2026-06-23
**Leadership decisions**: 7/7 captured (see §16)
**Engineering actions identified**: 23 gaps mapped to specific work items
**Next checkpoint**: end of Week 1 — re-assess based on vCISO availability + Big 4 RFP responses
**Owner accountability**: §12 matrix + §17 by-task assignment
**Plan stored**: `.omo/plans/soc2-readiness-implementation-plan.md` (UNCOMMITTED per OMO convention)
