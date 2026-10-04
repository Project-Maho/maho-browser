use rusqlite::{params, Connection};
use serde::Serialize;
use uuid::Uuid;

use crate::error::AppError;

#[derive(Debug, Clone, Serialize)]
pub struct PendingMutation {
    pub id: String,
    pub account_id: String,
    pub email_uid: Option<i64>,
    pub folder_path: Option<String>,
    pub mutation_type: String,
    pub target_folder: Option<String>,
    pub created_at: String,
    pub calendar_event_id: Option<String>,
    pub payload_json: Option<String>,
}

pub fn queue_mutation(
    conn: &Connection,
    account_id: &str,
    email_uid: i64,
    folder_path: &str,
    mutation_type: &str,
    target_folder: Option<&str>,
) -> Result<(), AppError> {
    let id = Uuid::new_v4().to_string();
    let epoch: i64 = conn.query_row(
        "SELECT COALESCE((SELECT uid_validity FROM folders WHERE account_id=?1 AND path=?2), 0)",
        params![account_id, folder_path], |row| row.get(0),
    )?;
    let payload = serde_json::json!({"uid_validity": epoch}).to_string();
    conn.execute(
        "INSERT OR REPLACE INTO pending_mutations (id, account_id, email_uid, folder_path, mutation_type, target_folder, payload_json)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
        params![id, account_id, email_uid, folder_path, mutation_type, target_folder, payload],
    )?;
    Ok(())
}

/// Pending remote intent takes precedence over a fetched provider snapshot.
pub fn has_pending_mail_mutation(conn: &Connection, account: &str, folder: &str, uid: u32) -> Result<bool, AppError> {
    Ok(conn.query_row(
        "SELECT EXISTS(SELECT 1 FROM pending_mutations WHERE account_id=?1 AND folder_path=?2 AND email_uid=?3 AND calendar_event_id IS NULL)",
        params![account, folder, uid], |row| row.get(0),
    )?)
}

pub fn queue_calendar_mutation(
    conn: &Connection,
    account_id: &str,
    mutation_type: &str,
    event_id: &str,
    payload_json: Option<&str>,
) -> Result<(), AppError> {
    if mutation_type == "calendar_update" {
        let existing_create: Option<(String, Option<String>)> = conn.query_row(
            "SELECT id, payload_json FROM pending_mutations 
             WHERE account_id = ?1 AND calendar_event_id = ?2 AND mutation_type = 'calendar_create'",
            params![account_id, event_id],
            |row| Ok((row.get(0)?, row.get::<_, Option<String>>(1)?)),
        ).ok();

        if let Some((row_id, create_payload_opt)) = existing_create {
            if let Some(update_payload_str) = payload_json {
                if let Ok(mut create_val) = serde_json::from_str::<serde_json::Value>(
                    create_payload_opt.as_deref().unwrap_or("{}"),
                ) {
                    if let Ok(update_val) =
                        serde_json::from_str::<serde_json::Value>(update_payload_str)
                    {
                        if let (Some(c_obj), Some(u_obj)) =
                            (create_val.as_object_mut(), update_val.as_object())
                        {
                            for (k, v) in u_obj {
                                c_obj.insert(k.clone(), v.clone());
                            }
                            let merged_payload =
                                serde_json::to_string(&create_val).unwrap_or_default();
                            conn.execute(
                                "UPDATE pending_mutations SET payload_json = ?1 WHERE id = ?2",
                                params![merged_payload, row_id],
                            )?;
                            return Ok(());
                        }
                    }
                }
            }
        }

        let existing_update: Option<(String, Option<String>)> = conn.query_row(
            "SELECT id, payload_json FROM pending_mutations 
             WHERE account_id = ?1 AND calendar_event_id = ?2 AND mutation_type = 'calendar_update'",
            params![account_id, event_id],
            |row| Ok((row.get(0)?, row.get::<_, Option<String>>(1)?)),
        ).ok();

        if let Some((row_id, update_payload_opt)) = existing_update {
            if let Some(update_payload_str) = payload_json {
                if let Ok(mut existing_val) = serde_json::from_str::<serde_json::Value>(
                    update_payload_opt.as_deref().unwrap_or("{}"),
                ) {
                    if let Ok(update_val) =
                        serde_json::from_str::<serde_json::Value>(update_payload_str)
                    {
                        if let (Some(e_obj), Some(u_obj)) =
                            (existing_val.as_object_mut(), update_val.as_object())
                        {
                            for (k, v) in u_obj {
                                e_obj.insert(k.clone(), v.clone());
                            }
                            let merged_payload =
                                serde_json::to_string(&existing_val).unwrap_or_default();
                            conn.execute(
                                "UPDATE pending_mutations SET payload_json = ?1 WHERE id = ?2",
                                params![merged_payload, row_id],
                            )?;
                            return Ok(());
                        }
                    }
                }
            }
        }
    }

    if mutation_type == "calendar_delete" {
        let existing_create: Option<String> = conn.query_row(
            "SELECT id FROM pending_mutations 
             WHERE account_id = ?1 AND calendar_event_id = ?2 AND mutation_type = 'calendar_create'",
            params![account_id, event_id],
            |row| row.get(0),
        ).ok();

        if existing_create.is_some() {
            conn.execute(
                "DELETE FROM pending_mutations WHERE account_id = ?1 AND calendar_event_id = ?2",
                params![account_id, event_id],
            )?;
            return Ok(());
        }
    }

    conn.execute(
        "DELETE FROM pending_mutations
         WHERE account_id = ?1 AND calendar_event_id = ?2 AND mutation_type = ?3",
        params![account_id, event_id, mutation_type],
    )?;

    let id = Uuid::new_v4().to_string();
    conn.execute(
        "INSERT INTO pending_mutations
            (id, account_id, email_uid, folder_path, mutation_type, target_folder,
             calendar_event_id, payload_json)
         VALUES (?1, ?2, NULL, NULL, ?3, NULL, ?4, ?5)",
        params![id, account_id, mutation_type, event_id, payload_json],
    )?;
    Ok(())
}

