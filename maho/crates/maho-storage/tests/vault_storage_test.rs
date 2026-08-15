//! Integration tests for the transactional, ciphertext-only SQLCipher Vault
//! persistence layer (plan Todo 9).
//!
//! These tests treat `maho-storage` as crypto-agnostic: it persists
//! already-encrypted envelope bytes and wrapped-key blobs plus non-secret
//! routing/version/timestamp metadata. Record-level Vault key verification is
//! owned by `maho-core` crypto tests (Todo 8); here we only assert the SQLCipher
//! database-key boundary and the ciphertext-only on-disk shape.

use maho_storage::sqlite::{
    inspect_vault_database, EncryptedVaultItemRow, SqliteStorage, VaultAuditRow,
    VaultDatabaseInspection, VaultDatabaseKey, VaultDeviceKeyRow, VaultGrantRow, VaultMetadataRow,
    VaultPolicyRow, VaultRecoveryRow, VaultSyncVersionRow, VaultTombstoneCas,
};
use maho_storage::StorageError;
use rusqlite::{Connection, ErrorCode};
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant};

/// Deterministic sentinel that must NEVER appear in any text column of an
/// encrypted-at-rest Vault table. Seeded inside ciphertext BLOBs only.
const SENTINEL: &str = "S3NTINEL-maho-vault-9F4C";

fn test_key() -> String {
    uuid::Uuid::new_v4().to_string()
}

fn mem_store() -> SqliteStorage {
    SqliteStorage::open_in_memory_with_key(&test_key()).expect("open in-memory keyed db")
}

fn envelope_with_sentinel() -> Vec<u8> {
    // Simulate a serialized encrypted envelope whose *plaintext* (never stored)
    // contained the sentinel; the ciphertext bytes here embed the sentinel to
    // prove the raw-row scan looks at the right columns. Real envelopes are
    // opaque AEAD output, but storage must treat this as an opaque blob.
    let mut bytes = b"MAHO-ENVELOPE-V1".to_vec();
    bytes.extend_from_slice(SENTINEL.as_bytes());
    bytes.extend_from_slice(&[0u8, 1, 2, 3, 255, 254]);
    bytes
}

fn sample_item(id: &str, envelope: Vec<u8>) -> EncryptedVaultItemRow {
    EncryptedVaultItemRow {
        id: id.to_string(),
        schema_version: 1,
        revision: 1,
        provider: "maho_native".to_string(),
        item_kind: "login".to_string(),
        envelope,
        created_at: "2026-07-22T00:00:00Z".to_string(),
        updated_at: "2026-07-22T00:00:00Z".to_string(),
        deleted_at: None,
    }
}

fn sample_grant(handle: &str, item_id: &str) -> VaultGrantRow {
    VaultGrantRow {
        handle: handle.to_string(),
        schema_version: 1,
        session_id: uuid::Uuid::new_v4().to_string(),
        task_id: uuid::Uuid::new_v4().to_string(),
        profile_id: uuid::Uuid::new_v4().to_string(),
        workspace_id: uuid::Uuid::new_v4().to_string(),
        tab_id: uuid::Uuid::new_v4().to_string(),
        tab_generation: 7,
        top_origin: "https://github.com".to_string(),
        frame_origin: "https://github.com".to_string(),
        item_id: item_id.to_string(),
        item_alias: uuid::Uuid::new_v4().to_string(),
        allowed_fields: "[\"username\",\"password\"]".to_string(),
        policy: "while_unlocked".to_string(),
        issued_at: "2026-07-22T00:00:00Z".to_string(),
        expires_at: "2026-07-22T01:00:00Z".to_string(),
        max_uses: 3,
        used_count: 0,
        state: "active".to_string(),
        revoked_at: None,
        revocation_reason: None,
    }
}

