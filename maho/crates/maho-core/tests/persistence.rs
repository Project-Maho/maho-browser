use maho_core::content_blocker::compile_engine_snapshot;
use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{SyncEntity, SyncEntityType, SyncStatus};
use maho_types::common::{DateTime, ScrollPosition, Url};
use maho_types::content_blocking::{ContentBlockingMode, FilterListUpdateResponse};
use maho_types::events::core_update::CoreUpdate;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::{SpaceId, TabId};
use maho_types::settings::{PrivacySettingsUpdate, SettingsUpdate};
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

        let tab_id = core
            .get_tab_view_models()
            .first()
            .expect("tab should be created")
            .id
            .clone();
        tab_id
    };

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

        let tab_id = core
            .get_tab_view_models()
            .first()
            .expect("tab should be created")
            .id
            .clone();

        core.handle_event(ShellEvent::CloseTab {
            tab_id,
            expected_space_id: Some(space_id.clone()),
        });

        core.handle_event(ShellEvent::AppWillTerminate);
        space_id
    };

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
        let profile = core
            .create_profile_persisted("Work Profile".to_string())
            .unwrap();
        let color = SpaceColor {
            hue: 120.0,
            saturation: 0.5,
            brightness: 0.5,
            grain: 0.0,
        };
        let space = core.create_space("New Space", color, profile.id);
        space.id
    };

    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("state should restore");

    let spaces = restored.get_space_view_models();
    assert!(spaces.iter().any(|s| s.id == space_id));
}

#[test]
fn lmdb_persists_active_space_change_immediately() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let new_space_id = {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let profile = core
            .create_profile_persisted("Work Profile".to_string())
            .unwrap();
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

        let tab_id = core
            .get_tab_view_models()
            .first()
            .expect("tab should be created")
            .id
            .clone();

        let profile = core
            .create_profile_persisted("Work Profile".to_string())
            .unwrap();
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

    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("state should restore");

    let tabs = restored.get_tab_view_models();
    let moved_tab = tabs
        .iter()
        .find(|t| t.id == tab_id)
        .expect("tab should exist");
    assert_eq!(moved_tab.space_id, target_space_id);
}

