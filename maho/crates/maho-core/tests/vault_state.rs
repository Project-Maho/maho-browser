//! Todo 10 — locked/unlocked Vault core state machine.
//!
//! Behavioral, secret-free coverage for every plan acceptance criterion driven
//! through the public `VaultManager` / `MahoCore` surface. Time is injected as an
//! explicit timestamp seam (no sleeps); storage-backed cases use real in-memory
//! SQLCipher via `MahoCore::with_storage`.

use base64::Engine as _;
use chrono::{Duration, TimeZone, Utc};
use std::cell::Cell;
use zeroize::Zeroizing;

use maho_core::maho_core::MahoCore;
use maho_core::oscrypt::{
    VaultDeviceBinding, VaultDevicePlatform, VaultDeviceProtector, VaultDeviceProtectorError,
};
use maho_core::vault_manager::{
    ProtectedEnvelopeReader, VaultHydrationOutcome, VaultHydrationSlots, VaultManager,
    VaultManagerError,
};
use maho_storage::sqlite::{EncryptedVaultItemRow, VaultDeviceKeyRow, VaultGrantRow};
use maho_types::events::shell_event::ShellEvent;
use maho_types::vault::{VaultItemId, VaultLockState};

const MASTER: &[u8] = b"correct horse battery staple";
const RECOVERY: &[u8] = b"recovery-emergency-phrase-deterministic";
const SENTINEL: &str = "S3NTINEL-maho-vault-9F4C";

fn t0() -> chrono::DateTime<Utc> {
    Utc.with_ymd_and_hms(2026, 7, 22, 12, 0, 0).unwrap()
}

fn initialized_unlocked() -> VaultManager {
    let mut manager = VaultManager::new();
    manager
        .initialize(MASTER, RECOVERY, t0())
        .expect("initialize must succeed");
    manager
}

struct TestDeviceProtector {
    secret: Vec<u8>,
}

impl TestDeviceProtector {
    fn new(secret: &[u8]) -> Self {
        Self {
            secret: secret.to_vec(),
        }
    }
}

impl VaultDeviceProtector for TestDeviceProtector {
    fn device_secret(&self) -> Result<Zeroizing<Vec<u8>>, VaultDeviceProtectorError> {
        Ok(Zeroizing::new(self.secret.clone()))
    }
}

struct FailingDeviceProtector;

impl VaultDeviceProtector for FailingDeviceProtector {
    fn device_secret(&self) -> Result<Zeroizing<Vec<u8>>, VaultDeviceProtectorError> {
        Err(VaultDeviceProtectorError::Unavailable)
    }
}

fn device_binding(profile_id: &str, device_id: &str) -> VaultDeviceBinding {
    VaultDeviceBinding::new(profile_id, device_id, VaultDevicePlatform::Desktop)
}

// --- Uninitialized / initialized / unlocked -------------------------------

#[test]
fn vault_state_fresh_manager_is_uninitialized() {
    let manager = VaultManager::new();
    assert_eq!(manager.status().lock_state, VaultLockState::Uninitialized);
    assert!(!manager.is_unlocked());
}

#[test]
fn vault_state_initialize_transitions_to_unlocked() {
    let manager = initialized_unlocked();
    assert_eq!(manager.status().lock_state, VaultLockState::Unlocked);
    assert!(manager.is_unlocked());
}

// --- Explicit lock / key clearing -----------------------------------------

#[test]
fn vault_state_explicit_lock_clears_key_and_reports_locked() {
    let mut manager = initialized_unlocked();
    manager.lock();
    assert_eq!(manager.status().lock_state, VaultLockState::Locked);
    assert!(!manager.is_unlocked());
    assert!(matches!(
        manager.active_key(),
        Err(VaultManagerError::VaultLocked)
    ));
}

#[test]
fn vault_state_unlock_after_lock_restores_unlocked() {
    let mut manager = initialized_unlocked();
    manager.lock();
    manager.unlock(MASTER, t0()).expect("unlock must succeed");
    assert_eq!(manager.status().lock_state, VaultLockState::Unlocked);
    assert!(manager.is_unlocked());
}

// --- Device-wrapper K0 contract -------------------------------------------

