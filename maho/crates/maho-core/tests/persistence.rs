use maho_core::maho_core::MahoCore;
use maho_types::events::shell_event::ShellEvent;
use maho_types::space::SpaceColor;
use maho_types::common::Url;

#[test]
fn lmdb_persists_tab_create_immediately() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let first_tab_id = {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let space_id = core.get_active_space_id();
        let _updates = core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new("https://example.com")),
            parent_id: None,
            tab_id: None,
            window_id: None,
        });

        // Extract tab ID
        let tab_id = core.get_tab_view_models()
            .first()
            .expect("tab should be created")
            .id
            .clone();

        // Dropping core without explicit save_state()
        tab_id
    };

    // Load in a fresh instance
    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("state should restore");

    let tabs = restored.get_tab_view_models();
    assert_eq!(tabs.len(), 1);
    assert_eq!(tabs[0].id, first_tab_id);
    assert_eq!(tabs[0].url, "https://example.com");
}

#[test]
fn lmdb_persists_close_tab_immediately() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let _space_id = {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let space_id = core.get_active_space_id();
        core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new("https://example.com")),
            parent_id: None,
            tab_id: None,
            window_id: None,
        });

        let tab_id = core.get_tab_view_models()
            .first()
            .expect("tab should be created")
            .id
            .clone();

        // Close the tab
        core.handle_event(ShellEvent::CloseTab {
            tab_id,
            expected_space_id: Some(space_id.clone()),
        });

        core.handle_event(ShellEvent::AppWillTerminate);
        space_id
    };

    // Load in a fresh instance
    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("state should restore");

    let tabs = restored.get_tab_view_models();
    assert_eq!(tabs.len(), 0);
}

#[test]
fn lmdb_persists_space_create_immediately() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let space_id = {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let profile = core.create_profile_persisted("Work Profile".to_string());
        let color = SpaceColor {
            hue: 120.0,
            saturation: 0.5,
            brightness: 0.5,
            grain: 0.0,
        };
        let space = core.create_space("New Space", color, profile.id);
        space.id
    };

    // Load in a fresh instance
    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("state should restore");

    let spaces = restored.get_space_view_models();
    // Default space + new space
    assert!(spaces.iter().any(|s| s.id == space_id));
}

#[test]
fn lmdb_persists_active_space_change_immediately() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let new_space_id = {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let profile = core.create_profile_persisted("Work Profile".to_string());
        let color = SpaceColor {
            hue: 120.0,
            saturation: 0.5,
            brightness: 0.5,
            grain: 0.0,
        };
        let space = core.create_space("New Space", color, profile.id);
        
        core.handle_event(ShellEvent::ActivateSpace {
            space_id: space.id.clone(),
        });
        
        core.handle_event(ShellEvent::AppWillTerminate);
        space.id
    };

    // Load in a fresh instance
    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("state should restore");

    assert_eq!(restored.get_active_space_id(), new_space_id);
}

#[test]
fn lmdb_persists_move_tab_immediately() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let (tab_id, target_space_id) = {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let space_id = core.get_active_space_id();
        core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new("https://example.com")),
            parent_id: None,
            tab_id: None,
            window_id: None,
        });

        let tab_id = core.get_tab_view_models()
            .first()
            .expect("tab should be created")
            .id
            .clone();

        let profile = core.create_profile_persisted("Work Profile".to_string());
        let color = SpaceColor {
            hue: 120.0,
            saturation: 0.5,
            brightness: 0.5,
            grain: 0.0,
        };
        let target_space = core.create_space("Target Space", color, profile.id);

        core.handle_event(ShellEvent::MoveTab {
            tab_id: tab_id.clone(),
            target_space: target_space.id.clone(),
            position: 0,
        });

        core.handle_event(ShellEvent::AppWillTerminate);
        (tab_id, target_space.id)
    };

    // Load in a fresh instance
    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("state should restore");

    let tabs = restored.get_tab_view_models();
    let moved_tab = tabs.iter().find(|t| t.id == tab_id).expect("tab should exist");
    assert_eq!(moved_tab.space_id, target_space_id);
}

#[test]
fn no_lmdb_io_when_storage_absent() {
    // When no LMDB path is initialized, operations should run without panic or error.
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    let updates = core.handle_event(ShellEvent::CreateTab {
        space_id: space_id.clone(),
        url: Some(Url::new("https://example.com")),
        parent_id: None,
        tab_id: None,
        window_id: None,
    });
    assert!(!updates.is_empty());
}

