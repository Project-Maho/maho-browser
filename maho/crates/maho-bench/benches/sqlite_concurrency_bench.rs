use criterion::{criterion_group, criterion_main, BenchmarkId, Criterion};
use maho_storage::sqlite::SqliteStorage;
use rusqlite::Connection;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;
use tempfile::TempDir;

const BENCH_KEY: &str = "test-encryption-key-for-sqlite-concurrency-bench";
const INITIAL_ROWS: usize = 1000;
const QUERIES_PER_READER_PER_ITER: usize = 10;

fn init_unencrypted_db(path: &Path, initial_rows: usize) {
    let mut conn = Connection::open(path).expect("init unencrypted conn");
    let _: String = conn
        .query_row("PRAGMA journal_mode = WAL;", [], |row| row.get(0))
        .expect("wal");
    conn.execute("PRAGMA synchronous = NORMAL;", [])
        .expect("sync");
    conn.busy_timeout(Duration::from_millis(2000))
        .expect("busy timeout");
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            url TEXT NOT NULL,
            title TEXT NOT NULL,
            visited_at TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_history_url ON history(url);
        CREATE INDEX IF NOT EXISTS idx_history_title ON history(title);",
    )
    .expect("schema");

    let tx = conn.transaction().expect("tx");
    {
        let mut stmt = tx
            .prepare(
                "INSERT INTO history (url, title, visited_at) VALUES (?1, ?2, datetime('now'))",
            )
            .expect("insert stmt");
        for i in 0..initial_rows {
            let url = format!("https://example-{}.org/item-{}", i % 200, i);
            let title = format!("Item-{} documentation and reference page", i);
            stmt.execute(rusqlite::params![url, title])
                .expect("exec insert");
        }
    }
    tx.commit().expect("commit");
}

fn init_encrypted_db(path: &Path, key: &str, initial_rows: usize) {
    let storage =
        SqliteStorage::open_with_key(path.to_str().expect("path"), key).expect("init storage");
    for i in 0..initial_rows {
        let url = format!("https://example-{}.org/item-{}", i % 200, i);
        let title = format!("Item-{} documentation and reference page", i);
        storage
            .add_history_entry(&url, &title)
            .expect("insert history");
    }
}

struct BackgroundWriter {
    stop_signal: Arc<AtomicBool>,
    handle: Option<std::thread::JoinHandle<()>>,
}

impl BackgroundWriter {
    fn start_unencrypted(path: PathBuf) -> Self {
        let stop_signal = Arc::new(AtomicBool::new(false));
        let stop = Arc::clone(&stop_signal);
        let handle = std::thread::spawn(move || {
            let conn = Connection::open(&path).expect("writer conn");
            let _: String = conn
                .query_row("PRAGMA journal_mode = WAL;", [], |row| row.get(0))
                .expect("wal");
            conn.execute("PRAGMA synchronous = NORMAL;", [])
                .expect("sync");
            conn.busy_timeout(Duration::from_millis(2000))
                .expect("timeout");
            let mut seq = 0u64;
            while !stop.load(Ordering::Relaxed) {
                seq += 1;
                let url = format!("https://writer-stream.test/item/{seq}");
                let title = format!("Streaming write item #{seq}");
                let _ = conn.execute(
                    "INSERT INTO history (url, title, visited_at) VALUES (?1, ?2, datetime('now'))",
                    rusqlite::params![url, title],
                );
                std::thread::sleep(Duration::from_micros(100));
            }
        });
        Self {
            stop_signal,
            handle: Some(handle),
        }
    }

    fn start_encrypted(path: PathBuf, key: String) -> Self {
        let stop_signal = Arc::new(AtomicBool::new(false));
        let stop = Arc::clone(&stop_signal);
        let handle = std::thread::spawn(move || {
            let storage = SqliteStorage::open_with_key(path.to_str().expect("path"), &key)
                .expect("writer storage");
            let mut seq = 0u64;
            while !stop.load(Ordering::Relaxed) {
                seq += 1;
                let url = format!("https://writer-stream.test/item/{seq}");
                let title = format!("Streaming write item #{seq}");
                let _ = storage.add_history_entry(&url, &title);
                std::thread::sleep(Duration::from_micros(100));
            }
        });
        Self {
            stop_signal,
            handle: Some(handle),
        }
    }
}

impl Drop for BackgroundWriter {
    fn drop(&mut self) {
        self.stop_signal.store(true, Ordering::Relaxed);
        if let Some(h) = self.handle.take() {
            let _ = h.join();
        }
    }
}

