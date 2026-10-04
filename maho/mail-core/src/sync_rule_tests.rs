use super::*;
use maho_core::services::offline_queue;
use serde_json::json;

fn database() -> rusqlite::Connection {
    let conn = rusqlite::Connection::open_in_memory().unwrap();
    maho_core::db::migrations::run_migrations(&conn).unwrap();
    conn.execute_batch(
        "INSERT INTO accounts (id,email,display_name,auth_type,imap_host,smtp_host,username)
         VALUES ('acc1','acc1@example.invalid','Account 1','password','localhost','localhost','acc1');
         INSERT INTO folders (id,account_id,name,path,folder_type,last_synced_uid,uid_validity)
         VALUES ('inbox-1','acc1','Inbox','INBOX','inbox',10,1234);",
    )
    .unwrap();
    conn
}

fn envelope_with_sender(uid: u32, from: &str, subject: &str) -> ImapEnvelope {
    ImapEnvelope {
        uid,
        message_id: format!("<{uid}@example.invalid>"),
        in_reply_to: None,
        subject: subject.to_string(),
        from_address: from.to_string(),
        from_name: None,
        to_addresses: vec![],
        cc_addresses: vec![],
        date: "2026-09-05T00:00:00Z".into(),
        flags: vec![],
        size: 100,
        has_attachments: false,
        attachments: vec![],
    }
}

fn add_rule(
    conn: &rusqlite::Connection,
    rule_id: &str,
    account_id: &str,
    name: &str,
    conditions: serde_json::Value,
    actions: serde_json::Value,
) {
    conn.execute(
        "INSERT INTO mail_rules (id, account_id, name, priority, is_enabled, conditions_json, actions_json, stop_processing)
         VALUES (?1, ?2, ?3, 0, 1, ?4, ?5, 0)",
        rusqlite::params![
            rule_id,
            account_id,
            name,
            conditions.to_string(),
            actions.to_string(),
        ],
    )
    .unwrap();
}

#[test]
fn review_overlapping_sync_pages_apply_incoming_rules_once() {
    for action in ["mark_read", "add_star", "delete", "move_to_folder"] {
        let mut conn = database();
        conn.execute("INSERT INTO folders(id,account_id,name,path,folder_type) VALUES('archive','acc1','Archive','Archive','archive')", []).unwrap();
        add_rule(&conn, "rule", "acc1", "Rule", json!([{"field":"from","operator":"equals","value":"sender@example.invalid"}]), json!([{"type":action,"value":"Archive"}]));
        let target = sync_targets(&conn, "acc1").unwrap().into_iter().find(|f| f.path == "INBOX").unwrap();
        let page = [envelope_with_sender(11, "sender@example.invalid", "new")];
        let (inserted, _) = sync_envelopes_with_reconciliation(&mut conn, "acc1", &target, &page, 1234, Some(&page)).unwrap();
        assert_eq!(inserted, 1, "overlapping page reinserted {action} message");
        let state: Option<(String, bool, bool)> = conn.query_row("SELECT folder_id,is_read,is_starred FROM emails WHERE message_id='<11@example.invalid>'", [], |r| Ok((r.get(0)?,r.get(1)?,r.get(2)?))).optional().unwrap();
        match action {
            "mark_read" => assert!(state.unwrap().1, "second pass undid mark-read rule"),
            "add_star" => assert!(state.unwrap().2, "second pass undid star rule"),
            "delete" => assert!(state.is_none(), "second pass resurrected deleted mail"),
            _ => assert_eq!(state.unwrap().0, "archive"),
        }
        assert_eq!(offline_queue::list_pending_mutations(&conn, "acc1").unwrap().len(), 1);
    }
}

