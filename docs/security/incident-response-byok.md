# Incident Response Procedure: BYOK Secret Compromise

This document defines the incident response playbook for security incidents involving user BYOK keys or conversation history leakage, supporting SOC2 Security Criterion **CC7.4** (Incident Response).

---

## 1. Detection Phase

Incidents involving secret leakage are typically detected through:

1. **Structured Log Analysis:** Automated alerts triggered on S3 log aggregators scanning the `maho_agent::audit` stream for anomalous usage or leakages.
2. **Third-Party Notifications:** LLM vendors (OpenAI/Anthropic) notifying Maho that user API keys associated with the client user-agent have been leaked on GitHub or public portals.
3. **Internal Security Audits:** Regular scans of local log structures or crash dump databases detecting plaintext key footprints.
4. **User Submissions:** Responsible vulnerability disclosures received via `security@maho.dev`.

---

## 2. Triage & Severity Classification

Upon detection, the Security Lead will classify the incident:

| Severity Level | Definition | SLA for Initial Triage | Action Required |
|---|---|---|---|
| **P1 - Critical** | Clear evidence of active key extraction or systemic log leak exposing multiple keys. | 1 Hour | CTO + CSO emergency meeting; immediate rollout of key-clearing patches. |
| **P2 - High** | Single-user key exposure or crash dump containing a plaintext key fragment. | 4 Hours | Key revocation flow for the affected user profile. |
| **P3 - Medium** | Incorrect log annotation exposing Level 2 metadata (e.g. session paths). | 24 Hours | Scheduled patch release. |

---

## 3. Containment Playbook

If a BYOK API key compromise is confirmed, execute the following containment steps immediately:

1. **Invalidate Keys via Provider API:**
   - Scripted API revocation to OpenAI/Anthropic calling the revoke endpoint for compromised key hashes.
2. **Force Local Deletion:**
   - Force-update setting prefs on client profiles to invalidate the encrypted base64 storage string (`maho.ai.byok.openai.encrypted_b64`).
3. **Block Compromised User-Agents:**
   - Temporarily block the client agent instance from dispatching remote LLM requests from relay server headers.

---

## 4. Eradication & Remediation

1. **Locate & Purge:** Find all instances of plaintext keys within log streams or databases and trigger secure scrubbing.
2. **Patch Code Gaps:** Implement zeroization adjustments, fix crash dump filter registries, or fix logging omissions.
3. **Review Vulnerability Scans:** Run `bun run security-audit` to verify no dependencies were compromised as part of the attack vector.

---

## 5. Recovery

1. **User Notification SLA:** Notify affected users within **72 hours** of breach validation, providing instructions on key rotation.
2. **Client Session Restart:** Force the native agent to reload all sessions, forcing the UI settings page to re-prompt for fresh, secure BYOK inputs.

---

## 6. Post-Mortem Template (Lessons Learned)

Within 5 days of containment, the team must complete a post-mortem:
- **Incident Summary:** Date, timeline of events, scope.
- **Root Cause Analysis (RCA):** Why did the leakage occur? (e.g., missing `Zeroizing` wrapper).
- **Corrective Actions Matrix:** Tasks assigned to prevent recurrence.
