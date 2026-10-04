use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{SyncEntity, SyncEntityType, SyncStatus};
use maho_types::artifact::ArtifactInfo;
use maho_types::chat::{ConversationBulkOperation, ConversationListState};

fn core() -> MahoCore {
    maho_storage::sqlite::set_sqlcipher_key("conversation-management-core-key").unwrap();
    MahoCore::new().with_storage(":memory:")
}

fn sync_entity(
    core: &mut MahoCore,
    entity_type: SyncEntityType,
    entity_id: &str,
    payload_json: String,
) -> SyncEntity {
    SyncEntity {
        entity_type,
        entity_id: entity_id.into(),
        version: core.next_hlc_ts().unwrap(),
        device_id: 9,
        schema_version: 1,
        modified_at: 1,
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: None,
        payload_json,
    }
}

#[test]
fn fresh_sync_orders_project_before_conversation_with_non_null_project_id() {
    let mut core = core();
    core.start_sync("wss://localhost", "room");
    core.set_sync_status(SyncStatus::Synced);
    let conversation = sync_entity(
        &mut core,
        SyncEntityType::Conversation,
        "c",
        serde_json::json!({
            "id":"c",
            "title":"project conversation",
            "spaceId":null,
            "model":null,
            "createdAt":"2026-01-01T00:00:00Z",
            "updatedAt":"2026-01-02T00:00:00Z",
            "archivedAt":null,
            "projectId":"p"
        })
        .to_string(),
    );
    let project = sync_entity(
        &mut core,
        SyncEntityType::ConversationProject,
        "p",
        serde_json::json!({
            "id":"p",
            "name":"Project",
            "createdAt":"2026-01-01T00:00:00Z",
            "updatedAt":"2026-01-01T00:00:00Z"
        })
        .to_string(),
    );

    let applied = core.apply_sync_remote_entities(vec![conversation, project]);
    assert_eq!(applied.len(), 2);
    assert_eq!(core.list_conversation_projects().len(), 1);
    let row = core
        .list_conversations(ConversationListState::All, 10)
        .unwrap()
        .pop()
        .unwrap();
    assert_eq!(row.project_id.as_deref(), Some("p"));
}

#[test]
fn sync_upserts_existing_metadata_and_projects() {
    let mut core = core();
    core.start_sync("wss://localhost", "room");
    core.set_sync_status(SyncStatus::Synced);
    assert!(core.create_conversation_persisted("c", Some("old"), None, None));
    let entity = SyncEntity {
        entity_type: SyncEntityType::Conversation,
        entity_id: "c".into(),
        version: core.next_hlc_ts().unwrap(), device_id: 9, schema_version: 1,
        modified_at: 1, deleted: false, queue_row_id: None, fields_hlc_json: None,
        profile_id: None,
        payload_json: serde_json::json!({"id":"c","title":"new","spaceId":null,"model":"m","createdAt":"2026-01-01T00:00:00Z","updatedAt":"2026-01-02T00:00:00Z","archivedAt":"2026-01-03T00:00:00Z","projectId":null}).to_string(),
    };
    assert_eq!(core.apply_sync_remote_entities(vec![entity]).len(), 1);
    let row = core
        .list_conversations(ConversationListState::All, 10)
        .unwrap()
        .pop()
        .unwrap();
    assert_eq!(row.title.as_deref(), Some("new"));
    assert_eq!(row.archived_at.as_deref(), Some("2026-01-03T00:00:00Z"));
}

#[test]
fn direct_delete_rejects_active_sessions() {
    let mut core = core();
    assert!(core.create_conversation_persisted("c", None, None, None));
    core.mark_conversation_session_active("c");
    assert!(!core.delete_conversation_persisted("c"));
    assert!(core
        .list_conversations(ConversationListState::All, 10)
        .unwrap()
        .iter()
        .any(|conversation| conversation.id == "c"));
    core.mark_conversation_session_inactive("c");
    assert!(core.delete_conversation_persisted("c"));
}

#[test]
fn direct_delete_failure_preserves_turns_and_emits_no_tombstones() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("direct-delete-rollback.sqlite");
    let path_str = path.to_str().unwrap();
    let key = "conversation-direct-delete-rollback-key";
    maho_storage::sqlite::set_sqlcipher_key(key).unwrap();
    let mut core = MahoCore::new().with_storage(path_str);
    assert!(core.create_conversation_persisted("c", None, None, None));
    assert!(core.save_conversation_message_persisted("c", "user", "hello", None));
    core.storage_ref()
        .unwrap()
        .insert_artifact(&ArtifactInfo {
            artifact_id: "artifact".into(),
            session_id: "c".into(),
            display_name: "artifact.txt".into(),
            mime_type: "text/plain".into(),
            size_bytes: 5,
            storage_rel_path: "c/artifact.txt".into(),
            created_at_ms: 1,
            kind: None,
        })
        .unwrap();
    core.storage_ref()
        .unwrap()
        .set_setting(
            "web_ai.composer_draft.v1.conversation:c",
            r#"{"version":1,"text":"draft","updatedAt":"2026-01-01T00:00:00Z"}"#,
        )
        .unwrap();
    let baseline_queue_len = core
        .storage_ref()
        .unwrap()
        .load_pending_sync_entities()
        .unwrap()
        .len();

    let trigger_conn = rusqlite::Connection::open(path_str).unwrap();
    trigger_conn.pragma_update(None, "key", key).unwrap();
    trigger_conn
        .execute_batch(
            "CREATE TRIGGER fail_direct_composer_draft_delete
             BEFORE DELETE ON settings
             WHEN OLD.key = 'web_ai.composer_draft.v1.conversation:c'
             BEGIN
               SELECT RAISE(ABORT, 'injected draft cleanup failure');
             END;",
        )
        .unwrap();
    drop(trigger_conn);

    assert!(!core.delete_conversation_persisted("c"));
    assert_eq!(
        core.storage_ref()
            .unwrap()
            .get_conversation_messages("c")
            .unwrap()
            .len(),
        1
    );
    assert!(core
        .storage_ref()
        .unwrap()
        .conversation_exists("c")
        .unwrap());
    assert_eq!(
        core.storage_ref()
            .unwrap()
            .load_pending_sync_entities()
            .unwrap()
            .len(),
        baseline_queue_len
    );
}

#[test]
fn deletion_purges_draft_and_artifacts_and_active_bulk_delete_rejects_exactly() {
    let mut core = core();
    assert!(core.create_conversation_persisted("c", None, None, None));
    assert!(core.set_composer_draft(r#"{"kind":"conversation","conversationId":"c"}"#, "draft"));
    core.mark_conversation_session_active("c");
    let err = core
        .apply_conversation_bulk_operation(
            ConversationBulkOperation::Delete,
            vec!["c".into()],
            "2026-01-01T00:00:00Z",
        )
        .unwrap_err();
    assert_eq!(
        err,
        serde_json::json!({"error":"active_sessions","ids":["c"]})
    );
    core.mark_conversation_session_inactive("c");
    assert_eq!(
        core.apply_conversation_bulk_operation(
            ConversationBulkOperation::Delete,
            vec!["c".into()],
            "2026-01-01T00:00:00Z"
        )
        .unwrap()
        .affected_ids,
        vec!["c"]
    );
    assert!(core
        .get_composer_draft(r#"{"kind":"conversation","conversationId":"c"}"#)
        .is_none());
}