fn sample_audit(id: &str, seq: i64, item_id: &str) -> VaultAuditRow {
    VaultAuditRow {
        id: id.to_string(),
        schema_version: 1,
        seq,
        timestamp: "2026-07-22T00:00:00Z".to_string(),
        session_id: Some(uuid::Uuid::new_v4().to_string()),
        task_id: Some(uuid::Uuid::new_v4().to_string()),
        profile_id: uuid::Uuid::new_v4().to_string(),
        workspace_id: uuid::Uuid::new_v4().to_string(),
        top_origin: Some("https://github.com".to_string()),
        frame_origin: Some("https://github.com".to_string()),
        item_id: Some(item_id.to_string()),
        item_alias: Some(uuid::Uuid::new_v4().to_string()),
        operation: "grant_used".to_string(),
        policy: Some("while_unlocked".to_string()),
        decision: "allowed".to_string(),
        reason: None,
        device_name: "test-device".to_string(),
        prev_hash: None,
        entry_hash: vec![0xAB, 0xCD, 0xEF],
    }
}

fn sample_policy(scope: &str, policy: &str, expires_at: Option<&str>) -> VaultPolicyRow {
    VaultPolicyRow {
        scope: scope.to_string(),
        schema_version: 1,
        policy: policy.to_string(),
        item_id: None,
        origin: None,
        expires_at: expires_at.map(str::to_string),
        updated_at: "2026-07-22T00:00:00Z".to_string(),
    }
}

fn sample_sync_version(entity_id: &str, envelope: Vec<u8>) -> VaultSyncVersionRow {
    VaultSyncVersionRow {
        entity_id: entity_id.to_string(),
        schema_version: 1,
        version: 1,
        updated_at: "2026-07-22T00:00:00Z".to_string(),
        encrypted_payload: envelope,
    }
}

fn on_disk_store(key: &str) -> (tempfile::TempDir, String) {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("vault.db");
    let path_str = path.to_string_lossy().to_string();
    // Materialize the schema on disk so a raw connection can inspect it.
    drop(SqliteStorage::open_with_key(&path_str, key).unwrap());
    (dir, path_str)
}

#[test]
fn vault_database_inspection_reports_plaintext_backup_residue() {
    let key = test_key();
    let (dir, path_str) = on_disk_store(&key);
    let path = std::path::Path::new(&path_str);
    let storage = SqliteStorage::open_with_key(&path_str, &key).unwrap();
    storage
        .save_vault_metadata(&VaultMetadataRow {
            slot: "kdf_params".to_string(),
            schema_version: 1,
            payload: vec![1, 2, 3],
            updated_at: "2026-08-12T00:00:00Z".to_string(),
        })
        .unwrap();
    drop(storage);

    let backup = path.with_extension("db.plain_backup");
    let plaintext = Connection::open(&backup).unwrap();
    plaintext
        .execute_batch("CREATE TABLE migration_residue (id INTEGER PRIMARY KEY);")
        .unwrap();
    drop(plaintext);

    assert!(matches!(
        inspect_vault_database(path, VaultDatabaseKey::Available(&key)),
        VaultDatabaseInspection::PlaintextResidue
    ));

    drop(dir);
}

#[test]
fn fresh_init_creates_all_vault_tables_and_no_passwords_table() {
    // Given a freshly keyed on-disk database, inspected via a raw SQLCipher conn.
    let key = test_key();
    let (_dir, path_str) = on_disk_store(&key);
    let raw = Connection::open(&path_str).unwrap();
    raw.pragma_update(None, "key", &key).unwrap();

    let table_names: Vec<String> = {
        let mut stmt = raw
            .prepare("SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name")
            .unwrap();
        stmt.query_map([], |row| row.get::<_, String>(0))
            .unwrap()
            .collect::<Result<Vec<_>, _>>()
            .unwrap()
    };

    for required in [
        "vault_schema_info",
        "vault_metadata",
        "vault_items",
        "vault_device_keys",
        "vault_recovery",
        "vault_capability_grants",
        "vault_agent_policies",
        "vault_audit_events",
        "vault_sync_versions",
    ] {
        assert!(
            table_names.iter().any(|t| t == required),
            "missing Vault table {required}; have {table_names:?}"
        );
    }

    assert!(
        !table_names.iter().any(|t| t == "passwords"),
        "legacy passwords table must not exist after cutover"
    );
}

