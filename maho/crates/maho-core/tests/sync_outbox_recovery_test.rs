use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{SyncEntity, SyncEntityType, SyncStatus};

#[test]
fn inactive_extension_changes_do_not_write_an_outbox() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let mut core = MahoCore::new().with_storage(":memory:");
    let mut extension = maho_core::extension_bridge::InstalledExtension {
        id: "offline-extension".into(), enabled: true, ..Default::default()
    };
    core.set_installed_extensions(vec![extension.clone()]);
    extension.enabled = false;
    core.set_installed_extensions(vec![extension]);
    assert_eq!(core.storage_ref().unwrap().pending_sync_outbox_count().unwrap(), 0);
    core.start_sync("wss://relay.test", "room");
    assert!(core.storage_ref().unwrap().load_pending_sync_entities().unwrap().iter()
        .any(|row| row.1 == "extension" && row.2 == "offline-extension"));
}

#[test]
fn inactive_sync_keeps_local_icons_and_active_sync_omits_image_bytes() {
    use maho_types::events::{core_update::CoreUpdate, shell_event::ShellEvent};
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let mut core = MahoCore::new().with_storage(":memory:");
    let updates = core.handle_event(ShellEvent::CreateTab {
        space_id: core.get_active_space_id().clone(),
        url: Some(maho_types::common::Url::new("https://example.com")),
        parent_id: None, tab_id: None, window_id: None, is_private: false,
    });
    let tab_id = updates.into_iter().find_map(|update| match update {
        CoreUpdate::TabCreated { tab } => Some(tab.id),
        _ => None,
    }).unwrap();
    let bytes = vec![137, 80, 78, 71, 255];
    for _ in 0..50 {
        core.handle_event(ShellEvent::TabFaviconUpdated {
            tab_id: tab_id.clone(),
            favicon: Some(maho_types::common::ImageData {
                data: bytes.clone(), width: 16, height: 16,
                format: maho_types::common::ImageFormat::Png,
            }),
        });
    }
    assert_eq!(core.storage_ref().unwrap().pending_sync_outbox_count().unwrap(), 0);
    let snapshot = core.build_raw_snapshot().unwrap();
    let tab_payload: serde_json::Value = serde_json::from_str(
        &snapshot.entities["Tab"].iter().find(|entity| entity.entity_id == tab_id.to_string()).unwrap().payload_json,
    ).unwrap();
    assert!(tab_payload["favicon"].is_null());
    core.start_sync("wss://relay.test", "room");
    core.push_tab_sync(&tab_id);
    let rows = core.storage_ref().unwrap().load_pending_sync_entities().unwrap();
    let tabs: Vec<_> = rows.iter().filter(|row| row.1 == "tab" && row.2 == tab_id.to_string()).collect();
    assert_eq!(tabs.len(), 1);
    let payload: serde_json::Value = serde_json::from_str(&tabs[0].5).unwrap();
    assert!(payload["favicon"].is_null());
    let sidebar: serde_json::Value = serde_json::from_str(&core.get_sidebar_state_v2(&core.get_active_space_id()).unwrap()).unwrap();
    use base64::Engine as _;
    let local = sidebar["favicons"][tab_id.to_string()].as_str().unwrap();
    assert_eq!(base64::engine::general_purpose::STANDARD.decode(local).unwrap(), bytes);
}

#[test]
fn repeated_unsent_updates_deliver_only_the_latest_payload() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let mut core = MahoCore::new().with_storage(":memory:");
    let details: serde_json::Value = serde_json::from_str(&core.generate_sync_key()).unwrap();
    core.configure_sync_encryption_for_recovery_phrase("wss://relay.test", details["recoveryPhrase"].as_str().unwrap()).unwrap();
    core.set_sync_status(SyncStatus::Synced);
    for version in 1..=100 {
        core.sync_push_entity_persisted(SyncEntity {
            entity_type: SyncEntityType::Shortcut,
            entity_id: "latest".to_string(),
            version,
            device_id: 1,
            schema_version: 1,
            modified_at: version as i64,
            payload_json: serde_json::json!({"action":"latest", "version":version}).to_string(),
            deleted: false,
            queue_row_id: None,
            fields_hlc_json: None,
            profile_id: None,
        });
    }
    let rows = core.storage_ref().unwrap().load_pending_sync_entities().unwrap();
    let latest: Vec<_> = rows.iter().filter(|row| row.2 == "latest").collect();
    assert_eq!(latest.len(), 1);
    assert_eq!(latest[0].3, 100);
    let envelopes = core.drain_sync_outgoing_envelopes().unwrap();
    assert_eq!(envelopes.len(), rows.len());
    for envelope in envelopes { assert!(core.ack_sync_delivery(&envelope.delivery_id, 1).unwrap()); }
    assert_eq!(core.storage_ref().unwrap().pending_sync_outbox_count().unwrap(), 0);
}