#[test]
fn no_lmdb_io_when_storage_absent() {
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
    let tab_id = core.get_tab_view_models().first().expect("tab").id.clone();
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

fn create_tab_for_test(
    core: &mut MahoCore,
    space_id: SpaceId,
    url: &str,
    is_private: bool,
) -> TabId {
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

fn create_folder_for_test(
    core: &mut MahoCore,
    space_id: SpaceId,
    name: &str,
) -> maho_types::identifiers::FolderId {
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
        custom_icon: None,
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
        let normal_id = create_tab_for_test(
            &mut core,
            space_id.clone(),
            "https://normal.example.com",
            false,
        );
        let private_folder_id = create_tab_for_test(
            &mut core,
            space_id.clone(),
            "https://private-folder.example.com",
            true,
        );
        let folder_id = create_folder_for_test(&mut core, space_id.clone(), "Private Folder");
        core.handle_event(ShellEvent::MoveTabToFolder {
            space_id: space_id.clone(),
            folder_id,
            tab_id: private_folder_id.clone(),
        });
        let private_root_id = create_tab_for_test(
            &mut core,
            space_id.clone(),
            "https://private-root.example.com",
            true,
        );
        core.save_state().expect("save state");

        let live_tabs = core.get_tab_view_models();
        assert!(live_tabs.iter().any(|tab| tab.id == private_folder_id));
        assert!(live_tabs.iter().any(|tab| tab.id == private_root_id));
        (normal_id, private_folder_id, private_root_id, space_id)
    };

    let lmdb = maho_storage::lmdb::LmdbStorage::open(&storage_path).expect("open lmdb");
    let persisted_tabs: Vec<Tab> =
        serde_json::from_slice(&lmdb.get("tabs").unwrap().unwrap()).unwrap();
    assert!(persisted_tabs.iter().any(|tab| tab.id == normal_id));
    assert!(!persisted_tabs.iter().any(|tab| tab.id == private_folder_id));
    assert!(!persisted_tabs.iter().any(|tab| tab.id == private_root_id));

    let persisted_spaces: Vec<Space> =
        serde_json::from_slice(&lmdb.get("spaces").unwrap().unwrap()).unwrap();
    let persisted_space = persisted_spaces
        .iter()
        .find(|space| space.id == space_id)
        .unwrap();
    for private_id in [&private_folder_id, &private_root_id] {
        assert!(!persisted_space.tab_order.contains(private_id));
        assert!(!persisted_space
            .folders
            .iter()
            .any(|folder| folder.tab_ids.contains(private_id)));
        assert!(!persisted_space
            .root_order
            .iter()
            .any(|item| matches!(item, RootItem::Tab(tab_id) if tab_id == private_id)));
        assert_ne!(
            persisted_space.last_active_tab_id.as_ref(),
            Some(private_id)
        );
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
    let normal_id = create_tab_for_test(
        &mut source,
        space_id.clone(),
        "https://normal-sync.example.com",
        false,
    );
    let private_id = create_tab_for_test(
        &mut source,
        space_id,
        "https://private-sync.example.com",
        true,
    );
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
    dest.configure_sync_encryption_for_recovery_phrase("ws://127.0.0.1:9", &phrase)
        .unwrap();
    dest.apply_snapshot(&encrypted_snapshot)
        .expect("apply snapshot");
    let dest_tabs = dest.get_tab_view_models();
    assert!(dest_tabs
        .iter()
        .any(|tab| tab.url == "https://normal-sync.example.com"));
    assert!(!dest_tabs
        .iter()
        .any(|tab| tab.url == "https://private-sync.example.com"));
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
        color: SpaceColor {
            hue: 1.0,
            saturation: 1.0,
            brightness: 1.0,
            grain: 0.0,
        },
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
        profile_id: Some("profile".to_string()),
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
        profile_id: Some("profile".to_string()),
    };

    core.apply_sync_remote_entities(vec![tab_entity, space_entity]);
    assert!(!core
        .get_tab_view_models()
        .iter()
        .any(|tab| tab.id == private_id));
    let tabs = core.get_space_tabs(&space_id);
    assert!(!tabs.iter().any(|tab| tab.id == private_id));
}

#[test]
fn test_lmdb_schema_migration_v1_to_v2() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");
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
        let storage = maho_storage::lmdb::LmdbStorage::open(&storage_path).expect("init storage");
        storage.put("tabs", old_tabs_json.as_bytes()).unwrap();
    }

    let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
    core.load_state()
        .expect("should load and migrate state successfully");
    let tabs = core.get_tab_view_models();
    assert_eq!(tabs.len(), 1);
    assert_eq!(tabs[0].id.to_string(), "tab-1");
    assert!(tabs[0].is_favorite);
    assert_eq!(tabs[0].favorite_order, Some(3));
    assert_eq!(
        tabs[0].role,
        maho_types::tab::TabRole::Favorite { order: 3 }
    );

    let storage = maho_storage::lmdb::LmdbStorage::open(&storage_path).expect("re-init storage");
    let schema_version_data = storage.get("schema_version").unwrap().unwrap();
    let schema_version: u32 = serde_json::from_slice(&schema_version_data).unwrap();
    assert_eq!(schema_version, 4);
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
        assert_ne!(
            a, b,
            "duplicate-url favorites must have distinct ids (ghost scenario)"
        );
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

    let storage = maho_storage::lmdb::LmdbStorage::open(&storage_path).expect("re-init storage");
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

#[test]
fn keyless_outgoing_emits_nothing_and_preserves_pending() {
    let mut source = MahoCore::new().with_storage(":memory:");
    let space_id = source.get_active_space_id();
    let normal_id = create_tab_for_test(
        &mut source,
        space_id.clone(),
        "https://normal-sync.example.com",
        false,
    );
    source.start_sync("ws://127.0.0.1:9", "0123456789abcdef0123456789abcdef");
    source.set_sync_status(SyncStatus::Syncing);
    source.push_tab_sync(&normal_id);

    let outgoing = source.drain_sync_outgoing();
    assert!(
        outgoing.is_empty(),
        "keyless session must not emit any outgoing sync message"
    );
    let pending = source
        .storage_ref()
        .unwrap()
        .load_pending_sync_entities()
        .unwrap();
    assert!(
        !pending.is_empty(),
        "keyless push must leave the queue row pending for later encrypted retry"
    );
}

