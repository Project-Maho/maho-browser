use super::*;

fn fixture() -> (crate::state::AppCtx, FolderSyncTarget) {
    let ctx = crate::test_support::ctx(crate::test_support::pool_with_seeded_data());
    let conn = ctx.pool.get().unwrap();
    conn.execute("UPDATE folders SET uid_validity=1234,last_synced_uid=2 WHERE id='fold1'", []).unwrap();
    let folder = super::super::sync_targets(&conn, "acc1").unwrap().into_iter()
        .find(|folder| folder.id == "fold1").unwrap();
    drop(conn);
    (ctx, folder)
}

#[test]
fn review_stale_flags_cannot_undo_committed_user_action() {
    let (ctx, folder) = fixture();
    let mut conn = ctx.pool.get().unwrap();
    let snapshot = Snapshot { selection: select(&conn, &folder).unwrap(), flags: vec![(1, false, false), (2, true, true)] };
    maho_core::services::email::mark_read(&conn, "em1").unwrap();
    apply(&conn, &folder, &snapshot).unwrap();
    let env = ImapEnvelope { uid: 1, message_id: "same".into(), in_reply_to: None, subject: "same".into(), from_address: "sender@test".into(), from_name: None, to_addresses: vec![], cc_addresses: vec![], date: "2026-09-14T00:00:00Z".into(), flags: vec![], size: 1, has_attachments: false, attachments: vec![] };
    super::super::sync_envelopes_with_snapshots(&mut conn, "acc1", &folder, &[env.clone()], 1234, Some(&[env]), Some(&snapshot)).unwrap();
    let read: bool = conn.query_row("SELECT is_read FROM emails WHERE id='em1'", [], |r| r.get(0)).unwrap();
    assert!(read, "pre-action snapshot reverted successfully committed mark-read");
}

#[test]
fn review_pending_flags_survive_both_reconciliation_paths() {
    for envelope_page in [false, true] {
        let (ctx, folder) = fixture();
        let mut conn = ctx.pool.get().unwrap();
        maho_core::services::email::mark_read(&conn, "em1").unwrap();
        maho_core::services::offline_queue::queue_mutation(&conn, "acc1", 1, "INBOX", "mark_read", None).unwrap();
        let snapshot = Snapshot { selection: select(&conn, &folder).unwrap(), flags: vec![(1, false, false), (2, true, true)] };
        if envelope_page {
            let env = ImapEnvelope { uid: 1, message_id: "same".into(), in_reply_to: None, subject: "same".into(), from_address: "sender@test".into(), from_name: None, to_addresses: vec![], cc_addresses: vec![], date: "2026-09-14T00:00:00Z".into(), flags: vec![], size: 1, has_attachments: false, attachments: vec![] };
            super::super::sync_envelopes_with_snapshots(&mut conn, "acc1", &folder, &[], 1234, Some(&[env]), Some(&snapshot)).unwrap();
        } else { apply(&conn, &folder, &snapshot).unwrap(); }
        let read: bool = conn.query_row("SELECT is_read FROM emails WHERE id='em1'", [], |r| r.get(0)).unwrap();
        assert!(read, "provider flags overwrote pending offline intent (envelopes={envelope_page})");
    }
}

#[test]
fn committed_cursor_is_visible_to_a_later_read_and_wraps() {
    let (ctx, folder) = fixture();
    {
        let mut first = ctx.pool.get().unwrap();
        let selection = select(&first, &folder).unwrap();
        assert_eq!(selection.uids, [1, 2]);
        let tx = first.transaction().unwrap();
        apply(&tx, &folder, &Snapshot { selection, flags: vec![(1, true, false), (2, true, true)] }).unwrap();
        tx.commit().unwrap();
    }
    let second = ctx.pool.get().unwrap();
    let wrapped = select(&second, &folder).unwrap();
    assert_eq!(wrapped.cursor, 2);
    assert_eq!(wrapped.uids, [1, 2]);
}

#[test]
fn failed_checkpoint_rolls_back_remote_flag_and_delete_changes() {
    let (ctx, folder) = fixture();
    let mut conn = ctx.pool.get().unwrap();
    conn.execute_batch("CREATE TRIGGER reject_reconciliation BEFORE UPDATE OF reconciliation_uid ON folders
        BEGIN SELECT RAISE(FAIL,'fixture checkpoint failure'); END;").unwrap();
    let snapshot = Snapshot { selection: select(&conn, &folder).unwrap(), flags: vec![(1, true, true)] };

    let result = super::super::sync_envelopes_with_snapshots(
        &mut conn, "acc1", &folder, &[], 1234, None, Some(&snapshot),
    );

    assert!(result.is_err());
    let state: (i64, bool, bool, u32) = conn.query_row(
        "SELECT (SELECT count(*) FROM emails), is_read, is_starred,
         (SELECT reconciliation_uid FROM folders WHERE id='fold1') FROM emails WHERE id='em1'", [],
        |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?, row.get(3)?)),
    ).unwrap();
    assert_eq!(state, (2, false, false, 0));
}

#[test]
fn stale_page_cannot_delete_after_another_checkpoint_advances() {
    let (ctx, folder) = fixture();
    let conn = ctx.pool.get().unwrap();
    let snapshot = Snapshot { selection: select(&conn, &folder).unwrap(), flags: Vec::new() };
    conn.execute("UPDATE folders SET reconciliation_uid=2 WHERE id='fold1'", []).unwrap();

    apply(&conn, &folder, &snapshot).unwrap();

    let count: i64 = conn.query_row("SELECT count(*) FROM emails", [], |row| row.get(0)).unwrap();
    assert_eq!(count, 2);
}

#[test]
fn stale_page_cannot_apply_after_cursor_wrap() {
    let (ctx, folder) = fixture();
    let conn = ctx.pool.get().unwrap();
    conn.execute("UPDATE folders SET reconciliation_uid=2 WHERE id='fold1'", []).unwrap();
    let stale = Snapshot { selection: select(&conn, &folder).unwrap(), flags: Vec::new() };
    let fresh = Snapshot { selection: select(&conn, &folder).unwrap(), flags: vec![(1, true, true), (2, true, true)] };
    apply(&conn, &folder, &fresh).unwrap();
    apply(&conn, &folder, &stale).unwrap();
    let count: i64 = conn.query_row("SELECT count(*) FROM emails", [], |row| row.get(0)).unwrap();
    assert_eq!(count, 2);
}