#[test]
fn vault_state_device_wrapper_rejects_different_profile_or_device_binding() {
    // Given: an unlocked Vault wrapped for one desktop profile/device pair.
    let mut manager = initialized_unlocked();
    let protector = TestDeviceProtector::new(b"device-protector-secret");
    let bundle = manager
        .wrap_active_key_for_device(&device_binding("profile-a", "device-a"), &protector)
        .expect("device wrapping succeeds");
    manager.lock();

    // When: the same device protector attempts a different profile or device binding.
    let wrong_profile = manager.unlock_with_device(
        &bundle,
        &device_binding("profile-b", "device-a"),
        &protector,
        t0(),
    );
    let wrong_device = manager.unlock_with_device(
        &bundle,
        &device_binding("profile-a", "device-b"),
        &protector,
        t0(),
    );

    // Then: neither binding can activate a Vault root key.
    assert!(wrong_profile.is_err());
    assert!(wrong_device.is_err());
    assert!(!manager.is_unlocked());
    assert!(matches!(
        manager.active_key(),
        Err(VaultManagerError::VaultLocked)
    ));
}

#[test]
fn vault_state_device_wrapper_debug_and_serialization_are_opaque() {
    // Given: a device-wrapped active root key.
    let manager = initialized_unlocked();
    let protector_secret = b"S3NTINEL-device-protector-secret";
    let protector = TestDeviceProtector::new(protector_secret);
    let bundle = manager
        .wrap_active_key_for_device(&device_binding("profile-a", "device-a"), &protector)
        .expect("device wrapping succeeds");

    // When: diagnostics and the storage payload are rendered.
    let debug = format!("{bundle:?}");
    let opaque_bytes = bundle
        .to_opaque_bytes()
        .expect("bundle serializes to opaque storage bytes");
    let serialized = String::from_utf8(opaque_bytes.clone()).expect("JSON bundle uses UTF-8");
    let storage_row = VaultDeviceKeyRow {
        device_id: "device-a".to_string(),
        schema_version: 1,
        wrapped_key: opaque_bytes,
        created_at: t0().to_rfc3339(),
        updated_at: t0().to_rfc3339(),
        revoked_at: None,
    };

    // Then: provider secret material is absent; only opaque envelope bytes serialize.
    assert!(debug.contains("[REDACTED]"));
    assert!(!debug.contains("S3NTINEL"));
    assert!(!serialized.contains("S3NTINEL"));
    assert!(!serialized.contains(&base64::prelude::BASE64_STANDARD.encode(protector_secret)));
    assert_eq!(
        maho_core::oscrypt::VaultDeviceWrapBundle::from_opaque_bytes(&storage_row.wrapped_key)
            .expect("opaque row payload round trips"),
        bundle
    );
}

#[test]
fn vault_state_device_unlock_provider_failure_leaves_manager_locked() {
    // Given: a locked initialized Vault and a provider that refuses its secret.
    let mut wrapped_manager = initialized_unlocked();
    let wrapper = wrapped_manager
        .wrap_active_key_for_device(
            &device_binding("profile-a", "device-a"),
            &TestDeviceProtector::new(b"protector"),
        )
        .expect("device wrapping succeeds");
    wrapped_manager.lock();

    // When: device unlock cannot obtain the protector secret.
    let result = wrapped_manager.unlock_with_device(
        &wrapper,
        &device_binding("profile-a", "device-a"),
        &FailingDeviceProtector,
        t0(),
    );

    // Then: failure is surfaced and no root key becomes active.
    assert!(matches!(result, Err(VaultManagerError::DeviceProtector(_))));
    assert!(!wrapped_manager.is_unlocked());
    assert!(matches!(
        wrapped_manager.active_key(),
        Err(VaultManagerError::VaultLocked)
    ));
}

// --- Failed-unlock rate limiting with retry_at ----------------------------

#[test]
fn vault_state_wrong_master_returns_invalid_credentials_and_counts() {
    let mut manager = initialized_unlocked();
    manager.lock();
    assert!(matches!(
        manager.unlock(b"wrong", t0()),
        Err(VaultManagerError::InvalidCredentials)
    ));
    assert_eq!(manager.status().failed_unlock_count, 1);
    assert!(manager.status().retry_at.is_none());
}

