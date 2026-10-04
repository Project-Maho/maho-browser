use std::path::{Path, PathBuf};
use std::sync::RwLock;
use std::time::Duration;

use rusqlite::{Connection, OpenFlags, OptionalExtension};

use crate::StorageError;

mod vault;
pub use vault::{
    EncryptedVaultItemRow, VaultAuditRow, VaultDeviceKeyRow, VaultGrantRow, VaultMetadataRow,
    VaultPolicyRow, VaultRecoveryRow, VaultSyncVersionRow, VaultTombstoneCas, VaultTx,
};

/// Process-wide SQLCipher key slot. The browser (C++ via OSCrypt) injects the
/// real key once at startup through [`set_sqlcipher_key`]; it is never baked
/// into source. `open`/`open_in_memory` read from here so existing call sites
/// keep their signatures while the key stays dependency-injected.
static SQLCIPHER_KEY: RwLock<Option<String>> = RwLock::new(None);

const SQLITE_BUSY_TIMEOUT: Duration = Duration::from_millis(250);
const SQLITE_SCHEMA_VERSION: i64 = 2;

const REQUIRED_VAULT_TABLES: &[&str] = &[
    "vault_schema_info",
    "vault_metadata",
    "vault_items",
    "vault_device_keys",
    "vault_recovery",
    "vault_capability_grants",
    "vault_agent_policies",
    "vault_audit_events",
    "vault_sync_versions",
];

/// Caller-established state of the SQLCipher key material associated with an
/// existing profile database. `Available` means the caller has verified that
/// this is the persisted key for the database; `Unrecoverable` means key-store
/// checks proved the material missing, empty, corrupt, or replaced.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum VaultDatabaseKey<'key> {
    Available(&'key str),
    Unrecoverable,
}

/// Opaque Vault metadata read by the non-mutating database preflight. Payloads
/// remain uninterpreted here; `maho-core` owns their structural decoding.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct VaultDatabaseSnapshot {
    pub kdf_params: Option<Vec<u8>>,
    pub wrapped_user_key: Option<Vec<u8>>,
    pub wrapped_recovery_key: Option<Vec<u8>>,
    pub wrapped_account_key: Option<Vec<u8>>,
}

/// Storage-layer evidence returned without creating, migrating, repairing, or
/// otherwise mutating the database. `maho-core` maps this evidence to its public
/// typed Vault preflight state.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum VaultDatabaseInspection {
    Absent,
    Readable(VaultDatabaseSnapshot),
    UnrecoverableKey,
    StructuralCorruption,
    PlaintextResidue,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SyncQueueLease {
    pub id: i64,
    pub delivery_id: String,
}

/// Inspect an on-disk SQLCipher/Vault database without changing it.
///
/// Key loss is intentionally caller-established: SQLCipher gives the same
/// `not a database` signal for a wrong key and corrupt encrypted pages, so this
/// function never invents an unsafe oracle. Missing key material is only fatal
/// when a database exists. With verified key material, unreadable pages, failed
/// integrity checks, and broken Vault schema are structural corruption.
pub fn inspect_vault_database(path: &Path, key: VaultDatabaseKey<'_>) -> VaultDatabaseInspection {
    if !path.exists() {
        return VaultDatabaseInspection::Absent;
    }
    if !path.is_file() {
        return VaultDatabaseInspection::StructuralCorruption;
    }
    if is_plaintext_database(path) || migration_backup_path(path).exists() {
        return VaultDatabaseInspection::PlaintextResidue;
    }

    let cipher_key = match key {
        VaultDatabaseKey::Available(key) if !key.is_empty() => key,
        VaultDatabaseKey::Available(_) | VaultDatabaseKey::Unrecoverable => {
            return VaultDatabaseInspection::UnrecoverableKey;
        }
    };

    let Ok(conn) = Connection::open_with_flags(path, OpenFlags::SQLITE_OPEN_READ_ONLY) else {
        return VaultDatabaseInspection::StructuralCorruption;
    };
    if apply_key(&conn, cipher_key).is_err()
        || !database_integrity_is_valid(&conn)
        || !vault_schema_is_valid(&conn)
    {
        return VaultDatabaseInspection::StructuralCorruption;
    }
    if plaintext_vault_shape_exists(&conn) {
        return VaultDatabaseInspection::PlaintextResidue;
    }

    match read_vault_database_snapshot(&conn) {
        Ok(snapshot) => VaultDatabaseInspection::Readable(snapshot),
        Err(_) => VaultDatabaseInspection::StructuralCorruption,
    }
}

fn database_integrity_is_valid(conn: &Connection) -> bool {
    conn.query_row("PRAGMA quick_check(1);", [], |row| row.get::<_, String>(0))
        .is_ok_and(|result| result == "ok")
}

fn vault_schema_is_valid(conn: &Connection) -> bool {
    let version = conn.query_row(
        "SELECT vault_schema_version FROM vault_schema_info WHERE id = 0",
        [],
        |row| row.get::<_, i64>(0),
    );
    if !matches!(version, Ok(version) if version == vault::VAULT_SCHEMA_VERSION) {
        return false;
    }

    REQUIRED_VAULT_TABLES.iter().all(|table| {
        conn.query_row(
            "SELECT EXISTS(SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1)",
            rusqlite::params![table],
            |row| row.get::<_, bool>(0),
        )
        .unwrap_or(false)
    })
}

fn plaintext_vault_shape_exists(conn: &Connection) -> bool {
    let plaintext_table_exists = conn
        .query_row(
            "SELECT EXISTS(
                SELECT 1 FROM sqlite_master
                WHERE type = 'table' AND name IN ('passwords', 'legacy_passwords_archive')
            )",
            [],
            |row| row.get::<_, bool>(0),
        )
        .unwrap_or(false);
    if plaintext_table_exists {
        return true;
    }

    conn.query_row(
        "SELECT EXISTS(
            SELECT 1 FROM pragma_table_info('vault_items')
            WHERE name IN ('domain', 'username', 'password', 'notes', 'totp', 'passkey', 'title', 'origin')
        )",
        [],
        |row| row.get::<_, bool>(0),
    )
    .unwrap_or(false)
}

fn read_vault_database_snapshot(
    conn: &Connection,
) -> Result<VaultDatabaseSnapshot, rusqlite::Error> {
    fn payload(conn: &Connection, slot: &str) -> Result<Option<Vec<u8>>, rusqlite::Error> {
        conn.query_row(
            "SELECT payload FROM vault_metadata WHERE slot = ?1",
            rusqlite::params![slot],
            |row| row.get(0),
        )
        .optional()
    }

    Ok(VaultDatabaseSnapshot {
        kdf_params: payload(conn, "kdf_params")?,
        wrapped_user_key: payload(conn, "wrapped_user_key")?,
        wrapped_recovery_key: payload(conn, "wrapped_recovery_key")?,
        wrapped_account_key: payload(conn, "wrapped_account_key")?,
    })
}

/// Cap for unindexed `LIKE '%q%'` suggestion scans (see `search_bookmarks`).
const SEARCH_RESULT_LIMIT: i64 = 200;

/// Inject the SQLCipher key used by [`SqliteStorage::open`] and
/// [`SqliteStorage::open_in_memory`]. Call once before opening any database.
pub fn set_sqlcipher_key(key: &str) -> Result<(), StorageError> {
    ensure_sqlcipher_key(key)?;
    let mut guard = SQLCIPHER_KEY
        .write()
        .map_err(|_| StorageError::KeyStoreUnavailable)?;
    *guard = Some(key.to_string());
    Ok(())
}

/// Remove the injected key (primarily for teardown / tests).
pub fn clear_sqlcipher_key() -> Result<(), StorageError> {
    let mut guard = SQLCIPHER_KEY
        .write()
        .map_err(|_| StorageError::KeyStoreUnavailable)?;
    *guard = None;
    Ok(())
}

/// Check if the SQLCipher key is configured process-wide.
pub fn is_key_configured() -> bool {
    injected_key().is_ok()
}

fn injected_key() -> Result<String, StorageError> {
    let guard = SQLCIPHER_KEY
        .read()
        .map_err(|_| StorageError::KeyStoreUnavailable)?;
    guard.clone().ok_or(StorageError::KeyNotConfigured)
}

fn ensure_sqlcipher_key(key: &str) -> Result<(), StorageError> {
    if key.is_empty() {
        return Err(StorageError::EmptySqlCipherKey);
    }
    Ok(())
}

fn apply_key(conn: &Connection, key: &str) -> Result<(), StorageError> {
    ensure_sqlcipher_key(key)?;
    conn.pragma_update(None, "key", key)
        .map_err(StorageError::Encryption)
}

fn configure_connection(conn: &Connection, key: &str) -> Result<(), StorageError> {
    apply_key(conn, key)?;
    conn.execute("PRAGMA foreign_keys = ON;", [])?;
    conn.busy_timeout(SQLITE_BUSY_TIMEOUT)?;

    // WAL + NORMAL. Must run AFTER `apply_key`: SQLCipher rejects pragmas on an
    // unkeyed connection.
    //
    // Default rollback-journal + `synchronous = FULL` costs several fsyncs per
    // write, and writes here land on the browser UI thread (every navigation
    // calls `add_history_entry` through the synchronous FFI). WAL removes the
    // journal rewrite; NORMAL stops fsyncing the WAL on every commit.
    //
    // Tradeoff: under WAL, `NORMAL` is durable against process crashes but a
    // host power loss / OS crash can lose the most recent commits (the DB is
    // never corrupted). This matches Chromium's own SQLite defaults.
    //
    // `journal_mode` returns a row, so it must be read with `query_row` —
    // `execute` fails with `ExecuteReturnedResults`. The setting is persisted in
    // the DB header, so re-applying it on every open is a no-op after the first.
    let _: String = conn.query_row("PRAGMA journal_mode = WAL;", [], |row| row.get(0))?;
    conn.execute("PRAGMA synchronous = NORMAL;", [])?;
    Ok(())
}

/// A plaintext SQLite DB can read `sqlite_master` with no key; an encrypted
/// SQLCipher DB errors instead. That difference is how we detect the need to
/// migrate.
fn is_plaintext_database(path: &Path) -> bool {
    let Ok(conn) = Connection::open(path) else {
        return false;
    };
    conn.query_row("SELECT count(*) FROM sqlite_master", [], |r| {
        r.get::<_, i64>(0)
    })
    .is_ok()
}

fn verify_encrypted_opens(path: &Path, key: &str) -> bool {
    let Ok(conn) = Connection::open(path) else {
        return false;
    };
    if apply_key(&conn, key).is_err() {
        return false;
    }
    conn.query_row("SELECT count(*) FROM sqlite_master", [], |r| {
        r.get::<_, i64>(0)
    })
    .is_ok()
}

fn export_encrypted(
    plain_path: &Path,
    encrypted_path: &Path,
    key: &str,
) -> Result<(), StorageError> {
    ensure_sqlcipher_key(key)?;
    let conn = Connection::open(plain_path)?;
    let encrypted_path = encrypted_path.to_string_lossy();
    conn.execute(
        "ATTACH DATABASE ?1 AS encrypted KEY ?2",
        rusqlite::params![encrypted_path.as_ref(), key],
    )
    .map_err(|e| StorageError::Migration(format!("encrypted attach failed: {e}")))?;
    let export_result = conn.execute_batch("SELECT sqlcipher_export('encrypted');");
    let detach_result = conn.execute_batch("DETACH DATABASE encrypted;");
    export_result.map_err(|e| StorageError::Migration(format!("sqlcipher_export failed: {e}")))?;
    detach_result.map_err(|e| StorageError::Migration(format!("encrypted detach failed: {e}")))?;
    Ok(())
}

/// One-time plaintext -> SQLCipher migration. Delegates to the injectable inner
/// so tests can drive the verification-failure (rollback) path deterministically.
fn migrate_plaintext_to_encrypted(db_path: &str, key: &str) -> Result<(), StorageError> {
    migrate_plaintext_to_encrypted_inner(db_path, key, verify_encrypted_opens)
}

fn secure_delete_and_remove(path: &Path) -> std::io::Result<()> {
    use std::io::Write;
    if !path.exists() {
        return Ok(());
    }
    let metadata = std::fs::metadata(path)?;
    let len = metadata.len();

    let mut file = std::fs::OpenOptions::new().write(true).open(path)?;

    let zero_buffer = vec![0u8; 65536];
    let mut remaining = len;
    while remaining > 0 {
        let chunk_size = std::cmp::min(remaining, zero_buffer.len() as u64) as usize;
        file.write_all(&zero_buffer[..chunk_size])?;
        remaining -= chunk_size as u64;
    }
    file.sync_all()?;
    drop(file);

    std::fs::remove_file(path)?;
    Ok(())
}

fn migrate_plaintext_to_encrypted_inner(
    db_path: &str,
    key: &str,
    verify: impl Fn(&Path, &str) -> bool,
) -> Result<(), StorageError> {
    if db_path == ":memory:" {
        return Ok(());
    }
    let path = Path::new(db_path);

    let backup_path = migration_backup_path(path);
    // A crash between the two renames leaves the original only at the backup
    // path. Restore it before a missing canonical path can become a new profile.
    if backup_path.exists() && !path.exists() {
        std::fs::rename(&backup_path, path)
            .map_err(|e| StorageError::Migration(format!("backup recovery failed: {e}")))?;
    }
    // An existing encrypted database does not prove a leftover backup belongs
    // to it. Only delete a backup created by the verified swap below.

    if !path.exists() || !is_plaintext_database(path) {
        return Ok(());
    }

    if backup_path.exists() {
        return Err(StorageError::Migration(
            "unresolved plaintext backup; original recovery copy left intact".to_string(),
        ));
    }
    let temp_path = migration_temp_path(path);
    if temp_path.exists() {
        std::fs::remove_file(&temp_path).map_err(|e| {
            StorageError::Migration(format!("could not clear stale temp file: {e}"))
        })?;
    }

    if let Err(e) = export_encrypted(path, &temp_path, key) {
        cleanup_migration_temp(&temp_path, "after export failure")?;
        return Err(e);
    }

    if !verify(&temp_path, key) {
        cleanup_migration_temp(&temp_path, "after verification failure")?;
        return Err(StorageError::Migration(
            "encrypted copy failed verification; original left intact".to_string(),
        ));
    }

    std::fs::rename(path, &backup_path)
        .map_err(|e| StorageError::Migration(format!("backup rename failed: {e}")))?;
    if let Err(e) = std::fs::rename(&temp_path, path) {
        let restore_result = std::fs::rename(&backup_path, path);
        let cleanup_result = std::fs::remove_file(&temp_path);
        let mut message = format!("atomic swap failed: {e}");
        match restore_result {
            Ok(()) => message.push_str("; original restored"),
            Err(restore_error) => {
                message.push_str(&format!(
                    "; original backup restore failed: {restore_error}"
                ));
            }
        }
        if let Err(cleanup_error) = cleanup_result {
            message.push_str(&format!("; temp cleanup failed: {cleanup_error}"));
        }
        return Err(StorageError::Migration(message));
    }

    // Securely delete and remove plain_backup file on success
    if let Err(e) = secure_delete_and_remove(&backup_path) {
        eprintln!(
            "Warning: Failed to securely delete plain backup DB at {}: {}",
            backup_path.display(),
            e
        );
    }

    Ok(())
}

fn migration_temp_path(path: &Path) -> PathBuf {
    path.with_extension("db.encrypting")
}

fn migration_backup_path(path: &Path) -> PathBuf {
    path.with_extension("db.plain_backup")
}

fn cleanup_migration_temp(path: &Path, context: &str) -> Result<(), StorageError> {
    if !path.exists() {
        return Ok(());
    }
    std::fs::remove_file(path)
        .map_err(|e| StorageError::Migration(format!("could not clear temp file {context}: {e}")))
}

type BookmarkRecord = (String, String, String, Option<String>, String);
type PasswordRecord = (
    String,
    String,
    String,
    String,
    Option<String>,
    Option<String>,
);
pub type AutofillPaymentRecord = (String, String, String, String, Option<String>);
pub type EntityVersionRecord = (u64, u32, Option<String>, Option<String>);
pub type MemoryRecord = (
    String,
    String,
    String,
    Option<String>,
    String,
    f64,
    Option<String>,
);
type AutofillAddressRecord = (
    String,
    String,
    String,
    String,
    String,
    String,
    String,
    Option<String>,
    Option<String>,
    Option<String>,
);
type BookmarkFolderRecord = (String, String, Option<String>, String);

pub struct AutofillAddressParams<'a> {
    pub id: &'a str,
    pub name: &'a str,
    pub street: &'a str,
    pub city: &'a str,
    pub state: &'a str,
    pub zip: &'a str,
    pub country: &'a str,
    pub phone: Option<&'a str>,
    pub email: Option<&'a str>,
    pub address_line2: Option<&'a str>,
}

pub struct MemoryInsertParams<'a> {
    pub id: &'a str,
    pub fact: &'a str,
    pub source: &'a str,
    pub session_id: Option<&'a str>,
    pub categories: &'a str,
    pub importance: f64,
    pub metadata: Option<&'a str>,
}

/// One persisted agent-session runtime-config row (Wave 1A). Stored verbatim;
/// consumers (maho-ffi session-open) re-validate the tier against the
/// canonical tier set so a hand-edited row fails closed.
#[derive(Debug, Clone, PartialEq)]
pub struct AgentRuntimeConfigRow {
    pub session_id: String,
    pub permission_tier: String,
    pub final_confirm: bool,
    pub proactive_mode: bool,
}

/// SQLite storage for persistent data (history, bookmarks, settings)
pub struct SqliteStorage {
    pub(crate) conn: Connection,
    path: String,
}

impl SqliteStorage {
    /// Open (creating if needed) an encrypted database at `path`, migrating a
    /// pre-existing plaintext database to SQLCipher first. The key is
    /// dependency-injected by the caller.
    pub fn open_with_key(path: &str, key: &str) -> Result<Self, StorageError> {
        migrate_plaintext_to_encrypted(path, key)?;
        let conn = Connection::open(path)?;
        configure_connection(&conn, key)?;
        let storage = Self {
            conn,
            path: path.to_string(),
        };
        storage.initialize_schema()?;
        Ok(storage)
    }

    /// Open the on-disk database using the process-wide injected key.
    /// Returns [`StorageError::KeyNotConfigured`] if no key was set.
    pub fn open(path: &str) -> Result<Self, StorageError> {
        let key = injected_key()?;
        Self::open_with_key(path, &key)
    }

    /// Open an in-memory database with a caller-supplied key.
    pub fn open_in_memory_with_key(key: &str) -> Result<Self, StorageError> {
        let conn = Connection::open_in_memory()?;
        configure_connection(&conn, key)?;
        let storage = Self {
            conn,
            path: ":memory:".to_string(),
        };
        storage.initialize_schema()?;
        Ok(storage)
    }

    /// Open an in-memory database using the process-wide injected key.
    pub fn open_in_memory() -> Result<Self, StorageError> {
        let key = injected_key()?;
        Self::open_in_memory_with_key(&key)
    }

    pub fn path(&self) -> &str {
        &self.path
    }

