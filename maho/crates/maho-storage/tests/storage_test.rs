use maho_storage::lmdb::LmdbStorage;
use maho_storage::sqlite::SqliteStorage;

// === SQLite Tests ===

#[test]
fn sqlite_open_in_memory() {
    SqliteStorage::open_in_memory().unwrap();
}

#[test]
fn sqlite_add_and_search_history() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.add_history_entry("https://rust-lang.org", "Rust")
        .unwrap();
    let results = db.search_history("Rust", 10).unwrap();
    assert_eq!(results.len(), 1);
    assert_eq!(results[0].0, "https://rust-lang.org");
    assert_eq!(results[0].1, "Rust");
}

#[test]
fn sqlite_search_history_by_url() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.add_history_entry("https://rust-lang.org", "Rust")
        .unwrap();
    let results = db.search_history("rust-lang", 10).unwrap();
    assert_eq!(results.len(), 1);
}

#[test]
fn sqlite_search_history_empty() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.add_history_entry("https://example.com", "Example")
        .unwrap();
    let results = db.search_history("nonexistent", 10).unwrap();
    assert!(results.is_empty());
}

#[test]
fn sqlite_search_history_limit() {
    let db = SqliteStorage::open_in_memory().unwrap();
    for i in 0..10 {
        db.add_history_entry(&format!("https://example.com/{i}"), &format!("Page {i}"))
            .unwrap();
    }
    let results = db.search_history("Page", 3).unwrap();
    assert_eq!(results.len(), 3);
}

#[test]
fn sqlite_get_setting_none() {
    let db = SqliteStorage::open_in_memory().unwrap();
    assert!(db.get_setting("missing").unwrap().is_none());
}

#[test]
fn sqlite_set_and_get_setting() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.set_setting("theme", "dark").unwrap();
    assert_eq!(db.get_setting("theme").unwrap().as_deref(), Some("dark"));
}

#[test]
fn sqlite_set_setting_overwrite() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.set_setting("theme", "light").unwrap();
    db.set_setting("theme", "dark").unwrap();
    assert_eq!(db.get_setting("theme").unwrap().as_deref(), Some("dark"));
}

#[test]
fn sqlite_multiple_history_entries() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.add_history_entry("https://a.com", "Site A").unwrap();
    db.add_history_entry("https://b.com", "Site B").unwrap();
    db.add_history_entry("https://c.com", "Site C").unwrap();
    let all = db.search_history("Site", 100).unwrap();
    assert_eq!(all.len(), 3);
}

#[test]
fn sqlite_save_and_load_notes() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.save_note(
        "n1",
        Some("tab1"),
        Some("https://example.com"),
        "My note",
        "2025-01-01T00:00:00",
        "2025-01-01T00:00:00",
    )
    .unwrap();
    db.save_note(
        "n2",
        None,
        None,
        "Standalone note",
        "2025-01-02T00:00:00",
        "2025-01-02T00:00:00",
    )
    .unwrap();
    let notes = db.load_notes().unwrap();
    assert_eq!(notes.len(), 2);
    assert_eq!(notes[0].0, "n2");
    assert_eq!(notes[0].3, "Standalone note");
    assert_eq!(notes[1].0, "n1");
    assert_eq!(notes[1].1, Some("tab1".to_string()));
    assert_eq!(notes[1].2, Some("https://example.com".to_string()));
}

#[test]
fn sqlite_delete_note() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.save_note(
        "n1",
        None,
        None,
        "content",
        "2025-01-01T00:00:00",
        "2025-01-01T00:00:00",
    )
    .unwrap();
    assert!(db.delete_note("n1").unwrap());
    assert!(!db.delete_note("n1").unwrap());
    assert!(db.load_notes().unwrap().is_empty());
}