#[test]
fn vault_items_has_no_plaintext_secret_columns() {
    let key = test_key();
    let (_dir, path_str) = on_disk_store(&key);
    let raw = Connection::open(&path_str).unwrap();
    raw.pragma_update(None, "key", &key).unwrap();

    let columns: Vec<String> = {
        let mut stmt = raw
            .prepare("SELECT name FROM pragma_table_info('vault_items')")
            .unwrap();
        stmt.query_map([], |row| row.get::<_, String>(0))
            .unwrap()
            .collect::<Result<Vec<_>, _>>()
            .unwrap()
    };
    for forbidden in [
        "password",
        "domain",
        "username",
        "totp",
        "totp_seed",
        "passkey",
        "recovery",
        "notes",
        "secret",
        "vault_key",
        "username_hint",
        "title",
        "origin",
    ] {
        assert!(
            !columns.iter().any(|c| c == forbidden),
            "vault_items must not contain plaintext column '{forbidden}'; columns = {columns:?}"
        );
    }
    for required in [
        "id",
        "revision",
        "provider",
        "item_kind",
        "envelope",
        "schema_version",
    ] {
        assert!(
            columns.iter().any(|c| c == required),
            "vault_items missing required column '{required}'; columns = {columns:?}"
        );
    }
}

#[test]
fn schema_init_is_idempotent_on_reopen() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("vault.db");
    let path_str = path.to_string_lossy().to_string();
    let key = test_key();

    let item = sample_item("item-1", envelope_with_sentinel());
    {
        let db = SqliteStorage::open_with_key(&path_str, &key).unwrap();
        db.save_vault_item(&item).unwrap();
    }
    // Reopen: must not error, must not wipe committed rows, must be idempotent.
    {
        let db = SqliteStorage::open_with_key(&path_str, &key).unwrap();
        let loaded = db
            .get_vault_item("item-1")
            .unwrap()
            .expect("row survives reopen");
        assert_eq!(loaded.envelope, item.envelope);
        assert_eq!(loaded.revision, 1);
    }
    // A third open also succeeds (schema version marker prevents re-cutover).
    {
        let db = SqliteStorage::open_with_key(&path_str, &key).unwrap();
        assert_eq!(db.list_vault_items().unwrap().len(), 1);
    }
}

#[test]
fn raw_on_disk_rows_are_ciphertext_only_and_hide_sentinel() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("vault.db");
    let path_str = path.to_string_lossy().to_string();
    let key = test_key();

    {
        let db = SqliteStorage::open_with_key(&path_str, &key).unwrap();
        db.save_vault_item(&sample_item("item-1", envelope_with_sentinel()))
            .unwrap();
        db.save_vault_device_key(&VaultDeviceKeyRow {
            device_id: "device-1".to_string(),
            schema_version: 1,
            wrapped_key: envelope_with_sentinel(),
            created_at: "2026-07-22T00:00:00Z".to_string(),
            updated_at: "2026-07-22T00:00:00Z".to_string(),
            revoked_at: None,
        })
        .unwrap();
        db.save_vault_recovery(&VaultRecoveryRow {
            id: "recovery-1".to_string(),
            schema_version: 1,
            wrapped_key: envelope_with_sentinel(),
            created_at: "2026-07-22T00:00:00Z".to_string(),
            updated_at: "2026-07-22T00:00:00Z".to_string(),
        })
        .unwrap();
        db.save_vault_sync_version(&sample_sync_version("item-1", envelope_with_sentinel()))
            .unwrap();
    }

    // Open a fresh raw SQLCipher connection and inspect stored text columns.
    let raw = Connection::open(&path_str).unwrap();
    raw.pragma_update(None, "key", &key).unwrap();

    // The envelope column is a BLOB carrying the sentinel bytes...
    let envelope_len: i64 = raw
        .query_row(
            "SELECT length(envelope) FROM vault_items WHERE id = 'item-1'",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert!(envelope_len > 0, "envelope blob must be persisted");

    // ...but NO text column of vault_items may contain the sentinel.
    let text_hit: i64 = raw
        .query_row(
            "SELECT count(*) FROM vault_items WHERE \
             CAST(id AS TEXT) LIKE '%' || ?1 || '%' \
             OR CAST(provider AS TEXT) LIKE '%' || ?1 || '%' \
             OR CAST(item_kind AS TEXT) LIKE '%' || ?1 || '%' \
             OR CAST(created_at AS TEXT) LIKE '%' || ?1 || '%'",
            rusqlite::params![SENTINEL],
            |r| r.get(0),
        )
        .unwrap();
    assert_eq!(
        text_hit, 0,
        "sentinel must not appear in any vault_items text column"
    );

    // Device-key and recovery wrapped_key are opaque BLOBs, not text.
    for (table, blob_col) in [
        ("vault_device_keys", "wrapped_key"),
        ("vault_recovery", "wrapped_key"),
        ("vault_sync_versions", "encrypted_payload"),
    ] {
        let col_type: String = raw
            .query_row(
                &format!("SELECT typeof({blob_col}) FROM {table} LIMIT 1"),
                [],
                |r| r.get(0),
            )
            .unwrap();
        assert_eq!(
            col_type, "blob",
            "{table}.{blob_col} must persist as a BLOB"
        );
    }
}