struct ReaderWorker {
    task_tx: Option<std::sync::mpsc::Sender<()>>,
    done_rx: std::sync::mpsc::Receiver<()>,
    handle: Option<std::thread::JoinHandle<()>>,
}

impl Drop for ReaderWorker {
    fn drop(&mut self) {
        // Drop task_tx first so the worker's recv() returns Err and loop terminates
        drop(self.task_tx.take());
        if let Some(h) = self.handle.take() {
            if let Err(error) = h.join() {
                if std::thread::panicking() {
                    eprintln!("reader worker also panicked while reporting a benchmark failure");
                } else {
                    std::panic::resume_unwind(error);
                }
            }
        }
    }
}

struct ReaderPool {
    workers: Vec<ReaderWorker>,
}

impl ReaderPool {
    fn new_unencrypted(path: &Path, count: usize, queries_per_reader: usize) -> Self {
        let mut workers = Vec::with_capacity(count);
        for worker_idx in 0..count {
            let (task_tx, task_rx) = std::sync::mpsc::channel();
            let (done_tx, done_rx) = std::sync::mpsc::channel();
            let db_path = path.to_path_buf();
            let handle = std::thread::spawn(move || {
                let conn = Connection::open(&db_path).expect("reader conn");
                let _: String = conn
                    .query_row("PRAGMA journal_mode = WAL;", [], |row| row.get(0))
                    .expect("wal");
                conn.execute("PRAGMA synchronous = NORMAL;", [])
                    .expect("sync");
                conn.busy_timeout(Duration::from_millis(2000))
                    .expect("timeout");
                done_tx.send(()).expect("report reader readiness");

                while task_rx.recv().is_ok() {
                    for i in 0..queries_per_reader {
                        let query_pattern = format!("%item-{}%", (worker_idx * 17 + i * 13) % 200);
                        let mut stmt = conn
                            .prepare_cached(
                                "SELECT url, title, visited_at FROM history WHERE title LIKE ?1 OR url LIKE ?1 ORDER BY visited_at DESC LIMIT 20",
                            )
                            .expect("prepare");
                        let rows = stmt
                            .query_map([&query_pattern], |row| {
                                Ok((
                                    row.get::<_, String>(0)?,
                                    row.get::<_, String>(1)?,
                                    row.get::<_, String>(2)?,
                                ))
                            })
                            .expect("query");
                        let mut count = 0;
                        for r in rows {
                            r.expect("read query row");
                            count += 1;
                        }
                        std::hint::black_box(count);
                    }
                    done_tx.send(()).expect("report reader completion");
                }
            });
            done_rx
                .recv()
                .expect("reader must initialize before measurement");
            workers.push(ReaderWorker {
                task_tx: Some(task_tx),
                done_rx,
                handle: Some(handle),
            });
        }
        Self { workers }
    }

    fn new_encrypted(path: &Path, key: &str, count: usize, queries_per_reader: usize) -> Self {
        let mut workers = Vec::with_capacity(count);
        for worker_idx in 0..count {
            let (task_tx, task_rx) = std::sync::mpsc::channel();
            let (done_tx, done_rx) = std::sync::mpsc::channel();
            let db_path = path.to_path_buf();
            let key_str = key.to_string();
            let handle = std::thread::spawn(move || {
                let storage =
                    SqliteStorage::open_with_key(db_path.to_str().expect("db path"), &key_str)
                        .expect("reader storage");
                done_tx.send(()).expect("report reader readiness");

                while task_rx.recv().is_ok() {
                    for i in 0..queries_per_reader {
                        let query_pattern = format!("item-{}", (worker_idx * 17 + i * 13) % 200);
                        let results = storage
                            .search_history(&query_pattern, 20)
                            .expect("reader history query");
                        std::hint::black_box(results);
                    }
                    done_tx.send(()).expect("report reader completion");
                }
            });
            done_rx
                .recv()
                .expect("reader must initialize before measurement");
            workers.push(ReaderWorker {
                task_tx: Some(task_tx),
                done_rx,
                handle: Some(handle),
            });
        }
        Self { workers }
    }

    fn execute_round(&self) {
        for w in &self.workers {
            if let Some(ref tx) = w.task_tx {
                tx.send(()).expect("reader command channel disconnected");
            }
        }
        for w in &self.workers {
            w.done_rx
                .recv()
                .expect("reader completion channel disconnected");
        }
    }
}