#[test]
fn test_configure_sync_encryption_roundtrip() {
    let mut source = MahoCore::new();
    let space_id = source.get_active_space_id();
    let normal_id = create_tab_for_test(
        &mut source,
        space_id.clone(),
        "https://normal-sync.example.com",
        false,
    );
    let phrase = recovery_phrase(&source);
    source
        .configure_sync_encryption_for_recovery_phrase("ws://127.0.0.1:9", &phrase)
        .unwrap();
    source.set_sync_status(SyncStatus::Syncing);
    source.push_tab_sync(&normal_id);

    let outgoing = source.drain_sync_outgoing();
    assert!(!outgoing.is_empty());
    for msg in &outgoing {
        assert!(matches!(
            msg,
            maho_core::sync_models::SyncMessage::Encrypted { .. }
        ));
    }

    let mut dest = MahoCore::new();
    dest.configure_sync_encryption_for_recovery_phrase("ws://127.0.0.1:9", &phrase)
        .unwrap();
    let encrypted_snapshot = source.build_snapshot().expect("snapshot");
    dest.apply_snapshot(&encrypted_snapshot)
        .expect("apply snapshot");
    let dest_tabs = dest.get_tab_view_models();
    assert!(dest_tabs
        .iter()
        .any(|tab| tab.url == "https://normal-sync.example.com"));
}

/// Decrypt a snapshot blob back into its entity map for field-level inspection.
fn decode_snapshot(blob: &[u8], phrase: &str) -> maho_core::snapshot::Snapshot {
    let seed = maho_core::sync_crypto::decode_recovery_phrase(phrase).unwrap();
    let key = maho_core::sync_crypto::derive_encryption_key(&seed).unwrap();
    let plaintext = maho_core::sync_crypto::decrypt_update(blob, &key).unwrap();
    serde_json::from_slice(&plaintext).unwrap()
}

/// A snapshot entity whose payload already names a profile must keep that
/// profile. The stamp resolves *missing* scope; it must never relabel an entity
/// that belongs to another profile as the active one, which would silently move
/// data between profiles on restore.
#[test]
fn snapshot_scope_stamp_preserves_an_explicit_foreign_profile() {
    let mut source = MahoCore::new();
    let space_id = source.get_active_space_id();
    create_tab_for_test(
        &mut source,
        space_id.clone(),
        "https://foreign-scope.example.com",
        false,
    );

    // Relabel the space to a profile that is NOT the active one, exactly as a
    // space imported from another profile would arrive.
    source.update_space_config(maho_types::space::SpaceConfigUpdate {
        space_id: space_id.clone(),
        name: None,
        color: None,
        theme: None,
        icon: None,
        profile_id: Some(maho_types::identifiers::ProfileId::new(
            "foreign-profile-id".to_string(),
        )),
    });

    let phrase = recovery_phrase(&source);
    source
        .configure_sync_encryption_for_recovery_phrase("ws://127.0.0.1:9", &phrase)
        .unwrap();
    let snapshot = decode_snapshot(&source.build_snapshot().expect("snapshot"), &phrase);

    let space_entity = snapshot.entities["Space"]
        .iter()
        .find(|entity| entity.entity_id == space_id.to_string())
        .expect("space entity in snapshot");
    assert_eq!(
        space_entity.profile_id.as_deref(),
        Some("foreign-profile-id"),
        "an explicit foreign profile must not be overwritten by the active one"
    );
    let payload: serde_json::Value = serde_json::from_str(&space_entity.payload_json).unwrap();
    assert_eq!(payload["profileId"], "foreign-profile-id");
}

/// Every profile-scoped type must be stamped, not just the subset whose payload
/// happens to define a `profileId`. Boost / CssMod / Easel carry no profile
/// field of their own, so an unstamped entity of those types is dropped by
/// `bind_remote_profile_scope` on every receiver.
#[test]
fn snapshot_stamps_profile_scoped_types_that_lack_a_payload_profile_field() {
    let mut source = MahoCore::new();
    source.create_temp_boost_persisted("scoped-boost.example.com".to_string());
    let phrase = recovery_phrase(&source);
    source
        .configure_sync_encryption_for_recovery_phrase("ws://127.0.0.1:9", &phrase)
        .unwrap();

    let snapshot = decode_snapshot(&source.build_snapshot().expect("snapshot"), &phrase);
    let boost = snapshot.entities["Boost"]
        .first()
        .expect("boost entity in snapshot");

    let active_profile = source
        .get_active_profile_id()
        .expect("active profile")
        .to_string();
    assert_eq!(boost.profile_id.as_deref(), Some(active_profile.as_str()));
    let payload: serde_json::Value = serde_json::from_str(&boost.payload_json).unwrap();
    assert_eq!(
        payload["profileId"], active_profile,
        "scope must travel inside the payload, since the receiver reads it from there"
    );

    // The stamp rewrites payload_json, so the field digest must be recomputed
    // over the FINAL payload or per-field merge compares against a stale hash.
    let fields_hlc = boost
        .fields_hlc_json
        .as_deref()
        .expect("non-deleted entity carries a field digest");
    let expected = maho_core::sync_models::build_fields_hlc(
        &boost.payload_json,
        boost.version,
        boost.device_id,
    )
    .expect("digest over the stamped payload");
    assert_eq!(
        serde_json::from_str::<serde_json::Value>(fields_hlc).unwrap(),
        serde_json::from_str::<serde_json::Value>(&expected).unwrap(),
        "fields_hlc_json must be computed over the stamped payload"
    );
}

