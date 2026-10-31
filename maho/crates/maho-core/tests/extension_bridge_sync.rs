use maho_core::extension_bridge::InstalledExtension;
use maho_core::maho_core::MahoCore;
use maho_types::events::core_update::CoreUpdate;
use std::cell::RefCell;
use std::rc::Rc;

#[test]
fn set_installed_extensions_replaces_map() {
    let mut core = MahoCore::new();
    assert!(core.get_installed_extensions().is_empty());

    let extensions = vec![
        InstalledExtension {
            id: "ext1".to_string(),
            name: "Extension One".to_string(),
            version: "1.0.0".to_string(),
            description: String::new(),
            enabled: true,
            permissions: vec![],
            manifest_version: 0,
        },
        InstalledExtension {
            id: "ext2".to_string(),
            name: "Extension Two".to_string(),
            version: "2.1.3".to_string(),
            description: String::new(),
            enabled: false,
            permissions: vec![],
            manifest_version: 0,
        },
    ];

    core.set_installed_extensions(extensions.clone());
    let installed = core.get_installed_extensions();
    assert_eq!(installed.len(), 2);

    let ext1 = installed.iter().find(|e| e.id == "ext1").unwrap();
    assert_eq!(ext1.name, "Extension One");
    assert!(ext1.enabled);

    let ext2 = installed.iter().find(|e| e.id == "ext2").unwrap();
    assert_eq!(ext2.name, "Extension Two");
    assert!(!ext2.enabled);
}

#[test]
fn clear_installed_extensions_empties_map() {
    let mut core = MahoCore::new();
    let extensions = vec![InstalledExtension {
        id: "ext1".to_string(),
        name: "Extension One".to_string(),
        version: "1.0.0".to_string(),
        description: String::new(),
        enabled: true,
        permissions: vec![],
        manifest_version: 0,
    }];

    core.set_installed_extensions(extensions);
    assert_eq!(core.get_installed_extensions().len(), 1);

    core.clear_installed_extensions();
    assert!(core.get_installed_extensions().is_empty());
}

#[test]
fn set_then_toggle_keeps_id_and_flips_enabled() {
    let mut core = MahoCore::new();
    let extensions = vec![InstalledExtension {
        id: "ext1".to_string(),
        name: "Extension One".to_string(),
        version: "1.0.0".to_string(),
        description: String::new(),
        enabled: true,
        permissions: vec![],
        manifest_version: 0,
    }];

    core.set_installed_extensions(extensions);
    let enabled = core.toggle_extension("ext1");
    assert_eq!(enabled, Some(false));

    let installed = core.get_installed_extensions();
    let ext1 = installed.iter().find(|e| e.id == "ext1").unwrap();
    assert!(!ext1.enabled);
}

#[test]
fn register_extension_emits_update() {
    let mut core = MahoCore::new();
    let updates = Rc::new(RefCell::new(Vec::new()));

    let updates_clone = updates.clone();
    core.on_update(Box::new(move |update| {
        updates_clone.borrow_mut().push(update);
    }));

    core.register_extension(InstalledExtension {
        id: "ext1".to_string(),
        name: "Extension One".to_string(),
        version: "1.0.0".to_string(),
        description: String::new(),
        enabled: true,
        permissions: vec![],
        manifest_version: 0,
    });

    let received = updates.borrow();
    assert_eq!(received.len(), 1);
    assert!(matches!(
        received[0],
        CoreUpdate::InstalledExtensionsUpdated
    ));
}

