use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{SyncEntity, SyncEntityType, SyncStatus};

#[test]
fn test_sync_sqlite_integration_flow() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let mut core = MahoCore::new().with_storage(":memory:");

    // Fail-closed E2EE requires an installed key before entity transport.
    let key_info: serde_json::Value =
        serde_json::from_str(&core.generate_sync_key()).expect("key info");
    let phrase = key_info["recoveryPhrase"]
        .as_str()
        .expect("phrase")
        .to_string();
    core.configure_sync_encryption_for_recovery_phrase("wss://localhost:8080", &phrase)
        .expect("configure encryption");

    let payload = serde_json::json!({"action": "test-action", "enabled": true}).to_string();
    let version = core
        .next_hlc_ts()
        .expect("next_hlc_ts should succeed on in-memory storage");
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
        fields_hlc_json: None,
        profile_id: None,
    };

    core.set_sync_status(SyncStatus::Synced);
    core.sync_push_entity_persisted(entity.clone());

    let outgoing = core
        .drain_sync_outgoing_envelopes()
        .expect("V2 envelope drain");
    assert_eq!(outgoing.len(), 1);

    let pending = core
        .storage_ref()
        .unwrap()
        .load_pending_sync_entities()
        .unwrap();
    assert_eq!(pending.len(), 1, "outbox remains durable until relay ACK");

    assert!(
        core.ack_sync_delivery(&outgoing[0].delivery_id, 1)
            .expect("ACK delivery"),
        "relay ACK must complete the matching durable outbox row"
    );
    assert!(
        core.storage_ref()
            .unwrap()
            .load_pending_sync_entities()
            .unwrap()
            .is_empty(),
        "ACKed outbox entry must not retry"
    );

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
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
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
            fields_hlc_json: None,
            profile_id: None,
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
            fields_hlc_json: None,
            profile_id: None,
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
            fields_hlc_json: None,
            profile_id: None,
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
            fields_hlc_json: None,
            profile_id: None,
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
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
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
            fields_hlc_json: None,
            profile_id: None,
        };
        core.sync_push_entity_persisted(entity);

        let pending = core
            .storage_ref()
            .unwrap()
            .load_pending_sync_entities()
            .unwrap();
        assert_eq!(
            pending.len(),
            1,
            "entity should be persisted while sync_manager is None"
        );
    }

    {
        let mut core2 = MahoCore::new().with_storage(&db_path);

        let pending_before = core2
            .storage_ref()
            .unwrap()
            .load_pending_sync_entities()
            .unwrap();
        assert_eq!(pending_before.len(), 1, "queue entry must survive restart");

        // Fail-closed E2EE requires an installed key before the queue can flush.
        let key_info: serde_json::Value =
            serde_json::from_str(&core2.generate_sync_key()).expect("key info");
        let phrase = key_info["recoveryPhrase"]
            .as_str()
            .expect("phrase")
            .to_string();
        core2
            .configure_sync_encryption_for_recovery_phrase("wss://localhost:8080", &phrase)
            .expect("configure encryption");
        core2.set_sync_status(SyncStatus::Synced);

        let msgs = core2
            .drain_sync_outgoing_envelopes()
            .expect("drain V2 envelopes");
        assert_eq!(
            msgs.len(),
            1,
            "restarted sync_manager must flush persisted queue entry (encrypted)"
        );
        assert!(
            matches!(
                serde_json::from_slice::<maho_core::sync_models::SyncMessage>(
                    &msgs[0].payload_bytes().expect("decode envelope")
                )
                .expect("decode encrypted message"),
                maho_core::sync_models::SyncMessage::Encrypted { .. }
            ),
            "flushed queue entry must be encrypted, never plaintext"
        );

        let pending_after = core2
            .storage_ref()
            .unwrap()
            .load_pending_sync_entities()
            .unwrap();
        assert_eq!(
            pending_after.len(),
            1,
            "queue entry must remain until the relay acknowledges its delivery"
        );
        core2
            .ack_sync_delivery(&msgs[0].delivery_id, 2)
            .expect("ACK restarted delivery");
        assert!(
            core2
                .storage_ref()
                .unwrap()
                .load_pending_sync_entities()
                .unwrap()
                .is_empty(),
            "ACKed restarted delivery must leave the durable outbox"
        );
    }
}