#[test]
fn test_room_id_and_key_derivation_parity() {
    let source = MahoCore::new();
    let key_info_json = source.generate_sync_key();
    let key_info: serde_json::Value = serde_json::from_str(&key_info_json).unwrap();
    let phrase = key_info["recoveryPhrase"].as_str().unwrap();
    let room_id = key_info["roomId"].as_str().unwrap();
    let seed = maho_core::sync_crypto::decode_recovery_phrase(phrase).unwrap();
    let derived_room_id = maho_core::sync_crypto::derive_room_id(&seed).unwrap();
    assert_eq!(room_id, derived_room_id);
    assert_eq!(room_id.len(), 32);
}

#[test]
fn sqlite_traffic_rule_glob_match_type_survives_restart() {
    use maho_types::air_traffic::{LinkDestination, MatchType, TrafficRule};
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let db_path = temp_dir
        .path()
        .join("atc-roundtrip.db")
        .to_string_lossy()
        .to_string();
    let work = SpaceId::new("space-work");

    {
        let mut core = MahoCore::new().with_storage(&db_path);
        core.create_traffic_rule_persisted(TrafficRule {
            id: "glob-rule-1".to_string(),
            url_pattern: "*.github.com".to_string(),
            match_type: MatchType::Glob,
            target_space_id: work.clone(),
            enabled: true,
        });
        assert_eq!(
            core.decide_link_destination("https://github.com/signup", false, &[]),
            LinkDestination::Space(work.clone()),
            "freshly created glob rule must route"
        );
    }

    let mut restored = MahoCore::new().with_storage(&db_path);
    restored
        .load_persisted_data()
        .expect("state should restore");
    assert_eq!(
        restored.decide_link_destination("https://github.com/signup", false, &[]),
        LinkDestination::Space(work.clone()),
        "restored glob rule must still route after restart"
    );
    assert_eq!(
        restored.decide_link_destination("https://api.github.com/x", false, &[]),
        LinkDestination::Space(work),
        "subdomain must still match after restart"
    );
    assert_eq!(
        restored.decide_link_destination("https://evilgithub.com", false, &[]),
        LinkDestination::Normal,
        "boundary guard must survive restart"
    );
}

#[test]
fn archive_tab_by_id_persists_archived_state_after_reload() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");
    let (tab_id, space_id) = {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let space_id = core.get_active_space_id();
        let tab_id = create_tab_for_test(
            &mut core,
            space_id.clone(),
            "https://archive-persist.example.com",
            false,
        );
        core.handle_event(ShellEvent::ArchiveTabById {
            tab_id: tab_id.clone(),
        });
        core.handle_event(ShellEvent::AppWillTerminate);
        (tab_id, space_id)
    };

    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("state should restore");
    let tabs = restored.get_tab_view_models();
    assert!(!tabs.iter().any(|t| t.id == tab_id));
    let archived_tabs = restored.get_archived_tabs(&space_id);
    assert!(archived_tabs.iter().any(|t| t.id == tab_id));
    assert_eq!(archived_tabs[0].space_id, space_id);
}

