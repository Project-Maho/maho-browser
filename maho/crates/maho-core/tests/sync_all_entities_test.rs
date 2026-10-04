use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{SyncEntity, SyncEntityType, SyncStatus};

#[test]
fn test_sync_all_entities_round_trip() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let mut core = MahoCore::new().with_storage(":memory:");
    core.start_sync("wss://localhost:8080", "room-123");
    core.set_sync_status(SyncStatus::Synced);
    let version = core.next_hlc_ts().unwrap();
    let device_id = 456;

    // 1. Shortcut
    let shortcut_payload = serde_json::json!({
        "action": "navigate_back",
        "enabled": true,
        "keyCombo": {"key": "b", "modifiers": ["cmd"]}
    })
    .to_string();
    let e_shortcut = SyncEntity {
        entity_type: SyncEntityType::Shortcut,
        entity_id: "navigate_back".to_string(),
        version,
        device_id,
        schema_version: 1,
        modified_at: 1000,
        payload_json: shortcut_payload,
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: None,
    };
    let applied = core.apply_sync_remote_entities(vec![e_shortcut]);
    assert_eq!(applied.len(), 1);
    assert!(core.get_all_shortcuts_json().contains("navigate_back"));

    // 2. Extension
    let extension_payload = serde_json::json!({
        "id": "ext-1",
        "enabled": true
    })
    .to_string();
    let e_extension = SyncEntity {
        entity_type: SyncEntityType::Extension,
        entity_id: "ext-1".to_string(),
        version,
        device_id,
        schema_version: 1,
        modified_at: 1000,
        payload_json: extension_payload,
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: None,
    };
    let applied = core.apply_sync_remote_entities(vec![e_extension]);
    assert_eq!(applied.len(), 1);

    // 3. Tab
    let tab_payload = serde_json::json!({
        "id": "tab-1",
        "parentId": null,
        "spaceId": "space-1",
        "url": "https://google.com",
        "title": "Google",
        "favicon": null,
        "state": {"kind": "active"},
        "role": {"type": "normal"},
        "isMuted": false,
        "zoomLevel": 1.0,
        "createdAt": "2026-07-09T00:00:00Z",
        "lastActiveAt": "2026-07-09T00:00:00Z",
        "scrollPosition": {"x": 0.0, "y": 0.0}
    })
    .to_string();
    let e_tab = SyncEntity {
        entity_type: SyncEntityType::Tab,
        entity_id: "tab-1".to_string(),
        version,
        device_id,
        schema_version: 1,
        modified_at: 1000,
        payload_json: tab_payload,
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: Some("default".to_string()),
    };
    let applied = core.apply_sync_remote_entities(vec![e_tab]);
    assert_eq!(applied.len(), 1);
    assert!(core
        .get_tab_view_models()
        .iter()
        .any(|t| t.id.to_string() == "tab-1"));

    // 4. Space
    let space_payload = serde_json::json!({
        "id": "space-1",
        "profileId": "default",
        "name": "Default Space",
        "color": {"hue": 0.5, "saturation": 0.8, "brightness": 0.9, "grain": 0.1},
        "theme": null,
        "icon": null,
        "tabOrder": [],
        "folders": [],
        "rootOrder": [],
        "atcRules": [],
        "isActive": true,
        "createdAt": "2026-07-09T00:00:00Z",
        "lastActiveTabId": null
    })
    .to_string();
    let e_space = SyncEntity {
        entity_type: SyncEntityType::Space,
        entity_id: "space-1".to_string(),
        version,
        device_id,
        schema_version: 1,
        modified_at: 1000,
        payload_json: space_payload,
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: Some("default".to_string()),
    };
    let applied = core.apply_sync_remote_entities(vec![e_space]);
    assert_eq!(applied.len(), 1);
    assert!(core
        .get_space_view_models()
        .iter()
        .any(|s| s.id.to_string() == "space-1"));

    // 5. Bookmark
    let bookmark_payload = serde_json::json!({
        "id": "bm-1",
        "title": "Google",
        "url": "https://google.com",
        "folder_id": null,
        "favicon": null,
        "created_at": "2026-07-09T00:00:00Z"
    })
    .to_string();
    let e_bookmark = SyncEntity {
        entity_type: SyncEntityType::Bookmark,
        entity_id: "bm-1".to_string(),
        version,
        device_id,
        schema_version: 1,
        modified_at: 1000,
        payload_json: bookmark_payload,
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: Some("default".to_string()),
    };
    let applied = core.apply_sync_remote_entities(vec![e_bookmark]);
    assert_eq!(applied.len(), 1);
    assert!(core
        .search_bookmarks("Google")
        .iter()
        .any(|b| b.0 == "bm-1"));

    // 6. Note
    let note_payload = serde_json::json!({
        "id": "note-1",
        "linkedTabId": null,
        "linkedUrl": null,
        "content": "Secret note",
        "createdAt": "2026-07-09T00:00:00Z",
        "updatedAt": "2026-07-09T00:00:00Z"
    })
    .to_string();
    let e_note = SyncEntity {
        entity_type: SyncEntityType::Note,
        entity_id: "note-1".to_string(),
        version,
        device_id,
        schema_version: 1,
        modified_at: 1000,
        payload_json: note_payload,
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: Some("default".to_string()),
    };
    let applied = core.apply_sync_remote_entities(vec![e_note]);
    assert_eq!(applied.len(), 1);
    assert!(core
        .get_note_view_models()
        .iter()
        .any(|n| n.id.to_string() == "note-1"));

    // 7. Boost
    let boost_payload = serde_json::json!({
        "id": "boost-1",
        "domain": "google.com",
        "name": "Google Custom",
        "color": {
            "dotPos": {"x": 0.0, "y": 0.0},
            "dotDistance": 0.0,
            "dotAngleDeg": 0.0,
            "secondaryDotPos": {"x": 0.0, "y": 0.0},
            "secondaryDotAngleDegDelta": 0.0,
            "magicTheme": false,
            "colorBoostEnabled": false,
            "smartInvert": false,
            "contrast": 1.0,
            "brightness": 1.0,
            "saturation": 1.0
        },
        "typography": {
            "fontFamily": "Inter",
            "caseMode": "none",
            "sizeMode": "k100"
        },
        "zapSelectors": [],
        "customCss": "body { background: red; }",
        "changeWasMade": false,
        "createdAt": "2026-07-09T00:00:00Z",
        "updatedAt": "2026-07-09T00:00:00Z"
    })
    .to_string();
    let e_boost = SyncEntity {
        entity_type: SyncEntityType::Boost,
        entity_id: "boost-1".to_string(),
        version,
        device_id,
        schema_version: 1,
        modified_at: 1000,
        payload_json: boost_payload,
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: Some("default".to_string()),
    };
    let applied = core.apply_sync_remote_entities(vec![e_boost]);
    assert_eq!(applied.len(), 1);
    assert!(core
        .get_boost_by_id(&maho_types::identifiers::BoostId::new("boost-1"))
        .is_some());

    // 8. ReadingListItem
    let item_payload = serde_json::json!({
        "id": "item-1",
        "url": "https://wikipedia.org",
        "title": "Wikipedia",
        "excerpt": null,
        "site_name": null,
        "favicon_url": null,
        "preview_image_url": null,
        "added_at": "2026-07-09T00:00:00Z",
        "read_at": null,
        "is_read": false,
        "estimated_read_minutes": null,
        "offline_content": null,
        "tags": []
    })
    .to_string();
    let e_item = SyncEntity {
        entity_type: SyncEntityType::ReadingListItem,
        entity_id: "item-1".to_string(),
        version,
        device_id,
        schema_version: 1,
        modified_at: 1000,
        payload_json: item_payload,
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: Some("default".to_string()),
    };
    let applied = core.apply_sync_remote_entities(vec![e_item]);
    assert_eq!(applied.len(), 1);
    assert!(core.get_reading_list_json().contains("Wikipedia"));

    // 9. SearchEngine
    let engine_payload = serde_json::json!({
        "id": "engine-1",
        "name": "Wikipedia Search",
        "urlTemplate": "https://wikipedia.org/wiki/{query}",
        "shortcut": "@w",
        "iconUrl": null,
        "isDefault": false
    })
    .to_string();
    let e_engine = SyncEntity {
        entity_type: SyncEntityType::SearchEngine,
        entity_id: "engine-1".to_string(),
        version,
        device_id,
        schema_version: 1,
        modified_at: 1000,
        payload_json: engine_payload,
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: None,
    };
    let applied = core.apply_sync_remote_entities(vec![e_engine]);
    assert_eq!(applied.len(), 1);
    assert!(core
        .get_search_engines()
        .iter()
        .any(|se| se.id == "engine-1"));

    // 10. Memory
    let memory_payload = serde_json::json!({
        "fact": "The user likes rust",
        "source": "chat",
        "session_id": "session-1",
        "categories": "tech",
        "importance": 0.9,
        "metadata": null
    })
    .to_string();
    let e_memory = SyncEntity {
        entity_type: SyncEntityType::Memory,
        entity_id: "memory-1".to_string(),
        version,
        device_id,
        schema_version: 1,
        modified_at: 1000,
        payload_json: memory_payload,
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: None,
    };
    let applied = core.apply_sync_remote_entities(vec![e_memory]);
    assert_eq!(applied.len(), 1);
}
