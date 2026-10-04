//! Durable storage for routine execution results (the "routine inbox").
//!
//! Built-in and user-defined routines run via the agent backend. Historically
//! their results were logged and discarded; this module persists every run so
//! that a scheduled/manual/event run can be listed, inspected, and surfaced in
//! a routine inbox. Failed runs are recorded too, with the error text stored in
//! `content` and `success = false`.
//!
//! The table is created in `SqliteStorage::run_routines_migrations`
//! (see `sqlite.rs`). Follows the same typed-CRUD-on-`SqliteStorage`
//! convention as `routine_store.rs`.

use crate::error::StorageError;
use crate::sqlite::SqliteStorage;
use rusqlite::params;
use serde::{Deserialize, Serialize};

/// One persisted routine execution.
///
/// `success = false` records a failed run whose `content` is the error text.
/// `source` describes how the run was triggered: `"scheduled"`, `"manual"`, or
/// `"event"`.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RoutineRunRecord {
    pub result_id: i64,
    pub routine_id: String,
    pub ran_at: i64,
    pub success: bool,
    pub content: String,
    pub source: String,
}

impl SqliteStorage {
    /// Record one routine execution. Returns the auto-assigned `result_id`.
    pub fn record_routine_result(
        &self,
        routine_id: &str,
        ran_at: i64,
        success: bool,
        content: &str,
        source: &str,
    ) -> Result<i64, StorageError> {
        self.conn.execute(
            "INSERT INTO routine_results (routine_id, ran_at, success, content, source)
             VALUES (?1, ?2, ?3, ?4, ?5)",
            params![routine_id, ran_at, success as i32, content, source],
        )?;
        Ok(self.conn.last_insert_rowid())
    }

    /// List the most recent routine results across all routines, newest first.
    /// `limit` caps the number of rows returned.
    pub fn list_routine_results(&self, limit: u32) -> Result<Vec<RoutineRunRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT result_id, routine_id, ran_at, success, content, source
             FROM routine_results
             ORDER BY ran_at DESC, result_id DESC
             LIMIT ?1",
        )?;
        let rows = stmt.query_map(params![limit], row_to_record)?;
        collect(rows)
    }

    /// List the most recent results for a single routine, newest first.
    pub fn list_routine_results_for(
        &self,
        routine_id: &str,
        limit: u32,
    ) -> Result<Vec<RoutineRunRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT result_id, routine_id, ran_at, success, content, source
             FROM routine_results
             WHERE routine_id = ?1
             ORDER BY ran_at DESC, result_id DESC
             LIMIT ?2",
        )?;
        let rows = stmt.query_map(params![routine_id, limit], row_to_record)?;
        collect(rows)
    }

    /// Fetch the single most recent result for a routine, or `None` if it has
    /// never run.
    pub fn latest_routine_result(
        &self,
        routine_id: &str,
    ) -> Result<Option<RoutineRunRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT result_id, routine_id, ran_at, success, content, source
             FROM routine_results
             WHERE routine_id = ?1
             ORDER BY ran_at DESC, result_id DESC
             LIMIT 1",
        )?;
        let mut rows = stmt.query(params![routine_id])?;
        match rows.next()? {
            Some(row) => Ok(Some(row_to_record(row)?)),
            None => Ok(None),
        }
    }
}

fn row_to_record(row: &rusqlite::Row<'_>) -> rusqlite::Result<RoutineRunRecord> {
    Ok(RoutineRunRecord {
        result_id: row.get(0)?,
        routine_id: row.get(1)?,
        ran_at: row.get(2)?,
        success: row.get::<_, i32>(3)? != 0,
        content: row.get(4)?,
        source: row.get(5)?,
    })
}

fn collect(
    rows: impl Iterator<Item = rusqlite::Result<RoutineRunRecord>>,
) -> Result<Vec<RoutineRunRecord>, StorageError> {
    let mut out = Vec::new();
    for r in rows {
        out.push(r?);
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_storage() -> SqliteStorage {
        SqliteStorage::open_in_memory_with_key(&uuid::Uuid::new_v4().to_string()).unwrap()
    }

    #[test]
    fn record_and_latest_roundtrip() {
        let storage = test_storage();
        let id = storage
            .record_routine_result("morning_briefing", 100, true, "briefing content", "manual")
            .unwrap();
        assert!(id > 0);

        let latest = storage
            .latest_routine_result("morning_briefing")
            .unwrap()
            .expect("a result must exist");
        assert_eq!(latest.routine_id, "morning_briefing");
        assert!(latest.success);
        assert_eq!(latest.content, "briefing content");
        assert_eq!(latest.source, "manual");
        assert_eq!(latest.ran_at, 100);
    }

    #[test]
    fn failed_run_persists_with_error_text() {
        let storage = test_storage();
        storage
            .record_routine_result("close_old_tabs", 200, false, "agent crashed", "scheduled")
            .unwrap();

        let latest = storage
            .latest_routine_result("close_old_tabs")
            .unwrap()
            .unwrap();
        assert!(!latest.success, "failed run must persist success = false");
        assert_eq!(latest.content, "agent crashed");
        assert_eq!(latest.source, "scheduled");
    }

    #[test]
    fn list_orders_newest_first_and_filters_by_routine() {
        let storage = test_storage();
        storage
            .record_routine_result("a", 100, true, "old-a", "manual")
            .unwrap();
        storage
            .record_routine_result("b", 150, true, "b-run", "event")
            .unwrap();
        storage
            .record_routine_result("a", 200, true, "new-a", "manual")
            .unwrap();

        let all = storage.list_routine_results(10).unwrap();
        assert_eq!(all.len(), 3);
        assert_eq!(all[0].content, "new-a", "newest first across all routines");
        assert_eq!(all[1].routine_id, "b");
        assert_eq!(all[2].content, "old-a");

        let only_a = storage.list_routine_results_for("a", 10).unwrap();
        assert_eq!(only_a.len(), 2);
        assert_eq!(only_a[0].content, "new-a");
        assert_eq!(only_a[1].content, "old-a");
    }

    #[test]
    fn list_respects_limit() {
        let storage = test_storage();
        for i in 0..5 {
            storage
                .record_routine_result("r", 100 + i, true, "x", "manual")
                .unwrap();
        }
        assert_eq!(storage.list_routine_results(3).unwrap().len(), 3);
        assert_eq!(storage.list_routine_results_for("r", 2).unwrap().len(), 2);
    }

    #[test]
    fn latest_missing_returns_none() {
        let storage = test_storage();
        assert_eq!(storage.latest_routine_result("ghost").unwrap(), None);
    }
}
