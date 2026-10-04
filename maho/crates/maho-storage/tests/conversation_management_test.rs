use maho_storage::sqlite::SqliteStorage;
use maho_types::artifact::ArtifactInfo;
use maho_types::chat::{ConversationBulkOperation, ConversationListState};

fn store() -> SqliteStorage {
    SqliteStorage::open_in_memory_with_key(&uuid::Uuid::new_v4().to_string()).unwrap()
}

#[test]
fn conversation_delete_cleanup_rolls_back_as_one_transaction() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("cleanup-rollback.sqlite");
    let path_str = path.to_str().unwrap();
    let key = "conversation-cleanup-rollback-key";
    let db = SqliteStorage::open_with_key(path_str, key).unwrap();
    db.create_conversation("c", Some("Conversation"), None, None)
        .unwrap();
    db.set_setting(
        "web_ai.composer_draft.v1.conversation:c",
        r#"{"version":1,"text":"draft","updatedAt":"2026-01-01T00:00:00Z"}"#,
    )
    .unwrap();
    db.insert_artifact(&ArtifactInfo {
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

    let trigger_conn = rusqlite::Connection::open(path_str).unwrap();
    trigger_conn.pragma_update(None, "key", key).unwrap();
    trigger_conn
        .execute_batch(
            "CREATE TRIGGER fail_composer_draft_delete
             BEFORE DELETE ON settings
             WHEN OLD.key = 'web_ai.composer_draft.v1.conversation:c'
             BEGIN
               SELECT RAISE(ABORT, 'injected draft cleanup failure');
             END;",
        )
        .unwrap();
    drop(trigger_conn);

    assert!(db
        .apply_conversation_bulk_operation(
            ConversationBulkOperation::Delete,
            &["c".into()],
            "2026-01-02T00:00:00Z",
        )
        .is_err());
    assert!(db.conversation_exists("c").unwrap());
    assert_eq!(db.list_artifacts("c").unwrap().len(), 1);
    assert!(db
        .get_setting("web_ai.composer_draft.v1.conversation:c")
        .unwrap()
        .is_some());
}

#[test]
fn migration_is_idempotent_and_adds_project_set_null_integrity() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("existing-schema.sqlite");
    let path_str = path.to_str().unwrap();
    let key = "existing-schema-key";
    {
        let conn = rusqlite::Connection::open(path_str).unwrap();
        conn.pragma_update(None, "key", key).unwrap();
        conn.execute_batch(
            "CREATE TABLE conversations (
                id TEXT PRIMARY KEY,
                title TEXT,
                space_id TEXT,
                model TEXT,
                created_at TEXT NOT NULL DEFAULT (datetime('now')),
                updated_at TEXT NOT NULL DEFAULT (datetime('now'))
            );
            INSERT INTO conversations(id,title) VALUES('existing','Existing');",
        )
        .unwrap();
    }

    let db = SqliteStorage::open_with_key(path_str, key).unwrap();
    assert_eq!(
        db.list_conversations(ConversationListState::Active, 10)
            .unwrap()[0]
            .id,
        "existing"
    );
    db.create_conversation_project("p", "Project", "2026-01-01T00:00:00Z")
        .unwrap();
    db.move_conversations_to_project(&["existing".into()], Some("p"), "2026-01-02T00:00:00Z")
        .unwrap();
    drop(db);

    let conn = rusqlite::Connection::open(path_str).unwrap();
    conn.pragma_update(None, "key", key).unwrap();
    conn.execute("PRAGMA foreign_keys=ON", []).unwrap();
    let foreign_key = conn
        .query_row(
            "SELECT \"table\", \"from\", on_delete
             FROM pragma_foreign_key_list('conversations')
             WHERE \"from\"='project_id'",
            [],
            |row| {
                Ok((
                    row.get::<_, String>(0)?,
                    row.get::<_, String>(1)?,
                    row.get::<_, String>(2)?,
                ))
            },
        )
        .unwrap();
    assert_eq!(
        foreign_key,
        (
            "conversation_projects".into(),
            "project_id".into(),
            "SET NULL".into()
        )
    );
    conn.execute("DELETE FROM conversation_projects WHERE id='p'", [])
        .unwrap();
    assert_eq!(
        conn.query_row(
            "SELECT project_id FROM conversations WHERE id='existing'",
            [],
            |row| row.get::<_, Option<String>>(0),
        )
        .unwrap(),
        None
    );
    drop(conn);

    let reopened = SqliteStorage::open_with_key(path_str, key).unwrap();
    assert_eq!(
        reopened
            .list_conversations(ConversationListState::Active, 10)
            .unwrap()[0]
            .id,
        "existing"
    );
}

