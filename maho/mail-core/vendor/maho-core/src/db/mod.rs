pub mod migrations;

use std::ops::{Deref, DerefMut};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex, MutexGuard};

use rusqlite::Connection;

use crate::error::AppError;

/// Opens a single standalone connection to a mail database.
///
/// Used by tooling that owns the file for the duration of one operation, such
/// as the transfer importer. The mail helper itself goes through
/// [`init_database_pool`].
pub fn init_database(db_path: &Path) -> Result<Connection, AppError> {
    if let Some(parent) = db_path.parent() {
        std::fs::create_dir_all(parent)?;
    }

    let conn = Connection::open(db_path)?;

    conn.execute_batch("PRAGMA journal_mode=WAL;")?;
    conn.execute_batch("PRAGMA foreign_keys=ON;")?;
    conn.execute_batch("PRAGMA synchronous=NORMAL;")?;
    conn.execute_batch("PRAGMA cache_size=-8000;")?;
    conn.execute_batch("PRAGMA temp_store=MEMORY;")?;

    migrations::run_migrations(&conn)?;

    Ok(conn)
}

/// The one connection to a mail database, shared by every caller.
///
/// The mail helper is a single process owning a single database, so a pool of
/// interchangeable connections buys nothing and costs a whole class of
/// handover bugs. One connection behind a mutex is the whole contract.
///
/// Cloning shares the same connection.
#[derive(Clone)]
pub struct DbPool(Arc<Mutex<Connection>>);

/// Exclusive access to the shared connection. Derefs to [`Connection`], so
/// call sites keep the `let conn = pool.get()?; service::foo(&conn, ..)` shape.
///
/// This is a `std::sync::MutexGuard`, hence `!Send`: it must not be held across
/// an `.await`. Scope it in a block when async code follows.
pub struct DbConnection<'a>(MutexGuard<'a, Connection>);

impl Deref for DbConnection<'_> {
    type Target = Connection;

    fn deref(&self) -> &Connection {
        &self.0
    }
}

impl DerefMut for DbConnection<'_> {
    fn deref_mut(&mut self) -> &mut Connection {
        &mut self.0
    }
}

impl DbPool {
    fn new(conn: Connection) -> Self {
        Self(Arc::new(Mutex::new(conn)))
    }

    /// Waits for the shared connection and hands it out.
    ///
    /// Poisoning is recovered from deliberately: a thread that panicked while
    /// holding the connection has already had its `Transaction` rolled back by
    /// `Drop` during unwinding, and refusing every later query would turn one
    /// panicking mail operation into a dead helper process.
    pub fn get(&self) -> Result<DbConnection<'_>, AppError> {
        Ok(DbConnection(
            self.0.lock().unwrap_or_else(|poisoned| poisoned.into_inner()),
        ))
    }
}

pub type SqlitePool = DbPool;

/// Contention window against writers outside this process (a backup tool, an
/// interrupted previous run still flushing). Within the process the mutex
/// already serializes access.
const POOL_BUSY_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(10);

/// Names the failing step and carries SQLite's own codes, so a caller such as
/// the mail helper can tell lock contention apart from a key mismatch instead
/// of logging a bare "database is locked".
fn db_step_error(step: &str, db_path: &Path, err: &rusqlite::Error) -> AppError {
    let codes = match err {
        rusqlite::Error::SqliteFailure(ffi_err, _) => format!(
            " [sqlite code={:?} extended={}]",
            ffi_err.code, ffi_err.extended_code
        ),
        _ => String::new(),
    };
    AppError::Internal(format!(
        "database {step} failed for {}{codes}: {err}",
        db_path.display()
    ))
}

/// Puts the database into WAL mode, tolerating a concurrent converter.
///
/// `PRAGMA journal_mode=WAL` needs an exclusive lock and, unlike ordinary
/// statements, never goes through SQLite's busy handler: when another process
/// holds the database it returns `SQLITE_BUSY` immediately, no matter how long
/// `busy_timeout` is. The conversion only has to happen once per database, so a
/// busy answer is retried for a short while and the mode another writer
/// installed is accepted.
fn ensure_wal_journal(conn: &Connection, db_path: &Path) -> Result<(), AppError> {
    const ATTEMPTS: u32 = 20;
    const RETRY_DELAY: std::time::Duration = std::time::Duration::from_millis(25);

    let mut last_busy: Option<rusqlite::Error> = None;
    for attempt in 0..ATTEMPTS {
        match conn.query_row("PRAGMA journal_mode=WAL;", [], |row| row.get::<_, String>(0)) {
            Ok(mode) if mode.eq_ignore_ascii_case("wal") => return Ok(()),
            Ok(_) => {}
            Err(err) => {
                let busy = matches!(
                    &err,
                    rusqlite::Error::SqliteFailure(ffi_err, _)
                        if ffi_err.code == rusqlite::ErrorCode::DatabaseBusy
                            || ffi_err.code == rusqlite::ErrorCode::DatabaseLocked
                );
                if !busy {
                    return Err(db_step_error("journal_mode=WAL", db_path, &err));
                }
                last_busy = Some(err);
            }
        }

        match conn.query_row("PRAGMA journal_mode;", [], |row| row.get::<_, String>(0)) {
            Ok(mode) if mode.eq_ignore_ascii_case("wal") => return Ok(()),
            Ok(_) => {}
            Err(err) => return Err(db_step_error("journal_mode read", db_path, &err)),
        }

        if attempt + 1 < ATTEMPTS {
            std::thread::sleep(RETRY_DELAY);
        }
    }

    match last_busy {
        Some(err) => Err(db_step_error("journal_mode=WAL", db_path, &err)),
        None => Err(AppError::Internal(format!(
            "database {} stayed out of WAL mode after {ATTEMPTS} attempts",
            db_path.display()
        ))),
    }
}

