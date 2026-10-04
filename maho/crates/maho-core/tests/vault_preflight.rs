//! Typed, DB-aware Vault startup preflight coverage.
//!
//! Each case uses the public `MahoCore::vault_preflight_state` accessor intended
//! for the later FFI/C++ propagation slice. Tests use caller-supplied SQLCipher
//! keys rather than the process-global key slot, so they are deterministic when
//! run in parallel.

use chrono::{TimeZone, Utc};
use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::{VaultManager, VaultPreflightState};
use maho_storage::sqlite::{SqliteStorage, VaultDatabaseKey, VaultMetadataRow};
use rusqlite::Connection;
use std::path::Path;

const DATABASE_KEY: &str = "vault-preflight-database-key";
const MASTER_PASSPHRASE: &[u8] = b"correct horse battery staple";
const RECOVERY_SECRET: &[u8] = b"deterministic recovery secret";

fn preflight(path: &Path, key: VaultDatabaseKey<'_>) -> VaultPreflightState {
    MahoCore::new().vault_preflight_state(path, key)
}

fn create_encrypted_database(path: &Path) {
    drop(
        SqliteStorage::open_with_key(path.to_str().expect("UTF-8 test path"), DATABASE_KEY)
            .expect("create encrypted profile database"),
    );
}

#[test]
fn vault_preflight_reports_healthy_for_readable_database_without_vault() {
    let dir = tempfile::tempdir().expect("tempdir");
    let path = dir.path().join("healthy.db");
    create_encrypted_database(&path);

    assert_eq!(
        preflight(&path, VaultDatabaseKey::Available(DATABASE_KEY)),
        VaultPreflightState::Healthy
    );
}

#[test]
fn vault_preflight_reports_locked_for_valid_wrapped_key_slots() {
    let dir = tempfile::tempdir().expect("tempdir");
    let path = dir.path().join("locked.db");
    let storage =
        SqliteStorage::open_with_key(path.to_str().expect("UTF-8 test path"), DATABASE_KEY)
            .expect("create encrypted profile database");

    let mut manager = VaultManager::new();
    let now = Utc.with_ymd_and_hms(2026, 8, 12, 12, 0, 0).unwrap();
    let slots = manager
        .initialize(MASTER_PASSPHRASE, RECOVERY_SECRET, now)
        .expect("create valid wrapped-key slots");
    for (slot, payload) in [
        ("kdf_params", slots.kdf_params),
        ("wrapped_user_key", slots.wrapped_user_key),
        ("wrapped_recovery_key", slots.wrapped_recovery_key),
    ] {
        storage
            .save_vault_metadata(&VaultMetadataRow {
                slot: slot.to_string(),
                schema_version: 1,
                payload,
                updated_at: now.to_rfc3339(),
            })
            .expect("persist wrapped-key slot");
    }
    drop(storage);

    assert_eq!(
        preflight(&path, VaultDatabaseKey::Available(DATABASE_KEY)),
        VaultPreflightState::Locked
    );
}

#[test]
fn vault_preflight_reports_unrecoverable_key_for_existing_encrypted_database() {
    let dir = tempfile::tempdir().expect("tempdir");
    let path = dir.path().join("lost-key.db");
    create_encrypted_database(&path);

    assert_eq!(
        preflight(&path, VaultDatabaseKey::Unrecoverable),
        VaultPreflightState::UnrecoverableKey
    );
}

#[test]
fn vault_preflight_reports_structural_corruption_for_non_database_bytes() {
    let dir = tempfile::tempdir().expect("tempdir");
    let path = dir.path().join("corrupt.db");
    std::fs::write(&path, b"this is not a sqlite or sqlcipher database")
        .expect("write corrupt database fixture");

    assert_eq!(
        preflight(&path, VaultDatabaseKey::Available(DATABASE_KEY)),
        VaultPreflightState::StructuralCorruption
    );
}

#[test]
fn vault_preflight_reports_plaintext_residue_for_unencrypted_database() {
    let dir = tempfile::tempdir().expect("tempdir");
    let path = dir.path().join("plaintext.db");
    let plaintext = Connection::open(&path).expect("create plaintext SQLite database");
    plaintext
        .execute_batch(
            "CREATE TABLE passwords (
                id TEXT PRIMARY KEY,
                domain TEXT NOT NULL,
                username TEXT NOT NULL,
                password TEXT NOT NULL
            );
            INSERT INTO passwords VALUES ('id', 'example.com', 'alice', 'plaintext-secret');",
        )
        .expect("seed plaintext password residue");
    drop(plaintext);

    assert_eq!(
        preflight(&path, VaultDatabaseKey::Available(DATABASE_KEY)),
        VaultPreflightState::PlaintextResidue
    );
}