#[test]
fn archive_lists_bulk_and_projects_follow_the_contract() {
    let db = store();
    for id in ["a", "b", "c"] {
        db.create_conversation(id, Some(id), None, None).unwrap();
    }
    assert!(db
        .archive_conversation("a", "2026-01-02T00:00:00Z")
        .unwrap());
    assert_eq!(
        db.list_conversations(ConversationListState::Active, 10)
            .unwrap()
            .len(),
        2
    );
    assert_eq!(
        db.list_conversations(ConversationListState::Archived, 10)
            .unwrap()[0]
            .id,
        "a"
    );

    let result = db
        .apply_conversation_bulk_operation(
            ConversationBulkOperation::Archive,
            &["b".into(), "b".into(), "missing".into()],
            "2026-01-03T00:00:00Z",
        )
        .unwrap();
    assert_eq!(result.requested_count, 2);
    assert_eq!(result.affected_ids, vec!["b"]);
    assert_eq!(result.missing_ids, vec!["missing"]);
    assert_eq!(result.unchanged_ids, Vec::<String>::new());
    let unchanged = db
        .apply_conversation_bulk_operation(
            ConversationBulkOperation::Archive,
            &["b".into()],
            "2026-01-03T00:00:01Z",
        )
        .unwrap();
    assert_eq!(unchanged.unchanged_ids, vec!["b"]);

    assert!(db
        .unarchive_conversation("a", "2026-01-04T00:00:00Z")
        .unwrap());
    db.create_conversation_project("p", "Project", "2026-01-01T00:00:00Z")
        .unwrap();
    assert_eq!(
        db.move_conversations_to_project(&["a".into()], Some("p"), "2026-01-05T00:00:00Z")
            .unwrap()
            .affected_ids,
        vec!["a"]
    );
    assert!(db.delete_conversation_project("p").unwrap());
    assert_eq!(
        db.list_conversations(ConversationListState::All, 10)
            .unwrap()
            .into_iter()
            .find(|c| c.id == "a")
            .unwrap()
            .project_id,
        None
    );
}

#[test]
fn bulk_validation_auto_archive_and_message_unarchive_are_deterministic() {
    let db = store();
    assert!(db
        .apply_conversation_bulk_operation(ConversationBulkOperation::Delete, &[], "now")
        .is_err());
    let too_many: Vec<String> = (0..501).map(|i| format!("c{i}")).collect();
    assert!(db
        .apply_conversation_bulk_operation(ConversationBulkOperation::Delete, &too_many, "now")
        .is_err());
    let exactly_500: Vec<String> = (0..500).map(|i| format!("limit-{i}")).collect();
    for id in &exactly_500 {
        db.create_conversation(id, None, None, None).unwrap();
    }
    assert_eq!(
        db.apply_conversation_bulk_operation(
            ConversationBulkOperation::Archive,
            &exactly_500,
            "2026-01-01T00:00:00Z",
        )
        .unwrap()
        .affected_ids
        .len(),
        500
    );

    db.create_conversation("old", None, None, None).unwrap();
    db.upsert_conversation(&maho_types::chat::Conversation {
        id: "old".into(),
        title: None,
        space_id: None,
        model: None,
        created_at: "2026-01-01 00:00:00".into(),
        updated_at: "2026-01-01 00:00:00".into(),
        archived_at: None,
        project_id: None,
    })
    .unwrap();
    assert_eq!(
        db.auto_archive_conversations(1_768_435_200, 3).unwrap(),
        vec!["old"]
    );
    db.save_conversation_message("old", "user", "hello", None)
        .unwrap();
    assert_eq!(
        db.list_conversations(ConversationListState::Active, 10)
            .unwrap()[0]
            .id,
        "old"
    );
}