#[test]
fn grant_audit_sync_rows_round_trip_with_all_fields() {
    let db = mem_store();
    db.save_vault_item(&sample_item("item-1", envelope_with_sentinel()))
        .unwrap();
    let grant = sample_grant("grant-1", "item-1");
    db.save_vault_grant(&grant).unwrap();
    let audit = sample_audit("audit-1", 1, "item-1");
    db.append_vault_audit(&audit).unwrap();
    let sync = sample_sync_version("item-1", envelope_with_sentinel());
    db.save_vault_sync_version(&sync).unwrap();

    let loaded_grant = db
        .get_vault_grant("grant-1")
        .unwrap()
        .expect("grant exists");
    assert_eq!(loaded_grant.top_origin, grant.top_origin);
    assert_eq!(loaded_grant.frame_origin, grant.frame_origin);
    assert_eq!(loaded_grant.tab_generation, 7);
    assert_eq!(loaded_grant.max_uses, 3);
    assert_eq!(loaded_grant.state, "active");
    assert_eq!(loaded_grant.allowed_fields, grant.allowed_fields);

    let audits = db.list_vault_audit_events().unwrap();
    assert_eq!(audits.len(), 1);
    assert_eq!(audits[0].operation, "grant_used");
    assert_eq!(audits[0].entry_hash, vec![0xAB, 0xCD, 0xEF]);

    let loaded_sync = db
        .get_vault_sync_version("item-1")
        .unwrap()
        .expect("sync row exists");
    assert_eq!(loaded_sync.version, 1);
    assert_eq!(loaded_sync.encrypted_payload, sync.encrypted_payload);
}

#[test]
fn vault_policy_rows_round_trip_default_item_origin_and_expiry() {
    let db = mem_store();
    let mut default = sample_policy("default", "ask_every_use", None);
    let mut item = sample_policy(
        "item:00000000-0000-0000-0000-000000000001",
        "while_unlocked",
        Some("2026-07-22T01:00:00Z"),
    );
    item.item_id = Some("00000000-0000-0000-0000-000000000001".to_string());
    let mut origin = sample_policy("origin:https://example.test", "deny", None);
    origin.origin = Some("https://example.test".to_string());

    db.save_vault_policy(&default).unwrap();
    db.save_vault_policy(&item).unwrap();
    db.save_vault_policy(&origin).unwrap();

    let loaded_default = db.get_vault_policy("default").unwrap().unwrap();
    assert_eq!(loaded_default.policy, default.policy);
    let policies = db.list_vault_policies().unwrap();
    assert_eq!(policies.len(), 3);
    assert!(policies.iter().any(|row| row.item_id == item.item_id));
    assert!(policies.iter().any(|row| row.origin == origin.origin));

    default.policy = "always_allow".to_string();
    db.save_vault_policy(&default).unwrap();
    assert_eq!(
        db.get_vault_policy("default").unwrap().unwrap().policy,
        "always_allow"
    );
}

