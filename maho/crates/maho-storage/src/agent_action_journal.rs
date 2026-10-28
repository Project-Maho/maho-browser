//! Persistent, local-only journal of consequential agent actions.
//!
//! Records form submits, purchases, credential typing, mail send/delete,
//! cross-origin navigation and file uploads performed by an agent (CLI, MCP
//! or the AI panel) so the user can review them after the fact in
//! Settings > Privacy > Agent activity.
//!
//! Invariants:
//! - Rows are never enqueued into `sync_queue`; the journal stays on device.
//! - Rows older than [`AGENT_ACTION_JOURNAL_RETENTION_SECS`] (30 days, matching
//!   the relay sync-log retention) are pruned on every write and on demand.
//! - Callers must not record anything for incognito / off-the-record profiles;
//!   those profiles never reach this store (see the browser-side recorder).
//! - Recording is best-effort and must never block or fail the agent action:
//!   use [`record_agent_action_best_effort`].
//!
//! The table is created in `SqliteStorage::run_agent_action_journal_migrations`.

use crate::error::StorageError;
use crate::sqlite::SqliteStorage;
use rusqlite::params;
use serde::{Deserialize, Serialize};

/// 30 days, aligned with relay sync-log retention.
pub const AGENT_ACTION_JOURNAL_RETENTION_SECS: i64 = 30 * 86_400;

/// Maximum stored summary length (characters).
const MAX_SUMMARY_CHARS: usize = 512;

pub const AGENT_ACTION_ACTORS: &[&str] = &["cli", "mcp", "panel"];

pub const AGENT_ACTION_KINDS: &[&str] = &[
    "form_submit",
    "purchase",
    "credential_typing",
    "mail_send",
    "mail_delete",
    "cross_origin_navigation",
    "file_upload",
];

/// One journal row.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AgentActionJournalEntry {
    pub id: i64,
    /// Unix seconds.
    pub ts: i64,
    pub session_id: String,
    /// `cli` | `mcp` | `panel`.
    pub actor: String,
    /// One of [`AGENT_ACTION_KINDS`].
    pub action: String,
    pub origin: String,
    pub summary: String,
    /// Free-form outcome code, e.g. `completed` / `failed`.
    pub outcome: String,
}

/// Fields supplied by the caller when recording a new action.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NewAgentAction<'a> {
    pub ts: i64,
    pub session_id: &'a str,
    pub actor: &'a str,
    pub action: &'a str,
    pub origin: &'a str,
    pub summary: &'a str,
    pub outcome: &'a str,
}

/// Optional list filter. `None` fields match everything.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct AgentActionJournalFilter {
    pub actor: Option<String>,
    pub action: Option<String>,
    /// Case-insensitive substring match on origin or summary.
    pub query: Option<String>,
}

fn truncate_chars(s: &str, max: usize) -> String {
    s.chars().take(max).collect()
}

impl SqliteStorage {
    /// Insert one journal row and prune rows older than the retention window
    /// relative to `entry.ts`. Returns the new row id.
    pub fn record_agent_action(&self, entry: &NewAgentAction<'_>) -> Result<i64, StorageError> {
        if !AGENT_ACTION_ACTORS.contains(&entry.actor) {
            return Err(StorageError::Other(format!(
                "agent_action_journal: unknown actor {}",
                entry.actor
            )));
        }
        if !AGENT_ACTION_KINDS.contains(&entry.action) {
            return Err(StorageError::Other(format!(
                "agent_action_journal: unknown action {}",
                entry.action
            )));
        }
        self.conn.execute(
            "INSERT INTO agent_action_journal
                 (ts, session_id, actor, action, origin, summary, outcome)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            params![
                entry.ts,
                entry.session_id,
                entry.actor,
                entry.action,
                entry.origin,
                truncate_chars(entry.summary, MAX_SUMMARY_CHARS),
                entry.outcome,
            ],
        )?;
        let id = self.conn.last_insert_rowid();
        self.prune_agent_action_journal(entry.ts)?;
        Ok(id)
    }

