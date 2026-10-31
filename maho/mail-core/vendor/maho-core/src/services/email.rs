use crate::error::AppError;
use crate::models::attachment::Attachment;
use crate::models::email::{Email, EmailSummary};
use rusqlite::params;

/// Interpret existing envelope dates and local database timestamps as UTC instants.
/// Unknown dates have no instant; date-filtered searches exclude them.
pub(super) fn email_date_instant(value: &str) -> Option<chrono::DateTime<chrono::Utc>> {
    if let Ok(date) = chrono::DateTime::parse_from_rfc3339(value)
        .or_else(|_| chrono::DateTime::parse_from_rfc2822(value))
    {
        return Some(date.with_timezone(&chrono::Utc));
    }
    for format in ["%Y-%m-%d %H:%M:%S%.f", "%Y-%m-%dT%H:%M:%S%.f"] {
        if let Ok(date) = chrono::NaiveDateTime::parse_from_str(value, format) {
            return Some(date.and_utc());
        }
    }
    chrono::NaiveDate::parse_from_str(value, "%Y-%m-%d")
        .ok()?
        .and_hms_opt(0, 0, 0)
        .map(|date| date.and_utc())
}

pub fn row_to_email_summary(row: &rusqlite::Row) -> Result<EmailSummary, rusqlite::Error> {
    Ok(EmailSummary {
        id: row.get("id")?,
        account_id: row.get("account_id")?,
        folder_id: row.get("folder_id")?,
        uid: row.get("uid")?,
        message_id: row
            .get::<_, Option<String>>("message_id")?
            .unwrap_or_default(),
        subject: row.get("subject")?,
        from_address: row.get("from_address")?,
        from_name: row.get("from_name")?,
        date: row.get("date")?,
        snippet: row.get("snippet")?,
        is_read: row.get("is_read")?,
        is_starred: row.get("is_starred")?,
        is_draft: row.get("is_draft")?,
        has_attachments: row.get("has_attachments")?,
    })
}

pub fn list_emails(
    conn: &rusqlite::Connection,
    account_id: &str,
    folder_id: &str,
    limit: i64,
    offset: i64,
) -> Result<Vec<EmailSummary>, AppError> {
    let mut stmt = conn.prepare(
        "SELECT id, account_id, folder_id, uid, message_id, subject, from_address, from_name,
                date, snippet, is_read, is_starred, is_draft, has_attachments
         FROM emails
         WHERE account_id = ?1 AND folder_id = ?2
           AND (snoozed_until IS NULL OR julianday(snoozed_until) <= julianday('now'))
         ORDER BY uid DESC
         LIMIT ?3 OFFSET ?4",
    )?;
    let emails = stmt
        .query_map(params![account_id, folder_id, limit, offset], |row| {
            row_to_email_summary(row)
        })?
        .collect::<Result<Vec<_>, _>>()?;
    Ok(emails)
}

pub fn list_thread_emails(
    conn: &rusqlite::Connection,
    account_id: &str,
    message_id: &str,
) -> Result<Vec<EmailSummary>, AppError> {
    let mut stmt = conn.prepare(
        "WITH RECURSIVE thread(message_id) AS (
             SELECT message_id
             FROM emails
             WHERE account_id = ?1 AND message_id = ?2

             UNION

             SELECT e.in_reply_to
             FROM emails e
             INNER JOIN thread t ON e.message_id = t.message_id
             WHERE e.account_id = ?1 AND e.in_reply_to IS NOT NULL

             UNION

             SELECT e.message_id
             FROM emails e
             INNER JOIN thread t ON e.in_reply_to = t.message_id
             WHERE e.account_id = ?1
         )
          SELECT DISTINCT id, account_id, folder_id, message_id, subject, from_address, from_name,
                 uid, date, snippet, is_read, is_starred, is_draft, has_attachments
          FROM emails
          WHERE account_id = ?1
            AND (
             message_id IN (SELECT message_id FROM thread)
             OR in_reply_to IN (SELECT message_id FROM thread)
           )",
    )?;
    let mut emails = stmt
        .query_map(params![account_id, message_id], row_to_email_summary)?
        .collect::<Result<Vec<_>, _>>()?;
    emails.sort_by_cached_key(|email| (email_date_instant(&email.date), email.id.clone()));
    collapse_folder_copies(conn, emails)
}