pub fn list_pending_mutations(
    conn: &Connection,
    account_id: &str,
) -> Result<Vec<PendingMutation>, AppError> {
    let mut stmt = conn.prepare(
        "SELECT id, account_id, email_uid, folder_path, mutation_type, target_folder, created_at,
                calendar_event_id, payload_json
         FROM pending_mutations WHERE account_id = ?1 ORDER BY created_at ASC, rowid ASC",
    )?;
    let rows = stmt
        .query_map(params![account_id], |row| {
            Ok(PendingMutation {
                id: row.get(0)?,
                account_id: row.get(1)?,
                email_uid: row.get(2).ok(),
                folder_path: row.get(3).ok(),
                mutation_type: row.get(4)?,
                target_folder: row.get(5).ok().flatten(),
                created_at: row.get(6)?,
                calendar_event_id: row.get(7).ok().flatten(),
                payload_json: row.get(8).ok().flatten(),
            })
        })?
        .collect::<Result<Vec<_>, _>>()?;
    Ok(rows)
}

pub fn remove_mutation(conn: &Connection, id: &str) -> Result<(), AppError> {
    conn.execute("DELETE FROM pending_mutations WHERE id = ?1", params![id])?;
    Ok(())
}

pub fn clear_mutations(conn: &Connection, account_id: &str) -> Result<(), AppError> {
    conn.execute(
        "DELETE FROM pending_mutations WHERE account_id = ?1",
        params![account_id],
    )?;
    Ok(())
}

pub fn count_pending_mutations(conn: &Connection) -> Result<i64, AppError> {
    let count: i64 = conn.query_row("SELECT COUNT(*) FROM pending_mutations", [], |row| {
        row.get(0)
    })?;
    Ok(count)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_helpers::setup_test_db;

    fn seed_account(conn: &Connection) {
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
             VALUES ('acc1', 'test@test.com', 'Test', 'imap.test.com', 993, 'Tls', 'smtp.test.com', 587, 'Tls', 'test@test.com')",
            [],
        ).unwrap();
    }

    #[test]
    fn test_queue_and_list_mutations() {
        let conn = setup_test_db();
        seed_account(&conn);

        queue_mutation(&conn, "acc1", 100, "INBOX", "mark_read", None).unwrap();
        queue_mutation(&conn, "acc1", 101, "INBOX", "star", None).unwrap();

        let mutations = list_pending_mutations(&conn, "acc1").unwrap();
        assert_eq!(mutations.len(), 2);
        assert_eq!(mutations[0].email_uid, Some(100));
        assert_eq!(mutations[0].mutation_type, "mark_read");
        assert_eq!(mutations[1].mutation_type, "star");
    }

    #[test]
    fn test_queue_mutation_deduplicates() {
        let conn = setup_test_db();
        seed_account(&conn);

        queue_mutation(&conn, "acc1", 100, "INBOX", "star", None).unwrap();
        queue_mutation(&conn, "acc1", 100, "INBOX", "star", None).unwrap();
        queue_mutation(&conn, "acc1", 100, "INBOX", "star", None).unwrap();

        let mutations = list_pending_mutations(&conn, "acc1").unwrap();
        assert_eq!(mutations.len(), 1);
    }

    #[test]
    fn test_remove_mutation() {
        let conn = setup_test_db();
        seed_account(&conn);

        queue_mutation(&conn, "acc1", 100, "INBOX", "mark_read", None).unwrap();
        let mutations = list_pending_mutations(&conn, "acc1").unwrap();
        assert_eq!(mutations.len(), 1);

        remove_mutation(&conn, &mutations[0].id).unwrap();
        let after = list_pending_mutations(&conn, "acc1").unwrap();
        assert_eq!(after.len(), 0);
    }

    #[test]
    fn test_clear_mutations() {
        let conn = setup_test_db();
        seed_account(&conn);

        queue_mutation(&conn, "acc1", 100, "INBOX", "mark_read", None).unwrap();
        queue_mutation(&conn, "acc1", 101, "INBOX", "star", None).unwrap();

        clear_mutations(&conn, "acc1").unwrap();
        let mutations = list_pending_mutations(&conn, "acc1").unwrap();
        assert_eq!(mutations.len(), 0);
    }

    #[test]
    fn test_count_pending_mutations() {
        let conn = setup_test_db();
        seed_account(&conn);

        assert_eq!(count_pending_mutations(&conn).unwrap(), 0);

        queue_mutation(&conn, "acc1", 100, "INBOX", "mark_read", None).unwrap();
        queue_mutation(&conn, "acc1", 101, "INBOX", "star", None).unwrap();

        assert_eq!(count_pending_mutations(&conn).unwrap(), 2);
    }

    #[test]
    fn test_queue_mutation_with_target_folder() {
        let conn = setup_test_db();
        seed_account(&conn);

        queue_mutation(&conn, "acc1", 100, "INBOX", "move", Some("Archive")).unwrap();
        let mutations = list_pending_mutations(&conn, "acc1").unwrap();
        assert_eq!(mutations.len(), 1);
        assert_eq!(mutations[0].target_folder.as_deref(), Some("Archive"));
    }
}