    fn run_migrations(&self) -> Result<(), StorageError> {
        let _ = self.conn.execute(
            "ALTER TABLE shortcuts ADD COLUMN enabled INTEGER NOT NULL DEFAULT 1",
            [],
        );
        let _ = self.conn.execute(
            "ALTER TABLE shortcuts ADD COLUMN updated_at INTEGER NOT NULL DEFAULT 0",
            [],
        );

        // downloads: completed_at + mime_type added post-v1 (existing DBs lack them).
        let _ = self
            .conn
            .execute("ALTER TABLE downloads ADD COLUMN completed_at TEXT", []);
        let _ = self
            .conn
            .execute("ALTER TABLE downloads ADD COLUMN mime_type TEXT", []);
        let _ = self
            .conn
            .execute("ALTER TABLE downloads ADD COLUMN chromium_guid TEXT", []);

        // atc_rules: match_type added post-v1. Traffic rules persisted before
        // this column existed lose their match type and are re-inferred on
        // load (see MahoCore ATC restore).
        let _ = self
            .conn
            .execute("ALTER TABLE atc_rules ADD COLUMN match_type TEXT", []);

        let has_dot_pos_x: bool = self
            .conn
            .query_row(
                "SELECT 1 FROM pragma_table_info('boosts') WHERE name='dot_pos_x'",
                [],
                |_| Ok(true),
            )
            .unwrap_or(false);

        if !has_dot_pos_x {
            let _ = self.conn.execute(
                "ALTER TABLE boosts ADD COLUMN dot_pos_x REAL NOT NULL DEFAULT 0.76",
                [],
            );
            let _ = self.conn.execute(
                "ALTER TABLE boosts ADD COLUMN dot_pos_y REAL NOT NULL DEFAULT 0.66",
                [],
            );
            let _ = self.conn.execute(
                "ALTER TABLE boosts ADD COLUMN dot_distance REAL NOT NULL DEFAULT 0",
                [],
            );
            let _ = self.conn.execute(
                "ALTER TABLE boosts ADD COLUMN secondary_dot_pos_x REAL NOT NULL DEFAULT 0.5",
                [],
            );
            let _ = self.conn.execute(
                "ALTER TABLE boosts ADD COLUMN secondary_dot_pos_y REAL NOT NULL DEFAULT 0.81",
                [],
            );
            let _ = self.conn.execute(
                "ALTER TABLE boosts ADD COLUMN change_was_made INTEGER NOT NULL DEFAULT 0",
                [],
            );
            let _ = self.conn.execute(
                "ALTER TABLE boosts ADD COLUMN size_mode TEXT NOT NULL DEFAULT 'k100'",
                [],
            );

            self.conn.execute_batch(
                "UPDATE boosts SET size_mode = CASE
                    WHEN size_override IS NULL OR abs(size_override - 1.0) < 0.01 THEN 'k100'
                    WHEN abs(size_override - 0.9) < 0.06 THEN 'k90'
                    WHEN abs(size_override - 1.1) < 0.06 THEN 'k110'
                    WHEN abs(size_override - 1.25) < 0.08 THEN 'k125'
                    WHEN abs(size_override - 1.5) < 0.13 THEN 'k150'
                    WHEN size_override < 0.95 THEN 'k90'
                    WHEN size_override < 1.05 THEN 'k100'
                    WHEN size_override < 1.175 THEN 'k110'
                    WHEN size_override < 1.375 THEN 'k125'
                    ELSE 'k150'
                END;",
            )?;
        }

        // --- Conversation Migration (Legacy Schema Detection & Migration) ---
        let table_exists = |table_name: &str| -> bool {
            self.conn
                .query_row(
                    "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1",
                    rusqlite::params![table_name],
                    |_| Ok(true),
                )
                .unwrap_or(false)
        };

        if table_exists("chat_sessions") {
            let mut stmt = self
                .conn
                .prepare("SELECT id, title, created_at FROM chat_sessions")?;
            let sessions: Vec<(String, Option<String>, String)> = stmt
                .query_map([], |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)))?
                .collect::<Result<_, _>>()?;
            for (id, title, created_at) in sessions {
                let _ = self.conn.execute(
                    "INSERT OR IGNORE INTO conversations (id, title, created_at, updated_at) VALUES (?1, ?2, ?3, ?3)",
                    rusqlite::params![id, title, created_at],
                );
            }
            if table_exists("chat_messages") {
                let mut stmt = self.conn.prepare(
                    "SELECT id, session_id, role, content, created_at FROM chat_messages",
                )?;
                let messages: Vec<(String, String, String, String, String)> = stmt
                    .query_map([], |row| {
                        Ok((
                            row.get(0)?,
                            row.get(1)?,
                            row.get(2)?,
                            row.get(3)?,
                            row.get(4)?,
                        ))
                    })?
                    .collect::<Result<_, _>>()?;
                for (id, session_id, role, content, created_at) in messages {
                    let _ = self.conn.execute(
                        "INSERT OR IGNORE INTO conversation_turns (id, session_id, role, content, created_at) VALUES (?1, ?2, ?3, ?4, ?5)",
                        rusqlite::params![id, session_id, role, content, created_at],
                    );
                }
                let _ = self.conn.execute("DROP TABLE chat_messages", []);
            }
            let _ = self.conn.execute("DROP TABLE chat_sessions", []);
        }

        if table_exists("ai_sessions") {
            let mut stmt = self
                .conn
                .prepare("SELECT id, title, created_at FROM ai_sessions")?;
            let sessions: Vec<(String, Option<String>, String)> = stmt
                .query_map([], |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)))?
                .collect::<Result<_, _>>()?;
            for (id, title, created_at) in sessions {
                let _ = self.conn.execute(
                    "INSERT OR IGNORE INTO conversations (id, title, created_at, updated_at) VALUES (?1, ?2, ?3, ?3)",
                    rusqlite::params![id, title, created_at],
                );
            }
            if table_exists("ai_messages") {
                let mut stmt = self
                    .conn
                    .prepare("SELECT id, session_id, role, content, created_at FROM ai_messages")?;
                let messages: Vec<(String, String, String, String, String)> = stmt
                    .query_map([], |row| {
                        Ok((
                            row.get(0)?,
                            row.get(1)?,
                            row.get(2)?,
                            row.get(3)?,
                            row.get(4)?,
                        ))
                    })?
                    .collect::<Result<_, _>>()?;
                for (id, session_id, role, content, created_at) in messages {
                    let _ = self.conn.execute(
                        "INSERT OR IGNORE INTO conversation_turns (id, session_id, role, content, created_at) VALUES (?1, ?2, ?3, ?4, ?5)",
                        rusqlite::params![id, session_id, role, content, created_at],
                    );
                }
                let _ = self.conn.execute("DROP TABLE ai_messages", []);
            }
            let _ = self.conn.execute("DROP TABLE ai_sessions", []);
        }

        if table_exists("session_event_logs") {
            let has_role: bool = self
                .conn
                .query_row(
                    "SELECT 1 FROM pragma_table_info('session_event_logs') WHERE name='role'",
                    [],
                    |_| Ok(true),
                )
                .unwrap_or(false);

            if has_role {
                let mut stmt = self.conn.prepare(
                    "SELECT session_id, role, content, created_at FROM session_event_logs",
                )?;
                let logs: Vec<(String, String, String, String)> = stmt
                    .query_map([], |row| {
                        Ok((row.get(0)?, row.get(1)?, row.get(2)?, row.get(3)?))
                    })?
                    .collect::<Result<_, _>>()?;
                for (session_id, role, content, created_at) in logs {
                    let _ = self.conn.execute(
                        "INSERT OR IGNORE INTO conversations (id, title, created_at, updated_at) VALUES (?1, ?2, ?3, ?3)",
                        rusqlite::params![session_id, "Migrated Session", created_at],
                    );
                    let turn_id = uuid::Uuid::new_v4().to_string();
                    let _ = self.conn.execute(
                        "INSERT OR IGNORE INTO conversation_turns (id, session_id, role, content, created_at) VALUES (?1, ?2, ?3, ?4, ?5)",
                        rusqlite::params![turn_id, session_id, role, content, created_at],
                    );
                }
            }
            let _ = self.conn.execute("DROP TABLE session_event_logs", []);
        }

        let has_archived_at = self
            .conn
            .query_row(
                "SELECT 1 FROM pragma_table_info('conversations') WHERE name='archived_at'",
                [],
                |_| Ok(true),
            )
            .unwrap_or(false);
        if !has_archived_at {
            self.conn
                .execute("ALTER TABLE conversations ADD COLUMN archived_at TEXT", [])?;
        }
        self.conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS conversation_projects (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                created_at TEXT NOT NULL DEFAULT (datetime('now')),
                updated_at TEXT NOT NULL DEFAULT (datetime('now'))
            );",
        )?;
        let has_project_id = self
            .conn
            .query_row(
                "SELECT 1 FROM pragma_table_info('conversations') WHERE name='project_id'",
                [],
                |_| Ok(true),
            )
            .unwrap_or(false);
        if !has_project_id {
            self.conn
                .execute("ALTER TABLE conversations ADD COLUMN project_id TEXT", [])?;
        }
        let has_project_foreign_key = {
            let mut stmt = self
                .conn
                .prepare("PRAGMA foreign_key_list('conversations')")?;
            let rows = stmt.query_map([], |row| {
                Ok((
                    row.get::<_, String>(2)?,
                    row.get::<_, String>(3)?,
                    row.get::<_, String>(6)?,
                ))
            })?;
            let has_foreign_key = rows.filter_map(Result::ok).any(|(table, from, on_delete)| {
                table == "conversation_projects"
                    && from == "project_id"
                    && on_delete.eq_ignore_ascii_case("SET NULL")
            });
            has_foreign_key
        };
        if !has_project_foreign_key {
            let has_system_prompt_snapshot = self
                .conn
                .query_row(
                    "SELECT 1 FROM pragma_table_info('conversations') WHERE name='system_prompt_snapshot'",
                    [],
                    |_| Ok(true),
                )
                .unwrap_or(false);
            self.conn.execute_batch("PRAGMA foreign_keys=OFF;")?;
            let rebuild_result = (|| -> Result<(), StorageError> {
                let tx = self.conn.unchecked_transaction()?;
                tx.execute_batch(
                    "CREATE TABLE conversations_with_project_fk (
                        id TEXT PRIMARY KEY,
                        title TEXT,
                        space_id TEXT,
                        model TEXT,
                        created_at TEXT NOT NULL DEFAULT (datetime('now')),
                        updated_at TEXT NOT NULL DEFAULT (datetime('now')),
                        archived_at TEXT,
                        project_id TEXT REFERENCES conversation_projects(id) ON DELETE SET NULL,
                        system_prompt_snapshot TEXT
                    );",
                )?;
                let snapshot_source = if has_system_prompt_snapshot {
                    "system_prompt_snapshot"
                } else {
                    "NULL"
                };
                tx.execute_batch(&format!(
                    "INSERT INTO conversations_with_project_fk
                        (id,title,space_id,model,created_at,updated_at,archived_at,project_id,system_prompt_snapshot)
                     SELECT id,title,space_id,model,created_at,updated_at,archived_at,
                        CASE WHEN project_id IS NULL OR EXISTS(
                            SELECT 1 FROM conversation_projects WHERE conversation_projects.id=conversations.project_id
                        ) THEN project_id ELSE NULL END,
                        {snapshot_source}
                     FROM conversations;
                     DROP TABLE conversations;
                     ALTER TABLE conversations_with_project_fk RENAME TO conversations;"
                ))?;
                tx.commit()?;
                Ok(())
            })();
            let foreign_keys_result = self.conn.execute_batch("PRAGMA foreign_keys=ON;");
            rebuild_result?;
            foreign_keys_result?;
        }
        self.conn.execute_batch(
            "CREATE INDEX IF NOT EXISTS idx_conversations_active_updated
                ON conversations(updated_at DESC) WHERE archived_at IS NULL;
            CREATE INDEX IF NOT EXISTS idx_conversations_archived_at
                ON conversations(archived_at DESC) WHERE archived_at IS NOT NULL;
            CREATE INDEX IF NOT EXISTS idx_conversations_project_id
                ON conversations(project_id);",
        )?;

        self.run_ai_extensibility_migrations()?;

        self.run_memory_index_migrations()?;

        self.run_routines_migrations()?;

        Ok(())
    }

    /// Additive change journal used by the persistent memory ANN index.
    ///
    /// Triggers keep the journal complete even when memory rows are changed by
    /// pruning or future call sites that bypass the storage helper methods.
    fn run_memory_index_migrations(&self) -> Result<(), StorageError> {
        self.conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS memory_index_changes (
                sequence INTEGER PRIMARY KEY AUTOINCREMENT,
                fact_id TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_memory_index_changes_fact
                ON memory_index_changes(fact_id, sequence DESC);
            CREATE TRIGGER IF NOT EXISTS memory_index_track_insert
                AFTER INSERT ON memory_facts
                BEGIN
                    INSERT INTO memory_index_changes(fact_id) VALUES (NEW.id);
                END;
            CREATE TRIGGER IF NOT EXISTS memory_index_track_embedding_update
                AFTER UPDATE OF embedding ON memory_facts
                BEGIN
                    INSERT INTO memory_index_changes(fact_id) VALUES (NEW.id);
                END;
            CREATE TRIGGER IF NOT EXISTS memory_index_track_delete
                AFTER DELETE ON memory_facts
                BEGIN
                    INSERT INTO memory_index_changes(fact_id) VALUES (OLD.id);
                END;",
        )?;
        Ok(())
    }

    /// Additive migration for user-configurable Routines (P2.3).
    ///
    /// `event_trigger` (not `trigger`, which is a SQLite reserved keyword) holds
    /// the serialized event kind; `schedule` holds a cron string. A routine uses
    /// exactly one of the two (the other is NULL).
    fn run_routines_migrations(&self) -> Result<(), StorageError> {
        self.conn
            .execute_batch(
                "CREATE TABLE IF NOT EXISTS custom_routines (
                    id TEXT PRIMARY KEY,
                    name TEXT NOT NULL,
                    prompt TEXT NOT NULL,
                    schedule TEXT,
                    event_trigger TEXT,
                    enabled INTEGER NOT NULL DEFAULT 1,
                    created_at TEXT NOT NULL
                );
                CREATE INDEX IF NOT EXISTS idx_custom_routines_enabled ON custom_routines(enabled);",
            )
            .map_err(|e| StorageError::Other(format!("custom_routines migration failed: {e}")))?;

        // Durable routine-run ledger (routine inbox). Each row is one execution of
        // a built-in or user-defined routine. `success = 0` rows carry the error
        // text in `content`. `source` records how the run was triggered
        // ("scheduled" | "manual" | "event"). This is the persistence layer behind
        // the routine inbox: scheduled/manual/event runs are recorded here rather
        // than discarded to logs.
        self.conn
            .execute_batch(
                "CREATE TABLE IF NOT EXISTS routine_results (
                    result_id INTEGER PRIMARY KEY AUTOINCREMENT,
                    routine_id TEXT NOT NULL,
                    ran_at INTEGER NOT NULL,
                    success INTEGER NOT NULL,
                    content TEXT NOT NULL,
                    source TEXT NOT NULL DEFAULT 'manual'
                );
                CREATE INDEX IF NOT EXISTS idx_routine_results_routine
                    ON routine_results(routine_id, ran_at DESC);
                CREATE INDEX IF NOT EXISTS idx_routine_results_ran_at
                    ON routine_results(ran_at DESC);",
            )
            .map_err(|e| StorageError::Other(format!("routine_results migration failed: {e}")))?;
        Ok(())
    }

    fn run_ai_extensibility_migrations(&self) -> Result<(), StorageError> {
        self.conn
            .execute_batch(
                "CREATE TABLE IF NOT EXISTS ai_profiles (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                system_prompt TEXT NOT NULL DEFAULT '',
                preferred_model TEXT,
                tools_json TEXT NOT NULL DEFAULT '[]',
                mcp_servers_json TEXT NOT NULL DEFAULT '[]',
                is_default INTEGER NOT NULL DEFAULT 0,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS ai_workspaces (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                profile_id TEXT REFERENCES ai_profiles(id) ON DELETE SET NULL,
                space_id TEXT,
                workspace_root TEXT,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS ai_mcp_servers (
                id TEXT PRIMARY KEY,
                workspace_id TEXT NOT NULL REFERENCES ai_workspaces(id) ON DELETE CASCADE,
                name TEXT NOT NULL,
                transport TEXT NOT NULL CHECK(transport IN ('stdio', 'http', 'uds')),
                command TEXT,
                url TEXT,
                auth_keychain_id TEXT,
                trusted INTEGER NOT NULL DEFAULT 0,
                trusted_tools_json TEXT,
                timeout_ms INTEGER NOT NULL DEFAULT 60000,
                output_cap_bytes INTEGER NOT NULL DEFAULT 10240,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL,
                socket_path TEXT,
                UNIQUE(workspace_id, name)
            );
            CREATE TABLE IF NOT EXISTS ai_cli_tools (
                id TEXT PRIMARY KEY,
                workspace_id TEXT NOT NULL REFERENCES ai_workspaces(id) ON DELETE CASCADE,
                name TEXT NOT NULL,
                description TEXT NOT NULL,
                command_template TEXT NOT NULL,
                parameters_schema_json TEXT NOT NULL,
                sensitive INTEGER NOT NULL DEFAULT 0,
                timeout_ms INTEGER NOT NULL DEFAULT 60000,
                output_cap_bytes INTEGER NOT NULL DEFAULT 10240,
                working_directory TEXT,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL,
                UNIQUE(workspace_id, name)
            );
            CREATE INDEX IF NOT EXISTS idx_ai_workspaces_space ON ai_workspaces(space_id);
            CREATE INDEX IF NOT EXISTS idx_ai_workspaces_profile ON ai_workspaces(profile_id);
            CREATE INDEX IF NOT EXISTS idx_ai_mcp_servers_workspace ON ai_mcp_servers(workspace_id);
            CREATE INDEX IF NOT EXISTS idx_ai_cli_tools_workspace ON ai_cli_tools(workspace_id);",
            )
            .map_err(|e| StorageError::Other(format!("Migration failed: {e}")))?;

        let has_prompt_col: bool = self.conn.query_row(
            "SELECT 1 FROM pragma_table_info('conversations') WHERE name='system_prompt_snapshot'",
            [],
            |_| Ok(true)
        ).unwrap_or(false);
        if !has_prompt_col {
            let _ = self.conn.execute(
                "ALTER TABLE conversations ADD COLUMN system_prompt_snapshot TEXT",
                [],
            );
        }

        let has_socket_path_col: bool = self
            .conn
            .query_row(
                "SELECT 1 FROM pragma_table_info('ai_mcp_servers') WHERE name='socket_path'",
                [],
                |_| Ok(true),
            )
            .unwrap_or(false);
        if !has_socket_path_col {
            self.conn.execute("PRAGMA foreign_keys=OFF;", [])?;
            let rebuild = (|| -> Result<(), StorageError> {
                let tx = self.conn.unchecked_transaction()?;
                tx.execute_batch(
                    "CREATE TABLE ai_mcp_servers_dg_tmp AS SELECT * FROM ai_mcp_servers;
                    DROP TABLE ai_mcp_servers;
                    CREATE TABLE ai_mcp_servers (
                        id TEXT PRIMARY KEY,
                        workspace_id TEXT NOT NULL REFERENCES ai_workspaces(id) ON DELETE CASCADE,
                        name TEXT NOT NULL,
                        transport TEXT NOT NULL CHECK(transport IN ('stdio', 'http', 'uds')),
                        command TEXT,
                        url TEXT,
                        auth_keychain_id TEXT,
                        trusted INTEGER NOT NULL DEFAULT 0,
                        trusted_tools_json TEXT,
                        timeout_ms INTEGER NOT NULL DEFAULT 60000,
                        output_cap_bytes INTEGER NOT NULL DEFAULT 10240,
                        created_at TEXT NOT NULL,
                        updated_at TEXT NOT NULL,
                        socket_path TEXT,
                        UNIQUE(workspace_id, name)
                    );
                    INSERT INTO ai_mcp_servers (id, workspace_id, name, transport, command, url, auth_keychain_id, trusted, trusted_tools_json, timeout_ms, output_cap_bytes, created_at, updated_at)
                    SELECT id, workspace_id, name, transport, command, url, auth_keychain_id, trusted, trusted_tools_json, timeout_ms, output_cap_bytes, created_at, updated_at
                    FROM ai_mcp_servers_dg_tmp;
                    DROP TABLE ai_mcp_servers_dg_tmp;
                    CREATE INDEX idx_ai_mcp_servers_workspace ON ai_mcp_servers(workspace_id);",
                )?;
                tx.commit()?;
                Ok(())
            })();
            self.conn.execute("PRAGMA foreign_keys=ON;", [])?;
            rebuild?;
        }

        let count: i64 = self
            .conn
            .query_row("SELECT count(*) FROM ai_profiles", [], |row| row.get(0))
            .unwrap_or(0);

        if count == 0 {
            let now = chrono::Utc::now().to_rfc3339();

            self.conn.execute(
                "INSERT INTO ai_profiles (id, name, system_prompt, preferred_model, tools_json, mcp_servers_json, is_default, created_at, updated_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
                rusqlite::params![
                    "blank",
                    "Blank",
                    "",
                    Option::<String>::None,
                    "[]",
                    "[]",
                    1,
                    now,
                    now
                ]
            ).map_err(|e| StorageError::Other(format!("Seeding Blank profile failed: {e}")))?;

            self.conn.execute(
                "INSERT INTO ai_profiles (id, name, system_prompt, preferred_model, tools_json, mcp_servers_json, is_default, created_at, updated_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
                rusqlite::params![
                    "code_reviewer",
                    "Code Reviewer",
                    "Review code for bugs, style issues, and improvements",
                    Option::<String>::None,
                    "[]",
                    "[]",
                    0,
                    now,
                    now
                ]
            ).map_err(|e| StorageError::Other(format!("Seeding Code Reviewer profile failed: {e}")))?;

            self.conn.execute(
                "INSERT INTO ai_profiles (id, name, system_prompt, preferred_model, tools_json, mcp_servers_json, is_default, created_at, updated_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
                rusqlite::params![
                    "researcher",
                    "Researcher",
                    "Search the web and synthesize findings into concise answers",
                    Option::<String>::None,
                    "[]",
                    "[]",
                    0,
                    now,
                    now
                ]
            ).map_err(|e| StorageError::Other(format!("Seeding Researcher profile failed: {e}")))?;

            self.conn.execute(
                "INSERT INTO ai_profiles (id, name, system_prompt, preferred_model, tools_json, mcp_servers_json, is_default, created_at, updated_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
                rusqlite::params![
                    "writer",
                    "Writer",
                    "Help write and edit text with clear, concise prose",
                    Option::<String>::None,
                    "[]",
                    "[]",
                    0,
                    now,
                    now
                ]
            ).map_err(|e| StorageError::Other(format!("Seeding Writer profile failed: {e}")))?;

            self.conn.execute(
                "INSERT INTO ai_profiles (id, name, system_prompt, preferred_model, tools_json, mcp_servers_json, is_default, created_at, updated_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
                rusqlite::params![
                    "debugger",
                    "Debugger",
                    "Help diagnose and fix bugs step by step",
                    Option::<String>::None,
                    "[]",
                    "[]",
                    0,
                    now,
                    now
                ]
            ).map_err(|e| StorageError::Other(format!("Seeding Debugger profile failed: {e}")))?;
        }

        Ok(())
    }

    fn initialize_schema(&self) -> Result<(), StorageError> {
        let schema_version = self
            .conn
            .query_row("PRAGMA user_version", [], |row| row.get::<_, i64>(0))?;
        if schema_version >= SQLITE_SCHEMA_VERSION {
            return Ok(());
        }

        let has_old_schema: bool = self
            .conn
            .query_row(
                "SELECT 1 FROM pragma_table_info('boosts') WHERE name='url_pattern'",
                [],
                |_| Ok(true),
            )
            .unwrap_or(false);
        if has_old_schema {
            let _ = self
                .conn
                .execute("DROP TABLE IF EXISTS domain_boost_state", []);
            let _ = self.conn.execute("DROP TABLE IF EXISTS boosts", []);
            let _ = self
                .conn
                .execute("DROP INDEX IF EXISTS idx_boosts_url_pattern", []);
        }

        self.conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                url TEXT NOT NULL,
                title TEXT NOT NULL,
                visited_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS bookmarks (
                id TEXT PRIMARY KEY,
                url TEXT NOT NULL,
                title TEXT NOT NULL,
                folder_id TEXT,
                created_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS settings (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS notes (
                id TEXT PRIMARY KEY,
                linked_tab_id TEXT,
                linked_url TEXT,
                content TEXT NOT NULL,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS boosts (
                id TEXT PRIMARY KEY,
                domain TEXT NOT NULL,
                name TEXT NOT NULL,
                enable_color_boost INTEGER NOT NULL DEFAULT 0,
                dot_angle_deg REAL NOT NULL DEFAULT 0.0,
                secondary_dot_angle_deg_delta REAL NOT NULL DEFAULT 0.0,
                brightness REAL NOT NULL DEFAULT 1.0,
                saturation REAL NOT NULL DEFAULT 1.0,
                contrast REAL NOT NULL DEFAULT 1.0,
                auto_theme INTEGER NOT NULL DEFAULT 0,
                smart_invert INTEGER NOT NULL DEFAULT 0,
                font_family TEXT,
                text_case_override TEXT NOT NULL DEFAULT 'none',
                size_override REAL,
                zap_selectors TEXT NOT NULL DEFAULT '[]',
                custom_css TEXT NOT NULL DEFAULT '',
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS domain_boost_state (
                domain TEXT PRIMARY KEY,
                active_boost_id TEXT,
                FOREIGN KEY(active_boost_id) REFERENCES boosts(id) ON DELETE SET NULL
            );
            CREATE TABLE IF NOT EXISTS css_mods (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                description TEXT,
                author TEXT,
                version TEXT,
                css TEXT NOT NULL,
                enabled INTEGER NOT NULL DEFAULT 1,
                homepage TEXT,
                source_url TEXT,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS permissions (
                origin TEXT NOT NULL,
                permission TEXT NOT NULL,
                policy TEXT NOT NULL DEFAULT 'ask',
                PRIMARY KEY (origin, permission)
            );
            CREATE TABLE IF NOT EXISTS content_blocker_lists (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                url TEXT NOT NULL,
                enabled INTEGER NOT NULL DEFAULT 1,
                raw_content TEXT NOT NULL DEFAULT '',
                rule_count INTEGER NOT NULL DEFAULT 0,
                etag TEXT,
                last_modified TEXT,
                sha256 TEXT,
                last_attempt_timestamp INTEGER,
                last_success_timestamp INTEGER,
                failure_count INTEGER NOT NULL DEFAULT 0,
                last_status INTEGER,
                last_error TEXT
            );
            CREATE TABLE IF NOT EXISTS content_blocker_exceptions (
                key TEXT PRIMARY KEY,
                created_at INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS content_blocker_state (
                id INTEGER PRIMARY KEY CHECK (id = 1),
                mode TEXT NOT NULL DEFAULT 'native',
                generation INTEGER NOT NULL DEFAULT 0,
                engine_cache BLOB,
                engine_version TEXT,
                engine_hash TEXT,
                updated_at INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS atc_rules (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                space_id TEXT,
                url_pattern TEXT,
                max_age_hours INTEGER,
                max_tabs INTEGER,
                enabled INTEGER NOT NULL DEFAULT 1,
                match_type TEXT
            );
            CREATE TABLE IF NOT EXISTS search_history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                query TEXT NOT NULL,
                searched_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS per_site_zoom (
                origin TEXT PRIMARY KEY,
                zoom_level REAL NOT NULL DEFAULT 1.0
            );
            CREATE TABLE IF NOT EXISTS downloads (
                id TEXT PRIMARY KEY,
                filename TEXT NOT NULL,
                url TEXT NOT NULL,
                total_bytes INTEGER NOT NULL DEFAULT 0,
                received_bytes INTEGER NOT NULL DEFAULT 0,
                state TEXT NOT NULL DEFAULT 'downloading',
                file_path TEXT,
                started_at TEXT NOT NULL,
                completed_at TEXT,
                mime_type TEXT,
                chromium_guid TEXT
            );
            CREATE TABLE IF NOT EXISTS bookmark_folders (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                parent_id TEXT,
                created_at TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_history_url ON history(url);
            CREATE INDEX IF NOT EXISTS idx_history_visited ON history(visited_at);
            CREATE INDEX IF NOT EXISTS idx_notes_linked_tab ON notes(linked_tab_id);
            CREATE INDEX IF NOT EXISTS idx_notes_linked_url ON notes(linked_url);
            CREATE INDEX IF NOT EXISTS idx_boosts_domain ON boosts(domain);
            CREATE INDEX IF NOT EXISTS idx_search_history_searched ON search_history(searched_at);
            CREATE INDEX IF NOT EXISTS idx_downloads_state ON downloads(state);
            CREATE TABLE IF NOT EXISTS usage_stats (
                item_key TEXT PRIMARY KEY,
                item_type TEXT NOT NULL,
                usage_count INTEGER NOT NULL DEFAULT 0,
                last_used_at TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_usage_stats_type ON usage_stats(item_type);
            CREATE INDEX IF NOT EXISTS idx_usage_stats_count ON usage_stats(usage_count DESC);
            CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(note_id, content, tokenize='porter unicode61');
            CREATE TABLE IF NOT EXISTS profiles (
                id TEXT PRIMARY KEY,
                data TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS autofill_addresses (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                street TEXT NOT NULL,
                city TEXT NOT NULL,
                state TEXT NOT NULL,
                zip TEXT NOT NULL,
                country TEXT NOT NULL,
                phone TEXT,
                email TEXT,
                address_line2 TEXT
            );
            CREATE TABLE IF NOT EXISTS autofill_payments (
                id TEXT PRIMARY KEY,
                card_name TEXT NOT NULL,
                last_four TEXT NOT NULL,
                expiry TEXT NOT NULL,
                card_network TEXT
            );
            CREATE TABLE IF NOT EXISTS reading_list (
                id TEXT PRIMARY KEY,
                url TEXT NOT NULL,
                title TEXT NOT NULL,
                excerpt TEXT,
                site_name TEXT,
                favicon_url TEXT,
                preview_image_url TEXT,
                added_at TEXT NOT NULL,
                read_at TEXT,
                is_read INTEGER NOT NULL DEFAULT 0,
                estimated_read_minutes INTEGER,
                tags TEXT
            );
            CREATE INDEX IF NOT EXISTS idx_reading_list_added ON reading_list(added_at);
            CREATE TABLE IF NOT EXISTS search_engines (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                url_template TEXT NOT NULL,
                shortcut TEXT,
                icon_url TEXT,
                is_default INTEGER NOT NULL DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS shortcuts (
                action TEXT PRIMARY KEY,
                key_combo_json TEXT NOT NULL,
                is_custom INTEGER NOT NULL DEFAULT 1,
                enabled INTEGER NOT NULL DEFAULT 1
            );
            CREATE TABLE IF NOT EXISTS easels (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                canvas_items_json TEXT NOT NULL DEFAULT '[]',
                viewport_json TEXT NOT NULL DEFAULT '{}',
                created_at TEXT NOT NULL DEFAULT (datetime('now')),
                updated_at TEXT NOT NULL DEFAULT (datetime('now'))
            );
            CREATE TABLE IF NOT EXISTS sync_queue (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                entity_type TEXT NOT NULL,
                entity_id TEXT NOT NULL,
                version INTEGER NOT NULL,
                modified_at INTEGER NOT NULL,
                payload_json TEXT NOT NULL,
                deleted INTEGER NOT NULL DEFAULT 0,
                created_at INTEGER NOT NULL,
                sent_at INTEGER,
                acked_at INTEGER,
                delivery_id TEXT NOT NULL,
                state TEXT NOT NULL DEFAULT 'pending',
                attempt_count INTEGER NOT NULL DEFAULT 0,
                last_attempt_at INTEGER,
                relay_seq INTEGER
            );
            CREATE TABLE IF NOT EXISTS sync_state (
                entity_type TEXT PRIMARY KEY,
                last_pulled_version INTEGER NOT NULL DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS shared_collections (
                id TEXT PRIMARY KEY,
                space_id TEXT NOT NULL,
                name TEXT NOT NULL,
                permission TEXT NOT NULL DEFAULT 'view',
                member_count INTEGER NOT NULL DEFAULT 1,
                share_link TEXT,
                created_at TEXT NOT NULL DEFAULT (datetime('now'))
            );
            CREATE TABLE IF NOT EXISTS conversation_projects (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                created_at TEXT NOT NULL DEFAULT (datetime('now')),
                updated_at TEXT NOT NULL DEFAULT (datetime('now'))
            );
            CREATE TABLE IF NOT EXISTS conversations (
                id TEXT PRIMARY KEY,
                title TEXT,
                space_id TEXT,
                model TEXT,
                created_at TEXT NOT NULL DEFAULT (datetime('now')),
                updated_at TEXT NOT NULL DEFAULT (datetime('now')),
                archived_at TEXT,
                project_id TEXT REFERENCES conversation_projects(id) ON DELETE SET NULL
            );
            CREATE TABLE IF NOT EXISTS conversation_turns (
                id TEXT PRIMARY KEY,
                session_id TEXT NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
                role TEXT NOT NULL,
                content TEXT NOT NULL,
                url_context TEXT,
                created_at TEXT NOT NULL DEFAULT (datetime('now'))
            );
            CREATE INDEX IF NOT EXISTS idx_conversation_turns_session ON conversation_turns(session_id, created_at);
            CREATE TABLE IF NOT EXISTS session_artifacts (
                artifact_id TEXT PRIMARY KEY,
                session_id TEXT NOT NULL,
                display_name TEXT NOT NULL,
                mime_type TEXT NOT NULL,
                size_bytes INTEGER NOT NULL,
                storage_rel_path TEXT NOT NULL,
                created_at_ms INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_session_artifacts_session ON session_artifacts(session_id, created_at_ms);
            CREATE TABLE IF NOT EXISTS agent_runtime_config (
                session_id TEXT PRIMARY KEY,
                permission_tier TEXT NOT NULL,
                final_confirm INTEGER NOT NULL,
                proactive_mode INTEGER NOT NULL,
                updated_at TEXT NOT NULL DEFAULT (datetime('now'))
            );
            CREATE TABLE IF NOT EXISTS memory_facts (
                id TEXT PRIMARY KEY,
                fact TEXT NOT NULL,
                source TEXT NOT NULL,
                session_id TEXT,
                categories TEXT NOT NULL DEFAULT '[]',
                importance REAL NOT NULL DEFAULT 0.5,
                metadata TEXT,
                embedding BLOB,
                created_at TEXT NOT NULL DEFAULT (datetime('now'))
            );
            CREATE VIRTUAL TABLE IF NOT EXISTS memory_facts_fts USING fts5(id UNINDEXED, fact, tokenize='porter unicode61');
            CREATE TABLE IF NOT EXISTS memory_blocks (
                label TEXT PRIMARY KEY,
                content TEXT NOT NULL,
                tier TEXT NOT NULL,
                token_count INTEGER NOT NULL DEFAULT 0,
                updated_at TEXT NOT NULL DEFAULT (datetime('now'))
            );
            CREATE INDEX IF NOT EXISTS idx_memory_blocks_tier ON memory_blocks(tier);
            CREATE TABLE IF NOT EXISTS browse_sessions (
                id TEXT PRIMARY KEY,
                url TEXT NOT NULL,
                title TEXT,
                started_at TEXT NOT NULL DEFAULT (datetime('now'))
            );
            CREATE INDEX IF NOT EXISTS idx_browse_sessions_started ON browse_sessions(started_at);
            CREATE TABLE IF NOT EXISTS hlc_state (
                id INTEGER PRIMARY KEY CHECK (id = 0),
                last_hlc_ts INTEGER NOT NULL DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS entity_versions (
                entity_type TEXT NOT NULL,
                entity_id TEXT NOT NULL,
                hlc_ts INTEGER NOT NULL,
                device_id INTEGER NOT NULL,
                PRIMARY KEY (entity_type, entity_id)
            );
            CREATE INDEX IF NOT EXISTS idx_entity_versions_type ON entity_versions(entity_type);",
        )?;

        let has_artifact_kind: bool = self.conn.query_row(
            "SELECT EXISTS(SELECT 1 FROM pragma_table_info('session_artifacts') WHERE name='kind')",
            [], |row| row.get(0),
        )?;
        if !has_artifact_kind {
            self.conn.execute("ALTER TABLE session_artifacts ADD COLUMN kind TEXT", [])?;
        }

        self.initialize_vault_schema()?;

        let has_acked_at: bool = self
            .conn
            .query_row(
                "SELECT 1 FROM pragma_table_info('sync_queue') WHERE name='acked_at'",
                [],
                |_| Ok(true),
            )
            .unwrap_or(false);
        if !has_acked_at {
            let _ = self
                .conn
                .execute("ALTER TABLE sync_queue ADD COLUMN acked_at INTEGER", []);
        }

        for (column, sql) in [
            (
                "delivery_id",
                "ALTER TABLE sync_queue ADD COLUMN delivery_id TEXT",
            ),
            (
                "state",
                "ALTER TABLE sync_queue ADD COLUMN state TEXT NOT NULL DEFAULT 'pending'",
            ),
            (
                "attempt_count",
                "ALTER TABLE sync_queue ADD COLUMN attempt_count INTEGER NOT NULL DEFAULT 0",
            ),
            (
                "last_attempt_at",
                "ALTER TABLE sync_queue ADD COLUMN last_attempt_at INTEGER",
            ),
            (
                "relay_seq",
                "ALTER TABLE sync_queue ADD COLUMN relay_seq INTEGER",
            ),
        ] {
            let exists: bool = self
                .conn
                .query_row(
                    "SELECT 1 FROM pragma_table_info('sync_queue') WHERE name=?1",
                    [column],
                    |_| Ok(true),
                )
                .unwrap_or(false);
            if !exists {
                self.conn.execute(sql, [])?;
            }
        }

        let mut missing_delivery_ids = self
            .conn
            .prepare("SELECT id FROM sync_queue WHERE delivery_id IS NULL OR delivery_id = ''")?;
        let missing_delivery_ids = missing_delivery_ids
            .query_map([], |row| row.get::<_, i64>(0))?
            .collect::<Result<Vec<_>, _>>()?;
        for id in missing_delivery_ids {
            self.conn.execute(
                "UPDATE sync_queue SET delivery_id = ?1 WHERE id = ?2",
                rusqlite::params![uuid::Uuid::new_v4().to_string(), id],
            )?;
        }
        self.conn.execute(
            "UPDATE sync_queue
             SET state = CASE
                 WHEN acked_at IS NOT NULL THEN 'acknowledged'
                 ELSE 'pending'
             END
             WHERE state IS NULL OR state = '' OR state = 'inflight'",
            [],
        )?;
        self.conn.execute(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_sync_queue_delivery_id
             ON sync_queue(delivery_id)",
            [],
        )?;
        self.conn.execute(
            "CREATE TABLE IF NOT EXISTS sync_receive_cursors (
                room_id TEXT PRIMARY KEY,
                last_relay_seq INTEGER NOT NULL DEFAULT 0
            )",
            [],
        )?;
        self.conn.execute(
            "CREATE TABLE IF NOT EXISTS sync_tombstones (
                entity_type TEXT NOT NULL,
                entity_id TEXT NOT NULL,
                hlc_ts INTEGER NOT NULL,
                device_id INTEGER NOT NULL,
                deleted_at INTEGER NOT NULL,
                PRIMARY KEY (entity_type, entity_id)
            )",
            [],
        )?;

        let has_fields_hlc: bool = self
            .conn
            .query_row(
                "SELECT 1 FROM pragma_table_info('entity_versions') WHERE name='fields_hlc_json'",
                [],
                |_| Ok(true),
            )
            .unwrap_or(false);
        if !has_fields_hlc {
            let _ = self.conn.execute(
                "ALTER TABLE entity_versions ADD COLUMN fields_hlc_json TEXT",
                [],
            );
        }

        let has_payload: bool = self
            .conn
            .query_row(
                "SELECT 1 FROM pragma_table_info('entity_versions') WHERE name='payload_json'",
                [],
                |_| Ok(true),
            )
            .unwrap_or(false);
        if !has_payload {
            let _ = self.conn.execute(
                "ALTER TABLE entity_versions ADD COLUMN payload_json TEXT",
                [],
            );
        }
        let has_address_line2: bool = self
            .conn
            .query_row(
                "SELECT 1 FROM pragma_table_info('autofill_addresses') WHERE name='address_line2'",
                [],
                |_| Ok(true),
            )
            .unwrap_or(false);
        if !has_address_line2 {
            let _ = self.conn.execute(
                "ALTER TABLE autofill_addresses ADD COLUMN address_line2 TEXT",
                [],
            );
        }

        let has_card_network: bool = self
            .conn
            .query_row(
                "SELECT 1 FROM pragma_table_info('autofill_payments') WHERE name='card_network'",
                [],
                |_| Ok(true),
            )
            .unwrap_or(false);
        if !has_card_network {
            let _ = self.conn.execute(
                "ALTER TABLE autofill_payments ADD COLUMN card_network TEXT",
                [],
            );
        }

        // Reset sent_at to NULL for old unacknowledged entries older than 30 days to trigger retransmission
        let cutoff_30d = chrono::Utc::now().timestamp() - (30 * 86400);
        let _ = self.conn.execute(
            "UPDATE sync_queue SET sent_at = NULL WHERE sent_at IS NOT NULL AND sent_at < ?1 AND acked_at IS NULL",
            rusqlite::params![cutoff_30d],
        );

        self.run_migrations()?;
        self.conn
            .pragma_update(None, "user_version", SQLITE_SCHEMA_VERSION)?;
        Ok(())
    }

    // === History ===

    pub fn add_history_entry(&self, url: &str, title: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO history (url, title, visited_at) VALUES (?1, ?2, datetime('now'))",
            rusqlite::params![url, title],
        )?;
        Ok(())
    }

    pub fn search_history(
        &self,
        query: &str,
        limit: usize,
    ) -> Result<Vec<(String, String, String)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT url, title, visited_at FROM history WHERE title LIKE ?1 OR url LIKE ?1 ORDER BY visited_at DESC LIMIT ?2",
        )?;
        let pattern = format!("%{query}%");
        let rows = stmt.query_map(rusqlite::params![pattern, limit], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn clear_history(&self) -> Result<(), StorageError> {
        self.conn.execute("DELETE FROM history", [])?;
        Ok(())
    }

    /// Most recently visited distinct URLs, newest first, as
    /// `(url, title_of_latest_visit, latest_visit_unix_millis)`. Used to
    /// rehydrate the in-memory command-bar history at startup.
    pub fn load_recent_history(
        &self,
        limit: usize,
    ) -> Result<Vec<(String, String, u64)>, StorageError> {
        // SQLite resolves the bare `title` column from the row that supplied
        // MAX(visited_at), so each URL carries its latest title.
        let mut stmt = self.conn.prepare(
            "SELECT url, title, MAX(visited_at) AS last_visit FROM history \
             GROUP BY url ORDER BY last_visit DESC LIMIT ?1",
        )?;
        let rows = stmt.query_map(rusqlite::params![limit], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            let (url, title, visited_at) = row?;
            let millis = chrono::NaiveDateTime::parse_from_str(&visited_at, "%Y-%m-%d %H:%M:%S")
                .map(|dt| dt.and_utc().timestamp_millis().max(0) as u64)
                .unwrap_or(0);
            results.push((url, title, millis));
        }
        Ok(results)
    }

    pub fn delete_history_entry(&self, entry_id: i64) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM history WHERE id = ?1",
            rusqlite::params![entry_id],
        )?;
        Ok(affected > 0)
    }

    pub fn delete_history_entry_by_url(&self, url: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM history WHERE url = ?1", rusqlite::params![url])?;
        Ok(affected > 0)
    }

    // === Bookmarks ===

    pub fn save_bookmark(
        &self,
        id: &str,
        url: &str,
        title: &str,
        folder_id: Option<&str>,
        created_at: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO bookmarks (id, url, title, folder_id, created_at) VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params![id, url, title, folder_id, created_at],
        )?;
        Ok(())
    }

    pub fn delete_bookmark(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM bookmarks WHERE id = ?1", rusqlite::params![id])?;
        Ok(affected > 0)
    }

    pub fn search_bookmarks(&self, query: &str) -> Result<Vec<BookmarkRecord>, StorageError> {
        // Bounded: the leading-wildcard LIKE cannot use an index, so this is a
        // full scan + sort. Without a cap it materializes every matching
        // bookmark; callers only ever render a suggestion list.
        let mut stmt = self.conn.prepare(
            "SELECT id, url, title, folder_id, created_at FROM bookmarks WHERE title LIKE ?1 OR url LIKE ?1 ORDER BY created_at DESC LIMIT ?2",
        )?;
        let pattern = format!("%{query}%");
        let rows = stmt.query_map(rusqlite::params![pattern, SEARCH_RESULT_LIMIT], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, Option<String>>(3)?,
                row.get::<_, String>(4)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn get_all_bookmarks(&self) -> Result<Vec<BookmarkRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, url, title, folder_id, created_at FROM bookmarks ORDER BY created_at DESC",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, Option<String>>(3)?,
                row.get::<_, String>(4)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn move_bookmark(&self, id: &str, folder_id: Option<&str>) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "UPDATE bookmarks SET folder_id = ?1 WHERE id = ?2",
            rusqlite::params![folder_id, id],
        )?;
        Ok(affected > 0)
    }

    pub fn save_bookmark_folder(
        &self,
        id: &str,
        name: &str,
        parent_id: Option<&str>,
        created_at: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO bookmark_folders (id, name, parent_id, created_at) VALUES (?1, ?2, ?3, ?4)",
            rusqlite::params![id, name, parent_id, created_at],
        )?;
        Ok(())
    }

    // === Settings ===

    pub fn get_setting(&self, key: &str) -> Result<Option<String>, StorageError> {
        let mut stmt = self
            .conn
            .prepare("SELECT value FROM settings WHERE key = ?1")?;
        let mut rows = stmt.query(rusqlite::params![key])?;
        match rows.next()? {
            Some(row) => Ok(Some(row.get(0)?)),
            None => Ok(None),
        }
    }

    pub fn set_setting(&self, key: &str, value: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO settings (key, value) VALUES (?1, ?2)",
            rusqlite::params![key, value],
        )?;
        Ok(())
    }

    /// Delete a settings row. Returns true when a row was removed, false when the key
    /// was absent (not an error).
    pub fn delete_setting(&self, key: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM settings WHERE key = ?1",
            rusqlite::params![key],
        )?;
        Ok(affected > 0)
    }

    // === Profiles ===

    pub fn save_profile(&self, id: &str, data: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO profiles (id, data) VALUES (?1, ?2)",
            rusqlite::params![id, data],
        )?;
        Ok(())
    }

    pub fn get_all_profiles(&self) -> Result<Vec<String>, StorageError> {
        let mut stmt = self.conn.prepare("SELECT data FROM profiles")?;
        let rows = stmt.query_map([], |row| row.get::<_, String>(0))?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_profile(&self, id: &str) -> Result<(), StorageError> {
        self.conn
            .execute("DELETE FROM profiles WHERE id = ?1", rusqlite::params![id])?;
        Ok(())
    }

    // === Notes ===

    pub fn save_note(
        &self,
        id: &str,
        linked_tab_id: Option<&str>,
        linked_url: Option<&str>,
        content: &str,
        created_at: &str,
        updated_at: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO notes (id, linked_tab_id, linked_url, content, created_at, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
            rusqlite::params![id, linked_tab_id, linked_url, content, created_at, updated_at],
        )?;
        self.index_note_fts(id, content)?;
        Ok(())
    }

    #[allow(clippy::type_complexity)]
    pub fn load_notes(
        &self,
    ) -> Result<
        Vec<(
            String,
            Option<String>,
            Option<String>,
            String,
            String,
            String,
        )>,
        StorageError,
    > {
        let mut stmt = self.conn.prepare(
            "SELECT id, linked_tab_id, linked_url, content, created_at, updated_at FROM notes ORDER BY updated_at DESC",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, Option<String>>(1)?,
                row.get::<_, Option<String>>(2)?,
                row.get::<_, String>(3)?,
                row.get::<_, String>(4)?,
                row.get::<_, String>(5)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_note(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM notes WHERE id = ?1", rusqlite::params![id])?;
        self.remove_note_fts(id)?;
        Ok(affected > 0)
    }

    pub fn update_note_content(
        &self,
        id: &str,
        content: &str,
        updated_at: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "UPDATE notes SET content = ?1, updated_at = ?2 WHERE id = ?3",
            rusqlite::params![content, updated_at, id],
        )?;
        self.index_note_fts(id, content)?;
        Ok(())
    }

    pub fn index_note_fts(&self, note_id: &str, content: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO notes_fts(note_id, content) VALUES (?1, ?2)",
            rusqlite::params![note_id, content],
        )?;
        Ok(())
    }

    pub fn remove_note_fts(&self, note_id: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "DELETE FROM notes_fts WHERE note_id = ?1",
            rusqlite::params![note_id],
        )?;
        Ok(())
    }

    // === Easels ===

    pub fn save_easel(&self, easel: &maho_types::easel::Easel) -> Result<(), StorageError> {
        let canvas_items_json =
            serde_json::to_string(&easel.canvas_items).unwrap_or_else(|_| "[]".to_string());
        let viewport_json =
            serde_json::to_string(&easel.viewport).unwrap_or_else(|_| "{}".to_string());
        self.conn.execute(
            "INSERT OR REPLACE INTO easels (id, name, canvas_items_json, viewport_json, updated_at) VALUES (?1, ?2, ?3, ?4, datetime('now'))",
            rusqlite::params![easel.id.as_ref(), easel.name, canvas_items_json, viewport_json],
        )?;
        Ok(())
    }

    pub fn load_easels(&self) -> Result<Vec<maho_types::easel::Easel>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, canvas_items_json, viewport_json FROM easels ORDER BY updated_at DESC",
        )?;
        let rows = stmt.query_map([], |row| {
            let id: String = row.get(0)?;
            let name: String = row.get(1)?;
            let canvas_items_json: String = row.get(2)?;
            let viewport_json: String = row.get(3)?;
            Ok((id, name, canvas_items_json, viewport_json))
        })?;
        let mut results = Vec::new();
        for row in rows {
            let (id, name, items_json, vp_json) = row?;
            let canvas_items: Vec<maho_types::easel::CanvasItem> =
                serde_json::from_str(&items_json).unwrap_or_default();
            let viewport: maho_types::common::Viewport =
                serde_json::from_str(&vp_json).unwrap_or(maho_types::common::Viewport {
                    offset: maho_types::common::Point { x: 0.0, y: 0.0 },
                    zoom: 1.0,
                });
            results.push(maho_types::easel::Easel {
                id: maho_types::identifiers::EaselId::new(id),
                name,
                canvas_items,
                viewport,
            });
        }
        Ok(results)
    }

    pub fn delete_easel(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM easels WHERE id = ?1", rusqlite::params![id])?;
        Ok(affected > 0)
    }

    pub fn search_notes_fts(&self, query: &str) -> Result<Vec<(String, f64)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT note_id, bm25(notes_fts) as rank FROM notes_fts WHERE notes_fts MATCH ?1 ORDER BY rank LIMIT 50"
        )?;
        let results = stmt
            .query_map(rusqlite::params![query], |row| {
                Ok((row.get::<_, String>(0)?, row.get::<_, f64>(1)?))
            })?
            .filter_map(|r| r.ok())
            .collect();
        Ok(results)
    }

    pub fn reindex_all_notes_fts(&self, notes: &[(String, String)]) -> Result<(), StorageError> {
        self.conn.execute("DELETE FROM notes_fts", [])?;
        let mut stmt = self
            .conn
            .prepare("INSERT INTO notes_fts(note_id, content) VALUES (?1, ?2)")?;
        for (id, content) in notes {
            stmt.execute(rusqlite::params![id, content])?;
        }
        Ok(())
    }

    // === Boosts ===

    pub fn save_boost(&self, boost: &maho_types::boost::Boost) -> Result<(), StorageError> {
        let case_mode_str = match &boost.typography.case_mode {
            maho_types::boost::CaseMode::None => "none",
            maho_types::boost::CaseMode::Upper => "upper",
            maho_types::boost::CaseMode::Lower => "lower",
            maho_types::boost::CaseMode::Capitalize => "capitalize",
        };
        let zap_selectors_json =
            serde_json::to_string(&boost.zap_selectors).unwrap_or_else(|_| "[]".to_string());
        let font_family_val: Option<&str> = if boost.typography.font_family.is_empty() {
            None
        } else {
            Some(&boost.typography.font_family)
        };

        self.conn.execute(
            "INSERT OR REPLACE INTO boosts (
                id, domain, name, enable_color_boost, dot_angle_deg, secondary_dot_angle_deg_delta,
                brightness, saturation, contrast, auto_theme, smart_invert,
                font_family, text_case_override, zap_selectors, custom_css,
                created_at, updated_at,
                dot_pos_x, dot_pos_y, dot_distance, secondary_dot_pos_x, secondary_dot_pos_y,
                change_was_made, size_mode
            ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24)",
            rusqlite::params![
                boost.id.as_ref(),
                &boost.domain,
                &boost.name,
                boost.color.color_boost_enabled as i32,
                boost.color.dot_angle_deg,
                boost.color.secondary_dot_angle_deg_delta,
                boost.color.brightness,
                boost.color.saturation,
                boost.color.contrast,
                boost.color.magic_theme as i32,
                boost.color.smart_invert as i32,
                font_family_val,
                case_mode_str,
                zap_selectors_json,
                &boost.custom_css,
                boost.created_at.as_ref(),
                boost.updated_at.as_ref(),
                boost.color.dot_pos.x as f64,
                boost.color.dot_pos.y as f64,
                boost.color.dot_distance as f64,
                boost.color.secondary_dot_pos.x as f64,
                boost.color.secondary_dot_pos.y as f64,
                boost.change_was_made as i32,
                boost.typography.size_mode.as_db_string(),
            ],
        )?;
        Ok(())
    }

    pub fn load_boosts(&self) -> Result<Vec<maho_types::boost::Boost>, StorageError> {
        self.load_boosts_with_active_state()
            .map(|rows| rows.into_iter().map(|(boost, _)| boost).collect())
    }

    pub fn load_boosts_with_active_state(
        &self,
    ) -> Result<Vec<(maho_types::boost::Boost, Option<String>)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT b.id, b.domain, b.name, b.enable_color_boost, b.dot_angle_deg,
                    b.secondary_dot_angle_deg_delta, b.brightness, b.saturation, b.contrast,
                    b.auto_theme, b.smart_invert, b.font_family, b.text_case_override,
                    b.zap_selectors, b.custom_css, b.created_at, b.updated_at,
                    b.dot_pos_x, b.dot_pos_y, b.dot_distance, b.secondary_dot_pos_x,
                    b.secondary_dot_pos_y, b.change_was_made, b.size_mode,
                    s.active_boost_id
             FROM boosts b
             LEFT JOIN domain_boost_state s ON s.domain = b.domain",
        )?;
        let rows = stmt.query_map([], |row| {
            let id = row.get::<_, String>(0)?;
            let domain = row.get::<_, String>(1)?;
            let name = row.get::<_, String>(2)?;
            let color_boost_enabled = row.get::<_, i32>(3)? != 0;
            let dot_angle_deg = row.get::<_, f64>(4)?;
            let secondary_dot_angle_deg_delta = row.get::<_, f64>(5)?;
            let brightness = row.get::<_, f64>(6)?;
            let saturation = row.get::<_, f64>(7)?;
            let contrast = row.get::<_, f64>(8)?;
            let magic_theme = row.get::<_, i32>(9)? != 0;
            let smart_invert = row.get::<_, i32>(10)? != 0;
            let font_family_opt = row.get::<_, Option<String>>(11)?;
            let text_case_str = row.get::<_, String>(12)?;
            let zap_selectors_json = row.get::<_, String>(13)?;
            let custom_css = row.get::<_, String>(14)?;
            let created_at_str = row.get::<_, String>(15)?;
            let updated_at_str = row.get::<_, String>(16)?;
            let dot_pos_x = row.get::<_, f64>(17).unwrap_or(0.76) as f32;
            let dot_pos_y = row.get::<_, f64>(18).unwrap_or(0.66) as f32;
            let dot_distance = row.get::<_, f64>(19).unwrap_or(0.0) as f32;
            let secondary_dot_pos_x = row.get::<_, f64>(20).unwrap_or(0.5) as f32;
            let secondary_dot_pos_y = row.get::<_, f64>(21).unwrap_or(0.81) as f32;
            let change_was_made = row.get::<_, i32>(22).unwrap_or(0) != 0;
            let size_mode_str = row
                .get::<_, String>(23)
                .unwrap_or_else(|_| "k100".to_string());

            let case_mode = match text_case_str.as_str() {
                "upper" => maho_types::boost::CaseMode::Upper,
                "lower" => maho_types::boost::CaseMode::Lower,
                "capitalize" => maho_types::boost::CaseMode::Capitalize,
                _ => maho_types::boost::CaseMode::None,
            };

            let size_mode = maho_types::boost::SizeMode::from_db_string(&size_mode_str)
                .unwrap_or(maho_types::boost::SizeMode::K100);

            let zap_selectors: Vec<String> =
                serde_json::from_str(&zap_selectors_json).unwrap_or_default();

            let active_boost_id = row.get::<_, Option<String>>(24)?;

            Ok((
                maho_types::boost::Boost {
                    id: maho_types::identifiers::BoostId::new(id),
                    domain,
                    name,
                    color: maho_types::boost::ColorBoost {
                        dot_pos: maho_types::boost::Point {
                            x: dot_pos_x,
                            y: dot_pos_y,
                        },
                        dot_distance,
                        dot_angle_deg,
                        secondary_dot_pos: maho_types::boost::Point {
                            x: secondary_dot_pos_x,
                            y: secondary_dot_pos_y,
                        },
                        secondary_dot_angle_deg_delta,
                        magic_theme,
                        color_boost_enabled,
                        smart_invert,
                        contrast,
                        brightness,
                        saturation,
                    },
                    typography: maho_types::boost::TypographyBoost {
                        font_family: font_family_opt.unwrap_or_default(),
                        case_mode,
                        size_mode,
                    },
                    zap_selectors,
                    custom_css,
                    change_was_made,
                    created_at: maho_types::common::DateTime::from_iso(created_at_str),
                    updated_at: maho_types::common::DateTime::from_iso(updated_at_str),
                },
                active_boost_id,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_boost(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM boosts WHERE id = ?1", rusqlite::params![id])?;
        Ok(affected > 0)
    }

    pub fn set_active_boost(
        &self,
        domain: &str,
        active_boost_id: Option<&str>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO domain_boost_state (domain, active_boost_id) VALUES (?1, ?2)",
            rusqlite::params![domain, active_boost_id],
        )?;
        Ok(())
    }

    pub fn get_active_boost(&self, domain: &str) -> Result<Option<String>, StorageError> {
        let mut stmt = self
            .conn
            .prepare("SELECT active_boost_id FROM domain_boost_state WHERE domain = ?1")?;
        let mut rows = stmt.query(rusqlite::params![domain])?;
        if let Some(row) = rows.next()? {
            Ok(row.get::<_, Option<String>>(0)?)
        } else {
            Ok(None)
        }
    }

    // === CSS Mods ===

    pub fn save_css_mod(
        &self,
        mod_entry: &maho_types::css_mod::CssMod,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO css_mods (id, name, description, author, version, css, enabled, homepage, source_url, created_at, updated_at)\n         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)",
            rusqlite::params![
                &mod_entry.id,
                &mod_entry.name,
                mod_entry.description.as_deref(),
                mod_entry.author.as_deref(),
                mod_entry.version.as_deref(),
                &mod_entry.css,
                mod_entry.enabled as i32,
                mod_entry.homepage.as_deref(),
                mod_entry.source_url.as_deref(),
                &mod_entry.created_at,
                &mod_entry.updated_at,
            ],
        )?;
        Ok(())
    }

    pub fn load_css_mods(&self) -> Result<Vec<maho_types::css_mod::CssMod>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, description, author, version, css, enabled, homepage, source_url, created_at, updated_at FROM css_mods ORDER BY name"
        )?;
        let rows = stmt.query_map([], |row| {
            Ok(maho_types::css_mod::CssMod {
                id: row.get(0)?,
                name: row.get(1)?,
                description: row.get(2)?,
                author: row.get(3)?,
                version: row.get(4)?,
                css: row.get(5)?,
                enabled: row.get::<_, i32>(6)? != 0,
                homepage: row.get(7)?,
                source_url: row.get(8)?,
                created_at: row.get(9)?,
                updated_at: row.get(10)?,
            })
        })?;
        rows.collect::<Result<Vec<_>, _>>()
            .map_err(StorageError::from)
    }

    pub fn delete_css_mod(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM css_mods WHERE id = ?1", [id])?;
        Ok(affected > 0)
    }

    // === Permissions ===

    pub fn save_permission(
        &self,
        origin: &str,
        permission: &str,
        policy: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO permissions (origin, permission, policy) VALUES (?1, ?2, ?3)",
            rusqlite::params![origin, permission, policy],
        )?;
        Ok(())
    }

    pub fn load_permissions(&self) -> Result<Vec<(String, String, String)>, StorageError> {
        let mut stmt = self
            .conn
            .prepare("SELECT origin, permission, policy FROM permissions ORDER BY origin")?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_permissions_for_origin(&self, origin: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "DELETE FROM permissions WHERE origin = ?1",
            rusqlite::params![origin],
        )?;
        Ok(())
    }

    // === ATC Rules ===

    #[allow(clippy::too_many_arguments)]
    pub fn save_atc_rule(
        &self,
        id: &str,
        name: &str,
        space_id: Option<&str>,
        url_pattern: Option<&str>,
        max_age_hours: Option<u32>,
        max_tabs: Option<usize>,
        enabled: bool,
        match_type: Option<&str>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO atc_rules (id, name, space_id, url_pattern, max_age_hours, max_tabs, enabled, match_type) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)",
            rusqlite::params![id, name, space_id, url_pattern, max_age_hours, max_tabs, enabled, match_type],
        )?;
        Ok(())
    }

    #[allow(clippy::type_complexity)]
    pub fn load_atc_rules(
        &self,
    ) -> Result<
        Vec<(
            String,
            String,
            Option<String>,
            Option<String>,
            Option<u32>,
            Option<usize>,
            bool,
            Option<String>,
        )>,
        StorageError,
    > {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, space_id, url_pattern, max_age_hours, max_tabs, enabled, match_type FROM atc_rules",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, Option<String>>(2)?,
                row.get::<_, Option<String>>(3)?,
                row.get::<_, Option<u32>>(4)?,
                row.get::<_, Option<usize>>(5)?,
                row.get::<_, bool>(6)?,
                row.get::<_, Option<String>>(7)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_atc_rule(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM atc_rules WHERE id = ?1", rusqlite::params![id])?;
        Ok(affected > 0)
    }

    // === Search History ===

    pub fn save_search(&self, query: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO search_history (query, searched_at) VALUES (?1, datetime('now'))",
            rusqlite::params![query],
        )?;
        Ok(())
    }

    pub fn load_recent_searches(
        &self,
        limit: usize,
    ) -> Result<Vec<(String, String)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT query, searched_at FROM search_history ORDER BY searched_at DESC LIMIT ?1",
        )?;
        let rows = stmt.query_map(rusqlite::params![limit], |row| {
            Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn clear_search_history(&self) -> Result<(), StorageError> {
        self.conn.execute("DELETE FROM search_history", [])?;
        Ok(())
    }

    // === Per-site Zoom ===

    pub fn save_zoom(&self, origin: &str, zoom_level: f64) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO per_site_zoom (origin, zoom_level) VALUES (?1, ?2)",
            rusqlite::params![origin, zoom_level],
        )?;
        Ok(())
    }

    pub fn get_zoom_for_site(&self, origin: &str) -> Result<Option<f64>, StorageError> {
        let mut stmt = self
            .conn
            .prepare("SELECT zoom_level FROM per_site_zoom WHERE origin = ?1")?;
        let mut rows = stmt.query(rusqlite::params![origin])?;
        match rows.next()? {
            Some(row) => Ok(Some(row.get(0)?)),
            None => Ok(None),
        }
    }

    pub fn delete_zoom(&self, origin: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM per_site_zoom WHERE origin = ?1",
            rusqlite::params![origin],
        )?;
        Ok(affected > 0)
    }

    // === Downloads ===

    #[allow(clippy::too_many_arguments)]
    pub fn save_download(
        &self,
        id: &str,
        filename: &str,
        url: &str,
        total_bytes: u64,
        received_bytes: u64,
        state: &str,
        file_path: Option<&str>,
        started_at: &str,
        completed_at: Option<&str>,
        mime_type: Option<&str>,
        chromium_guid: Option<&str>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO downloads (id, filename, url, total_bytes, received_bytes, state, file_path, started_at, completed_at, mime_type, chromium_guid) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)",
            rusqlite::params![id, filename, url, total_bytes, received_bytes, state, file_path, started_at, completed_at, mime_type, chromium_guid],
        )?;
        Ok(())
    }

    #[allow(clippy::type_complexity)]
    pub fn load_downloads(
        &self,
    ) -> Result<
        Vec<(
            String,
            String,
            String,
            u64,
            u64,
            String,
            Option<String>,
            String,
            Option<String>,
            Option<String>,
            Option<String>,
        )>,
        StorageError,
    > {
        let mut stmt = self.conn.prepare(
            "SELECT id, filename, url, total_bytes, received_bytes, state, file_path, started_at, completed_at, mime_type, chromium_guid FROM downloads ORDER BY started_at DESC",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, u64>(3)?,
                row.get::<_, u64>(4)?,
                row.get::<_, String>(5)?,
                row.get::<_, Option<String>>(6)?,
                row.get::<_, String>(7)?,
                row.get::<_, Option<String>>(8)?,
                row.get::<_, Option<String>>(9)?,
                row.get::<_, Option<String>>(10)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_download(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM downloads WHERE id = ?1", rusqlite::params![id])?;
        Ok(affected > 0)
    }

    pub fn update_download_progress(
        &self,
        id: &str,
        received_bytes: u64,
        state: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "UPDATE downloads SET received_bytes = ?1, state = ?2 WHERE id = ?3",
            rusqlite::params![received_bytes, state, id],
        )?;
        Ok(())
    }

    // === Usage Stats ===

    pub fn increment_usage(&self, item_key: &str, item_type: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO usage_stats (item_key, item_type, usage_count, last_used_at) 
             VALUES (?1, ?2, 1, datetime('now'))
             ON CONFLICT(item_key) DO UPDATE SET 
             usage_count = usage_count + 1, 
             last_used_at = datetime('now')",
            rusqlite::params![item_key, item_type],
        )?;
        Ok(())
    }

    pub fn get_usage_count(&self, item_key: &str) -> Result<u32, StorageError> {
        let count: u32 = self
            .conn
            .query_row(
                "SELECT usage_count FROM usage_stats WHERE item_key = ?1",
                rusqlite::params![item_key],
                |row| row.get(0),
            )
            .unwrap_or(0);
        Ok(count)
    }

    pub fn get_top_used(
        &self,
        item_type: &str,
        limit: usize,
    ) -> Result<Vec<(String, u32)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT item_key, usage_count FROM usage_stats WHERE item_type = ?1 ORDER BY usage_count DESC LIMIT ?2",
        )?;
        let rows = stmt.query_map(rusqlite::params![item_type, limit], |row| {
            Ok((row.get::<_, String>(0)?, row.get::<_, u32>(1)?))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn clear_usage_stats(&self) -> Result<(), StorageError> {
        self.conn.execute("DELETE FROM usage_stats", [])?;
        Ok(())
    }

    // === Passwords (legacy; removed by Vault cutover) ===
    //
    // The plaintext `passwords` table no longer exists. These signatures remain
    // only so downstream `maho-core` callers keep compiling until Todos 10-12
    // migrate them to the Vault APIs. They fail clearly and never recreate or
    // write a plaintext password table.

    pub fn save_password(
        &self,
        _id: &str,
        _domain: &str,
        _username: &str,
        _created_at: &str,
        _password: Option<&str>,
    ) -> Result<(), StorageError> {
        Err(StorageError::Other(
            "legacy plaintext password storage removed; use the Vault APIs".to_string(),
        ))
    }

    pub fn get_all_passwords(&self) -> Result<Vec<PasswordRecord>, StorageError> {
        Err(StorageError::Other(
            "legacy plaintext password storage removed; use the Vault APIs".to_string(),
        ))
    }

    pub fn delete_password(&self, _id: &str) -> Result<bool, StorageError> {
        Err(StorageError::Other(
            "legacy plaintext password storage removed; use the Vault APIs".to_string(),
        ))
    }

    // === Autofill Addresses ===

    pub fn save_autofill_address(
        &self,
        address: AutofillAddressParams<'_>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO autofill_addresses (id, name, street, city, state, zip, country, phone, email, address_line2) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)",
            rusqlite::params![
                address.id,
                address.name,
                address.street,
                address.city,
                address.state,
                address.zip,
                address.country,
                address.phone,
                address.email,
                address.address_line2,
            ],
        )?;
        Ok(())
    }

    pub fn load_autofill_addresses(&self) -> Result<Vec<AutofillAddressRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, street, city, state, zip, country, phone, email, address_line2 FROM autofill_addresses",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, String>(3)?,
                row.get::<_, String>(4)?,
                row.get::<_, String>(5)?,
                row.get::<_, String>(6)?,
                row.get::<_, Option<String>>(7)?,
                row.get::<_, Option<String>>(8)?,
                row.get::<_, Option<String>>(9)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_autofill_address(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM autofill_addresses WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(affected > 0)
    }

    // === Autofill Payments ===

    pub fn save_autofill_payment(
        &self,
        id: &str,
        card_name: &str,
        last_four: &str,
        expiry: &str,
        card_network: Option<&str>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO autofill_payments (id, card_name, last_four, expiry, card_network) VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params![id, card_name, last_four, expiry, card_network],
        )?;
        Ok(())
    }

    pub fn load_autofill_payments(&self) -> Result<Vec<AutofillPaymentRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, card_name, last_four, expiry, card_network FROM autofill_payments",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, String>(3)?,
                row.get::<_, Option<String>>(4)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_autofill_payment(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM autofill_payments WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(affected > 0)
    }

    // === Search Engines ===

    pub fn save_search_engine(
        &self,
        id: &str,
        name: &str,
        url_template: &str,
        shortcut: Option<&str>,
        icon_url: Option<&str>,
        is_default: bool,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO search_engines (id, name, url_template, shortcut, icon_url, is_default) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
            rusqlite::params![id, name, url_template, shortcut, icon_url, is_default],
        )?;
        Ok(())
    }

    #[allow(clippy::type_complexity)]
    pub fn load_search_engines(
        &self,
    ) -> Result<Vec<(String, String, String, Option<String>, Option<String>, bool)>, StorageError>
    {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, url_template, shortcut, icon_url, is_default FROM search_engines",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, Option<String>>(3)?,
                row.get::<_, Option<String>>(4)?,
                row.get::<_, bool>(5)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_search_engine(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM search_engines WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(affected > 0)
    }

    pub fn update_default_search_engine(&self, id: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "UPDATE search_engines SET is_default = 0 WHERE is_default = 1",
            [],
        )?;
        self.conn.execute(
            "UPDATE search_engines SET is_default = 1 WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(())
    }

    // === Shortcuts ===

    pub fn save_shortcut(
        &self,
        action: &str,
        key_combo_json: &str,
        updated_at: u64,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO shortcuts (action, key_combo_json, is_custom, enabled, updated_at) VALUES (?1, ?2, 1, 1, ?3) ON CONFLICT(action) DO UPDATE SET key_combo_json = excluded.key_combo_json, is_custom = 1, enabled = COALESCE(shortcuts.enabled, 1), updated_at = excluded.updated_at",
            rusqlite::params![action, key_combo_json, updated_at as i64],
        )?;
        Ok(())
    }

    pub fn save_shortcut_enabled(
        &self,
        action: &str,
        enabled: bool,
        updated_at: u64,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO shortcuts (action, key_combo_json, is_custom, enabled, updated_at) VALUES (?1, '', 0, ?2, ?3) ON CONFLICT(action) DO UPDATE SET enabled = excluded.enabled, updated_at = excluded.updated_at",
            rusqlite::params![action, enabled as i32, updated_at as i64],
        )?;
        Ok(())
    }

    pub fn delete_shortcut(&self, action: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM shortcuts WHERE action = ?1",
            rusqlite::params![action],
        )?;
        Ok(affected > 0)
    }

    pub fn delete_all_shortcuts(&self) -> Result<(), StorageError> {
        self.conn.execute("DELETE FROM shortcuts", [])?;
        Ok(())
    }

    pub fn load_shortcuts(&self) -> Result<Vec<(String, String, bool, u64)>, StorageError> {
        let mut stmt = self
            .conn
            .prepare("SELECT action, key_combo_json, enabled, updated_at FROM shortcuts")?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, i32>(2)? != 0,
                row.get::<_, i64>(3)? as u64,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    // === Bookmark Folders ===

    pub fn delete_bookmark_folder(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM bookmark_folders WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(affected > 0)
    }

    pub fn load_bookmark_folders(&self) -> Result<Vec<BookmarkFolderRecord>, StorageError> {
        let mut stmt = self
            .conn
            .prepare("SELECT id, name, parent_id, created_at FROM bookmark_folders")?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, Option<String>>(2)?,
                row.get::<_, String>(3)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }
}

