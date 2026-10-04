//! Account-escrowed Vault (workstream W1): the account-seed wrap slot,
//! zero-input provisioning/unwrap at sign-in, and the migration primitive.

use std::path::Path;

use base64::Engine as _;
use chrono::{TimeZone, Utc};

use maho_core::maho_core::{MahoCore, VaultAccountEnsureOutcome};
use maho_core::vault_manager::{
    VaultHydrationOutcome, VaultHydrationSlots, VaultManager, VaultManagerError,
};
use maho_types::vault::{VaultItemId, VaultLockState};

const SEED: &[u8; 32] = b"escrow-account-seed-0123456789ab";
const OTHER_SEED: &[u8; 32] = b"other-account-seed-0123456789xyz";
const MASTER: &[u8] = b"account-escrow-master-passphrase";
const RECOVERY: &[u8] = b"account-escrow-recovery-secret";

fn now() -> chrono::DateTime<Utc> {
    Utc.with_ymd_and_hms(2026, 9, 24, 9, 0, 0).unwrap()
}

fn storage_backed_core(path: &Path) -> MahoCore {
    maho_storage::sqlite::set_sqlcipher_key("vault-account-escrow-test-key")
        .expect("configure key");
    MahoCore::new().with_storage(path.to_str().expect("utf8 path"))
}

fn fresh_core() -> (tempfile::TempDir, MahoCore) {
    let dir = tempfile::tempdir().expect("temp dir");
    let path = dir.path().join("vault-account-escrow.sqlite");
    let core = storage_backed_core(&path);
    (dir, core)
}

fn fresh_core_with_path() -> (tempfile::TempDir, std::path::PathBuf, MahoCore) {
    let dir = tempfile::tempdir().expect("temp dir");
    let path = dir.path().join("vault-account-escrow.sqlite");
    let core = storage_backed_core(&path);
    (dir, path, core)
}

#[test]
fn manager_account_kit_provisions_hydrates_and_unlocks() {
    let mut manager = VaultManager::new();
    let slots = manager
        .initialize_account(SEED, now())
        .expect("initialize account vault");
    assert!(!slots.kdf_params.is_empty());
    assert!(!slots.wrapped_account_key.is_empty());
    assert!(manager.is_unlocked());
    assert!(manager.has_account_wrap());
    assert!(manager.status().account_escrowed);

    manager.lock();
    assert!(!manager.is_unlocked());
    assert!(manager.has_account_wrap());

    let mut reopened = VaultManager::new();
    let outcome = reopened
        .hydrate_locked(VaultHydrationSlots {
            kdf_params: Some(slots.kdf_params),
            wrapped_user_key: None,
            wrapped_recovery_key: None,
            wrapped_account_key: Some(slots.wrapped_account_key),
        })
        .expect("hydrate account kit");
    assert_eq!(outcome, VaultHydrationOutcome::Locked);
    assert!(reopened.has_account_wrap());
    assert!(!reopened.is_unlocked());

    reopened
        .unlock_with_account(SEED, now())
        .expect("account unlock");
    assert!(reopened.is_unlocked());

    reopened.lock();
    assert!(matches!(
        reopened.unlock_with_account(OTHER_SEED, now()),
        Err(VaultManagerError::InvalidCredentials)
    ));
    assert!(!reopened.is_unlocked());
}

#[test]
fn provision_reverts_to_uninitialized_on_persist_failure() {
    let mut manager = VaultManager::new();
    manager
        .initialize_account(SEED, now())
        .expect("initialize account vault");
    manager.revert_uninitialized();
    assert_eq!(
        manager.status().lock_state,
        VaultLockState::Uninitialized
    );
    assert!(!manager.has_account_wrap());
}

#[test]
fn adopted_slots_decrypt_envelopes_from_the_provisioning_device() {
    let mut device_a = VaultManager::new();
    let slots_a = device_a
        .initialize_account(SEED, now())
        .expect("provision device A");
    let item_id = VaultItemId::new();
    let envelope = device_a
        .encrypt_and_wrap(b"adopted-secret", &item_id)
        .expect("encrypt on A");

    let mut device_b = VaultManager::new();
    let outcome = device_b
        .hydrate_locked(VaultHydrationSlots {
            kdf_params: Some(slots_a.kdf_params.clone()),
            wrapped_user_key: None,
            wrapped_recovery_key: None,
            wrapped_account_key: Some(slots_a.wrapped_account_key.clone()),
        })
        .expect("hydrate device B");
    assert_eq!(outcome, VaultHydrationOutcome::Locked);
    device_b
        .unlock_with_account(SEED, now())
        .expect("unlock device B");
    assert_eq!(
        device_b
            .decrypt_record(&envelope, &item_id)
            .expect("decrypt on B")
            .as_slice(),
        b"adopted-secret"
    );

    let mut forked = VaultManager::new();
    forked
        .initialize_account(SEED, now())
        .expect("fresh provision with the same seed");
    assert!(
        forked.decrypt_record(&envelope, &item_id).is_err(),
        "fresh provisioning must never reproduce another device's vault key"
    );
}