#[test]
fn vault_state_rate_limits_after_five_failures_with_retry_at() {
    let mut manager = initialized_unlocked();
    manager.lock();
    for _ in 0..5 {
        assert!(matches!(
            manager.unlock(b"wrong", t0()),
            Err(VaultManagerError::InvalidCredentials)
        ));
    }
    // Sixth attempt is blocked before any KDF work runs.
    assert!(matches!(
        manager.unlock(MASTER, t0()),
        Err(VaultManagerError::RateLimited)
    ));
    let status = manager.status();
    assert_eq!(status.failed_unlock_count, 5);
    assert!(status.retry_at.is_some());
    assert!(status.retry_at.unwrap() > t0());
}

#[test]
fn vault_state_rate_limit_window_is_finite_and_resets_after_expiry() {
    let mut manager = initialized_unlocked();
    manager.lock();
    for _ in 0..5 {
        let _ = manager.unlock(b"wrong", t0());
    }
    let retry_at = manager.status().retry_at.expect("retry_at populated");
    // After the finite window elapses the correct passphrase unlocks again.
    let after = retry_at + Duration::seconds(1);
    manager
        .unlock(MASTER, after)
        .expect("unlock after window must succeed");
    assert_eq!(manager.status().lock_state, VaultLockState::Unlocked);
    assert_eq!(manager.status().failed_unlock_count, 0);
    assert!(manager.status().retry_at.is_none());
}

#[test]
fn vault_state_successful_unlock_resets_failed_count() {
    let mut manager = initialized_unlocked();
    manager.lock();
    let _ = manager.unlock(b"wrong", t0());
    let _ = manager.unlock(b"wrong", t0());
    manager.unlock(MASTER, t0()).expect("unlock must succeed");
    assert_eq!(manager.status().failed_unlock_count, 0);
}

// --- Recovery unlock ------------------------------------------------------

#[test]
fn vault_state_recovery_unlock_succeeds_while_locked() {
    let mut manager = initialized_unlocked();
    manager.lock();
    manager
        .unlock_with_recovery(RECOVERY, t0())
        .expect("recovery unlock must succeed");
    assert_eq!(manager.status().lock_state, VaultLockState::Unlocked);
}

#[test]
fn vault_state_recovery_unlock_rejects_wrong_recovery() {
    let mut manager = initialized_unlocked();
    manager.lock();
    assert!(matches!(
        manager.unlock_with_recovery(b"not-the-recovery", t0()),
        Err(VaultManagerError::InvalidCredentials)
    ));
    assert_eq!(manager.status().lock_state, VaultLockState::Locked);
}

// --- Auto-lock (deterministic inactivity, no sleeps) ----------------------

#[test]
fn vault_state_auto_lock_after_inactivity_clears_key() {
    let mut manager = initialized_unlocked();
    manager.set_auto_lock_minutes(15);
    manager.record_activity(t0());
    let later = t0() + Duration::minutes(16);
    assert!(manager.evaluate_auto_lock(later));
    assert_eq!(manager.status().lock_state, VaultLockState::AutoLocked);
    assert!(matches!(
        manager.active_key(),
        Err(VaultManagerError::VaultLocked)
    ));
}

#[test]
fn vault_state_auto_lock_not_triggered_before_timeout() {
    let mut manager = initialized_unlocked();
    manager.set_auto_lock_minutes(15);
    manager.record_activity(t0());
    let within = t0() + Duration::minutes(14);
    assert!(!manager.evaluate_auto_lock(within));
    assert_eq!(manager.status().lock_state, VaultLockState::Unlocked);
}

// --- Locked denial happens BEFORE any downstream payload read -------------

struct CountingEnvelopeReader {
    calls: Cell<usize>,
    envelope: maho_types::vault::VaultCiphertextEnvelope,
}

impl ProtectedEnvelopeReader for CountingEnvelopeReader {
    fn load_envelope(
        &self,
        _item_id: &VaultItemId,
    ) -> Result<maho_types::vault::VaultCiphertextEnvelope, VaultManagerError> {
        self.calls.set(self.calls.get() + 1);
        Ok(self.envelope.clone())
    }
}

