//! Storage for user-configurable Routines (P2.3).
//!
//! Built-in routines are hardcoded in `maho-core::routines`. This module
//! persists *user-defined* recipes: a name, a prompt, and either a cron
//! `schedule` OR an event `trigger` (event kind), plus an `enabled` flag.
//!
//! Follows the same typed-CRUD-on-`SqliteStorage` convention as
//! `ai_extensibility_store.rs`. The table is created in
//! `SqliteStorage::run_routines_migrations` (see `sqlite.rs`).

use crate::error::StorageError;
use crate::sqlite::SqliteStorage;
use rusqlite::params;
use serde::{Deserialize, Serialize};

/// A persisted user-defined routine recipe.
///
/// A routine is triggered by EITHER `schedule` (a cron string) OR `trigger`
/// (a serialized event kind, e.g. `"on_startup"` / `"on_many_tabs:20"`).
/// Both being `None` means the routine has no trigger and will never fire
/// automatically (it can still be run on demand by id).
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct CustomRoutine {
    pub id: String,
    pub name: String,
    pub prompt: String,
    /// Cron string when cron-scheduled; `None` for event-triggered routines.
    pub schedule: Option<String>,
    /// Serialized event trigger kind when event-triggered; `None` for cron.
    pub trigger: Option<String>,
    pub enabled: bool,
    pub created_at: String,
}

impl SqliteStorage {
    /// Insert (or replace) a user-defined routine.
    pub fn create_custom_routine(&self, routine: &CustomRoutine) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO custom_routines
                 (id, name, prompt, schedule, event_trigger, enabled, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            params![
                routine.id,
                routine.name,
                routine.prompt,
                routine.schedule,
                routine.trigger,
                routine.enabled as i32,
                routine.created_at,
            ],
        )?;
        Ok(())
    }

    /// Fetch a single user-defined routine by id.
    pub fn get_custom_routine(&self, id: &str) -> Result<Option<CustomRoutine>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, prompt, schedule, event_trigger, enabled, created_at
             FROM custom_routines WHERE id = ?1",
        )?;
        let mut rows = stmt.query(params![id])?;
        if let Some(row) = rows.next()? {
            Ok(Some(CustomRoutine {
                id: row.get(0)?,
                name: row.get(1)?,
                prompt: row.get(2)?,
                schedule: row.get(3)?,
                trigger: row.get(4)?,
                enabled: row.get::<_, i32>(5)? != 0,
                created_at: row.get(6)?,
            }))
        } else {
            Ok(None)
        }
    }

    /// List all user-defined routines, newest first.
    pub fn list_custom_routines(&self) -> Result<Vec<CustomRoutine>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, prompt, schedule, event_trigger, enabled, created_at
             FROM custom_routines ORDER BY created_at DESC",
        )?;
        let rows = stmt.query_map(params![], |row| {
            Ok(CustomRoutine {
                id: row.get(0)?,
                name: row.get(1)?,
                prompt: row.get(2)?,
                schedule: row.get(3)?,
                trigger: row.get(4)?,
                enabled: row.get::<_, i32>(5)? != 0,
                created_at: row.get(6)?,
            })
        })?;
        let mut res = Vec::new();
        for r in rows {
            res.push(r?);
        }
        Ok(res)
    }

    /// Delete a user-defined routine by id. Returns `true` if a row was removed.
    pub fn delete_custom_routine(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM custom_routines WHERE id = ?1", params![id])?;
        Ok(affected > 0)
    }

    /// Enable or disable a user-defined routine. Returns `true` if a row changed.
    pub fn set_custom_routine_enabled(
        &self,
        id: &str,
        enabled: bool,
    ) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "UPDATE custom_routines SET enabled = ?2 WHERE id = ?1",
            params![id, enabled as i32],
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

    fn test_storage() -> SqliteStorage {
        SqliteStorage::open_in_memory_with_key(&test_key()).unwrap()
    }

    fn sample(id: &str) -> CustomRoutine {
        CustomRoutine {
            id: id.to_string(),
            name: "My Recipe".to_string(),
            prompt: "Summarize my open tabs.".to_string(),
            schedule: Some("0 9 * * *".to_string()),
            trigger: None,
            enabled: true,
            created_at: "2026-07-13T00:00:00Z".to_string(),
        }
    }

    #[test]
    fn create_get_roundtrip() {
        let storage = test_storage();
        let r = sample("r1");
        storage.create_custom_routine(&r).unwrap();
        let got = storage.get_custom_routine("r1").unwrap();
        assert_eq!(got, Some(r));
    }

    #[test]
    fn get_missing_returns_none() {
        let storage = test_storage();
        assert_eq!(storage.get_custom_routine("nope").unwrap(), None);
    }

    #[test]
    fn list_orders_and_includes_event_trigger() {
        let storage = test_storage();
        let mut cron = sample("cron1");
        cron.created_at = "2026-07-13T00:00:00Z".to_string();
        let mut evt = sample("evt1");
        evt.schedule = None;
        evt.trigger = Some("on_many_tabs:20".to_string());
        evt.created_at = "2026-07-14T00:00:00Z".to_string();

        storage.create_custom_routine(&cron).unwrap();
        storage.create_custom_routine(&evt).unwrap();

        let all = storage.list_custom_routines().unwrap();
        assert_eq!(all.len(), 2);
        // newest (evt, 07-14) first
        assert_eq!(all[0].id, "evt1");
        assert_eq!(all[0].trigger.as_deref(), Some("on_many_tabs:20"));
        assert_eq!(all[0].schedule, None);
        assert_eq!(all[1].id, "cron1");
    }

    #[test]
    fn delete_removes_row() {
        let storage = test_storage();
        storage.create_custom_routine(&sample("r1")).unwrap();
        assert!(storage.delete_custom_routine("r1").unwrap());
        assert!(!storage.delete_custom_routine("r1").unwrap());
        assert_eq!(storage.get_custom_routine("r1").unwrap(), None);
    }

    #[test]
    fn set_enabled_toggles() {
        let storage = test_storage();
        storage.create_custom_routine(&sample("r1")).unwrap();
        assert!(storage.set_custom_routine_enabled("r1", false).unwrap());
        assert!(!storage.get_custom_routine("r1").unwrap().unwrap().enabled);
        assert!(!storage
            .set_custom_routine_enabled("missing", false)
            .unwrap());
    }

    #[test]
    fn migration_is_idempotent_across_reopen() {
        // open_in_memory can't persist across connections, so use a temp file.
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("routines.sqlite");
        let path_str = path.to_str().unwrap();
        let key = test_key();
        {
            let s = SqliteStorage::open_with_key(path_str, &key).unwrap();
            s.create_custom_routine(&sample("persist")).unwrap();
        }
        // Reopen: run_migrations runs again; CREATE TABLE IF NOT EXISTS must be a no-op.
        let s2 = SqliteStorage::open_with_key(path_str, &key).unwrap();
        assert!(s2.get_custom_routine("persist").unwrap().is_some());
    }
}