#[test]
fn inactive_deletion_survives_reenable_without_an_outbox_write() {
    use maho_types::events::{core_update::CoreUpdate, shell_event::ShellEvent};
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let mut core = MahoCore::new().with_storage(":memory:");
    let updates = core.handle_event(ShellEvent::CreateTab {
        space_id: core.get_active_space_id(), url: Some(maho_types::common::Url::new("https://example.com")),
        parent_id: None, tab_id: None, window_id: None, is_private: false,
    });
    let id = updates.into_iter().find_map(|update| match update {
        CoreUpdate::TabCreated { tab } => Some(tab.id), _ => None,
    }).unwrap();
    core.handle_event(ShellEvent::CloseTab { tab_id: id.clone(), expected_space_id: None });
    assert_eq!(core.storage_ref().unwrap().pending_sync_outbox_count().unwrap(), 0);
    core.start_sync("wss://relay.test", "room");
    let rows = core.storage_ref().unwrap().load_pending_sync_entities().unwrap();
    assert!(rows.iter().any(|row| row.1 == "tab" && row.2 == id.to_string() && row.6));
}

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
    let before = core.storage_ref().unwrap().pending_sync_outbox_count().unwrap();
    assert!(before > 0);
    assert!(core.ack_sync_delivery(&delivery_id, 1).expect("ACK"));
    assert_eq!(
        core.storage_ref()
            .expect("storage")
            .pending_sync_outbox_count()
            .expect("count"),
        before - 1
    );
}

#[test]
fn lease_is_capped_at_the_relay_push_batch_and_keeps_the_remainder_queued() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let mut core = MahoCore::new().with_storage(":memory:");
    let details: serde_json::Value = serde_json::from_str(&core.generate_sync_key()).unwrap();
    core.configure_sync_encryption_for_recovery_phrase("wss://relay.test", details["recoveryPhrase"].as_str().unwrap()).unwrap();
    core.set_sync_status(SyncStatus::Synced);
    for index in 0..70 {
        core.sync_push_entity_persisted(SyncEntity {
            entity_type: SyncEntityType::Shortcut,
            entity_id: format!("batch-{index}"),
            version: 1,
            device_id: 1,
            schema_version: 1,
            modified_at: index,
            payload_json: serde_json::json!({"action": format!("batch-{index}")}).to_string(),
            deleted: false,
            queue_row_id: None,
            fields_hlc_json: None,
            profile_id: None,
        });
    }
    // 70 queued shortcut rows (plus any rows sync bootstrap queued itself).
    let total = core.storage_ref().unwrap().load_pending_sync_entities().unwrap().len();
    assert!((70..=128).contains(&total));

    // An over-limit request is clamped to the relay's MAX_PUSH_BATCH.
    let first = core.drain_sync_outgoing_envelopes_limited(1_000).unwrap();
    assert_eq!(first.len(), maho_core::maho_core::MAX_SYNC_PUSH_BATCH);
    for envelope in &first { assert!(core.ack_sync_delivery(&envelope.delivery_id, 1).unwrap()); }
    assert_eq!(core.storage_ref().unwrap().pending_sync_outbox_count().unwrap(), total - 64);

    // The remainder (6 when only the 70 rows are queued) stays leasable, in order.
    let second = core.drain_sync_outgoing_envelopes_limited(64).unwrap();
    assert_eq!(second.len(), total - 64);
    let first_ids: std::collections::HashSet<_> = first.iter().map(|e| e.delivery_id.clone()).collect();
    assert!(second.iter().all(|e| !first_ids.contains(&e.delivery_id)));
}

