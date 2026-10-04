use criterion::{black_box, criterion_group, criterion_main, Criterion};
use rusqlite::{params, Connection};

fn setup_bench_db() -> Connection {
    let conn = Connection::open_in_memory().unwrap();
    conn.pragma_update(None, "key", "test-key-not-for-production")
        .unwrap();
    conn.execute_batch("PRAGMA journal_mode=WAL;").unwrap();
    conn.execute_batch("PRAGMA foreign_keys=ON;").unwrap();
    maho_core::db::migrations::run_migrations(&conn).unwrap();

    // seed account + folder
    conn.execute(
        "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
         VALUES ('bench-acc', 'bench@test.com', 'Bench', 'imap.test.com', 993, 'Tls', 'smtp.test.com', 587, 'Tls', 'bench')",
        [],
    ).unwrap();
    conn.execute(
        "INSERT INTO folders (id, account_id, name, path, folder_type)
         VALUES ('bench-fold', 'bench-acc', 'INBOX', 'INBOX', 'inbox')",
        [],
    )
    .unwrap();

    conn
}

fn setup_bench_db_with_emails(count: usize) -> Connection {
    let conn = setup_bench_db();

    // Insert emails in batches for better performance
    let batch_size = 1000;
    for batch in 0..(count / batch_size) {
        let tx = conn.unchecked_transaction().unwrap();
        for i in 0..batch_size {
            let idx = batch * batch_size + i;
            tx.execute(
                "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, date, snippet, is_read, is_starred, is_draft, has_attachments, raw_size, body_text)
                 VALUES (?1, 'bench-acc', 'bench-fold', ?2, ?3, ?4, 'sender@test.com', '2024-01-01', 'snippet', 0, 0, 0, 0, 1024, ?5)",
                params![
                    format!("em-{}", idx),
                    idx as i64,
                    format!("<msg-{}@bench>", idx),
                    format!("Subject {}", idx),
                    format!("Body text for email number {}", idx),
                ],
            ).unwrap();
        }
        tx.commit().unwrap();
    }

    conn
}

fn bench_bulk_insert(c: &mut Criterion) {
    let mut group = c.benchmark_group("bulk_insert");
    group.sample_size(10);

    group.bench_function("insert_100k_emails", |b| {
        b.iter_with_setup(
            || setup_bench_db(),
            |conn| {
                // Insert 100k emails in batches of 1000
                for batch in 0..100 {
                    let tx = conn.unchecked_transaction().unwrap();
                    for i in 0..1000 {
                        let idx = batch * 1000 + i;
                        tx.execute(
                            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, date, snippet, is_read, is_starred, is_draft, has_attachments, raw_size)
                             VALUES (?1, 'bench-acc', 'bench-fold', ?2, ?3, ?4, 'sender@test.com', '2024-01-01', 'snippet', 0, 0, 0, 0, 1024)",
                            params![
                                format!("em-{}", idx),
                                idx as i64,
                                format!("<msg-{}@bench>", idx),
                                format!("Subject {}", idx),
                            ],
                        ).unwrap();
                    }
                    tx.commit().unwrap();
                }
                black_box(&conn);
            },
        );
    });

    group.finish();
}

fn bench_fts5_search(c: &mut Criterion) {
    let mut group = c.benchmark_group("fts5_search");

    // Setup: Create DB with 100k emails
    let conn = setup_bench_db_with_emails(100_000);

    group.bench_function("single_word_search", |b| {
        b.iter(|| {
            let results: Vec<String> = conn
                .prepare(
                    "SELECT e.id
                     FROM emails e
                     JOIN emails_fts fts ON fts.rowid = e.rowid
                     WHERE emails_fts MATCH ?1
                     LIMIT 100",
                )
                .unwrap()
                .query_map(["Subject"], |row| row.get(0))
                .unwrap()
                .collect::<Result<_, _>>()
                .unwrap();
            black_box(results);
        });
    });

    group.bench_function("multi_word_search", |b| {
        b.iter(|| {
            let results: Vec<String> = conn
                .prepare(
                    "SELECT e.id
                     FROM emails e
                     JOIN emails_fts fts ON fts.rowid = e.rowid
                     WHERE emails_fts MATCH ?1
                     LIMIT 100",
                )
                .unwrap()
                .query_map(["Subject 12345"], |row| row.get(0))
                .unwrap()
                .collect::<Result<_, _>>()
                .unwrap();
            black_box(results);
        });
    });

    group.bench_function("body_text_search", |b| {
        b.iter(|| {
            let results: Vec<String> = conn
                .prepare(
                    "SELECT e.id
                     FROM emails e
                     JOIN emails_fts fts ON fts.rowid = e.rowid
                     WHERE emails_fts MATCH ?1
                     LIMIT 100",
                )
                .unwrap()
                .query_map(["Body text"], |row| row.get(0))
                .unwrap()
                .collect::<Result<_, _>>()
                .unwrap();
            black_box(results);
        });
    });

    group.finish();
}

fn bench_concurrent_ops(c: &mut Criterion) {
    let mut group = c.benchmark_group("concurrent_ops");
    group.sample_size(10);

    group.bench_function("mixed_read_write_10k_base", |b| {
        b.iter_with_setup(
            || setup_bench_db_with_emails(10_000),
            |conn| {
                // Simulate mixed read/write operations
                // 100 reads + 10 writes per iteration

                // Reads: Query emails with different filters
                for i in 0..100 {
                    let _count: i64 = conn
                        .query_row(
                            "SELECT COUNT(*) FROM emails WHERE subject LIKE ?1",
                            [format!("%Subject {}%", i * 100)],
                            |row| row.get(0),
                        )
                        .unwrap();
                }

                // Writes: Update email read status
                let tx = conn.unchecked_transaction().unwrap();
                for i in 0..10 {
                    tx.execute(
                        "UPDATE emails SET is_read = ?1 WHERE id = ?2",
                        params![i % 2 == 0, format!("em-{}", i * 1000)],
                    )
                    .unwrap();
                }
                tx.commit().unwrap();

                black_box(&conn);
            },
        );
    });

    group.finish();
}

criterion_group!(
    benches,
    bench_bulk_insert,
    bench_fts5_search,
    bench_concurrent_ops
);
criterion_main!(benches);
