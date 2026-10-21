use maho_core::maho_core::MahoCore;
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

fn device(seed: u8) -> MahoCore {
    let _ = maho_storage::sqlite::set_sqlcipher_key("site-session-device-key");
    let mut core = MahoCore::new().with_storage(":memory:");
    use base64::Engine;
    let seed_b64 = base64::engine::general_purpose::STANDARD.encode([seed; 32]);
    core.configure_sync_encryption_from_bootstrap("https://relay.mahobrowser.com", &seed_b64)
        .expect("configure sync key");
    core
}

fn paired_devices() -> (MahoCore, MahoCore) {
    let mut a = device(7);
    let mut b = device(7);
    let profile = a.list_profiles()[0].clone();
    a.adopt_profile_for_sync_test(profile.clone());
    b.adopt_profile_for_sync_test(profile);
    (a, b)
}

fn record(origin: &str, sid: &str) -> SiteSessionRecord {
    SiteSessionRecord::new(
        origin,
        vec![format!("sid={sid}")],
        HashMap::from([("theme".to_string(), "dark".to_string())]),
    )
    .expect("valid record")
}

fn opt_in(core: &mut MahoCore, origins: &[&str]) {
    let origins: Vec<String> = origins.iter().map(|o| o.to_string()).collect();
    core.set_site_session_opted_origins(&origins)
        .expect("opt in");
}

#[test]
fn two_devices_round_trip_only_opted_origins() {
    let (mut a, mut b) = paired_devices();
    opt_in(&mut a, &["https://app.example.com"]);
    opt_in(&mut b, &["https://app.example.com"]);
    assert!(a
        .record_site_session(record("https://app.example.com", "abc"))
        .is_ok());
    assert!(
        a.record_site_session(record("https://other.example.com", "zzz"))
            .is_err(),
        "a non-opted origin is never recorded"
    );

    let snapshot = a.export_sync_snapshot().expect("export");
    assert!(
        !snapshot.contains("sid=abc"),
        "site data never leaves in plaintext"
    );
    b.apply_sync_snapshot(&snapshot).expect("apply");

    let applied = b.take_site_session_applies();
    assert_eq!(applied.len(), 1);
    assert_eq!(applied[0].origin, "https://app.example.com");
    assert_eq!(applied[0].cookies, vec!["sid=abc".to_string()]);
    assert!(
        b.take_site_session_applies().is_empty(),
        "each record is handed out once"
    );

    b.apply_sync_snapshot(&snapshot).expect("re-apply");
    assert!(
        b.take_site_session_applies().is_empty(),
        "re-applying the same snapshot is idempotent"
    );
}

#[test]
fn receiving_device_that_did_not_opt_in_applies_nothing() {
    let (mut a, mut b) = paired_devices();
    opt_in(&mut a, &["https://app.example.com"]);
    a.record_site_session(record("https://app.example.com", "abc"))
        .expect("record");
    b.apply_sync_snapshot(&a.export_sync_snapshot().expect("export"))
        .expect("apply");
    assert!(b.take_site_session_applies().is_empty());
}

#[test]
fn opting_out_stops_export_and_drops_pending_applies() {
    let (mut a, mut b) = paired_devices();
    opt_in(&mut a, &["https://app.example.com"]);
    opt_in(&mut b, &["https://app.example.com"]);
    a.record_site_session(record("https://app.example.com", "abc"))
        .expect("record");
    let before = a.export_sync_snapshot().expect("export");
    b.apply_sync_snapshot(&before).expect("apply");

    opt_in(&mut b, &[]);
    assert!(
        b.take_site_session_applies().is_empty(),
        "opt-out drops pending data"
    );

    opt_in(&mut a, &[]);
    assert!(a.site_session_origins_for_export().is_empty());
    let after = a.export_sync_snapshot().expect("export after opt-out");
    let mut c = device(7);
    c.adopt_profile_for_sync_test(a.list_profiles()[0].clone());
    opt_in(&mut c, &["https://app.example.com"]);
    c.apply_sync_snapshot(&after).expect("apply");
    assert!(
        c.take_site_session_applies().is_empty(),
        "opted-out origin is not exported"
    );
}

#[test]
fn forged_envelope_and_malformed_or_oversize_records_are_rejected() {
    let (mut a, mut b) = paired_devices();
    opt_in(&mut a, &["https://app.example.com"]);
    opt_in(&mut b, &["https://app.example.com"]);

    let mut forger = device(9);
    forger.adopt_profile_for_sync_test(a.list_profiles()[0].clone());
    opt_in(&mut forger, &["https://app.example.com"]);
    forger
        .record_site_session(record("https://app.example.com", "evil"))
        .expect("record");
    let forged = forger.export_sync_snapshot().expect("export");
    assert!(
        b.apply_sync_snapshot(&forged).is_err(),
        "another key's envelope is rejected"
    );
    assert!(b.take_site_session_applies().is_empty());

    let oversize = SiteSessionRecord::new(
        "https://app.example.com",
        vec!["x".repeat(600 * 1024)],
        HashMap::new(),
    )
    .expect("record");
    assert!(a.record_site_session(oversize).is_err());
    assert!(a
        .set_site_session_opted_origins(&["http://app.example.com".to_string()])
        .is_err());
    assert!(a.set_site_session_opted_origins(&["".to_string()]).is_err());
    assert_eq!(
        a.site_session_origins_for_export(),
        vec!["https://app.example.com".to_string()],
        "a rejected update leaves the opt-in set unchanged"
    );
}

#[test]
fn newer_record_wins_and_profiles_stay_separate() {
    let (mut a, mut b) = paired_devices();
    opt_in(&mut a, &["https://app.example.com"]);
    opt_in(&mut b, &["https://app.example.com"]);
    a.record_site_session(record("https://app.example.com", "old"))
        .expect("record");
    a.record_site_session(record("https://app.example.com", "new"))
        .expect("record");
    b.apply_sync_snapshot(&a.export_sync_snapshot().expect("export"))
        .expect("apply");
    let applied = b.take_site_session_applies();
    assert_eq!(applied.len(), 1);
    assert_eq!(applied[0].cookies, vec!["sid=new".to_string()]);

    let mut other_profile = device(7);
    opt_in(&mut other_profile, &["https://app.example.com"]);
    other_profile
        .apply_sync_snapshot(&a.export_sync_snapshot().expect("export"))
        .expect("apply");
    assert!(
        other_profile.take_site_session_applies().is_empty(),
        "a different profile never receives this profile's sessions"
    );
}