impl SqliteStorage {
    // Reading List storage methods for SqliteStorage

    // === Reading List ===

    #[allow(clippy::too_many_arguments)]
    pub fn save_reading_list_item(
        &self,
        id: &str,
        url: &str,
        title: &str,
        excerpt: Option<&str>,
        site_name: Option<&str>,
        favicon_url: Option<&str>,
        preview_image_url: Option<&str>,
        added_at: &str,
        read_at: Option<&str>,
        is_read: bool,
        estimated_read_minutes: Option<u32>,
        tags_json: Option<&str>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
        "INSERT OR REPLACE INTO reading_list (id, url, title, excerpt, site_name, favicon_url, preview_image_url, added_at, read_at, is_read, estimated_read_minutes, tags) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)",
        rusqlite::params![
            id,
            url,
            title,
            excerpt,
            site_name,
            favicon_url,
            preview_image_url,
            added_at,
            read_at,
            is_read,
            estimated_read_minutes,
            tags_json
        ],
    )?;
        Ok(())
    }

    #[allow(clippy::type_complexity)]
    pub fn load_reading_list(
        &self,
    ) -> Result<
        Vec<(
            String,
            String,
            String,
            Option<String>,
            Option<String>,
            Option<String>,
            Option<String>,
            String,
            Option<String>,
            bool,
            Option<u32>,
            Option<String>,
        )>,
        StorageError,
    > {
        let mut stmt = self.conn.prepare(
        "SELECT id, url, title, excerpt, site_name, favicon_url, preview_image_url, added_at, read_at, is_read, estimated_read_minutes, tags FROM reading_list ORDER BY added_at DESC",
    )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, Option<String>>(3)?,
                row.get::<_, Option<String>>(4)?,
                row.get::<_, Option<String>>(5)?,
                row.get::<_, Option<String>>(6)?,
                row.get::<_, String>(7)?,
                row.get::<_, Option<String>>(8)?,
                row.get::<_, bool>(9)?,
                row.get::<_, Option<u32>>(10)?,
                row.get::<_, Option<String>>(11)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_reading_list_item(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM reading_list WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(affected > 0)
    }

    pub fn update_reading_list_read_status(
        &self,
        id: &str,
        is_read: bool,
        read_at: Option<&str>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "UPDATE reading_list SET is_read = ?1, read_at = ?2 WHERE id = ?3",
            rusqlite::params![is_read, read_at, id],
        )?;
        Ok(())
    }

    // === Sync Queue ===

    /// Enqueue an entity push to the persistent sync queue
    pub fn enqueue_sync_entity(
        &self,
        entity_type: &str,
        entity_id: &str,
        version: u64,
        modified_at: i64,
        payload_json: &str,
        deleted: bool,
    ) -> Result<i64, StorageError> {
        let now = chrono::Utc::now().timestamp();
        self.conn.execute(
            "INSERT INTO sync_queue (
                entity_type, entity_id, version, modified_at, payload_json,
                deleted, created_at, delivery_id, state
             ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, 'pending')",
            rusqlite::params![
                entity_type,
                entity_id,
                version,
                modified_at,
                payload_json,
                deleted as i32,
                now,
                uuid::Uuid::new_v4().to_string(),
            ],
        )?;
        Ok(self.conn.last_insert_rowid())
    }

    /// Load all unsent sync queue entries (sent_at IS NULL)
    #[allow(clippy::type_complexity)]
    pub fn load_pending_sync_entities(
        &self,
    ) -> Result<Vec<(i64, String, String, u64, i64, String, bool)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, entity_type, entity_id, version, modified_at, payload_json, deleted
             FROM sync_queue
             WHERE state != 'acknowledged'
             ORDER BY id ASC",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, i64>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, u64>(3)?,
                row.get::<_, i64>(4)?,
                row.get::<_, String>(5)?,
                row.get::<_, bool>(6)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    /// Mark sync queue entries as sent
    pub fn mark_sync_entities_sent(&self, ids: &[i64]) -> Result<(), StorageError> {
        let now = chrono::Utc::now().timestamp();
        for id in ids {
            self.conn.execute(
                "UPDATE sync_queue
                 SET state = 'inflight',
                     attempt_count = attempt_count + 1,
                     last_attempt_at = ?1
                 WHERE id = ?2 AND state != 'acknowledged'",
                rusqlite::params![now, id],
            )?;
        }
        Ok(())
    }

    /// Acknowledge sync queue entries (set acked_at)
    pub fn ack_sync_entities(&self, ids: &[i64]) -> Result<(), StorageError> {
        let now = chrono::Utc::now().timestamp();
        for id in ids {
            self.conn.execute(
                "UPDATE sync_queue
                 SET acked_at = ?1, state = 'acknowledged'
                 WHERE id = ?2",
                rusqlite::params![now, id],
            )?;
        }
        Ok(())
    }

    /// Acknowledge a sync queue entry by entity type, id and version
    pub fn ack_sync_entity_by_key(
        &self,
        entity_type: &str,
        entity_id: &str,
        version: u64,
    ) -> Result<(), StorageError> {
        let now = chrono::Utc::now().timestamp();
        self.conn.execute(
            "UPDATE sync_queue
             SET acked_at = ?1, state = 'acknowledged'
             WHERE entity_type = ?2 AND entity_id = ?3 AND version = ?4",
            rusqlite::params![now, entity_type, entity_id, version],
        )?;
        Ok(())
    }

    pub fn lease_sync_entities(&self, ids: &[i64]) -> Result<Vec<SyncQueueLease>, StorageError> {
        self.mark_sync_entities_sent(ids)?;
        let mut statement = self.conn.prepare(
            "SELECT id, delivery_id
             FROM sync_queue
             WHERE id = ?1 AND state = 'inflight'
             ORDER BY id ASC",
        )?;
        let mut leases = Vec::with_capacity(ids.len());
        for id in ids {
            if let Some(lease) = statement
                .query_row([id], |row| {
                    Ok(SyncQueueLease {
                        id: row.get(0)?,
                        delivery_id: row.get(1)?,
                    })
                })
                .optional()?
            {
                leases.push(lease);
            }
        }
        Ok(leases)
    }

    pub fn ack_sync_delivery(
        &self,
        delivery_id: &str,
        relay_seq: u64,
    ) -> Result<bool, StorageError> {
        let now = chrono::Utc::now().timestamp();
        let changed = self.conn.execute(
            "UPDATE sync_queue
             SET acked_at = ?1, state = 'acknowledged', relay_seq = ?2
             WHERE delivery_id = ?3 AND state != 'acknowledged'",
            rusqlite::params![now, relay_seq, delivery_id],
        )?;
        Ok(changed == 1)
    }

    pub fn pending_sync_outbox_count(&self) -> Result<usize, StorageError> {
        self.conn
            .query_row(
                "SELECT COUNT(*) FROM sync_queue WHERE state != 'acknowledged'",
                [],
                |row| row.get(0),
            )
            .map_err(StorageError::from)
    }

    pub fn get_sync_receive_cursor(&self, room_id: &str) -> Result<u64, StorageError> {
        self.conn
            .query_row(
                "SELECT last_relay_seq FROM sync_receive_cursors WHERE room_id = ?1",
                [room_id],
                |row| row.get(0),
            )
            .optional()
            .map(|cursor| cursor.unwrap_or(0))
            .map_err(StorageError::from)
    }

    pub fn set_sync_receive_cursor(
        &self,
        room_id: &str,
        relay_seq: u64,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO sync_receive_cursors (room_id, last_relay_seq)
             VALUES (?1, ?2)
             ON CONFLICT(room_id) DO UPDATE SET
                 last_relay_seq = MAX(last_relay_seq, excluded.last_relay_seq)",
            rusqlite::params![room_id, relay_seq],
        )?;
        Ok(())
    }

    pub fn get_sync_tombstone(
        &self,
        entity_type: &str,
        entity_id: &str,
    ) -> Result<Option<(u64, u32)>, StorageError> {
        self.conn
            .query_row(
                "SELECT hlc_ts, device_id
                 FROM sync_tombstones
                 WHERE entity_type = ?1 AND entity_id = ?2",
                rusqlite::params![entity_type, entity_id],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )
            .optional()
            .map_err(StorageError::from)
    }

    pub fn save_sync_tombstone(
        &self,
        entity_type: &str,
        entity_id: &str,
        hlc_ts: u64,
        device_id: u32,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO sync_tombstones (
                entity_type, entity_id, hlc_ts, device_id, deleted_at
             ) VALUES (?1, ?2, ?3, ?4, ?5)
             ON CONFLICT(entity_type, entity_id) DO UPDATE SET
                hlc_ts = excluded.hlc_ts,
                device_id = excluded.device_id,
                deleted_at = excluded.deleted_at
             WHERE (excluded.hlc_ts, excluded.device_id) >
                   (sync_tombstones.hlc_ts, sync_tombstones.device_id)",
            rusqlite::params![
                entity_type,
                entity_id,
                hlc_ts,
                device_id,
                chrono::Utc::now().timestamp(),
            ],
        )?;
        Ok(())
    }

    pub fn remove_sync_tombstone(
        &self,
        entity_type: &str,
        entity_id: &str,
        hlc_ts: u64,
        device_id: u32,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "DELETE FROM sync_tombstones
             WHERE entity_type = ?1 AND entity_id = ?2
               AND (hlc_ts, device_id) < (?3, ?4)",
            rusqlite::params![entity_type, entity_id, hlc_ts, device_id],
        )?;
        Ok(())
    }

    /// Prune acknowledged sync queue entries older than 24h
    pub fn prune_acked_sync_entities(&self) -> Result<(), StorageError> {
        let cutoff = chrono::Utc::now().timestamp() - 86400; // 24 hours ago
        self.conn.execute(
            "DELETE FROM sync_queue WHERE acked_at IS NOT NULL AND acked_at < ?1",
            rusqlite::params![cutoff],
        )?;
        Ok(())
    }

    /// Get last pulled version for an entity type
    pub fn get_sync_last_pulled_version(&self, entity_type: &str) -> Result<u64, StorageError> {
        let version: u64 = self
            .conn
            .query_row(
                "SELECT last_pulled_version FROM sync_state WHERE entity_type = ?1",
                rusqlite::params![entity_type],
                |row| row.get(0),
            )
            .unwrap_or(0);
        Ok(version)
    }

    /// Set last pulled version for an entity type
    pub fn set_sync_last_pulled_version(
        &self,
        entity_type: &str,
        version: u64,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO sync_state (entity_type, last_pulled_version) VALUES (?1, ?2)",
            rusqlite::params![entity_type, version],
        )?;
        Ok(())
    }

    pub fn get_last_hlc_ts(&self) -> Result<u64, StorageError> {
        let ts: u64 = self
            .conn
            .query_row(
                "SELECT last_hlc_ts FROM hlc_state WHERE id = 0",
                [],
                |row| row.get(0),
            )
            .unwrap_or(0);
        Ok(ts)
    }

    pub fn set_last_hlc_ts(&self, ts: u64) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO hlc_state (id, last_hlc_ts) VALUES (0, ?1)",
            rusqlite::params![ts],
        )?;
        Ok(())
    }

    pub fn get_entity_version(
        &self,
        entity_type: &str,
        entity_id: &str,
    ) -> Result<Option<EntityVersionRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT hlc_ts, device_id, fields_hlc_json, payload_json FROM entity_versions WHERE entity_type = ?1 AND entity_id = ?2",
        )?;
        let mut rows = stmt.query(rusqlite::params![entity_type, entity_id])?;
        if let Some(row) = rows.next()? {
            Ok(Some((
                row.get::<_, u64>(0)?,
                row.get::<_, u32>(1)?,
                row.get::<_, Option<String>>(2)?,
                row.get::<_, Option<String>>(3)?,
            )))
        } else {
            Ok(None)
        }
    }

    pub fn save_entity_version(
        &self,
        entity_type: &str,
        entity_id: &str,
        hlc_ts: u64,
        device_id: u32,
        fields_hlc_json: Option<&str>,
        payload_json: Option<&str>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO entity_versions (entity_type, entity_id, hlc_ts, device_id, fields_hlc_json, payload_json) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
            rusqlite::params![entity_type, entity_id, hlc_ts, device_id, fields_hlc_json, payload_json],
        )?;
        Ok(())
    }

    pub fn delete_entity_version(
        &self,
        entity_type: &str,
        entity_id: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "DELETE FROM entity_versions WHERE entity_type=?1 AND entity_id=?2",
            rusqlite::params![entity_type, entity_id],
        )?;
        Ok(())
    }

    pub fn save_shared_collection(
        &self,
        collection: &maho_types::sharing::SharedCollection,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO shared_collections (id, space_id, name, permission, member_count, share_link, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            rusqlite::params![collection.id, collection.space_id, collection.name, collection.permission, collection.member_count, collection.share_link, collection.created_at],
        )?;
        Ok(())
    }

    pub fn get_all_shared_collections(
        &self,
    ) -> Result<Vec<maho_types::sharing::SharedCollection>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, space_id, name, permission, member_count, share_link, created_at FROM shared_collections",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok(maho_types::sharing::SharedCollection {
                id: row.get::<_, String>(0)?,
                space_id: row.get::<_, String>(1)?,
                name: row.get::<_, String>(2)?,
                permission: row.get::<_, String>(3)?,
                member_count: row.get::<_, u32>(4)?,
                share_link: row.get::<_, Option<String>>(5)?,
                created_at: row.get::<_, String>(6)?,
            })
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_shared_collection(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM shared_collections WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(affected > 0)
    }

    pub fn update_shared_collection_permission(
        &self,
        id: &str,
        permission: &str,
    ) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "UPDATE shared_collections SET permission = ?1 WHERE id = ?2",
            rusqlite::params![permission, id],
        )?;
        Ok(affected > 0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_key() -> String {
        uuid::Uuid::new_v4().to_string()
    }

    fn test_store() -> SqliteStorage {
        SqliteStorage::open_in_memory_with_key(&test_key()).unwrap()
    }

    #[test]
    fn load_recent_history_groups_by_url_newest_first() {
        let db = test_store();
        for (url, title, at) in [
            ("https://a.test/", "A old", "2026-01-01 00:00:00"),
            ("https://b.test/", "B", "2026-01-02 00:00:00"),
            ("https://a.test/", "A new", "2026-01-03 00:00:00"),
        ] {
            db.conn
                .execute(
                    "INSERT INTO history (url, title, visited_at) VALUES (?1, ?2, ?3)",
                    rusqlite::params![url, title, at],
                )
                .unwrap();
        }
        let rows = db.load_recent_history(10).unwrap();
        assert_eq!(rows.len(), 2);
        assert_eq!(rows[0].0, "https://a.test/");
        assert_eq!(rows[0].1, "A new");
        assert_eq!(rows[1].0, "https://b.test/");
        assert!(rows[0].2 > rows[1].2);
        assert_eq!(db.load_recent_history(1).unwrap().len(), 1);
    }

    const RECENT_CREATED_AT: &str = "2999-01-01 00:00:00";
    const OLD_CREATED_AT: &str = "2020-01-01 00:00:00";

    struct MemoryFactFixture<'a> {
        id: &'a str,
        fact: &'a str,
        importance: f64,
        created_at: &'a str,
    }

    fn insert_memory_fact(db: &SqliteStorage, fact: &MemoryFactFixture<'_>) {
        db.conn
            .execute(
                "INSERT INTO memory_facts \
                 (id, fact, source, session_id, categories, importance, metadata, created_at) \
                 VALUES (?1, ?2, 'test', NULL, '[]', ?3, NULL, ?4)",
                rusqlite::params![fact.id, fact.fact, fact.importance, fact.created_at],
            )
            .unwrap();
        db.conn
            .execute(
                "INSERT INTO memory_facts_fts (id, fact) VALUES (?1, ?2)",
                rusqlite::params![fact.id, fact.fact],
            )
            .unwrap();
    }

    fn string_column(db: &SqliteStorage, sql: &str) -> Vec<String> {
        let mut stmt = db.conn.prepare(sql).unwrap();
        stmt.query_map([], |row| row.get::<_, String>(0))
            .unwrap()
            .collect::<rusqlite::Result<Vec<String>>>()
            .unwrap()
    }

    fn memory_search_ids(results: &[(String, String, f64)]) -> Vec<&str> {
        results.iter().map(|(id, _, _)| id.as_str()).collect()
    }

    fn note_search_ids(results: &[(String, f64)]) -> Vec<&str> {
        results.iter().map(|(id, _)| id.as_str()).collect()
    }

    #[test]
    fn review_interrupted_encryption_swap_preserves_profile() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("profile.db");
        let key = test_key();
        {
            let conn = Connection::open(&path).unwrap();
            conn.execute_batch("CREATE TABLE marker(value TEXT); INSERT INTO marker VALUES ('original profile');").unwrap();
        }
        export_encrypted(&path, &migration_temp_path(&path), &key).unwrap();
        std::fs::rename(&path, migration_backup_path(&path)).unwrap();
        for _ in 0..2 {
            let db = SqliteStorage::open_with_key(path.to_str().unwrap(), &key).unwrap();
            let value: String = db.conn.query_row("SELECT value FROM marker", [], |r| r.get(0)).unwrap();
            assert_eq!(value, "original profile");
        }
    }

    #[test]
    fn review_mcp_restore_failure_preserves_original_rows() {
        let db = test_store();
        db.conn.execute_batch("DROP TABLE ai_mcp_servers;
            CREATE TABLE ai_mcp_servers (
                id TEXT, workspace_id TEXT, name TEXT, transport TEXT, command TEXT, url TEXT,
                auth_keychain_id TEXT, trusted INTEGER, trusted_tools_json TEXT,
                timeout_ms INTEGER, output_cap_bytes INTEGER, created_at TEXT, updated_at TEXT);
            INSERT INTO ai_mcp_servers VALUES ('server', 'workspace', 'name', 'invalid', 'cmd', NULL,
                'credential-ref', 1, '[\"tool\"]', 60000, 10240, 'created', 'updated');").unwrap();
        let result = db.run_ai_extensibility_migrations();
        let rows: i64 = db.conn.query_row("SELECT count(*) FROM ai_mcp_servers WHERE id='server' AND auth_keychain_id='credential-ref' AND trusted=1", [], |r| r.get(0)).unwrap();
        assert_eq!(rows, 1, "failed rebuild must preserve server and trust metadata");
        assert!(result.is_err());
        assert!(db.conn.query_row("PRAGMA foreign_keys", [], |r| r.get::<_, bool>(0)).unwrap());
        db.conn.execute("UPDATE ai_mcp_servers SET transport='stdio'", []).unwrap();
        db.run_ai_extensibility_migrations().unwrap();
        let restored: (String, bool, Option<String>) = db.conn.query_row(
            "SELECT auth_keychain_id, trusted, socket_path FROM ai_mcp_servers WHERE id='server'", [],
            |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?))).unwrap();
        assert_eq!(restored, ("credential-ref".into(), true, None));
    }

    #[test]
    fn plaintext_migration_failure_keeps_original_database() {
        let temp_dir = tempfile::tempdir().unwrap();
        let db_path = temp_dir.path().join("migration.db");
        let path_str = db_path.to_str().unwrap();
        {
            let conn = rusqlite::Connection::open(path_str).unwrap();
            conn.execute_batch(
                "CREATE TABLE marker (id INTEGER PRIMARY KEY, value TEXT NOT NULL);
                 INSERT INTO marker (value) VALUES ('still here');",
            )
            .unwrap();
        }

        let key = test_key();
        let err = migrate_plaintext_to_encrypted_inner(path_str, &key, |_, _| false).unwrap_err();

        assert!(matches!(err, StorageError::Migration(_)));
        assert!(is_plaintext_database(&db_path));
        assert!(!migration_temp_path(&db_path).exists());
        let conn = rusqlite::Connection::open(path_str).unwrap();
        let value: String = conn
            .query_row("SELECT value FROM marker WHERE id = 1", [], |row| {
                row.get(0)
            })
            .unwrap();
        assert_eq!(value, "still here");
    }

    #[test]
    fn plaintext_migration_success_deletes_plaintext_backup() {
        let temp_dir = tempfile::tempdir().unwrap();
        let db_path = temp_dir.path().join("migration.db");
        let backup_path = migration_backup_path(&db_path);
        let path_str = db_path.to_str().unwrap();
        {
            let conn = rusqlite::Connection::open(path_str).unwrap();
            conn.execute_batch(
                "CREATE TABLE marker (id INTEGER PRIMARY KEY, value TEXT NOT NULL);
                 INSERT INTO marker (value) VALUES ('backed up');",
            )
            .unwrap();
        }

        let key = test_key();
        migrate_plaintext_to_encrypted_inner(path_str, &key, verify_encrypted_opens).unwrap();

        assert!(!backup_path.exists());
        assert!(!is_plaintext_database(&db_path));
        assert!(verify_encrypted_opens(&db_path, &key));
    }

    #[test]
    fn plaintext_migration_preserves_unverified_backup() {
        let temp_dir = tempfile::tempdir().unwrap();
        let db_path = temp_dir.path().join("migration.db");
        let backup_path = migration_backup_path(&db_path);
        let path_str = db_path.to_str().unwrap();
        let key = test_key();

        // 1. Create a valid encrypted DB
        {
            let conn = rusqlite::Connection::open(path_str).unwrap();
            apply_key(&conn, &key).unwrap();
            conn.execute_batch(
                "CREATE TABLE marker (id INTEGER PRIMARY KEY, value TEXT NOT NULL);
                 INSERT INTO marker (value) VALUES ('secure data');",
            )
            .unwrap();
        }

        // 2. Create a stale backup file
        std::fs::write(&backup_path, b"stale plaintext backup content").unwrap();
        assert!(backup_path.exists());

        // An opening canonical database does not prove this backup was migrated.
        migrate_plaintext_to_encrypted_inner(path_str, &key, verify_encrypted_opens).unwrap();

        // Preserve recovery evidence rather than deleting an unrelated backup.
        assert!(backup_path.exists());
        assert!(db_path.exists());
        assert!(verify_encrypted_opens(&db_path, &key));
    }

    #[test]
    fn test_conversation_migration() {
        let temp_dir = tempfile::tempdir().unwrap();
        let db_path = temp_dir.path().join("test.db");
        let path_str = db_path.to_str().unwrap();
        let key = test_key();

        {
            let seed_conn = rusqlite::Connection::open(path_str).unwrap();
            seed_conn.pragma_update(None, "key", &key).unwrap();
            seed_conn.execute_batch("
                CREATE TABLE chat_sessions (id TEXT PRIMARY KEY, title TEXT, created_at TEXT);
                CREATE TABLE chat_messages (id TEXT PRIMARY KEY, session_id TEXT, role TEXT, content TEXT, created_at TEXT);
                CREATE TABLE ai_sessions (id TEXT PRIMARY KEY, title TEXT, created_at TEXT);
                CREATE TABLE ai_messages (id TEXT PRIMARY KEY, session_id TEXT, role TEXT, content TEXT, created_at TEXT);
                CREATE TABLE session_event_logs (session_id TEXT, role TEXT, content TEXT, created_at TEXT);
            ").unwrap();
            seed_conn.execute("INSERT INTO chat_sessions VALUES ('v1-sess', 'V1 Title', '2026-06-01 10:00:00')", []).unwrap();
            seed_conn.execute("INSERT INTO chat_messages VALUES ('v1-msg-1', 'v1-sess', 'user', 'Hello V1', '2026-06-01 10:00:01')", []).unwrap();
            seed_conn
                .execute(
                    "INSERT INTO ai_sessions VALUES ('v2-sess', 'V2 Title', '2026-06-02 10:00:00')",
                    [],
                )
                .unwrap();
            seed_conn.execute("INSERT INTO ai_messages VALUES ('v2-msg-1', 'v2-sess', 'assistant', 'Hello V2', '2026-06-02 10:00:01')", []).unwrap();
            seed_conn.execute("INSERT INTO session_event_logs VALUES ('v3-sess', 'user', 'Hello V3', '2026-06-03 10:00:01')", []).unwrap();
        }

        // Open via SqliteStorage which will automatically run migrations!
        let storage = SqliteStorage::open_with_key(path_str, &key).unwrap();

        // Verify the sessions are in the `conversations` table
        let mut stmt = storage
            .conn
            .prepare("SELECT id, title FROM conversations ORDER BY id")
            .unwrap();
        let sessions: Vec<(String, Option<String>)> = stmt
            .query_map([], |row| Ok((row.get(0)?, row.get(1)?)))
            .unwrap()
            .collect::<Result<_, _>>()
            .unwrap();

        assert_eq!(sessions.len(), 3);
        assert_eq!(sessions[0].0, "v1-sess");
        assert_eq!(sessions[0].1.as_deref(), Some("V1 Title"));
        assert_eq!(sessions[1].0, "v2-sess");
        assert_eq!(sessions[1].1.as_deref(), Some("V2 Title"));
        assert_eq!(sessions[2].0, "v3-sess");

        // Verify conversation turns
        let mut stmt = storage
            .conn
            .prepare("SELECT session_id, role, content FROM conversation_turns ORDER BY session_id")
            .unwrap();
        let turns: Vec<(String, String, String)> = stmt
            .query_map([], |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)))
            .unwrap()
            .collect::<Result<_, _>>()
            .unwrap();

        assert_eq!(turns.len(), 3);
        assert_eq!(turns[0].0, "v1-sess");
        assert_eq!(turns[0].1, "user");
        assert_eq!(turns[0].2, "Hello V1");

        assert_eq!(turns[1].0, "v2-sess");
        assert_eq!(turns[1].1, "assistant");
        assert_eq!(turns[1].2, "Hello V2");

        assert_eq!(turns[2].0, "v3-sess");
        assert_eq!(turns[2].1, "user");
        assert_eq!(turns[2].2, "Hello V3");
    }

    #[test]
    fn session_artifacts_persist_across_reopen() {
        let temp_dir = tempfile::tempdir().unwrap();
        let db_path = temp_dir.path().join("artifacts.db");
        let path_str = db_path.to_str().unwrap();
        let key = test_key();

        let artifact = maho_types::artifact::ArtifactInfo {
            artifact_id: "artifact-1".to_string(),
            session_id: "sess-1".to_string(),
            display_name: "Report.pdf".to_string(),
            mime_type: "application/pdf".to_string(),
            size_bytes: 2048,
            storage_rel_path: "sess-1/report.pdf".to_string(),
            created_at_ms: 1_754_000_000_000,
            kind: Some("pdf".to_string()),
        };

        // Upgrade a database containing the pre-kind artifact schema.
        {
            let conn = Connection::open(path_str).unwrap();
            apply_key(&conn, &key).unwrap();
            conn.execute_batch("CREATE TABLE session_artifacts (
                artifact_id TEXT PRIMARY KEY, session_id TEXT NOT NULL,
                display_name TEXT NOT NULL, mime_type TEXT NOT NULL,
                size_bytes INTEGER NOT NULL, storage_rel_path TEXT NOT NULL,
                created_at_ms INTEGER NOT NULL);").unwrap();
        }
        {
            let storage = SqliteStorage::open_with_key(path_str, &key).unwrap();
            storage.insert_artifact(&artifact).unwrap();
            // A different session must not leak into sess-1's listing.
            storage
                .insert_artifact(&maho_types::artifact::ArtifactInfo {
                    artifact_id: "artifact-2".to_string(),
                    session_id: "other-sess".to_string(),
                    ..artifact.clone()
                })
                .unwrap();
        }

        // Reopen the same database (simulates a session reload / restart).
        let storage = SqliteStorage::open_with_key(path_str, &key).unwrap();
        let listed = storage.list_artifacts("sess-1").unwrap();
        assert_eq!(listed.len(), 1);
        assert_eq!(listed[0], artifact);

        // Unknown session yields an empty list, not an error.
        assert!(storage.list_artifacts("nope").unwrap().is_empty());
    }

    #[test]
    fn agent_runtime_config_persists_across_reopen() {
        let temp_dir = tempfile::tempdir().unwrap();
        let db_path = temp_dir.path().join("runtime-config.db");
        let path_str = db_path.to_str().unwrap();
        let key = test_key();

        {
            let storage = SqliteStorage::open_with_key(path_str, &key).unwrap();
            // No row before the first write.
            assert!(storage
                .load_agent_runtime_config("sess-a")
                .unwrap()
                .is_none());
            storage
                .save_agent_runtime_config("sess-a", "read_only", false, true)
                .unwrap();
            storage
                .save_agent_runtime_config("sess-b", "full_access", true, false)
                .unwrap();
        }

        // Reopen the same database (simulates app restart / session reload).
        let storage = SqliteStorage::open_with_key(path_str, &key).unwrap();

        let row_a = storage
            .load_agent_runtime_config("sess-a")
            .unwrap()
            .unwrap();
        assert_eq!(row_a.session_id, "sess-a");
        assert_eq!(row_a.permission_tier, "read_only");
        assert!(!row_a.final_confirm);
        assert!(row_a.proactive_mode);

        let row_b = storage
            .load_agent_runtime_config("sess-b")
            .unwrap()
            .unwrap();
        assert_eq!(row_b.permission_tier, "full_access");
        assert!(row_b.final_confirm);
        assert!(!row_b.proactive_mode);

        // Unknown session yields None, not an error.
        assert!(storage.load_agent_runtime_config("nope").unwrap().is_none());

        // Upsert: the most recent write wins.
        storage
            .save_agent_runtime_config("sess-a", "guard", true, false)
            .unwrap();
        let updated = storage
            .load_agent_runtime_config("sess-a")
            .unwrap()
            .unwrap();
        assert_eq!(updated.permission_tier, "guard");
        assert!(updated.final_confirm);
        assert!(!updated.proactive_mode);
    }

    #[test]
    fn test_enqueue_sync_entity() {
        let db = test_store();
        let id = db
            .enqueue_sync_entity("bookmarks", "bm-1", 1, 1000, r#"{"title":"Test"}"#, false)
            .unwrap();
        assert!(id > 0);

        let pending = db.load_pending_sync_entities().unwrap();
        assert_eq!(pending.len(), 1);
        let (row_id, entity_type, entity_id, version, modified_at, payload_json, deleted) =
            &pending[0];
        assert_eq!(*row_id, id);
        assert_eq!(entity_type, "bookmarks");
        assert_eq!(entity_id, "bm-1");
        assert_eq!(*version, 1);
        assert_eq!(*modified_at, 1000);
        assert_eq!(payload_json, r#"{"title":"Test"}"#);
        assert!(!deleted);
    }

    #[test]
    fn test_load_pending_sync_entities_order() {
        let db = test_store();
        let id1 = db
            .enqueue_sync_entity("bookmarks", "bm-1", 1, 1000, "{}", false)
            .unwrap();
        let id2 = db
            .enqueue_sync_entity("notes", "n-1", 2, 2000, "{}", false)
            .unwrap();
        let id3 = db
            .enqueue_sync_entity("bookmarks", "bm-2", 3, 3000, "{}", true)
            .unwrap();

        let pending = db.load_pending_sync_entities().unwrap();
        assert_eq!(pending.len(), 3);
        assert_eq!(pending[0].0, id1);
        assert_eq!(pending[1].0, id2);
        assert_eq!(pending[2].0, id3);
        assert!(id1 < id2);
        assert!(id2 < id3);
    }

    #[test]
    fn sqlite_save_and_load_css_mods() {
        let store = test_store();
        let m = maho_types::css_mod::CssMod {
            id: "mod1".into(),
            name: "Dark Scrollbars".into(),
            description: Some("Makes scrollbars dark".into()),
            author: Some("test".into()),
            version: Some("1.0.0".into()),
            css: "::-webkit-scrollbar { background: #1a1a1a; }".into(),
            enabled: true,
            homepage: None,
            source_url: None,
            created_at: "2024-01-01T00:00:00Z".into(),
            updated_at: "2024-01-01T00:00:00Z".into(),
        };
        store.save_css_mod(&m).unwrap();
        let loaded = store.load_css_mods().unwrap();
        assert_eq!(loaded.len(), 1);
        assert_eq!(loaded[0].name, "Dark Scrollbars");
        assert!(loaded[0].enabled);
    }

    #[test]
    fn sqlite_delete_css_mod() {
        let store = test_store();
        let m = maho_types::css_mod::CssMod {
            id: "mod2".into(),
            name: "Test Mod".into(),
            description: None,
            author: None,
            version: None,
            css: "body { color: red; }".into(),
            enabled: false,
            homepage: None,
            source_url: None,
            created_at: "2024-01-01T00:00:00Z".into(),
            updated_at: "2024-01-01T00:00:00Z".into(),
        };
        store.save_css_mod(&m).unwrap();
        assert!(store.delete_css_mod("mod2").unwrap());
        assert!(!store.delete_css_mod("mod2").unwrap());
        assert!(store.load_css_mods().unwrap().is_empty());
    }

    /// Sync V2 outbox contract (`docs/operations/sync-v2-protocol.md`):
    /// "Only the exact returned ACK removes the row from the outbox" and an
    /// interrupted client "resends the same delivery ID after restart". Marking
    /// an entity sent therefore leases it (`inflight`) and counts the attempt,
    /// but must keep it deliverable — dropping it at send time would lose the
    /// entity whenever the process dies before the ACK.
    #[test]
    fn test_mark_sync_entities_sent() {
        let db = test_store();
        let id1 = db
            .enqueue_sync_entity("bookmarks", "bm-1", 1, 1000, "{}", false)
            .unwrap();
        let id2 = db
            .enqueue_sync_entity("bookmarks", "bm-2", 2, 2000, "{}", false)
            .unwrap();

        db.mark_sync_entities_sent(&[id1]).unwrap();

        let pending = db.load_pending_sync_entities().unwrap();
        assert_eq!(
            pending.iter().map(|row| row.0).collect::<Vec<_>>(),
            vec![id1, id2],
            "a sent-but-unacked row stays redeliverable"
        );

        let (state, attempts): (String, i64) = db
            .conn
            .query_row(
                "SELECT state, attempt_count FROM sync_queue WHERE id = ?1",
                rusqlite::params![id1],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )
            .unwrap();
        assert_eq!(state, "inflight");
        assert_eq!(attempts, 1);

        // Only the ACK retires the row.
        db.ack_sync_entities(&[id1]).unwrap();
        let pending = db.load_pending_sync_entities().unwrap();
        assert_eq!(
            pending.iter().map(|row| row.0).collect::<Vec<_>>(),
            vec![id2]
        );
    }

    #[test]
    fn test_ack_sync_entities() {
        let db = test_store();
        let id1 = db
            .enqueue_sync_entity("bookmarks", "bm-1", 1, 1000, "{}", false)
            .unwrap();
        let id2 = db
            .enqueue_sync_entity("bookmarks", "bm-2", 2, 2000, "{}", false)
            .unwrap();

        db.ack_sync_entities(&[id1]).unwrap();

        let pending = db.load_pending_sync_entities().unwrap();
        assert_eq!(pending.len(), 1);
        assert_eq!(pending[0].0, id2);
    }

    #[test]
    fn test_get_last_pulled_version_default() {
        let db = test_store();
        let version = db.get_sync_last_pulled_version("bookmarks").unwrap();
        assert_eq!(version, 0);
    }

    #[test]
    fn test_set_and_get_last_pulled_version() {
        let db = test_store();
        db.set_sync_last_pulled_version("bookmarks", 42).unwrap();
        let version = db.get_sync_last_pulled_version("bookmarks").unwrap();
        assert_eq!(version, 42);

        db.set_sync_last_pulled_version("bookmarks", 99).unwrap();
        let version = db.get_sync_last_pulled_version("bookmarks").unwrap();
        assert_eq!(version, 99);
    }

    #[test]
    fn test_pulled_version_per_entity_type() {
        let db = test_store();
        db.set_sync_last_pulled_version("bookmarks", 10).unwrap();
        db.set_sync_last_pulled_version("notes", 20).unwrap();

        assert_eq!(db.get_sync_last_pulled_version("bookmarks").unwrap(), 10);
        assert_eq!(db.get_sync_last_pulled_version("notes").unwrap(), 20);
    }

    #[test]
    fn test_search_memories_fts_importance_ordering() {
        // Given: two equally relevant, equally recent facts with different importance.
        let db = test_store();
        insert_memory_fact(
            &db,
            &MemoryFactFixture {
                id: "high-importance",
                fact: "saturn project context",
                importance: 0.9,
                created_at: RECENT_CREATED_AT,
            },
        );
        insert_memory_fact(
            &db,
            &MemoryFactFixture {
                id: "low-importance",
                fact: "saturn project context",
                importance: 0.1,
                created_at: RECENT_CREATED_AT,
            },
        );

        // When: hybrid FTS search scores the shared query term.
        let results = db.search_memories_fts("saturn", 2).unwrap();

        // Then: larger importance makes the negative bm25 score more negative, so it ranks first.
        assert_eq!(
            memory_search_ids(&results),
            vec!["high-importance", "low-importance"]
        );
    }

    #[test]
    fn test_search_memories_fts_recency_ordering() {
        // Given: two equally relevant, equally important facts with different ages.
        let db = test_store();
        insert_memory_fact(
            &db,
            &MemoryFactFixture {
                id: "recent-fact",
                fact: "orion project context",
                importance: 0.7,
                created_at: RECENT_CREATED_AT,
            },
        );
        insert_memory_fact(
            &db,
            &MemoryFactFixture {
                id: "old-fact",
                fact: "orion project context",
                importance: 0.7,
                created_at: OLD_CREATED_AT,
            },
        );

        // When: hybrid FTS search applies the recency multiplier.
        let results = db.search_memories_fts("orion", 2).unwrap();

        // Then: the recent fact keeps the strongest negative score and ranks first.
        assert_eq!(memory_search_ids(&results), vec!["recent-fact", "old-fact"]);
    }

    #[test]
    fn test_search_memories_fts_relevance_still_dominates_reasonably() {
        // Given: a stronger textual match with decent importance and a weaker high-importance match.
        let db = test_store();
        insert_memory_fact(
            &db,
            &MemoryFactFixture {
                id: "strong-decent-importance",
                fact: "nebula nebula nebula nebula nebula context",
                importance: 0.7,
                created_at: RECENT_CREATED_AT,
            },
        );
        insert_memory_fact(
            &db,
            &MemoryFactFixture {
                id: "weak-high-importance",
                fact: "nebula context",
                importance: 1.0,
                created_at: RECENT_CREATED_AT,
            },
        );

        // When: current hybrid scoring multiplies negative bm25 by importance.
        let results = db.search_memories_fts("nebula", 2).unwrap();

        // Then: empirically, the 1.0-importance weak match outranks the stronger
        // 0.7-importance match. Lock that behavior rather than imposing relevance dominance.
        assert_eq!(
            memory_search_ids(&results),
            vec!["weak-high-importance", "strong-decent-importance"]
        );
    }

    #[test]
    fn memory_embedding_change_journal_returns_latest_state() {
        let db = test_store();
        db.insert_memory(MemoryInsertParams {
            id: "changed-fact",
            fact: "journal fact",
            source: "test",
            session_id: None,
            categories: "[]",
            importance: 0.5,
            metadata: None,
        })
        .unwrap();
        let after_insert = db.memory_index_change_sequence().unwrap();

        db.update_memory_embedding("changed-fact", &[1, 2, 3, 4])
            .unwrap();
        let (after_embedding, changes) = db.load_memory_embeddings_since(after_insert).unwrap();
        assert!(after_embedding > after_insert);
        assert_eq!(
            changes,
            vec![("changed-fact".to_string(), Some(vec![1, 2, 3, 4]))]
        );

        db.delete_memory("changed-fact").unwrap();
        let (after_delete, changes) = db.load_memory_embeddings_since(after_embedding).unwrap();
        assert!(after_delete > after_embedding);
        assert_eq!(changes, vec![("changed-fact".to_string(), None)]);
    }

    #[test]
    fn test_search_memories_fts_respects_top_k() {
        // Given: more matching facts than the requested result limit.
        let db = test_store();
        for index in 0..5 {
            let id = format!("top-k-{index}");
            let fact = format!("topk project context {index}");
            insert_memory_fact(
                &db,
                &MemoryFactFixture {
                    id: &id,
                    fact: &fact,
                    importance: 0.5,
                    created_at: RECENT_CREATED_AT,
                },
            );
        }

        // When: search is capped below the number of matches.
        let results = db.search_memories_fts("topk", 3).unwrap();

        // Then: SQLite LIMIT is honored by the public search API.
        assert_eq!(results.len(), 3);
    }

    #[test]
    fn test_prune_old_memories_keeps_highest_importance_recent() {
        // Given: more memory facts than the retention limit with varied priority and age.
        let db = test_store();
        for fact in [
            MemoryFactFixture {
                id: "highest-recent",
                fact: "prune token highest recent",
                importance: 0.9,
                created_at: RECENT_CREATED_AT,
            },
            MemoryFactFixture {
                id: "highest-old",
                fact: "prune token highest old",
                importance: 0.9,
                created_at: OLD_CREATED_AT,
            },
            MemoryFactFixture {
                id: "medium-recent",
                fact: "prune token medium recent",
                importance: 0.5,
                created_at: RECENT_CREATED_AT,
            },
            MemoryFactFixture {
                id: "medium-old",
                fact: "prune token medium old",
                importance: 0.5,
                created_at: OLD_CREATED_AT,
            },
            MemoryFactFixture {
                id: "low-recent",
                fact: "prune token low recent",
                importance: 0.1,
                created_at: RECENT_CREATED_AT,
            },
        ] {
            insert_memory_fact(&db, &fact);
        }

        // When: pruning keeps only the three highest-priority facts.
        db.prune_old_memories(3).unwrap();

        // Then: rows are retained by importance DESC, then created_at DESC.
        assert_eq!(
            string_column(
                &db,
                "SELECT id FROM memory_facts ORDER BY importance DESC, created_at DESC",
            ),
            vec!["highest-recent", "highest-old", "medium-recent"]
        );
        assert_eq!(
            string_column(&db, "SELECT id FROM memory_facts_fts ORDER BY id"),
            vec!["highest-old", "highest-recent", "medium-recent"]
        );
    }

    #[test]
    fn test_search_notes_fts_bm25_order() {
        // Given: note FTS rows with one strong and one weak match for the same term.
        let db = test_store();
        db.index_note_fts("strong-note", "lumen lumen lumen lumen lumen context")
            .unwrap();
        db.index_note_fts("weak-note", "lumen context").unwrap();

        // When: plain note FTS search orders by bm25 rank.
        let results = db.search_notes_fts("lumen").unwrap();

        // Then: SQLite's more negative bm25 score ranks first under ORDER BY rank ASC.
        assert_eq!(note_search_ids(&results), vec!["strong-note", "weak-note"]);
        assert!(results[0].1 < results[1].1);
    }

    #[test]
    fn test_boost_migration_adds_new_columns_and_migrates_size() {
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        let key = test_key();
        conn.pragma_update(None, "key", &key).unwrap();

        conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS boosts (
                id TEXT PRIMARY KEY,
                domain TEXT NOT NULL,
                name TEXT NOT NULL,
                enable_color_boost INTEGER NOT NULL DEFAULT 0,
                dot_angle_deg REAL NOT NULL DEFAULT 0.0,
                secondary_dot_angle_deg_delta REAL NOT NULL DEFAULT 0.0,
                brightness REAL NOT NULL DEFAULT 1.0,
                saturation REAL NOT NULL DEFAULT 1.0,
                contrast REAL NOT NULL DEFAULT 1.0,
                auto_theme INTEGER NOT NULL DEFAULT 0,
                smart_invert INTEGER NOT NULL DEFAULT 0,
                font_family TEXT,
                text_case_override TEXT NOT NULL DEFAULT 'none',
                size_override REAL,
                zap_selectors TEXT NOT NULL DEFAULT '[]',
                custom_css TEXT NOT NULL DEFAULT '',
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS passwords (id TEXT PRIMARY KEY);
            CREATE TABLE IF NOT EXISTS shortcuts (id TEXT PRIMARY KEY);",
        )
        .unwrap();

        conn.execute(
            "INSERT INTO boosts (id, domain, name, brightness, saturation, contrast,
                size_override, text_case_override, created_at, updated_at)
             VALUES ('b1', 'example.com', 'Test', 1.0, 1.0, 1.0,
                1.25, 'capitalize', '2025-01-01T00:00:00', '2025-01-01T00:00:00')",
            [],
        )
        .unwrap();

        let db = SqliteStorage {
            conn,
            path: ":memory:".to_string(),
        };
        // Exercise the real upgrade path. `run_migrations` is never called on a
        // bare database in production: `initialize_schema` first creates every
        // base table with `IF NOT EXISTS` (so the legacy `boosts` table above
        // survives untouched) and only then migrates. Calling `run_migrations`
        // directly leaves the conversation tables absent and the conversation
        // migration fails hard on `no such table: conversations`.
        db.initialize_schema().unwrap();

        let dot_pos_x: f64 = db
            .conn
            .query_row("SELECT dot_pos_x FROM boosts WHERE id = 'b1'", [], |r| {
                r.get(0)
            })
            .unwrap();
        assert!((dot_pos_x - 0.76).abs() < 0.01);

        let dot_pos_y: f64 = db
            .conn
            .query_row("SELECT dot_pos_y FROM boosts WHERE id = 'b1'", [], |r| {
                r.get(0)
            })
            .unwrap();
        assert!((dot_pos_y - 0.66).abs() < 0.01);

        let size_mode: String = db
            .conn
            .query_row("SELECT size_mode FROM boosts WHERE id = 'b1'", [], |r| {
                r.get(0)
            })
            .unwrap();
        assert_eq!(size_mode, "k125");

        let brightness: f64 = db
            .conn
            .query_row("SELECT brightness FROM boosts WHERE id = 'b1'", [], |r| {
                r.get(0)
            })
            .unwrap();
        assert!((brightness - 1.0).abs() < f64::EPSILON);

        let change_was_made: i32 = db
            .conn
            .query_row(
                "SELECT change_was_made FROM boosts WHERE id = 'b1'",
                [],
                |r| r.get(0),
            )
            .unwrap();
        assert_eq!(change_was_made, 0);

        // Migrations are re-entrant: a second pass over an already-migrated
        // database must succeed and must not disturb the migrated values.
        db.run_migrations().unwrap();
        let size_mode_after_rerun: String = db
            .conn
            .query_row("SELECT size_mode FROM boosts WHERE id = 'b1'", [], |r| {
                r.get(0)
            })
            .unwrap();
        assert_eq!(size_mode_after_rerun, "k125");
    }
}

