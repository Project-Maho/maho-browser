# Maho Vault Handoff Brief

Purpose: give future sessions a concise, safe starting point for Maho Vault work without overstating shipped security or marketing readiness.

## Status warning

Maho Vault and password-manager work remains an implementation track, not a launch or marketing claim. `docs/password-manager-security-fix-plan.md` continues to block customer-facing security promises until the old password-manager security issues are resolved and verified, especially plaintext disk storage, plaintext backup cleanup, reveal reauth, provider selection correctness, and FFI CRUD authorization gaps.

Use this brief as an implementation handoff only. Do not claim the Vault, password manager, or agent credential path is shipped, marketable, or ready for customer promises.

## Current source-backed state

### Shared Vault contracts

Source: `maho/crates/maho-types/src/vault.rs`

The shared contract file contains the Vault wire and policy types that future UI, FFI, storage, and agent code should use. Important names already present include:

- `VaultItemKind`
- `VaultAgentPolicy`
- `CredentialCapabilityGrant`
- `CredentialGrantDto`
- `AgentVaultItemDto`
- `VaultAuditRecord`
- `VaultStatus`

The same file also defines origin, passkey, grant state, revocation reason, audit operation, audit decision, and error-code types. Keep new Vault-facing APIs aligned with this contract file instead of inventing local duplicates.

### Vault manager state machine

Source: `maho/crates/maho-core/src/vault_manager.rs`

`VaultManager` currently owns locked/unlocked runtime state and bounded zeroizing key material. It tracks KDF metadata, wrapped user keys, wrapped recovery keys, item count, auto-lock settings, failed unlock throttling, and the default agent policy.

Security-relevant behavior already present:

- Active key material exists only while the manager is unlocked.
- `active_key()` fails closed when the Vault is not unlocked.
- Hydration drops prior state first, then hydrates locked metadata only.
- Partial or malformed persisted key slots are treated as corrupted Vault data.
- Auto-lock clears active key material and moves state to `AutoLocked`.
- Initialization returns opaque persisted slots for transactional storage by the caller.

### Durable Vault item CRUD surface

Source: `maho/crates/maho-core/src/maho_core/vault_items.rs`

The file states the intended CRUD contract directly: durable Vault CRUD requires an unlocked Vault, storage is the source of truth, and plaintext is never retained. The module is split across `add`, `mutate`, `query`, `shared`, and `use_secret` submodules to keep the concern boundary small.

Treat those three rules as non-negotiable when wiring UI, FFI, or agent access:

1. Require unlocked Vault before CRUD or secret-use operations.
2. Read durable truth from storage, not cached plaintext.
3. Never retain plaintext after the operation that needs it.

### Browser-process secret-use surface

Sources: `maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom`, `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc`, and `maho-chromium/browser/ui/webui/maho_settings/maho_settings_password_helpers.cc`

Current Settings exposes `UseVaultSecret(string item_id, uint64 expected_revision, SecretAction action) => (VaultOperationResult result)` for Saved Passwords row actions. This is the new sanctioned status-only browser-process secret-use surface for the password-manager plan:

- `SecretAction` currently covers copy and fill.
- The handler reuses the existing device reauth flow before use.
- The helper asks core/FFI for a short-lived secret buffer, performs the action inside the browser process, frees the buffer through `maho_vault_free_secret_buffer`, and returns only `VaultOperationResult` status.
- `VaultOperationResult` is status-only and has no password, secret, raw key, or plaintext field.

Clipboard lifetime is not an additional promise: the browser process frees its transient buffers after copy, but the operating system clipboard keeps the copied value until the user, another app, or the OS overwrites or clears it. Any expiring clipboard policy is future product work.

### Saved-password compatibility shims

Sources: `maho_settings.mojom`, `maho_settings_page_handler.cc`, and `maho_settings_password_helpers.cc`

The new Saved Passwords UI uses Vault metadata APIs plus `UseVaultSecret`; it does not call a plaintext reveal path. Remaining legacy saved-password methods are classified as follows:

| Method | Current classification | Notes |
|---|---|---|
| `GetSavedPasswords` | Compatibility shim | Still exposed on the old Settings Mojo surface; delegates to Vault-backed metadata list and omits plaintext. |
| `SearchPasswords` | Compatibility shim | Still exposed on the old Settings Mojo surface; filters Vault-backed metadata and omits plaintext. |
| `AddPassword` | Compatibility shim | Still exposed on the old Settings Mojo surface; reauthenticates, then delegates to the Vault-backed add adapter. |
| `UpdatePasswordUsername` / `DeletePassword` | Compatibility shim | Still exposed on the old Settings Mojo surface; reauthenticate, then delegate through Vault-backed metadata/update/delete helpers. |
| Legacy reveal-by-ID method | Removed WebUI surface | Settings Mojo, page handler, generated TS typings, and UI bridge no longer expose a password-string-returning reveal method. Copy/fill stays on `UseVaultSecret`, which returns only `VaultOperationResult`. |

Do not remove any remaining compatibility method until a fresh caller search and focused tests prove no caller remains. The plaintext reveal surface has that proof recorded in `.omo/start-work/artifacts/maho-password-manager/fix-legacy-revealpassword/`.