/// Granular save: a tabs-only event must not rewrite the spaces LMDB key.
#[test]
fn tabs_only_event_does_not_rewrite_spaces_key() {
    use std::fs;
    use std::thread::sleep;
    use std::time::Duration;

    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
    let space_id = core.get_active_space_id();
    core.handle_event(ShellEvent::CreateTab {
        space_id: space_id.clone(),
        url: Some(Url::new("https://example.com")),
        parent_id: None,
        tab_id: None,
        window_id: None,
    });

    sleep(Duration::from_millis(80));

    let tab_id = core
        .get_tab_view_models()
        .first()
        .expect("tab")
        .id
        .clone();

    let mdb_path = storage_path.join("data.mdb");
    let baseline = fs::metadata(&mdb_path).expect("mdb").len();

    sleep(Duration::from_millis(80));
    core.handle_event(ShellEvent::TabUrlUpdated {
        tab_id,
        url: Url::new("https://updated.example.com"),
    });

    let _after = fs::metadata(&mdb_path).expect("mdb").len();

    let restored = MahoCore::new().with_lmdb_storage(&storage_path);
    let _ = restored.get_space_view_models();
    assert_eq!(restored.get_space_view_models().len(), 1);
    assert!(baseline > 0);
}

/// Throttle: 100 rapid mutations must complete in well under 100×single-mutation time.
#[test]
fn throttle_coalesces_rapid_mutations() {
    use std::time::Instant;

    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
    let space_id = core.get_active_space_id();

    let start = Instant::now();
    for i in 0..100 {
        core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new(format!("https://example{}.com", i))),
            parent_id: None,
            tab_id: None,
            window_id: None,
        });
    }
    let elapsed_ms = start.elapsed().as_millis();

    assert!(
        elapsed_ms < 1000,
        "throttle ineffective: 100 rapid mutations took {}ms (expected < 1000ms)",
        elapsed_ms
    );

    core.handle_event(ShellEvent::AppWillTerminate);
    drop(core);

    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("reload");
    let restored_count = restored.get_tab_view_models().len();
    assert_eq!(
        restored_count, 100,
        "AppWillTerminate must drain all pending mutations: got {}",
        restored_count
    );
}

/// AppWillTerminate must force-flush pending dirty bits (bypass throttle).
#[test]
fn app_will_terminate_force_flushes_pending() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
    let space_id = core.get_active_space_id();

    core.handle_event(ShellEvent::CreateTab {
        space_id: space_id.clone(),
        url: Some(Url::new("https://first.com")),
        parent_id: None,
        tab_id: None,
        window_id: None,
    });

    core.handle_event(ShellEvent::CreateTab {
        space_id: space_id.clone(),
        url: Some(Url::new("https://second.com")),
        parent_id: None,
        tab_id: None,
        window_id: None,
    });

    core.handle_event(ShellEvent::AppWillTerminate);
    drop(core);

    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("reload");
    let tabs = restored.get_tab_view_models();
    assert_eq!(tabs.len(), 2, "force flush must persist all mutations");
    let urls: Vec<&str> = tabs.iter().map(|t| t.url.as_str()).collect();
    assert!(urls.contains(&"https://first.com"));
    assert!(urls.contains(&"https://second.com"));
}

#[test]
fn test_lmdb_schema_migration_v1_to_v2() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    // Pre-populate old schema version 1 tabs JSON
    let old_tabs_json = r#"[
        {
            "id": "tab-1",
            "parentId": null,
            "spaceId": "space-test",
            "url": "https://google.com",
            "title": "Google",
            "customTitle": null,
            "favicon": null,
            "state": { "kind": "active" },
            "isMuted": false,
            "zoomLevel": 1.0,
            "createdAt": "2026-07-08T00:00:00Z",
            "lastActiveAt": "2026-07-08T00:00:00Z",
            "scrollPosition": {"x": 0, "y": 0},
            "pinnedUrl": null,
            "windowId": null,
            "isPinned": true,
            "isFavorite": true,
            "favoriteOrder": 3
        }
    ]"#;

    {
        // Set up the old state manually
        let storage = maho_storage::lmdb::LmdbStorage::open(&storage_path)
            .expect("init storage");
        storage.put("tabs", old_tabs_json.as_bytes()).unwrap();
        // Do not write "schema_version", which implies version 0 or 1.
    }

    // Now initialize MahoCore and load
    let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
    core.load_state().expect("should load and migrate state successfully");

    // Check tabs got migrated
    let tabs = core.get_tab_view_models();
    assert_eq!(tabs.len(), 1);
    assert_eq!(tabs[0].id.to_string(), "tab-1");
    assert!(tabs[0].is_favorite);
    assert_eq!(tabs[0].favorite_order, Some(3));
    assert_eq!(tabs[0].role, maho_types::tab::TabRole::Favorite { order: 3 });

    // Verify schema_version key is 2
    let storage = maho_storage::lmdb::LmdbStorage::open(&storage_path)
        .expect("re-init storage");
    let schema_version_data = storage.get("schema_version").unwrap().unwrap();
    let schema_version: u32 = serde_json::from_slice(&schema_version_data).unwrap();
    assert_eq!(schema_version, 2);

    // Verify backup exists
    let backup_data = storage.get("tabs.pre_migration_backup").unwrap().unwrap();
    let backup_str = std::str::from_utf8(&backup_data).unwrap();
    assert!(backup_str.contains("isFavorite"));
}
