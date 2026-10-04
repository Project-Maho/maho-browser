use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{SyncEntity, SyncEntityType, SyncStatus};

fn entity(version: u64, device_id: u32, deleted: bool) -> SyncEntity {
    SyncEntity {
        entity_type: SyncEntityType::Shortcut,
        entity_id: "deleted-shortcut".to_string(),
        version,
        device_id,
        schema_version: 1,
        modified_at: 1_700_000_000,
        payload_json: serde_json::json!({"action": "deleted-shortcut"}).to_string(),
        deleted,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: None,
    }
}

#[test]
fn tombstone_rejects_stale_recreate_and_allows_strictly_newer_recreate() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let mut core = MahoCore::new().with_storage(":memory:");
    core.start_sync("wss://localhost:8080", "test-room");
    core.set_sync_status(SyncStatus::Synced);

    assert_eq!(
        core.apply_sync_remote_entities(vec![entity(10, 1, false)])
            .len(),
        1
    );
    assert_eq!(
        core.apply_sync_remote_entities(vec![entity(20, 2, true)])
            .len(),
        1
    );
    assert!(
        core.apply_sync_remote_entities(vec![entity(19, 99, false)])
            .is_empty(),
        "older remote payload must not resurrect the tombstone"
    );
    assert!(
        core.apply_sync_remote_entities(vec![entity(20, 2, false)])
            .is_empty(),
        "equal ordering tuple must not resurrect the tombstone"
    );
    assert_eq!(
        core.apply_sync_remote_entities(vec![entity(21, 1, false)])
            .len(),
        1,
        "strictly newer user edit intentionally recreates the entity"
    );
}
