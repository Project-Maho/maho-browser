//! Dedicated, crypto-agnostic SQLCipher persistence for the Maho Vault.
//!
//! Extracted from the 4k-line `sqlite.rs` per plan Todo 9. `maho-storage`
//! stores already-encrypted envelope bytes and opaque wrapped-key blobs plus
//! non-secret routing/version/timestamp metadata. It never derives, unwraps, or
//! interprets cryptographic material: record encryption and key wrapping are
//! owned by `maho-core` (Todo 8). No logical plaintext password / domain /
//! username / TOTP / passkey / recovery / raw-key columns exist here.
//
// allow: SIZE_OK - single-responsibility Vault persistence module (schema +
// cutover + typed rows + transactional CRUD) deliberately extracted from the
// oversized sqlite.rs; the plan mandates one dedicated Vault storage file.

use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use rusqlite::{Connection, OptionalExtension, Transaction, TransactionBehavior};

use super::SqliteStorage;
use crate::StorageError;

/// Installed Vault relational schema version. Bumping this triggers the
/// development-only destructive cutover in [`SqliteStorage::initialize_vault_schema`].
pub(crate) const VAULT_SCHEMA_VERSION: i64 = 2;

/// Bounded audit pagination: positive limits clamp to MAX_PAGE; limit 0 selects
/// DEFAULT_PAGE. Keeps every caller's page allocation bounded.
pub const VAULT_AUDIT_MAX_PAGE: usize = 1_000;
pub const VAULT_AUDIT_DEFAULT_PAGE: usize = 100;

const VAULT_POLICY_TABLE_SQL: &str = "\
CREATE TABLE IF NOT EXISTS vault_agent_policies (
    scope TEXT PRIMARY KEY,
    schema_version INTEGER NOT NULL,
    policy TEXT NOT NULL,
    item_id TEXT,
    origin TEXT,
    expires_at TEXT,
    updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_vault_agent_policies_item ON vault_agent_policies(item_id);
CREATE INDEX IF NOT EXISTS idx_vault_agent_policies_origin ON vault_agent_policies(origin);";

const VAULT_SCHEMA_SQL: &str = "\
CREATE TABLE vault_metadata (
    slot TEXT PRIMARY KEY,
    schema_version INTEGER NOT NULL,
    payload BLOB NOT NULL,
    updated_at TEXT NOT NULL
);
CREATE TABLE vault_items (
    id TEXT PRIMARY KEY,
    schema_version INTEGER NOT NULL,
    revision INTEGER NOT NULL,
    provider TEXT NOT NULL,
    item_kind TEXT NOT NULL,
    envelope BLOB NOT NULL,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    deleted_at TEXT
);
CREATE INDEX idx_vault_items_kind ON vault_items(item_kind);
CREATE TABLE vault_device_keys (
    device_id TEXT PRIMARY KEY,
    schema_version INTEGER NOT NULL,
    wrapped_key BLOB NOT NULL,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    revoked_at TEXT
);
CREATE TABLE vault_recovery (
    id TEXT PRIMARY KEY,
    schema_version INTEGER NOT NULL,
    wrapped_key BLOB NOT NULL,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
CREATE TABLE vault_capability_grants (
    handle TEXT PRIMARY KEY,
    schema_version INTEGER NOT NULL,
    session_id TEXT NOT NULL,
    task_id TEXT NOT NULL,
    profile_id TEXT NOT NULL,
    workspace_id TEXT NOT NULL,
    tab_id TEXT NOT NULL,
    tab_generation INTEGER NOT NULL,
    top_origin TEXT NOT NULL,
    frame_origin TEXT NOT NULL,
    item_id TEXT NOT NULL,
    item_alias TEXT NOT NULL,
    allowed_fields TEXT NOT NULL,
    policy TEXT NOT NULL,
    issued_at TEXT NOT NULL,
    expires_at TEXT NOT NULL,
    max_uses INTEGER NOT NULL,
    used_count INTEGER NOT NULL DEFAULT 0,
    state TEXT NOT NULL,
    revoked_at TEXT,
    revocation_reason TEXT,
    FOREIGN KEY(item_id) REFERENCES vault_items(id) ON DELETE CASCADE
);
CREATE INDEX idx_vault_grants_item ON vault_capability_grants(item_id);
CREATE TABLE vault_agent_policies (
    scope TEXT PRIMARY KEY,
    schema_version INTEGER NOT NULL,
    policy TEXT NOT NULL,
    item_id TEXT,
    origin TEXT,
    expires_at TEXT,
    updated_at TEXT NOT NULL
);
CREATE INDEX idx_vault_agent_policies_item ON vault_agent_policies(item_id);
CREATE INDEX idx_vault_agent_policies_origin ON vault_agent_policies(origin);
CREATE TABLE vault_audit_events (
    id TEXT PRIMARY KEY,
    schema_version INTEGER NOT NULL,
    seq INTEGER NOT NULL,
    timestamp TEXT NOT NULL,
    session_id TEXT,
    task_id TEXT,
    profile_id TEXT NOT NULL,
    workspace_id TEXT NOT NULL,
    top_origin TEXT,
    frame_origin TEXT,
    item_id TEXT,
    item_alias TEXT,
    operation TEXT NOT NULL,
    policy TEXT,
    decision TEXT NOT NULL,
    reason TEXT,
    device_name TEXT NOT NULL,
    prev_hash BLOB,
    entry_hash BLOB NOT NULL
);
CREATE INDEX idx_vault_audit_seq ON vault_audit_events(seq);
CREATE TABLE vault_sync_versions (
    entity_id TEXT PRIMARY KEY,
    schema_version INTEGER NOT NULL,
    version INTEGER NOT NULL,
    updated_at TEXT NOT NULL,
    encrypted_payload BLOB NOT NULL
);";

/// Opaque Vault metadata / KDF params / wrapped-user-key slot. `payload` is
/// serialized encrypted or otherwise opaque bytes; only `slot` routes it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VaultMetadataRow {
    pub slot: String,
    pub schema_version: i64,
    pub payload: Vec<u8>,
    pub updated_at: String,
}

