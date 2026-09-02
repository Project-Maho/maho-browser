use crate::error::PlatformError;
#[cfg(not(target_os = "macos"))]
use crate::error::NON_MACOS_REMEDIATION;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(tag = "status", rename_all = "snake_case")]
pub enum FdaStatus {
    Granted,
    Denied { deep_link: String },
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Chat {
    pub guid: String,
    pub identifier: String,
    pub name: Option<String>,
    pub is_group: bool,
    pub participants: Vec<String>,
    pub last_message: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Message {
    pub rowid: i64,
    pub guid: String,
    pub text: Option<String>,
    pub sender: String,
    pub is_from_me: bool,
    pub date_unix: i64,
    pub is_delivered: bool,
    pub error: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct SearchResult {
    pub messages: Vec<Message>,
    pub truncated: bool,
}

pub fn classify_sqlite_error(msg: &str) -> PlatformError {
    let lower = msg.to_lowercase();
    let fda_patterns = [
        "unable to open database file",
        "authorization denied",
        "attempt to write a readonly database",
        "file is not a database",
    ];

    if fda_patterns.iter().any(|pattern| lower.contains(pattern)) {
        PlatformError::PermissionRequired {
            scope: "full-disk-access".to_string(),
            deep_link: crate::error::DEEP_LINK_FDA.to_string(),
        }
    } else {
        PlatformError::Sqlite(msg.to_string())
    }
}

#[cfg(target_os = "macos")]
fn resolve_db_path(db_path: Option<&str>) -> Result<std::path::PathBuf, PlatformError> {
    if let Some(p) = db_path {
        if let Some(rest) = p.strip_prefix("~/") {
            let home = std::env::var("HOME").map_err(|e| PlatformError::Io(e.to_string()))?;
            Ok(std::path::PathBuf::from(home).join(rest))
        } else {
            Ok(std::path::PathBuf::from(p))
        }
    } else {
        let home = std::env::var("HOME").map_err(|e| PlatformError::Io(e.to_string()))?;
        Ok(std::path::PathBuf::from(home).join("Library/Messages/chat.db"))
    }
}

#[cfg(target_os = "macos")]
fn open_connection(db_path: Option<&str>) -> Result<rusqlite::Connection, PlatformError> {
    let path = resolve_db_path(db_path)?;
    rusqlite::Connection::open_with_flags(&path, rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY)
        .map_err(|e| classify_sqlite_error(&e.to_string()))
}

#[cfg(target_os = "macos")]
fn apple_to_unix_timestamp(apple_date: i64) -> i64 {
    const APPLE_EPOCH_OFFSET: i64 = 978_307_200;
    if apple_date.abs() > 10_000_000_000 {
        (apple_date / 1_000_000_000) + APPLE_EPOCH_OFFSET
    } else {
        apple_date + APPLE_EPOCH_OFFSET
    }
}

pub fn chats(db_path: Option<&str>) -> Result<Vec<Chat>, PlatformError> {
    #[cfg(target_os = "macos")]
    {
        let conn = open_connection(db_path)?;
        let mut stmt = conn
            .prepare(
                r#"
                SELECT 
                    c.ROWID,
                    c.guid,
                    c.chat_identifier,
                    c.display_name,
                    (
                        SELECT m.text 
                        FROM message m 
                        JOIN chat_message_join cmj ON cmj.message_id = m.ROWID 
                        WHERE cmj.chat_id = c.ROWID 
                        ORDER BY m.ROWID DESC 
                        LIMIT 1
                    ) AS last_message,
                    (
                        SELECT MAX(cmj.message_id) 
                        FROM chat_message_join cmj 
                        WHERE cmj.chat_id = c.ROWID
                    ) AS max_msg_rowid
                FROM chat c
                ORDER BY max_msg_rowid DESC NULLS LAST, c.ROWID DESC
                LIMIT 100
                "#,
            )
            .map_err(|e| classify_sqlite_error(&e.to_string()))?;

        struct ChatRow {
            rowid: i64,
            guid: String,
            identifier: String,
            display_name: Option<String>,
            last_message: Option<String>,
        }

        let chat_rows = stmt
            .query_map([], |row| {
                let raw_text: Option<String> = row.get(4)?;
                // Note: message.text may be NULL when Apple stores attributedBody.
                // We read the text column only and fall back to an empty string (lossy).
                let last_message = raw_text;
                Ok(ChatRow {
                    rowid: row.get(0)?,
                    guid: row.get(1)?,
                    identifier: row.get(2)?,
                    display_name: row.get(3)?,
                    last_message,
                })
            })
            .map_err(|e| classify_sqlite_error(&e.to_string()))?;

        let mut participant_stmt = conn
            .prepare(
                r#"
                SELECT h.id 
                FROM handle h 
                JOIN chat_handle_join chj ON chj.handle_id = h.ROWID 
                WHERE chj.chat_id = ?1 
                ORDER BY h.ROWID ASC
                "#,
            )
            .map_err(|e| classify_sqlite_error(&e.to_string()))?;

        let mut chats = Vec::new();
        for row_res in chat_rows {
            let cr = row_res.map_err(|e| classify_sqlite_error(&e.to_string()))?;
            let participants: Vec<String> = participant_stmt
                .query_map([cr.rowid], |row| row.get(0))
                .map_err(|e| classify_sqlite_error(&e.to_string()))?
                .filter_map(|r| r.ok())
                .collect();

            let is_group = cr.guid.contains(";+;")
                || cr.identifier.starts_with("chat")
                || participants.len() > 1;
            let name = cr.display_name.filter(|s| !s.is_empty());

            chats.push(Chat {
                guid: cr.guid,
                identifier: cr.identifier,
                name,
                is_group,
                participants,
                last_message: cr.last_message,
            });
        }

        Ok(chats)
    }
    #[cfg(not(target_os = "macos"))]
    {
        let _ = db_path;

        Err(PlatformError::Unsupported {
            platform: std::env::consts::OS.to_string(),
            capability: "imessage".to_string(),
            remediation: NON_MACOS_REMEDIATION.to_string(),
        })
    }
}

pub fn history(
    chat_guid: &str,
    limit: u32,
    db_path: Option<&str>,
) -> Result<Vec<Message>, PlatformError> {
    #[cfg(target_os = "macos")]
    {
        let conn = open_connection(db_path)?;
        let mut stmt = conn
            .prepare(
                r#"
                SELECT 
                    m.ROWID,
                    m.guid,
                    m.text,
                    m.is_from_me,
                    m.date,
                    m.is_delivered,
                    m.error,
                    h.id AS sender
                FROM message m
                JOIN chat_message_join cmj ON cmj.message_id = m.ROWID
                JOIN chat c ON c.ROWID = cmj.chat_id
                LEFT JOIN handle h ON m.handle_id = h.ROWID
                WHERE c.guid = ?1
                ORDER BY m.ROWID DESC
                LIMIT ?2
                "#,
            )
            .map_err(|e| classify_sqlite_error(&e.to_string()))?;

        let rows = stmt
            .query_map(rusqlite::params![chat_guid, limit as i64], |row| {
                let rowid: i64 = row.get(0)?;
                let guid: String = row.get(1)?;
                let raw_text: Option<String> = row.get(2)?;
                // Note: message.text may be NULL when Apple stores attributedBody.
                // We read the text column only and fall back to an empty string (lossy).
                let text = Some(raw_text.unwrap_or_default());
                let is_from_me: i64 = row.get(3)?;
                let is_from_me_bool = is_from_me != 0;
                let date_val: i64 = row.get(4)?;
                let is_delivered_val: i64 = row.get(5).unwrap_or(1);
                let is_delivered_bool = is_delivered_val != 0;
                let error_raw: Option<rusqlite::types::Value> = row.get(6).unwrap_or(None);
                let error = match error_raw {
                    Some(rusqlite::types::Value::Integer(i)) if i != 0 => Some(i.to_string()),
                    Some(rusqlite::types::Value::Text(s)) if !s.is_empty() && s != "0" => Some(s),
                    _ => None,
                };
                let handle_sender: Option<String> = row.get(7).unwrap_or(None);
                let sender = if is_from_me_bool {
                    "me".to_string()
                } else {
                    handle_sender.unwrap_or_default()
                };

                Ok(Message {
                    rowid,
                    guid,
                    text,
                    sender,
                    is_from_me: is_from_me_bool,
                    date_unix: apple_to_unix_timestamp(date_val),
                    is_delivered: is_delivered_bool,
                    error,
                })
            })
            .map_err(|e| classify_sqlite_error(&e.to_string()))?;

        let mut messages = Vec::new();
        for msg_res in rows {
            messages.push(msg_res.map_err(|e| classify_sqlite_error(&e.to_string()))?);
        }

        Ok(messages)
    }
    #[cfg(not(target_os = "macos"))]
    {
        let _ = chat_guid;
        let _ = limit;
        let _ = db_path;

        Err(PlatformError::Unsupported {
            platform: std::env::consts::OS.to_string(),
            capability: "imessage".to_string(),
            remediation: NON_MACOS_REMEDIATION.to_string(),
        })
    }
}

pub fn search(
    query: Option<&str>,
    sender: Option<&str>,
    db_path: Option<&str>,
) -> Result<SearchResult, PlatformError> {
    #[cfg(target_os = "macos")]
    {
        let conn = open_connection(db_path)?;
        let mut sql = String::from(
            r#"
            SELECT 
                m.ROWID,
                m.guid,
                m.text,
                m.is_from_me,
                m.date,
                m.is_delivered,
                m.error,
                h.id AS sender
            FROM message m
            LEFT JOIN handle h ON m.handle_id = h.ROWID
            WHERE 1=1
            "#,
        );

        let mut params: Vec<Box<dyn rusqlite::ToSql>> = Vec::new();

        if let Some(q) = query {
            if !q.is_empty() {
                sql.push_str(" AND m.text LIKE ?");
                params.push(Box::new(format!("%{q}%")));
            }
        }

        if let Some(s) = sender {
            if !s.is_empty() {
                if s.eq_ignore_ascii_case("me") {
                    sql.push_str(" AND m.is_from_me = 1");
                } else {
                    sql.push_str(" AND (m.is_from_me = 0 AND h.id LIKE ?)");
                    params.push(Box::new(format!("%{s}%")));
                }
            }
        }

        sql.push_str(" ORDER BY m.ROWID DESC LIMIT 2001");

        let mut stmt = conn
            .prepare(&sql)
            .map_err(|e| classify_sqlite_error(&e.to_string()))?;

        let params_ref: Vec<&dyn rusqlite::ToSql> = params.iter().map(|p| p.as_ref()).collect();

        let rows = stmt
            .query_map(rusqlite::params_from_iter(params_ref), |row| {
                let rowid: i64 = row.get(0)?;
                let guid: String = row.get(1)?;
                let raw_text: Option<String> = row.get(2)?;
                // Note: message.text may be NULL when Apple stores attributedBody.
                // We read the text column only and fall back to an empty string (lossy).
                let text = Some(raw_text.unwrap_or_default());
                let is_from_me: i64 = row.get(3)?;
                let is_from_me_bool = is_from_me != 0;
                let date_val: i64 = row.get(4)?;
                let is_delivered_val: i64 = row.get(5).unwrap_or(1);
                let is_delivered_bool = is_delivered_val != 0;
                let error_raw: Option<rusqlite::types::Value> = row.get(6).unwrap_or(None);
                let error = match error_raw {
                    Some(rusqlite::types::Value::Integer(i)) if i != 0 => Some(i.to_string()),
                    Some(rusqlite::types::Value::Text(s)) if !s.is_empty() && s != "0" => Some(s),
                    _ => None,
                };
                let handle_sender: Option<String> = row.get(7).unwrap_or(None);
                let sender = if is_from_me_bool {
                    "me".to_string()
                } else {
                    handle_sender.unwrap_or_default()
                };

                Ok(Message {
                    rowid,
                    guid,
                    text,
                    sender,
                    is_from_me: is_from_me_bool,
                    date_unix: apple_to_unix_timestamp(date_val),
                    is_delivered: is_delivered_bool,
                    error,
                })
            })
            .map_err(|e| classify_sqlite_error(&e.to_string()))?;

        let mut messages = Vec::new();
        for msg_res in rows {
            messages.push(msg_res.map_err(|e| classify_sqlite_error(&e.to_string()))?);
        }

        let truncated = messages.len() > 2000;
        if truncated {
            messages.truncate(2000);
        }

        Ok(SearchResult {
            messages,
            truncated,
        })
    }
    #[cfg(not(target_os = "macos"))]
    {
        let _ = query;
        let _ = sender;
        let _ = db_path;

        Err(PlatformError::Unsupported {
            platform: std::env::consts::OS.to_string(),
            capability: "imessage".to_string(),
            remediation: NON_MACOS_REMEDIATION.to_string(),
        })
    }
}

pub fn fda_status(db_path: Option<&str>) -> Result<FdaStatus, PlatformError> {
    #[cfg(target_os = "macos")]
    {
        match open_connection(db_path) {
            Ok(conn) => {
                match conn.query_row("SELECT 1 FROM sqlite_master LIMIT 1;", [], |_| Ok(())) {
                    Ok(_) | Err(rusqlite::Error::QueryReturnedNoRows) => Ok(FdaStatus::Granted),
                    Err(e) => {
                        let err = classify_sqlite_error(&e.to_string());
                        if matches!(err, PlatformError::PermissionRequired { .. }) {
                            Ok(FdaStatus::Denied {
                                deep_link: crate::error::DEEP_LINK_FDA.to_string(),
                            })
                        } else {
                            Err(err)
                        }
                    }
                }
            }
            Err(PlatformError::PermissionRequired { deep_link, .. }) => {
                Ok(FdaStatus::Denied { deep_link })
            }
            Err(err) => Err(err),
        }
    }
    #[cfg(not(target_os = "macos"))]
    {
        let _ = db_path;

        Err(PlatformError::Unsupported {
            platform: std::env::consts::OS.to_string(),
            capability: "imessage".to_string(),
            remediation: NON_MACOS_REMEDIATION.to_string(),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::error::DEEP_LINK_FDA;

    struct TempTestDir(std::path::PathBuf);

    impl TempTestDir {
        fn new() -> Self {
            let unique = format!(
                "maho_imessage_test_{}_{}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_nanos()
            );
            let dir = std::env::temp_dir().join(unique);
            std::fs::create_dir_all(&dir).expect("failed to create test temp dir");
            Self(dir)
        }

        fn path(&self) -> &std::path::Path {
            &self.0
        }
    }

    impl Drop for TempTestDir {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }

    #[test]
    fn test_classify_sqlite_error_table() {
        let cases = vec![
            (
                "unable to open database file: /Users/foo/Library/Messages/chat.db",
                true,
            ),
            ("authorization denied", true),
            ("attempt to write a readonly database", true),
            ("file is not a database", true),
            ("UNABLE TO OPEN DATABASE FILE", true),
            ("syntax error near SELECT", false),
            ("no such table: message", false),
            ("database disk image is malformed", false),
        ];

        for (msg, is_fda) in cases {
            let err = classify_sqlite_error(msg);
            if is_fda {
                match err {
                    PlatformError::PermissionRequired { scope, deep_link } => {
                        assert_eq!(scope, "full-disk-access");
                        assert_eq!(deep_link, DEEP_LINK_FDA);
                    }
                    other => panic!("expected PermissionRequired for '{msg}', got {other:?}"),
                }
            } else {
                match err {
                    PlatformError::Sqlite(s) => {
                        assert_eq!(s, msg);
                    }
                    other => panic!("expected Sqlite for '{msg}', got {other:?}"),
                }
            }
        }
    }

    #[cfg(target_os = "macos")]
    fn setup_fixture_db() -> (TempTestDir, std::path::PathBuf) {
        let temp_dir = TempTestDir::new();
        let db_path = temp_dir.path().join("chat.db");

        let conn = rusqlite::Connection::open(&db_path).expect("failed to create fixture db");

        conn.execute_batch(
            r#"
            CREATE TABLE chat (
                ROWID INTEGER PRIMARY KEY AUTOINCREMENT,
                guid TEXT UNIQUE NOT NULL,
                chat_identifier TEXT NOT NULL,
                display_name TEXT
            );

            CREATE TABLE handle (
                ROWID INTEGER PRIMARY KEY AUTOINCREMENT,
                id TEXT NOT NULL
            );

            CREATE TABLE chat_handle_join (
                chat_id INTEGER,
                handle_id INTEGER
            );

            CREATE TABLE chat_message_join (
                chat_id INTEGER,
                message_id INTEGER
            );

            CREATE TABLE message (
                ROWID INTEGER PRIMARY KEY AUTOINCREMENT,
                guid TEXT UNIQUE NOT NULL,
                text TEXT,
                date INTEGER,
                is_from_me INTEGER DEFAULT 0,
                is_delivered INTEGER DEFAULT 1,
                error INTEGER DEFAULT 0,
                handle_id INTEGER
            );

            -- 3 Chats
            INSERT INTO chat (ROWID, guid, chat_identifier, display_name) VALUES
                (1, 'iMessage;-;+15551111111', '+15551111111', NULL),
                (2, 'iMessage;-;+15552222222', '+15552222222', NULL),
                (3, 'iMessage;+;chat3333333333', 'chat3333333333', 'Project Team');

            -- Handles
            INSERT INTO handle (ROWID, id) VALUES
                (1, '+15551111111'),
                (2, '+15552222222'),
                (3, '+15553333333');

            -- Chat handle joins
            INSERT INTO chat_handle_join (chat_id, handle_id) VALUES
                (1, 1),
                (2, 2),
                (3, 1),
                (3, 2),
                (3, 3);

            -- 5 Messages (including one from-me and one with 'G-123456')
            INSERT INTO message (ROWID, guid, text, date, is_from_me, is_delivered, error, handle_id) VALUES
                (1, 'msg-1', 'Hello from Alice', 700000000000000000, 0, 1, 0, 1),
                (2, 'msg-2', 'Your verification code is G-123456', 700001000000000000, 0, 1, 0, 1),
                (3, 'msg-3', 'Hey Bob here', 700002000000000000, 0, 1, 0, 2),
                (4, 'msg-4', 'Outgoing reply from me', 700003000000000000, 1, 1, 0, 0),
                (5, 'msg-5', 'Team update message', 700004000000000000, 0, 1, 0, 3);

            -- Chat message joins
            INSERT INTO chat_message_join (chat_id, message_id) VALUES
                (1, 1),
                (1, 2),
                (2, 3),
                (3, 4),
                (3, 5);
            "#,
        )
        .expect("failed to populate fixture db");

        (temp_dir, db_path)
    }

    #[test]
    #[cfg(target_os = "macos")]
    fn test_imessage_fixture_queries() {
        let (_dir, db_path) = setup_fixture_db();
        let db_str = db_path.to_str().expect("valid utf8 path");

        // 1. FDA status probe on fixture DB
        let fda = fda_status(Some(db_str)).expect("fda_status failed");
        assert_eq!(fda, FdaStatus::Granted);

        // 2. chats() query
        let chat_list = chats(Some(db_str)).expect("chats failed");
        assert_eq!(chat_list.len(), 3);

        let c1 = chat_list
            .iter()
            .find(|c| c.guid == "iMessage;-;+15551111111")
            .expect("chat 1 not found");
        assert_eq!(c1.identifier, "+15551111111");
        assert!(!c1.is_group);
        assert_eq!(c1.participants, vec!["+15551111111"]);
        assert_eq!(
            c1.last_message.as_deref(),
            Some("Your verification code is G-123456")
        );

        let c3 = chat_list
            .iter()
            .find(|c| c.guid == "iMessage;+;chat3333333333")
            .expect("chat 3 not found");
        assert_eq!(c3.identifier, "chat3333333333");
        assert_eq!(c3.name.as_deref(), Some("Project Team"));
        assert!(c3.is_group);
        assert_eq!(c3.participants.len(), 3);
        assert_eq!(c3.last_message.as_deref(), Some("Team update message"));

        // 3. history() query
        let hist = history("iMessage;-;+15551111111", 10, Some(db_str)).expect("history failed");
        assert_eq!(hist.len(), 2);
        assert_eq!(hist[0].guid, "msg-2");
        assert_eq!(
            hist[0].text.as_deref(),
            Some("Your verification code is G-123456")
        );
        assert_eq!(hist[0].sender, "+15551111111");
        assert!(!hist[0].is_from_me);
        // date_unix = 700001000 + 978307200 = 1678308200
        assert_eq!(hist[0].date_unix, 1678308200);

        assert_eq!(hist[1].guid, "msg-1");
        assert_eq!(hist[1].text.as_deref(), Some("Hello from Alice"));

        // 4. search() queries
        let search_code =
            search(Some("G-123456"), None, Some(db_str)).expect("search query failed");
        assert_eq!(search_code.messages.len(), 1);
        assert_eq!(search_code.messages[0].guid, "msg-2");
        assert!(!search_code.truncated);

        let search_from_me = search(None, Some("me"), Some(db_str)).expect("search sender failed");
        assert_eq!(search_from_me.messages.len(), 1);
        assert_eq!(search_from_me.messages[0].guid, "msg-4");
        assert!(search_from_me.messages[0].is_from_me);
        assert_eq!(search_from_me.messages[0].sender, "me");

        let search_bob =
            search(None, Some("+15552222222"), Some(db_str)).expect("search sender failed");
        assert_eq!(search_bob.messages.len(), 1);
        assert_eq!(search_bob.messages[0].guid, "msg-3");
    }
}
