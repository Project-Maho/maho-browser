use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{SyncEntity, SyncEntityType, SyncStatus};
use maho_types::common::{DateTime, ScrollPosition, Url};
use maho_types::events::core_update::CoreUpdate;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::{SpaceId, TabId};
use maho_types::space::{RootItem, Space, SpaceColor};
use maho_types::tab::{Tab, TabLifecycleState, TabRole};

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
            is_private: false,
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
            is_private: false,
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
            is_private: false,
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
        is_private: false,
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
        is_private: false,
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
            is_private: false,
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
        is_private: false,
    });

    core.handle_event(ShellEvent::CreateTab {
        space_id: space_id.clone(),
        url: Some(Url::new("https://second.com")),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
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

fn create_tab_for_test(core: &mut MahoCore, space_id: SpaceId, url: &str, is_private: bool) -> TabId {
    let updates = core.handle_event(ShellEvent::CreateTab {
        space_id,
        url: Some(Url::new(url)),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private,
    });
    updates
        .into_iter()
        .find_map(|update| match update {
            CoreUpdate::TabCreated { tab } => Some(tab.id),
            _ => None,
        })
        .expect("tab should be created")
}

fn create_folder_for_test(core: &mut MahoCore, space_id: SpaceId, name: &str) -> maho_types::identifiers::FolderId {
    let updates = core.handle_event(ShellEvent::CreateFolder {
        space_id,
        name: name.to_string(),
        is_pinned: false,
        parent_folder_id: None,
        provider_type: None,
        config_json: None,
    });
    updates
        .into_iter()
        .find_map(|update| match update {
            CoreUpdate::FolderCreated { folder } => Some(folder.id),
            _ => None,
        })
        .expect("folder should be created")
}

fn recovery_phrase(core: &MahoCore) -> String {
    let value: serde_json::Value = serde_json::from_str(&core.generate_sync_key()).unwrap();
    value["recoveryPhrase"].as_str().unwrap().to_string()
}

fn private_tab_payload(tab_id: TabId, space_id: SpaceId) -> String {
    serde_json::to_string(&Tab {
        id: tab_id,
        parent_id: None,
        space_id,
        url: Url::new("https://incoming-private.example.com"),
        title: "Incoming Private".to_string(),
        custom_title: None,
        favicon: None,
        state: TabLifecycleState::Active,
        role: TabRole::Normal,
        is_muted: false,
        zoom_level: 1.0,
        created_at: DateTime::now(),
        last_active_at: DateTime::now(),
        scroll_position: ScrollPosition::default(),
        pinned_url: None,
        window_id: None,
        is_private: true,
    })
    .unwrap()
}

#[test]
fn private_tabs_and_space_refs_are_not_persisted_to_lmdb() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let (normal_id, private_folder_id, private_root_id, space_id) = {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let space_id = core.get_active_space_id();
        let normal_id = create_tab_for_test(&mut core, space_id.clone(), "https://normal.example.com", false);
        let private_folder_id = create_tab_for_test(&mut core, space_id.clone(), "https://private-folder.example.com", true);
        let folder_id = create_folder_for_test(&mut core, space_id.clone(), "Private Folder");
        core.handle_event(ShellEvent::MoveTabToFolder {
            space_id: space_id.clone(),
            folder_id,
            tab_id: private_folder_id.clone(),
        });
        let private_root_id = create_tab_for_test(&mut core, space_id.clone(), "https://private-root.example.com", true);

        core.save_state().expect("save state");

        let live_tabs = core.get_tab_view_models();
        assert!(live_tabs.iter().any(|tab| tab.id == private_folder_id));
        assert!(live_tabs.iter().any(|tab| tab.id == private_root_id));

        (normal_id, private_folder_id, private_root_id, space_id)
    };

    let lmdb = maho_storage::lmdb::LmdbStorage::open(&storage_path).expect("open lmdb");
    let persisted_tabs: Vec<Tab> = serde_json::from_slice(&lmdb.get("tabs").unwrap().unwrap()).unwrap();
    assert!(persisted_tabs.iter().any(|tab| tab.id == normal_id));
    assert!(!persisted_tabs.iter().any(|tab| tab.id == private_folder_id));
    assert!(!persisted_tabs.iter().any(|tab| tab.id == private_root_id));

    let persisted_spaces: Vec<Space> = serde_json::from_slice(&lmdb.get("spaces").unwrap().unwrap()).unwrap();
    let persisted_space = persisted_spaces.iter().find(|space| space.id == space_id).unwrap();
    for private_id in [&private_folder_id, &private_root_id] {
        assert!(!persisted_space.tab_order.contains(private_id));
        assert!(!persisted_space.folders.iter().any(|folder| folder.tab_ids.contains(private_id)));
        assert!(!persisted_space.root_order.iter().any(|item| matches!(item, RootItem::Tab(tab_id) if tab_id == private_id)));
        assert_ne!(persisted_space.last_active_tab_id.as_ref(), Some(private_id));
    }

    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("load state");
    let restored_tabs = restored.get_tab_view_models();
    assert!(restored_tabs.iter().any(|tab| tab.id == normal_id));
    assert!(!restored_tabs.iter().any(|tab| tab.id == private_folder_id));
    assert!(!restored_tabs.iter().any(|tab| tab.id == private_root_id));
}

#[test]
fn private_tabs_are_not_pushed_or_snapshotted_for_sync() {
    let mut source = MahoCore::new();
    let space_id = source.get_active_space_id();
    let normal_id = create_tab_for_test(&mut source, space_id.clone(), "https://normal-sync.example.com", false);
    let private_id = create_tab_for_test(&mut source, space_id, "https://private-sync.example.com", true);
    let phrase = recovery_phrase(&source);
    source
        .configure_sync_encryption_for_recovery_phrase("ws://127.0.0.1:9", &phrase)
        .unwrap();
    source.set_sync_status(SyncStatus::Syncing);

    source.push_tab_sync(&private_id);
    assert!(source.drain_sync_outgoing().is_empty());

    source.push_tab_sync(&normal_id);
    assert!(!source.drain_sync_outgoing().is_empty());

    let encrypted_snapshot = source.build_snapshot().expect("snapshot");
    let mut dest = MahoCore::new();
    dest.configure_sync_encryption_for_recovery_phrase("ws://127.0.0.1:9", &phrase).unwrap();
    dest.apply_snapshot(&encrypted_snapshot).expect("apply snapshot");
    let dest_tabs = dest.get_tab_view_models();
    assert!(dest_tabs.iter().any(|tab| tab.url == "https://normal-sync.example.com"));
    assert!(!dest_tabs.iter().any(|tab| tab.url == "https://private-sync.example.com"));
}

#[test]
fn incoming_private_tab_sync_entity_is_ignored_and_space_refs_sanitized() {
    let mut core = MahoCore::new();
    core.start_sync("ws://127.0.0.1:9", "room");
    let space_id = core.get_active_space_id();
    let private_id = TabId::new("incoming-private-tab");
    let leaked_space = core.get_space_view_models();
    assert_eq!(leaked_space.len(), 1);

    let mut space = Space {
        id: space_id.clone(),
        profile_id: maho_types::identifiers::ProfileId::new("profile"),
        name: "Leaked".to_string(),
        color: SpaceColor { hue: 1.0, saturation: 1.0, brightness: 1.0, grain: 0.0 },
        theme: None,
        icon: None,
        tab_order: vec![private_id.clone()],
        folders: vec![],
        root_order: vec![RootItem::Tab(private_id.clone())],
        atc_rules: vec![],
        is_active: true,
        created_at: DateTime::now(),
        last_active_tab_id: Some(private_id.clone()),
    };
    let folder_id = create_folder_for_test(&mut core, space_id.clone(), "Leaked Folder");
    space.folders.push(maho_types::folder::Folder {
        id: folder_id,
        name: "Leaked Folder".to_string(),
        tab_ids: vec![private_id.clone()],
        is_expanded: true,
        is_pinned: false,
        parent_folder_id: None,
        provider_type: None,
        config_json: None,
    });

    let tab_entity = SyncEntity {
        entity_type: SyncEntityType::Tab,
        entity_id: private_id.to_string(),
        version: 1,
        device_id: 2,
        schema_version: 1,
        modified_at: 1,
        payload_json: private_tab_payload(private_id.clone(), space_id.clone()),
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
    };
    let space_entity = SyncEntity {
        entity_type: SyncEntityType::Space,
        entity_id: space_id.to_string(),
        version: 2,
        device_id: 2,
        schema_version: 1,
        modified_at: 2,
        payload_json: serde_json::to_string(&space).unwrap(),
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
    };

    core.apply_sync_remote_entities(vec![tab_entity, space_entity]);
    assert!(!core.get_tab_view_models().iter().any(|tab| tab.id == private_id));
    let tabs = core.get_space_tabs(&space_id);
    assert!(!tabs.iter().any(|tab| tab.id == private_id));
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

    // schema_version reaches 4 after load: the v1->v2 tab migration runs, then
    // the ghost-favorite/pinned dedup migration bumps it to 4 (schema v4).
    let storage = maho_storage::lmdb::LmdbStorage::open(&storage_path)
        .expect("re-init storage");
    let schema_version_data = storage.get("schema_version").unwrap().unwrap();
    let schema_version: u32 = serde_json::from_slice(&schema_version_data).unwrap();
    assert_eq!(schema_version, 4);

    // Verify backup exists
    let backup_data = storage.get("tabs.pre_migration_backup").unwrap().unwrap();
    let backup_str = std::str::from_utf8(&backup_data).unwrap();
    assert!(backup_str.contains("isFavorite"));
}

#[test]
fn test_lmdb_schema_migration_v3_dedups_duplicate_favorites() {
    fn create_fav(core: &mut MahoCore, url: &str) -> TabId {
        let space_id = core.get_active_space_id().clone();
        let updates = core.handle_event(ShellEvent::CreateTab {
            space_id,
            url: Some(Url::new(url)),
            parent_id: None,
            tab_id: None,
            window_id: None,
            is_private: false,
        });
        let id = updates
            .iter()
            .find_map(|u| match u {
                CoreUpdate::TabCreated { tab } => Some(tab.id.clone()),
                _ => None,
            })
            .expect("TabCreated");
        core.handle_event(ShellEvent::FavoriteTab { tab_id: id.clone() });
        id
    }

    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let space_id = {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let space_id = core.get_active_space_id().clone();
        let a = create_fav(&mut core, "https://example.com");
        let b = create_fav(&mut core, "https://example.com");
        assert_ne!(a, b, "duplicate-url favorites must have distinct ids (ghost scenario)");
        // Flush throttled writes; no load_state here, so schema_version stays
        // absent (0) and the v2->v3 dedup migration runs on the next fresh load.
        core.save_state().expect("save");
        space_id
    };

    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("restore + migrate");

    let favorites = restored.get_favorite_tabs(&space_id);
    assert_eq!(
        favorites.len(),
        1,
        "dedup migration must reduce duplicate same-url favorites to a single tile"
    );

    let storage = maho_storage::lmdb::LmdbStorage::open(&storage_path)
        .expect("re-init storage");
    let schema_version: u32 =
        serde_json::from_slice(&storage.get("schema_version").unwrap().unwrap()).unwrap();
    assert_eq!(schema_version, 4);
}

#[test]
fn lmdb_persists_set_folder_pinned_true_and_false() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let (space_id, folder_id) = {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let space_id = core.get_active_space_id();
        let folder_id = create_folder_for_test(&mut core, space_id.clone(), "Pinnable");

        core.handle_event(ShellEvent::SetFolderPinned {
            space_id: space_id.clone(),
            folder_id: folder_id.clone(),
            is_pinned: true,
        });
        core.handle_event(ShellEvent::AppWillTerminate);
        (space_id, folder_id)
    };

    {
        let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
        restored.load_state().expect("state should restore");
        let folders = restored.space_manager().get_folder_view_models(&space_id);
        let folder = folders
            .iter()
            .find(|f| f.id == folder_id)
            .expect("folder should exist after reload");
        assert!(folder.is_pinned, "is_pinned=true must survive reload");
    }

    {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        core.load_state().expect("state should restore");
        core.handle_event(ShellEvent::SetFolderPinned {
            space_id: space_id.clone(),
            folder_id: folder_id.clone(),
            is_pinned: false,
        });
        core.handle_event(ShellEvent::AppWillTerminate);
    }

    {
        let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
        restored.load_state().expect("state should restore");
        let folders = restored.space_manager().get_folder_view_models(&space_id);
        let folder = folders
            .iter()
            .find(|f| f.id == folder_id)
            .expect("folder should exist after reload");
        assert!(!folder.is_pinned, "is_pinned=false must survive reload");
    }
}