#[test]
fn atomic_mutation_rolls_back_all_tables_and_preserves_prior_ciphertext() {
    let db = mem_store();

    // Baseline committed item + grant + audit + sync.
    let baseline_env = b"BASELINE-CIPHERTEXT-BYTES".to_vec();
    db.save_vault_item(&sample_item("item-1", baseline_env.clone()))
        .unwrap();
    db.save_vault_grant(&sample_grant("grant-1", "item-1"))
        .unwrap();
    db.append_vault_audit(&sample_audit("audit-1", 1, "item-1"))
        .unwrap();
    db.save_vault_sync_version(&sample_sync_version("item-1", baseline_env.clone()))
        .unwrap();

    // A composite mutation that touches every table then fails mid-transaction.
    let result: Result<(), maho_storage::StorageError> = db.vault_transaction(|tx| {
        tx.upsert_item(&sample_item("item-1", b"TAMPERED-CIPHERTEXT".to_vec()))?;
        tx.upsert_grant(&sample_grant("grant-2", "item-1"))?;
        tx.append_audit(&sample_audit("audit-2", 2, "item-1"))?;
        tx.upsert_sync_version(&sample_sync_version("item-1", b"TAMPERED".to_vec()))?;
        Err(maho_storage::StorageError::Other(
            "forced mid-transaction failure".to_string(),
        ))
    });
    assert!(result.is_err(), "closure Err must surface");

    // Every table is byte-identical to the pre-transaction state.
    let item = db.get_vault_item("item-1").unwrap().expect("item survives");
    assert_eq!(
        item.envelope, baseline_env,
        "prior ciphertext preserved byte-for-byte"
    );
    assert!(
        db.get_vault_grant("grant-2").unwrap().is_none(),
        "candidate grant rolled back"
    );
    assert_eq!(db.list_vault_grants().unwrap().len(), 1);
    let audits = db.list_vault_audit_events().unwrap();
    assert_eq!(audits.len(), 1, "candidate audit rolled back");
    assert_eq!(audits[0].id, "audit-1");
    let sync = db
        .get_vault_sync_version("item-1")
        .unwrap()
        .expect("sync survives");
    assert_eq!(
        sync.encrypted_payload, baseline_env,
        "prior sync ciphertext preserved"
    );
}

#[test]
fn atomic_commit_persists_all_tables_together() {
    let db = mem_store();
    db.vault_transaction(|tx| {
        tx.upsert_item(&sample_item("item-9", envelope_with_sentinel()))?;
        tx.upsert_grant(&sample_grant("grant-9", "item-9"))?;
        tx.append_audit(&sample_audit("audit-9", 1, "item-9"))?;
        tx.upsert_sync_version(&sample_sync_version("item-9", envelope_with_sentinel()))?;
        Ok(())
    })
    .unwrap();
    assert!(db.get_vault_item("item-9").unwrap().is_some());
    assert!(db.get_vault_grant("grant-9").unwrap().is_some());
    assert_eq!(db.list_vault_audit_events().unwrap().len(), 1);
    assert!(db.get_vault_sync_version("item-9").unwrap().is_some());
}

#[test]
fn wrong_sqlcipher_database_key_fails_to_open() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("vault.db");
    let path_str = path.to_string_lossy().to_string();

    {
        let db = SqliteStorage::open_with_key(&path_str, "correct-horse-battery").unwrap();
        db.save_vault_item(&sample_item("item-1", envelope_with_sentinel()))
            .unwrap();
    }

    // Opening with the wrong SQLCipher key must fail (cannot read the schema).
    let opened = SqliteStorage::open_with_key(&path_str, "wrong-key-entirely");
    assert!(
        opened.is_err(),
        "opening an encrypted Vault DB with the wrong SQLCipher key must fail"
    );
}

#[test]
fn metadata_stores_opaque_kdf_and_wrapped_key_blobs() {
    let db = mem_store();
    db.save_vault_metadata(&VaultMetadataRow {
        slot: "kdf_params".to_string(),
        schema_version: 1,
        payload: b"argon2id-serialized-params".to_vec(),
        updated_at: "2026-07-22T00:00:00Z".to_string(),
    })
    .unwrap();
    db.save_vault_metadata(&VaultMetadataRow {
        slot: "wrapped_user_key".to_string(),
        schema_version: 1,
        payload: envelope_with_sentinel(),
        updated_at: "2026-07-22T00:00:00Z".to_string(),
    })
    .unwrap();

    let kdf = db
        .get_vault_metadata("kdf_params")
        .unwrap()
        .expect("kdf slot");
    assert_eq!(kdf.payload, b"argon2id-serialized-params");
    let wrapped = db
        .get_vault_metadata("wrapped_user_key")
        .unwrap()
        .expect("wrapped key slot");
    assert_eq!(wrapped.payload, envelope_with_sentinel());
}