#[test]
fn restore_archived_tab_transitions_back_to_active_and_is_idempotent() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");
    let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
    let space_id = core.get_active_space_id();
    let tab_id = create_tab_for_test(
        &mut core,
        space_id.clone(),
        "https://archive-persist.example.com",
        false,
    );
    core.handle_event(ShellEvent::ArchiveTabById {
        tab_id: tab_id.clone(),
    });
    assert!(core
        .get_archived_tabs(&space_id)
        .iter()
        .any(|t| t.id == tab_id));
    core.handle_event(ShellEvent::RestoreArchivedTab {
        tab_id: tab_id.clone(),
    });
    assert!(!core
        .get_archived_tabs(&space_id)
        .iter()
        .any(|t| t.id == tab_id));
    assert!(core.get_tab_view_models().iter().any(|t| t.id == tab_id));
    core.handle_event(ShellEvent::RestoreArchivedTab {
        tab_id: tab_id.clone(),
    });
    assert!(core.get_tab_view_models().iter().any(|t| t.id == tab_id));
    let random_id = maho_types::identifiers::TabId::new("random-id");
    core.handle_event(ShellEvent::RestoreArchivedTab { tab_id: random_id });
}

#[test]
fn credential_sync_refuses_keyless_transport() {
    let mut manager =
        maho_core::sync_manager::SyncManager::new(maho_core::sync_models::SyncConfig {
            device_id: "test-device".to_string(),
            server_url: "wss://relay.example.com".to_string(),
            sync_key: None,
            device_name: "test-device".to_string(),
            auto_sync: true,
            sync_interval_secs: 30,
            disabled_entity_types: std::collections::HashSet::new(),
        });
    manager.set_status(SyncStatus::Synced);

    let entity = SyncEntity {
        entity_id: "test-entity-1".to_string(),
        entity_type: SyncEntityType::AutofillAddress,
        schema_version: 1,
        version: 1,
        modified_at: 1000,
        device_id: 1,
        payload_json: "{\"test\":\"data\"}".to_string(),
        fields_hlc_json: None,
        deleted: false,
        queue_row_id: Some(42),
        profile_id: None,
    };

    manager.push_entity(entity);
    assert!(
        manager.drain_outgoing_messages().is_empty(),
        "Outgoing messages must be empty when keyless"
    );
    assert!(
        manager.last_error().is_some(),
        "last_error must be populated on keyless push"
    );
}

const CB_KEY: &str = "test_key";
fn cb_db_path(dir: &tempfile::TempDir, name: &str) -> String {
    dir.path().join(name).to_string_lossy().to_string()
}

#[test]
fn content_blocker_custom_list_metadata_and_exception_created_at_survive_restart() {
    let _ = maho_storage::sqlite::set_sqlcipher_key(CB_KEY);
    let dir = tempfile::tempdir().expect("temp dir");
    let db = cb_db_path(&dir, "cb-meta.db");
    let persisted_created_at;
    {
        let mut core = MahoCore::new().with_storage(&db);
        assert!(core.add_filter_list(
            "custom".into(),
            "Custom List".into(),
            "https://filters.example/custom.txt".into()
        ));
        core.update_filter_list_content("custom", "||tracker.example^\n! a comment\n".into());
        assert!(
            core.install_content_blocker_compiled_engine(compile_engine_snapshot(
                core.create_content_blocker_compile_snapshot()
            ))
        );
        core.add_content_blocker_site_exception("https://www.paused.example:8443");
        core.set_content_blocking_mode(ContentBlockingMode::Native);
        assert!(core.should_block_request(
            "https://tracker.example/x",
            "https://site.test",
            "script"
        ));
        persisted_created_at = core
            .get_content_blocker_site_exceptions()
            .into_iter()
            .find(|e| e.key == "paused.example")
            .expect("exception exists in first session")
            .created_at;
    }

    let core = MahoCore::new().with_storage(&db);
    let dto = core.get_content_blocker_state_dto();
    let custom = dto
        .lists
        .iter()
        .find(|l| l.id == "custom")
        .expect("custom list must survive restart");
    assert_eq!(custom.name, "Custom List");
    assert_eq!(custom.url, "https://filters.example/custom.txt");
    assert!(custom.enabled);
    assert_eq!(
        custom.rule_count, 1,
        "rule-count metadata must survive restart"
    );
    assert!(
        custom.last_success_timestamp.is_some(),
        "validator timestamps must survive restart"
    );
    assert!(
        core.should_block_request("https://tracker.example/x", "https://site.test", "script"),
        "last-known-good raw content must survive restart and still block"
    );
    let exc = core
        .get_content_blocker_site_exceptions()
        .into_iter()
        .find(|e| e.key == "paused.example")
        .expect("canonical exception key must survive restart");
    assert_eq!(
        exc.created_at, persisted_created_at,
        "original created_at must survive unchanged, not be regenerated at restart"
    );
    assert_eq!(core.get_content_blocker_mode(), ContentBlockingMode::Native);
}