/// One encrypted Vault item. `envelope` is the ciphertext-only serialized AEAD
/// envelope produced by `maho-core`; storage never inspects it. There are no
/// plaintext secret columns by construction.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct EncryptedVaultItemRow {
    pub id: String,
    pub schema_version: i64,
    pub revision: i64,
    pub provider: String,
    pub item_kind: String,
    pub envelope: Vec<u8>,
    pub created_at: String,
    pub updated_at: String,
    pub deleted_at: Option<String>,
}

/// A device-wrapped Vault key. `wrapped_key` is an opaque wrapping envelope.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VaultDeviceKeyRow {
    pub device_id: String,
    pub schema_version: i64,
    pub wrapped_key: Vec<u8>,
    pub created_at: String,
    pub updated_at: String,
    pub revoked_at: Option<String>,
}

/// Recovery-wrapped Vault key material. `wrapped_key` is opaque.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VaultRecoveryRow {
    pub id: String,
    pub schema_version: i64,
    pub wrapped_key: Vec<u8>,
    pub created_at: String,
    pub updated_at: String,
}

/// Origin/frame/task-bound credential capability grant. All fields are
/// non-secret binding/revocation metadata; no credential value is stored.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VaultGrantRow {
    pub handle: String,
    pub schema_version: i64,
    pub session_id: String,
    pub task_id: String,
    pub profile_id: String,
    pub workspace_id: String,
    pub tab_id: String,
    pub tab_generation: i64,
    pub top_origin: String,
    pub frame_origin: String,
    pub item_id: String,
    pub item_alias: String,
    pub allowed_fields: String,
    pub policy: String,
    pub issued_at: String,
    pub expires_at: String,
    pub max_uses: i64,
    pub used_count: i64,
    pub state: String,
    pub revoked_at: Option<String>,
    pub revocation_reason: Option<String>,
}

/// Persisted agent policy row. The `scope` key is derived from the canonical
/// VaultPolicy DTO target (`default`, `item:<uuid>`, `origin:<origin>`, or the
/// combined item+origin key). The policy itself is non-secret control metadata.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VaultPolicyRow {
    pub scope: String,
    pub schema_version: i64,
    pub policy: String,
    pub item_id: Option<String>,
    pub origin: Option<String>,
    pub expires_at: Option<String>,
    pub updated_at: String,
}

/// Tamper-evident audit event. Holds only safe structured metadata plus
/// hash-chain fields; never raw tool arguments or secrets.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VaultAuditRow {
    pub id: String,
    pub schema_version: i64,
    pub seq: i64,
    pub timestamp: String,
    pub session_id: Option<String>,
    pub task_id: Option<String>,
    pub profile_id: String,
    pub workspace_id: String,
    pub top_origin: Option<String>,
    pub frame_origin: Option<String>,
    pub item_id: Option<String>,
    pub item_alias: Option<String>,
    pub operation: String,
    pub policy: Option<String>,
    pub decision: String,
    pub reason: Option<String>,
    pub device_name: String,
    pub prev_hash: Option<Vec<u8>>,
    pub entry_hash: Vec<u8>,
}

/// Ciphertext-only sync-version state for one Vault entity. `encrypted_payload`
/// is opaque; `version` is the non-secret monotonic routing counter.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VaultSyncVersionRow {
    pub entity_id: String,
    pub schema_version: i64,
    pub version: i64,
    pub updated_at: String,
    pub encrypted_payload: Vec<u8>,
}

/// Parameters for a conditional (optimistic-revision) tombstone update. Grouped
/// into one value object so [`VaultTx::tombstone_item_cas`] stays single-argument.
/// `envelope` is the opaque tombstone ciphertext that replaces the live secret.
pub struct VaultTombstoneCas<'a> {
    pub id: &'a str,
    pub expected_revision: i64,
    pub new_revision: i64,
    pub envelope: &'a [u8],
    pub updated_at: &'a str,
    pub deleted_at: &'a str,
}

impl SqliteStorage {
    /// Install the current Vault schema, performing a development-only
    /// destructive cutover when a stale/partial schema is present.
    ///
    /// Idempotent: on reopen with a matching `vault_schema_info` version it is a
    /// no-op. When the version differs (fresh DB or old partial Vault/legacy
    /// `passwords` tables), every Vault table and the legacy `passwords` table
    /// are dropped and recreated inside one transaction. No backups are kept;
    /// there are no released users, so unreleased development rows are discarded.
    pub(crate) fn initialize_vault_schema(&self) -> Result<(), StorageError> {
        self.conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS vault_schema_info (
                id INTEGER PRIMARY KEY CHECK (id = 0),
                vault_schema_version INTEGER NOT NULL
            );",
        )?;

        let installed: Option<i64> = self
            .conn
            .query_row(
                "SELECT vault_schema_version FROM vault_schema_info WHERE id = 0",
                [],
                |row| row.get(0),
            )
            .optional()?;

        if installed == Some(VAULT_SCHEMA_VERSION) {
            return Ok(());
        }

        if installed == Some(1) {
            let tx = self.conn.unchecked_transaction()?;
            tx.execute_batch(VAULT_POLICY_TABLE_SQL)?;
            tx.execute(
                "UPDATE vault_schema_info SET vault_schema_version = ?1 WHERE id = 0",
                rusqlite::params![VAULT_SCHEMA_VERSION],
            )?;
            tx.commit()?;
            return Ok(());
        }