#[test]
fn vault_state_locked_read_denied_before_downstream_read() {
    let mut manager = initialized_unlocked();
    let item_id = VaultItemId::new();
    let envelope = manager
        .encrypt_and_wrap(SENTINEL.as_bytes(), &item_id)
        .expect("encrypt while unlocked");
    let reader = CountingEnvelopeReader {
        calls: Cell::new(0),
        envelope,
    };

    manager.lock();
    assert!(matches!(
        manager.read_protected(&reader, &item_id),
        Err(VaultManagerError::VaultLocked)
    ));
    assert_eq!(
        reader.calls.get(),
        0,
        "downstream payload reader must not run while locked"
    );
}

#[test]
fn vault_state_unlocked_read_invokes_downstream_once() {
    let manager = initialized_unlocked();
    let item_id = VaultItemId::new();
    let envelope = manager
        .encrypt_and_wrap(SENTINEL.as_bytes(), &item_id)
        .expect("encrypt while unlocked");
    let reader = CountingEnvelopeReader {
        calls: Cell::new(0),
        envelope,
    };
    let plaintext = manager
        .read_protected(&reader, &item_id)
        .expect("read while unlocked");
    assert_eq!(plaintext.as_slice(), SENTINEL.as_bytes());
    assert_eq!(reader.calls.get(), 1);
}

// --- Status never leaks secrets -------------------------------------------

#[test]
fn vault_state_status_debug_contains_no_secret_sentinel() {
    let manager = initialized_unlocked();
    let debug = format!("{:?}", manager.status());
    let json = serde_json::to_string(&manager.status()).expect("status serializes");
    assert!(!debug.contains(SENTINEL));
    assert!(!json.contains(SENTINEL));
    assert!(!debug.contains(std::str::from_utf8(MASTER).unwrap()));
    assert!(!json.contains(std::str::from_utf8(RECOVERY).unwrap()));
}

// --- MahoCore integration: persistence, hydration, sign-out, grants -------

fn core_with_temp_db(dir: &tempfile::TempDir, name: &str) -> MahoCore {
    maho_storage::sqlite::set_sqlcipher_key("vault-state-test-key").expect("configure key");
    let path = dir.path().join(name);
    MahoCore::new().with_storage(path.to_str().expect("utf8 path"))
}

#[test]
fn vault_state_maho_core_initialize_persists_and_hydrates_locked() {
    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = core_with_temp_db(&dir, "vault.sqlite");
    core.initialize_vault(MASTER, RECOVERY)
        .expect("initialize_vault must persist");
    assert_eq!(core.vault_status().lock_state, VaultLockState::Unlocked);
    let path = core.sqlite_db_path().expect("db path").to_string();
    drop(core);

    // Reopen the same encrypted DB; only metadata slots are hydrated (locked).
    let mut reopened = MahoCore::new().with_storage(&path);
    reopened
        .load_persisted_data()
        .expect("load_persisted_data must succeed");
    assert_eq!(reopened.vault_status().lock_state, VaultLockState::Locked);
    reopened
        .unlock_vault(MASTER)
        .expect("unlock after hydration must succeed");
    assert_eq!(reopened.vault_status().lock_state, VaultLockState::Unlocked);
}

