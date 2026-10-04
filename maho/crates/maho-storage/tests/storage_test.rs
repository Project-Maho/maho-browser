use maho_storage::lmdb::LmdbStorage;
use maho_storage::sqlite::{MemoryInsertParams, SqliteStorage};

fn test_key() -> String {
    uuid::Uuid::new_v4().to_string()
}

fn sqlite_store() -> SqliteStorage {
    SqliteStorage::open_in_memory_with_key(&test_key()).unwrap()
}

// === SQLite Tests ===

#[test]
fn sqlite_open_in_memory() {
    sqlite_store();
}

#[test]
fn sqlite_add_and_search_history() {
    let db = sqlite_store();
    db.add_history_entry("https://rust-lang.org", "Rust")
        .unwrap();
    let results = db.search_history("Rust", 10).unwrap();
    assert_eq!(results.len(), 1);
    assert_eq!(results[0].0, "https://rust-lang.org");
    assert_eq!(results[0].1, "Rust");
}

#[test]
fn sqlite_search_history_by_url() {
    let db = sqlite_store();
    db.add_history_entry("https://rust-lang.org", "Rust")
        .unwrap();
    let results = db.search_history("rust-lang", 10).unwrap();
    assert_eq!(results.len(), 1);
}

#[test]
fn sqlite_search_history_empty() {
    let db = sqlite_store();
    db.add_history_entry("https://example.com", "Example")
        .unwrap();
    let results = db.search_history("nonexistent", 10).unwrap();
    assert!(results.is_empty());
}

#[test]
fn sqlite_search_history_limit() {
    let db = sqlite_store();
    for i in 0..10 {
        db.add_history_entry(&format!("https://example.com/{i}"), &format!("Page {i}"))
            .unwrap();
    }
    let results = db.search_history("Page", 3).unwrap();
    assert_eq!(results.len(), 3);
}

#[test]
fn sqlite_get_setting_none() {
    let db = sqlite_store();
    assert!(db.get_setting("missing").unwrap().is_none());
}

#[test]
fn sqlite_set_and_get_setting() {
    let db = sqlite_store();
    db.set_setting("theme", "dark").unwrap();
    assert_eq!(db.get_setting("theme").unwrap().as_deref(), Some("dark"));
}

#[test]
fn sqlite_set_setting_overwrite() {
    let db = sqlite_store();
    db.set_setting("theme", "light").unwrap();
    db.set_setting("theme", "dark").unwrap();
    assert_eq!(db.get_setting("theme").unwrap().as_deref(), Some("dark"));
}

#[test]
fn sqlite_multiple_history_entries() {
    let db = sqlite_store();
    db.add_history_entry("https://a.com", "Site A").unwrap();
    db.add_history_entry("https://b.com", "Site B").unwrap();
    db.add_history_entry("https://c.com", "Site C").unwrap();
    let all = db.search_history("Site", 100).unwrap();
    assert_eq!(all.len(), 3);
}

#[test]
fn sqlite_save_and_load_notes() {
    let db = sqlite_store();
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
    let db = sqlite_store();
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
    let db = sqlite_store();
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
    let db = sqlite_store();
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
    assert_eq!(
        loaded.typography.case_mode,
        maho_types::boost::CaseMode::Capitalize
    );
    assert_eq!(
        loaded.typography.size_mode,
        maho_types::boost::SizeMode::K125
    );
    assert_eq!(loaded.zap_selectors, vec![".ad-banner".to_string()]);
    assert_eq!(loaded.custom_css, "body { background: blue; }");

    // Test active boost state
    assert_eq!(db.get_active_boost("example.com").unwrap(), None);
    db.set_active_boost("example.com", Some("b1")).unwrap();
    assert_eq!(
        db.get_active_boost("example.com").unwrap().as_deref(),
        Some("b1")
    );
    let boosts_with_state = db.load_boosts_with_active_state().unwrap();
    assert_eq!(boosts_with_state.len(), 1);
    assert_eq!(boosts_with_state[0].0.id.as_ref(), "b1");
    assert_eq!(boosts_with_state[0].1.as_deref(), Some("b1"));
}

#[test]
fn sqlite_delete_boost() {
    let db = sqlite_store();
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
    let db = sqlite_store();
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
    let db = sqlite_store();
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
    let db = sqlite_store();
    db.save_atc_rule(
        "r1",
        "Close old tabs",
        Some("space1"),
        Some("*.social.com/*"),
        Some(24),
        Some(50),
        true,
        None,
    )
    .unwrap();
    db.save_atc_rule("r2", "Limit news", None, None, None, Some(10), false, None)
        .unwrap();
    db.save_atc_rule(
        "r3",
        "*.github.com",
        Some("space2"),
        None,
        None,
        None,
        true,
        Some("glob"),
    )
    .unwrap();
    let rules = db.load_atc_rules().unwrap();
    assert_eq!(rules.len(), 3);
    let r1 = rules.iter().find(|r| r.0 == "r1").unwrap();
    assert_eq!(r1.1, "Close old tabs");
    assert_eq!(r1.2, Some("space1".to_string()));
    assert_eq!(r1.4, Some(24));
    assert_eq!(r1.5, Some(50));
    assert!(r1.6);
    assert_eq!(r1.7, None);
    // Traffic rules round-trip their match type through the match_type column.
    let r3 = rules.iter().find(|r| r.0 == "r3").unwrap();
    assert_eq!(r3.1, "*.github.com");
    assert_eq!(r3.7, Some("glob".to_string()));
}