fn bench_sqlite_unencrypted_concurrency(c: &mut Criterion) {
    let mut group = c.benchmark_group("sqlite_unencrypted_concurrency");
    group.sample_size(10);
    group.measurement_time(Duration::from_secs(4));

    let temp_dir = TempDir::new().expect("temp dir");
    let db_path = temp_dir.path().join("bench_unencrypted.db");
    init_unencrypted_db(&db_path, INITIAL_ROWS);

    let _writer = BackgroundWriter::start_unencrypted(db_path.clone());

    for num_readers in [1, 2, 4, 8] {
        let pool = ReaderPool::new_unencrypted(&db_path, num_readers, QUERIES_PER_READER_PER_ITER);
        group.bench_with_input(
            BenchmarkId::new("readers_with_writer", num_readers),
            &num_readers,
            |b, _| {
                b.iter(|| {
                    pool.execute_round();
                });
            },
        );
    }
    group.finish();
}

fn bench_sqlite_encrypted_concurrency(c: &mut Criterion) {
    let mut group = c.benchmark_group("sqlite_encrypted_concurrency");
    group.sample_size(10);
    group.measurement_time(Duration::from_secs(4));

    let temp_dir = TempDir::new().expect("temp dir");
    let db_path = temp_dir.path().join("bench_encrypted.db");
    init_encrypted_db(&db_path, BENCH_KEY, INITIAL_ROWS);

    let _writer = BackgroundWriter::start_encrypted(db_path.clone(), BENCH_KEY.to_string());

    for num_readers in [1, 2, 4, 8] {
        let pool = ReaderPool::new_encrypted(
            &db_path,
            BENCH_KEY,
            num_readers,
            QUERIES_PER_READER_PER_ITER,
        );
        group.bench_with_input(
            BenchmarkId::new("readers_with_writer", num_readers),
            &num_readers,
            |b, _| {
                b.iter(|| {
                    pool.execute_round();
                });
            },
        );
    }
    group.finish();
}

fn bench_sqlite_idle_vs_active_writer(c: &mut Criterion) {
    let mut group = c.benchmark_group("sqlite_idle_vs_active_writer");
    group.sample_size(10);
    group.measurement_time(Duration::from_secs(4));

    // Unencrypted idle
    {
        let temp_dir = TempDir::new().expect("temp dir");
        let db_path = temp_dir.path().join("unencrypted_idle.db");
        init_unencrypted_db(&db_path, INITIAL_ROWS);
        let pool = ReaderPool::new_unencrypted(&db_path, 4, QUERIES_PER_READER_PER_ITER);
        group.bench_function("unencrypted_4_readers_idle", |b| {
            b.iter(|| pool.execute_round());
        });
    }

    // Unencrypted with active writer
    {
        let temp_dir = TempDir::new().expect("temp dir");
        let db_path = temp_dir.path().join("unencrypted_active.db");
        init_unencrypted_db(&db_path, INITIAL_ROWS);
        let _writer = BackgroundWriter::start_unencrypted(db_path.clone());
        let pool = ReaderPool::new_unencrypted(&db_path, 4, QUERIES_PER_READER_PER_ITER);
        group.bench_function("unencrypted_4_readers_with_writer", |b| {
            b.iter(|| pool.execute_round());
        });
    }

    // Encrypted idle
    {
        let temp_dir = TempDir::new().expect("temp dir");
        let db_path = temp_dir.path().join("encrypted_idle.db");
        init_encrypted_db(&db_path, BENCH_KEY, INITIAL_ROWS);
        let pool = ReaderPool::new_encrypted(&db_path, BENCH_KEY, 4, QUERIES_PER_READER_PER_ITER);
        group.bench_function("encrypted_4_readers_idle", |b| {
            b.iter(|| pool.execute_round());
        });
    }

    // Encrypted with active writer
    {
        let temp_dir = TempDir::new().expect("temp dir");
        let db_path = temp_dir.path().join("encrypted_active.db");
        init_encrypted_db(&db_path, BENCH_KEY, INITIAL_ROWS);
        let _writer = BackgroundWriter::start_encrypted(db_path.clone(), BENCH_KEY.to_string());
        let pool = ReaderPool::new_encrypted(&db_path, BENCH_KEY, 4, QUERIES_PER_READER_PER_ITER);
        group.bench_function("encrypted_4_readers_with_writer", |b| {
            b.iter(|| pool.execute_round());
        });
    }

    group.finish();
}

criterion_group!(
    benches,
    bench_sqlite_unencrypted_concurrency,
    bench_sqlite_encrypted_concurrency,
    bench_sqlite_idle_vs_active_writer,
);
criterion_main!(benches);
