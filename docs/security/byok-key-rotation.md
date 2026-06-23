# BYOK Key Rotation Procedure

## 1. Overview

This document specifies the rotation workflows and compliance mandates for Bring Your Own Key (BYOK) API credentials used by the Maho Browser Agent, supporting SOC2 Security and Confidentiality TSCs.

---

## 2. User-Facing Rotation Workflow

To rotate OpenAI or Anthropic API credentials, the user must execute the following:

1. **Generate New Key:** Create a new API credential through the vendor's developer dashboard (OpenAI or Anthropic).
2. **Update Settings:**
   - Navigate to `chrome://maho-settings/ai`.
   - Under the **BYOK Key Section**, select the relevant field (e.g. OpenAI BYOK Key).
   - Paste the new API key and click **Save**.
3. **Revoke Old Key:** Return to the provider's developer dashboard and revoke the previous key.

---

## 3. Engineering Workflows & Internal Triggers

At the codebase level, rotation is handled dynamically:

1. **Preference Write:**
   - Mojo endpoint `SetBYOKKey` encrypts the input immediately using OSCrypt and writes base64 bytes to preferences.
2. **Session Eviction:**
   - Any active agent session is flagged. The next task/turn instantiation loads the fresh key from `PrefService` snapshot.
3. **Volatile Scrubbing:**
   - Previous keys residing in memory are overwritten via Zeroize traits on dropping `ZeroizedString`.
   - Evidence Location: [maho_unified_agent_adapter.cc](file:///Users/indo/code/project/maho-workspace/maho-chromium/browser/ai/maho_unified_agent_adapter.cc#L237-L252) cleans up the temporary decrypt buffers immediately.

---

## 4. Compliance Rotation SLA

- **Mandatory Rotation Cadence:** It is recommended that users rotate keys every **90 days** to minimize leakage risk.
- **Leakage Response SLA:** If a key is flagged as leaked (e.g. via Git Guardian, OpenAI notification), the key must be rotated or cleared within **24 hours**.