/// Gmail exposes one message under every label folder (INBOX, All Mail,
/// Important, ...), so the local store holds one row per folder copy. Views
/// that span folders must show each message once; keep the copy from the
/// most specific folder so row actions target the folder the user expects.
pub(crate) fn collapse_folder_copies(
    conn: &rusqlite::Connection,
    emails: Vec<EmailSummary>,
) -> Result<Vec<EmailSummary>, AppError> {
    let mut folder_rank = std::collections::HashMap::new();
    let mut stmt = conn.prepare("SELECT id, folder_type FROM folders")?;
    let rows = stmt.query_map([], |row| {
        Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?))
    })?;
    for row in rows {
        let (id, folder_type) = row?;
        let rank = match folder_type.as_str() {
            "inbox" => 0,
            "archive" => 2,
            "spam" | "trash" => 3,
            _ => 1,
        };
        folder_rank.insert(id, rank);
    }
    let rank_of = |email: &EmailSummary| folder_rank.get(&email.folder_id).copied().unwrap_or(1);

    let mut seen: std::collections::HashMap<(String, String), usize> =
        std::collections::HashMap::new();
    let mut collapsed: Vec<EmailSummary> = Vec::with_capacity(emails.len());
    for email in emails {
        if email.message_id.is_empty() {
            collapsed.push(email);
            continue;
        }
        let key = (email.account_id.clone(), email.message_id.clone());
        match seen.get(&key) {
            Some(&index) => {
                if rank_of(&email) < rank_of(&collapsed[index]) {
                    collapsed[index] = email;
                }
            }
            None => {
                seen.insert(key, collapsed.len());
                collapsed.push(email);
            }
        }
    }
    Ok(collapsed)
}

pub fn get_email_from_db(
    conn: &rusqlite::Connection,
    email_id: &str,
) -> Result<(Email, Vec<Attachment>, bool), AppError> {
    let email = conn
        .query_row(
            "SELECT id, account_id, folder_id, uid, message_id, in_reply_to, subject,
                from_address, from_name, to_addresses, cc_addresses, bcc_addresses,
                date, snippet, is_read, is_starred, is_draft, has_attachments,
                body_text, body_html, raw_size, created_at, body_fetched_at, mdn_requested,
                draft_attachments_json, email_references, read_receipt
         FROM emails WHERE id = ?1",
            [email_id],
            |row| {
                Ok(Email {
                    id: row.get("id")?,
                    account_id: row.get("account_id")?,
                    folder_id: row.get("folder_id")?,
                    uid: row.get("uid")?,
                    message_id: row
                        .get::<_, Option<String>>("message_id")?
                        .unwrap_or_default(),
                    in_reply_to: row.get("in_reply_to")?,
                    subject: row.get("subject")?,
                    from_address: row.get("from_address")?,
                    from_name: row.get("from_name")?,
                    to_addresses: row.get("to_addresses")?,
                    cc_addresses: row.get("cc_addresses")?,
                    bcc_addresses: row.get("bcc_addresses")?,
                    date: row.get("date")?,
                    snippet: row.get("snippet")?,
                    is_read: row.get("is_read")?,
                    is_starred: row.get("is_starred")?,
                    is_draft: row.get("is_draft")?,
                    has_attachments: row.get("has_attachments")?,
                    body_text: row.get("body_text")?,
                    body_html: row.get("body_html")?,
                    raw_size: row.get("raw_size")?,
                    created_at: row.get("created_at")?,
                    body_fetched_at: row.get("body_fetched_at")?,
                    draft_attachments_json: row.get("draft_attachments_json")?,
                    email_references: row.get("email_references")?,
                    read_receipt: row.get("read_receipt")?,
                    mdn_requested: row.get("mdn_requested")?,
                })
            },
        )
        .map_err(|_| AppError::NotFound(format!("Email {email_id} not found")))?;

    let mut stmt = conn.prepare(
        "SELECT id, email_id, part_id, filename, mime_type, size, content_id
          FROM attachments WHERE email_id = ?1",
    )?;
    let attachments = stmt
        .query_map([email_id], |row| {
            Ok(Attachment {
                id: row.get("id")?,
                email_id: row.get("email_id")?,
                part_id: row.get("part_id")?,
                filename: row.get("filename")?,
                mime_type: row.get("mime_type")?,
                size: row.get("size")?,
                content_id: row.get("content_id")?,
            })
        })?
        .collect::<Result<Vec<_>, _>>()?;

    // Use explicit body_fetched_at state instead of checking body content
    let needs_fetch = email.body_fetched_at.is_none();
    Ok((email, attachments, needs_fetch))
}