        let tx = self.conn.unchecked_transaction()?;
        // Safely archive legacy passwords table if present to prevent data loss on schema cutover
        let has_legacy_passwords: bool = tx
            .query_row(
                "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='passwords'",
                [],
                |row| row.get(0),
            )
            .unwrap_or(0)
            > 0;
        if has_legacy_passwords {
            let has_archived: bool = tx
                .query_row(
                    "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='legacy_passwords_archive'",
                    [],
                    |row| row.get(0),
                )
                .unwrap_or(0) > 0;
            if !has_archived {
                let _ =
                    tx.execute_batch("ALTER TABLE passwords RENAME TO legacy_passwords_archive;");
            }
        }
        tx.execute_batch(
            "DROP INDEX IF EXISTS idx_vault_items_domain;
             DROP INDEX IF EXISTS idx_vault_items_kind;
             DROP INDEX IF EXISTS idx_vault_grants_item;
             DROP INDEX IF EXISTS idx_vault_agent_policies_item;
             DROP INDEX IF EXISTS idx_vault_agent_policies_origin;
             DROP INDEX IF EXISTS idx_vault_audit_seq;
             DROP TABLE IF EXISTS vault_items;
             DROP TABLE IF EXISTS vault_metadata;
             DROP TABLE IF EXISTS vault_device_keys;
             DROP TABLE IF EXISTS vault_recovery;
             DROP TABLE IF EXISTS vault_capability_grants;
             DROP TABLE IF EXISTS vault_agent_policies;
             DROP TABLE IF EXISTS vault_audit_events;
             DROP TABLE IF EXISTS vault_sync_versions;",
        )?;
        tx.execute_batch(VAULT_SCHEMA_SQL)?;
        tx.execute(
            "INSERT INTO vault_schema_info (id, vault_schema_version) VALUES (0, ?1)
             ON CONFLICT(id) DO UPDATE SET vault_schema_version = excluded.vault_schema_version",
            rusqlite::params![VAULT_SCHEMA_VERSION],
        )?;
        tx.commit()?;
        Ok(())
    }

    /// Run `body` inside one SQLite transaction spanning every Vault table.
    /// On `Ok` it commits; any `Err` returned by the closure (or any SQL error)
    /// drops the transaction and SQLite rolls back every write, so prior
    /// ciphertext stays byte-identical. This is the atomic primitive on which
    /// item + grant + audit + sync-version mutations compose.
    pub fn vault_transaction<T>(
        &self,
        body: impl FnOnce(&VaultTx<'_>) -> Result<T, StorageError>,
    ) -> Result<T, StorageError> {
        let tx = self.conn.unchecked_transaction()?;
        let conn: &Connection = &tx;
        let handle = VaultTx { conn };
        let value = body(&handle)?;
        tx.commit()?;
        Ok(value)
    }

    /// Acquire the writer reservation before reading the audit head. Independent
    /// connections therefore allocate chain positions only after prior commits.
    pub fn vault_audit_transaction<T>(
        &self,
        body: impl FnOnce(&VaultTx<'_>) -> Result<T, StorageError>,
    ) -> Result<T, StorageError> {
        let tx = Transaction::new_unchecked(&self.conn, TransactionBehavior::Immediate)?;
        let handle = VaultTx { conn: &tx };
        let value = body(&handle)?;
        tx.commit()?;
        Ok(value)
    }

    pub fn save_vault_metadata(&self, row: &VaultMetadataRow) -> Result<(), StorageError> {
        self.vault_transaction(|tx| tx.upsert_metadata(row))
    }

    pub fn get_vault_metadata(&self, slot: &str) -> Result<Option<VaultMetadataRow>, StorageError> {
        self.conn
            .query_row(
                "SELECT slot, schema_version, payload, updated_at FROM vault_metadata WHERE slot = ?1",
                rusqlite::params![slot],
                |row| {
                    Ok(VaultMetadataRow {
                        slot: row.get(0)?,
                        schema_version: row.get(1)?,
                        payload: row.get(2)?,
                        updated_at: row.get(3)?,
                    })
                },
            )
            .optional()
            .map_err(StorageError::from)
    }

    pub fn save_vault_item(&self, row: &EncryptedVaultItemRow) -> Result<(), StorageError> {
        self.vault_transaction(|tx| tx.upsert_item(row))
    }

    pub fn get_vault_item(&self, id: &str) -> Result<Option<EncryptedVaultItemRow>, StorageError> {
        self.conn
            .query_row(
                "SELECT id, schema_version, revision, provider, item_kind, envelope, created_at, updated_at, deleted_at \
                 FROM vault_items WHERE id = ?1",
                rusqlite::params![id],
                map_item_row,
            )
            .optional()
            .map_err(StorageError::from)
    }

    pub fn list_vault_items(&self) -> Result<Vec<EncryptedVaultItemRow>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, schema_version, revision, provider, item_kind, envelope, created_at, updated_at, deleted_at \
             FROM vault_items ORDER BY created_at ASC, id ASC",
        )?;
        let rows = stmt.query_map([], map_item_row)?;
        rows.collect::<Result<Vec<_>, _>>()
            .map_err(StorageError::from)
    }

    pub fn delete_vault_item(&self, id: &str) -> Result<bool, StorageError> {
        self.vault_transaction(|tx| tx.delete_item(id))
    }

    /// Insert-only create (Todo 11 CRUD path). Never overwrites an existing id;
    /// returns `true` only when a new row was actually inserted.
    pub fn insert_vault_item_new(&self, row: &EncryptedVaultItemRow) -> Result<bool, StorageError> {
        self.vault_transaction(|tx| tx.insert_item_new(row))
    }

    /// Optimistic-revision update (Todo 11 CRUD path): atomic compare-and-set on
    /// `revision` for a non-tombstoned row. `true` iff exactly one row matched.
    pub fn update_vault_item_cas(
        &self,
        row: &EncryptedVaultItemRow,
        expected_revision: i64,
    ) -> Result<bool, StorageError> {
        self.vault_transaction(|tx| tx.update_item_cas(row, expected_revision))
    }

    /// Optimistic-revision tombstone (Todo 11 CRUD path): retains the row for
    /// sync while destroying the live secret envelope. `true` iff it matched.
    pub fn tombstone_vault_item_cas(
        &self,
        cas: &VaultTombstoneCas<'_>,
    ) -> Result<bool, StorageError> {
        self.vault_transaction(|tx| tx.tombstone_item_cas(cas))
    }

    /// Count non-tombstoned items without reading any envelope payload, so locked
    /// callers can refresh a cached count safely.
    pub fn count_active_vault_items(&self) -> Result<u64, StorageError> {
        let count: i64 = self.conn.query_row(
            "SELECT COUNT(*) FROM vault_items WHERE deleted_at IS NULL",
            [],
            |row| row.get(0),
        )?;
        Ok(u64::try_from(count).unwrap_or(0))
    }

    pub fn save_vault_device_key(&self, row: &VaultDeviceKeyRow) -> Result<(), StorageError> {
        self.vault_transaction(|tx| tx.upsert_device_key(row))
    }

    pub fn get_vault_device_key(
        &self,
        device_id: &str,
    ) -> Result<Option<VaultDeviceKeyRow>, StorageError> {
        self.conn
            .query_row(
                "SELECT device_id, schema_version, wrapped_key, created_at, updated_at, revoked_at \
                 FROM vault_device_keys WHERE device_id = ?1",
                rusqlite::params![device_id],
                |row| {
                    Ok(VaultDeviceKeyRow {
                        device_id: row.get(0)?,
                        schema_version: row.get(1)?,
                        wrapped_key: row.get(2)?,
                        created_at: row.get(3)?,
                        updated_at: row.get(4)?,
                        revoked_at: row.get(5)?,
                    })
                },
            )
            .optional()
            .map_err(StorageError::from)
    }

    pub fn save_vault_recovery(&self, row: &VaultRecoveryRow) -> Result<(), StorageError> {
        self.vault_transaction(|tx| tx.upsert_recovery(row))
    }

    pub fn get_vault_recovery(&self, id: &str) -> Result<Option<VaultRecoveryRow>, StorageError> {
        self.conn
            .query_row(
                "SELECT id, schema_version, wrapped_key, created_at, updated_at \
                 FROM vault_recovery WHERE id = ?1",
                rusqlite::params![id],
                |row| {
                    Ok(VaultRecoveryRow {
                        id: row.get(0)?,
                        schema_version: row.get(1)?,
                        wrapped_key: row.get(2)?,
                        created_at: row.get(3)?,
                        updated_at: row.get(4)?,
                    })
                },
            )
            .optional()
            .map_err(StorageError::from)
    }

    pub fn save_vault_grant(&self, row: &VaultGrantRow) -> Result<(), StorageError> {
        self.vault_transaction(|tx| tx.upsert_grant(row))
    }

    pub fn get_vault_grant(&self, handle: &str) -> Result<Option<VaultGrantRow>, StorageError> {
        self.conn
            .query_row(
                GRANT_SELECT_BY_HANDLE,
                rusqlite::params![handle],
                map_grant_row,
            )
            .optional()
            .map_err(StorageError::from)
    }

    pub fn list_vault_grants(&self) -> Result<Vec<VaultGrantRow>, StorageError> {
        let mut stmt = self.conn.prepare(GRANT_SELECT_ALL)?;
        let rows = stmt.query_map([], map_grant_row)?;
        rows.collect::<Result<Vec<_>, _>>()
            .map_err(StorageError::from)
    }

    pub fn save_vault_policy(&self, row: &VaultPolicyRow) -> Result<(), StorageError> {
        self.vault_transaction(|tx| tx.upsert_policy(row))
    }

    pub fn get_vault_policy(&self, scope: &str) -> Result<Option<VaultPolicyRow>, StorageError> {
        self.conn
            .query_row(
                POLICY_SELECT_BY_SCOPE,
                rusqlite::params![scope],
                map_policy_row,
            )
            .optional()
            .map_err(StorageError::from)
    }

    pub fn list_vault_policies(&self) -> Result<Vec<VaultPolicyRow>, StorageError> {
        let mut stmt = self.conn.prepare(POLICY_SELECT_ALL)?;
        let rows = stmt.query_map([], map_policy_row)?;
        rows.collect::<Result<Vec<_>, _>>()
            .map_err(StorageError::from)
    }

    pub fn append_vault_audit(&self, row: &VaultAuditRow) -> Result<(), StorageError> {
        // Immediate transaction: sequence allocation inside the writer
        // reservation must serialize across independent connections so
        // concurrent appends cannot fork the audit hash chain.
        self.vault_audit_transaction(|tx| tx.append_audit(row))
    }

    pub fn list_vault_audit_events(&self) -> Result<Vec<VaultAuditRow>, StorageError> {
        let mut stmt = self.conn.prepare(AUDIT_SELECT_ALL)?;
        let rows = stmt.query_map([], map_audit_row)?;
        rows.collect::<Result<Vec<_>, _>>()
            .map_err(StorageError::from)
    }

    pub fn vault_audit_tail(&self) -> Result<Option<(i64, Vec<u8>)>, StorageError> {
        self.vault_transaction(|tx| tx.audit_tail())
    }

    pub fn page_vault_audit_events(
        &self,
        after_seq: Option<i64>,
        limit: usize,
    ) -> Result<(Vec<VaultAuditRow>, Option<i64>), StorageError> {
        self.vault_transaction(|tx| tx.page_audit_events(after_seq, limit))
    }

    pub fn explain_query_plan(&self, sql: &str) -> Result<String, StorageError> {
        self.conn
            .query_row(&format!("EXPLAIN QUERY PLAN {sql}"), [], |row| row.get(3))
            .map_err(StorageError::from)
    }

    pub fn start_audit_materialization_tracking() {
        AUDIT_ROWS_MATERIALIZED.store(0, Ordering::SeqCst);
        AUDIT_ROW_OBSERVER_ENABLED.store(true, Ordering::SeqCst);
    }

    pub fn audit_rows_materialized() -> usize {
        AUDIT_ROWS_MATERIALIZED.load(Ordering::SeqCst)
    }

    pub fn stop_audit_materialization_tracking() {
        AUDIT_ROW_OBSERVER_ENABLED.store(false, Ordering::SeqCst);
        AUDIT_ROWS_MATERIALIZED.store(0, Ordering::SeqCst);
    }

    pub fn save_vault_sync_version(&self, row: &VaultSyncVersionRow) -> Result<(), StorageError> {
        self.vault_transaction(|tx| tx.upsert_sync_version(row))
    }

    pub fn get_vault_sync_version(
        &self,
        entity_id: &str,
    ) -> Result<Option<VaultSyncVersionRow>, StorageError> {
        self.conn
            .query_row(
                "SELECT entity_id, schema_version, version, updated_at, encrypted_payload \
                 FROM vault_sync_versions WHERE entity_id = ?1",
                rusqlite::params![entity_id],
                |row| {
                    Ok(VaultSyncVersionRow {
                        entity_id: row.get(0)?,
                        schema_version: row.get(1)?,
                        version: row.get(2)?,
                        updated_at: row.get(3)?,
                        encrypted_payload: row.get(4)?,
                    })
                },
            )
            .optional()
            .map_err(StorageError::from)
    }
}

