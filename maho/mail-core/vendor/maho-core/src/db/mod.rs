pub mod migrations;

use std::path::Path;

use rand::Rng;
use rusqlite::Connection;

use crate::error::AppError;

fn load_or_generate_encryption_key(db_path: &Path) -> Result<String, AppError> {
    let key_path = db_path.with_extension(match db_path.extension() {
        Some(ext) => format!("{}.key", ext.to_string_lossy()),
        None => "key".to_string(),
    });

    if key_path.exists() {
        let key = std::fs::read_to_string(&key_path)?;
        return Ok(key.trim().to_string());
    }

    let mut rng = rand::thread_rng();
    let mut bytes = [0u8; 32];
    rng.fill(&mut bytes);
    let hex_key: String = bytes.iter().map(|b| format!("{b:02x}")).collect();

    if let Some(parent) = key_path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    std::fs::write(&key_path, &hex_key)?;

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&key_path, std::fs::Permissions::from_mode(0o600))?;
    }

    Ok(hex_key)
}

/// Must be called BEFORE `init_database` opens the file with a key.
fn migrate_plain_to_encrypted(db_path: &Path, key: &str) -> Result<(), AppError> {
    if !db_path.exists() {
        return Ok(());
    }

    let plain_conn = Connection::open(db_path)?;
    let is_plain = plain_conn
        .query_row("SELECT count(*) FROM sqlite_master", [], |row| {
            row.get::<_, i64>(0)
        })
        .is_ok();

    if !is_plain {
        drop(plain_conn);
        return Ok(());
    }

    let encrypted_path = db_path.with_extension("db.encrypted");
    let encrypted_path_str = encrypted_path.to_string_lossy().to_string();

    if encrypted_path.exists() {
        std::fs::remove_file(&encrypted_path)?;
    }

    plain_conn.execute_batch(&format!(
        "ATTACH DATABASE '{}' AS encrypted KEY '{}';",
        encrypted_path_str.replace('\'', "''"),
        key.replace('\'', "''"),
    ))?;
    plain_conn.execute_batch("SELECT sqlcipher_export('encrypted');")?;
    plain_conn.execute_batch("DETACH DATABASE encrypted;")?;
    drop(plain_conn);

    let backup_path = db_path.with_extension("db.plain_backup");
    std::fs::rename(db_path, &backup_path)?;
    std::fs::rename(&encrypted_path, db_path)?;

    // Verify the encrypted DB can be opened before deleting the backup
    let verify_conn = Connection::open(db_path)?;
    verify_conn.pragma_update(None, "key", key)?;
    let verified = verify_conn
        .query_row("SELECT count(*) FROM sqlite_master", [], |row| {
            row.get::<_, i64>(0)
        })
        .is_ok();
    drop(verify_conn);

    if verified {
        let _ = std::fs::remove_file(&backup_path);
    } else {
        // Rollback: restore backup
        let _ = std::fs::rename(&backup_path, db_path);
        return Err(AppError::Internal(
            "Encrypted DB verification failed after migration; backup restored".to_string(),
        ));
    }

    Ok(())
}

pub fn init_database(db_path: &Path) -> Result<Connection, AppError> {
    if let Some(parent) = db_path.parent() {
        std::fs::create_dir_all(parent)?;
    }

    let key = load_or_generate_encryption_key(db_path)?;

    migrate_plain_to_encrypted(db_path, &key)?;

    let conn = Connection::open(db_path)?;

    // PRAGMA key MUST be the first statement after open
    conn.pragma_update(None, "key", &key)?;

    conn.execute_batch("PRAGMA journal_mode=WAL;")?;
    conn.execute_batch("PRAGMA foreign_keys=ON;")?;
    conn.execute_batch("PRAGMA synchronous=NORMAL;")?;
    conn.execute_batch("PRAGMA cache_size=-8000;")?;
    conn.execute_batch("PRAGMA temp_store=MEMORY;")?;

    migrations::run_migrations(&conn)?;

    Ok(conn)
}

pub fn init_database_with_key(db_path: &Path, key: &str) -> Result<Connection, AppError> {
    if let Some(parent) = db_path.parent() {
        std::fs::create_dir_all(parent)?;
    }

    migrate_plain_to_encrypted(db_path, key)?;

    let conn = Connection::open(db_path)?;

    // PRAGMA key MUST be the first statement after open
    conn.pragma_update(None, "key", key)?;

    conn.execute_batch("PRAGMA journal_mode=WAL;")?;
    conn.execute_batch("PRAGMA foreign_keys=ON;")?;
    conn.execute_batch("PRAGMA synchronous=NORMAL;")?;
    conn.execute_batch("PRAGMA cache_size=-8000;")?;
    conn.execute_batch("PRAGMA temp_store=MEMORY;")?;

    migrations::run_migrations(&conn)?;

    Ok(conn)
}