#[test]
fn sqlite_update_note_content() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.save_note(
        "n1",
        None,
        None,
        "old content",
        "2025-01-01T00:00:00",
        "2025-01-01T00:00:00",
    )
    .unwrap();
    db.update_note_content("n1", "new content", "2025-01-02T00:00:00")
        .unwrap();
    let notes = db.load_notes().unwrap();
    assert_eq!(notes[0].3, "new content");
    assert_eq!(notes[0].5, "2025-01-02T00:00:00");
}

#[test]
fn sqlite_save_and_load_boosts() {
    let db = SqliteStorage::open_in_memory().unwrap();
    let boost1 = maho_types::boost::Boost {
        id: maho_types::identifiers::BoostId::new("b1"),
        domain: "example.com".to_string(),
        name: "My Boost".to_string(),
        color: maho_types::boost::ColorBoost {
            dot_pos: maho_types::boost::Point { x: 0.76, y: 0.66 },
            dot_distance: 0.0,
            dot_angle_deg: 45.0,
            secondary_dot_pos: maho_types::boost::Point { x: 0.5, y: 0.81 },
            secondary_dot_angle_deg_delta: 10.0,
            magic_theme: true,
            color_boost_enabled: true,
            smart_invert: false,
            contrast: 0.9,
            brightness: 1.1,
            saturation: 1.2,
        },
        typography: maho_types::boost::TypographyBoost {
            font_family: "Helvetica".to_string(),
            case_mode: maho_types::boost::CaseMode::Capitalize,
            size_mode: maho_types::boost::SizeMode::K125,
        },
        zap_selectors: vec![".ad-banner".to_string()],
        custom_css: "body { background: blue; }".to_string(),
        change_was_made: false,
        created_at: maho_types::common::DateTime::now(),
        updated_at: maho_types::common::DateTime::now(),
    };
    db.save_boost(&boost1).unwrap();

    let boosts = db.load_boosts().unwrap();
    assert_eq!(boosts.len(), 1);
    let loaded = &boosts[0];
    assert_eq!(loaded.id.as_ref(), "b1");
    assert_eq!(loaded.domain, "example.com");
    assert_eq!(loaded.name, "My Boost");
    assert!(loaded.color.color_boost_enabled);
    assert_eq!(loaded.color.dot_angle_deg, 45.0);
    assert_eq!(loaded.typography.font_family, "Helvetica");
    assert_eq!(loaded.typography.case_mode, maho_types::boost::CaseMode::Capitalize);
    assert_eq!(loaded.typography.size_mode, maho_types::boost::SizeMode::K125);
    assert_eq!(loaded.zap_selectors, vec![".ad-banner".to_string()]);
    assert_eq!(loaded.custom_css, "body { background: blue; }");
    
    // Test active boost state
    assert_eq!(db.get_active_boost("example.com").unwrap(), None);
    db.set_active_boost("example.com", Some("b1")).unwrap();
    assert_eq!(db.get_active_boost("example.com").unwrap().as_deref(), Some("b1"));
}

#[test]
fn sqlite_delete_boost() {
    let db = SqliteStorage::open_in_memory().unwrap();
    let boost1 = maho_types::boost::Boost {
        id: maho_types::identifiers::BoostId::new("b1"),
        domain: "example.com".to_string(),
        name: "My Boost".to_string(),
        color: maho_types::boost::ColorBoost {
            dot_pos: maho_types::boost::Point { x: 0.76, y: 0.66 },
            dot_distance: 0.0,
            dot_angle_deg: 45.0,
            secondary_dot_pos: maho_types::boost::Point { x: 0.5, y: 0.81 },
            secondary_dot_angle_deg_delta: 10.0,
            magic_theme: true,
            color_boost_enabled: true,
            smart_invert: false,
            contrast: 0.9,
            brightness: 1.1,
            saturation: 1.2,
        },
        typography: maho_types::boost::TypographyBoost {
            font_family: "Helvetica".to_string(),
            case_mode: maho_types::boost::CaseMode::Capitalize,
            size_mode: maho_types::boost::SizeMode::K125,
        },
        zap_selectors: vec![],
        custom_css: "".to_string(),
        change_was_made: false,
        created_at: maho_types::common::DateTime::now(),
        updated_at: maho_types::common::DateTime::now(),
    };
    db.save_boost(&boost1).unwrap();
    assert!(db.delete_boost("b1").unwrap());
    assert!(!db.delete_boost("b1").unwrap());
    assert!(db.load_boosts().unwrap().is_empty());
}