const GRANT_SELECT_BY_HANDLE: &str =
    "SELECT handle, schema_version, session_id, task_id, profile_id, workspace_id, \
    tab_id, tab_generation, top_origin, frame_origin, item_id, item_alias, allowed_fields, policy, \
    issued_at, expires_at, max_uses, used_count, state, revoked_at, revocation_reason \
    FROM vault_capability_grants WHERE handle = ?1";
const GRANT_SELECT_ALL: &str =
    "SELECT handle, schema_version, session_id, task_id, profile_id, workspace_id, \
    tab_id, tab_generation, top_origin, frame_origin, item_id, item_alias, allowed_fields, policy, \
    issued_at, expires_at, max_uses, used_count, state, revoked_at, revocation_reason \
    FROM vault_capability_grants ORDER BY issued_at ASC, handle ASC";
const POLICY_SELECT_BY_SCOPE: &str =
    "SELECT scope, schema_version, policy, item_id, origin, expires_at, updated_at \
     FROM vault_agent_policies WHERE scope = ?1";
const POLICY_SELECT_ALL: &str =
    "SELECT scope, schema_version, policy, item_id, origin, expires_at, updated_at \
     FROM vault_agent_policies ORDER BY scope ASC";
const AUDIT_SELECT_ALL: &str = "SELECT id, schema_version, seq, timestamp, session_id, task_id, profile_id, \
    workspace_id, top_origin, frame_origin, item_id, item_alias, operation, policy, decision, reason, \
    device_name, prev_hash, entry_hash FROM vault_audit_events ORDER BY seq ASC, id ASC";