#[test]
fn vault_state_device_initialization_persists_opaque_wrappers_and_restarts_locked() {
    // Given a durable Vault initialization with a desktop device protector.
    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = core_with_temp_db(&dir, "device-init.sqlite");
    let binding = device_binding("profile-a", "device-a");
    let protector_secret = b"device-protector-sentinel";
    let protector = TestDeviceProtector::new(protector_secret);

    // When the device-aware initialization commits.
    core.initialize_vault_with_device(MASTER, RECOVERY, &binding, &protector)
        .expect("device-aware initialization must persist");
    let storage = core.storage_ref().expect("storage configured");

    // Then the user/recovery metadata and opaque device wrapper exist together,
    // without any raw root key, protector, or recovery material in their bytes.
    let payloads = [
        storage
            .get_vault_metadata("kdf_params")
            .expect("kdf read")
            .expect("kdf persisted")
            .payload,
        storage
            .get_vault_metadata("wrapped_user_key")
            .expect("user read")
            .expect("user persisted")
            .payload,
        storage
            .get_vault_metadata("wrapped_recovery_key")
            .expect("recovery read")
            .expect("recovery persisted")
            .payload,
        storage
            .get_vault_device_key("device-a")
            .expect("device read")
            .expect("device persisted")
            .wrapped_key,
    ];
    for payload in payloads {
        for secret in [MASTER, RECOVERY, protector_secret, SENTINEL.as_bytes()] {
            assert!(
                !payload.windows(secret.len()).any(|bytes| bytes == secret),
                "raw secret must not be persisted outside an opaque envelope"
            );
        }
    }
    let path = core.sqlite_db_path().expect("db path").to_string();
    drop(core);

    let raw_database = std::fs::read(&path).expect("raw profile database read");
    for secret in [MASTER, RECOVERY, protector_secret, SENTINEL.as_bytes()] {
        assert!(
            !raw_database
                .windows(secret.len())
                .any(|bytes| bytes == secret),
            "raw profile database must not contain plaintext secret material"
        );
    }

    let mut reopened = MahoCore::new().with_storage(&path);
    reopened
        .load_persisted_data()
        .expect("restart hydration succeeds");
    assert_eq!(reopened.vault_status().lock_state, VaultLockState::Locked);
    reopened
        .unlock_vault_with_device(&binding, &protector)
        .expect("persisted device wrapper unlocks the hydrated Vault");
    assert_eq!(reopened.vault_status().lock_state, VaultLockState::Unlocked);
}

#[test]
fn vault_state_device_unlock_failures_stay_locked_without_automatic_fallback() {
    // Given a persisted device wrapper for one profile/device/provider pair.
    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = core_with_temp_db(&dir, "device-failure.sqlite");
    let binding = device_binding("profile-a", "device-a");
    let protector = TestDeviceProtector::new(b"device-protector");
    core.initialize_vault_with_device(MASTER, RECOVERY, &binding, &protector)
        .expect("device-aware initialization");
    let path = core.sqlite_db_path().expect("db path").to_string();
    drop(core);

    let mut reopened = MahoCore::new().with_storage(&path);
    reopened.load_persisted_data().expect("restart hydration");

    // When the binding or provider is invalid.
    for (attempt_binding, attempt_protector) in [
        (
            device_binding("profile-b", "device-a"),
            &protector as &dyn VaultDeviceProtector,
        ),
        (
            device_binding("profile-a", "device-b"),
            &protector as &dyn VaultDeviceProtector,
        ),
        (
            binding.clone(),
            &FailingDeviceProtector as &dyn VaultDeviceProtector,
        ),
    ] {
        assert!(
            reopened
                .unlock_vault_with_device(&attempt_binding, attempt_protector)
                .is_err(),
            "device unlock failure must surface"
        );
        // Then no user/recovery or SQLCipher fallback runs implicitly.
        assert_eq!(reopened.vault_status().lock_state, VaultLockState::Locked);
        assert_ne!(reopened.vault_status().lock_state, VaultLockState::Unlocked);
    }
}