#[test]
fn sqlite_save_and_load_permissions() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.save_permission("https://example.com", "camera", "allow")
        .unwrap();
    db.save_permission("https://example.com", "microphone", "deny")
        .unwrap();
    db.save_permission("https://other.com", "location", "ask")
        .unwrap();
    let perms = db.load_permissions().unwrap();
    assert_eq!(perms.len(), 3);
    assert_eq!(perms[0].0, "https://example.com");
    assert_eq!(perms[0].1, "camera");
    assert_eq!(perms[0].2, "allow");
}

#[test]
fn sqlite_delete_permissions_for_origin() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.save_permission("https://example.com", "camera", "allow")
        .unwrap();
    db.save_permission("https://example.com", "microphone", "deny")
        .unwrap();
    db.save_permission("https://other.com", "location", "ask")
        .unwrap();
    db.delete_permissions_for_origin("https://example.com")
        .unwrap();
    let perms = db.load_permissions().unwrap();
    assert_eq!(perms.len(), 1);
    assert_eq!(perms[0].0, "https://other.com");
}

#[test]
fn sqlite_save_and_load_atc_rules() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.save_atc_rule(
        "r1",
        "Close old tabs",
        Some("space1"),
        Some("*.social.com/*"),
        Some(24),
        Some(50),
        true,
    )
    .unwrap();
    db.save_atc_rule("r2", "Limit news", None, None, None, Some(10), false)
        .unwrap();
    let rules = db.load_atc_rules().unwrap();
    assert_eq!(rules.len(), 2);
    let r1 = rules.iter().find(|r| r.0 == "r1").unwrap();
    assert_eq!(r1.1, "Close old tabs");
    assert_eq!(r1.2, Some("space1".to_string()));
    assert_eq!(r1.4, Some(24));
    assert_eq!(r1.5, Some(50));
    assert!(r1.6);
}

#[test]
fn sqlite_delete_atc_rule() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.save_atc_rule("r1", "Rule", None, None, None, None, true)
        .unwrap();
    assert!(db.delete_atc_rule("r1").unwrap());
    assert!(!db.delete_atc_rule("r1").unwrap());
    assert!(db.load_atc_rules().unwrap().is_empty());
}

#[test]
fn sqlite_save_and_load_recent_searches() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.save_search("rust programming").unwrap();
    db.save_search("sqlite tutorial").unwrap();
    db.save_search("cargo workspace").unwrap();
    let searches = db.load_recent_searches(2).unwrap();
    assert_eq!(searches.len(), 2);
    assert_eq!(searches[0].0, "cargo workspace");
    assert_eq!(searches[1].0, "sqlite tutorial");
}

#[test]
fn sqlite_clear_search_history() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.save_search("query1").unwrap();
    db.save_search("query2").unwrap();
    db.clear_search_history().unwrap();
    assert!(db.load_recent_searches(100).unwrap().is_empty());
}

#[test]
fn sqlite_delete_history_entry_by_url() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.add_history_entry("https://example.com", "Example")
        .unwrap();
    db.add_history_entry("https://other.com", "Other").unwrap();

    let deleted = db
        .delete_history_entry_by_url("https://example.com")
        .unwrap();
    assert!(deleted);

    let remaining = db.search_history("", 100).unwrap();
    assert_eq!(remaining.len(), 1);
    assert_eq!(remaining[0].0, "https://other.com");
}