#[test]
fn sqlite_delete_atc_rule() {
    let db = sqlite_store();
    db.save_atc_rule("r1", "Rule", None, None, None, None, true, None)
        .unwrap();
    assert!(db.delete_atc_rule("r1").unwrap());
    assert!(!db.delete_atc_rule("r1").unwrap());
    assert!(db.load_atc_rules().unwrap().is_empty());
}

#[test]
fn sqlite_save_and_load_recent_searches() {
    let db = sqlite_store();
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
    let db = sqlite_store();
    db.save_search("query1").unwrap();
    db.save_search("query2").unwrap();
    db.clear_search_history().unwrap();
    assert!(db.load_recent_searches(100).unwrap().is_empty());
}

#[test]
fn sqlite_delete_history_entry_by_url() {
    let db = sqlite_store();
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
    let db = sqlite_store();
    let deleted = db
        .delete_history_entry_by_url("https://nonexistent.com")
        .unwrap();
    assert!(!deleted);
}

#[test]
fn sqlite_save_and_get_zoom() {
    let db = sqlite_store();
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
    let db = sqlite_store();
    db.save_zoom("https://example.com", 1.25).unwrap();
    assert!(db.delete_zoom("https://example.com").unwrap());
    assert!(!db.delete_zoom("https://example.com").unwrap());
    assert_eq!(db.get_zoom_for_site("https://example.com").unwrap(), None);
}

#[test]
fn sqlite_save_and_load_downloads() {
    let db = sqlite_store();
    db.save_download(
        "d1",
        "file.zip",
        "https://example.com/file.zip",
        1000,
        500,
        "downloading",
        None,
        "2025-01-01T00:00:00",
        None,
        None,
        Some("test-guid-1"),
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
        None,
        None,
        None,
    )
    .unwrap();
    let downloads = db.load_downloads().unwrap();
    assert_eq!(downloads.len(), 2);
    assert_eq!(downloads[0].0, "d2");
    assert_eq!(downloads[0].1, "doc.pdf");
    assert_eq!(downloads[0].3, 2000);
    assert_eq!(downloads[0].5, "completed");
    assert_eq!(downloads[0].6, Some("/tmp/doc.pdf".to_string()));
    assert_eq!(downloads[0].10, None);
    assert_eq!(downloads[1].0, "d1");
    assert_eq!(downloads[1].4, 500);
    assert_eq!(downloads[1].10, Some("test-guid-1".to_string()));
}