#[test]
fn test_extension_sync_push_and_callback() {
    let mut core = MahoCore::new();
    core.start_sync("wss://localhost:8080", "test-room");
    let seed = maho_core::sync_crypto::generate_sync_seed();
    let phrase = maho_core::sync_crypto::encode_recovery_phrase(&seed).unwrap();
    core.configure_sync_encryption_for_recovery_phrase("wss://localhost:8080", &phrase)
        .expect("configure sync encryption");
    core.set_sync_status(maho_core::sync_models::SyncStatus::Synced);

    // Register sync callback to trace remote events
    use std::sync::{Arc, Mutex};
    let cb_called = Arc::new(Mutex::new(None));
    let cb_called_clone = cb_called.clone();
    core.register_extension_sync_callback(Some(Box::new(move |id, enabled, deleted| {
        *cb_called_clone.lock().unwrap() = Some((id.to_string(), enabled, deleted));
    })));

    let extensions = vec![InstalledExtension {
        id: "ext-sync-test".to_string(),
        name: "Sync Test".to_string(),
        version: "1.0.0".to_string(),
        description: String::new(),
        enabled: true,
        permissions: vec![],
        manifest_version: 0,
    }];
    core.set_installed_extensions(extensions);

    let extensions = vec![InstalledExtension {
        id: "ext-sync-test".to_string(),
        name: "Sync Test".to_string(),
        version: "1.0.0".to_string(),
        description: String::new(),
        enabled: false,
        permissions: vec![],
        manifest_version: 0,
    }];
    core.set_installed_extensions(extensions);

    // Verify that the entity is pushed in outgoing queue
    let outgoing = core.drain_sync_outgoing();
    assert!(!outgoing.is_empty());
    let key = maho_core::sync_crypto::derive_encryption_key(&seed).unwrap();
    let message = match &outgoing[0] {
        maho_core::sync_models::SyncMessage::Encrypted { data } => {
            let decrypted = maho_core::sync_crypto::decrypt_update(data, &key).unwrap();
            serde_json::from_slice::<maho_core::sync_models::SyncMessage>(&decrypted).unwrap()
        }
        maho_core::sync_models::SyncMessage::EntityPush { entity } => {
            maho_core::sync_models::SyncMessage::EntityPush {
                entity: entity.clone(),
            }
        }
        _ => panic!("Expected EncryptedPayload or EntityPush"),
    };
    let local_version = match &message {
        maho_core::sync_models::SyncMessage::EntityPush { entity } => entity.version,
        _ => panic!("Expected EntityPush inside payload"),
    };

    // Step 2: Receive remote sync entity toggling enabled state to false
    use maho_core::sync_models::{SyncEntity, SyncEntityType};
    let remote_entity = SyncEntity {
        entity_type: SyncEntityType::Extension,
        entity_id: "ext-sync-test".to_string(),
        version: local_version + 1, // newer version to trigger LWW update
        device_id: 0,
        schema_version: 1,
        modified_at: 12345678,
        payload_json: serde_json::json!({
            "id": "ext-sync-test",
            "enabled": false,
        })
        .to_string(),
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: None,
    };

    let applied = core.apply_sync_remote_entities(vec![remote_entity]);
    assert_eq!(applied.len(), 1);

    // Verify callback was called
    let cb_res = cb_called.lock().unwrap().clone();
    assert!(cb_res.is_some());
    let (cb_id, cb_enabled, cb_deleted) = cb_res.unwrap();
    assert_eq!(cb_id, "ext-sync-test");
    assert_eq!(cb_enabled, false);
    assert_eq!(cb_deleted, false);

    // Verify local extension state was updated
    let installed = core.get_installed_extensions();
    let ext = installed.iter().find(|e| e.id == "ext-sync-test").unwrap();
    assert!(!ext.enabled);
}

#[test]
fn extension_can_register_side_panel() {
    use maho_core::extension_bridge::{ExtensionBridge, SidePanelLayout, SidePanelOptions};
    let mut bridge = ExtensionBridge::new();
    let ext_id = "ext1";
    bridge.register_side_panel(
        ext_id,
        SidePanelOptions {
            path: "panel.html".into(),
            layout: SidePanelLayout::Right,
            default_width: 320,
        },
    );
    let panels = bridge.list_side_panels();
    assert_eq!(panels.len(), 1);
    assert_eq!(panels[0].extension_id, ext_id);
    assert_eq!(panels[0].path, "panel.html");
    assert_eq!(panels[0].layout, SidePanelLayout::Right);
    assert_eq!(panels[0].default_width, 320);

    bridge.unregister_side_panel(ext_id);
    assert_eq!(bridge.list_side_panels().len(), 0);
}