#[test]
fn vault_state_recovery_rewrap_replaces_only_after_valid_recovery_material() {
    // Given a device wrapper protected by the original device protector.
    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = core_with_temp_db(&dir, "device-rewrap.sqlite");
    let binding = device_binding("profile-a", "device-a");
    let original_protector = TestDeviceProtector::new(b"original-device-protector");
    let replacement_protector = TestDeviceProtector::new(b"replacement-device-protector");
    core.initialize_vault_with_device(MASTER, RECOVERY, &binding, &original_protector)
        .expect("device-aware initialization");
    core.lock_vault().expect("lock before recovery rewrap");
    let before = core
        .storage_ref()
        .expect("storage")
        .get_vault_device_key("device-a")
        .expect("device row read")
        .expect("device row")
        .wrapped_key;

    // When recovery material is wrong, the committed wrapper remains byte-identical.
    assert!(core
        .rewrap_vault_device_with_recovery(
            b"wrong recovery material",
            &binding,
            &replacement_protector,
        )
        .is_err());
    let after_wrong = core
        .storage_ref()
        .expect("storage")
        .get_vault_device_key("device-a")
        .expect("device row read")
        .expect("device row")
        .wrapped_key;
    assert_eq!(after_wrong, before, "wrong recovery must not alter wrapper");
    assert_eq!(core.vault_status().lock_state, VaultLockState::Locked);

    // Then valid recovery replaces the device row and only the replacement protector unlocks it.
    core.rewrap_vault_device_with_recovery(RECOVERY, &binding, &replacement_protector)
        .expect("valid recovery rewrap");
    let after_valid = core
        .storage_ref()
        .expect("storage")
        .get_vault_device_key("device-a")
        .expect("device row read")
        .expect("device row")
        .wrapped_key;
    assert_ne!(after_valid, before, "replacement wrapper must be committed");
    core.lock_vault().expect("lock after rewrap");
    assert!(core
        .unlock_vault_with_device(&binding, &original_protector)
        .is_err());
    assert_eq!(core.vault_status().lock_state, VaultLockState::Locked);
    core.unlock_vault_with_device(&binding, &replacement_protector)
        .expect("replacement protector unlocks the rewrapped Vault");
}

fn active_grant_row(handle: &str, item_id: &str) -> VaultGrantRow {
    VaultGrantRow {
        handle: handle.to_string(),
        schema_version: 1,
        session_id: VaultItemId::new().to_string(),
        task_id: VaultItemId::new().to_string(),
        profile_id: "default".to_string(),
        workspace_id: VaultItemId::new().to_string(),
        tab_id: "tab-1".to_string(),
        tab_generation: 1,
        top_origin: "https://example.com".to_string(),
        frame_origin: "https://example.com".to_string(),
        item_id: item_id.to_string(),
        item_alias: VaultItemId::new().to_string(),
        allowed_fields: "[\"password\"]".to_string(),
        policy: "while_unlocked".to_string(),
        issued_at: t0().to_rfc3339(),
        expires_at: (t0() + Duration::hours(1)).to_rfc3339(),
        max_uses: 5,
        used_count: 0,
        state: "active".to_string(),
        revoked_at: None,
        revocation_reason: None,
    }
}

fn seed_item_and_grant(core: &MahoCore, handle: &str) -> String {
    let item_id = VaultItemId::new().to_string();
    let storage = core.storage_ref().expect("storage present");
    storage
        .save_vault_item(&EncryptedVaultItemRow {
            id: item_id.clone(),
            schema_version: 1,
            revision: 1,
            provider: "maho_native".to_string(),
            item_kind: "login".to_string(),
            envelope: vec![1, 2, 3],
            created_at: t0().to_rfc3339(),
            updated_at: t0().to_rfc3339(),
            deleted_at: None,
        })
        .expect("seed item");
    storage
        .save_vault_grant(&active_grant_row(handle, &item_id))
        .expect("seed grant");
    handle.to_string()
}

#[test]
fn vault_state_sign_out_locks_and_revokes_grants_session_ended() {
    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = core_with_temp_db(&dir, "signout.sqlite");
    core.initialize_vault(MASTER, RECOVERY).expect("init");
    seed_item_and_grant(&core, "grant-signout");

    core.handle_event(ShellEvent::SignOut);

    assert_eq!(core.vault_status().lock_state, VaultLockState::Locked);
    let grant = core
        .storage_ref()
        .unwrap()
        .get_vault_grant("grant-signout")
        .unwrap()
        .expect("grant present");
    assert_eq!(grant.state, "revoked");
    assert_eq!(grant.revocation_reason.as_deref(), Some("session_ended"));
}

#[test]
fn vault_state_explicit_lock_revokes_grants_vault_locked() {
    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = core_with_temp_db(&dir, "lock.sqlite");
    core.initialize_vault(MASTER, RECOVERY).expect("init");
    seed_item_and_grant(&core, "grant-lock");

    core.lock_vault().expect("lock ok");

    assert_eq!(core.vault_status().lock_state, VaultLockState::Locked);
    let grant = core
        .storage_ref()
        .unwrap()
        .get_vault_grant("grant-lock")
        .unwrap()
        .expect("grant present");
    assert_eq!(grant.state, "revoked");
    assert_eq!(grant.revocation_reason.as_deref(), Some("vault_locked"));
}