#[test]
fn fresh_device_adopts_escrow_record_keeping_the_same_vault_key() {
    let (_dir_a, mut core_a) = fresh_core();
    core_a.ensure_vault_for_account(SEED).expect("provision");
    let (kdf_params, wrapped) = core_a
        .export_vault_account_escrow()
        .expect("export escrow")
        .expect("escrow slots present");

    let (_dir_b, mut core_b) = fresh_core();
    let outcome = core_b
        .ensure_vault_for_account_with_escrow(SEED, Some(&kdf_params), Some(&wrapped))
        .expect("adopt");
    assert_eq!(outcome, VaultAccountEnsureOutcome::Adopted);
    assert_eq!(core_b.last_vault_account_outcome_code(), Some("adopted"));
    assert_eq!(core_b.vault_status().lock_state, VaultLockState::Unlocked);
    assert!(core_b.vault_status().account_escrowed);

    let (_dir_c, mut core_c) = fresh_core();
    let wrong = core_c.ensure_vault_for_account_with_escrow(
        OTHER_SEED,
        Some(&kdf_params),
        Some(&wrapped),
    );
    assert!(matches!(wrong, Err(VaultManagerError::InvalidCredentials)));
    assert_eq!(core_c.last_vault_account_outcome_code(), Some("failed"));
    assert_eq!(core_c.vault_status().lock_state, VaultLockState::Locked);
}

#[test]
fn escrow_record_is_ignored_when_a_local_vault_already_exists() {
    let (_dir, mut core) = fresh_core();
    core.ensure_vault_for_account(SEED).expect("provision");

    let outcome = core
        .ensure_vault_for_account_with_escrow(SEED, Some(b"{not-a-slot}"), Some(b"{not-a-slot}"))
        .expect("ensure ignores escrow for an existing vault");
    assert_eq!(outcome, VaultAccountEnsureOutcome::AlreadyUnlocked);
    assert_eq!(core.vault_status().lock_state, VaultLockState::Unlocked);
}

#[test]
fn legacy_vault_auto_migrates_on_first_passphrase_unlock_while_signed_in() {
    let (_dir, path, mut core) = fresh_core_with_path();
    core.initialize_vault(MASTER, RECOVERY)
        .expect("initialize legacy vault");
    core.lock_vault().expect("lock");

    let seed_b64 = base64::engine::general_purpose::STANDARD.encode(SEED);
    core.configure_sync_encryption_from_bootstrap("https://relay.test", &seed_b64)
        .expect("bootstrap");
    assert_eq!(core.last_vault_account_outcome_code(), Some("needs_migration"));
    assert!(!core.vault_status().account_escrowed);

    core.unlock_vault(MASTER).expect("passphrase unlock");
    assert!(
        core.vault_status().account_escrowed,
        "the passphrase unlock while signed in must add the account wrap"
    );

    core.lock_vault().expect("lock");
    drop(core);

    let mut reopened = storage_backed_core(&path);
    reopened.load_persisted_data().expect("load persisted data");
    assert!(reopened.vault_status().account_escrowed);
    let outcome = reopened.ensure_vault_for_account(SEED).expect("ensure");
    assert_eq!(outcome, VaultAccountEnsureOutcome::Unwrapped);
    assert_eq!(reopened.vault_status().lock_state, VaultLockState::Unlocked);
}

#[test]
fn ensure_provisions_account_vault_for_fresh_profile() {
    let (_dir, mut core) = fresh_core();
    assert_eq!(
        core.vault_status().lock_state,
        VaultLockState::Uninitialized
    );
    let outcome = core.ensure_vault_for_account(SEED).expect("ensure");
    assert_eq!(outcome, VaultAccountEnsureOutcome::Provisioned);
    assert_eq!(core.last_vault_account_outcome_code(), Some("provisioned"));
    let status = core.vault_status();
    assert_eq!(status.lock_state, VaultLockState::Unlocked);
    assert!(status.account_escrowed);
}

#[test]
fn ensure_is_idempotent_within_a_session() {
    let (_dir, mut core) = fresh_core();
    let first = core.ensure_vault_for_account(SEED).expect("ensure");
    assert_eq!(first, VaultAccountEnsureOutcome::Provisioned);
    let second = core.ensure_vault_for_account(SEED).expect("ensure again");
    assert_eq!(second, VaultAccountEnsureOutcome::AlreadyUnlocked);
    assert_eq!(core.last_vault_account_outcome_code(), Some("already_unlocked"));
    assert_eq!(core.vault_status().lock_state, VaultLockState::Unlocked);
}