impl SqliteStorage {
    // === Memory facts ===

    pub fn delete_all_memories(&self) -> Result<(), StorageError> {
        self.conn.execute("DELETE FROM memory_facts", [])?;
        self.conn.execute("DELETE FROM memory_facts_fts", [])?;
        Ok(())
    }

    pub fn delete_memory(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM memory_facts WHERE id = ?1",
            rusqlite::params![id],
        )?;
        if affected > 0 {
            let _ = self.conn.execute(
                "DELETE FROM memory_facts_fts WHERE id = ?1",
                rusqlite::params![id],
            );
        }
        Ok(affected > 0)
    }

    pub fn insert_memory(&self, params: MemoryInsertParams<'_>) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO memory_facts \
             (id, fact, source, session_id, categories, importance, metadata) \
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            rusqlite::params![
                params.id,
                params.fact,
                params.source,
                params.session_id,
                params.categories,
                params.importance,
                params.metadata
            ],
        )?;
        let _ = self.conn.execute(
            "DELETE FROM memory_facts_fts WHERE id = ?1",
            rusqlite::params![params.id],
        );
        self.conn.execute(
            "INSERT INTO memory_facts_fts (id, fact) VALUES (?1, ?2)",
            rusqlite::params![params.id, params.fact],
        )?;
        self.prune_old_memories(1000)?;
        Ok(())
    }

    pub fn prune_old_memories(&self, limit: usize) -> Result<(), StorageError> {
        self.conn.execute(
            "DELETE FROM memory_facts WHERE id NOT IN ( \
             SELECT id FROM memory_facts \
             ORDER BY importance DESC, created_at DESC \
             LIMIT ?1)",
            rusqlite::params![limit as i64],
        )?;
        self.conn.execute(
            "DELETE FROM memory_facts_fts WHERE id NOT IN (SELECT id FROM memory_facts)",
            [],
        )?;
        Ok(())
    }

    pub fn update_memory_embedding(&self, fact_id: &str, bytes: &[u8]) -> Result<(), StorageError> {
        self.conn.execute(
            "UPDATE memory_facts SET embedding = ?1 WHERE id = ?2",
            rusqlite::params![bytes, fact_id],
        )?;
        Ok(())
    }

    pub fn load_all_memories(&self) -> Result<Vec<MemoryRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, fact, source, session_id, categories, importance, metadata FROM memory_facts",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, Option<String>>(3)?,
                row.get::<_, String>(4)?,
                row.get::<_, f64>(5)?,
                row.get::<_, Option<String>>(6)?,
            ))
        })?;
        let mut out = Vec::new();
        for r in rows {
            out.push(r?);
        }
        Ok(out)
    }

    // === Memory blocks ===

    pub fn upsert_memory_block(
        &self,
        label: &str,
        content: &str,
        tier: &str,
        token_count: i64,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO memory_blocks (label, content, tier, token_count, updated_at) \
             VALUES (?1, ?2, ?3, ?4, datetime('now')) \
             ON CONFLICT(label) DO UPDATE SET \
               content = excluded.content, \
               tier = excluded.tier, \
               token_count = excluded.token_count, \
               updated_at = excluded.updated_at",
            rusqlite::params![label, content, tier, token_count],
        )?;
        Ok(())
    }

    pub fn get_memory_block(&self, label: &str) -> Result<Option<String>, StorageError> {
        match self.conn.query_row(
            "SELECT content FROM memory_blocks WHERE label = ?1",
            rusqlite::params![label],
            |row| row.get::<_, String>(0),
        ) {
            Ok(c) => Ok(Some(c)),
            Err(rusqlite::Error::QueryReturnedNoRows) => Ok(None),
            Err(e) => Err(StorageError::from(e)),
        }
    }

    pub fn list_memory_blocks_by_tier(
        &self,
        tier: &str,
    ) -> Result<Vec<(String, String)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT label, content FROM memory_blocks WHERE tier = ?1 ORDER BY updated_at DESC",
        )?;
        let rows = stmt.query_map(rusqlite::params![tier], |row| {
            Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?))
        })?;
        let mut out = Vec::new();
        for row in rows {
            out.push(row?);
        }
        Ok(out)
    }

    // === Browse sessions ===

    pub fn list_browse_sessions_since(
        &self,
        since_iso: &str,
    ) -> Result<Vec<(String, String, Option<String>)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, url, title FROM browse_sessions WHERE started_at >= ?1 ORDER BY started_at DESC",
        )?;
        let rows = stmt.query_map(rusqlite::params![since_iso], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, Option<String>>(2)?,
            ))
        })?;
        let mut out = Vec::new();
        for row in rows {
            out.push(row?);
        }
        Ok(out)
    }

    // === Memory embeddings ===

    pub fn load_all_active_memory_embeddings(
        &self,
    ) -> Result<Vec<(String, Vec<u8>)>, StorageError> {
        let mut stmt = self
            .conn
            .prepare("SELECT id, embedding FROM memory_facts WHERE embedding IS NOT NULL")?;
        let rows = stmt.query_map([], |row| {
            Ok((row.get::<_, String>(0)?, row.get::<_, Vec<u8>>(1)?))
        })?;
        let mut out = Vec::new();
        for row in rows {
            out.push(row?);
        }
        Ok(out)
    }

    /// Return the latest state of every memory fact changed after `sequence`,
    /// along with the journal high-water mark. A `None` embedding means the
    /// fact was deleted or currently has no embedding and must be removed from
    /// the ANN index.
    pub fn load_memory_embeddings_since(
        &self,
        sequence: i64,
    ) -> Result<(i64, Vec<(String, Option<Vec<u8>>)>), StorageError> {
        let high_water = self.conn.query_row(
            "SELECT COALESCE(MAX(sequence), 0) FROM memory_index_changes",
            [],
            |row| row.get::<_, i64>(0),
        )?;
        if high_water == 0 || high_water == sequence {
            return Ok((high_water, Vec::new()));
        }
        if high_water < sequence {
            return Ok((high_water, Vec::new()));
        }

        let mut stmt = self.conn.prepare(
            "SELECT c.fact_id, f.embedding
             FROM memory_index_changes c
             LEFT JOIN memory_facts f ON f.id = c.fact_id
             WHERE c.sequence > ?1 AND c.sequence <= ?2
               AND c.sequence = (
                   SELECT MAX(latest.sequence)
                   FROM memory_index_changes latest
                   WHERE latest.fact_id = c.fact_id AND latest.sequence <= ?2
               )
             ORDER BY c.sequence",
        )?;
        let rows = stmt.query_map(rusqlite::params![sequence, high_water], |row| {
            Ok((row.get::<_, String>(0)?, row.get::<_, Option<Vec<u8>>>(1)?))
        })?;
        let changes = rows.collect::<Result<Vec<_>, _>>()?;
        Ok((high_water, changes))
    }

    pub fn memory_index_change_sequence(&self) -> Result<i64, StorageError> {
        self.conn
            .query_row(
                "SELECT COALESCE(MAX(sequence), 0) FROM memory_index_changes",
                [],
                |row| row.get(0),
            )
            .map_err(StorageError::from)
    }

    pub fn search_memories_fts(
        &self,
        query: &str,
        top_k: usize,
    ) -> Result<Vec<(String, String, f64)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT f.id, f.fact, \
             (bm25(memory_facts_fts) * MAX(0.01, f.importance) * (1.0 / (1.0 + CAST(MAX(0, COALESCE(strftime('%s', 'now') - strftime('%s', f.created_at), 0)) AS REAL) / 86400.0))) AS score \
             FROM memory_facts_fts \
             JOIN memory_facts f ON f.id = memory_facts_fts.id \
             WHERE memory_facts_fts MATCH ?1 \
             ORDER BY score \
             LIMIT ?2",
        )?;
        let rows = stmt.query_map(rusqlite::params![query, top_k as i64], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, f64>(2)?,
            ))
        })?;
        let mut out = Vec::new();
        for row in rows {
            out.push(row?);
        }
        Ok(out)
    }

    // === Conversation turns (low-level) ===

    pub fn insert_conversation_turn(
        &self,
        id: &str,
        session_id: &str,
        role: &str,
        content: &str,
        url_context: Option<&str>,
    ) -> Result<(), StorageError> {
        // Ensure the parent conversations row exists to satisfy the
        // conversation_turns.session_id FOREIGN KEY constraint. Idempotent via
        // INSERT OR IGNORE: no-op when the row already exists.
        self.conn.execute(
            "INSERT OR IGNORE INTO conversations (id) VALUES (?1)",
            rusqlite::params![session_id],
        )?;
        self.conn.execute(
            "INSERT OR IGNORE INTO conversation_turns \
             (id, session_id, role, content, url_context) \
             VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params![id, session_id, role, content, url_context],
        )?;
        let _ = self.conn.execute(
            "UPDATE conversations SET updated_at = datetime('now'), archived_at = NULL WHERE id = ?1",
            rusqlite::params![session_id],
        );
        Ok(())
    }

    pub fn load_conversation_session(
        &self,
        session_id: &str,
    ) -> Result<Vec<(String, String)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT role, content FROM conversation_turns WHERE session_id = ?1 ORDER BY created_at ASC",
        )?;
        let rows = stmt.query_map(rusqlite::params![session_id], |row| {
            Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?))
        })?;
        let mut out = Vec::new();
        for row in rows {
            out.push(row?);
        }
        Ok(out)
    }

    // === Conversations (high-level CRUD) ===

    pub fn list_conversations(
        &self,
        state: maho_types::chat::ConversationListState,
        limit: usize,
    ) -> Result<Vec<maho_types::chat::Conversation>, StorageError> {
        let filter = match state {
            maho_types::chat::ConversationListState::Active => "WHERE archived_at IS NULL",
            maho_types::chat::ConversationListState::Archived => "WHERE archived_at IS NOT NULL",
            maho_types::chat::ConversationListState::All => "",
        };
        let order = match state {
            maho_types::chat::ConversationListState::Archived => {
                "ORDER BY archived_at DESC, updated_at DESC"
            }
            _ => "ORDER BY updated_at DESC",
        };
        let sql = format!(
            "SELECT id, title, space_id, model, created_at, updated_at, archived_at, project_id \
             FROM conversations {filter} {order} LIMIT ?1"
        );
        let mut stmt = self.conn.prepare(&sql)?;
        let rows = stmt.query_map(rusqlite::params![limit as i64], |row| {
            Ok(maho_types::chat::Conversation {
                id: row.get(0)?,
                title: row.get(1)?,
                space_id: row.get(2)?,
                model: row.get(3)?,
                created_at: row.get(4)?,
                updated_at: row.get(5)?,
                archived_at: row.get(6)?,
                project_id: row.get(7)?,
            })
        })?;
        let mut out = Vec::new();
        for row in rows {
            out.push(row?);
        }
        Ok(out)
    }

    pub fn get_conversation_messages(
        &self,
        session_id: &str,
    ) -> Result<Vec<maho_types::chat::ConversationTurn>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, session_id, role, content, url_context, created_at \
             FROM conversation_turns WHERE session_id = ?1 ORDER BY created_at DESC, id DESC LIMIT 100",
        )?;
        let rows = stmt.query_map(rusqlite::params![session_id], |row| {
            Ok(maho_types::chat::ConversationTurn {
                id: row.get(0)?,
                session_id: row.get(1)?,
                role: row.get(2)?,
                content: row.get(3)?,
                url_context: row.get(4)?,
                created_at: row.get(5)?,
            })
        })?;
        let mut out = Vec::new();
        for row in rows {
            out.push(row?);
        }
        out.reverse();
        Ok(out)
    }

    pub fn insert_artifact(
        &self,
        artifact: &maho_types::artifact::ArtifactInfo,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO session_artifacts \
             (artifact_id, session_id, display_name, mime_type, size_bytes, storage_rel_path, created_at_ms, kind) \
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)",
            rusqlite::params![
                artifact.artifact_id,
                artifact.session_id,
                artifact.display_name,
                artifact.mime_type,
                artifact.size_bytes as i64,
                artifact.storage_rel_path,
                artifact.created_at_ms,
                artifact.kind,
            ],
        )?;
        Ok(())
    }

    pub fn list_artifacts(
        &self,
        session_id: &str,
    ) -> Result<Vec<maho_types::artifact::ArtifactInfo>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT artifact_id, session_id, display_name, mime_type, size_bytes, storage_rel_path, created_at_ms, kind \
             FROM session_artifacts WHERE session_id = ?1 ORDER BY created_at_ms ASC",
        )?;
        let rows = stmt.query_map(rusqlite::params![session_id], |row| {
            Ok(maho_types::artifact::ArtifactInfo {
                artifact_id: row.get(0)?,
                session_id: row.get(1)?,
                display_name: row.get(2)?,
                mime_type: row.get(3)?,
                size_bytes: row.get::<_, i64>(4)? as u64,
                storage_rel_path: row.get(5)?,
                created_at_ms: row.get(6)?,
                kind: row.get(7)?,
            })
        })?;
        let mut out = Vec::new();
        for row in rows {
            out.push(row?);
        }
        Ok(out)
    }

    /// Persist the agent session's runtime-config triple (Wave 1A), keyed by
    /// the FFI session id. Upsert: the most recent write wins.
    pub fn save_agent_runtime_config(
        &self,
        session_id: &str,
        permission_tier: &str,
        final_confirm: bool,
        proactive_mode: bool,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO agent_runtime_config \
             (session_id, permission_tier, final_confirm, proactive_mode, updated_at) \
             VALUES (?1, ?2, ?3, ?4, datetime('now'))",
            rusqlite::params![
                session_id,
                permission_tier,
                final_confirm as i64,
                proactive_mode as i64,
            ],
        )?;
        Ok(())
    }

    /// Load the persisted agent runtime-config row for a session, if any.
    /// A missing row means "no persisted config" — the caller applies its own
    /// defaults (the FFI session falls back to the plumbing defaults).
    pub fn load_agent_runtime_config(
        &self,
        session_id: &str,
    ) -> Result<Option<AgentRuntimeConfigRow>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT session_id, permission_tier, final_confirm, proactive_mode \
             FROM agent_runtime_config WHERE session_id = ?1",
        )?;
        let row = stmt
            .query_row(rusqlite::params![session_id], |row| {
                Ok(AgentRuntimeConfigRow {
                    session_id: row.get(0)?,
                    permission_tier: row.get(1)?,
                    final_confirm: row.get::<_, i64>(2)? != 0,
                    proactive_mode: row.get::<_, i64>(3)? != 0,
                })
            })
            .optional()?;
        Ok(row)
    }

    pub fn create_conversation(
        &self,
        id: &str,
        title: Option<&str>,
        space_id: Option<&str>,
        model: Option<&str>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR IGNORE INTO conversations (id, title, space_id, model) VALUES (?1, ?2, ?3, ?4)",
            rusqlite::params![id, title, space_id, model],
        )?;
        Ok(())
    }

    pub fn upsert_conversation(
        &self,
        conversation: &maho_types::chat::Conversation,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO conversations (id,title,space_id,model,created_at,updated_at,archived_at,project_id)
             VALUES (?1,?2,?3,?4,?5,?6,?7,?8)
             ON CONFLICT(id) DO UPDATE SET title=excluded.title,space_id=excluded.space_id,
             model=excluded.model,created_at=excluded.created_at,updated_at=excluded.updated_at,
             archived_at=excluded.archived_at,project_id=excluded.project_id",
            rusqlite::params![conversation.id,conversation.title,conversation.space_id,conversation.model,
                conversation.created_at,conversation.updated_at,conversation.archived_at,conversation.project_id],
        )?;
        Ok(())
    }

    pub fn set_system_prompt_snapshot_if_absent(
        &self,
        conversation_id: &str,
        system_prompt: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR IGNORE INTO conversations (id) VALUES (?1)",
            rusqlite::params![conversation_id],
        )?;
        self.conn.execute(
            "UPDATE conversations SET system_prompt_snapshot = ?1 \
             WHERE id = ?2 AND system_prompt_snapshot IS NULL",
            rusqlite::params![system_prompt, conversation_id],
        )?;
        Ok(())
    }

    pub fn get_system_prompt_snapshot(
        &self,
        conversation_id: &str,
    ) -> Result<Option<String>, StorageError> {
        let result = self.conn.query_row(
            "SELECT system_prompt_snapshot FROM conversations WHERE id = ?1",
            rusqlite::params![conversation_id],
            |row| row.get::<_, Option<String>>(0),
        );
        match result {
            Ok(v) => Ok(v),
            Err(rusqlite::Error::QueryReturnedNoRows) => Ok(None),
            Err(e) => Err(e.into()),
        }
    }

    /// True when a conversations row with this id exists.
    pub fn conversation_exists(&self, id: &str) -> Result<bool, StorageError> {
        let mut stmt = self
            .conn
            .prepare("SELECT 1 FROM conversations WHERE id = ?1 LIMIT 1")?;
        let mut rows = stmt.query(rusqlite::params![id])?;
        Ok(rows.next()?.is_some())
    }

    pub fn delete_conversation(&self, id: &str) -> Result<bool, StorageError> {
        let tx = self.conn.unchecked_transaction()?;
        tx.execute(
            "DELETE FROM session_artifacts WHERE session_id=?1",
            rusqlite::params![id],
        )?;
        tx.execute(
            "DELETE FROM settings WHERE key=?1",
            rusqlite::params![format!("web_ai.composer_draft.v1.conversation:{id}")],
        )?;
        let affected = tx.execute(
            "DELETE FROM conversations WHERE id=?1",
            rusqlite::params![id],
        )?;
        tx.commit()?;
        Ok(affected > 0)
    }

    pub fn archive_conversation(&self, id: &str, now: &str) -> Result<bool, StorageError> {
        Ok(self.conn.execute(
            "UPDATE conversations SET archived_at=?1 WHERE id=?2 AND archived_at IS NULL",
            rusqlite::params![now, id],
        )? > 0)
    }

    pub fn unarchive_conversation(&self, id: &str, now: &str) -> Result<bool, StorageError> {
        Ok(self.conn.execute("UPDATE conversations SET archived_at=NULL,updated_at=?1 WHERE id=?2 AND archived_at IS NOT NULL", rusqlite::params![now,id])? > 0)
    }

    pub fn apply_conversation_bulk_operation(
        &self,
        operation: maho_types::chat::ConversationBulkOperation,
        ids: &[String],
        now: &str,
    ) -> Result<maho_types::chat::ConversationBulkResult, StorageError> {
        let mut seen = std::collections::HashSet::new();
        let unique: Vec<&str> = ids
            .iter()
            .map(String::as_str)
            .filter(|id| seen.insert((*id).to_string()))
            .collect();
        if unique.is_empty() || unique.len() > 500 {
            return Err(StorageError::Other(
                "conversation bulk ids must contain 1..=500 unique ids".into(),
            ));
        }
        let tx = self.conn.unchecked_transaction()?;
        let mut result = maho_types::chat::ConversationBulkResult {
            requested_count: unique.len(),
            affected_ids: vec![],
            unchanged_ids: vec![],
            missing_ids: vec![],
        };
        for id in unique {
            let archived_at: Option<Option<String>> = tx
                .query_row(
                    "SELECT archived_at FROM conversations WHERE id=?1",
                    rusqlite::params![id],
                    |row| row.get(0),
                )
                .optional()?;
            let Some(archived_at) = archived_at else {
                result.missing_ids.push(id.into());
                continue;
            };
            let affected = match operation {
                maho_types::chat::ConversationBulkOperation::Archive if archived_at.is_none() => {
                    tx.execute(
                        "UPDATE conversations SET archived_at=?1 WHERE id=?2",
                        rusqlite::params![now, id],
                    )?;
                    true
                }
                maho_types::chat::ConversationBulkOperation::Unarchive if archived_at.is_some() => {
                    tx.execute(
                        "UPDATE conversations SET archived_at=NULL,updated_at=?1 WHERE id=?2",
                        rusqlite::params![now, id],
                    )?;
                    true
                }
                maho_types::chat::ConversationBulkOperation::Delete => {
                    tx.execute(
                        "DELETE FROM session_artifacts WHERE session_id=?1",
                        rusqlite::params![id],
                    )?;
                    tx.execute(
                        "DELETE FROM settings WHERE key=?1",
                        rusqlite::params![format!("web_ai.composer_draft.v1.conversation:{id}")],
                    )?;
                    tx.execute(
                        "DELETE FROM conversations WHERE id=?1",
                        rusqlite::params![id],
                    )?;
                    true
                }
                _ => false,
            };
            if affected {
                result.affected_ids.push(id.into());
            } else {
                result.unchanged_ids.push(id.into());
            }
        }
        tx.commit()?;
        Ok(result)
    }

    pub fn auto_archive_conversations(
        &self,
        now_sec: i64,
        after_days: i32,
    ) -> Result<Vec<String>, StorageError> {
        if after_days <= 0 {
            return Err(StorageError::Other("after_days must be positive".into()));
        }
        let cutoff = now_sec
            .checked_sub(i64::from(after_days) * 86_400)
            .ok_or_else(|| StorageError::Other("auto archive cutoff overflow".into()))?;
        let tx = self.conn.unchecked_transaction()?;
        let ids = {
            let mut stmt = tx.prepare("SELECT id FROM conversations WHERE archived_at IS NULL AND updated_at <= datetime(?1,'unixepoch') ORDER BY updated_at ASC,id ASC")?;
            let rows = stmt.query_map(rusqlite::params![cutoff], |row| row.get::<_, String>(0))?;
            rows.collect::<Result<Vec<_>, _>>()?
        };
        for id in &ids {
            tx.execute("UPDATE conversations SET archived_at=datetime(?1,'unixepoch') WHERE id=?2 AND archived_at IS NULL", rusqlite::params![now_sec,id])?;
        }
        tx.commit()?;
        Ok(ids)
    }

    pub fn list_conversation_projects(
        &self,
    ) -> Result<Vec<maho_types::chat::ConversationProject>, StorageError> {
        let mut stmt = self.conn.prepare("SELECT id,name,created_at,updated_at FROM conversation_projects ORDER BY updated_at DESC,id ASC")?;
        let rows = stmt.query_map([], |row| {
            Ok(maho_types::chat::ConversationProject {
                id: row.get(0)?,
                name: row.get(1)?,
                created_at: row.get(2)?,
                updated_at: row.get(3)?,
            })
        })?;
        Ok(rows.collect::<Result<Vec<_>, _>>()?)
    }

    pub fn create_conversation_project(
        &self,
        id: &str,
        name: &str,
        now: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO conversation_projects(id,name,created_at,updated_at) VALUES(?1,?2,?3,?3)",
            rusqlite::params![id, name, now],
        )?;
        Ok(())
    }

    pub fn upsert_conversation_project(
        &self,
        project: &maho_types::chat::ConversationProject,
    ) -> Result<(), StorageError> {
        self.conn.execute("INSERT INTO conversation_projects(id,name,created_at,updated_at) VALUES(?1,?2,?3,?4) ON CONFLICT(id) DO UPDATE SET name=excluded.name,created_at=excluded.created_at,updated_at=excluded.updated_at", rusqlite::params![project.id,project.name,project.created_at,project.updated_at])?;
        Ok(())
    }

    pub fn rename_conversation_project(
        &self,
        id: &str,
        name: &str,
        now: &str,
    ) -> Result<bool, StorageError> {
        Ok(self.conn.execute(
            "UPDATE conversation_projects SET name=?1,updated_at=?2 WHERE id=?3",
            rusqlite::params![name, now, id],
        )? > 0)
    }

    pub fn delete_conversation_project(&self, id: &str) -> Result<bool, StorageError> {
        let tx = self.conn.unchecked_transaction()?;
        tx.execute(
            "UPDATE conversations SET project_id=NULL WHERE project_id=?1",
            rusqlite::params![id],
        )?;
        let affected = tx.execute(
            "DELETE FROM conversation_projects WHERE id=?1",
            rusqlite::params![id],
        )?;
        tx.commit()?;
        Ok(affected > 0)
    }

    pub fn move_conversations_to_project(
        &self,
        ids: &[String],
        project_id: Option<&str>,
        now: &str,
    ) -> Result<maho_types::chat::ConversationBulkResult, StorageError> {
        let mut seen = std::collections::HashSet::new();
        let unique: Vec<&str> = ids
            .iter()
            .map(String::as_str)
            .filter(|id| seen.insert((*id).to_string()))
            .collect();
        if unique.is_empty() || unique.len() > 500 {
            return Err(StorageError::Other(
                "conversation move ids must contain 1..=500 unique ids".into(),
            ));
        }
        let tx = self.conn.unchecked_transaction()?;
        if let Some(project_id) = project_id {
            if tx
                .query_row(
                    "SELECT 1 FROM conversation_projects WHERE id=?1",
                    rusqlite::params![project_id],
                    |_| Ok(()),
                )
                .optional()?
                .is_none()
            {
                return Err(StorageError::NotFound {
                    entity: "conversation_project".into(),
                    id: project_id.into(),
                });
            }
        }
        let mut result = maho_types::chat::ConversationBulkResult {
            requested_count: unique.len(),
            affected_ids: vec![],
            unchanged_ids: vec![],
            missing_ids: vec![],
        };
        for id in unique {
            let current: Option<Option<String>> = tx
                .query_row(
                    "SELECT project_id FROM conversations WHERE id=?1",
                    rusqlite::params![id],
                    |row| row.get(0),
                )
                .optional()?;
            match current {
                None => result.missing_ids.push(id.into()),
                Some(current) if current.as_deref() == project_id => {
                    result.unchanged_ids.push(id.into())
                }
                Some(_) => {
                    tx.execute(
                        "UPDATE conversations SET project_id=?1,updated_at=?2 WHERE id=?3",
                        rusqlite::params![project_id, now, id],
                    )?;
                    result.affected_ids.push(id.into());
                }
            }
        }
        tx.commit()?;
        Ok(result)
    }

    pub fn delete_conversation_turn(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM conversation_turns WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(affected > 0)
    }

    /// Wave 1C (G10): ensure the conversation row exists, stamping `title`
    /// only on creation. Idempotent INSERT OR IGNORE: an existing row (e.g. a
    /// session about to be resumed) keeps its current title — re-titling on
    /// resume is deliberately out of scope. Returns true when the row was
    /// created here.
    pub fn ensure_conversation_with_title(
        &self,
        id: &str,
        title: &str,
    ) -> Result<bool, StorageError> {
        let inserted = self.conn.execute(
            "INSERT OR IGNORE INTO conversations (id, title) VALUES (?1, ?2)",
            rusqlite::params![id, title],
        )?;
        Ok(inserted > 0)
    }

    pub fn rename_conversation(&self, id: &str, title: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "UPDATE conversations SET title = ?1, updated_at = datetime('now') WHERE id = ?2",
            rusqlite::params![title, id],
        )?;
        Ok(affected > 0)
    }

    pub fn save_conversation_message(
        &self,
        session_id: &str,
        role: &str,
        content: &str,
        url_context: Option<&str>,
    ) -> Result<bool, StorageError> {
        let id = uuid::Uuid::new_v4().to_string();
        self.conn.execute(
            "INSERT OR IGNORE INTO conversations (id) VALUES (?1)",
            rusqlite::params![session_id],
        )?;
        self.conn.execute(
            "INSERT INTO conversation_turns (id, session_id, role, content, url_context) \
             VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params![id, session_id, role, content, url_context],
        )?;
        let _ = self.conn.execute(
            "UPDATE conversations SET updated_at = datetime('now'), archived_at = NULL WHERE id = ?1",
            rusqlite::params![session_id],
        );
        Ok(true)
    }

    // === Content Blocker Persistence ===

    pub fn save_content_blocker_list(
        &self,
        list: &maho_types::content_blocking::FilterListMetadata,
        raw_content: Option<&str>,
    ) -> Result<(), StorageError> {
        upsert_content_blocker_list(&self.conn, list, raw_content)
    }

    pub fn load_content_blocker_lists(
        &self,
    ) -> Result<Vec<(maho_types::content_blocking::FilterListMetadata, String)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, url, enabled, raw_content, rule_count, etag, last_modified, sha256,
                    last_attempt_timestamp, last_success_timestamp, failure_count, last_status, last_error
             FROM content_blocker_lists",
        )?;
        let rows = stmt.query_map([], |row| {
            let enabled_int: i32 = row.get(3)?;
            let list = maho_types::content_blocking::FilterListMetadata {
                id: row.get(0)?,
                name: row.get(1)?,
                url: row.get(2)?,
                enabled: enabled_int != 0,
                rule_count: row.get::<_, i64>(5)? as usize,
                etag: row.get(6)?,
                last_modified: row.get(7)?,
                sha256: row.get(8)?,
                last_attempt_timestamp: row.get(9)?,
                last_success_timestamp: row.get(10)?,
                failure_count: row.get(11)?,
                last_status: row.get(12)?,
                last_error: row.get(13)?,
            };
            let raw_content: String = row.get(4)?;
            Ok((list, raw_content))
        })?;
        let mut out = Vec::new();
        for row in rows {
            out.push(row?);
        }
        Ok(out)
    }

    pub fn delete_content_blocker_list(&self, id: &str) -> Result<bool, StorageError> {
        let count = self.conn.execute(
            "DELETE FROM content_blocker_lists WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(count > 0)
    }

    pub fn save_site_exception(&self, key: &str, created_at: i64) -> Result<(), StorageError> {
        upsert_site_exception(&self.conn, key, created_at)
    }

    pub fn delete_site_exception(&self, key: &str) -> Result<bool, StorageError> {
        let count = self.conn.execute(
            "DELETE FROM content_blocker_exceptions WHERE key = ?1",
            rusqlite::params![key],
        )?;
        Ok(count > 0)
    }

    pub fn load_site_exceptions(
        &self,
    ) -> Result<Vec<maho_types::content_blocking::CanonicalSiteException>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT key, created_at FROM content_blocker_exceptions ORDER BY created_at ASC",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok(maho_types::content_blocking::CanonicalSiteException {
                key: row.get(0)?,
                created_at: row.get(1)?,
            })
        })?;
        let mut out = Vec::new();
        for row in rows {
            out.push(row?);
        }
        Ok(out)
    }

    pub fn save_content_blocker_state(
        &self,
        mode: maho_types::content_blocking::ContentBlockingMode,
        generation: u64,
        engine_cache: Option<&[u8]>,
        engine_version: Option<&str>,
        engine_hash: Option<&str>,
    ) -> Result<(), StorageError> {
        upsert_content_blocker_state(
            &self.conn,
            mode,
            generation,
            engine_cache,
            engine_version,
            engine_hash,
        )
    }

    pub fn load_content_blocker_state(
        &self,
    ) -> Result<
        Option<(
            maho_types::content_blocking::ContentBlockingMode,
            u64,
            Option<Vec<u8>>,
            Option<String>,
            Option<String>,
        )>,
        StorageError,
    > {
        let result = self.conn.query_row(
            "SELECT mode, generation, engine_cache, engine_version, engine_hash FROM content_blocker_state WHERE id = 1",
            [],
            |row| {
                let mode_str: String = row.get(0)?;
                let mode = match mode_str.as_str() {
                    "native" => maho_types::content_blocking::ContentBlockingMode::Native,
                    "extension" => maho_types::content_blocking::ContentBlockingMode::Extension,
                    "disabled" => maho_types::content_blocking::ContentBlockingMode::Disabled,
                    _ => maho_types::content_blocking::ContentBlockingMode::Unknown,
                };
                let gen: i64 = row.get(1)?;
                let cache: Option<Vec<u8>> = row.get(2)?;
                let ver: Option<String> = row.get(3)?;
                let hash: Option<String> = row.get(4)?;
                Ok((mode, gen as u64, cache, ver, hash))
            },
        );
        match result {
            Ok(res) => Ok(Some(res)),
            Err(rusqlite::Error::QueryReturnedNoRows) => Ok(None),
            Err(e) => Err(e.into()),
        }
    }

    /// Run `body` inside one SQLite transaction over the content-blocker tables.
    /// On `Ok` the transaction commits; on `Err` returned from `body` it is
    /// dropped and SQLite rolls back every write, so the previously committed
    /// lists, exceptions, and engine state stay byte-identical. This is the
    /// atomic primitive the core lane composes candidate promotion on top of.
    ///
    /// `unchecked_transaction` is used because the connection is reached through
    /// `&self`; all writes are single-threaded through `&self`, so the unchecked
    /// borrow cannot race a second live transaction.
    pub fn content_blocker_transaction<T>(
        &self,
        body: impl FnOnce(&ContentBlockerTx<'_>) -> Result<T, StorageError>,
    ) -> Result<T, StorageError> {
        let tx = self.conn.unchecked_transaction()?;
        let conn: &Connection = &tx;
        let handle = ContentBlockerTx { conn };
        let value = body(&handle)?;
        tx.commit()?;
        Ok(value)
    }

    /// Atomically replace the entire persisted content-blocker snapshot: every
    /// filter list (metadata + last-known-good raw body), every canonical
    /// exception, and the singleton engine state. Any failure rolls the whole
    /// replacement back, leaving the prior snapshot intact.
    pub fn replace_content_blocker_snapshot(
        &self,
        snapshot: &ContentBlockerSnapshot<'_>,
    ) -> Result<(), StorageError> {
        self.content_blocker_transaction(|tx| {
            tx.clear_lists()?;
            tx.clear_exceptions()?;
            for (list, raw_content) in snapshot.lists {
                tx.save_list(list, Some(raw_content.as_str()))?;
            }
            for exception in snapshot.exceptions {
                tx.save_exception(&exception.key, exception.created_at)?;
            }
            tx.save_state(
                snapshot.mode,
                snapshot.generation,
                snapshot.engine_cache,
                snapshot.engine_version,
                snapshot.engine_hash,
            )
        })
    }
}