fn map_item_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<EncryptedVaultItemRow> {
    Ok(EncryptedVaultItemRow {
        id: row.get(0)?,
        schema_version: row.get(1)?,
        revision: row.get(2)?,
        provider: row.get(3)?,
        item_kind: row.get(4)?,
        envelope: row.get(5)?,
        created_at: row.get(6)?,
        updated_at: row.get(7)?,
        deleted_at: row.get(8)?,
    })
}

fn map_grant_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<VaultGrantRow> {
    Ok(VaultGrantRow {
        handle: row.get(0)?,
        schema_version: row.get(1)?,
        session_id: row.get(2)?,
        task_id: row.get(3)?,
        profile_id: row.get(4)?,
        workspace_id: row.get(5)?,
        tab_id: row.get(6)?,
        tab_generation: row.get(7)?,
        top_origin: row.get(8)?,
        frame_origin: row.get(9)?,
        item_id: row.get(10)?,
        item_alias: row.get(11)?,
        allowed_fields: row.get(12)?,
        policy: row.get(13)?,
        issued_at: row.get(14)?,
        expires_at: row.get(15)?,
        max_uses: row.get(16)?,
        used_count: row.get(17)?,
        state: row.get(18)?,
        revoked_at: row.get(19)?,
        revocation_reason: row.get(20)?,
    })
}

fn map_policy_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<VaultPolicyRow> {
    Ok(VaultPolicyRow {
        scope: row.get(0)?,
        schema_version: row.get(1)?,
        policy: row.get(2)?,
        item_id: row.get(3)?,
        origin: row.get(4)?,
        expires_at: row.get(5)?,
        updated_at: row.get(6)?,
    })
}

static AUDIT_ROW_OBSERVER_ENABLED: AtomicBool = AtomicBool::new(false);
static AUDIT_ROWS_MATERIALIZED: AtomicUsize = AtomicUsize::new(0);

fn map_audit_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<VaultAuditRow> {
    if AUDIT_ROW_OBSERVER_ENABLED.load(Ordering::Relaxed) {
        AUDIT_ROWS_MATERIALIZED.fetch_add(1, Ordering::Relaxed);
    }
    Ok(VaultAuditRow {
        id: row.get(0)?,
        schema_version: row.get(1)?,
        seq: row.get(2)?,
        timestamp: row.get(3)?,
        session_id: row.get(4)?,
        task_id: row.get(5)?,
        profile_id: row.get(6)?,
        workspace_id: row.get(7)?,
        top_origin: row.get(8)?,
        frame_origin: row.get(9)?,
        item_id: row.get(10)?,
        item_alias: row.get(11)?,
        operation: row.get(12)?,
        policy: row.get(13)?,
        decision: row.get(14)?,
        reason: row.get(15)?,
        device_name: row.get(16)?,
        prev_hash: row.get(17)?,
        entry_hash: row.get(18)?,
    })
}

/// Transaction-scoped Vault writer. Every write goes through the enclosing
/// transaction, so returning `Err` from the [`SqliteStorage::vault_transaction`]
/// closure rolls all of them back atomically.
pub struct VaultTx<'conn> {
    conn: &'conn Connection,
}

