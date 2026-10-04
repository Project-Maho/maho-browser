use super::*;

fn database() -> rusqlite::Connection {
    let conn = rusqlite::Connection::open_in_memory().unwrap();
    maho_core::db::migrations::run_migrations(&conn).unwrap();
    conn.execute_batch(
        "INSERT INTO accounts (id,email,display_name,auth_type,imap_host,smtp_host,username)
         VALUES ('review','review@example.invalid','Review','password','localhost','localhost','review');
         INSERT INTO folders (id,account_id,name,path,folder_type,last_synced_uid,uid_validity)
         VALUES ('review-inbox','review','Inbox','INBOX','inbox',10,1234);",
    ).unwrap();
    conn
}

fn envelope(uid: u32) -> ImapEnvelope {
    ImapEnvelope {
        uid, message_id: format!("<{uid}@example.invalid>"), in_reply_to: None,
        subject: format!("Message {uid}"), from_address: "sender@example.invalid".into(),
        from_name: None, to_addresses: vec![], cc_addresses: vec![],
        date: "Fri, 04 Sep 2026 23:30:00 -0700".into(), flags: vec![],
        size: 100, has_attachments: false, attachments: vec![],
    }
}

#[test]
fn spam_and_trash_are_sync_targets() {
    let conn = database();
    conn.execute_batch(
        "INSERT INTO folders (id,account_id,name,path,folder_type)
         VALUES ('spam','review','Spam','Spam','spam'),('trash','review','Trash','Trash','trash');",
    ).unwrap();
    let mut paths: Vec<_> = sync_targets(&conn, "review").unwrap().into_iter().map(|f| f.path).collect();
    paths.sort();
    assert_eq!(paths, ["INBOX", "Spam", "Trash"]);
}

#[test]
fn ingestion_normalizes_rfc2822_date_to_utc_sortable_storage() {
    let mut conn = database();
    let target = sync_targets(&conn, "review").unwrap().remove(0);
    sync_envelopes_into_cache(&mut conn, "review", &target, &[envelope(11)], 1234).unwrap();
    let date: String = conn.query_row("SELECT date FROM emails", [], |r| r.get(0)).unwrap();
    assert_eq!(date, "2026-09-05T06:30:00Z");
}

#[test]
fn late_old_epoch_batch_cannot_replace_new_epoch_cache() {
    let mut conn = database();
    let old = sync_targets(&conn, "review").unwrap().remove(0);
    sync_envelopes_into_cache(&mut conn, "review", &old, &[envelope(1)], 5678).unwrap();
    // A second fetch started from the same old snapshot but completes after reset.
    let _ = sync_envelopes_into_cache(&mut conn, "review", &old, &[envelope(11)], 1234);
    let state: (i64, i64) = conn.query_row(
        "SELECT uid_validity,last_synced_uid FROM folders WHERE id='review-inbox'", [],
        |r| Ok((r.get(0)?,r.get(1)?)),
    ).unwrap();
    let uids: Vec<i64> = conn.prepare("SELECT uid FROM emails ORDER BY uid").unwrap()
        .query_map([], |r| r.get(0)).unwrap().collect::<std::result::Result<_,_>>().unwrap();
    assert_eq!(state, (5678,1));
    assert_eq!(uids, [1]);
}

#[test]
fn same_epoch_late_batch_cannot_regress_forward_cursor() {
    let mut conn = database();
    let old = sync_targets(&conn, "review").unwrap().remove(0);
    sync_envelopes_into_cache(&mut conn, "review", &old, &[envelope(20)], 1234).unwrap();
    let _ = sync_envelopes_into_cache(&mut conn, "review", &old, &[envelope(11)], 1234);
    let cursor: i64 = conn.query_row("SELECT last_synced_uid FROM folders", [], |r| r.get(0)).unwrap();
    assert_eq!(cursor, 20);
}

#[test]
fn changed_uidvalidity_preserves_local_only_draft() {
    let mut conn = database();
    conn.execute_batch(
        "INSERT INTO emails (id,account_id,folder_id,uid,subject,from_address,date,is_draft,body_text)
         VALUES ('local','review','review-inbox',0,'Local draft','review@example.invalid','2026-09-05',1,'unsent body');",
    ).unwrap();
    let target = sync_targets(&conn, "review").unwrap().remove(0);
    sync_envelopes_with_reconciliation(
        &mut conn, "review", &target, &[envelope(1)], 5678, Some(&[envelope(1)]),
    ).unwrap();
    let draft: (u32, bool, String) = conn.query_row(
        "SELECT uid,is_draft,body_text FROM emails WHERE id='local'", [],
        |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
    ).unwrap();
    assert_eq!(draft, (0, true, "unsent body".into()));
}

#[test]
fn failed_reconciliation_rolls_back_deletion_and_checkpoint() {
    let mut conn = database();
    let target = sync_targets(&conn, "review").unwrap().remove(0);
    sync_envelopes_into_cache(&mut conn, "review", &target, &[envelope(11)], 1234).unwrap();
    conn.execute_batch(
        "CREATE TRIGGER reject_reconciliation_delete BEFORE DELETE ON emails
         BEGIN SELECT RAISE(ABORT, 'injected write failure'); END;", 
    ).unwrap();
    let target = sync_targets(&conn, "review").unwrap().remove(0);
    assert!(sync_envelopes_with_reconciliation(
        &mut conn, "review", &target, &[envelope(20)], 1234, Some(&[envelope(20)]),
    ).is_err());
    let state: (i64, i64) = conn.query_row(
        "SELECT last_synced_uid,(SELECT uid FROM emails) FROM folders", [],
        |row| Ok((row.get(0)?, row.get(1)?)),
    ).unwrap();
    assert_eq!(state, (11, 11));
}

#[test]
fn new_arrival_in_reconciliation_snapshot_is_reported_once() {
    let mut conn = database();
    let target = sync_targets(&conn, "review").unwrap().remove(0);
    sync_envelopes_into_cache(&mut conn, "review", &target, &[envelope(11)], 1234).unwrap();
    let target = sync_targets(&conn, "review").unwrap().remove(0);

    let (_, arrivals) = sync_envelopes_with_reconciliation(
        &mut conn, "review", &target, &[], 1234, Some(&[envelope(12), envelope(11)]),
    ).unwrap();

    assert_eq!(arrivals.len(), 1);
    assert_eq!(arrivals[0].cursor, 12);
    let target = sync_targets(&conn, "review").unwrap().remove(0);
    assert_eq!(target.last_synced_uid, 12);
    let (_, repeated) = sync_envelopes_with_reconciliation(
        &mut conn, "review", &target, &[], 1234, Some(&[envelope(12), envelope(11)]),
    ).unwrap();
    assert!(repeated.is_empty());
}
