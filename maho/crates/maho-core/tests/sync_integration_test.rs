use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{SyncEntity, SyncEntityType, SyncStatus};

#[test]
fn test_sync_sqlite_integration_flow() {
    let mut core = MahoCore::new().with_storage(":memory:");

    core.start_sync("wss://localhost:8080", "room-123");

    let payload = serde_json::json!({"action": "test-action", "enabled": true}).to_string();
    let version = core.next_hlc_ts().expect("next_hlc_ts should succeed on in-memory storage");
    let entity = SyncEntity {
        entity_type: SyncEntityType::Shortcut,
        entity_id: "test-action".to_string(),
        version,
        device_id: 12345,
        schema_version: 1,
        modified_at: 1000,
        payload_json: payload,
        deleted: false,
        queue_row_id: None,
    };

    core.set_sync_status(SyncStatus::Synced);
    core.sync_push_entity_persisted(entity.clone());

    let outgoing = core.drain_sync_outgoing();
    assert_eq!(outgoing.len(), 1);

    let pending = core.storage_ref().unwrap().load_pending_sync_entities().unwrap();
    assert!(pending.is_empty());

    core.ack_sync_entity("shortcut", "test-action", version);

    let mut remote_entity = entity.clone();
    remote_entity.version = version + 1;
    let applied = core.apply_sync_remote_entities(vec![remote_entity]);
    assert_eq!(applied.len(), 1);

    let mut older_entity = entity.clone();
    older_entity.version = version.saturating_sub(1);
    let applied_older = core.apply_sync_remote_entities(vec![older_entity]);
    assert!(applied_older.is_empty());
}

#[test]
fn test_sync_survives_restart_sqlite() {
    let tmp = tempfile::NamedTempFile::new().expect("temp file");
    let db_path = tmp.path().to_str().expect("utf-8 path").to_string();

    let seeded_hlc: u64;
    let entity_id = "restart-entity".to_string();
    let entity_type_str = "shortcut";
    let device_id: u32 = 42;

    {
        let mut core = MahoCore::new().with_storage(&db_path);
        core.start_sync("wss://localhost:8080", "room-restart");
        core.set_sync_status(SyncStatus::Synced);

        seeded_hlc = core.next_hlc_ts().expect("next_hlc_ts");

        let payload = serde_json::json!({"marker": "v1"}).to_string();
        let entity = SyncEntity {
            entity_type: SyncEntityType::Shortcut,
            entity_id: entity_id.clone(),
            version: seeded_hlc,
            device_id,
            schema_version: 1,
            modified_at: 1_700_000_000,
            payload_json: payload,
            deleted: false,
            queue_row_id: None,
        };
        let applied = core.apply_sync_remote_entities(vec![entity]);
        assert_eq!(applied.len(), 1, "first apply should accept the entity");
    }

    {
        let mut core2 = MahoCore::new().with_storage(&db_path);
        core2.start_sync("wss://localhost:8080", "room-restart");
        core2.set_sync_status(SyncStatus::Synced);

        let post_restart_hlc = core2.next_hlc_ts().expect("next_hlc_ts after restart");
        assert!(
            post_restart_hlc > seeded_hlc,
            "HLC must not regress across restart (pre={}, post={})",
            seeded_hlc,
            post_restart_hlc
        );

        let replay_entity = SyncEntity {
            entity_type: SyncEntityType::Shortcut,
            entity_id: entity_id.clone(),
            version: seeded_hlc,
            device_id,
            schema_version: 1,
            modified_at: 1_700_000_000,
            payload_json: serde_json::json!({"marker": "v1"}).to_string(),
            deleted: false,
            queue_row_id: None,
        };
        let applied = core2.apply_sync_remote_entities(vec![replay_entity]);
        assert!(
            applied.is_empty(),
            "replayed entity with same (hlc, device_id) must be rejected after restart"
        );

        let older_replay = SyncEntity {
            entity_type: SyncEntityType::Shortcut,
            entity_id: entity_id.clone(),
            version: seeded_hlc.saturating_sub(1),
            device_id,
            schema_version: 1,
            modified_at: 1_700_000_000,
            payload_json: serde_json::json!({"marker": "older"}).to_string(),
            deleted: false,
            queue_row_id: None,
        };
        let applied_older = core2.apply_sync_remote_entities(vec![older_replay]);
        assert!(
            applied_older.is_empty(),
            "older entity must still be rejected after restart"
        );

        let newer = SyncEntity {
            entity_type: SyncEntityType::Shortcut,
            entity_id: entity_id.clone(),
            version: post_restart_hlc,
            device_id,
            schema_version: 1,
            modified_at: 1_700_000_001,
            payload_json: serde_json::json!({"marker": "v2"}).to_string(),
            deleted: false,
            queue_row_id: None,
        };
        let applied_newer = core2.apply_sync_remote_entities(vec![newer]);
        assert_eq!(
            applied_newer.len(),
            1,
            "newer entity must be accepted after restart"
        );

        let _ = entity_type_str;
    }
}

#[test]
fn test_sync_queue_survives_restart_and_flushes() {
    let tmp = tempfile::NamedTempFile::new().expect("temp file");
    let db_path = tmp.path().to_str().expect("utf-8 path").to_string();

    {
        let mut core = MahoCore::new().with_storage(&db_path);

        let payload = serde_json::json!({"action": "queued-action", "enabled": true}).to_string();
        let version = core.next_hlc_ts().expect("next_hlc_ts");
        let entity = SyncEntity {
            entity_type: SyncEntityType::Shortcut,
            entity_id: "queued-action".to_string(),
            version,
            device_id: 7,
            schema_version: 1,
            modified_at: 1_700_000_000,
            payload_json: payload,
            deleted: false,
            queue_row_id: None,
        };
        core.sync_push_entity_persisted(entity);

        let pending = core
            .storage_ref()
            .unwrap()
            .load_pending_sync_entities()
            .unwrap();
        assert_eq!(pending.len(), 1, "entity should be persisted while sync_manager is None");
    }

    {
        let mut core2 = MahoCore::new().with_storage(&db_path);

        let pending_before = core2
            .storage_ref()
            .unwrap()
            .load_pending_sync_entities()
            .unwrap();
        assert_eq!(pending_before.len(), 1, "queue entry must survive restart");

        core2.start_sync("wss://localhost:8080", "room-queue");
        core2.set_sync_status(SyncStatus::Synced);

        let msgs = core2.drain_sync_outgoing();
        assert_eq!(
            msgs.len(),
            1,
            "restarted sync_manager must flush persisted queue entry"
        );

        let pending_after = core2
            .storage_ref()
            .unwrap()
            .load_pending_sync_entities()
            .unwrap();
        assert!(
            pending_after.is_empty(),
            "queue entry must be marked sent after flush"
        );
    }
}