pub type SqlitePool = r2d2::Pool<r2d2_sqlite::SqliteConnectionManager>;

pub fn init_database_pool(db_path: &Path, key: &str) -> Result<SqlitePool, AppError> {
    if let Some(parent) = db_path.parent() {
        std::fs::create_dir_all(parent)?;
    }

    migrate_plain_to_encrypted(db_path, key)?;

    let key_owned = key.to_string();
    let manager = r2d2_sqlite::SqliteConnectionManager::file(db_path).with_init(move |conn| {
        conn.pragma_update(None, "key", &key_owned)?;
        conn.execute_batch("PRAGMA journal_mode=WAL;")?;
        conn.execute_batch("PRAGMA foreign_keys=ON;")?;
        conn.execute_batch("PRAGMA synchronous=NORMAL;")?;
        conn.execute_batch("PRAGMA cache_size=-8000;")?;
        conn.execute_batch("PRAGMA temp_store=MEMORY;")?;
        Ok(())
    });

    let pool = r2d2::Pool::builder()
        .max_size(10)
        .build(manager)
        .map_err(|e| AppError::Internal(format!("Failed to build connection pool: {}", e)))?;

    let conn = pool
        .get()
        .map_err(|e| AppError::Internal(format!("Failed to get connection from pool: {}", e)))?;
    migrations::run_migrations(&conn)?;

    Ok(pool)
}

#[cfg(test)]
mod tests {
    use rusqlite::Connection;

    fn setup_test_db() -> Connection {
        let conn = Connection::open_in_memory().unwrap();
        conn.pragma_update(None, "key", "test-key-not-for-production")
            .unwrap();
        conn.execute_batch("PRAGMA journal_mode=WAL;").unwrap();
        conn.execute_batch("PRAGMA foreign_keys=ON;").unwrap();
        crate::db::migrations::run_migrations(&conn).unwrap();
        conn
    }

    #[test]
    fn test_schema_creation() {
        let conn = setup_test_db();
        let tables: Vec<String> = conn
            .prepare("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")
            .unwrap()
            .query_map([], |row| row.get(0))
            .unwrap()
            .collect::<Result<_, _>>()
            .unwrap();

        assert!(tables.contains(&"accounts".to_string()));
        assert!(tables.contains(&"emails".to_string()));
        assert!(tables.contains(&"folders".to_string()));
        assert!(tables.contains(&"attachments".to_string()));
        assert!(tables.contains(&"ai_configs".to_string()));
    }

