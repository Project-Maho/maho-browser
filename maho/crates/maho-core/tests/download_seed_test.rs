// Regression tests for the downloads seed/cleanup path (v2 plan D4).
//
// D4: on startup, MahoCore seeds persisted downloads from SQLite. A completed row
// that was persisted with an empty filename AND no file_path (the "AVIF" stale-row
// symptom — a broken row from a mid-development build/crash) can never be repaired,
// so it must be dropped during seeding while valid rows survive.

use maho_core::maho_core::MahoCore;
use maho_storage::sqlite::SqliteStorage;
use tempfile::tempdir;

#[test]
fn seed_drops_empty_completed_download_rows_but_keeps_valid() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let dir = tempdir().expect("temp dir");
    let db_path = dir.path().join("maho.db");
    let db_path_str = db_path.to_str().unwrap();

    {
        let sqlite = SqliteStorage::open(db_path_str).expect("open sqlite");
        // Broken stale row: completed, empty filename, no file_path -> must be dropped.
        sqlite
            .save_download(
                "empty1",
                "",
                "https://example.com/a.avif",
                0,
                0,
                "completed",
                None,
                "2025-01-01T00:00:00",
                None,
                None,
                None,
            )
            .expect("save empty row");
        // Valid completed row -> must survive.
        sqlite
            .save_download(
                "ok1",
                "file.zip",
                "https://example.com/file.zip",
                100,
                100,
                "completed",
                Some("/tmp/file.zip"),
                "2025-01-02T00:00:00",
                Some("2025-01-02T00:01:00"),
                Some("application/zip"),
                Some("guid-1"),
            )
            .expect("save valid row");
    }

    let core = MahoCore::new().with_storage(db_path_str);
    let vms = core.get_download_view_models();

    assert_eq!(
        vms.len(),
        1,
        "empty completed row must be dropped on seed; only the valid row remains"
    );
    assert_eq!(vms[0].filename, "file.zip");

    // The broken row must also be gone from storage (deleted, not merely un-seeded).
    let remaining = SqliteStorage::open(db_path_str)
        .expect("reopen sqlite")
        .load_downloads()
        .expect("load");
    assert!(
        remaining.iter().all(|row| row.0 != "empty1"),
        "empty1 must be deleted from the downloads table"
    );
}

#[test]
fn seed_keeps_completed_row_that_has_filename_only() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let dir = tempdir().expect("temp dir");
    let db_path = dir.path().join("maho.db");
    let db_path_str = db_path.to_str().unwrap();

    {
        let sqlite = SqliteStorage::open(db_path_str).expect("open sqlite");
        // Has filename but no file_path -> not "completely broken", must survive
        // (D4 only drops rows where BOTH filename and file_path are empty).
        sqlite
            .save_download(
                "named",
                "report.pdf",
                "https://example.com/report.pdf",
                10,
                10,
                "completed",
                None,
                "2025-01-03T00:00:00",
                None,
                None,
                None,
            )
            .expect("save named row");
    }

    let core = MahoCore::new().with_storage(db_path_str);
    let vms = core.get_download_view_models();
    assert_eq!(vms.len(), 1);
    assert_eq!(vms[0].filename, "report.pdf");
}