### Password import preview and commit jobs

Sources: `maho/crates/maho-import/src/password_import_job.rs`, `maho/crates/maho-ffi/src/import.rs`, `maho_settings.mojom`, `maho_settings_password_helpers.cc`, and `maho-chromium/browser/resources/maho_settings/react/passwords_import_*`

The Settings password import preview/commit job is implemented and uses explicit per-session job state rather than the onboarding `PENDING_PASSWORD_CSV` slot. The Settings flow selects a file, creates a source-format-specific preview, shows counts and safe messages/errors only, and commits by preview token after confirmation. Locked Vault commit attempts fail closed and do not fall back to legacy saved-password writes.

Supported user-export import formats are exactly:

| Source | Accepted formats | Platform statement |
|---|---|---|
| 1Password | CSV, 1PUX | Import is file-based and platform-independent wherever Maho can select the exported file. Export instructions may cover 1Password on macOS, Windows, and Linux, but there is no separate platform-specific Maho parser. |
| Bitwarden | Individual CSV, organization CSV, JSON | Import is file-based and platform-independent wherever Maho can select the exported file. Encrypted JSON and attachment ZIP exports are not accepted in this plan. |
| Apple Passwords | CSV/file export | Import is a user-selected CSV/file export only. Apple native app-to-app credential receipt is out of scope for this plan, and Maho must not promise a Windows-side Apple export path unless current Apple documentation proves one. |
| KeePass / KeePassXC | CSV | Import is file-based and platform-independent wherever Maho can select the exported file. Native `.kdbx` database import is not accepted in this plan. |

The import source list is separate from runtime fill providers. Apple is an import source only, not a `PasswordProviderKind` runtime provider.

### Agent Vault tools

Source: `maho/crates/maho-agent/src/vault_tools.rs`

The agent-side Vault tools are gated off by default behind the `vault-agent-tools` feature. Current code defines:

- `VaultCredentialHandle`, an opaque handle that serializes without the source item ID.
- `VaultCredentialLeaseManager`, which grants short-lived leases and consumes them on validation.
- Handle-based request parameters, so item IDs are not serialized into agent requests.
- Tests that confirm tools are not advertised unless the feature is enabled.
- Tests that confirm origin mismatch fails closed.
- Tests that confirm generated handles do not expose item IDs or secrets.

This is the right direction for agent safety, but it is not enough by itself. Keep item IDs, secret values, and stable credential identifiers out of prompts, model context, event logs, and renderer IPC.

## Known remaining blockers

- Task 15 runtime sentinel seeding remains blocked by missing browser CDP/runtime access. Static/artifact/log sweeps passed in the recorded evidence, but no runtime import/manual-add/copy/fill cleanliness is claimed until Maho is launched with a reachable CDP endpoint.
- Task 17 rendered visual QA remains blocked by missing current app/CDP artifact. Static IA review exists, but no final rendered visual/runtime pass is claimed until fresh screenshots are captured from a current build.
- Task 13 recorded a lock-backed Chromium GN duplicate-object blocker while trying to build the guided import UI target. Do not edit GN files as a workaround without explicit approval.

## Safe next-session checklist

Use this checklist before enabling, demoing, or marketing any agent-vault path:

1. Verify storage persistence. Confirm encrypted Vault items, KDF slots, wrapped user keys, wrapped recovery keys, revisions, tombstones, and migration paths persist correctly and fail closed on corruption.
2. Verify the FFI/UI boundary. Confirm renderer and WebUI surfaces never receive plaintext secrets, raw Vault keys, or stable item IDs, and that browser-process gates enforce unlocked state.
3. Verify audit emission. Confirm initialize, unlock, lock, item CRUD, grant issue/use/revoke, fill, TOTP fill, denied access, and failed access emit `VaultAuditRecord` data with no secrets.
4. Verify grant revocation. Confirm grants revoke on vault lock, session end, navigation, tab generation change, policy change, expiry, and max-use exhaustion.
5. Verify origin and frame binding. Confirm top origin, frame origin, tab id, tab generation, profile id, workspace id, session id, and task id are bound into capability checks.
6. Add negative tests. Cover locked Vault access, corrupted persisted slots, stale handles, reused handles, origin mismatch, frame mismatch, revoked grants, expired grants, and storage failure.
7. Check no secret-in-context leakage. Inspect logs, audit output, serialized agent messages, tool args, prompt context, traces, crash annotations, and test failures for secret values.

## Operating rules for future work

- Start from `maho-types/src/vault.rs`; do not create duplicate Vault DTOs in UI, FFI, or agent crates.
- Keep all secret access capability-based and handle-based.
- For Settings Saved Passwords, use `UseVaultSecret` for browser-process copy/fill and observe only status in the renderer.
- Keep import source support limited to the exact user-export formats listed above; do not add native receiver, keychain scraping, live database extraction, or unverified platform export promises.
- Treat storage as durable truth and runtime keys as ephemeral.
- Keep the default agent policy deny-first until all security checks above pass.
- Keep `docs/password-manager-security-fix-plan.md` as the marketing blocker until its critical issues are resolved and verified.
