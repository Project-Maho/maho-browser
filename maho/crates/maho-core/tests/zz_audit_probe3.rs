//! TEMPORARY AUDIT PROBE 3 — delete after run.
use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{build_fields_hlc, SyncEntity, SyncEntityType, SyncStatus};

fn core_with(db: &str) -> MahoCore {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let mut core = MahoCore::new().with_storage(db);
    core.start_sync("wss://localhost:8080", "room");
    core.set_sync_status(SyncStatus::Synced);
    core
}

fn e(
    t: SyncEntityType,
    id: &str,
    version: u64,
    dev: u32,
    payload: &str,
    deleted: bool,
) -> SyncEntity {
    SyncEntity {
        entity_type: t,
        entity_id: id.to_string(),
        version,
        device_id: dev,
        schema_version: 1,
        modified_at: 1_700_000_000,
        payload_json: payload.to_string(),
        deleted,
        queue_row_id: None,
        fields_hlc_json: if deleted {
            None
        } else {
            build_fields_hlc(payload, version, dev)
        },
        profile_id: None,
    }
}

/// Orphan conversation turn: arrives before its conversation (separate batch).
#[test]
fn probe_orphan_conversation_turn() {
    let dir = tempfile::tempdir().unwrap();
    let db = dir.path().join("c.db").to_str().unwrap().to_string();
    let mut core = core_with(&db);
    let now = core.next_hlc_ts().unwrap();
    let turn = serde_json::json!({
        "id":"turn-1","sessionId":"conv-1","role":"user","content":"hi","urlContext":null,"createdAt":"2026-07-09T00:00:00Z"
    })
    .to_string();
    let applied = core.apply_sync_remote_entities(vec![e(
        SyncEntityType::ConversationTurn,
        "turn-1",
        now,
        7,
        &turn,
        false,
    )]);
    println!("PROBE orphan turn applied(returned)={}", applied.len());
    let s = maho_storage::sqlite::SqliteStorage::open(&db).unwrap();
    println!(
        "PROBE entity_versions for turn = {:?}",
        s.get_entity_version("conversationturn", "turn-1")
            .unwrap()
            .map(|r| r.0)
    );
    println!(
        "PROBE last_pulled(conversationturn) = {:?}",
        s.get_sync_last_pulled_version("conversationturn").unwrap()
    );
    // Now the conversation shows up, then the turn is retried with the SAME version.
    let conv = serde_json::json!({
        "id":"conv-1","title":"T","spaceId":null,"model":null,
        "createdAt":"2026-07-09T00:00:00Z","updatedAt":"2026-07-09T00:00:00Z",
        "archivedAt":null,"projectId":null
    })
    .to_string();
    core.apply_sync_remote_entities(vec![e(
        SyncEntityType::Conversation,
        "conv-1",
        now + 1,
        7,
        &conv,
        false,
    )]);
    let retry = core.apply_sync_remote_entities(vec![e(
        SyncEntityType::ConversationTurn,
        "turn-1",
        now,
        7,
        &turn,
        false,
    )]);
    println!("PROBE turn retry applied={}", retry.len());
}

/// schema_version > 1 is rejected — does last_pulled still advance elsewhere?
#[test]
fn probe_future_schema_version() {
    let dir = tempfile::tempdir().unwrap();
    let db = dir.path().join("s.db").to_str().unwrap().to_string();
    let mut core = core_with(&db);
    let now = core.next_hlc_ts().unwrap();
    let mut ent = e(
        SyncEntityType::Bookmark,
        "bm-9",
        now,
        7,
        r#"{"id":"bm-9","url":"https://e.com","title":"T","folder_id":null,"favicon":null,"created_at":"2026-07-09T00:00:00Z"}"#,
        false,
    );
    ent.schema_version = 2;
    let applied = core.apply_sync_remote_entities(vec![ent]);
    println!("PROBE future-schema applied={}", applied.len());
    println!(
        "PROBE transport error = {:?}",
        core.sync_last_transport_error().map(|e| e.code())
    );
}

/// Deleted flag does not participate in whole-entity comparison when payload is
/// non-object: check the `{}` tombstone against an entity that has never been seen.
#[test]
fn probe_unseen_tombstone_then_older_create() {
    let dir = tempfile::tempdir().unwrap();
    let db = dir.path().join("t.db").to_str().unwrap().to_string();
    let mut core = core_with(&db);
    let now = core.next_hlc_ts().unwrap();
    // Tombstone for an entity this device has never seen.
    let applied = core.apply_sync_remote_entities(vec![e(
        SyncEntityType::Bookmark,
        "bm-ghost",
        now + 100,
        7,
        "{}",
        true,
    )]);
    println!("PROBE unseen tombstone applied={}", applied.len());
    // Then the (older) create for the same entity arrives out of order.
    let applied = core.apply_sync_remote_entities(vec![e(
        SyncEntityType::Bookmark,
        "bm-ghost",
        now + 1,
        7,
        r#"{"id":"bm-ghost","url":"https://e.com","title":"Ghost","folder_id":null,"favicon":null,"created_at":"2026-07-09T00:00:00Z"}"#,
        false,
    )]);
    println!("PROBE out-of-order create applied={}", applied.len());
    println!(
        "PROBE ghost bookmark present = {}",
        core.get_all_bookmark_entries()
            .iter()
            .any(|b| b.id.0 == "bm-ghost")
    );
}

/// Does a remote apply of a *disabled* entity type get filtered on inbound?
#[test]
fn probe_disabled_type_inbound() {
    let dir = tempfile::tempdir().unwrap();
    let db = dir.path().join("d.db").to_str().unwrap().to_string();
    let mut core = core_with(&db);
    let now = core.next_hlc_ts().unwrap();
    let payload = serde_json::json!({
        "id":"addr-1","name":"N","street":"S","city":"C","state":"ST","zip":"1","country":"US",
        "phone":null,"email":null,"address_line2":null
    })
    .to_string();
    let applied = core.apply_sync_remote_entities(vec![e(
        SyncEntityType::AutofillAddress,
        "addr-1",
        now,
        7,
        &payload,
        false,
    )]);
    println!("PROBE disabled-type inbound applied={}", applied.len());
    let s = maho_storage::sqlite::SqliteStorage::open(&db).unwrap();
    println!(
        "PROBE autofill rows = {}",
        s.load_autofill_addresses().map(|v| v.len()).unwrap_or(0)
    );
}

/// HLC receive-side clamp: what happens to an entity with a huge future version?
#[test]
fn probe_future_hlc_rejected() {
    let dir = tempfile::tempdir().unwrap();
    let db = dir.path().join("h.db").to_str().unwrap().to_string();
    let mut core = core_with(&db);
    let applied = core.apply_sync_remote_entities(vec![e(
        SyncEntityType::Bookmark,
        "bm-future",
        u64::MAX / 2,
        7,
        r#"{"id":"bm-future","url":"https://e.com","title":"F","folder_id":null,"favicon":null,"created_at":"2026-07-09T00:00:00Z"}"#,
        false,
    )]);
    println!("PROBE future-hlc applied={}", applied.len());
}
