use maho_core::maho_core::MahoCore;
use maho_types::common::{Orientation, Url};
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::WindowId;

#[test]
fn test_split_view_persistence_roundtrip() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let _ = {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let space_id = core.get_active_space_id();
        let w = WindowId::new("window-1");

        // Create two tabs with window_id = Some(1)
        core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new("https://example1.com")),
            parent_id: None,
            tab_id: None,
            window_id: Some(1),
            is_private: false,
        });
        core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new("https://example2.com")),
            parent_id: None,
            tab_id: None,
            window_id: Some(1),
            is_private: false,
        });

        let tabs = core.get_tab_view_models();
        assert_eq!(tabs.len(), 2);
        let tab1 = tabs
            .iter()
            .find(|t| t.url == "https://example1.com")
            .unwrap()
            .id
            .clone();
        let tab2 = tabs
            .iter()
            .find(|t| t.url == "https://example2.com")
            .unwrap()
            .id
            .clone();

        // Create split in window-1
        core.handle_event(ShellEvent::CreateSplit {
            window_id: w.clone(),
            tab_ids: vec![tab1.clone(), tab2.clone()],
            orientation: Orientation::Horizontal,
            layout: None,
        });

        // Trigger manual flush/terminate to persist
        core.handle_event(ShellEvent::AppWillTerminate);
        println!("[DEBUG] First session: tab1={}, tab2={}", tab1, tab2);
        (tab1, tab2)
    };

    // Load state in a fresh instance
    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("state should restore");

    let w = WindowId::new("window-1");
    let config = restored
        .get_split_view_config(&w)
        .expect("split config should be restored");
    println!("[DEBUG] Restored config panes: {:?}", config.panes);
    assert_eq!(config.orientation, Orientation::Horizontal);
    assert_eq!(config.panes.len(), 2);

    // We expect the restored TabIds to be matched properly by the StablePaneIdentity mapping
    let restored_tabs = restored.get_tab_view_models();
    println!("[DEBUG] Restored tabs in tab_manager: {:?}", restored_tabs);
    assert_eq!(restored_tabs.len(), 2);
    let r_tab1 = restored_tabs
        .iter()
        .find(|t| t.url == "https://example1.com")
        .unwrap()
        .id
        .clone();
    let r_tab2 = restored_tabs
        .iter()
        .find(|t| t.url == "https://example2.com")
        .unwrap()
        .id
        .clone();

    assert_eq!(config.panes[0].tab_id, r_tab1);
    assert_eq!(config.panes[1].tab_id, r_tab2);
}

#[test]
fn test_split_view_persistence_ambiguity() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    // First session: Create two tabs and a split view config
    let _ = {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let space_id = core.get_active_space_id();
        let w = WindowId::new("window-1");

        core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new("https://example.com")),
            parent_id: None,
            tab_id: None,
            window_id: Some(1),
            is_private: false,
        });
        core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new("https://example.com")),
            parent_id: None,
            tab_id: None,
            window_id: Some(1),
            is_private: false,
        });

        let tabs = core.get_tab_view_models();
        assert_eq!(tabs.len(), 2);
        let tab1 = tabs[0].id.clone();
        let tab2 = tabs[1].id.clone();

        core.handle_event(ShellEvent::CreateSplit {
            window_id: w.clone(),
            tab_ids: vec![tab1, tab2],
            orientation: Orientation::Horizontal,
            layout: None,
        });

        core.handle_event(ShellEvent::AppWillTerminate);
    };

    // Hijack LMDB: Parse "tabs" JSON and force identical "created_at" timestamps
    {
        let db = maho_storage::lmdb::LmdbStorage::open(&storage_path).expect("open db");
        let tabs_bytes = db.get("tabs").expect("get tabs").expect("tabs exist");
        let mut tabs_val: serde_json::Value =
            serde_json::from_slice(&tabs_bytes).expect("parse tabs json");

        if let Some(arr) = tabs_val.as_array_mut() {
            for tab in arr {
                tab["created_at"] =
                    serde_json::Value::String("2026-07-09T08:00:00.000Z".to_string());
            }
        }

        let updated_tabs = serde_json::to_vec(&tabs_val).expect("serialize tabs");

        // Rewrite split_view_configs with matching stable pane identities
        // Tab creation time in micros: 2026-07-09T08:00:00Z = 1783584000000000
        let stable_panes = serde_json::json!([
            {
                "url": "https://example.com",
                "tab_creation_time_micros": 1783584000000000u64,
                "window_session_uuid": "1"
            },
            {
                "url": "https://example.com",
                "tab_creation_time_micros": 1783584000000000u64,
                "window_session_uuid": "1"
            }
        ]);

        let persist_map = serde_json::json!({
            "window-1": {
                "panes": stable_panes,
                "orientation": "Horizontal",
                "ratios": [0.5, 0.5]
            }
        });

        let split_json = serde_json::to_vec(&persist_map).expect("serialize split view");

        db.put("tabs", &updated_tabs).expect("put tabs");
        db.put("split_view_configs", &split_json)
            .expect("put split view");
    }

    // Load state in a fresh instance
    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("state should restore");

    let w = WindowId::new("window-1");
    // Should skip restoring due to ambiguity
    let config = restored.get_split_view_config(&w);
    assert!(
        config.is_none(),
        "Ambiguity should cause split view to be skipped cleanly"
    );
}