impl VaultTx<'_> {
    pub fn latest_audit_event(&self) -> Result<Option<VaultAuditRow>, StorageError> {
        self.conn
            .query_row(
                "SELECT id, schema_version, seq, timestamp, session_id, task_id, profile_id, \
                 workspace_id, top_origin, frame_origin, item_id, item_alias, operation, policy, decision, reason, \
                 device_name, prev_hash, entry_hash FROM vault_audit_events ORDER BY seq DESC, id DESC LIMIT 1",
                [],
                map_audit_row,
            )
            .optional()
            .map_err(StorageError::from)
    }

    pub fn audit_tail(&self) -> Result<Option<(i64, Vec<u8>)>, StorageError> {
        // Tail primitive: read only (seq, entry_hash) of the highest committed
        // row so an append never materializes the audit history.
        self.conn
            .query_row(
                "SELECT seq, entry_hash FROM vault_audit_events \
                 ORDER BY seq DESC, id DESC LIMIT 1",
                [],
                |row| Ok((row.get::<_, i64>(0)?, row.get::<_, Vec<u8>>(1)?)),
            )
            .optional()
            .map_err(StorageError::from)
    }

    pub fn page_audit_events(
        &self,
        after_seq: Option<i64>,
        limit: usize,
    ) -> Result<(Vec<VaultAuditRow>, Option<i64>), StorageError> {
        // Keyset pagination over idx_vault_audit_seq: fetch limit + 1 rows so a
        // next cursor is returned only when an extra row exists. Positive limits
        // clamp to 1,000; zero means the bounded default page of 100.
        if after_seq.is_some_and(|cursor| cursor < 0) {
            return Err(StorageError::Other(
                "invalid vault audit cursor: must be nonnegative".to_string(),
            ));
        }
        let effective_limit = if limit == 0 {
            VAULT_AUDIT_DEFAULT_PAGE
        } else {
            limit.min(VAULT_AUDIT_MAX_PAGE)
        };
        let fetch_limit = effective_limit
            .checked_add(1)
            .ok_or_else(|| StorageError::Other("vault audit page limit overflow".to_string()))?;
        let fetch_limit = i64::try_from(fetch_limit).unwrap_or(i64::MAX);
        let mut stmt = self.conn.prepare(
            "SELECT id, schema_version, seq, timestamp, session_id, task_id, profile_id, \
             workspace_id, top_origin, frame_origin, item_id, item_alias, operation, policy, \
             decision, reason, device_name, prev_hash, entry_hash FROM vault_audit_events \
             WHERE seq > ?1 ORDER BY seq ASC, id ASC LIMIT ?2",
        )?;
        let rows = stmt.query_map(
            rusqlite::params![after_seq.unwrap_or(0), fetch_limit],
            map_audit_row,
        )?;
        let mut page: Vec<VaultAuditRow> = rows.collect::<Result<Vec<_>, _>>()?;
        let next_cursor = if page.len() > effective_limit {
            page.truncate(effective_limit);
            page.last().map(|row| row.seq)
        } else {
            None
        };
        Ok((page, next_cursor))
    }

    pub fn count_active_items(&self) -> Result<u64, StorageError> {
        self.conn
            .query_row(
                "SELECT COUNT(*) FROM vault_items WHERE deleted_at IS NULL",
                [],
                |row| row.get(0),
            )
            .map_err(StorageError::from)
    }

    pub fn upsert_metadata(&self, row: &VaultMetadataRow) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO vault_metadata (slot, schema_version, payload, updated_at) \
             VALUES (?1, ?2, ?3, ?4) \
             ON CONFLICT(slot) DO UPDATE SET \
             schema_version = excluded.schema_version, payload = excluded.payload, updated_at = excluded.updated_at",
            rusqlite::params![row.slot, row.schema_version, row.payload, row.updated_at],
        )?;
        Ok(())
    }

    pub fn upsert_item(&self, row: &EncryptedVaultItemRow) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO vault_items (id, schema_version, revision, provider, item_kind, envelope, created_at, updated_at, deleted_at) \
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9) \
             ON CONFLICT(id) DO UPDATE SET \
             schema_version = excluded.schema_version, revision = excluded.revision, provider = excluded.provider, \
             item_kind = excluded.item_kind, envelope = excluded.envelope, updated_at = excluded.updated_at, \
             deleted_at = excluded.deleted_at",
            rusqlite::params![
                row.id, row.schema_version, row.revision, row.provider, row.item_kind, row.envelope,
                row.created_at, row.updated_at, row.deleted_at
            ],
        )?;
        Ok(())
    }

    pub fn delete_item(&self, id: &str) -> Result<bool, StorageError> {
        let count = self.conn.execute(
            "DELETE FROM vault_items WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(count > 0)
    }

    /// Insert-only create used by the Todo 11 user CRUD path. A pre-existing id
    /// is left untouched and reported as `false`; there is no overwrite here (the
    /// unconditional [`Self::upsert_item`] is reserved for Todo 36 reconciliation).
    pub fn insert_item_new(&self, row: &EncryptedVaultItemRow) -> Result<bool, StorageError> {
        let count = self.conn.execute(
            "INSERT INTO vault_items (id, schema_version, revision, provider, item_kind, envelope, created_at, updated_at, deleted_at) \
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9) \
             ON CONFLICT(id) DO NOTHING",
            rusqlite::params![
                row.id, row.schema_version, row.revision, row.provider, row.item_kind, row.envelope,
                row.created_at, row.updated_at, row.deleted_at
            ],
        )?;
        Ok(count == 1)
    }

    /// One conditional UPDATE guarded by `id` + `revision` + not-tombstoned. This
    /// single statement is the optimistic compare-and-set: `true` iff exactly one
    /// live row matched the expected revision. Concurrent writers cannot both win.
    pub fn update_item_cas(
        &self,
        row: &EncryptedVaultItemRow,
        expected_revision: i64,
    ) -> Result<bool, StorageError> {
        let count = self.conn.execute(
            "UPDATE vault_items SET schema_version = ?1, revision = ?2, provider = ?3, \
             item_kind = ?4, envelope = ?5, updated_at = ?6 \
             WHERE id = ?7 AND revision = ?8 AND deleted_at IS NULL",
            rusqlite::params![
                row.schema_version,
                row.revision,
                row.provider,
                row.item_kind,
                row.envelope,
                row.updated_at,
                row.id,
                expected_revision
            ],
        )?;
        Ok(count == 1)
    }

    /// One conditional UPDATE that tombstones a live row: bump revision, swap in
    /// the tombstone envelope, and stamp `updated_at` + `deleted_at`. Guarded by
    /// `id` + `revision` + not-already-tombstoned, so it is idempotent-safe.
    pub fn tombstone_item_cas(&self, cas: &VaultTombstoneCas<'_>) -> Result<bool, StorageError> {
        let count = self.conn.execute(
            "UPDATE vault_items SET revision = ?1, envelope = ?2, updated_at = ?3, deleted_at = ?4 \
             WHERE id = ?5 AND revision = ?6 AND deleted_at IS NULL",
            rusqlite::params![
                cas.new_revision, cas.envelope, cas.updated_at, cas.deleted_at, cas.id,
                cas.expected_revision
            ],
        )?;
        Ok(count == 1)
    }

    pub fn upsert_device_key(&self, row: &VaultDeviceKeyRow) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO vault_device_keys (device_id, schema_version, wrapped_key, created_at, updated_at, revoked_at) \
             VALUES (?1, ?2, ?3, ?4, ?5, ?6) \
             ON CONFLICT(device_id) DO UPDATE SET \
             schema_version = excluded.schema_version, wrapped_key = excluded.wrapped_key, \
             updated_at = excluded.updated_at, revoked_at = excluded.revoked_at",
            rusqlite::params![
                row.device_id, row.schema_version, row.wrapped_key, row.created_at, row.updated_at, row.revoked_at
            ],
        )?;
        Ok(())
    }

    pub fn upsert_recovery(&self, row: &VaultRecoveryRow) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO vault_recovery (id, schema_version, wrapped_key, created_at, updated_at) \
             VALUES (?1, ?2, ?3, ?4, ?5) \
             ON CONFLICT(id) DO UPDATE SET \
             schema_version = excluded.schema_version, wrapped_key = excluded.wrapped_key, updated_at = excluded.updated_at",
            rusqlite::params![row.id, row.schema_version, row.wrapped_key, row.created_at, row.updated_at],
        )?;
        Ok(())
    }

    pub fn upsert_grant(&self, row: &VaultGrantRow) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO vault_capability_grants (handle, schema_version, session_id, task_id, profile_id, \
             workspace_id, tab_id, tab_generation, top_origin, frame_origin, item_id, item_alias, allowed_fields, \
             policy, issued_at, expires_at, max_uses, used_count, state, revoked_at, revocation_reason) \
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21) \
             ON CONFLICT(handle) DO UPDATE SET \
             tab_generation = excluded.tab_generation, allowed_fields = excluded.allowed_fields, policy = excluded.policy, \
             expires_at = excluded.expires_at, max_uses = excluded.max_uses, used_count = excluded.used_count, \
             state = excluded.state, revoked_at = excluded.revoked_at, revocation_reason = excluded.revocation_reason",
            rusqlite::params![
                row.handle, row.schema_version, row.session_id, row.task_id, row.profile_id, row.workspace_id,
                row.tab_id, row.tab_generation, row.top_origin, row.frame_origin, row.item_id, row.item_alias,
                row.allowed_fields, row.policy, row.issued_at, row.expires_at, row.max_uses, row.used_count,
                row.state, row.revoked_at, row.revocation_reason
            ],
        )?;
        Ok(())
    }

    pub fn upsert_policy(&self, row: &VaultPolicyRow) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO vault_agent_policies (scope, schema_version, policy, item_id, origin, expires_at, updated_at) \
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) \
             ON CONFLICT(scope) DO UPDATE SET \
             schema_version = excluded.schema_version, policy = excluded.policy, item_id = excluded.item_id, \
             origin = excluded.origin, expires_at = excluded.expires_at, updated_at = excluded.updated_at",
            rusqlite::params![
                row.scope,
                row.schema_version,
                row.policy,
                row.item_id,
                row.origin,
                row.expires_at,
                row.updated_at
            ],
        )?;
        Ok(())
    }

    pub fn append_audit(&self, row: &VaultAuditRow) -> Result<(), StorageError> {
        // Hashes are opaque here: the caller must select the head and hash the
        // final row inside vault_audit_transaction. Never rebase hashed fields.
        let tail = self.audit_tail()?;
        let (next, prev_hash) = match &tail {
            Some((seq, hash)) => (
                seq.checked_add(1).ok_or_else(|| {
                    StorageError::Other("vault audit sequence overflow".to_string())
                })?,
                Some(hash.as_slice()),
            ),
            None => (1, None),
        };
        // Explicit forward gaps remain supported, but stale rows and incorrect
        // predecessors must be rejected rather than silently rewritten.
        if row.seq < next || row.prev_hash.as_deref() != prev_hash {
            return Err(StorageError::Other("stale vault audit head".to_string()));
        }
        self.conn.execute(
            "INSERT INTO vault_audit_events (id, schema_version, seq, timestamp, session_id, task_id, profile_id, \
             workspace_id, top_origin, frame_origin, item_id, item_alias, operation, policy, decision, reason, \
             device_name, prev_hash, entry_hash) \
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19)",
            rusqlite::params![
                row.id, row.schema_version, row.seq, row.timestamp, row.session_id, row.task_id, row.profile_id,
                row.workspace_id, row.top_origin, row.frame_origin, row.item_id, row.item_alias, row.operation,
                row.policy, row.decision, row.reason, row.device_name, row.prev_hash, row.entry_hash
            ],
        )?;
        Ok(())
    }

    pub fn upsert_sync_version(&self, row: &VaultSyncVersionRow) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO vault_sync_versions (entity_id, schema_version, version, updated_at, encrypted_payload) \
             VALUES (?1, ?2, ?3, ?4, ?5) \
             ON CONFLICT(entity_id) DO UPDATE SET \
             schema_version = excluded.schema_version, version = excluded.version, updated_at = excluded.updated_at, \
             encrypted_payload = excluded.encrypted_payload",
            rusqlite::params![row.entity_id, row.schema_version, row.version, row.updated_at, row.encrypted_payload],
        )?;
        Ok(())
    }
}