fn synced_core_with_shortcuts(ids: &[&str]) -> MahoCore {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let mut core = MahoCore::new().with_storage(":memory:");
    let details: serde_json::Value = serde_json::from_str(&core.generate_sync_key()).unwrap();
    core.configure_sync_encryption_for_recovery_phrase("wss://relay.test", details["recoveryPhrase"].as_str().unwrap()).unwrap();
    core.set_sync_status(SyncStatus::Synced);
    // Flush anything sync bootstrap queued so the tests see only their own rows.
    for envelope in core.drain_sync_outgoing_envelopes().unwrap() {
        assert!(core.ack_sync_delivery(&envelope.delivery_id, 1).unwrap());
    }
    for (index, id) in ids.iter().enumerate() {
        core.sync_push_entity_persisted(SyncEntity {
            entity_type: SyncEntityType::Shortcut,
            entity_id: id.to_string(),
            version: 1,
            device_id: 1,
            schema_version: 1,
            modified_at: index as i64,
            payload_json: serde_json::json!({"action": id}).to_string(),
            deleted: false,
            queue_row_id: None,
            fields_hlc_json: None,
            profile_id: None,
        });
    }
    core
}

#[test]
fn requeued_lease_is_leased_again_with_the_same_deliveries_in_order() {
    let mut core = synced_core_with_shortcuts(&["requeue-a", "requeue-b", "requeue-c"]);

    let first = core.drain_sync_outgoing_envelopes_limited(3).unwrap();
    assert_eq!(first.len(), 3);
    // A failed push: nothing was acknowledged.
    assert_eq!(core.requeue_sync_lease().unwrap(), 3);
    assert_eq!(core.storage_ref().unwrap().pending_sync_outbox_count().unwrap(), 3);

    let second = core.drain_sync_outgoing_envelopes_limited(3).unwrap();
    let ids = |envelopes: &[maho_core::sync_models::SyncEnvelopeV2]| {
        envelopes.iter().map(|e| (e.delivery_id.clone(), e.payload.clone())).collect::<Vec<_>>()
    };
    assert_eq!(ids(&second), ids(&first));

    // Leasing while a lease is outstanding requeues it first; nothing is lost.
    let third = core.drain_sync_outgoing_envelopes_limited(3).unwrap();
    assert_eq!(ids(&third), ids(&first));
    for envelope in &third {
        assert!(core.ack_sync_delivery(&envelope.delivery_id, 1).unwrap());
    }
    assert_eq!(core.storage_ref().unwrap().pending_sync_outbox_count().unwrap(), 0);
    assert_eq!(core.requeue_sync_lease().unwrap(), 0);
    assert!(core.drain_sync_outgoing_envelopes_limited(3).unwrap().is_empty());
}

#[test]
fn rowless_send_tab_ack_succeeds_and_does_not_block_row_acks() {
    let mut core = synced_core_with_shortcuts(&["row-before-send-tab"]);
    core.send_tab_to_device("https://example.com", "Example", "other-device");

    let leased = core.drain_sync_outgoing_envelopes().unwrap();
    assert_eq!(leased.len(), 2, "one outbox row plus one rowless SendTab");

    // A failed push requeues the rowless SendTab with its original delivery ID.
    core.requeue_sync_lease().unwrap();
    let retried = core.drain_sync_outgoing_envelopes().unwrap();
    assert_eq!(
        retried.iter().map(|e| &e.delivery_id).collect::<Vec<_>>(),
        leased.iter().map(|e| &e.delivery_id).collect::<Vec<_>>()
    );

    // ACK the rowless SendTab (queued last) before the row.
    for envelope in retried.iter().rev() {
        assert!(core.ack_sync_delivery(&envelope.delivery_id, 1).unwrap());
    }
    assert_eq!(core.storage_ref().unwrap().pending_sync_outbox_count().unwrap(), 0);
    assert_eq!(core.requeue_sync_lease().unwrap(), 0);
    assert!(core.drain_sync_outgoing_envelopes().unwrap().is_empty());
    assert!(!core.ack_sync_delivery(&uuid::Uuid::new_v4().to_string(), 1).unwrap());
}
