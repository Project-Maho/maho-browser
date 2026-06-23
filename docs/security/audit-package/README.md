# Maho Sync Relay, External Audit Handoff Package

This README is the entry point for the external auditor engaged for the 2026 sync-relay security review. It points at the code that's in scope, the threat model the auditor should challenge, and the limitations we already know about.

For the formal scope and engagement terms, see `../audit-scope-2026.md`. This file is the **prep**, not the scope.

## 1. What the Auditor Receives

A read-only Git mirror of the Maho monorepo, pinned to a specific engagement-start commit. The auditor should focus on the paths below; everything else in the repo is out of scope (see `audit-scope-2026.md` §3).

### 1.1 Code paths in scope

| Path | Purpose | Languages |
|------|---------|-----------|
| `maho/relay/` | The relay server itself: HTTP/WebSocket entry, persistence, framing, rate limiting | Rust |
| `maho/relay/server/` | Routing, handlers, middleware | Rust |
| `maho/relay/entity/` | Persistence model definitions | Rust + SeaORM |
| `maho/relay/migration/` | Schema migrations | Rust + SeaORM |
| `maho/crates/maho-core/src/sync_crypto.rs` | Client-side E2EE: seal, unseal, key wrap | Rust |
| `maho/crates/maho-core/src/sync_manager.rs` | Client orchestration, session, retry | Rust |
| `maho/crates/maho-core/src/sync_models.rs` | Shared sync types | Rust |

### 1.2 Approximate size

These numbers are taken at the engagement-start commit. The auditor will get the exact figures with the source drop.

- **`maho/relay/`**: 46 Rust files, approximately 5,987 lines (including tests, excluding `target/`).
- **`maho/crates/maho-core/src/sync_*.rs`**: 3 files, approximately 1,089 lines.
- **Combined in-scope code**: roughly **7,000 lines of Rust**, plus migration SQL.

This is intentionally a small surface. The relay does one job; we want the auditor's hours spent reading carefully, not skimming.

### 1.3 Documentation in scope

- `maho/relay/README.md`: how the relay is built, run, and configured.
- `maho/relay/AGENTS.md`: developer-facing notes on the crate layout.
- `docs/security/audit-scope-2026.md`: this engagement's scope.
- This file.

## 2. Threat Model Summary

The auditor should challenge the model below, not just verify it. If we've drawn the trust boundary in the wrong place, that's a finding.

### 2.1 Trust boundary

The relay holds **end-to-end-encrypted blobs**. The relay does not have the keys to read them. Plaintext exists only on the user's devices.

```
[ Device A ]                                     [ Device B ]
   |                                                  |
   | plaintext                                        | plaintext
   v                                                  v
[ E2EE seal ]                                    [ E2EE seal ]
   |                                                  |
   | ciphertext + auth tag                            |
   v                                                  v
   +---------------------+    relay     +-----------------+
   |  POST /sync/blob    | -----------> |   GET /sync/blob |
   +---------------------+              +-----------------+
                                  |
                                  v
                          [ relay storage ]
                          (ciphertext only)
```

The relay sees:
- Account identifier (opaque ID, not email).
- Room identifier.
- Blob bytes (ciphertext).
- Timestamps and ordering metadata.

The relay does **not** see:
- The user's passphrase.
- The derived sync key.
- The plaintext of any blob.

### 2.2 Adversaries we worry about

1. **Malicious external attacker.** Goal: read or modify other users' data. Mitigation: E2EE, JWT-bound room ACL, TLS.
2. **Compromised relay operator (i.e., us).** Goal: read user data despite running the server. Mitigation: relay never holds keys. The audit should verify this is structurally true, not just policy.
3. **Compromised user device.** Out of scope; if the device is owned, the keys are owned. We don't claim otherwise.
4. **Network observer.** Mitigation: TLS plus E2EE. Even if TLS were stripped, ciphertext alone leaks little. Traffic-analysis side channels (size, timing) should be reviewed.
5. **Other authenticated users.** Goal: read or write rooms they don't belong to. Mitigation: room ownership and ACL checks on every request.

### 2.3 Critical claims to verify

If any of the following claims is false, that's a critical finding:

- C1. The relay cannot recover plaintext of a stored blob.
- C2. A user can only read or write rooms their JWT grants access to.
- C3. JWT signature verification is enforced on every authenticated route.
- C4. Argon2id parameters are strong enough to make offline brute-force of stolen passphrase hashes impractical at current attacker capability.
- C5. Re-using a session token after logout is rejected.
- C6. A device removed from the account cannot continue to read new blobs in that account's rooms.

### 2.4 Known limitations (we already accept these)

- We do not protect against an adversary running custom code on the user's own device.
- We do not blind metadata: the relay knows which account is talking to which room at which time.
- We do not currently rotate keys on user-perceived events (e.g., changed passphrase). That's tracked separately.
- We do not currently enforce per-device fingerprinting beyond JWT. A stolen, unrevoked token is as good as a logged-in device until expiry.

If the auditor thinks any of these "accepted" items shouldn't be accepted, we want that called out.

## 3. Prior Audits

**None.** This is the first external security audit of the Maho sync relay. There is no prior report to compare against, no list of previously fixed CVEs, and no remediation history. The auditor is starting from a clean baseline.

Any internal review notes Maho has produced are available on request, but none constitute a formal audit and they should not be treated as one.

## 4. Build and Run

The auditor needs a working relay to test against. The minimum path:

```bash
cd maho/relay
cargo test                           # unit and integration tests pass
cargo run --bin maho-relay-server    # local server on default port
```

A staging relay with synthetic accounts will be provided for live testing. Credentials will be sent over the engagement's secure channel, not in this repo.

## 5. Reporting Channel

Findings should land in the engagement's shared issue tracker as they're discovered, not held until a final report. Maho will triage daily during active testing weeks. For a critical finding (anything implying claim C1 through C6 above is false), the auditor should contact the Maho security lead directly via the channel established at kickoff.

## 6. Out of Scope (Reminder)

- Chromium fork.
- Mail client.
- Native shells (macOS, iOS, Android).
- Marketing site, billing flow, Stripe.
- Third-party Rust dependencies.

See `../audit-scope-2026.md` §3 for the authoritative list.

---

*Draft v0.1, last updated 2026-06-17.*
