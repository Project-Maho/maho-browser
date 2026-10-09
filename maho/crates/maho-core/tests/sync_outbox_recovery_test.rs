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