/// A full content-blocker persistence snapshot handed to
/// [`SqliteStorage::replace_content_blocker_snapshot`] for atomic replacement.
pub struct ContentBlockerSnapshot<'a> {
    pub mode: maho_types::content_blocking::ContentBlockingMode,
    pub generation: u64,
    pub engine_cache: Option<&'a [u8]>,
    pub engine_version: Option<&'a str>,
    pub engine_hash: Option<&'a str>,
    pub lists: &'a [(maho_types::content_blocking::FilterListMetadata, String)],
    pub exceptions: &'a [maho_types::content_blocking::CanonicalSiteException],
}

/// Transaction-scoped writer handed to the closure passed to
/// [`SqliteStorage::content_blocker_transaction`]. Every write goes through the
/// enclosing transaction, so returning `Err` from the closure rolls them back.
pub struct ContentBlockerTx<'conn> {
    conn: &'conn Connection,
}

impl ContentBlockerTx<'_> {
    pub fn clear_lists(&self) -> Result<(), StorageError> {
        self.conn.execute("DELETE FROM content_blocker_lists", [])?;
        Ok(())
    }

    pub fn clear_exceptions(&self) -> Result<(), StorageError> {
        self.conn
            .execute("DELETE FROM content_blocker_exceptions", [])?;
        Ok(())
    }

    pub fn save_list(
        &self,
        list: &maho_types::content_blocking::FilterListMetadata,
        raw_content: Option<&str>,
    ) -> Result<(), StorageError> {
        upsert_content_blocker_list(self.conn, list, raw_content)
    }

    pub fn delete_list(&self, id: &str) -> Result<bool, StorageError> {
        let count = self.conn.execute(
            "DELETE FROM content_blocker_lists WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(count > 0)
    }

    pub fn save_exception(&self, key: &str, created_at: i64) -> Result<(), StorageError> {
        upsert_site_exception(self.conn, key, created_at)
    }

    pub fn delete_exception(&self, key: &str) -> Result<bool, StorageError> {
        let count = self.conn.execute(
            "DELETE FROM content_blocker_exceptions WHERE key = ?1",
            rusqlite::params![key],
        )?;
        Ok(count > 0)
    }

    pub fn save_state(
        &self,
        mode: maho_types::content_blocking::ContentBlockingMode,
        generation: u64,
        engine_cache: Option<&[u8]>,
        engine_version: Option<&str>,
        engine_hash: Option<&str>,
    ) -> Result<(), StorageError> {
        upsert_content_blocker_state(
            self.conn,
            mode,
            generation,
            engine_cache,
            engine_version,
            engine_hash,
        )
    }
}