    /// List journal rows newest first, optionally filtered.
    pub fn list_agent_actions(
        &self,
        filter: &AgentActionJournalFilter,
        limit: u32,
    ) -> Result<Vec<AgentActionJournalEntry>, StorageError> {
        let query = filter
            .query
            .as_deref()
            .filter(|q| !q.is_empty())
            .map(|q| format!("%{}%", q.to_lowercase()));
        let mut stmt = self.conn.prepare(
            "SELECT id, ts, session_id, actor, action, origin, summary, outcome
             FROM agent_action_journal
             WHERE (?1 IS NULL OR actor = ?1)
               AND (?2 IS NULL OR action = ?2)
               AND (?3 IS NULL OR lower(origin) LIKE ?3 OR lower(summary) LIKE ?3)
             ORDER BY ts DESC, id DESC
             LIMIT ?4",
        )?;
        let rows = stmt.query_map(params![filter.actor, filter.action, query, limit], |row| {
            Ok(AgentActionJournalEntry {
                id: row.get(0)?,
                ts: row.get(1)?,
                session_id: row.get(2)?,
                actor: row.get(3)?,
                action: row.get(4)?,
                origin: row.get(5)?,
                summary: row.get(6)?,
                outcome: row.get(7)?,
            })
        })?;
        let mut out = Vec::new();
        for row in rows {
            out.push(row?);
        }
        Ok(out)
    }

    /// Delete every journal row. Returns the number of rows removed.
    pub fn clear_agent_actions(&self) -> Result<usize, StorageError> {
        Ok(self.conn.execute("DELETE FROM agent_action_journal", [])?)
    }

    /// Delete rows older than the 30-day retention window relative to `now`.
    pub fn prune_agent_action_journal(&self, now: i64) -> Result<usize, StorageError> {
        Ok(self.conn.execute(
            "DELETE FROM agent_action_journal WHERE ts < ?1",
            params![now - AGENT_ACTION_JOURNAL_RETENTION_SECS],
        )?)
    }
}