#[test]
fn test_split_view_persistence_no_match() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    // First session: Create one tab and a split view config
    let _ = {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let space_id = core.get_active_space_id();
        let w = WindowId::new("window-1");

        core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new("https://example.com")),
            parent_id: None,
            tab_id: None,
            window_id: Some(1),
            is_private: false,
        });

        let tabs = core.get_tab_view_models();
        let tab1 = tabs[0].id.clone();

        core.handle_event(ShellEvent::CreateSplit {
            window_id: w.clone(),
            tab_ids: vec![tab1.clone(), tab1],
            orientation: Orientation::Horizontal,
            layout: None,
        });

        core.handle_event(ShellEvent::AppWillTerminate);
    };

    // Hijack LMDB: Clear the "tabs" array entirely (simulating deletion/no-match)
    {
        let db = maho_storage::lmdb::LmdbStorage::open(&storage_path).expect("open db");
        let empty_tabs = serde_json::to_vec(&serde_json::Value::Array(vec![])).unwrap();
        db.put("tabs", &empty_tabs).expect("put empty tabs");
    }

    // Load state in a fresh instance
    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("state should restore");

    let w = WindowId::new("window-1");
    let config = restored.get_split_view_config(&w);
    assert!(
        config.is_none(),
        "No-match should cause split view to be skipped cleanly"
    );
}

#[test]
fn test_split_view_persistence_orientation_and_ratio() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let _ = {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let space_id = core.get_active_space_id();
        let w = WindowId::new("window-1");

        core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new("https://example1.com")),
            parent_id: None,
            tab_id: None,
            window_id: Some(1),
            is_private: false,
        });
        core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new("https://example2.com")),
            parent_id: None,
            tab_id: None,
            window_id: Some(1),
            is_private: false,
        });

        let tabs = core.get_tab_view_models();
        let tab1 = tabs
            .iter()
            .find(|t| t.url == "https://example1.com")
            .unwrap()
            .id
            .clone();
        let tab2 = tabs
            .iter()
            .find(|t| t.url == "https://example2.com")
            .unwrap()
            .id
            .clone();

        core.handle_event(ShellEvent::CreateSplit {
            window_id: w.clone(),
            tab_ids: vec![tab1.clone(), tab2.clone()],
            orientation: Orientation::Vertical,
            layout: None,
        });

        // Set custom ratio
        core.handle_event(ShellEvent::ResizeSplit {
            window_id: w.clone(),
            pane_id: tab1.0.clone(),
            ratio: 0.35,
        });

        core.handle_event(ShellEvent::AppWillTerminate);
    };

    // Load state in a fresh instance
    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("state should restore");

    let w = WindowId::new("window-1");
    let config = restored
        .get_split_view_config(&w)
        .expect("split config should be restored");
    assert_eq!(config.orientation, Orientation::Vertical);
    assert_eq!(config.panes.len(), 2);
    // Custom ratios: tab1 = 0.35, remaining tab2 = 0.65
    assert!((config.ratios[0] - 0.35).abs() < 1e-5);
    assert!((config.ratios[1] - 0.65).abs() < 1e-5);
}