#[test]
fn sqlite_delete_history_entry_by_url_nonexistent() {
    let db = SqliteStorage::open_in_memory().unwrap();
    let deleted = db
        .delete_history_entry_by_url("https://nonexistent.com")
        .unwrap();
    assert!(!deleted);
}

#[test]
fn sqlite_save_and_get_zoom() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.save_zoom("https://example.com", 1.5).unwrap();
    assert_eq!(
        db.get_zoom_for_site("https://example.com").unwrap(),
        Some(1.5)
    );
    assert_eq!(db.get_zoom_for_site("https://other.com").unwrap(), None);
    db.save_zoom("https://example.com", 2.0).unwrap();
    assert_eq!(
        db.get_zoom_for_site("https://example.com").unwrap(),
        Some(2.0)
    );
}

#[test]
fn sqlite_delete_zoom() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.save_zoom("https://example.com", 1.25).unwrap();
    assert!(db.delete_zoom("https://example.com").unwrap());
    assert!(!db.delete_zoom("https://example.com").unwrap());
    assert_eq!(db.get_zoom_for_site("https://example.com").unwrap(), None);
}

#[test]
fn sqlite_save_and_load_downloads() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.save_download(
        "d1",
        "file.zip",
        "https://example.com/file.zip",
        1000,
        500,
        "downloading",
        None,
        "2025-01-01T00:00:00",
    )
    .unwrap();
    db.save_download(
        "d2",
        "doc.pdf",
        "https://example.com/doc.pdf",
        2000,
        2000,
        "completed",
        Some("/tmp/doc.pdf"),
        "2025-01-02T00:00:00",
    )
    .unwrap();
    let downloads = db.load_downloads().unwrap();
    assert_eq!(downloads.len(), 2);
    assert_eq!(downloads[0].0, "d2");
    assert_eq!(downloads[0].1, "doc.pdf");
    assert_eq!(downloads[0].3, 2000);
    assert_eq!(downloads[0].5, "completed");
    assert_eq!(downloads[0].6, Some("/tmp/doc.pdf".to_string()));
    assert_eq!(downloads[1].0, "d1");
    assert_eq!(downloads[1].4, 500);
}

#[test]
fn sqlite_update_download_progress() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.save_download(
        "d1",
        "file.zip",
        "https://example.com/file.zip",
        1000,
        0,
        "downloading",
        None,
        "2025-01-01T00:00:00",
    )
    .unwrap();
    db.update_download_progress("d1", 750, "downloading")
        .unwrap();
    let downloads = db.load_downloads().unwrap();
    assert_eq!(downloads[0].4, 750);
    db.update_download_progress("d1", 1000, "completed")
        .unwrap();
    let downloads = db.load_downloads().unwrap();
    assert_eq!(downloads[0].4, 1000);
    assert_eq!(downloads[0].5, "completed");
}

#[test]
fn sqlite_delete_download() {
    let db = SqliteStorage::open_in_memory().unwrap();
    db.save_download(
        "d1",
        "file.zip",
        "https://example.com/file.zip",
        1000,
        1000,
        "completed",
        Some("/tmp/file.zip"),
        "2025-01-01T00:00:00",
    )
    .unwrap();
    assert!(db.delete_download("d1").unwrap());
    assert!(!db.delete_download("d1").unwrap());
    assert!(db.load_downloads().unwrap().is_empty());
}

// === LMDB Tests ===

#[test]
fn lmdb_open() {
    let dir = tempfile::tempdir().unwrap();
    LmdbStorage::open(dir.path()).unwrap();
}

#[test]
fn lmdb_put_and_get() {
    let dir = tempfile::tempdir().unwrap();
    let db = LmdbStorage::open(dir.path()).unwrap();
    db.put("key1", b"value1").unwrap();
    let val = db.get("key1").unwrap().unwrap();
    assert_eq!(val, b"value1");
}

#[test]
fn lmdb_get_missing_key() {
    let dir = tempfile::tempdir().unwrap();
    let db = LmdbStorage::open(dir.path()).unwrap();
    assert!(db.get("missing").unwrap().is_none());
}