#[test]
fn incoming_rule_mark_read_sets_flag_and_queues_remote_mutation() {
    let mut conn = database();
    add_rule(
        &conn,
        "rule-read",
        "acc1",
        "Mark Read Rule",
        json!([{"field": "from", "operator": "equals", "value": "read-match@example.invalid"}]),
        json!([{"type": "mark_read"}]),
    );
    let target = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();
    let env = envelope_with_sender(11, "read-match@example.invalid", "Newsletter");

    let (inserted, arrivals) =
        sync_envelopes_into_cache(&mut conn, "acc1", &target, &[env], 1234).unwrap();
    assert_eq!(inserted, 1);

    // Notification privacy: messages marked read by rule must not produce arrival notifications
    assert!(
        arrivals.is_empty(),
        "mark_read rule must suppress arrival notification for privacy"
    );

    // Email stored with is_read = true
    let is_read: bool = conn
        .query_row(
            "SELECT is_read FROM emails WHERE account_id = 'acc1' AND uid = 11",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert!(
        is_read,
        "incoming matching envelope was not marked read by rule"
    );

    // Durable remote intent queued in pending_mutations
    let mutations = offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
    assert_eq!(
        mutations.len(),
        1,
        "missing durable remote intent in pending_mutations"
    );
    assert_eq!(mutations[0].mutation_type, "mark_read");
    assert_eq!(mutations[0].email_uid, Some(11));
    assert_eq!(mutations[0].folder_path.as_deref(), Some("INBOX"));
}

#[test]
fn incoming_rule_mark_read_suppresses_arrival_notification() {
    let mut conn = database();
    add_rule(
        &conn,
        "rule-read",
        "acc1",
        "Mark Read Rule",
        json!([{"field": "from", "operator": "equals", "value": "read-me@example.invalid"}]),
        json!([{"type": "mark_read"}]),
    );
    let target = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();

    let read_env = envelope_with_sender(11, "read-me@example.invalid", "Read by rule");
    let unread_env = envelope_with_sender(12, "friend@example.invalid", "Keep unread");

    let (_inserted, arrivals) =
        sync_envelopes_into_cache(&mut conn, "acc1", &target, &[read_env, unread_env], 1234)
            .unwrap();

    // Normal unread email must produce an arrival notification
    assert!(
        arrivals.iter().any(|a| a.cursor == 12),
        "normal unread email arrival notification was missing"
    );

    // Message marked read by rule must NOT produce arrival notification
    assert!(
        arrivals.iter().all(|a| a.cursor != 11),
        "email marked read by rule produced arrival notification"
    );
}

#[test]
fn incoming_rule_star_sets_flag_and_queues_remote_mutation() {
    let mut conn = database();
    add_rule(
        &conn,
        "rule-star",
        "acc1",
        "Star Rule",
        json!([{"field": "subject", "operator": "contains", "value": "Urgent"}]),
        json!([{"type": "add_star"}]),
    );
    let target = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();
    let env = envelope_with_sender(11, "boss@example.invalid", "Urgent request");

    let (inserted, arrivals) =
        sync_envelopes_into_cache(&mut conn, "acc1", &target, &[env], 1234).unwrap();
    assert_eq!(inserted, 1);
    // Unread starred mail generates arrival notification
    assert_eq!(arrivals.len(), 1);

    // Email stored with is_starred = true
    let is_starred: bool = conn
        .query_row(
            "SELECT is_starred FROM emails WHERE account_id = 'acc1' AND uid = 11",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert!(
        is_starred,
        "incoming matching envelope was not starred by rule"
    );

    // Durable remote intent queued in pending_mutations
    let mutations = offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
    assert_eq!(
        mutations.len(),
        1,
        "missing durable remote intent in pending_mutations"
    );
    assert_eq!(mutations[0].mutation_type, "star");
    assert_eq!(mutations[0].email_uid, Some(11));
    assert_eq!(mutations[0].folder_path.as_deref(), Some("INBOX"));
}

#[test]
fn incoming_rule_delete_removes_email_and_queues_remote_mutation() {
    let mut conn = database();
    add_rule(
        &conn,
        "rule-delete",
        "acc1",
        "Delete Rule",
        json!([{"field": "from", "operator": "equals", "value": "spammer@example.invalid"}]),
        json!([{"type": "delete"}]),
    );
    let target = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();
    let env = envelope_with_sender(11, "spammer@example.invalid", "Spam offer");

    let (_inserted, arrivals) =
        sync_envelopes_into_cache(&mut conn, "acc1", &target, &[env], 1234).unwrap();

    // Email row must not exist in emails table
    let count: i64 = conn
        .query_row(
            "SELECT COUNT(*) FROM emails WHERE account_id = 'acc1' AND uid = 11",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert_eq!(count, 0, "deleted email row still present in database");

    // Durable remote intent queued in pending_mutations
    let mutations = offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
    assert_eq!(
        mutations.len(),
        1,
        "rule deletion discarded remote intent"
    );
    assert_eq!(mutations[0].mutation_type, "delete");
    assert_eq!(mutations[0].email_uid, Some(11));
    assert_eq!(mutations[0].folder_path.as_deref(), Some("INBOX"));

    // Deleted email must not appear in arrivals notification
    assert!(
        arrivals.iter().all(|a| a.cursor != 11),
        "deleted email was published as arrival notification"
    );
}

#[test]
fn incoming_rule_delete_suppresses_arrival_notification() {
    let mut conn = database();
    add_rule(
        &conn,
        "rule-block",
        "acc1",
        "Block Sender Rule",
        json!([{"field": "from", "operator": "equals", "value": "blocked@example.invalid"}]),
        json!([{"type": "delete"}]),
    );
    let target = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();

    // Sync two envelopes: one blocked, one normal
    let blocked_env = envelope_with_sender(11, "blocked@example.invalid", "Blocked message");
    let normal_env = envelope_with_sender(12, "friend@example.invalid", "Hello friend");

    let (_inserted, arrivals) =
        sync_envelopes_into_cache(&mut conn, "acc1", &target, &[blocked_env, normal_env], 1234)
            .unwrap();

    // Normal email arrives and produces arrival notification
    assert!(
        arrivals.iter().any(|a| a.cursor == 12),
        "normal email arrival notification was missing"
    );

    // Blocked/deleted email must NOT produce arrival notification
    assert!(
        arrivals.iter().all(|a| a.cursor != 11),
        "deleted email produced an arrival notification"
    );
}

#[test]
fn incoming_rule_move_updates_folder_and_queues_remote_mutation() {
    let mut conn = database();
    conn.execute(
        "INSERT INTO folders (id, account_id, name, path, folder_type, last_synced_uid, uid_validity)
         VALUES ('archive-1', 'acc1', 'Archive', 'Archive', 'archive', 0, 1234)",
        [],
    )
    .unwrap();
    add_rule(
        &conn,
        "rule-move",
        "acc1",
        "Move to Archive Rule",
        json!([{"field": "from", "operator": "equals", "value": "archive-me@example.invalid"}]),
        json!([{"type": "move_to_folder", "value": "Archive"}]),
    );
    let target = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();
    let env = envelope_with_sender(11, "archive-me@example.invalid", "Old reports");

    let (inserted, arrivals) =
        sync_envelopes_into_cache(&mut conn, "acc1", &target, &[env], 1234).unwrap();
    assert_eq!(inserted, 1);
    // Message moved out of folder must not generate arrival notification in source inbox
    assert!(
        arrivals.is_empty(),
        "message moved out of inbox produced arrival notification"
    );

    // Email row exists in archive folder, not inbox
    let folder_id: String = conn
        .query_row(
            "SELECT folder_id FROM emails WHERE account_id = 'acc1' AND message_id = '<11@example.invalid>'",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert_eq!(
        folder_id, "archive-1",
        "email was not moved to destination folder"
    );

    // Durable remote intent queued in pending_mutations with canonical path
    let mutations = offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
    assert_eq!(
        mutations.len(),
        1,
        "missing durable remote intent for move"
    );
    assert_eq!(mutations[0].mutation_type, "move");
    assert_eq!(mutations[0].email_uid, Some(11));
    assert_eq!(mutations[0].folder_path.as_deref(), Some("INBOX"));
    assert_eq!(mutations[0].target_folder.as_deref(), Some("Archive"));
}

#[test]
fn incoming_rule_move_queues_canonical_path_on_display_name_mismatch() {
    let mut conn = database();
    // Folder has display name "My Archive" but canonical path "INBOX.Archive"
    conn.execute(
        "INSERT INTO folders (id, account_id, name, path, folder_type, last_synced_uid, uid_validity)
         VALUES ('arch-custom', 'acc1', 'My Archive', 'INBOX.Archive', 'archive', 0, 1234)",
        [],
    )
    .unwrap();
    add_rule(
        &conn,
        "rule-move-mismatch",
        "acc1",
        "Move Rule",
        json!([{"field": "from", "operator": "equals", "value": "divert@example.invalid"}]),
        json!([{"type": "move_to_folder", "value": "My Archive"}]),
    );
    let target = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();
    let env = envelope_with_sender(11, "divert@example.invalid", "Divert mail");

    let (_inserted, _arrivals) =
        sync_envelopes_into_cache(&mut conn, "acc1", &target, &[env], 1234).unwrap();

    let mutations = offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
    assert_eq!(mutations.len(), 1);
    assert_eq!(mutations[0].mutation_type, "move");
    // MUST queue canonical target path "INBOX.Archive", NOT the display name "My Archive"
    assert_eq!(
        mutations[0].target_folder.as_deref(),
        Some("INBOX.Archive"),
        "move mutation queued display name instead of canonical target path"
    );
}

#[test]
fn incoming_rule_move_to_missing_folder_rolls_back_transaction() {
    let mut conn = database();
    add_rule(
        &conn,
        "rule-move-missing",
        "acc1",
        "Move to Missing Folder",
        json!([{"field": "from", "operator": "equals", "value": "nowhere@example.invalid"}]),
        json!([{"type": "move_to_folder", "value": "NonExistentFolder"}]),
    );
    let target = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();
    let env = envelope_with_sender(11, "nowhere@example.invalid", "Lost message");

    let result = sync_envelopes_into_cache(&mut conn, "acc1", &target, &[env], 1234);
    assert!(
        result.is_err(),
        "missing move target folder did not abort sync transaction"
    );

    // Transaction rolled back: envelope not committed
    let count: i64 = conn
        .query_row(
            "SELECT COUNT(*) FROM emails WHERE account_id = 'acc1' AND uid = 11",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert_eq!(count, 0, "uncommitted envelope persisted after missing target");

    // Cursor must NOT advance
    let cursor: i64 = conn
        .query_row(
            "SELECT last_synced_uid FROM folders WHERE id = 'inbox-1'",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert_eq!(cursor, 10, "folder cursor advanced despite failed target");
}

#[test]
fn incoming_rules_isolate_by_account() {
    let mut conn = database();
    conn.execute_batch(
        "INSERT INTO accounts (id,email,display_name,auth_type,imap_host,smtp_host,username)
         VALUES ('acc2','acc2@example.invalid','Account 2','password','localhost','localhost','acc2');
         INSERT INTO folders (id,account_id,name,path,folder_type,last_synced_uid,uid_validity)
         VALUES ('inbox-2','acc2','Inbox','INBOX','inbox',10,1234);",
    )
    .unwrap();

    // acc1 has a rule to mark read
    add_rule(
        &conn,
        "rule-acc1-read",
        "acc1",
        "Acc1 Mark Read Rule",
        json!([{"field": "from", "operator": "equals", "value": "shared@example.invalid"}]),
        json!([{"type": "mark_read"}]),
    );

    // acc2 has a rule to delete messages from the same sender
    add_rule(
        &conn,
        "rule-acc2-delete",
        "acc2",
        "Acc2 Delete Rule",
        json!([{"field": "from", "operator": "equals", "value": "shared@example.invalid"}]),
        json!([{"type": "delete"}]),
    );

    // Envelope arrives for acc1 from the shared sender
    let target1 = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();
    let env = envelope_with_sender(11, "shared@example.invalid", "Hello Acc1");

    let (inserted, arrivals) =
        sync_envelopes_into_cache(&mut conn, "acc1", &target1, &[env], 1234).unwrap();
    assert_eq!(inserted, 1);
    // acc1 rule marked it read -> arrival suppressed for privacy
    assert!(arrivals.is_empty());

    // acc1 mail must NOT be deleted by acc2 rule
    let count: i64 = conn
        .query_row(
            "SELECT COUNT(*) FROM emails WHERE account_id = 'acc1' AND uid = 11",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert_eq!(count, 1, "acc2 rule improperly deleted acc1 email");

    // acc1 rule must apply to acc1 mail (mark_read)
    let is_read: bool = conn
        .query_row(
            "SELECT is_read FROM emails WHERE account_id = 'acc1' AND uid = 11",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert!(is_read, "acc1 rule was not applied to acc1 mail");

    // Zero pending mutations queued for acc2
    let acc2_mutations = offline_queue::list_pending_mutations(&conn, "acc2").unwrap();
    assert!(
        acc2_mutations.is_empty(),
        "acc2 received pending mutations from acc1 sync"
    );
}

#[test]
fn injected_pending_mutation_failure_rolls_back_envelope_and_cursor() {
    let mut conn = database();
    add_rule(
        &conn,
        "rule-delete-fail",
        "acc1",
        "Delete Rule",
        json!([{"field": "from", "operator": "equals", "value": "fail@example.invalid"}]),
        json!([{"type": "delete"}]),
    );

    // Inject trigger to abort pending_mutations insertion
    conn.execute_batch(
        "CREATE TRIGGER reject_mutation_queue
         BEFORE INSERT ON pending_mutations
         BEGIN
             SELECT RAISE(ABORT, 'injected queue failure');
         END;",
    )
    .unwrap();

    let target = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();
    let env = envelope_with_sender(11, "fail@example.invalid", "Must fail");

    let result = sync_envelopes_into_cache(&mut conn, "acc1", &target, &[env], 1234);
    assert!(
        result.is_err(),
        "queue failure did not abort sync transaction"
    );

    // Envelope 11 must NOT exist in emails table
    let count: i64 = conn
        .query_row(
            "SELECT COUNT(*) FROM emails WHERE account_id = 'acc1' AND uid = 11",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert_eq!(count, 0, "failed transaction left uncommitted envelope");

    // Cursor must NOT have advanced past 10
    let cursor: i64 = conn
        .query_row(
            "SELECT last_synced_uid FROM folders WHERE id = 'inbox-1'",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert_eq!(cursor, 10, "failed transaction advanced folder cursor");
}

#[test]
fn existing_uid_refresh_does_not_reapply_rules() {
    let mut conn = database();
    add_rule(
        &conn,
        "rule-mark-read",
        "acc1",
        "Mark Read Rule",
        json!([{"field": "from", "operator": "equals", "value": "repeat@example.invalid"}]),
        json!([{"type": "mark_read"}]),
    );

    let target = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();

    // Initial ingestion of UID 11: rule applies, marks read
    let env_initial = envelope_with_sender(11, "repeat@example.invalid", "First arrival");
    let (inserted, arrivals) =
        sync_envelopes_into_cache(&mut conn, "acc1", &target, &[env_initial], 1234).unwrap();
    assert_eq!(inserted, 1);
    // Mark read suppresses arrivals
    assert!(arrivals.is_empty());

    let is_read: bool = conn
        .query_row(
            "SELECT is_read FROM emails WHERE account_id = 'acc1' AND uid = 11",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert!(is_read, "new arrival was not marked read by rule");

    // Clear pending mutations to establish a clean state for refresh test
    conn.execute("DELETE FROM pending_mutations WHERE account_id = 'acc1'", [])
        .unwrap();

    // User explicitly marks the email as unread locally
    conn.execute(
        "UPDATE emails SET is_read = 0 WHERE account_id = 'acc1' AND uid = 11",
        [],
    )
    .unwrap();

    // Now a flag refresh occurs for existing UID 11 from server without \Seen flag
    let target_refreshed = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();
    assert_eq!(target_refreshed.last_synced_uid, 11);

    let env_refresh = envelope_with_sender(11, "repeat@example.invalid", "First arrival");
    let (inserted_refresh, arrivals_refresh) =
        sync_envelopes_into_cache(&mut conn, "acc1", &target_refreshed, &[env_refresh], 1234)
            .unwrap();

    assert_eq!(inserted_refresh, 0);
    assert!(arrivals_refresh.is_empty(), "existing UID produced arrivals");

    // The rule must NOT re-run to flip is_read back to 1 on flag refresh
    let is_read_after_refresh: bool = conn
        .query_row(
            "SELECT is_read FROM emails WHERE account_id = 'acc1' AND uid = 11",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert!(
        !is_read_after_refresh,
        "existing UID refresh improperly reapplied incoming rule"
    );

    // No new pending mutations queued during refresh
    let mutations = offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
    assert!(
        mutations.is_empty(),
        "existing UID refresh queued spurious mutation"
    );
}

#[test]
fn historical_envelope_below_highwater_does_not_apply_rules() {
    let mut conn = database();
    // Advance cursor to highwater mark of 100
    conn.execute("UPDATE folders SET last_synced_uid = 100 WHERE id = 'inbox-1'", [])
        .unwrap();

    // Rule configured to mark read messages from historical sender
    add_rule(
        &conn,
        "rule-read-hist",
        "acc1",
        "Mark Read Rule",
        json!([{"field": "from", "operator": "equals", "value": "history@example.invalid"}]),
        json!([{"type": "mark_read"}]),
    );

    let target = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();
    assert_eq!(target.last_synced_uid, 100);

    // Historical sync ingests envelope with UID 50 (below highwater mark 100)
    let hist_env = envelope_with_sender(50, "history@example.invalid", "Historical message");
    let (hist_inserted, hist_arrivals) =
        sync_envelopes_into_cache(&mut conn, "acc1", &target, &[hist_env], 1234).unwrap();
    assert_eq!(hist_inserted, 1);
    assert!(
        hist_arrivals.is_empty(),
        "historical mail produced arrivals notification"
    );

    // Historical email must NOT have incoming rules applied (is_read remains false)
    let is_read: bool = conn
        .query_row(
            "SELECT is_read FROM emails WHERE account_id = 'acc1' AND uid = 50",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert!(
        !is_read,
        "historical envelope below highwater had incoming rule applied"
    );

    // No mutation queued in pending_mutations for UID 50
    let mutations = offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
    assert!(
        mutations.is_empty(),
        "historical envelope below highwater queued mutation"
    );

    // Cursor must remain at highwater mark 100
    let cursor: i64 = conn
        .query_row(
            "SELECT last_synced_uid FROM folders WHERE id = 'inbox-1'",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert_eq!(cursor, 100, "historical envelope regressed folder cursor");

    // In contrast, a genuine new arrival with UID 101 (> highwater 100) MUST apply rules
    let new_env = envelope_with_sender(101, "history@example.invalid", "New message");
    let target_now = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();
    let (new_inserted, new_arrivals) =
        sync_envelopes_into_cache(&mut conn, "acc1", &target_now, &[new_env], 1234).unwrap();
    assert_eq!(new_inserted, 1);
    // Mark read suppresses arrivals
    assert!(new_arrivals.is_empty());

    let new_is_read: bool = conn
        .query_row(
            "SELECT is_read FROM emails WHERE account_id = 'acc1' AND uid = 101",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert!(new_is_read, "new arrival above highwater did not apply rule");

    let new_mutations = offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
    assert_eq!(
        new_mutations.len(),
        1,
        "new arrival above highwater failed to queue mutation"
    );
    assert_eq!(new_mutations[0].email_uid, Some(101));
}

#[test]
fn historical_sync_below_highwater_never_pushes_arrivals() {
    let mut conn = database();
    conn.execute("UPDATE folders SET last_synced_uid = 200 WHERE id = 'inbox-1'", [])
        .unwrap();

    let target = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();

    // Ingest three historical envelopes below highwater mark 200 without any rules
    let env1 = envelope_with_sender(10, "h1@example.invalid", "Old 1");
    let env2 = envelope_with_sender(50, "h2@example.invalid", "Old 2");
    let env3 = envelope_with_sender(150, "h3@example.invalid", "Old 3");

    let (inserted, arrivals) =
        sync_envelopes_into_cache(&mut conn, "acc1", &target, &[env1, env2, env3], 1234).unwrap();
    assert_eq!(inserted, 3);
    assert!(
        arrivals.is_empty(),
        "historical envelopes below highwater must never produce arrivals"
    );
}

#[test]
fn invalid_rule_row_fails_sync_truthfully_and_rolls_back() {
    let mut conn = database();
    // Insert corrupted JSON in conditions_json
    conn.execute(
        "INSERT INTO mail_rules (id, account_id, name, priority, is_enabled, conditions_json, actions_json, stop_processing)
         VALUES ('corrupt-rule', 'acc1', 'Corrupt Rule', 0, 1, '{not valid json', '[]', 0)",
        [],
    )
    .unwrap();

    let target = sync_targets(&conn, "acc1")
        .unwrap()
        .into_iter()
        .find(|f| f.path == "INBOX")
        .unwrap();
    let env = envelope_with_sender(11, "sender@example.invalid", "Hello");

    let result = sync_envelopes_into_cache(&mut conn, "acc1", &target, &[env], 1234);
    assert!(
        result.is_err(),
        "invalid rule row was silently swallowed instead of truthful failure"
    );

    // Rollback guarantees envelope not committed
    let count: i64 = conn
        .query_row(
            "SELECT COUNT(*) FROM emails WHERE account_id = 'acc1' AND uid = 11",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert_eq!(count, 0, "envelope persisted after invalid rule failure");
}