#[test]
fn content_blocker_deleted_seed_does_not_reappear_and_seeds_only_when_empty() {
    let _ = maho_storage::sqlite::set_sqlcipher_key(CB_KEY);
    let dir = tempfile::tempdir().expect("temp dir");
    let db = cb_db_path(&dir, "cb-seed.db");
    {
        let mut core = MahoCore::new().with_storage(&db);
        let dto = core.get_content_blocker_state_dto();
        assert!(
            dto.lists.iter().any(|l| l.id == "easylist"),
            "fresh DB seeds EasyList"
        );
        assert!(
            dto.lists.iter().any(|l| l.id == "easyprivacy"),
            "fresh DB seeds EasyPrivacy"
        );
        core.remove_filter_list("easyprivacy");
    }
    let core = MahoCore::new().with_storage(&db);
    let dto = core.get_content_blocker_state_dto();
    assert!(
        dto.lists.iter().any(|l| l.id == "easylist"),
        "surviving seed must remain"
    );
    assert!(dto.lists.iter().all(|l| l.id != "easyprivacy"), "a deleted seed must not reappear after restart (seeding happens only when no persisted rows exist)");
}

#[test]
fn content_blocker_valid_cache_reopen_and_invalid_version_rebuilds_from_raw() {
    let _ = maho_storage::sqlite::set_sqlcipher_key(CB_KEY);
    let dir = tempfile::tempdir().expect("temp dir");
    let db = cb_db_path(&dir, "cb-cache.db");
    let (generation, hash) = {
        let mut core = MahoCore::new().with_storage(&db);
        core.update_filter_list_content("easylist", "||adcache.example^\n".into());
        assert!(
            core.install_content_blocker_compiled_engine(compile_engine_snapshot(
                core.create_content_blocker_compile_snapshot()
            ))
        );
        core.set_content_blocking_mode(ContentBlockingMode::Native);
        let dto = core.get_content_blocker_state_dto();
        (dto.engine_generation, dto.engine_hash.clone())
    };
    assert!(
        hash.is_some(),
        "a valid session must persist an engine hash"
    );

    {
        let core = MahoCore::new().with_storage(&db);
        let dto = core.get_content_blocker_state_dto();
        assert_eq!(
            dto.engine_generation, generation,
            "valid cache reopen keeps persisted generation"
        );
        assert_eq!(
            dto.engine_hash, hash,
            "valid cache reopen keeps persisted hash"
        );
        assert!(core.should_block_request("https://adcache.example/x", "https://s.test", "script"));
    }

    {
        let core = MahoCore::new().with_storage(&db);
        let storage = core.storage_ref().expect("storage handle");
        let (mode, g, cache, _ver, h) = storage
            .load_content_blocker_state()
            .unwrap()
            .expect("state row present");
        storage
            .save_content_blocker_state(
                mode,
                g,
                cache.as_deref(),
                Some("adblock-WRONG-VERSION"),
                h.as_deref(),
            )
            .expect("poison engine version");
    }

    let core = MahoCore::new().with_storage(&db);
    assert!(
        core.should_block_request("https://adcache.example/x", "https://s.test", "script"),
        "an invalid engine cache must rebuild from persisted raw content and still block"
    );
    let dto = core.get_content_blocker_state_dto();
    assert_eq!(
        core.get_content_blocker_mode(),
        ContentBlockingMode::Native,
        "mode config preserved on rebuild"
    );
    assert!(
        dto.lists.iter().any(|l| l.id == "easylist"),
        "lists config preserved on rebuild"
    );
}

#[test]
fn content_blocker_mode_converges_across_settings_shellevent_and_direct_api() {
    let mut core = MahoCore::new().with_storage(":memory:");
    core.set_content_blocking_mode(ContentBlockingMode::Disabled);
    assert_eq!(
        core.get_content_blocker_mode(),
        ContentBlockingMode::Disabled
    );
    assert_eq!(
        core.get_settings().privacy.content_blocking_mode,
        ContentBlockingMode::Disabled
    );

    core.handle_event(ShellEvent::SetContentBlockingMode {
        mode: ContentBlockingMode::Extension,
    });
    assert_eq!(
        core.get_content_blocker_mode(),
        ContentBlockingMode::Extension
    );
    assert_eq!(
        core.get_settings().privacy.content_blocking_mode,
        ContentBlockingMode::Extension
    );

    core.handle_event(ShellEvent::UpdateSettings {
        changes: SettingsUpdate {
            privacy: Some(PrivacySettingsUpdate {
                content_blocking_mode: Some(ContentBlockingMode::Native),
                ..Default::default()
            }),
            ..Default::default()
        },
    });
    assert_eq!(
        core.get_content_blocker_mode(),
        ContentBlockingMode::Native,
        "a settings-driven mode change must converge onto the authoritative blocker mode"
    );
    assert_eq!(
        core.get_settings().privacy.content_blocking_mode,
        ContentBlockingMode::Native
    );
}

