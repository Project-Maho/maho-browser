use heed::types::{SerdeBincode, Str};
use heed::{Database, Env, EnvOpenOptions};
use std::path::Path;

use crate::StorageError;

pub type SnapshotEntry = (Vec<u8>, Vec<u8>);

/// LMDB storage for hot data (tab state, space state, session data)
pub struct LmdbStorage {
    env: Env,
    db: Database<Str, SerdeBincode<Vec<u8>>>,
}

impl LmdbStorage {
    pub fn open(path: &Path) -> Result<Self, StorageError> {
        std::fs::create_dir_all(path).map_err(|e| StorageError::Other(e.to_string()))?;
        let env = unsafe {
            EnvOpenOptions::new()
                .map_size(100 * 1024 * 1024) // 100MB
                .max_dbs(10)
                .open(path)?
        };
        let mut wtxn = env.write_txn()?;
        let db = env.create_database(&mut wtxn, Some("maho_state"))?;
        wtxn.commit()?;
        Ok(Self { env, db })
    }

    pub fn get(&self, key: &str) -> Result<Option<Vec<u8>>, StorageError> {
        let rtxn = self.env.read_txn()?;
        let value = self.db.get(&rtxn, key)?;
        Ok(value.map(|v| v.to_vec()))
    }

    pub fn put(&self, key: &str, value: &[u8]) -> Result<(), StorageError> {
        let mut wtxn = self.env.write_txn()?;
        self.db.put(&mut wtxn, key, &value.to_vec())?;
        wtxn.commit()?;
        Ok(())
    }

    pub fn put_batch(&self, entries: &[(&str, &[u8])]) -> Result<(), StorageError> {
        let mut wtxn = self.env.write_txn()?;
        for (key, value) in entries {
            self.db.put(&mut wtxn, key, &value.to_vec())?;
        }
        wtxn.commit()?;
        Ok(())
    }

    pub fn delete(&self, key: &str) -> Result<bool, StorageError> {
        let mut wtxn = self.env.write_txn()?;
        let existed = self.db.delete(&mut wtxn, key)?;
        wtxn.commit()?;
        Ok(existed)
    }

    /// Read-only enumeration of `maho_state`: one read transaction, key-sorted
    /// `(key, value)` rows, no write txn / db creation / put / delete / cursor
    /// mutation. Value bytes match [`LmdbStorage::get`]; per-tab
    /// `scroll_position` lives inside the `tabs` JSON value, not a separate key.
    pub fn snapshot_entries(&self) -> Result<Vec<SnapshotEntry>, StorageError> {
        let rtxn = self.env.read_txn()?;
        let mut entries: Vec<SnapshotEntry> = Vec::new();
        for item in self.db.iter(&rtxn)? {
            let (key, value) = item?;
            entries.push((key.as_bytes().to_vec(), value));
        }
        entries.sort_by(|a, b| a.0.cmp(&b.0));
        Ok(entries)
    }

    pub fn save_preview(&self, tab_id: &str, image_data: &[u8]) -> Result<(), StorageError> {
        let key = format!("preview:{tab_id}");
        self.put(&key, image_data)
    }

    pub fn load_preview(&self, tab_id: &str) -> Result<Option<Vec<u8>>, StorageError> {
        let key = format!("preview:{tab_id}");
        self.get(&key)
    }

    pub fn delete_preview(&self, tab_id: &str) -> Result<bool, StorageError> {
        let key = format!("preview:{tab_id}");
        self.delete(&key)
    }
}