    #[test]
    fn test_account_crud() {
        let conn = setup_test_db();

        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)",
            rusqlite::params![
                "acc1",
                "test@example.com",
                "Test User",
                "imap.example.com",
                993,
                "Tls",
                "smtp.example.com",
                587,
                "StartTls",
                "test@example.com"
            ],
        )
        .unwrap();

        let email: String = conn
            .query_row(
                "SELECT email FROM accounts WHERE id = ?1",
                ["acc1"],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(email, "test@example.com");

        conn.execute("DELETE FROM accounts WHERE id = ?1", ["acc1"])
            .unwrap();
        let count: i64 = conn
            .query_row("SELECT COUNT(*) FROM accounts", [], |row| row.get(0))
            .unwrap();
        assert_eq!(count, 0);
    }

    #[test]
    fn test_email_fts5_search() {
        let conn = setup_test_db();

        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)",
            rusqlite::params![
                "acc1",
                "test@example.com",
                "Test",
                "imap.test.com",
                993,
                "Tls",
                "smtp.test.com",
                587,
                "Tls",
                "test"
            ],
        )
        .unwrap();

        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type)
             VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params!["fold1", "acc1", "INBOX", "INBOX", "inbox"],
        )
        .unwrap();

        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, in_reply_to, subject, from_address, from_name, to_addresses, cc_addresses, bcc_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, body_text, body_html, raw_size)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21)",
            rusqlite::params![
                "em1",
                "acc1",
                "fold1",
                1_i64,
                "<msg1@test.com>",
                Option::<String>::None,
                "Important Meeting",
                "john@test.com",
                "John Doe",
                "[\"recipient@test.com\"]",
                Option::<String>::None,
                Option::<String>::None,
                "2024-01-15T10:00:00Z",
                "Let's discuss the project",
                false,
                false,
                false,
                false,
                "Let's discuss the project timeline",
                Option::<String>::None,
                1234_i64
            ],
        )
        .unwrap();

        let results: Vec<String> = conn
            .prepare(
                "SELECT e.subject
                 FROM emails e
                 JOIN emails_fts fts ON fts.rowid = e.rowid
                 WHERE emails_fts MATCH ?1",
            )
            .unwrap()
            .query_map(["project"], |row| row.get(0))
            .unwrap()
            .collect::<Result<_, _>>()
            .unwrap();

        assert_eq!(results.len(), 1);
        assert_eq!(results[0], "Important Meeting");
    }

    #[test]
    fn test_cascade_delete() {
        let conn = setup_test_db();

        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)",
            rusqlite::params![
                "acc1",
                "test@test.com",
                "Test",
                "imap.test.com",
                993,
                "Tls",
                "smtp.test.com",
                587,
                "Tls",
                "test"
            ],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type) VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params!["f1", "acc1", "INBOX", "INBOX", "inbox"],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, in_reply_to, subject, from_address, from_name, to_addresses, cc_addresses, bcc_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, body_text, body_html, raw_size, body_fetched_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22)",
            rusqlite::params![
                "e1",
                "acc1",
                "f1",
                1_i64,
                "<msg@test.com>",
                Option::<String>::None,
                "Test",
                "sender@test.com",
                Option::<String>::None,
                "[\"recipient@test.com\"]",
                Option::<String>::None,
                Option::<String>::None,
                "2024-01-01T00:00:00Z",
                "snippet",
                false,
                false,
                false,
                false,
                Option::<String>::None,
                Option::<String>::None,
                2048_i64,
                Option::<String>::None
            ],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO attachments (id, email_id, part_id, filename, mime_type, size) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
            rusqlite::params!["att1", "e1", "1", "file.pdf", "application/pdf", 1024],
        )
        .unwrap();

        conn.execute("DELETE FROM accounts WHERE id = ?1", ["acc1"])
            .unwrap();

        let folder_count: i64 = conn
            .query_row("SELECT COUNT(*) FROM folders", [], |row| row.get(0))
            .unwrap();
        let email_count: i64 = conn
            .query_row("SELECT COUNT(*) FROM emails", [], |row| row.get(0))
            .unwrap();
        let att_count: i64 = conn
            .query_row("SELECT COUNT(*) FROM attachments", [], |row| row.get(0))
            .unwrap();

        assert_eq!(folder_count, 0);
        assert_eq!(email_count, 0);
        assert_eq!(att_count, 0);
    }

    #[test]
    fn test_body_fetched_at_migration() {
        let conn = setup_test_db();

        // Verify body_fetched_at column exists
        let col_count: i64 = conn
            .query_row(
                "SELECT COUNT(*) FROM pragma_table_info('emails') WHERE name = 'body_fetched_at'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(col_count, 1, "body_fetched_at column should exist");

        // Verify index exists
        let index_count: i64 = conn
            .query_row(
                "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='idx_emails_body_fetched_at'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(
            index_count, 1,
            "idx_emails_body_fetched_at index should exist"
        );

        // Insert an email with body content
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)",
            rusqlite::params![
                "acc1",
                "test@test.com",
                "Test",
                "imap.test.com",
                993,
                "Tls",
                "smtp.test.com",
                587,
                "Tls",
                "test"
            ],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type) VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params!["f1", "acc1", "INBOX", "INBOX", "inbox"],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, date, snippet, is_read, is_starred, is_draft, has_attachments, body_text, body_html, raw_size, body_fetched_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)",
            rusqlite::params![
                "e1",
                "acc1",
                "f1",
                1_i64,
                "<msg@test.com>",
                "Test",
                "sender@test.com",
                "2024-01-01T00:00:00Z",
                "snippet",
                false,
                false,
                false,
                false,
                "Body content",
                Option::<String>::None,
                2048_i64,
                Option::<String>::None
            ],
        )
        .unwrap();

        // Query emails needing body fetch
        let needing_fetch: i64 = conn
            .query_row(
                "SELECT COUNT(*) FROM emails WHERE body_fetched_at IS NULL",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(needing_fetch, 1);

        // Update body and set fetched_at
        let now = chrono::Utc::now().format("%Y-%m-%d %H:%M:%S").to_string();
        conn.execute(
            "UPDATE emails SET body_fetched_at = ?1 WHERE id = ?2",
            rusqlite::params![now, "e1"],
        )
        .unwrap();

        let needing_fetch_after: i64 = conn
            .query_row(
                "SELECT COUNT(*) FROM emails WHERE body_fetched_at IS NULL",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(needing_fetch_after, 0);
    }
}