#[test]
fn content_blocker_state_change_payload_carries_authoritative_mode_and_popup() {
    use std::cell::RefCell;
    use std::rc::Rc;
    let mut core = MahoCore::new().with_storage(":memory:");
    let captured: Rc<RefCell<Vec<(ContentBlockingMode, bool)>>> = Rc::new(RefCell::new(Vec::new()));
    let sink = captured.clone();
    core.on_update(Box::new(move |update| {
        if let CoreUpdate::ContentBlockerStateChanged(state) = update {
            sink.borrow_mut().push((state.mode, state.popup_blocking));
        }
    }));
    core.set_content_blocking_mode(ContentBlockingMode::Extension);
    let last = captured
        .borrow()
        .last()
        .copied()
        .expect("a content-blocker state change must be emitted");
    assert_eq!(
        last.0,
        ContentBlockingMode::Extension,
        "payload carries the authoritative mode"
    );
    assert!(last.1, "payload carries popup_blocking state");
}

#[test]
fn content_blocker_effective_block_behavior_identical_across_restart() {
    let _ = maho_storage::sqlite::set_sqlcipher_key(CB_KEY);
    let dir = tempfile::tempdir().expect("temp dir");
    let db = cb_db_path(&dir, "cb-e2e.db");
    let before_blocked;
    let before_excepted;
    {
        let mut core = MahoCore::new().with_storage(&db);
        assert!(core.add_filter_list(
            "custom".into(),
            "Custom".into(),
            "https://f.example/c.txt".into()
        ));
        let outcome = core.apply_filter_list_update_response(FilterListUpdateResponse {
            list_id: "custom".into(),
            status_code: 200,
            body: Some("||ads.evil.example^\n".into()),
            etag: Some("\"e1\"".into()),
            last_modified: None,
            sha256: None,
        });
        assert_eq!(outcome, Ok(true), "a 200 candidate signals rebuild-needed");
        let snapshot = core.create_content_blocker_compile_snapshot();
        let compiled = compile_engine_snapshot(snapshot);
        assert!(
            core.install_content_blocker_compiled_engine(compiled),
            "install must promote the candidate"
        );
        core.add_content_blocker_site_exception("https://trusted.example");
        core.set_content_blocking_mode(ContentBlockingMode::Native);
        before_blocked =
            core.should_block_request("https://ads.evil.example/x", "https://news.test", "script");
        before_excepted = core.should_block_request(
            "https://ads.evil.example/x",
            "https://trusted.example",
            "script",
        );
        assert!(before_blocked, "installed candidate rule must block");
        assert!(
            !before_excepted,
            "exception must suppress blocking on the trusted site"
        );
    }

    let core = MahoCore::new().with_storage(&db);
    let after_blocked =
        core.should_block_request("https://ads.evil.example/x", "https://news.test", "script");
    let after_excepted = core.should_block_request(
        "https://ads.evil.example/x",
        "https://trusted.example",
        "script",
    );
    assert_eq!(
        after_blocked, before_blocked,
        "effective block behavior must be identical after restart"
    );
    assert_eq!(
        after_excepted, before_excepted,
        "exception behavior must be identical after restart"
    );
    assert!(
        after_blocked && !after_excepted,
        "restart must preserve the full effective policy"
    );
}

#[test]
fn content_blocker_apply_update_records_candidate_without_synchronous_engine_change() {
    let _ = maho_storage::sqlite::set_sqlcipher_key(CB_KEY);
    let mut core = MahoCore::new().with_storage(":memory:");
    core.update_filter_list_content("easylist", "||good.example^\n".into());
    assert!(
        core.install_content_blocker_compiled_engine(compile_engine_snapshot(
            core.create_content_blocker_compile_snapshot()
        ))
    );
    core.set_content_blocking_mode(ContentBlockingMode::Native);
    assert!(core.should_block_request("https://good.example/x", "https://s.test", "script"));

    let outcome = core.apply_filter_list_update_response(FilterListUpdateResponse {
        list_id: "easylist".into(),
        status_code: 200,
        body: Some("||candidate.example^\n".into()),
        etag: None,
        last_modified: None,
        sha256: None,
    });
    assert_eq!(
        outcome,
        Ok(true),
        "a 200 must return the existing rebuild-needed outcome"
    );
    assert!(
        core.should_block_request("https://good.example/x", "https://s.test", "script"),
        "the last-known-good rule must stay active until compile+install"
    );
    assert!(
        !core.should_block_request("https://candidate.example/x", "https://s.test", "script"),
        "apply alone must not compile/install the candidate rule"
    );
}

