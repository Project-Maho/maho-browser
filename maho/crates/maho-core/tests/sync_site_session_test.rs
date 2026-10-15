use maho_core::sync_manager::SyncManager;
use maho_core::sync_models::{
    build_fields_hlc, canonical_site_session_origin, SiteSessionOriginError, SiteSessionOriginSet,
    SiteSessionRecord, SyncConfig, SyncEntity, SyncEntityType,
};
use std::collections::HashMap;

fn config() -> SyncConfig {
    SyncConfig {
        server_url: "wss://relay.test".to_string(),
        sync_key: Some("room".to_string()),
        device_id: "site-session-test-device".to_string(),
        device_name: "Site Session Test".to_string(),
        auto_sync: true,
        sync_interval_secs: 30,
        disabled_entity_types: Default::default(),
    }
}

fn entity(profile_id: &str, version: u64, device_id: u32, deleted: bool) -> SyncEntity {
    let payload_json = serde_json::json!({
        "origin": "https://app.example.com",
        "cookies": ["sid=abc"],
        "localStorage": {"theme": "dark"}
    })
    .to_string();
    SyncEntity {
        entity_type: SyncEntityType::SiteSession,
        entity_id: "https://app.example.com".to_string(),
        profile_id: Some(profile_id.to_string()),
        version,
        device_id,
        schema_version: 1,
        modified_at: 1_700_000_000,
        fields_hlc_json: (!deleted)
            .then(|| build_fields_hlc(&payload_json, version, device_id))
            .flatten(),
        payload_json,
        deleted,
        queue_row_id: None,
    }
}

#[test]
fn origin_validation_canonicalizes_https_and_rejects_other_schemes() {
    assert_eq!(
        canonical_site_session_origin("HTTPS://App.Example.COM:443").expect("canonical origin"),
        "https://app.example.com"
    );
    assert_eq!(
        canonical_site_session_origin("http://app.example.com"),
        Err(SiteSessionOriginError::HttpsRequired)
    );
    assert_eq!(
        canonical_site_session_origin("https://user@app.example.com"),
        Err(SiteSessionOriginError::InvalidOrigin)
    );
    assert_eq!(
        canonical_site_session_origin("https://app.example.com/path"),
        Err(SiteSessionOriginError::InvalidOrigin)
    );
}

#[test]
fn opt_in_set_is_default_off_and_matches_exact_canonical_origins() {
    let mut origins = SiteSessionOriginSet::default();
    assert!(!origins
        .contains("https://app.example.com")
        .expect("valid origin"));
    assert!(origins
        .insert("HTTPS://App.Example.COM:443")
        .expect("insert origin"));
    assert!(origins
        .contains("https://app.example.com")
        .expect("valid origin"));
    assert!(!origins
        .contains("https://sub.app.example.com")
        .expect("subdomain origin"));
    assert!(!origins
        .contains("https://example.com")
        .expect("parent origin"));
    assert!(origins
        .remove("https://app.example.com")
        .expect("remove origin"));
    assert!(!origins
        .contains("https://app.example.com")
        .expect("valid origin"));
}

#[test]
fn record_round_trip_contains_only_cookies_and_local_storage() {
    let record = SiteSessionRecord::new(
        "HTTPS://App.Example.COM",
        vec!["sid=abc".to_string()],
        HashMap::from([("theme".to_string(), "dark".to_string())]),
    )
    .expect("valid record");
    let encoded = serde_json::to_string(&record).expect("serialize record");
    let decoded: SiteSessionRecord = serde_json::from_str(&encoded).expect("deserialize record");
    assert_eq!(decoded, record);
    let value: serde_json::Value = serde_json::from_str(&encoded).expect("record JSON");
    assert!(value.get("cookies").is_some());
    assert!(value.get("localStorage").is_some());
    assert!(value.get("origin").is_some());
    assert!(value.get("indexedDB").is_none());
}

#[test]
fn profile_scoping_tombstones_hlc_ordering_and_wire_payload_are_preserved() {
    let storage = Some(
        maho_storage::sqlite::SqliteStorage::open_in_memory_with_key(
            "sync-site-session-scope-test-key",
        )
        .expect("storage"),
    );
    let mut manager = SyncManager::new(config());

    let mut removed_a = entity("profile-a", 20, 2, true);
    assert!(manager.apply_remote_entity(&mut removed_a, &storage));
    let mut stale_a = entity("profile-a", 19, 9, false);
    assert!(!manager.apply_remote_entity(&mut stale_a, &storage));
    let mut live_b = entity("profile-b", 19, 1, false);
    assert!(manager.apply_remote_entity(&mut live_b, &storage));

    let storage = storage.as_ref().expect("storage ref");
    let id_a = removed_a.storage_entity_id().expect("profile A storage id");
    let id_b = live_b.storage_entity_id().expect("profile B storage id");
    assert_ne!(id_a, id_b);
    assert_eq!(
        storage
            .get_sync_tombstone("sitesession", &id_a)
            .expect("A tombstone"),
        Some((20, 2))
    );
    assert_eq!(
        storage
            .get_sync_tombstone("sitesession", &id_b)
            .expect("B tombstone"),
        None
    );
    let wire = serde_json::to_value(&live_b).expect("wire entity");
    assert_eq!(wire["profileId"].as_str(), Some("profile-b"));
    assert_eq!(wire["entityType"].as_str(), Some("sitesession"));
    assert_eq!(wire["version"].as_u64(), Some(19));
    assert!(wire.get("fieldsHlcJson").is_some());
    let payload: serde_json::Value =
        serde_json::from_str(wire["payloadJson"].as_str().unwrap()).expect("payload JSON");
    assert_eq!(payload["cookies"][0], "sid=abc");
    assert_eq!(payload["localStorage"]["theme"], "dark");
    assert!(payload.get("indexedDB").is_none());
}