fn upsert_content_blocker_list(
    conn: &Connection,
    list: &maho_types::content_blocking::FilterListMetadata,
    raw_content: Option<&str>,
) -> Result<(), StorageError> {
    conn.execute(
        "INSERT INTO content_blocker_lists (
            id, name, url, enabled, raw_content, rule_count, etag, last_modified, sha256,
            last_attempt_timestamp, last_success_timestamp, failure_count, last_status, last_error
        ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)
        ON CONFLICT(id) DO UPDATE SET
            name = excluded.name,
            url = excluded.url,
            enabled = excluded.enabled,
            raw_content = CASE WHEN ?15 THEN excluded.raw_content ELSE raw_content END,
            rule_count = excluded.rule_count,
            etag = excluded.etag,
            last_modified = excluded.last_modified,
            sha256 = excluded.sha256,
            last_attempt_timestamp = excluded.last_attempt_timestamp,
            last_success_timestamp = excluded.last_success_timestamp,
            failure_count = excluded.failure_count,
            last_status = excluded.last_status,
            last_error = excluded.last_error",
        rusqlite::params![
            list.id,
            list.name,
            list.url,
            if list.enabled { 1 } else { 0 },
            raw_content.unwrap_or(""),
            list.rule_count as i64,
            list.etag,
            list.last_modified,
            list.sha256,
            list.last_attempt_timestamp,
            list.last_success_timestamp,
            list.failure_count,
            list.last_status,
            list.last_error,
            raw_content.is_some(),
        ],
    )?;
    Ok(())
}