pub fn update_email_body(
    conn: &rusqlite::Connection,
    email_id: &str,
    body_text: Option<String>,
    body_html: Option<String>,
) -> Result<(), AppError> {
    let now = chrono::Utc::now().format("%Y-%m-%d %H:%M:%S").to_string();
    conn.execute(
        "UPDATE emails SET body_text = ?1, body_html = ?2, body_fetched_at = ?3 WHERE id = ?4",
        params![body_text, body_html, now, email_id],
    )?;
    Ok(())
}

pub fn update_email_body_with_mdn(
    conn: &rusqlite::Connection,
    email_id: &str,
    body_text: Option<String>,
    body_html: Option<String>,
    mdn_requested: Option<String>,
) -> Result<(), AppError> {
    let now = chrono::Utc::now().format("%Y-%m-%d %H:%M:%S").to_string();
    conn.execute(
        "UPDATE emails SET body_text = ?1, body_html = ?2, body_fetched_at = ?3, mdn_requested = ?4 WHERE id = ?5",
        params![body_text, body_html, now, mdn_requested, email_id],
    )?;
    Ok(())
}

pub struct EmailImapInfo {
    pub uid: i64,
    pub account_id: String,
    pub folder_path: String,
    pub is_read: bool,
    pub is_starred: bool,
}

pub fn get_email_imap_info(
    conn: &rusqlite::Connection,
    email_id: &str,
) -> Result<EmailImapInfo, AppError> {
    conn.query_row(
        "SELECT e.uid, e.account_id, f.path, e.is_read, e.is_starred
         FROM emails e JOIN folders f ON e.folder_id = f.id
         WHERE e.id = ?1",
        [email_id],
        |row| {
            Ok(EmailImapInfo {
                uid: row.get("uid")?,
                account_id: row.get("account_id")?,
                folder_path: row.get("path")?,
                is_read: row.get("is_read")?,
                is_starred: row.get("is_starred")?,
            })
        },
    )
    .map_err(|_| AppError::NotFound(format!("Email {email_id} not found")))
}

