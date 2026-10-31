//! TEMPORARY AUDIT PROBE 2 — delete after run.
use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{build_fields_hlc, SyncEntity, SyncEntityType, SyncStatus};

fn bookmark_payload(title: &str) -> String {
    serde_json::json!({
        "id": "bm-1",
        "url": "https://example.com",
        "title": title,
        "folder_id": null,
        "favicon": null,
        "created_at": "2026-07-09T00:00:00Z"
    })
    .to_string()
}

fn ent(version: u64, device_id: u32, payload: String, deleted: bool) -> SyncEntity {
    let fields = if deleted {
        None
    } else {
        build_fields_hlc(&payload, version, device_id)
    };
    SyncEntity {
        entity_type: SyncEntityType::Bookmark,
        entity_id: "bm-1".to_string(),
        version,
        device_id,
        schema_version: 1,
        modified_at: 1_700_000_000,
        payload_json: payload,
        deleted,
        queue_row_id: None,
        fields_hlc_json: fields,
        profile_id: None,
    }
}

/// What does entity_versions hold after a tombstone is applied?
#[test]
fn probe_entity_versions_row_after_tombstone() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let dir = tempfile::tempdir().unwrap();
    let db = dir.path().join("t.db");
    let db_s = db.to_str().unwrap().to_string();
    {
        let mut core = MahoCore::new().with_storage(&db_s);
        core.start_sync("wss://localhost:8080", "room");
        core.set_sync_status(SyncStatus::Synced);
        let now = core.next_hlc_ts().unwrap();
        core.apply_sync_remote_entities(vec![ent(now, 7, bookmark_payload("Original"), false)]);
        core.apply_sync_remote_entities(vec![ent(now + 10, 7, "{}".to_string(), true)]);
    }
    let s = maho_storage::sqlite::SqliteStorage::open(&db_s).unwrap();
    let row = s.get_entity_version("bookmark", "bm-1").unwrap();
    println!("PROBE entity_versions after tombstone = {:?}", row);
}

/// Keyless push while "Synced": is the queued entity lost from memory?
#[test]
fn probe_keyless_push_drops_entity() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let dir = tempfile::tempdir().unwrap();
    let db = dir.path().join("t2.db");
    let db_s = db.to_str().unwrap().to_string();
    let mut core = MahoCore::new().with_storage(&db_s);
    core.start_sync("wss://localhost:8080", "room"); // no encryption key
    core.set_sync_status(SyncStatus::Synced);
    core.add_bookmark("https://a.example", "A", None);
    println!("PROBE outgoing = {}", core.drain_sync_outgoing().len());
    println!(
        "PROBE last transport error = {:?}",
        core.sync_last_transport_error().map(|e| e.code())
    );
    let s = maho_storage::sqlite::SqliteStorage::open(&db_s).unwrap();
    println!(
        "PROBE pending queue rows = {}",
        s.load_pending_sync_entities().unwrap().len()
    );
}

/// Ordering: does the type-priority sort actually order conversation types?
#[test]
fn probe_sort_stability() {
    let types = [
        SyncEntityType::ConversationTurn,
        SyncEntityType::Conversation,
        SyncEntityType::ConversationProject,
        SyncEntityType::Bookmark,
    ];
    let mut v: Vec<_> = types.iter().cloned().collect();
    v.sort_by_key(|t| match t {
        SyncEntityType::ConversationProject => 0,
        SyncEntityType::Conversation => 1,
        SyncEntityType::ConversationTurn => 2,
        _ => 0,
    });
    println!("PROBE sorted = {:?}", v);
}

/// Is a version regression possible for a locally-edited entity that had a
/// higher remote HLC? (create_sync_entity reuses the stored version.)
#[test]
fn probe_snapshot_entity_version_reuse() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let dir = tempfile::tempdir().unwrap();
    let db = dir.path().join("t3.db");
    let db_s = db.to_str().unwrap().to_string();
    let mut core = MahoCore::new().with_storage(&db_s);
    core.start_sync("wss://localhost:8080", "room");
    core.set_sync_status(SyncStatus::Synced);
    let now = core.next_hlc_ts().unwrap();
    core.apply_sync_remote_entities(vec![ent(now, 7, bookmark_payload("Remote"), false)]);
    let s = maho_storage::sqlite::SqliteStorage::open(&db_s).unwrap();
    println!(
        "PROBE stored version after remote apply = {:?}",
        s.get_entity_version("bookmark", "bm-1")
            .unwrap()
            .map(|r| (r.0, r.1))
    );
    println!("PROBE remote version was {}", now);
}

/// Does mark_sync_entities_sent + no ack leave rows recoverable?
#[test]
fn probe_sent_but_unacked_rows() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let dir = tempfile::tempdir().unwrap();
    let db = dir.path().join("t4.db");
    let db_s = db.to_str().unwrap().to_string();
    let s = maho_storage::sqlite::SqliteStorage::open(&db_s).unwrap();
    let id = s
        .enqueue_sync_entity("bookmark", "bm-x", 5, 1, "{}", false)
        .unwrap();
    s.mark_sync_entities_sent(&[id]).unwrap();
    println!(
        "PROBE pending after mark sent = {}",
        s.load_pending_sync_entities().unwrap().len()
    );
    drop(s);
    // reopen -> does the requeue-stale-sent migration path run?
    let s2 = maho_storage::sqlite::SqliteStorage::open(&db_s).unwrap();
    println!(
        "PROBE pending after reopen = {}",
        s2.load_pending_sync_entities().unwrap().len()
    );
}
