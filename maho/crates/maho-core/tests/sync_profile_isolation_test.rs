use maho_core::maho_core::MahoCore;
use maho_core::sync_manager::SyncManager;
use maho_core::sync_models::{build_fields_hlc, SyncConfig, SyncEntity, SyncEntityType};

fn config() -> SyncConfig {
    SyncConfig {
        server_url: "wss://relay.test".to_string(),
        sync_key: Some("room".to_string()),
        device_id: "profile-isolation-device".to_string(),
        device_name: "Profile Isolation Test".to_string(),
        auto_sync: true,
        sync_interval_secs: 30,
        disabled_entity_types: std::collections::HashSet::new(),
    }
}

fn scoped_entity(
    entity_type: SyncEntityType,
    entity_id: &str,
    profile_id: &str,
    version: u64,
    deleted: bool,
) -> SyncEntity {
    let payload_json = match entity_type {
        SyncEntityType::Tab => serde_json::json!({
            "id": entity_id,
            "spaceId": format!("space-{profile_id}"),
            "profileId": profile_id,
            "url": "https://example.com"
        })
        .to_string(),
        SyncEntityType::Space => serde_json::json!({
            "id": entity_id,
            "profileId": profile_id,
            "name": profile_id
        })
        .to_string(),
        other => panic!("unexpected test entity type: {other:?}"),
    };
    SyncEntity {
        entity_type,
        entity_id: entity_id.to_string(),
        profile_id: None,
        version,
        device_id: 7,
        schema_version: 1,
        modified_at: 1_700_000_000,
        fields_hlc_json: (!deleted)
            .then(|| build_fields_hlc(&payload_json, version, 7))
            .flatten(),
        payload_json,
        deleted,
        queue_row_id: None,
    }
}

#[test]
fn profile_scoped_lww_and_tombstones_do_not_cross_profiles() {
    let storage = Some(
        maho_storage::sqlite::SqliteStorage::open_in_memory_with_key(
            "sync-profile-isolation-test-key",
        )
        .expect("storage"),
    );
    let mut manager = SyncManager::new(config());

    let mut deleted_a = scoped_entity(SyncEntityType::Tab, "same-tab", "profile-a", 10, true);
    assert!(manager.apply_remote_entity(&mut deleted_a, &storage));

    let mut stale_a = scoped_entity(SyncEntityType::Tab, "same-tab", "profile-a", 5, false);
    assert!(!manager.apply_remote_entity(&mut stale_a, &storage));

    let mut live_b = scoped_entity(SyncEntityType::Tab, "same-tab", "profile-b", 5, false);
    assert!(manager.apply_remote_entity(&mut live_b, &storage));
    let wire = serde_json::to_value(&live_b).expect("wire entity");
    assert_eq!(wire["profileId"].as_str(), Some("profile-b"));

    let mut mismatched = scoped_entity(SyncEntityType::Tab, "mismatch", "profile-a", 3, false);
    mismatched.profile_id = Some("profile-b".to_string());
    assert!(!manager.apply_remote_entity(&mut mismatched, &storage));

    let storage_ref = storage.as_ref().expect("storage ref");
    let a_storage_id = deleted_a.storage_entity_id().expect("scoped A id");
    let b_storage_id = live_b.storage_entity_id().expect("scoped B id");
    assert_ne!(a_storage_id, b_storage_id);
    assert_eq!(
        storage_ref
            .get_sync_tombstone("tab", &a_storage_id)
            .expect("A tombstone"),
        Some((10, 7))
    );
    assert_eq!(
        storage_ref
            .get_sync_tombstone("tab", &b_storage_id)
            .expect("B tombstone"),
        None
    );

    let mut space_a = scoped_entity(SyncEntityType::Space, "same-space", "profile-a", 20, false);
    let mut space_b = scoped_entity(SyncEntityType::Space, "same-space", "profile-b", 1, false);
    assert!(manager.apply_remote_entity(&mut space_a, &storage));
    assert!(manager.apply_remote_entity(&mut space_b, &storage));
    assert_ne!(space_a.storage_entity_id(), space_b.storage_entity_id());
}

#[test]
fn switched_profile_is_bound_before_local_tab_is_durably_queued() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("sync-profile-core-scope-key");
    let mut core = MahoCore::new().with_storage(":memory:");
    let work = core
        .create_profile_persisted("Work".to_string())
        .expect("profile");
    let work_id = work.id.to_string();

    core.start_sync("wss://relay.test", "room");
    assert!(core.switch_profile(&work.id));

    let version = core.next_hlc_ts().expect("HLC");
    let payload_json = serde_json::json!({
        "id": "local-tab",
        "spaceId": "unmapped-space",
        "url": "https://example.com"
    })
    .to_string();
    core.sync_push_entity_persisted(SyncEntity {
        entity_type: SyncEntityType::Tab,
        entity_id: "local-tab".to_string(),
        profile_id: None,
        version,
        device_id: 11,
        schema_version: 1,
        modified_at: 1_700_000_000,
        fields_hlc_json: build_fields_hlc(&payload_json, version, 11),
        payload_json,
        deleted: false,
        queue_row_id: None,
    });

    let storage = core.storage_ref().expect("storage");
    let pending = storage
        .load_pending_sync_entities()
        .expect("pending sync rows");
    let row = pending
        .iter()
        .find(|(_, entity_type, entity_id, ..)| entity_type == "tab" && entity_id == "local-tab")
        .expect("local tab queue row");
    let persisted_payload: serde_json::Value =
        serde_json::from_str(&row.5).expect("queued tab payload");
    assert_eq!(
        persisted_payload["profileId"].as_str(),
        Some(work_id.as_str())
    );

    let scoped_id = format!("{}:{}", work_id, "local-tab");
    assert!(storage
        .get_entity_version("tab", &scoped_id)
        .expect("scoped entity version")
        .is_some());
    assert!(storage
        .get_entity_version("tab", "local-tab")
        .expect("raw entity version")
        .is_none());
}