#[cfg(test)]
mod audit_tests;

#[cfg(test)]
mod tests {
    use super::*;

    fn store() -> SqliteStorage {
        SqliteStorage::open_in_memory_with_key("vault-schema-test-key").unwrap()
    }

    fn table_exists(db: &SqliteStorage, name: &str) -> bool {
        db.conn
            .query_row(
                "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1",
                rusqlite::params![name],
                |_| Ok(true),
            )
            .optional()
            .unwrap()
            .unwrap_or(false)
    }

    fn column_names(db: &SqliteStorage, table: &str) -> Vec<String> {
        let mut stmt = db
            .conn
            .prepare(&format!("SELECT name FROM pragma_table_info('{table}')"))
            .unwrap();
        stmt.query_map([], |row| row.get::<_, String>(0))
            .unwrap()
            .collect::<Result<Vec<_>, _>>()
            .unwrap()
    }

    #[test]
    fn fresh_schema_has_version_marker_and_all_tables() {
        let db = store();
        let version: i64 = db
            .conn
            .query_row(
                "SELECT vault_schema_version FROM vault_schema_info WHERE id = 0",
                [],
                |r| r.get(0),
            )
            .unwrap();
        assert_eq!(version, VAULT_SCHEMA_VERSION);
        for t in [
            "vault_metadata",
            "vault_items",
            "vault_device_keys",
            "vault_recovery",
            "vault_capability_grants",
            "vault_agent_policies",
            "vault_audit_events",
            "vault_sync_versions",
        ] {
            assert!(table_exists(&db, t), "missing table {t}");
        }
        assert!(!table_exists(&db, "passwords"), "passwords must be absent");
    }

