//! The mail database has to outlive the process that created it.
//!
//! The mail helper opens its database through `init_database_pool` on every
//! launch. A database the previous launch wrote must therefore be readable by
//! the next one; when it was not, the helper quarantined it and every browser
//! restart silently threw the user's mail away.

use maho_core::db::init_database_pool;

fn temp_db_path(name: &str) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "maho-mail-db-{}-{}-{:?}",
        name,
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or_default()
    ));
    std::fs::create_dir_all(&dir).expect("temp dir");
    dir.join("maho_mail.db")
}

#[test]
fn a_database_reopens_after_the_writing_connection_is_gone() {
    let path = temp_db_path("reopen");

    let pool = init_database_pool(&path).expect("first open");
    {
        let conn = pool.get().expect("connection");
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
             VALUES ('acc1', 'user@example.com', 'User', 'imap.example.com', 993, 'Tls', 'smtp.example.com', 587, 'Tls', 'user')",
            [],
        )
        .expect("insert");
    }
    drop(pool);

    let reopened = init_database_pool(&path).expect("second open must not need a key");
    let conn = reopened.get().expect("connection");
    let email: String = conn
        .query_row("SELECT email FROM accounts WHERE id = 'acc1'", [], |row| {
            row.get(0)
        })
        .expect("row written by the previous connection survives the reopen");
    assert_eq!(email, "user@example.com");
}

#[test]
fn the_database_file_itself_carries_the_schema() {
    let path = temp_db_path("schema");
    let pool = init_database_pool(&path).expect("open");
    drop(pool);

    let size = std::fs::metadata(&path).expect("database file").len();
    assert!(
        size > 8192,
        "schema belongs in the database file, found {size} bytes"
    );
}

#[test]
fn the_pool_handle_shares_one_connection() {
    let path = temp_db_path("shared");
    let pool = init_database_pool(&path).expect("open");

    {
        let conn = pool.get().expect("first handle");
        conn.execute("CREATE TABLE probe (id INTEGER PRIMARY KEY)", [])
            .expect("create");
    }

    let clone = pool.clone();
    let conn = clone.get().expect("second handle");
    let count: i64 = conn
        .query_row("SELECT count(*) FROM probe", [], |row| row.get(0))
        .expect("the clone sees the same database");
    assert_eq!(count, 0);
}

#[test]
fn concurrent_access_through_the_single_connection_all_succeeds() {
    let path = temp_db_path("concurrent");
    let pool = init_database_pool(&path).expect("open");
    {
        let conn = pool.get().expect("setup");
        conn.execute("CREATE TABLE probe (id INTEGER PRIMARY KEY)", [])
            .expect("create");
    }

    let handles: Vec<_> = (0..8)
        .map(|i| {
            let pool = pool.clone();
            std::thread::spawn(move || {
                let conn = pool.get().expect("worker connection");
                conn.execute("INSERT INTO probe (id) VALUES (?1)", [i])
                    .expect("worker insert");
            })
        })
        .collect();
    for handle in handles {
        handle.join().expect("worker thread");
    }

    let conn = pool.get().expect("connection");
    let count: i64 = conn
        .query_row("SELECT count(*) FROM probe", [], |row| row.get(0))
        .expect("count");
    assert_eq!(count, 8);
}

#[test]
fn a_file_that_is_not_a_database_is_quarantined_and_recreated() {
    let path = temp_db_path("garbage");
    std::fs::write(&path, b"this is not a database, not even close").expect("write garbage");

    let pool = init_database_pool(&path).expect("a damaged file must not block startup");
    let conn = pool.get().expect("connection");
    conn.query_row("SELECT count(*) FROM accounts", [], |row| {
        row.get::<_, i64>(0)
    })
    .expect("a fresh database took its place");

    let dir = path.parent().expect("parent");
    let quarantined = std::fs::read_dir(dir)
        .expect("read dir")
        .filter_map(|entry| entry.ok())
        .any(|entry| entry.file_name().to_string_lossy().contains(".unreadable-"));
    assert!(quarantined, "the damaged file is kept for inspection");
}