pub fn mark_read(conn: &rusqlite::Connection, email_id: &str) -> Result<(), AppError> {
    let changed = conn.execute("UPDATE emails SET is_read = 1 WHERE id = ?1", [email_id])?;
    if changed == 0 {
        return Err(AppError::NotFound(format!("Email {email_id} not found")));
    }
    super::folder::update_folder_unread_count(conn, email_id)?;
    conn.execute("UPDATE folders SET reconciliation_version=reconciliation_version+1
        WHERE id=(SELECT folder_id FROM emails WHERE id=?1)", [email_id])?;
    Ok(())
}

pub fn mark_unread(conn: &rusqlite::Connection, email_id: &str) -> Result<(), AppError> {
    let changed = conn.execute("UPDATE emails SET is_read = 0 WHERE id = ?1", [email_id])?;
    if changed == 0 {
        return Err(AppError::NotFound(format!("Email {email_id} not found")));
    }
    super::folder::update_folder_unread_count(conn, email_id)?;
    conn.execute("UPDATE folders SET reconciliation_version=reconciliation_version+1
        WHERE id=(SELECT folder_id FROM emails WHERE id=?1)", [email_id])?;
    Ok(())
}

pub fn toggle_star(conn: &rusqlite::Connection, email_id: &str) -> Result<(), AppError> {
    let changed = conn.execute(
        "UPDATE emails SET is_starred = CASE WHEN is_starred = 1 THEN 0 ELSE 1 END WHERE id = ?1",
        [email_id],
    )?;
    if changed == 0 {
        return Err(AppError::NotFound(format!("Email {email_id} not found")));
    }
    conn.execute("UPDATE folders SET reconciliation_version=reconciliation_version+1
        WHERE id=(SELECT folder_id FROM emails WHERE id=?1)", [email_id])?;
    Ok(())
}

pub fn delete_email(conn: &rusqlite::Connection, email_id: &str) -> Result<(), AppError> {
    let folder_id: String = conn
        .query_row(
            "SELECT folder_id FROM emails WHERE id = ?1",
            [email_id],
            |row| row.get(0),
        )
        .map_err(|_| AppError::NotFound(format!("Email {email_id} not found")))?;

    conn.execute("DELETE FROM emails WHERE id = ?1", [email_id])?;

    conn.execute(
        "UPDATE folders SET
            total_count = (SELECT COUNT(*) FROM emails WHERE folder_id = ?1),
            unread_count = (SELECT COUNT(*) FROM emails WHERE folder_id = ?1 AND is_read = 0)
         WHERE id = ?1",
        [&folder_id],
    )?;

    Ok(())
}

pub fn move_email(
    conn: &rusqlite::Connection,
    email_id: &str,
    target_folder_id: &str,
) -> Result<(), AppError> {
    let old_folder_id: String = conn
        .query_row(
            "SELECT folder_id FROM emails WHERE id = ?1",
            [email_id],
            |row| row.get(0),
        )
        .map_err(|_| AppError::NotFound(format!("Email {email_id} not found")))?;

    let moved = conn.execute(
        "UPDATE emails SET uid = CASE WHEN folder_id = ?1 THEN uid ELSE -rowid END,
             folder_id = ?1 WHERE id = ?2
         AND account_id = (SELECT account_id FROM folders WHERE id = ?1)",
        params![target_folder_id, email_id],
    )?;
    if moved == 0 {
        return Err(AppError::Validation(
            "Target folder must belong to the message account".to_string(),
        ));
    }

    for fid in &[&old_folder_id, &target_folder_id.to_string()] {
        conn.execute(
            "UPDATE folders SET
                total_count = (SELECT COUNT(*) FROM emails WHERE folder_id = ?1),
                unread_count = (SELECT COUNT(*) FROM emails WHERE folder_id = ?1 AND is_read = 0)
             WHERE id = ?1",
            [fid],
        )?;
    }

    Ok(())
}

/// Get emails that need body fetch for a specific account, limited to batch size
pub fn get_emails_needing_body_fetch(
    conn: &rusqlite::Connection,
    account_id: &str,
    limit: i64,
) -> Result<Vec<(String, String, i64)>, AppError> {
    let mut stmt = conn.prepare(
        "SELECT e.id, f.path, e.uid
         FROM emails e
         JOIN folders f ON e.folder_id = f.id AND e.account_id = f.account_id
         WHERE e.account_id = ?1 AND e.body_fetched_at IS NULL
           AND e.uid BETWEEN 1 AND 4294967295 AND e.is_draft = 0
         ORDER BY e.date DESC
         LIMIT ?2",
    )?;

    let emails = stmt
        .query_map(rusqlite::params![account_id, limit], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, i64>(2)?,
            ))
        })?
        .collect::<Result<Vec<_>, _>>()?;

    Ok(emails)
}

