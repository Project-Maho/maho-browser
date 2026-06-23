# Security Policy

## Vulnerability Disclosure Policy

We take the security of the Maho Browser and its ecosystem seriously. If you believe you have found a security vulnerability, please report it to us responsibly using the procedure outlined below.

### How to Report a Vulnerability

Please email your report to **security@maho.dev**.

In your report, please include:
- A detailed description of the vulnerability.
- Steps to reproduce or a working proof-of-concept (PoC).
- The potential impact of the vulnerability.
- Any suggested mitigations.

Do **NOT** disclose the vulnerability publicly or to any third party before we have resolved it.

### Response SLA

- **Acknowledgement:** We will acknowledge receipt of your report within **72 hours**.
- **Investigation:** We will investigate and provide status updates on our findings.
- **Resolution:** We aim to fix valid, in-scope vulnerabilities within **90 days** of acknowledgement.

### Scope

#### In-Scope
- `maho/crates/maho-agent/` (Rust runtime)
- BYOK secret handling & key storage (iOS Keychain, Android KeyStore, Desktop OSCrypt)
- Permission gating & audit logging infrastructure
- Desktop Chromium overlay adapters (`maho_unified_agent_adapter`)
- Mobile setting views and Native Shell bridges

#### Out-of-Scope
- Standalone sync relay server (`maho/relay/`)
- Upstream Chromium codebase
- Third-party LLM provider APIs (OpenAI, Anthropic)

### Bounty & Hall of Fame
We currently do not offer a monetary bug bounty program, but we will gladly credit reporters in our release notes and changelog (with their consent) once the vulnerability has been patched.