fn upsert_site_exception(
    conn: &Connection,
    key: &str,
    created_at: i64,
) -> Result<(), StorageError> {
    conn.execute(
        "INSERT INTO content_blocker_exceptions (key, created_at) VALUES (?1, ?2)
         ON CONFLICT(key) DO UPDATE SET created_at = excluded.created_at",
        rusqlite::params![key, created_at],
    )?;
    Ok(())
}

fn upsert_content_blocker_state(
    conn: &Connection,
    mode: maho_types::content_blocking::ContentBlockingMode,
    generation: u64,
    engine_cache: Option<&[u8]>,
    engine_version: Option<&str>,
    engine_hash: Option<&str>,
) -> Result<(), StorageError> {
    let mode_str = match mode {
        maho_types::content_blocking::ContentBlockingMode::Native => "native",
        maho_types::content_blocking::ContentBlockingMode::Extension => "extension",
        maho_types::content_blocking::ContentBlockingMode::Disabled => "disabled",
        maho_types::content_blocking::ContentBlockingMode::Unknown => "unknown",
    };
    let now = chrono::Utc::now().timestamp();
    conn.execute(
        "INSERT INTO content_blocker_state (id, mode, generation, engine_cache, engine_version, engine_hash, updated_at)
         VALUES (1, ?1, ?2, ?3, ?4, ?5, ?6)
         ON CONFLICT(id) DO UPDATE SET
            mode = excluded.mode,
            generation = excluded.generation,
            engine_cache = excluded.engine_cache,
            engine_version = excluded.engine_version,
            engine_hash = excluded.engine_hash,
            updated_at = excluded.updated_at",
        rusqlite::params![mode_str, generation as i64, engine_cache, engine_version, engine_hash, now],
    )?;
    Ok(())
}