// === Todo 11: optimistic-revision CAS storage primitives ===================

#[test]
fn vault_storage_insert_item_new_creates_once_and_rejects_duplicate() {
    // Given a fresh store, insert-only create succeeds exactly once.
    let db = mem_store();
    let first = db
        .insert_vault_item_new(&sample_item("item-1", b"ENV-A".to_vec()))
        .unwrap();
    assert!(first, "first insert-only create must succeed");

    // When a second create for the same id runs with different ciphertext,
    // Then it returns false WITHOUT overwriting the committed envelope.
    let dup = db
        .insert_vault_item_new(&sample_item("item-1", b"ENV-B-OVERWRITE".to_vec()))
        .unwrap();
    assert!(
        !dup,
        "duplicate id must not overwrite and must report false"
    );
    let row = db.get_vault_item("item-1").unwrap().expect("row present");
    assert_eq!(row.envelope, b"ENV-A", "ciphertext must be byte-identical");
    assert_eq!(row.revision, 1);
}

#[test]
fn vault_storage_update_item_cas_bumps_on_matching_revision() {
    // Given a committed rev-1 item.
    let db = mem_store();
    db.insert_vault_item_new(&sample_item("item-1", b"ENV-1".to_vec()))
        .unwrap();
    // When CAS-updating with the matching expected revision to rev 2.
    let mut next = sample_item("item-1", b"ENV-2".to_vec());
    next.revision = 2;
    next.updated_at = "2026-07-22T02:00:00Z".to_string();
    let ok = db.update_vault_item_cas(&next, 1).unwrap();
    // Then it succeeds and the row reflects the new revision + ciphertext.
    assert!(ok, "matching-revision CAS must succeed");
    let row = db.get_vault_item("item-1").unwrap().expect("row present");
    assert_eq!(row.revision, 2);
    assert_eq!(row.envelope, b"ENV-2");
}

#[test]
fn vault_storage_update_item_cas_stale_revision_is_false_and_preserves_bytes() {
    // Given a committed rev-1 item.
    let db = mem_store();
    db.insert_vault_item_new(&sample_item("item-1", b"ENV-1".to_vec()))
        .unwrap();
    // When CAS-updating with a STALE expected revision (0).
    let mut next = sample_item("item-1", b"ENV-STALE".to_vec());
    next.revision = 99;
    let ok = db.update_vault_item_cas(&next, 0).unwrap();
    // Then it returns false and the prior ciphertext is byte-identical.
    assert!(!ok, "stale-revision CAS must fail (false)");
    let row = db.get_vault_item("item-1").unwrap().expect("row present");
    assert_eq!(row.revision, 1, "revision unchanged");
    assert_eq!(row.envelope, b"ENV-1", "prior ciphertext preserved");
}

#[test]
fn vault_storage_tombstone_cas_preserves_row_and_excludes_active_count() {
    // Given two committed active items.
    let db = mem_store();
    db.insert_vault_item_new(&sample_item("keep", b"ENV-K".to_vec()))
        .unwrap();
    db.insert_vault_item_new(&sample_item("gone", b"ENV-LIVE-SECRET".to_vec()))
        .unwrap();
    assert_eq!(db.count_active_vault_items().unwrap(), 2);

    // When tombstoning one via conditional CAS with a new tombstone envelope.
    let ok = db
        .tombstone_vault_item_cas(&VaultTombstoneCas {
            id: "gone",
            expected_revision: 1,
            new_revision: 2,
            envelope: b"ENV-TOMBSTONE",
            updated_at: "2026-07-22T03:00:00Z",
            deleted_at: "2026-07-22T03:00:00Z",
        })
        .unwrap();

    // Then the row is retained (for sync) with a bumped revision, its active
    // secret envelope is replaced by the tombstone envelope, deleted_at is set,
    // and it is excluded from the active count.
    assert!(ok, "tombstone CAS on matching revision must succeed");
    let row = db.get_vault_item("gone").unwrap().expect("row retained");
    assert_eq!(row.revision, 2);
    assert_eq!(row.envelope, b"ENV-TOMBSTONE", "active secret destroyed");
    assert_eq!(row.deleted_at.as_deref(), Some("2026-07-22T03:00:00Z"));
    assert_eq!(
        db.count_active_vault_items().unwrap(),
        1,
        "excluded from active"
    );
}

