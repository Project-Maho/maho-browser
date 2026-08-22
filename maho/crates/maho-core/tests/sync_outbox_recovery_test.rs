use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{SyncEntity, SyncEntityType, SyncStatus};

#[test]
fn outbox_row_stays_retryable_until_the_exact_ack_arrives() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let database = tempfile::NamedTempFile::new().expect("database");
    let path = database.path().to_str().expect("path").to_string();
    let seed_core = MahoCore::new();
    let details: serde_json::Value =
        serde_json::from_str(&seed_core.generate_sync_key()).expect("recovery phrase");
    let phrase = details["recoveryPhrase"]
        .as_str()
        .expect("phrase")
        .to_string();

    let mut core = MahoCore::new().with_storage(&path);
    core.configure_sync_encryption_for_recovery_phrase("wss://relay.test", &phrase)
        .expect("configure sync");
    core.set_sync_status(SyncStatus::Synced);
    core.sync_push_entity_persisted(SyncEntity {
        entity_type: SyncEntityType::Shortcut,
        entity_id: "ack-required".to_string(),
        version: core.next_hlc_ts().expect("HLC"),
        device_id: 1,
        schema_version: 1,
        modified_at: 1_700_000_000,
        payload_json: serde_json::json!({"action": "ack-required"}).to_string(),
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: None,
    });

    let delivery_id = core
        .drain_sync_outgoing_envelopes()
        .expect("lease")
        .into_iter()
        .next()
        .expect("envelope")
        .delivery_id;
    assert_eq!(
        core.storage_ref()
            .expect("storage")
            .pending_sync_outbox_count()
            .expect("count"),
        1
    );
    assert!(core.ack_sync_delivery(&delivery_id, 1).expect("ACK"));
    assert_eq!(
        core.storage_ref()
            .expect("storage")
            .pending_sync_outbox_count()
            .expect("count"),
        0
    );
}