#[test]
fn content_blocker_offthread_compile_install_promotes_and_persists_candidate_once() {
    let _ = maho_storage::sqlite::set_sqlcipher_key(CB_KEY);
    let dir = tempfile::tempdir().expect("temp dir");
    let db = cb_db_path(&dir, "cb-offthread.db");
    {
        let mut core = MahoCore::new().with_storage(&db);
        core.update_filter_list_content("easylist", "||good.example^\n".into());
        assert!(
            core.install_content_blocker_compiled_engine(compile_engine_snapshot(
                core.create_content_blocker_compile_snapshot()
            ))
        );
        core.set_content_blocking_mode(ContentBlockingMode::Native);
        assert_eq!(
            core.apply_filter_list_update_response(FilterListUpdateResponse {
                list_id: "easylist".into(),
                status_code: 200,
                body: Some("||candidate.example^\n".into()),
                etag: None,
                last_modified: None,
                sha256: None,
            }),
            Ok(true)
        );
        assert!(!core.should_block_request(
            "https://candidate.example/x",
            "https://s.test",
            "script"
        ));
        let snapshot = core.create_content_blocker_compile_snapshot();
        assert!(
            snapshot
                .raw_contents
                .iter()
                .any(|c| c.contains("candidate.example")),
            "the compile snapshot must include the pending candidate body"
        );
        let compiled = compile_engine_snapshot(snapshot);
        assert!(
            core.install_content_blocker_compiled_engine(compiled),
            "install must promote the candidate"
        );
        assert!(core.should_block_request(
            "https://candidate.example/x",
            "https://s.test",
            "script"
        ));
        assert!(!core.should_block_request("https://good.example/x", "https://s.test", "script"));
        let snap2 = core.create_content_blocker_compile_snapshot();
        assert!(snap2
            .raw_contents
            .iter()
            .any(|c| c.contains("candidate.example")));
        assert!(
            !snap2
                .raw_contents
                .iter()
                .any(|c| c.contains("good.example")),
            "the promoted body replaced the old one; no stale duplication remains"
        );
        let compiled2 = compile_engine_snapshot(snap2);
        assert!(core.install_content_blocker_compiled_engine(compiled2));
        assert!(
            core.should_block_request("https://candidate.example/x", "https://s.test", "script"),
            "a second compile+install keeps the same active policy (no duplicate promotion)"
        );
    }
    let core = MahoCore::new().with_storage(&db);
    assert!(
        core.should_block_request("https://candidate.example/x", "https://s.test", "script"),
        "the promoted candidate must survive restart"
    );
    assert!(
        !core.should_block_request("https://good.example/x", "https://s.test", "script"),
        "the superseded body must not survive restart"
    );
}

#[test]
fn content_blocker_first_candidate_install_is_durable_after_exactly_one_install() {
    let _ = maho_storage::sqlite::set_sqlcipher_key(CB_KEY);
    let dir = tempfile::tempdir().expect("temp dir");
    let db = cb_db_path(&dir, "cb-first-install.db");
    {
        let mut core = MahoCore::new().with_storage(&db);
        assert_eq!(
            core.apply_filter_list_update_response(FilterListUpdateResponse {
                list_id: "easylist".into(),
                status_code: 200,
                body: Some("||first-install.example^\n".into()),
                etag: None,
                last_modified: None,
                sha256: None,
            }),
            Ok(true)
        );
        assert!(
            core.install_content_blocker_compiled_engine(compile_engine_snapshot(
                core.create_content_blocker_compile_snapshot()
            ))
        );
    }
    let restored = MahoCore::new().with_storage(&db);
    assert!(
        restored.should_block_request(
            "https://first-install.example/x",
            "https://site.test",
            "script"
        ),
        "the first successful candidate promotion must be the durable snapshot"
    );
}