#[test]
fn reopened_account_vault_unwraps_without_input() {
    let (_dir, path, mut core) = fresh_core_with_path();
    core.ensure_vault_for_account(SEED)
        .expect("provision");
    drop(core);

    let mut reopened = storage_backed_core(&path);
    reopened.load_persisted_data().expect("load persisted data");
    assert_eq!(reopened.vault_status().lock_state, VaultLockState::Locked);
    assert!(reopened.vault_status().account_escrowed);

    let outcome = reopened.ensure_vault_for_account(SEED).expect("ensure");
    assert_eq!(outcome, VaultAccountEnsureOutcome::Unwrapped);
    assert_eq!(reopened.last_vault_account_outcome_code(), Some("unwrapped"));
    assert_eq!(reopened.vault_status().lock_state, VaultLockState::Unlocked);
}

#[test]
fn wrong_account_seed_fails_closed() {
    let (_dir, mut core) = fresh_core();
    core.ensure_vault_for_account(SEED).expect("provision");
    core.lock_vault().expect("lock");

    let outcome = core.ensure_vault_for_account(OTHER_SEED);
    assert!(matches!(outcome, Err(VaultManagerError::InvalidCredentials)));
    assert_eq!(core.last_vault_account_outcome_code(), Some("failed"));
    assert_eq!(core.vault_status().lock_state, VaultLockState::Locked);
}

#[test]
fn legacy_passphrase_vault_needs_migration_then_becomes_zero_input() {
    let (_dir, path, mut core) = fresh_core_with_path();
    core.initialize_vault(MASTER, RECOVERY)
        .expect("initialize legacy vault");
    core.lock_vault().expect("lock");

    let outcome = core.ensure_vault_for_account(SEED).expect("ensure");
    assert_eq!(outcome, VaultAccountEnsureOutcome::NeedsMigration);
    assert_eq!(core.last_vault_account_outcome_code(), Some("needs_migration"));
    assert_eq!(core.vault_status().lock_state, VaultLockState::Locked);

    core.unlock_vault(MASTER).expect("unlock");
    core.wrap_account_key_into_active_vault(SEED)
        .expect("add account wrap");
    assert!(core.vault_status().account_escrowed);

    assert!(matches!(
        core.wrap_account_key_into_active_vault(SEED),
        Err(VaultManagerError::AlreadyInitialized)
    ));
    core.lock_vault().expect("lock");
    drop(core);

    let mut reopened = storage_backed_core(&path);
    reopened.load_persisted_data().expect("load persisted data");
    assert_eq!(reopened.vault_status().lock_state, VaultLockState::Locked);
    assert!(reopened.vault_status().account_escrowed);
    let outcome = reopened.ensure_vault_for_account(SEED).expect("ensure");
    assert_eq!(outcome, VaultAccountEnsureOutcome::Unwrapped);
    assert_eq!(reopened.vault_status().lock_state, VaultLockState::Unlocked);
}

#[test]
fn migrated_vault_still_unlocks_with_the_original_passphrase() {
    let (_dir, _path, mut core) = fresh_core_with_path();
    core.initialize_vault(MASTER, RECOVERY)
        .expect("initialize legacy vault");
    core.unlock_vault(MASTER).expect("unlock");
    core.wrap_account_key_into_active_vault(SEED)
        .expect("add account wrap");
    core.lock_vault().expect("lock");

    core.unlock_vault(MASTER).expect("passphrase unlock still works");
    assert_eq!(core.vault_status().lock_state, VaultLockState::Unlocked);
}

#[test]
fn sync_bootstrap_login_hook_provisions_and_reports_failures() {
    let (_dir, mut core) = fresh_core();
    let seed_b64 = base64::engine::general_purpose::STANDARD.encode(SEED);
    let room_id = core
        .configure_sync_encryption_from_bootstrap("https://relay.test", &seed_b64)
        .expect("bootstrap");
    assert!(!room_id.is_empty());
    assert_eq!(core.last_vault_account_outcome_code(), Some("provisioned"));
    assert_eq!(core.vault_status().lock_state, VaultLockState::Unlocked);
    assert!(core.vault_status().account_escrowed);

    // Sync setup succeeds even when the vault ensure fails closed.
    core.lock_vault().expect("lock");
    let other_b64 = base64::engine::general_purpose::STANDARD.encode(OTHER_SEED);
    core.configure_sync_encryption_from_bootstrap("https://relay.test", &other_b64)
        .expect("bootstrap still succeeds");
    assert_eq!(core.last_vault_account_outcome_code(), Some("failed"));
    assert!(matches!(
        core.last_vault_lifecycle_error(),
        Some(VaultManagerError::InvalidCredentials)
    ));
    assert_eq!(core.vault_status().lock_state, VaultLockState::Locked);
}
