use maho_core::{
    maho_core::MahoCore,
    sync_manager::SyncManager,
    sync_models::{build_fields_hlc, SyncConfig, SyncEntity, SyncEntityType},
};

fn entity(kind: SyncEntityType, version: u64, title: &str) -> SyncEntity {
    let payload =
        serde_json::json!({"profileId":"default", "title":title, "url":"https://example.com"})
            .to_string();
    SyncEntity {
        entity_type: kind,
        entity_id: "review-entity".into(),
        version,
        device_id: 1,
        schema_version: 1,
        modified_at: 0,
        fields_hlc_json: build_fields_hlc(&payload, version, 1),
        payload_json: payload,
        deleted: false,
        queue_row_id: None,
        profile_id: Some("default".into()),
    }
}

#[test]
fn keyless_snapshot_export_is_rejected() {
    assert!(MahoCore::new().export_sync_snapshot().is_err());
}

#[test]
fn mixed_field_clocks_preserve_concurrent_title() {
    for reverse in [false, true] {
        let mut manager = SyncManager::new(SyncConfig::default());
        let mut local = entity(SyncEntityType::Tab, 5, "new title");
        let mut remote = entity(SyncEntityType::Tab, 6, "old title");
        remote.payload_json = serde_json::json!({"profileId":"default", "title":"old title", "url":"https://new.example.com"}).to_string();
        remote.fields_hlc_json =
            Some(serde_json::json!({"profileId":[1,1], "title":[1,1], "url":[6,1]}).to_string());
        let merged = if reverse {
            assert!(manager.apply_remote_entity(&mut remote, &None));
            assert!(manager.apply_remote_entity(&mut local, &None));
            local
        } else {
            assert!(manager.apply_remote_entity(&mut local, &None));
            assert!(manager.apply_remote_entity(&mut remote, &None));
            remote
        };
        let payload: serde_json::Value = serde_json::from_str(&merged.payload_json).unwrap();
        assert_eq!(payload["title"], "new title");
        assert_eq!(payload["url"], "https://new.example.com");
    }
}

#[test]
fn acknowledged_local_version_survives_restart() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("review-sync-key");
    for kind in [SyncEntityType::Tab, SyncEntityType::Shortcut] {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("core.db");
        let path = path.to_str().unwrap();
        {
            let mut core = MahoCore::new().with_storage(path);
            core.start_sync("wss://localhost", "review");
            core.sync_push_entity_persisted(entity(kind.clone(), 10, "new title"));
            let storage = core.storage_ref().unwrap();
            let ids: Vec<_> = storage
                .load_pending_sync_entities()
                .unwrap()
                .iter()
                .map(|e| e.0)
                .collect();
            storage.ack_sync_entities(&ids).unwrap();
            assert!(storage.load_pending_sync_entities().unwrap().is_empty());
        }
        let storage = Some(maho_storage::sqlite::SqliteStorage::open(path).unwrap());
        let mut restarted = SyncManager::new(SyncConfig::default());
        assert!(!restarted.apply_remote_entity(&mut entity(kind, 2, "stale title"), &storage));
    }
}

#[test]
fn alternate_version_key_does_not_lose_newer_local_edit() {
    for kind in [SyncEntityType::Tab, SyncEntityType::Shortcut] {
        let storage = Some(
            maho_storage::sqlite::SqliteStorage::open_in_memory_with_key("review-sync-key")
                .unwrap(),
        );
        let db = storage.as_ref().unwrap();
        let mut profile = maho_types::profile::ProfileConfig::new("Default".into());
        profile.id = maho_types::identifiers::ProfileId::new("default");
        db.save_profile("default", &serde_json::to_string(&profile).unwrap())
            .unwrap();
        let local = entity(kind.clone(), 10, "new title");
        let canonical_id = local.storage_entity_id().unwrap();
        db.save_entity_version(
            kind.as_str(),
            &canonical_id,
            1,
            1,
            None,
            Some(&local.payload_json),
        )
        .unwrap();
        db.save_entity_version(
            kind.as_str(),
            "profile:default\u{1f}review-entity",
            10,
            1,
            local.fields_hlc_json.as_deref(),
            Some(&local.payload_json),
        )
        .unwrap();
        let mut manager = SyncManager::new(SyncConfig::default());
        let mut remote = entity(kind.clone(), 2, "stale title");
        if !kind.is_profile_scoped() {
            remote.profile_id = None;
        }
        assert!(!manager.apply_remote_entity(&mut remote, &storage));
        assert_eq!(
            db.get_entity_version(kind.as_str(), &canonical_id)
                .unwrap()
                .unwrap()
                .0,
            10
        );
    }
}
