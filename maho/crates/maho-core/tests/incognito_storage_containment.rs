use maho_storage::lmdb::LmdbStorage;
use maho_storage::sqlite::SqliteStorage;
use tempfile::tempdir;

// Deterministic OTR canary tokens. If any of these ever appears in an exported
// named store, private-context data leaked into persistent storage. They mirror
// the five-row raw canary manifest used by the app-level task-15 lifecycle test.
const OTR_CANARIES: &[&str] = &[
    "OTR_DOWNLOAD_SECRET",
    "otr_session",
    "otr_local",
    "otr-cookie-canary-value",
    "otr-space-canary",
];

// Deterministic normal positives that MUST survive export (proves the selectors
// and the LMDB enumerator are actually connected, not blanket-empty).
const NORMAL_MARKERS: &[&str] = &[
    "Normal Alpha",
    "Normal Bookmark",
    "normal-setting-value",
    "https://normal.test/regular.bin",
    "space-normal-v1",
];

fn collect_exported_corpus(sqlite: &SqliteStorage, lmdb: &LmdbStorage) -> String {
    let mut corpus = String::new();

    for (url, title, visited) in sqlite.search_history("", 1000).unwrap() {
        corpus.push_str(&format!("{url}|{title}|{visited}\n"));
    }
    for (id, url, title, folder, created) in sqlite.get_all_bookmarks().unwrap() {
        corpus.push_str(&format!(
            "{id}|{url}|{title}|{}|{created}\n",
            folder.unwrap_or_default()
        ));
    }
    if let Some(v) = sqlite.get_setting("maho.normal.seed").unwrap() {
        corpus.push_str(&format!("setting:{v}\n"));
    }
    for row in sqlite.load_downloads().unwrap() {
        corpus.push_str(&format!("{}|{}|{}\n", row.0, row.1, row.2));
    }
    for row in sqlite.load_search_engines().unwrap() {
        corpus.push_str(&format!("{}|{}|{}\n", row.0, row.1, row.2));
    }
    for (key, value) in lmdb.snapshot_entries().unwrap() {
        corpus.push_str(&String::from_utf8_lossy(&key));
        corpus.push('|');
        corpus.push_str(&String::from_utf8_lossy(&value));
        corpus.push('\n');
    }

    corpus
}

#[test]
fn exported_named_stores_exclude_all_otr_values() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let dir = tempdir().expect("temp dir");
    let sqlite_path = dir.path().join("maho.db");
    let lmdb_dir = dir.path().join("maho-core.lmdb");

    let sqlite = SqliteStorage::open(sqlite_path.to_str().unwrap()).expect("open sqlite");
    sqlite
        .add_history_entry("https://normal.test/a", "Normal Alpha")
        .expect("history");
    sqlite
        .save_bookmark(
            "bm-1",
            "https://normal.test/b",
            "Normal Bookmark",
            None,
            "2024-01-01T00:00:00Z",
        )
        .expect("bookmark");
    sqlite
        .set_setting("maho.normal.seed", "normal-setting-value")
        .expect("setting");
    sqlite
        .save_download(
            "dl-1",
            "regular.bin",
            "https://normal.test/regular.bin",
            24,
            24,
            "complete",
            Some("/tmp/regular.bin"),
            "2024-01-01T00:00:00Z",
            None,
            None,
            None,
        )
        .expect("download");
    sqlite
        .save_search_engine(
            "se-1",
            "Normal Search",
            "https://normal.test/s?q={searchTerms}",
            Some("n"),
            None,
            true,
        )
        .expect("search engine");

    let lmdb = LmdbStorage::open(&lmdb_dir).expect("open lmdb");
    lmdb.put("tabs", br#"[{"id":"tab-a","scroll_position":0}]"#)
        .expect("tabs");
    lmdb.put("spaces", br#"[{"id":"space-normal-v1","name":"Normal"}]"#)
        .expect("spaces");
    lmdb.put("active_space_id", br#""space-normal-v1""#)
        .expect("active space");
    lmdb.put("schema_version", b"4").expect("schema version");

    let corpus = collect_exported_corpus(&sqlite, &lmdb);

    for marker in NORMAL_MARKERS {
        assert!(
            corpus.contains(marker),
            "normal positive {marker:?} missing from exported stores (selector disconnected?)"
        );
    }

    for canary in OTR_CANARIES {
        assert!(
            !corpus.contains(canary),
            "OTR canary {canary:?} leaked into an exported named store"
        );
    }
}