    #[test]
    fn vault_items_columns_are_ciphertext_only() {
        let db = store();
        let cols = column_names(&db, "vault_items");
        for forbidden in [
            "domain", "username", "password", "notes", "totp", "passkey", "title", "origin",
        ] {
            assert!(
                !cols.iter().any(|c| c == forbidden),
                "forbidden column {forbidden}"
            );
        }
        assert!(cols.iter().any(|c| c == "envelope"));
    }

    #[test]
    fn cutover_drops_preexisting_partial_vault_and_legacy_password_tables() {
        // Given a raw keyed DB pre-seeded with the OLD partial Vault schema and a
        // legacy plaintext passwords table (simulating a stale development DB).
        let conn = Connection::open_in_memory().unwrap();
        conn.pragma_update(None, "key", "cutover-test-key").unwrap();
        conn.execute_batch(
            "CREATE TABLE passwords (id TEXT PRIMARY KEY, domain TEXT, username TEXT, password TEXT);
             CREATE TABLE vault_items (id TEXT PRIMARY KEY, domain TEXT NOT NULL, username TEXT NOT NULL, \
             item_kind TEXT NOT NULL, ciphertext BLOB NOT NULL, created_at TEXT NOT NULL, last_used TEXT, \
             schema_version INTEGER NOT NULL DEFAULT 1);
             CREATE INDEX idx_vault_items_domain ON vault_items(domain);
             INSERT INTO passwords (id, domain, username, password) VALUES ('p1', 'x.com', 'bob', 'hunter2');
             INSERT INTO vault_items (id, domain, username, item_kind, ciphertext, created_at) \
             VALUES ('i1', 'x.com', 'bob', 'login', X'0102', '2020-01-01');",
        )
        .unwrap();

        let db = SqliteStorage {
            conn,
            path: ":memory:".to_string(),
        };
        db.initialize_vault_schema().unwrap();

        // Then the legacy passwords table is gone and vault_items is the new shape.
        assert!(
            !table_exists(&db, "passwords"),
            "legacy passwords must be dropped"
        );
        let cols = column_names(&db, "vault_items");
        assert!(
            cols.iter().any(|c| c == "envelope"),
            "new envelope column present"
        );
        assert!(
            cols.iter().any(|c| c == "revision"),
            "new revision column present"
        );
        assert!(
            !cols.iter().any(|c| c == "domain"),
            "old domain column dropped"
        );
        assert!(
            !cols.iter().any(|c| c == "username"),
            "old username column dropped"
        );
        // And the stale partial row did not survive the destructive cutover.
        let item_count: i64 = db
            .conn
            .query_row("SELECT count(*) FROM vault_items", [], |r| r.get(0))
            .unwrap();
        assert_eq!(item_count, 0, "stale partial rows must not survive cutover");
    }

    #[test]
    fn reinit_is_idempotent_and_preserves_rows() {
        let db = store();
        db.save_vault_item(&EncryptedVaultItemRow {
            id: "keep".to_string(),
            schema_version: 1,
            revision: 1,
            provider: "maho_native".to_string(),
            item_kind: "login".to_string(),
            envelope: vec![9, 9, 9],
            created_at: "2026-07-22T00:00:00Z".to_string(),
            updated_at: "2026-07-22T00:00:00Z".to_string(),
            deleted_at: None,
        })
        .unwrap();
        // Re-running init must be a no-op (version marker matches) and keep rows.
        db.initialize_vault_schema().unwrap();
        assert!(db.get_vault_item("keep").unwrap().is_some());
    }
}