#[test]
fn vault_state_auto_lock_seam_revokes_grants() {
    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = core_with_temp_db(&dir, "autolock.sqlite");
    core.initialize_vault(MASTER, RECOVERY).expect("init");
    core.set_vault_auto_lock_minutes(15)
        .expect("auto-lock policy update succeeds");
    seed_item_and_grant(&core, "grant-autolock");

    let locked = core.evaluate_vault_auto_lock_at(Utc::now() + Duration::minutes(16));

    assert!(locked);
    assert_eq!(core.vault_status().lock_state, VaultLockState::AutoLocked);
    let grant = core
        .storage_ref()
        .unwrap()
        .get_vault_grant("grant-autolock")
        .unwrap()
        .expect("grant present");
    assert_eq!(grant.state, "revoked");
}

#[test]
fn vault_state_malformed_metadata_hydrates_as_corruption_not_unlocked() {
    let mut manager = VaultManager::new();
    let slots = VaultHydrationSlots {
        kdf_params: Some(b"{not valid json".to_vec()),
        wrapped_user_key: Some(b"garbage".to_vec()),
        wrapped_recovery_key: None,
        wrapped_account_key: None,
    };
    assert!(matches!(
        manager.hydrate_locked(slots),
        Err(VaultManagerError::CorruptedVaultData)
    ));
    // Fail-closed: never becomes usable on malformed slots.
    assert!(!manager.is_unlocked());
    assert_eq!(manager.status().lock_state, VaultLockState::Uninitialized);
}

#[test]
fn vault_state_missing_slots_hydrate_as_uninitialized() {
    let mut manager = VaultManager::new();
    let outcome = manager
        .hydrate_locked(VaultHydrationSlots::default())
        .expect("missing slots is not an error");
    assert!(matches!(outcome, VaultHydrationOutcome::Uninitialized));
    assert_eq!(manager.status().lock_state, VaultLockState::Uninitialized);
}

// --- Legacy password API fail-closed (no plaintext retention) -------------

#[test]
fn vault_state_add_password_shim_never_retains_plaintext() {
    let mut core = MahoCore::new();
    let synthesized = core.add_password_persisted(
        "example.com".to_string(),
        "user".to_string(),
        Some(SENTINEL.to_string()),
    );
    assert!(
        synthesized.password.is_none(),
        "shim must not carry plaintext"
    );
    assert!(core.list_passwords().is_empty(), "shim must not retain");
    assert!(core.password_by_id(&synthesized.id).is_none());
    assert!(core.search_passwords("example").is_empty());
}

#[test]
fn vault_state_legacy_password_events_fail_closed() {
    let mut core = MahoCore::new();
    let add = core.handle_event(ShellEvent::AddPassword {
        domain: "example.com".to_string(),
        username: "user".to_string(),
        password: SENTINEL.to_string(),
    });
    assert!(add.is_empty(), "AddPassword event must be a no-op");
    assert!(core.list_passwords().is_empty());

    let del = core.handle_event(ShellEvent::DeletePassword {
        password_id: "whatever".to_string(),
    });
    assert!(del.is_empty(), "DeletePassword event must be a no-op");
    assert!(!core.delete_password_persisted("whatever"));
    assert!(!core.update_password_username_persisted("whatever", "new".to_string()));
}

// --- Remediation regressions (AdversarialVerify findings 6.1/6.2/6.4/6.5/6.6/6.7) ---

// 6.7: auto-lock must occur through the real product tick path with injected time.
#[test]
fn vault_state_auto_lock_triggers_through_tick_with_injected_clock() {
    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = core_with_temp_db(&dir, "tick-autolock.sqlite");
    core.initialize_vault(MASTER, RECOVERY).expect("init");
    core.set_vault_auto_lock_minutes(15)
        .expect("auto-lock policy update succeeds");

    let base = Utc::now();
    let _ = core.tick_at(base);
    assert_eq!(core.vault_status().lock_state, VaultLockState::Unlocked);

    let _ = core.tick_at(base + Duration::minutes(16));
    assert_eq!(core.vault_status().lock_state, VaultLockState::AutoLocked);
}