#[test]
fn lmdb_delete_existing() {
    let dir = tempfile::tempdir().unwrap();
    let db = LmdbStorage::open(dir.path()).unwrap();
    db.put("key", b"val").unwrap();
    assert!(db.delete("key").unwrap());
    assert!(db.get("key").unwrap().is_none());
}

#[test]
fn lmdb_delete_missing() {
    let dir = tempfile::tempdir().unwrap();
    let db = LmdbStorage::open(dir.path()).unwrap();
    assert!(!db.delete("missing").unwrap());
}

#[test]
fn lmdb_overwrite() {
    let dir = tempfile::tempdir().unwrap();
    let db = LmdbStorage::open(dir.path()).unwrap();
    db.put("key", b"first").unwrap();
    db.put("key", b"second").unwrap();
    let val = db.get("key").unwrap().unwrap();
    assert_eq!(val, b"second");
}

#[test]
fn lmdb_multiple_keys() {
    let dir = tempfile::tempdir().unwrap();
    let db = LmdbStorage::open(dir.path()).unwrap();
    db.put("a", b"1").unwrap();
    db.put("b", b"2").unwrap();
    db.put("c", b"3").unwrap();
    assert_eq!(db.get("a").unwrap().unwrap(), b"1");
    assert_eq!(db.get("b").unwrap().unwrap(), b"2");
    assert_eq!(db.get("c").unwrap().unwrap(), b"3");
}

#[test]
fn sqlite_boosts_migration_self_healing() {
    let dir = tempfile::tempdir().unwrap();
    let db_path = dir.path().join("test.db");
    let db_path_str = db_path.to_str().unwrap();

    {
        let conn = rusqlite::Connection::open(db_path_str).unwrap();
        conn.execute_batch(
            "CREATE TABLE boosts (
                id TEXT PRIMARY KEY,
                url_pattern TEXT NOT NULL,
                custom_css TEXT,
                custom_js TEXT,
                enabled INTEGER NOT NULL DEFAULT 0
            );
            CREATE INDEX idx_boosts_url_pattern ON boosts(url_pattern);
            INSERT INTO boosts (id, url_pattern, custom_css, custom_js, enabled)
            VALUES ('old_id', '*://example.com/*', 'body { color: red; }', 'console.log()', 1);"
        ).unwrap();
    }

    let storage = SqliteStorage::open(db_path_str).unwrap();

    let boosts = storage.load_boosts().unwrap();
    assert!(boosts.is_empty());

    let boost1 = maho_types::boost::Boost {
        id: maho_types::identifiers::BoostId::new("b1"),
        domain: "example.com".to_string(),
        name: "My Boost".to_string(),
        color: maho_types::boost::ColorBoost {
            dot_pos: maho_types::boost::Point { x: 0.76, y: 0.66 },
            dot_distance: 0.0,
            dot_angle_deg: 45.0,
            secondary_dot_pos: maho_types::boost::Point { x: 0.5, y: 0.81 },
            secondary_dot_angle_deg_delta: 10.0,
            magic_theme: true,
            color_boost_enabled: true,
            smart_invert: false,
            contrast: 0.9,
            brightness: 1.1,
            saturation: 1.2,
        },
        typography: maho_types::boost::TypographyBoost {
            font_family: "Helvetica".to_string(),
            case_mode: maho_types::boost::CaseMode::Capitalize,
            size_mode: maho_types::boost::SizeMode::K125,
        },
        zap_selectors: vec![".ad-banner".to_string()],
        custom_css: "body { background: blue; }".to_string(),
        change_was_made: false,
        created_at: maho_types::common::DateTime::now(),
        updated_at: maho_types::common::DateTime::now(),
    };
    storage.save_boost(&boost1).unwrap();
    let loaded = storage.load_boosts().unwrap();
    assert_eq!(loaded.len(), 1);
    assert_eq!(loaded[0].domain, "example.com");
}