#[cfg(test)]
mod content_blocker_load_tests {
    use super::*;

    fn store() -> SqliteStorage {
        SqliteStorage::open_in_memory_with_key("l2-load-test-key").unwrap()
    }

    #[test]
    fn load_state_maps_unrecognized_mode_string_to_unknown() {
        let db = store();
        db.conn
            .execute(
                "INSERT INTO content_blocker_state (id, mode, generation, updated_at) VALUES (1, ?1, 3, 0)",
                rusqlite::params!["future_mode_v99"],
            )
            .unwrap();

        let (mode, _, _, _, _) = db.load_content_blocker_state().unwrap().unwrap();
        assert_eq!(
            mode,
            maho_types::content_blocking::ContentBlockingMode::Unknown
        );
        assert!(
            !mode.is_native(),
            "unrecognized mode string must never load as native"
        );
    }

    #[test]
    fn load_state_maps_native_string_to_native() {
        let db = store();
        db.conn
            .execute(
                "INSERT INTO content_blocker_state (id, mode, generation, updated_at) VALUES (1, 'native', 1, 0)",
                [],
            )
            .unwrap();

        let (mode, _, _, _, _) = db.load_content_blocker_state().unwrap().unwrap();
        assert_eq!(
            mode,
            maho_types::content_blocking::ContentBlockingMode::Native
        );
    }
}
