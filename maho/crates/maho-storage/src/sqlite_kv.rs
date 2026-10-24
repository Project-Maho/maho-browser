//! SQLite-backed implementation of the hot-state key-value API.
//!
//! Used as `LmdbStorage` on iOS, where LMDB is not usable. The API and value
//! bytes match `HeedStorage` exactly so `MahoCore` persistence code is shared.

use std::path::Path;
use std::sync::Mutex;

use rusqlite::{params, Connection, OptionalExtension};

use crate::lmdb::SnapshotEntry;
use crate::StorageError;

/// File created inside the directory passed to [`SqliteKvStorage::open`].
pub const STATE_FILE_NAME: &str = "maho_state.sqlite";

pub struct SqliteKvStorage {
    conn: Mutex<Connection>,
}

impl SqliteKvStorage {
    /// `path` is a directory, like the LMDB environment path.
    pub fn open(path: &Path) -> Result<Self, StorageError> {
        std::fs::create_dir_all(path).map_err(|e| StorageError::Other(e.to_string()))?;
        let conn = Connection::open(path.join(STATE_FILE_NAME))?;
        let _: String = conn.query_row("PRAGMA journal_mode = WAL;", [], |row| row.get(0))?;
        conn.execute_batch(
            "PRAGMA synchronous = NORMAL;
             CREATE TABLE IF NOT EXISTS maho_state (key TEXT PRIMARY KEY NOT NULL, value BLOB NOT NULL);",
        )?;
        Ok(Self { conn: Mutex::new(conn) })
    }

    fn conn(&self) -> Result<std::sync::MutexGuard<'_, Connection>, StorageError> {
        self.conn
            .lock()
            .map_err(|_| StorageError::Other("state store lock poisoned".to_string()))
    }

    pub fn get(&self, key: &str) -> Result<Option<Vec<u8>>, StorageError> {
        Ok(self
            .conn()?
            .query_row("SELECT value FROM maho_state WHERE key = ?1", params![key], |row| row.get(0))
            .optional()?)
    }

    pub fn put(&self, key: &str, value: &[u8]) -> Result<(), StorageError> {
        self.put_batch(&[(key, value)])
    }

    /// All entries commit in one transaction, like an LMDB write txn.
    pub fn put_batch(&self, entries: &[(&str, &[u8])]) -> Result<(), StorageError> {
        let mut conn = self.conn()?;
        let tx = conn.transaction()?;
        {
            let mut stmt = tx.prepare_cached(
                "INSERT INTO maho_state (key, value) VALUES (?1, ?2)
                 ON CONFLICT(key) DO UPDATE SET value = excluded.value",
            )?;
            for (key, value) in entries {
                stmt.execute(params![key, value])?;
            }
        }
        tx.commit()?;
        Ok(())
    }

    pub fn delete(&self, key: &str) -> Result<bool, StorageError> {
        Ok(self.conn()?.execute("DELETE FROM maho_state WHERE key = ?1", params![key])? > 0)
    }

    /// Key-sorted `(key, value)` rows, read-only.
    pub fn snapshot_entries(&self) -> Result<Vec<SnapshotEntry>, StorageError> {
        let conn = self.conn()?;
        let mut stmt = conn.prepare("SELECT key, value FROM maho_state ORDER BY key")?;
        let rows = stmt.query_map([], |row| {
            let key: String = row.get(0)?;
            let value: Vec<u8> = row.get(1)?;
            Ok((key.into_bytes(), value))
        })?;
        rows.collect::<Result<Vec<_>, _>>().map_err(StorageError::from)
    }

    pub fn save_preview(&self, tab_id: &str, image_data: &[u8]) -> Result<(), StorageError> {
        self.put(&format!("preview:{tab_id}"), image_data)
    }

    pub fn load_preview(&self, tab_id: &str) -> Result<Option<Vec<u8>>, StorageError> {
        self.get(&format!("preview:{tab_id}"))
    }

    pub fn delete_preview(&self, tab_id: &str) -> Result<bool, StorageError> {
        self.delete(&format!("preview:{tab_id}"))
    }
}