// 6.6: reinitialization must be rejected without generating/overwriting key material.
#[test]
fn vault_state_reinitialize_is_rejected() {
    let mut manager = VaultManager::new();
    manager
        .initialize(MASTER, RECOVERY, t0())
        .expect("first init");
    assert!(matches!(
        manager.initialize(MASTER, RECOVERY, t0()),
        Err(VaultManagerError::AlreadyInitialized)
    ));

    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = core_with_temp_db(&dir, "reinit.sqlite");
    core.initialize_vault(MASTER, RECOVERY).expect("init");
    let before = core
        .storage_ref()
        .unwrap()
        .get_vault_metadata(maho_core::vault_manager::SLOT_WRAPPED_USER_KEY)
        .unwrap()
        .expect("user slot present")
        .payload;
    assert!(matches!(
        core.initialize_vault(MASTER, RECOVERY),
        Err(VaultManagerError::AlreadyInitialized)
    ));
    let after = core
        .storage_ref()
        .unwrap()
        .get_vault_metadata(maho_core::vault_manager::SLOT_WRAPPED_USER_KEY)
        .unwrap()
        .expect("user slot present")
        .payload;
    assert_eq!(
        before, after,
        "reinit must not overwrite persisted key material"
    );
}

// 6.1: initialize_vault without configured storage must not be a durable success.
#[test]
fn vault_state_initialize_without_storage_is_not_durable_success() {
    let mut core = MahoCore::new();
    assert!(
        core.initialize_vault(MASTER, RECOVERY).is_err(),
        "init without storage must fail"
    );
    assert_eq!(
        core.vault_status().lock_state,
        VaultLockState::Uninitialized
    );
}

// 6.4: hydrate on a live (unlocked) manager must force fail-closed on every outcome.
#[test]
fn vault_state_hydrate_on_unlocked_manager_forces_fail_closed() {
    let mut manager = initialized_unlocked();
    let outcome = manager
        .hydrate_locked(VaultHydrationSlots::default())
        .expect("missing slots => uninitialized");
    assert!(matches!(outcome, VaultHydrationOutcome::Uninitialized));
    assert!(!manager.is_unlocked());
    assert!(manager.active_key().is_err());
    assert_eq!(manager.status().lock_state, VaultLockState::Uninitialized);

    let mut manager2 = initialized_unlocked();
    let malformed = VaultHydrationSlots {
        kdf_params: Some(b"{bad".to_vec()),
        wrapped_user_key: Some(b"bad".to_vec()),
        wrapped_recovery_key: Some(b"bad".to_vec()),
        wrapped_account_key: None,
    };
    assert!(matches!(
        manager2.hydrate_locked(malformed),
        Err(VaultManagerError::CorruptedVaultData)
    ));
    assert!(!manager2.is_unlocked());
    assert!(manager2.active_key().is_err());
}

// 6.4: any partial slot set (including missing recovery) is corruption, not Locked.
#[test]
fn vault_state_hydrate_partial_slots_is_corruption() {
    let mut manager = VaultManager::new();
    let partial = VaultHydrationSlots {
        kdf_params: Some(b"{}".to_vec()),
        wrapped_user_key: Some(b"{}".to_vec()),
        wrapped_recovery_key: None,
        wrapped_account_key: None,
    };
    assert!(matches!(
        manager.hydrate_locked(partial),
        Err(VaultManagerError::CorruptedVaultData)
    ));
    assert!(!manager.is_unlocked());
    assert_eq!(manager.status().lock_state, VaultLockState::Uninitialized);
}

// 6.5: lock/sign-out on a never-initialized Vault must preserve Uninitialized.
#[test]
fn vault_state_signout_on_uninitialized_preserves_uninitialized() {
    let mut core = MahoCore::new();
    core.handle_event(ShellEvent::SignOut);
    assert_eq!(
        core.vault_status().lock_state,
        VaultLockState::Uninitialized
    );

    let mut manager = VaultManager::new();
    manager.lock();
    assert_eq!(manager.status().lock_state, VaultLockState::Uninitialized);
    manager.auto_lock();
    assert_eq!(manager.status().lock_state, VaultLockState::Uninitialized);
}