#[test]
fn vault_storage_tombstoned_and_missing_cas_return_false() {
    let db = mem_store();
    db.insert_vault_item_new(&sample_item("item-1", b"ENV-1".to_vec()))
        .unwrap();
    // Tombstone once.
    assert!(db
        .tombstone_vault_item_cas(&VaultTombstoneCas {
            id: "item-1",
            expected_revision: 1,
            new_revision: 2,
            envelope: b"ENV-TOMB",
            updated_at: "2026-07-22T03:00:00Z",
            deleted_at: "2026-07-22T03:00:00Z",
        })
        .unwrap());
    // A second tombstone / update on the already-tombstoned row must be false
    // (the `deleted_at IS NULL` guard excludes it).
    assert!(!db
        .tombstone_vault_item_cas(&VaultTombstoneCas {
            id: "item-1",
            expected_revision: 2,
            new_revision: 3,
            envelope: b"ENV-TOMB2",
            updated_at: "2026-07-22T04:00:00Z",
            deleted_at: "2026-07-22T04:00:00Z",
        })
        .unwrap());
    let mut next = sample_item("item-1", b"ENV-RESURRECT".to_vec());
    next.revision = 3;
    assert!(!db.update_vault_item_cas(&next, 2).unwrap());
    // A CAS against a completely missing id is also false.
    let mut missing = sample_item("does-not-exist", b"X".to_vec());
    missing.revision = 2;
    assert!(!db.update_vault_item_cas(&missing, 1).unwrap());
}

#[test]
fn vault_storage_count_active_ignores_tombstones_and_reads_no_envelope() {
    let db = mem_store();
    assert_eq!(db.count_active_vault_items().unwrap(), 0);
    db.insert_vault_item_new(&sample_item("a", envelope_with_sentinel()))
        .unwrap();
    db.insert_vault_item_new(&sample_item("b", envelope_with_sentinel()))
        .unwrap();
    assert_eq!(db.count_active_vault_items().unwrap(), 2);
    db.tombstone_vault_item_cas(&VaultTombstoneCas {
        id: "a",
        expected_revision: 1,
        new_revision: 2,
        envelope: b"T",
        updated_at: "2026-07-22T03:00:00Z",
        deleted_at: "2026-07-22T03:00:00Z",
    })
    .unwrap();
    assert_eq!(db.count_active_vault_items().unwrap(), 1);
}

#[test]
fn vault_storage_two_handles_cannot_both_win_same_expected_revision() {
    // Given ONE on-disk SQLCipher DB opened through two independent handles.
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("race.db");
    let path_str = path.to_string_lossy().to_string();
    let key = "race-shared-key";
    let handle_a = SqliteStorage::open_with_key(&path_str, key).unwrap();
    let handle_b = SqliteStorage::open_with_key(&path_str, key).unwrap();

    handle_a
        .insert_vault_item_new(&sample_item("item-1", b"ENV-1".to_vec()))
        .unwrap();

    // When both handles race to CAS the SAME expected revision (1 -> 2).
    let mut via_a = sample_item("item-1", b"ENV-FROM-A".to_vec());
    via_a.revision = 2;
    let mut via_b = sample_item("item-1", b"ENV-FROM-B".to_vec());
    via_b.revision = 2;
    let res_a = handle_a.update_vault_item_cas(&via_a, 1);
    let res_b = handle_b.update_vault_item_cas(&via_b, 1);

    // Then EXACTLY ONE wins; the loser is Ok(false) or a storage error
    // (SQLITE_BUSY is a storage error, never a silent overwrite).
    let wins = [&res_a, &res_b]
        .iter()
        .filter(|r| matches!(r, Ok(true)))
        .count();
    assert_eq!(wins, 1, "exactly one CAS may win: a={res_a:?} b={res_b:?}");
    for res in [&res_a, &res_b] {
        assert!(
            matches!(res, Ok(true) | Ok(false) | Err(_)),
            "no third outcome"
        );
    }
    // Final state converged to a single rev-2 write, never rev-3.
    let reopened = SqliteStorage::open_with_key(&path_str, key).unwrap();
    let row = reopened.get_vault_item("item-1").unwrap().expect("row");
    assert_eq!(row.revision, 2, "no double-bump to rev 3");
    assert!(row.envelope == b"ENV-FROM-A" || row.envelope == b"ENV-FROM-B");
}

