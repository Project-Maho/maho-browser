//! TEMPORARY AUDIT PROBE — delete after run.
use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{build_fields_hlc, SyncEntity, SyncEntityType, SyncStatus};

fn synced_core() -> MahoCore {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let mut core = MahoCore::new().with_storage(":memory:");
    core.start_sync("wss://localhost:8080", "room-audit");
    core.set_sync_status(SyncStatus::Synced);
    core
}

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

fn ent(id: &str, version: u64, device_id: u32, payload: String, deleted: bool) -> SyncEntity {
    let fields = if deleted {
        None
    } else {
        build_fields_hlc(&payload, version, device_id)
    };
    SyncEntity {
        entity_type: SyncEntityType::Bookmark,
        entity_id: id.to_string(),
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

#[test]
fn probe_tombstone_then_stale_update() {
    let mut core = synced_core();
    let now = core.next_hlc_ts().unwrap();

    // 1. remote create at v=now
    let applied = core.apply_sync_remote_entities(vec![ent(
        "bm-1",
        now,
        7,
        bookmark_payload("Original"),
        false,
    )]);
    println!("PROBE create applied={}", applied.len());
    println!(
        "PROBE bookmark present after create: {}",
        core.get_all_bookmark_entries()
            .iter()
            .any(|b| b.id.0 == "bm-1")
    );

    // 2. remote tombstone at v=now+10 (payload "{}")
    let applied =
        core.apply_sync_remote_entities(vec![ent("bm-1", now + 10, 7, "{}".to_string(), true)]);
    println!("PROBE delete applied={}", applied.len());
    for e in &applied {
        println!(
            "PROBE delete merged payload={} deleted={} version={}",
            e.payload_json, e.deleted, e.version
        );
    }
    println!(
        "PROBE bookmark present after delete: {}",
        core.get_all_bookmark_entries()
            .iter()
            .any(|b| b.id.0 == "bm-1")
    );

    // 3. stale remote update at v=now+5 (older than the tombstone)
    let applied = core.apply_sync_remote_entities(vec![ent(
        "bm-1",
        now + 5,
        9,
        bookmark_payload("Resurrected"),
        false,
    )]);
    println!("PROBE stale-update applied={}", applied.len());
    for e in &applied {
        println!("PROBE stale merged payload={}", e.payload_json);
    }
    println!(
        "PROBE bookmark present after stale update: {}",
        core.get_all_bookmark_entries()
            .iter()
            .any(|b| b.id.0 == "bm-1")
    );
}

#[test]
fn probe_delete_then_newer_field_write_from_other_device() {
    let mut core = synced_core();
    let now = core.next_hlc_ts().unwrap();
    core.apply_sync_remote_entities(vec![ent(
        "bm-1",
        now,
        7,
        bookmark_payload("Original"),
        false,
    )]);
    // tombstone
    core.apply_sync_remote_entities(vec![ent("bm-1", now + 10, 7, "{}".to_string(), true)]);
    println!(
        "PROBE present after tombstone: {}",
        core.get_all_bookmark_entries()
            .iter()
            .any(|b| b.id.0 == "bm-1")
    );
    // a *newer* write comes in from another device (concurrent edit)
    let applied = core.apply_sync_remote_entities(vec![ent(
        "bm-1",
        now + 20,
        9,
        bookmark_payload("Edited"),
        false,
    )]);
    println!("PROBE post-tombstone newer write applied={}", applied.len());
    println!(
        "PROBE present after newer write: {}",
        core.get_all_bookmark_entries()
            .iter()
            .any(|b| b.id.0 == "bm-1")
    );
}

#[test]
fn probe_local_edit_after_remote_apply_version() {
    // create_sync_entity reads stored version instead of allocating a new HLC.
    let mut core = synced_core();
    let now = core.next_hlc_ts().unwrap();
    core.apply_sync_remote_entities(vec![ent(
        "bm-1",
        now + 1000,
        7,
        bookmark_payload("Remote"),
        false,
    )]);
    // Local edit through the normal add path
    core.add_bookmark("https://example.com/local", "Local Title", None);
    let out = core.drain_sync_outgoing();
    println!("PROBE outgoing after local add: {}", out.len());
}

#[test]
fn probe_snapshot_versions_use_stored_hlc() {
    let mut core = synced_core();
    let now = core.next_hlc_ts().unwrap();
    core.apply_sync_remote_entities(vec![ent(
        "bm-1",
        now + 5000,
        7,
        bookmark_payload("Remote"),
        false,
    )]);
    let snap = match core.build_snapshot() {
        Ok(s) => s,
        Err(e) => {
            println!("PROBE snapshot err={e}");
            return;
        }
    };
    println!("PROBE snapshot bytes={}", snap.len());
    let txt = String::from_utf8_lossy(&snap);
    println!(
        "PROBE snapshot looks like json: {}",
        txt.chars().take(40).collect::<String>()
    );
}

#[test]
fn probe_ack_clobbers_fields_hlc() {
    let mut core = synced_core();
    let now = core.next_hlc_ts().unwrap();
    core.apply_sync_remote_entities(vec![ent(
        "bm-1",
        now,
        7,
        bookmark_payload("Original"),
        false,
    )]);
    // Simulate server ack for this entity (as the shell would call it)
    core.ack_sync_entity("bookmark", "bm-1", now);
    // Now a stale remote write arrives; with fields/payload cache nulled, the
    // merge engine loses the local payload.
    let applied = core.apply_sync_remote_entities(vec![ent(
        "bm-1",
        now + 1,
        9,
        serde_json::json!({"id":"bm-1","title":"Partial"}).to_string(),
        false,
    )]);
    for e in &applied {
        println!("PROBE post-ack merged payload={}", e.payload_json);
    }
    println!(
        "PROBE bookmark title after post-ack partial write: {:?}",
        core.get_all_bookmark_entries()
            .iter()
            .find(|b| b.id.0 == "bm-1")
            .map(|b| b.title.clone())
    );
}