/// Names the POSIX VFS explicitly through a URI filename.
///
/// The mail helper is a Chromium process, and Chromium installs its own SQLite
/// VFS as the default one. Naming the VFS keeps every launch of the helper
/// opening the file the same way regardless of what the surrounding process
/// installed.
fn vfs_uri(db_path: &Path) -> String {
    format!(
        "file:{}?vfs=unix",
        db_path.to_string_lossy().replace('?', "%3f").replace('#', "%23")
    )
}

fn open_flags() -> rusqlite::OpenFlags {
    rusqlite::OpenFlags::SQLITE_OPEN_READ_WRITE
        | rusqlite::OpenFlags::SQLITE_OPEN_CREATE
        | rusqlite::OpenFlags::SQLITE_OPEN_URI
        | rusqlite::OpenFlags::SQLITE_OPEN_NO_MUTEX
}

/// Opens `db_path` and gives writers a contention window.
fn open_mail_database(db_path: &Path) -> Result<Connection, AppError> {
    let conn = Connection::open_with_flags(vfs_uri(db_path), open_flags())
        .map_err(|e| db_step_error("open", db_path, &e))?;
    conn.busy_timeout(POOL_BUSY_TIMEOUT)
        .map_err(|e| db_step_error("busy_timeout", db_path, &e))?;
    Ok(conn)
}

/// SQLite reads the header lazily, on the first page read. This forces that
/// read so a damaged file surfaces here instead of inside a later step.
fn probe_readable(conn: &Connection) -> Result<(), rusqlite::Error> {
    conn.query_row("SELECT count(*) FROM sqlite_master;", [], |row| {
        row.get::<_, i64>(0)
    })
    .map(|_| ())
}

fn is_not_a_database(err: &rusqlite::Error) -> bool {
    matches!(
        err,
        rusqlite::Error::SqliteFailure(ffi_err, _)
            if ffi_err.code == rusqlite::ErrorCode::NotADatabase
    )
}

/// Moves aside a file that SQLite refuses to read as a database.
///
/// The file keeps its bytes under a timestamped name so it can be inspected or
/// recovered later; the write-ahead sidecars belong to it and are dropped.
fn quarantine_unreadable_database(db_path: &Path) -> Result<PathBuf, AppError> {
    let stamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|since| since.as_secs())
        .unwrap_or_default();
    let mut quarantined = db_path.as_os_str().to_os_string();
    quarantined.push(format!(".unreadable-{stamp}"));
    let quarantined = PathBuf::from(quarantined);
    std::fs::rename(db_path, &quarantined)?;
    for suffix in ["-wal", "-shm"] {
        let mut sidecar = db_path.as_os_str().to_os_string();
        sidecar.push(suffix);
        let sidecar = PathBuf::from(sidecar);
        if sidecar.exists() {
            std::fs::remove_file(&sidecar)?;
        }
    }
    Ok(quarantined)
}

/// Returns a connection to a database SQLite can actually read.
///
/// A truncated file, a half-written copy, or a database left behind by a build
/// that encrypted the whole file answers every open with `NotADatabase`. Mail
/// content is a re-syncable cache of the server, so such a file is quarantined
/// and a fresh database takes its place; leaving it in place would crash-loop
/// the mail helper forever.
fn open_setup_connection(db_path: &Path) -> Result<Connection, AppError> {
    let conn = open_mail_database(db_path)?;
    match probe_readable(&conn) {
        Ok(()) => return Ok(conn),
        Err(err) if is_not_a_database(&err) => {
            drop(conn);
            let quarantined = quarantine_unreadable_database(db_path)?;
            log::error!(
                "file {} is not a readable database; moved to {} and recreating",
                db_path.display(),
                quarantined.display()
            );
        }
        Err(err) => return Err(db_step_error("header read", db_path, &err)),
    }

    let conn = open_mail_database(db_path)?;
    probe_readable(&conn).map_err(|e| db_step_error("header read", db_path, &e))?;
    Ok(conn)
}

/// Opens the one connection the caller will use for the whole database's life.
///
/// The mail database is a plain SQLite file inside the profile directory, the
/// same shape Chromium gives its own History and Cookies stores. Nothing about
/// reopening it depends on a key: secrets are encrypted per field, by the
/// credential store, with a key the helper is given at startup. A database the
/// previous run wrote is therefore always readable by the next one.
pub fn init_database_pool(db_path: &Path) -> Result<SqlitePool, AppError> {
    if let Some(parent) = db_path.parent() {
        std::fs::create_dir_all(parent)?;
    }

    let conn = open_setup_connection(db_path)?;
    ensure_wal_journal(&conn, db_path)?;
    conn.execute_batch(
        "PRAGMA foreign_keys=ON;\
         PRAGMA synchronous=NORMAL;\
         PRAGMA cache_size=-8000;\
         PRAGMA temp_store=MEMORY;",
    )
    .map_err(|e| db_step_error("connection pragmas", db_path, &e))?;

    migrations::run_migrations(&conn)?;

    // Fold the schema the migrations just wrote into the database file itself.
    // With one long-lived connection nothing else would ever checkpoint, so the
    // main file would stay at its 4 KiB header while a multi-megabyte WAL
    // carried every page — and a crash before any later checkpoint would leave
    // exactly that pair on disk.
    let _ = conn.query_row("PRAGMA wal_checkpoint(TRUNCATE);", [], |_| Ok(()));

    Ok(DbPool::new(conn))
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