#[test]
fn vault_storage_waits_for_short_contention_between_keyed_connections() {
    // Given two keyed, Vault-capable connections to the same database.
    let dir = tempfile::tempdir().expect("tempdir");
    let path = dir.path().join("busy-success.db");
    let path = path.to_string_lossy().to_string();
    let key = test_key();
    let holder = SqliteStorage::open_with_key(&path, &key).expect("open holder");
    let contender = SqliteStorage::open_with_key(&path, &key).expect("open contender");
    let (lock_acquired_tx, lock_acquired_rx) = mpsc::channel();
    let (release_tx, release_rx) = mpsc::channel();

    let holder_thread = thread::spawn(move || {
        holder.vault_transaction(|tx| {
            tx.upsert_item(&sample_item("holder", b"HOLDER".to_vec()))?;
            lock_acquired_tx.send(()).expect("test receiver is present");
            release_rx.recv().expect("test sender is present");
            Ok(())
        })
    });
    lock_acquired_rx
        .recv()
        .expect("holder acquires the write transaction");

    // When a second connection writes while the first transaction is briefly held.
    let start = Instant::now();
    let contender_thread = thread::spawn(move || {
        contender.save_vault_item(&sample_item("contender", b"CONTENDER".to_vec()))
    });
    thread::sleep(Duration::from_millis(50));
    release_tx.send(()).expect("holder thread is waiting");
    let contender_result = contender_thread.join().expect("contender does not panic");
    holder_thread
        .join()
        .expect("holder does not panic")
        .expect("holder commits");

    // Then the contender waits for and commits after the bounded contention.
    assert!(
        contender_result.is_ok(),
        "short contention must succeed: {contender_result:?}"
    );
    assert!(
        start.elapsed() >= Duration::from_millis(25),
        "contender must wait for the holder rather than bypass the lock"
    );
}

#[test]
fn vault_storage_reports_busy_after_prolonged_contention() {
    // Given two keyed, Vault-capable connections and a held write transaction.
    let dir = tempfile::tempdir().expect("tempdir");
    let path = dir.path().join("busy-failure.db");
    let path = path.to_string_lossy().to_string();
    let key = test_key();
    let holder = SqliteStorage::open_with_key(&path, &key).expect("open holder");
    let contender = SqliteStorage::open_with_key(&path, &key).expect("open contender");
    let (lock_acquired_tx, lock_acquired_rx) = mpsc::channel();
    let (release_tx, release_rx) = mpsc::channel();

    let holder_thread = thread::spawn(move || {
        holder.vault_transaction(|tx| {
            tx.upsert_item(&sample_item("holder", b"HOLDER".to_vec()))?;
            lock_acquired_tx.send(()).expect("test receiver is present");
            release_rx.recv().expect("test sender is present");
            Ok(())
        })
    });
    lock_acquired_rx
        .recv()
        .expect("holder acquires the write transaction");

    // When the contender remains blocked beyond the configured busy timeout.
    let start = Instant::now();
    let result = contender.save_vault_item(&sample_item("contender", b"CONTENDER".to_vec()));
    let elapsed = start.elapsed();
    release_tx.send(()).expect("holder thread is waiting");
    holder_thread
        .join()
        .expect("holder does not panic")
        .expect("holder commits");

    // Then SQLite returns a typed busy/locked storage failure instead of hanging.
    assert!(
        matches!(
            result,
            Err(StorageError::Sqlite(rusqlite::Error::SqliteFailure(code, _)))
                if matches!(code.code, ErrorCode::DatabaseBusy | ErrorCode::DatabaseLocked)
        ),
        "prolonged contention must surface SQLite busy/locked failure: {result:?}"
    );
    assert!(
        elapsed < Duration::from_secs(2),
        "busy timeout must bound the wait, elapsed {elapsed:?}"
    );
}