#[test]
fn sqlite_update_download_progress() {
    let db = sqlite_store();
    db.save_download(
        "d1",
        "file.zip",
        "https://example.com/file.zip",
        1000,
        0,
        "downloading",
        None,
        "2025-01-01T00:00:00",
        None,
        None,
        None,
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
    let db = sqlite_store();
    db.save_download(
        "d1",
        "file.zip",
        "https://example.com/file.zip",
        1000,
        1000,
        "completed",
        Some("/tmp/file.zip"),
        "2025-01-01T00:00:00",
        None,
        None,
        None,
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
    let key = test_key();

    {
        let conn = rusqlite::Connection::open(db_path_str).unwrap();
        conn.pragma_update(None, "key", &key).unwrap();
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
            VALUES ('old_id', '*://example.com/*', 'body { color: red; }', 'console.log()', 1);",
        )
        .unwrap();
    }

    let storage = SqliteStorage::open_with_key(db_path_str, &key).unwrap();

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

#[test]
fn sqlite_conversation_crud() {
    let db = sqlite_store();

    // Create conversation
    db.create_conversation("c1", Some("Title 1"), Some("space1"), Some("model1"))
        .unwrap();

    // List conversations
    let list = db
        .list_conversations(maho_types::chat::ConversationListState::Active, 10)
        .unwrap();
    assert_eq!(list.len(), 1);
    assert_eq!(list[0].id, "c1");
    assert_eq!(list[0].title.as_deref(), Some("Title 1"));
    assert_eq!(list[0].space_id.as_deref(), Some("space1"));
    assert_eq!(list[0].model.as_deref(), Some("model1"));

    // Rename conversation
    assert!(db.rename_conversation("c1", "New Title").unwrap());
    let list = db
        .list_conversations(maho_types::chat::ConversationListState::Active, 10)
        .unwrap();
    assert_eq!(list[0].title.as_deref(), Some("New Title"));

    // Delete conversation
    assert!(db.delete_conversation("c1").unwrap());
    let list = db
        .list_conversations(maho_types::chat::ConversationListState::Active, 10)
        .unwrap();
    assert!(list.is_empty());
}

#[test]
fn sqlite_conversation_turns() {
    let db = sqlite_store();
    db.create_conversation("c1", Some("Title 1"), Some("space1"), Some("model1"))
        .unwrap();

    // Insert conversation turns
    db.insert_conversation_turn("t1", "c1", "user", "Hello", Some("https://example.com"))
        .unwrap();
    db.insert_conversation_turn("t2", "c1", "assistant", "Hi there!", None)
        .unwrap();

    // Load turns
    let turns = db.get_conversation_messages("c1").unwrap();
    assert_eq!(turns.len(), 2);
    assert_eq!(turns[0].id, "t1");
    assert_eq!(turns[0].role, "user");
    assert_eq!(turns[0].content, "Hello");
    assert_eq!(turns[0].url_context.as_deref(), Some("https://example.com"));

    assert_eq!(turns[1].id, "t2");
    assert_eq!(turns[1].role, "assistant");
    assert_eq!(turns[1].content, "Hi there!");
    assert_eq!(turns[1].url_context, None);
}

#[test]
fn sqlite_conversation_cascade_delete() {
    let db = sqlite_store();
    db.create_conversation("c1", Some("Title 1"), Some("space1"), Some("model1"))
        .unwrap();
    db.insert_conversation_turn("t1", "c1", "user", "Hello", None)
        .unwrap();

    // Verify turns exist
    let turns = db.get_conversation_messages("c1").unwrap();
    assert_eq!(turns.len(), 1);

    // Delete conversation
    db.delete_conversation("c1").unwrap();

    // Verify turns are deleted (cascade)
    let turns = db.get_conversation_messages("c1").unwrap();
    assert!(turns.is_empty());
}

#[test]
fn sqlite_ai_profiles_crud() {
    let db = sqlite_store();
    use maho_types::ai::AiProfile;

    let profile = AiProfile {
        id: "test-prof".to_string(),
        name: "Test Profile".to_string(),
        system_prompt: "You are a helpful assistant".to_string(),
        preferred_model: Some("gpt-4".to_string()),
        tools: vec!["search".to_string()],
        mcp_servers: vec!["server1".to_string()],
        is_default: false,
        created_at: "2026-07-09T00:00:00Z".to_string(),
        updated_at: "2026-07-09T00:00:00Z".to_string(),
    };

    // Create
    db.create_ai_profile(&profile).unwrap();

    // Read
    let loaded = db.get_ai_profile("test-prof").unwrap().unwrap();
    assert_eq!(loaded.name, "Test Profile");
    assert_eq!(loaded.system_prompt, "You are a helpful assistant");
    assert_eq!(loaded.preferred_model.as_deref(), Some("gpt-4"));
    assert_eq!(loaded.tools, vec!["search".to_string()]);
    assert_eq!(loaded.mcp_servers, vec!["server1".to_string()]);
    assert!(!loaded.is_default);

    // List
    let list = db.list_ai_profiles().unwrap();
    // Default profiles should also be loaded
    assert!(list.len() > 1);
    assert!(list.iter().any(|p| p.id == "test-prof"));

    // Update
    let mut updated = loaded;
    updated.name = "Updated Name".to_string();
    updated.system_prompt = "Updated Prompt".to_string();
    db.update_ai_profile(&updated).unwrap();

    let loaded_updated = db.get_ai_profile("test-prof").unwrap().unwrap();
    assert_eq!(loaded_updated.name, "Updated Name");
    assert_eq!(loaded_updated.system_prompt, "Updated Prompt");

    // Default Profile check
    let default_prof = db.get_default_ai_profile().unwrap().unwrap();
    assert_eq!(default_prof.id, "blank");
    assert!(default_prof.is_default);

    // Delete
    db.delete_ai_profile("test-prof").unwrap();
    assert!(db.get_ai_profile("test-prof").unwrap().is_none());
}

#[test]
fn sqlite_ai_workspaces_crud() {
    let db = sqlite_store();
    use maho_types::ai::AiWorkspace;

    let ws = AiWorkspace {
        id: "ws-1".to_string(),
        name: "My Workspace".to_string(),
        profile_id: Some("blank".to_string()),
        space_id: Some("space-x".to_string()),
        workspace_root: Some("/tmp".to_string()),
        created_at: "2026-07-09T00:00:00Z".to_string(),
        updated_at: "2026-07-09T00:00:00Z".to_string(),
    };

    // Create
    db.create_workspace(&ws).unwrap();

    // Read
    let loaded = db.get_workspace("ws-1").unwrap().unwrap();
    assert_eq!(loaded.name, "My Workspace");
    assert_eq!(loaded.profile_id.as_deref(), Some("blank"));
    assert_eq!(loaded.space_id.as_deref(), Some("space-x"));
    assert_eq!(loaded.workspace_root.as_deref(), Some("/tmp"));

    // Query by space_id
    let by_space = db.get_workspace_by_space("space-x").unwrap().unwrap();
    assert_eq!(by_space.id, "ws-1");

    // List
    let list = db.list_workspaces().unwrap();
    assert_eq!(list.len(), 1);

    // Update
    let mut updated = loaded;
    updated.name = "New Workspace Name".to_string();
    db.update_workspace(&updated).unwrap();

    let loaded_updated = db.get_workspace("ws-1").unwrap().unwrap();
    assert_eq!(loaded_updated.name, "New Workspace Name");

    // Delete
    db.delete_workspace("ws-1").unwrap();
    assert!(db.get_workspace("ws-1").unwrap().is_none());
}

#[test]
fn sqlite_ai_mcp_servers_and_cli_tools_crud() {
    let db = sqlite_store();
    use maho_types::ai::{AiCliTool, AiMcpServer, AiWorkspace, McpTransport};

    // First create a workspace
    let ws = AiWorkspace {
        id: "ws-1".to_string(),
        name: "My Workspace".to_string(),
        profile_id: Some("blank".to_string()),
        space_id: None,
        workspace_root: None,
        created_at: "2026-07-09T00:00:00Z".to_string(),
        updated_at: "2026-07-09T00:00:00Z".to_string(),
    };
    db.create_workspace(&ws).unwrap();

    // 1. MCP Servers
    let mcp = AiMcpServer {
        id: "mcp-1".to_string(),
        workspace_id: "ws-1".to_string(),
        name: "server-x".to_string(),
        transport: McpTransport::Stdio,
        command: Some("npx".to_string()),
        url: None,
        auth_keychain_id: Some("key-id".to_string()),
        trusted: true,
        trusted_tools: Some(vec!["tool-a".to_string()]),
        timeout_ms: 30000,
        output_cap_bytes: 5000,
        socket_path: None,
        created_at: "2026-07-09T00:00:00Z".to_string(),
        updated_at: "2026-07-09T00:00:00Z".to_string(),
    };

    db.create_mcp_server(&mcp).unwrap();

    let loaded_mcp = db.get_mcp_server("mcp-1").unwrap().unwrap();
    assert_eq!(loaded_mcp.name, "server-x");
    assert_eq!(loaded_mcp.transport, McpTransport::Stdio);
    assert_eq!(loaded_mcp.command.as_deref(), Some("npx"));
    assert_eq!(loaded_mcp.auth_keychain_id.as_deref(), Some("key-id"));
    assert!(loaded_mcp.trusted);
    assert_eq!(
        loaded_mcp.trusted_tools.as_ref().unwrap(),
        &vec!["tool-a".to_string()]
    );
    assert_eq!(loaded_mcp.timeout_ms, 30000);
    assert_eq!(loaded_mcp.output_cap_bytes, 5000);
    assert_eq!(loaded_mcp.socket_path, None);

    // Test Uds transport
    let mcp_uds = AiMcpServer {
        id: "mcp-uds".to_string(),
        workspace_id: "ws-1".to_string(),
        name: "server-uds".to_string(),
        transport: McpTransport::Uds,
        command: None,
        url: None,
        auth_keychain_id: None,
        trusted: false,
        trusted_tools: None,
        timeout_ms: 20000,
        output_cap_bytes: 4000,
        socket_path: Some("/tmp/test.sock".to_string()),
        created_at: "2026-07-09T00:00:00Z".to_string(),
        updated_at: "2026-07-09T00:00:00Z".to_string(),
    };
    db.create_mcp_server(&mcp_uds).unwrap();
    let loaded_uds = db.get_mcp_server("mcp-uds").unwrap().unwrap();
    assert_eq!(loaded_uds.transport, McpTransport::Uds);
    assert_eq!(loaded_uds.socket_path.as_deref(), Some("/tmp/test.sock"));

    let list_mcp = db.list_mcp_servers("ws-1").unwrap();
    assert_eq!(list_mcp.len(), 2);

    // Update
    let mut updated_mcp = loaded_mcp;
    updated_mcp.trusted = false;
    db.update_mcp_server(&updated_mcp).unwrap();

    let loaded_updated_mcp = db.get_mcp_server("mcp-1").unwrap().unwrap();
    assert!(!loaded_updated_mcp.trusted);

    // 2. CLI Tools
    let tool = AiCliTool {
        id: "cli-1".to_string(),
        workspace_id: "ws-1".to_string(),
        name: "rg".to_string(),
        description: "ripgrep".to_string(),
        command_template: "rg {pattern}".to_string(),
        parameters_schema: serde_json::json!({"type": "object"}),
        sensitive: true,
        timeout_ms: 10000,
        output_cap_bytes: 2000,
        working_directory: Some("/tmp".to_string()),
        created_at: "2026-07-09T00:00:00Z".to_string(),
        updated_at: "2026-07-09T00:00:00Z".to_string(),
    };

    db.create_cli_tool(&tool).unwrap();

    let loaded_tool = db.get_cli_tool("cli-1").unwrap().unwrap();
    assert_eq!(loaded_tool.name, "rg");
    assert_eq!(loaded_tool.command_template, "rg {pattern}");
    assert!(loaded_tool.sensitive);
    assert_eq!(loaded_tool.timeout_ms, 10000);
    assert_eq!(loaded_tool.output_cap_bytes, 2000);
    assert_eq!(loaded_tool.working_directory.as_deref(), Some("/tmp"));

    let list_tools = db.list_cli_tools("ws-1").unwrap();
    assert_eq!(list_tools.len(), 1);

    // Update
    let mut updated_tool = loaded_tool;
    updated_tool.sensitive = false;
    db.update_cli_tool(&updated_tool).unwrap();

    let loaded_updated_tool = db.get_cli_tool("cli-1").unwrap().unwrap();
    assert!(!loaded_updated_tool.sensitive);

    // Delete workspace should cascade delete mcp servers and cli tools
    db.delete_workspace("ws-1").unwrap();
    assert!(db.get_mcp_server("mcp-1").unwrap().is_none());
    assert!(db.get_cli_tool("cli-1").unwrap().is_none());
}

#[test]
fn test_autofill_migration_idempotent() {
    let dir = tempfile::tempdir().unwrap();
    let db_path = dir.path().join("test.db");
    let db_path_str = db_path.to_str().unwrap();
    let key = test_key();

    // 1. Create connection manually and set up OLD schema
    {
        let conn = rusqlite::Connection::open(db_path_str).unwrap();
        conn.pragma_update(None, "key", &key).unwrap();
        conn.execute_batch(
            "CREATE TABLE autofill_addresses (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                street TEXT NOT NULL,
                city TEXT NOT NULL,
                state TEXT NOT NULL,
                zip TEXT NOT NULL,
                country TEXT NOT NULL,
                phone TEXT,
                email TEXT
            );
            CREATE TABLE autofill_payments (
                id TEXT PRIMARY KEY,
                card_name TEXT NOT NULL,
                last_four TEXT NOT NULL,
                expiry TEXT NOT NULL
            );
            INSERT INTO autofill_addresses (id, name, street, city, state, zip, country, phone, email)
            VALUES ('addr-old', 'Old Name', 'Old St', 'Old City', 'OS', '12345', 'US', '123-456', 'old@test.com');
            INSERT INTO autofill_payments (id, card_name, last_four, expiry)
            VALUES ('card-old', 'Old Card', '4321', '12/29');"
        ).unwrap();
    }

    // 2. Open via SqliteStorage (this runs initialize_schema -> migration)
    let storage = SqliteStorage::open_with_key(db_path_str, &key).unwrap();

    // Verify migrations added the columns and they default to NULL / None
    let addresses = storage.load_autofill_addresses().unwrap();
    assert_eq!(addresses.len(), 1);
    assert_eq!(addresses[0].0, "addr-old");
    assert_eq!(addresses[0].9, None); // address_line2

    let payments = storage.load_autofill_payments().unwrap();
    assert_eq!(payments.len(), 1);
    assert_eq!(payments[0].0, "card-old");
    assert_eq!(payments[0].4, None); // card_network

    // 3. Save new values using new columns
    storage
        .save_autofill_address(maho_storage::sqlite::AutofillAddressParams {
            id: "addr-new",
            name: "New Name",
            street: "New St",
            city: "New City",
            state: "NS",
            zip: "54321",
            country: "US",
            phone: Some("987-654"),
            email: Some("new@test.com"),
            address_line2: Some("Apt 4B"),
        })
        .unwrap();

    storage
        .save_autofill_payment("card-new", "New Card", "1111", "01/30", Some("visa"))
        .unwrap();

    // Verify round-trip
    let addresses = storage.load_autofill_addresses().unwrap();
    let new_addr = addresses.iter().find(|a| a.0 == "addr-new").unwrap();
    assert_eq!(new_addr.9, Some("Apt 4B".to_string()));

    let payments = storage.load_autofill_payments().unwrap();
    let new_pay = payments.iter().find(|p| p.0 == "card-new").unwrap();
    assert_eq!(new_pay.4, Some("visa".to_string()));

    // 4. Run migrations again to verify idempotence
    let storage = SqliteStorage::open_with_key(db_path_str, &key).unwrap();

    let addresses = storage.load_autofill_addresses().unwrap();
    let new_addr = addresses.iter().find(|a| a.0 == "addr-new").unwrap();
    assert_eq!(new_addr.9, Some("Apt 4B".to_string()));
}

#[test]
fn sqlite_search_memories_fts_ordering() {
    let db = sqlite_store();

    // Add facts with different BM25 relevance for query "Rust"
    db.insert_memory(MemoryInsertParams {
        id: "f1",
        fact: "Rust is a systems programming language focused on safety and speed.",
        source: "src1",
        session_id: None,
        categories: "[]",
        importance: 0.8,
        metadata: None,
    })
    .unwrap();
    db.insert_memory(MemoryInsertParams {
        id: "f2",
        fact: "We write software using many tools.",
        source: "src2",
        session_id: None,
        categories: "[]",
        importance: 0.5,
        metadata: None,
    })
    .unwrap();
    db.insert_memory(MemoryInsertParams {
        id: "f3",
        fact: "Rust programming is very cool and fast.",
        source: "src3",
        session_id: None,
        categories: "[]",
        importance: 0.9,
        metadata: None,
    })
    .unwrap();

    // Search for "Rust"
    let results = db.search_memories_fts("Rust", 5).unwrap();
    assert_eq!(results.len(), 2);

    // Check that we got the relevant items
    let matched_ids: Vec<String> = results.iter().map(|(id, _, _)| id.clone()).collect();
    assert!(matched_ids.contains(&"f1".to_string()));
    assert!(matched_ids.contains(&"f3".to_string()));
}

#[test]
fn sqlite_search_notes_fts_ordering() {
    let db = sqlite_store();

    db.save_note(
        "n1",
        None,
        None,
        "Rust programming language",
        "2025-01-01",
        "2025-01-01",
    )
    .unwrap();
    db.save_note(
        "n2",
        None,
        None,
        "Random other notes",
        "2025-01-02",
        "2025-01-02",
    )
    .unwrap();
    db.save_note(
        "n3",
        None,
        None,
        "Learning Rust is fun",
        "2025-01-03",
        "2025-01-03",
    )
    .unwrap();

    db.reindex_all_notes_fts(&[
        ("n1".to_string(), "Rust programming language".to_string()),
        ("n2".to_string(), "Random other notes".to_string()),
        ("n3".to_string(), "Learning Rust is fun".to_string()),
    ])
    .unwrap();

    let results = db.search_notes_fts("Rust").unwrap();
    assert_eq!(results.len(), 2);

    let matched_ids: Vec<String> = results.iter().map(|(id, _)| id.clone()).collect();
    assert!(matched_ids.contains(&"n1".to_string()));
    assert!(matched_ids.contains(&"n3".to_string()));
}

// === Content Blocker Storage Tests ===

#[test]
fn sqlite_content_blocker_lists_round_trip() {
    let db = sqlite_store();
    let meta = maho_types::content_blocking::FilterListMetadata {
        id: "easylist".to_string(),
        name: "EasyList".to_string(),
        url: "https://easylist.to/easylist/easylist.txt".to_string(),
        enabled: true,
        rule_count: 1234,
        etag: Some("\"12345\"".to_string()),
        last_modified: Some("Wed, 21 Oct 2025 07:28:00 GMT".to_string()),
        sha256: Some("abc123def456".to_string()),
        last_attempt_timestamp: Some(1700000000),
        last_success_timestamp: Some(1700000000),
        failure_count: 0,
        last_status: Some(200),
        last_error: None,
    };
    db.save_content_blocker_list(&meta, Some("||example.com^"))
        .unwrap();

    let loaded = db.load_content_blocker_lists().unwrap();
    assert_eq!(loaded.len(), 1);
    assert_eq!(loaded[0].0, meta);
    assert_eq!(loaded[0].1, "||example.com^");

    // Deletion
    assert!(db.delete_content_blocker_list("easylist").unwrap());
    assert!(db.load_content_blocker_lists().unwrap().is_empty());
}

#[test]
fn sqlite_site_exceptions_round_trip() {
    let db = sqlite_store();
    db.save_site_exception("example.co.uk", 1700000100).unwrap();
    db.save_site_exception("sub.domain.org", 1700000200)
        .unwrap();

    let exceptions = db.load_site_exceptions().unwrap();
    assert_eq!(exceptions.len(), 2);
    assert_eq!(exceptions[0].key, "example.co.uk");
    assert_eq!(exceptions[1].key, "sub.domain.org");

    assert!(db.delete_site_exception("example.co.uk").unwrap());
    let remaining = db.load_site_exceptions().unwrap();
    assert_eq!(remaining.len(), 1);
    assert_eq!(remaining[0].key, "sub.domain.org");
}

#[test]
fn sqlite_content_blocker_state_blob_round_trip() {
    let db = sqlite_store();
    let cache_bytes = vec![1, 2, 3, 4, 5, 6, 7, 8];
    db.save_content_blocker_state(
        maho_types::content_blocking::ContentBlockingMode::Extension,
        42,
        Some(&cache_bytes),
        Some("v1.0"),
        Some("hash123"),
    )
    .unwrap();

    let state = db.load_content_blocker_state().unwrap().unwrap();
    assert_eq!(
        state.0,
        maho_types::content_blocking::ContentBlockingMode::Extension
    );
    assert_eq!(state.1, 42);
    assert_eq!(state.2.unwrap(), cache_bytes);
    assert_eq!(state.3.as_deref(), Some("v1.0"));
    assert_eq!(state.4.as_deref(), Some("hash123"));
}

// === Content Blocker Atomic Persistence (L2) ===

fn cb_meta(id: &str, name: &str, url: &str) -> maho_types::content_blocking::FilterListMetadata {
    maho_types::content_blocking::FilterListMetadata {
        id: id.to_string(),
        name: name.to_string(),
        url: url.to_string(),
        enabled: true,
        rule_count: 1,
        etag: None,
        last_modified: None,
        sha256: None,
        last_attempt_timestamp: None,
        last_success_timestamp: None,
        failure_count: 0,
        last_status: None,
        last_error: None,
    }
}

#[test]
fn sqlite_content_blocker_transaction_rollback_preserves_prior_state() {
    let db = sqlite_store();

    let mut baseline = cb_meta("easylist", "EasyList", "https://easylist.to/easylist.txt");
    baseline.rule_count = 3;
    baseline.sha256 = Some("goodhash".to_string());
    baseline.last_status = Some(200);
    db.save_content_blocker_list(&baseline, Some("||good.example^"))
        .unwrap();
    db.save_site_exception("keep.example", 111).unwrap();
    let good_cache = vec![9u8, 9, 9, 9];
    db.save_content_blocker_state(
        maho_types::content_blocking::ContentBlockingMode::Native,
        7,
        Some(&good_cache),
        Some("adblock-1.0"),
        Some("engine-good"),
    )
    .unwrap();

    let before_lists = db.load_content_blocker_lists().unwrap();
    let before_exceptions = db.load_site_exceptions().unwrap();
    let before_state = db.load_content_blocker_state().unwrap();

    let result: Result<(), maho_storage::StorageError> = db.content_blocker_transaction(|tx| {
        tx.clear_lists()?;
        let mut candidate = cb_meta("easylist", "Tampered", "https://evil.example/l.txt");
        candidate.rule_count = 999;
        candidate.sha256 = Some("badhash".to_string());
        tx.save_list(&candidate, Some("||bad.example^"))?;
        tx.save_exception("intruder.example", 222)?;
        tx.save_state(
            maho_types::content_blocking::ContentBlockingMode::Disabled,
            99,
            Some(&[1u8, 2, 3]),
            Some("adblock-bad"),
            Some("engine-bad"),
        )?;
        Err(maho_storage::StorageError::Other(
            "forced mid-transaction failure".to_string(),
        ))
    });

    assert!(
        result.is_err(),
        "closure returned Err so the call must surface it"
    );

    let after_lists = db.load_content_blocker_lists().unwrap();
    let after_exceptions = db.load_site_exceptions().unwrap();
    let after_state = db.load_content_blocker_state().unwrap();

    assert_eq!(
        after_lists, before_lists,
        "list metadata + raw content must be byte-identical after rollback"
    );
    assert_eq!(after_lists.len(), 1);
    assert_eq!(after_lists[0].1, "||good.example^", "raw content unchanged");
    assert_eq!(
        after_exceptions, before_exceptions,
        "exceptions must be unchanged after rollback"
    );
    assert!(
        after_exceptions.iter().all(|e| e.key != "intruder.example"),
        "candidate exception must not survive rollback"
    );
    assert_eq!(
        after_state, before_state,
        "engine state must be unchanged after rollback"
    );

    let (mode, generation, cache, version, hash) = after_state.unwrap();
    assert_eq!(
        mode,
        maho_types::content_blocking::ContentBlockingMode::Native
    );
    assert_eq!(generation, 7);
    assert_eq!(cache.unwrap(), good_cache);
    assert_eq!(version.as_deref(), Some("adblock-1.0"));
    assert_eq!(hash.as_deref(), Some("engine-good"));
}

/// The atomic snapshot replace clears prior lists/exceptions and commits the
/// new full set plus singleton engine state as one unit.
#[test]
fn sqlite_content_blocker_atomic_snapshot_round_trip() {
    let db = sqlite_store();

    db.save_content_blocker_list(
        &cb_meta("stale", "Stale", "https://stale.example/l.txt"),
        Some("||stale^"),
    )
    .unwrap();
    db.save_site_exception("stale.example", 1).unwrap();

    let lists = vec![
        (
            cb_meta("easylist", "EasyList", "https://easylist.to/easylist.txt"),
            "||ads.example^".to_string(),
        ),
        (
            cb_meta(
                "easyprivacy",
                "EasyPrivacy",
                "https://easylist.to/easyprivacy.txt",
            ),
            "||track.example^".to_string(),
        ),
    ];
    let exceptions = vec![maho_types::content_blocking::CanonicalSiteException {
        key: "trusted.example".to_string(),
        created_at: 500,
    }];
    let cache = vec![7u8; 16];
    let snapshot = maho_storage::sqlite::ContentBlockerSnapshot {
        mode: maho_types::content_blocking::ContentBlockingMode::Native,
        generation: 12,
        engine_cache: Some(&cache),
        engine_version: Some("adblock-9"),
        engine_hash: Some("agg-hash"),
        lists: &lists,
        exceptions: &exceptions,
    };
    db.replace_content_blocker_snapshot(&snapshot).unwrap();

    let mut loaded = db.load_content_blocker_lists().unwrap();
    loaded.sort_by(|a, b| a.0.id.cmp(&b.0.id));
    assert_eq!(loaded.len(), 2, "stale list must be replaced, not merged");
    assert_eq!(loaded[0].0.id, "easylist");
    assert_eq!(loaded[0].1, "||ads.example^");
    assert_eq!(loaded[1].0.id, "easyprivacy");
    assert_eq!(loaded[1].1, "||track.example^");

    let exc = db.load_site_exceptions().unwrap();
    assert_eq!(exc.len(), 1, "stale exception must be replaced");
    assert_eq!(exc[0].key, "trusted.example");

    let (mode, generation, c, v, h) = db.load_content_blocker_state().unwrap().unwrap();
    assert_eq!(
        mode,
        maho_types::content_blocking::ContentBlockingMode::Native
    );
    assert_eq!(generation, 12);
    assert_eq!(c.unwrap(), cache);
    assert_eq!(v.as_deref(), Some("adblock-9"));
    assert_eq!(h.as_deref(), Some("agg-hash"));
}

/// Exceptions persist independently of lists: writing/deleting a list never
/// touches exception rows (cascade-free), and vice versa.
#[test]
fn sqlite_content_blocker_exceptions_persist_independently() {
    let db = sqlite_store();

    db.save_site_exception("a.example", 1).unwrap();
    db.save_site_exception("b.example", 2).unwrap();
    assert!(
        db.load_content_blocker_lists().unwrap().is_empty(),
        "exceptions must not create list rows"
    );
    assert_eq!(db.load_site_exceptions().unwrap().len(), 2);

    db.save_content_blocker_list(
        &cb_meta("l1", "L1", "https://l1.example/l.txt"),
        Some("||x^"),
    )
    .unwrap();
    assert_eq!(
        db.load_site_exceptions().unwrap().len(),
        2,
        "adding a list must not touch exceptions"
    );
    assert_eq!(db.load_content_blocker_lists().unwrap().len(), 1);

    assert!(db.delete_content_blocker_list("l1").unwrap());
    assert_eq!(
        db.load_site_exceptions().unwrap().len(),
        2,
        "deleting a list must not cascade into exceptions"
    );
}

/// A user-added custom list with a multi-megabyte raw body and a 1 MiB engine
/// cache BLOB survives a snapshot round trip byte-identically.
#[test]
fn sqlite_content_blocker_custom_list_large_body_round_trip() {
    let db = sqlite_store();

    let big_body = "||ads.example^\n".repeat(200_000);
    let big_cache = vec![0xABu8; 1_048_576];
    let mut meta = cb_meta(
        "custom-user-list",
        "My Custom List",
        "https://mylists.example/custom.txt",
    );
    meta.rule_count = 200_000;

    let lists = vec![(meta.clone(), big_body.clone())];
    let exceptions: Vec<maho_types::content_blocking::CanonicalSiteException> = Vec::new();
    let snapshot = maho_storage::sqlite::ContentBlockerSnapshot {
        mode: maho_types::content_blocking::ContentBlockingMode::Native,
        generation: 3,
        engine_cache: Some(&big_cache),
        engine_version: Some("v-big"),
        engine_hash: Some("h-big"),
        lists: &lists,
        exceptions: &exceptions,
    };
    db.replace_content_blocker_snapshot(&snapshot).unwrap();

    let loaded = db.load_content_blocker_lists().unwrap();
    assert_eq!(loaded.len(), 1);
    assert_eq!(loaded[0].0, meta, "custom list metadata round trips");
    assert_eq!(loaded[0].1.len(), big_body.len());
    assert_eq!(loaded[0].1, big_body, "large raw body byte-identical");

    let (_, _, cache, _, _) = db.load_content_blocker_state().unwrap().unwrap();
    assert_eq!(cache.unwrap(), big_cache, "large BLOB byte-identical");
}

/// Tempfile-backed restart: an atomic snapshot committed to a real on-disk DB
/// is fully visible after reopening the database.
#[test]
fn sqlite_content_blocker_snapshot_tempfile_restart() {
    let dir = tempfile::tempdir().unwrap();
    let db_path = dir.path().join("cb.db");
    let db_path_str = db_path.to_str().unwrap();
    let key = test_key();

    let lists = vec![(
        cb_meta("easylist", "EasyList", "https://easylist.to/easylist.txt"),
        "||ads.example^".to_string(),
    )];
    let exceptions = vec![maho_types::content_blocking::CanonicalSiteException {
        key: "trusted.example".to_string(),
        created_at: 900,
    }];
    let cache = vec![3u8, 1, 4, 1, 5, 9, 2, 6];

    {
        let db = SqliteStorage::open_with_key(db_path_str, &key).unwrap();
        let snapshot = maho_storage::sqlite::ContentBlockerSnapshot {
            mode: maho_types::content_blocking::ContentBlockingMode::Native,
            generation: 21,
            engine_cache: Some(&cache),
            engine_version: Some("adblock-x"),
            engine_hash: Some("agg"),
            lists: &lists,
            exceptions: &exceptions,
        };
        db.replace_content_blocker_snapshot(&snapshot).unwrap();
    }

    let db2 = SqliteStorage::open_with_key(db_path_str, &key).unwrap();
    let loaded = db2.load_content_blocker_lists().unwrap();
    assert_eq!(loaded.len(), 1);
    assert_eq!(loaded[0].1, "||ads.example^");
    let exc = db2.load_site_exceptions().unwrap();
    assert_eq!(exc.len(), 1);
    assert_eq!(exc[0].key, "trusted.example");
    let (mode, generation, c, _, _) = db2.load_content_blocker_state().unwrap().unwrap();
    assert_eq!(
        mode,
        maho_types::content_blocking::ContentBlockingMode::Native
    );
    assert_eq!(generation, 21);
    assert_eq!(c.unwrap(), cache);
}

#[test]
fn sqlite_content_blocker_unknown_mode_loads_non_native() {
    let db = sqlite_store();
    db.save_content_blocker_state(
        maho_types::content_blocking::ContentBlockingMode::Unknown,
        5,
        None,
        None,
        None,
    )
    .unwrap();

    let (mode, _, _, _, _) = db.load_content_blocker_state().unwrap().unwrap();
    assert_eq!(
        mode,
        maho_types::content_blocking::ContentBlockingMode::Unknown
    );
    assert!(
        !mode.is_native(),
        "persisted Unknown must never load as native"
    );
}