/// Count emails that need body fetch for a specific account
pub fn count_emails_needing_body_fetch(
    conn: &rusqlite::Connection,
    account_id: &str,
) -> Result<i64, AppError> {
    let count: i64 = conn.query_row(
        "SELECT COUNT(*) FROM emails WHERE account_id = ?1 AND body_fetched_at IS NULL",
        [account_id],
        |row| row.get(0),
    )?;
    Ok(count)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_helpers::{seed_test_data, setup_test_db};

    #[test]
    fn test_list_emails() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let emails = list_emails(&conn, "acc1", "fold1", 10, 0).unwrap();
        assert_eq!(emails.len(), 2);
        assert_eq!(emails[0].id, "em2");
        assert_eq!(emails[1].id, "em1");
    }

    #[test]
    fn test_list_emails_sent_folder() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let emails = list_emails(&conn, "acc1", "fold2", 10, 0).unwrap();
        assert_eq!(emails.len(), 1);
        assert_eq!(emails[0].id, "em3");
    }

    #[test]
    fn test_list_emails_with_offset() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let emails = list_emails(&conn, "acc1", "fold1", 1, 1).unwrap();
        assert_eq!(emails.len(), 1);
        assert_eq!(emails[0].id, "em1");
    }

    #[test]
    fn test_list_emails_orders_rfc2822_dates_by_uid() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        conn.execute(
            "UPDATE emails SET uid = 100, date = 'Fri, 31 Jul 2026 09:00:00 +0900' WHERE id = 'em1'",
            [],
        )
        .unwrap();
        conn.execute(
            "UPDATE emails SET uid = 99, date = 'Thu, 30 Jul 2026 23:00:00 +0900' WHERE id = 'em2'",
            [],
        )
        .unwrap();

        let emails = list_emails(&conn, "acc1", "fold1", 10, 0).unwrap();

        assert_eq!(emails[0].id, "em1");
        assert_eq!(emails[1].id, "em2");
    }

    #[test]
    fn test_get_email_from_db() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let (email, attachments, needs_fetch) = get_email_from_db(&conn, "em1").unwrap();
        assert_eq!(email.id, "em1");
        assert_eq!(email.subject, "Hello World");
        assert_eq!(email.from_address, "sender@example.com");
        assert_eq!(attachments.len(), 1);
        assert_eq!(attachments[0].id, "att1");
        assert_eq!(attachments[0].part_id, "1");
        assert!(!needs_fetch);
    }

    #[test]
    fn test_get_email_not_found() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let result = get_email_from_db(&conn, "nonexistent");
        assert!(matches!(result, Err(AppError::NotFound(_))));
    }

    #[test]
    fn test_list_thread_emails() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let emails = list_thread_emails(&conn, "acc1", "<msg1@example.com>").unwrap();
        assert_eq!(emails.len(), 2);
        assert_eq!(emails[0].id, "em1");
        assert_eq!(emails[1].id, "em2");
    }

    #[test]
    fn test_list_thread_emails_collapses_gmail_label_copies() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type) VALUES ('allmail', 'acc1', 'All Mail', '[Gmail]/All Mail', 'archive')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, date, snippet, is_read, is_starred, is_draft, has_attachments)
             SELECT 'em1-allmail', account_id, 'allmail', 901, message_id, subject, from_address, date, snippet, is_read, is_starred, is_draft, has_attachments FROM emails WHERE id = 'em1'",
            [],
        )
        .unwrap();

        let emails = list_thread_emails(&conn, "acc1", "<msg1@example.com>").unwrap();

        let ids: Vec<&str> = emails.iter().map(|email| email.id.as_str()).collect();
        assert_eq!(ids, vec!["em1", "em2"]);
    }

    #[test]
    fn test_mark_read() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        mark_read(&conn, "em1").unwrap();
        let is_read: bool = conn
            .query_row("SELECT is_read FROM emails WHERE id = ?1", ["em1"], |row| {
                row.get(0)
            })
            .unwrap();
        assert!(is_read);
    }

    #[test]
    fn test_mark_unread() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        mark_unread(&conn, "em2").unwrap();
        let is_read: bool = conn
            .query_row("SELECT is_read FROM emails WHERE id = ?1", ["em2"], |row| {
                row.get(0)
            })
            .unwrap();
        assert!(!is_read);
    }

    #[test]
    fn test_toggle_star() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        toggle_star(&conn, "em1").unwrap();
        let is_starred: bool = conn
            .query_row(
                "SELECT is_starred FROM emails WHERE id = ?1",
                ["em1"],
                |row| row.get(0),
            )
            .unwrap();
        assert!(is_starred);
        toggle_star(&conn, "em1").unwrap();
        let is_starred: bool = conn
            .query_row(
                "SELECT is_starred FROM emails WHERE id = ?1",
                ["em1"],
                |row| row.get(0),
            )
            .unwrap();
        assert!(!is_starred);
    }

    #[test]
    fn test_delete_email() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        delete_email(&conn, "em1").unwrap();
        let result = get_email_from_db(&conn, "em1");
        assert!(matches!(result, Err(AppError::NotFound(_))));
        let total: i64 = conn
            .query_row(
                "SELECT total_count FROM folders WHERE id = 'fold1'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        let unread: i64 = conn
            .query_row(
                "SELECT unread_count FROM folders WHERE id = 'fold1'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(total, 1);
        assert_eq!(unread, 0);
    }

    #[test]
    fn test_move_email() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        move_email(&conn, "em1", "fold2").unwrap();
        let folder_id: String = conn
            .query_row(
                "SELECT folder_id FROM emails WHERE id = ?1",
                ["em1"],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(folder_id, "fold2");
        let fold1_total: i64 = conn
            .query_row(
                "SELECT total_count FROM folders WHERE id = 'fold1'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        let fold2_total: i64 = conn
            .query_row(
                "SELECT total_count FROM folders WHERE id = 'fold2'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(fold1_total, 1);
        assert_eq!(fold2_total, 2);
    }

    #[test]
    fn test_update_email_body() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        update_email_body(
            &conn,
            "em1",
            Some("New body".to_string()),
            Some("<p>New HTML</p>".to_string()),
        )
        .unwrap();
        let (email, _, needs_fetch) = get_email_from_db(&conn, "em1").unwrap();
        assert_eq!(email.body_text.as_deref(), Some("New body"));
        assert_eq!(email.body_html.as_deref(), Some("<p>New HTML</p>"));
        assert!(!needs_fetch);
        assert!(email.body_fetched_at.is_some());
    }

    #[test]
    fn test_get_emails_needing_body_fetch() {
        let conn = setup_test_db();
        seed_test_data(&conn);

        // Insert an email without body_fetched_at for acc1
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, date, snippet, is_read, is_starred, is_draft, has_attachments, body_text, body_html, raw_size, body_fetched_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)",
            rusqlite::params![
                "em4", "acc1", "fold1", 4_i64, "<msg4@example.com>", "No Body Yet",
                "sender@example.com", "2024-01-17T10:00:00Z", "Snippet", false, false, false, false,
                Option::<String>::None, Option::<String>::None, 1024_i64, Option::<String>::None
            ],
        ).unwrap();

        // Insert an email without body_fetched_at for acc2 (different account)
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, date, snippet, is_read, is_starred, is_draft, has_attachments, body_text, body_html, raw_size, body_fetched_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)",
            rusqlite::params![
                "em5", "acc2", "fold3", 5_i64, "<msg5@example.com>", "No Body Yet Acc2",
                "sender@example.com", "2024-01-17T11:00:00Z", "Snippet", false, false, false, false,
                Option::<String>::None, Option::<String>::None, 1024_i64, Option::<String>::None
            ],
        ).unwrap();

        // Should only return emails for acc1, not acc2
        let needing_fetch_acc1 = get_emails_needing_body_fetch(&conn, "acc1", 10).unwrap();
        assert_eq!(needing_fetch_acc1.len(), 1);
        assert_eq!(needing_fetch_acc1[0].0, "em4");
        assert_eq!(needing_fetch_acc1[0].1, "INBOX");
        assert_eq!(needing_fetch_acc1[0].2, 4);

        // Should only return emails for acc2, not acc1
        let needing_fetch_acc2 = get_emails_needing_body_fetch(&conn, "acc2", 10).unwrap();
        assert_eq!(needing_fetch_acc2.len(), 1);
        assert_eq!(needing_fetch_acc2[0].0, "em5");
        assert_eq!(needing_fetch_acc2[0].1, "INBOX");
        assert_eq!(needing_fetch_acc2[0].2, 5);
    }

    #[test]
    fn test_count_emails_needing_body_fetch() {
        let conn = setup_test_db();
        seed_test_data(&conn);

        let count_before = count_emails_needing_body_fetch(&conn, "acc1").unwrap();
        assert_eq!(count_before, 0);

        // Insert an email without body_fetched_at for acc1
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, date, snippet, is_read, is_starred, is_draft, has_attachments, body_text, body_html, raw_size, body_fetched_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)",
            rusqlite::params![
                "em4", "acc1", "fold1", 4_i64, "<msg4@example.com>", "No Body Yet",
                "sender@example.com", "2024-01-17T10:00:00Z", "Snippet", false, false, false, false,
                Option::<String>::None, Option::<String>::None, 1024_i64, Option::<String>::None
            ],
        ).unwrap();

        // Insert an email without body_fetched_at for acc2
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, date, snippet, is_read, is_starred, is_draft, has_attachments, body_text, body_html, raw_size, body_fetched_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)",
            rusqlite::params![
                "em5", "acc2", "fold3", 5_i64, "<msg5@example.com>", "No Body Yet Acc2",
                "sender@example.com", "2024-01-17T11:00:00Z", "Snippet", false, false, false, false,
                Option::<String>::None, Option::<String>::None, 1024_i64, Option::<String>::None
            ],
        ).unwrap();

        // Each account should only see its own pending emails
        let count_acc1 = count_emails_needing_body_fetch(&conn, "acc1").unwrap();
        assert_eq!(count_acc1, 1);

        let count_acc2 = count_emails_needing_body_fetch(&conn, "acc2").unwrap();
        assert_eq!(count_acc2, 1);
    }

    #[test]
    fn test_needs_fetch_based_on_body_fetched_at() {
        let conn = setup_test_db();
        seed_test_data(&conn);

        // Insert an email with body content but no body_fetched_at (simulating old data)
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, date, snippet, is_read, is_starred, is_draft, has_attachments, body_text, body_html, raw_size, body_fetched_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)",
            rusqlite::params![
                "em4", "acc1", "fold1", 4_i64, "<msg4@example.com>", "Body Without Timestamp",
                "sender@example.com", "2024-01-17T10:00:00Z", "Snippet", false, false, false, false,
                "Some body content", Option::<String>::None, 1024_i64, Option::<String>::None
            ],
        ).unwrap();

        // Should need fetch because body_fetched_at is None, even though body_text exists
        let (_, _, needs_fetch) = get_email_from_db(&conn, "em4").unwrap();
        assert!(needs_fetch);

        // After updating body, body_fetched_at should be set
        update_email_body(&conn, "em4", Some("Updated body".to_string()), None).unwrap();
        let (_, _, needs_fetch_after) = get_email_from_db(&conn, "em4").unwrap();
        assert!(!needs_fetch_after);
    }
}