/// Record an action without ever propagating a failure to the caller. A write
/// failure is logged to stderr and reported as `None`; the agent action that
/// triggered the record must proceed regardless.
pub fn record_agent_action_best_effort(
    storage: &SqliteStorage,
    entry: &NewAgentAction<'_>,
) -> Option<i64> {
    match storage.record_agent_action(entry) {
        Ok(id) => Some(id),
        Err(error) => {
            eprintln!(
                "[maho-storage] agent_action_journal write failed (action continues): {error}"
            );
            None
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_storage() -> SqliteStorage {
        SqliteStorage::open_in_memory_with_key(&uuid::Uuid::new_v4().to_string()).unwrap()
    }

    fn entry<'a>(ts: i64, actor: &'a str, action: &'a str, origin: &'a str) -> NewAgentAction<'a> {
        NewAgentAction {
            ts,
            session_id: "s1",
            actor,
            action,
            origin,
            summary: "summary",
            outcome: "completed",
        }
    }

    #[test]
    fn agent_action_journal_writes_and_lists_three_entries() {
        let storage = test_storage();
        let now = 1_700_000_000;
        storage
            .record_agent_action(&entry(now, "panel", "form_submit", "https://a.example"))
            .unwrap();
        storage
            .record_agent_action(&entry(now + 1, "mcp", "mail_send", "maho://mail"))
            .unwrap();
        storage
            .record_agent_action(&entry(now + 2, "cli", "purchase", "https://shop.example"))
            .unwrap();

        let all = storage
            .list_agent_actions(&AgentActionJournalFilter::default(), 50)
            .unwrap();
        assert_eq!(all.len(), 3);
        assert_eq!(
            all.iter().map(|e| e.action.as_str()).collect::<Vec<_>>(),
            vec!["purchase", "mail_send", "form_submit"],
            "newest first"
        );
        assert_eq!(all[0].actor, "cli");
        assert_eq!(all[0].outcome, "completed");
    }

    #[test]
    fn agent_action_journal_filters_and_clears() {
        let storage = test_storage();
        let now = 1_700_000_000;
        storage
            .record_agent_action(&entry(now, "panel", "form_submit", "https://a.example"))
            .unwrap();
        storage
            .record_agent_action(&entry(now + 1, "mcp", "mail_send", "maho://mail"))
            .unwrap();

        let by_actor = storage
            .list_agent_actions(
                &AgentActionJournalFilter {
                    actor: Some("mcp".into()),
                    ..Default::default()
                },
                50,
            )
            .unwrap();
        assert_eq!(by_actor.len(), 1);
        assert_eq!(by_actor[0].action, "mail_send");

        let by_query = storage
            .list_agent_actions(
                &AgentActionJournalFilter {
                    query: Some("A.EXAMPLE".into()),
                    ..Default::default()
                },
                50,
            )
            .unwrap();
        assert_eq!(by_query.len(), 1);
        assert_eq!(by_query[0].action, "form_submit");

        assert_eq!(storage.clear_agent_actions().unwrap(), 2);
        assert!(storage
            .list_agent_actions(&AgentActionJournalFilter::default(), 50)
            .unwrap()
            .is_empty());
    }

    #[test]
    fn agent_action_journal_prunes_after_30_days() {
        let storage = test_storage();
        let now = 1_700_000_000;
        storage
            .record_agent_action(&entry(
                now - AGENT_ACTION_JOURNAL_RETENTION_SECS - 1,
                "panel",
                "file_upload",
                "https://old.example",
            ))
            .unwrap();
        storage
            .record_agent_action(&entry(now, "panel", "file_upload", "https://new.example"))
            .unwrap();
        let all = storage
            .list_agent_actions(&AgentActionJournalFilter::default(), 50)
            .unwrap();
        assert_eq!(all.len(), 1);
        assert_eq!(all[0].origin, "https://new.example");
    }

    #[test]
    fn agent_action_journal_is_never_synced() {
        let storage = test_storage();
        storage
            .record_agent_action(&entry(1, "panel", "mail_delete", "maho://mail"))
            .unwrap();
        let queued: i64 = storage
            .conn
            .query_row(
                "SELECT COUNT(*) FROM sync_queue WHERE entity_type LIKE '%agent_action%'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(queued, 0);
    }

    #[test]
    fn agent_action_journal_write_failure_is_non_blocking() {
        let storage = test_storage();
        storage
            .conn
            .execute_batch("DROP TABLE agent_action_journal")
            .unwrap();
        // The action that triggered the record must still complete: the
        // best-effort recorder swallows (and logs) the failure.
        let recorded = record_agent_action_best_effort(
            &storage,
            &entry(1, "panel", "purchase", "https://shop.example"),
        );
        assert!(
            recorded.is_none(),
            "write failure must surface as None, not panic/err"
        );
        // Storage stays usable for the rest of the action.
        storage.set_setting("after_journal_failure", "ok").unwrap();
        assert_eq!(
            storage
                .get_setting("after_journal_failure")
                .unwrap()
                .as_deref(),
            Some("ok")
        );
    }

    #[test]
    fn agent_action_journal_created_on_db_already_at_v4() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("v4.db");
        let path = path.to_str().unwrap();
        let key = uuid::Uuid::new_v4().to_string();
        {
            let storage = SqliteStorage::open_with_key(path, &key).unwrap();
            // Simulate a DB migrated by todo 11 (v4) before the journal existed.
            storage
                .conn
                .execute_batch(
                    "DROP TABLE agent_action_journal;
                     PRAGMA user_version = 4;",
                )
                .unwrap();
        }
        let storage = SqliteStorage::open_with_key(path, &key).unwrap();
        storage
            .record_agent_action(&entry(1, "panel", "mail_send", "maho://mail"))
            .unwrap();
        assert_eq!(
            storage
                .list_agent_actions(&AgentActionJournalFilter::default(), 10)
                .unwrap()
                .len(),
            1
        );
    }

    #[test]
    fn agent_action_journal_present_when_already_at_current_version() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("v5.db");
        let path = path.to_str().unwrap();
        let key = uuid::Uuid::new_v4().to_string();
        {
            let storage = SqliteStorage::open_with_key(path, &key).unwrap();
            storage
                .conn
                .execute_batch("DROP TABLE agent_action_journal;")
                .unwrap();
        }
        // user_version is already current, so the early return fires; the
        // journal table must still be recreated.
        let storage = SqliteStorage::open_with_key(path, &key).unwrap();
        assert!(storage
            .list_agent_actions(&AgentActionJournalFilter::default(), 10)
            .is_ok());
    }

    #[test]
    fn agent_action_journal_rejects_unknown_kind() {
        let storage = test_storage();
        assert!(storage
            .record_agent_action(&entry(1, "panel", "browser_scroll", "https://a.example"))
            .is_err());
        assert!(storage
            .record_agent_action(&entry(1, "robot", "purchase", "https://a.example"))
            .is_err());
    }
}
