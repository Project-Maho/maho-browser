# SQLite Encryption Decision (G8 Control)

**Owner:** Engineering + Security Lead  
**Date:** 2026-06-23  
**Status:** Decided (Accepted Residual Risk)

---

## 1. Context & Criteria

Under SOC2 Security (CC6.6) and Confidentiality (C1.1/C1.2) controls, user chat conversation history (stored locally in a SQLite database `maho.db`) must be protected from unauthorized access at rest. The team evaluated two approaches:
1. **Option A:** Encrypt the SQLite database at-rest using SQLCipher or a similar extension.
2. **Option B:** Restrict access to the SQLite file using strict OS-level profile permission boundaries, zeroize memory, and document the accepted residual risk.

---

## 2. Decision & Rationale

We have decided to adopt **Option B** (document and accept the residual risk of local plaintext chat DB) for the following reasons:

1. **Host-Level Isolation:** The SQLite database is stored inside the user's host profile sandbox (`~/Library/Application Support/Maho/` on macOS, standard secure app sandbox directories on iOS/Android). It is protected from other local user accounts by OS-level file permissions.
2. **BYOK Keys Isolated:** Core credentials (API keys) are NOT stored in the database in plaintext; they are stored encrypted using secure keyrings (KeyStore, Keychain) or OSCrypt. Only conversational history (Level 2 Private data) resides in SQLite.
3. **Complexity & Performance:** Introducing SQLCipher would add significant compilation complexity across the Rust-FFI-Chromium pipeline and impact UI rendering performance.

---

## 3. Residual Risk Mitigation

To offset the risk of plaintext local conversation files, the following controls are implemented:
- **Daily database pruning** of history older than 30 days.
- **Immediate deletion** from disk on user request (no soft-deletes).
- **Strict audit logs** for database access events.

---

## 4. Sign-off

- **CTO / Technical Lead:** Approved
- **vCISO / Security Lead:** Approved
