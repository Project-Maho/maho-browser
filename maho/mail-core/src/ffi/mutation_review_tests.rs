//! Real migrated database and FFI regressions; no provider connections.
use super::*;
use crate::ffi::organize_api::evaluate_rules_for_email;
use maho_core::services::{email, offline_queue};
use serde_json::json;
use std::ffi::{CStr, CString};
use std::sync::mpsc;
use std::time::Duration;

fn rule(conn: &rusqlite::Connection, account: &str, actions: serde_json::Value) {
    conn.execute("INSERT INTO mail_rules (id, account_id, name, priority, conditions_json, actions_json) VALUES ('rule', ?1, 'fixture', 0, ?2, ?3)", rusqlite::params![account, json!([{"field":"from","operator":"equals","value":"sender@example.com"}]).to_string(), actions.to_string()]).unwrap();
}

#[test]
fn s02_move_rejects_foreign_account_without_changing_identity() {
    let pool = crate::test_support::pool_with_seeded_data();
    let conn = pool.get().unwrap();
    conn.execute("INSERT INTO folders (id, account_id, name, path, folder_type) VALUES ('foreign', 'acc2', 'Archive', 'Archive', 'archive')", []).unwrap();
    let result = email::move_email(&conn, "em1", "foreign");
    assert!(result.is_err(), "cross-account move was accepted");
    let identity: (String, i64) = conn
        .query_row(
            "SELECT folder_id, uid FROM emails WHERE id='em1'",
            [],
            |r| Ok((r.get(0)?, r.get(1)?)),
        )
        .unwrap();
    assert_eq!(identity, ("fold1".into(), 1));
}

#[test]
fn review_move_never_addresses_source_uid_in_destination() {
    for by_rule in [false, true] {
        let pool = crate::test_support::pool_with_seeded_data();
        let conn = pool.get().unwrap();
        if by_rule {
            rule(&conn, "acc1", json!([{"type":"move_to_folder","value":"Sent"}]));
            evaluate_rules_for_email(&conn, "acc1", "em1").unwrap();
        } else {
            email::move_email(&conn, "em1", "fold2").unwrap();
        }
        let folder: String = conn.query_row("SELECT folder_id FROM emails WHERE id='em1'", [], |r| r.get(0)).unwrap();
        assert_eq!(folder, "fold2");
        assert!(get_email_imap_info(&conn, "em1").unwrap().is_none(), "moved message must not address unrelated destination UID 1 (rule={by_rule})");
    }
}

#[test]
fn s09_queue_preserves_same_uid_in_two_mailboxes_across_reopen() {
    let pool = crate::test_support::pool_with_seeded_data();
    let conn = pool.get().unwrap();
    let path: String = conn
        .query_row("PRAGMA database_list", [], |r| r.get(2))
        .unwrap();
    offline_queue::queue_mutation(&conn, "acc1", 1, "INBOX", "star", None).unwrap();
    offline_queue::queue_mutation(&conn, "acc1", 1, "Sent", "star", None).unwrap();
    drop(conn);
    drop(pool);
    let reopened = maho_core::db::init_database_pool(std::path::Path::new(&path)).unwrap();
    let conn = reopened.get().unwrap();
    let mut folders: Vec<_> = offline_queue::list_pending_mutations(&conn, "acc1")
        .unwrap()
        .into_iter()
        .map(|m| m.folder_path.unwrap())
        .collect();
    folders.sort();
    assert_eq!(folders, ["INBOX", "Sent"]);
}

#[test]
fn s08_rules_cannot_apply_another_accounts_policy() {
    let pool = crate::test_support::pool_with_seeded_data();
    let conn = pool.get().unwrap();
    rule(&conn, "acc2", json!([{"type":"delete"}]));
    let _result = evaluate_rules_for_email(&conn, "acc2", "em1");
    let remaining: i64 = conn
        .query_row("SELECT COUNT(*) FROM emails WHERE id='em1'", [], |r| {
            r.get(0)
        })
        .unwrap();
    assert_eq!(remaining, 1, "foreign account rule deleted acc1 mail");
}

#[test]
fn s08_rules_apply_matching_account_policy() {
    let pool = crate::test_support::pool_with_seeded_data();
    let conn = pool.get().unwrap();
    conn.execute("UPDATE emails SET is_read=0 WHERE id='em1'", []).unwrap();
    rule(&conn, "acc1", json!([{"type":"mark_read"}]));

    evaluate_rules_for_email(&conn, "acc1", "em1").unwrap();

    let is_read: bool = conn.query_row(
        "SELECT is_read FROM emails WHERE id='em1'", [], |row| row.get(0),
    ).unwrap();
    assert!(is_read);
}

#[test]
fn s08_rules_report_database_read_failure() {
    let conn = rusqlite::Connection::open_in_memory().unwrap();

    let result = evaluate_rules_for_email(&conn, "acc1", "em1");

    assert!(matches!(result, Err(maho_core::error::AppError::Database(_))),
        "missing message table must not look like a successful rule evaluation: {result:?}");
}

#[test]
fn s08_rule_delete_has_durable_server_intent() {
    let pool = crate::test_support::pool_with_seeded_data();
    let conn = pool.get().unwrap();
    conn.execute("UPDATE folders SET uid_validity=1234 WHERE id='fold1'", [])
        .unwrap();
    rule(&conn, "acc1", json!([{"type":"delete"}]));
    evaluate_rules_for_email(&conn, "acc1", "em1").unwrap();
    let queued = offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
    assert_eq!(queued.len(), 1, "rule deletion discarded remote intent");
    assert_eq!(queued[0].mutation_type, "delete");
    assert_eq!(queued[0].folder_path.as_deref(), Some("INBOX"));
}

#[test]
fn s08_rule_queue_failure_is_observable_and_rolls_back_local_intent() {
    let pool = crate::test_support::pool_with_seeded_data();
    let conn = pool.get().unwrap();
    conn.execute("UPDATE folders SET uid_validity=1234 WHERE id='fold1'", [])
        .unwrap();
    rule(
        &conn,
        "acc1",
        json!([{"type":"mark_read"}, {"type":"delete"}]),
    );
    conn.execute_batch("CREATE TRIGGER reject_queue BEFORE INSERT ON pending_mutations BEGIN SELECT RAISE(ABORT, 'fixture queue failure'); END;").unwrap();
    let result = evaluate_rules_for_email(&conn, "acc1", "em1");
    assert!(result.is_err(), "queue failure was not reported");
    let is_read: bool = conn
        .query_row("SELECT is_read FROM emails WHERE id='em1'", [], |r| {
            r.get(0)
        })
        .unwrap();
    assert!(!is_read, "partial rule action escaped failed transaction");
}

#[test]
fn s02_write_identity_rejects_uid_overflow() {
    let pool = crate::test_support::pool_with_seeded_data();
    let conn = pool.get().unwrap();
    conn.execute("UPDATE emails SET uid=4294967297 WHERE id='em1'", [])
        .unwrap();
    let result = get_email_imap_info(&conn, "em1");
    assert!(
        !matches!(result, Ok(Some(_))),
        "out-of-range UID became a server command identity: {result:?}"
    );
}

#[test]
fn s02_write_identity_rejects_foreign_owned_folder() {
    let pool = crate::test_support::pool_with_seeded_data();
    let conn = pool.get().unwrap();
    conn.execute("UPDATE folders SET account_id='acc2' WHERE id='fold1'", [])
        .unwrap();
    let result = get_email_imap_info(&conn, "em1");
    assert!(
        !matches!(result, Ok(Some(_))),
        "foreign mailbox became an acc1 command identity: {result:?}"
    );
}

#[test]
fn s02_write_identity_preserves_maximum_valid_uid() {
    let pool = crate::test_support::pool_with_seeded_data();
    let conn = pool.get().unwrap();
    conn.execute("UPDATE emails SET uid=4294967295 WHERE id='em1'", [])
        .unwrap();
    assert_eq!(
        get_email_imap_info(&conn, "em1").unwrap(),
        Some(("acc1".into(), "INBOX".into(), u32::MAX))
    );
}

#[test]
fn s02_write_identity_does_not_address_local_uid_zero() {
    let pool = crate::test_support::pool_with_seeded_data();
    let conn = pool.get().unwrap();
    conn.execute("UPDATE emails SET uid=0 WHERE id='em1'", [])
        .unwrap();
    assert!(get_email_imap_info(&conn, "em1").unwrap().is_none());
}

unsafe extern "C" fn capture(ok: bool, payload: *const c_char, data: *mut c_void) {
    let sender = unsafe { Box::from_raw(data.cast::<mpsc::Sender<(bool, String)>>()) };
    let text = unsafe { CStr::from_ptr(payload) }
        .to_string_lossy()
        .into_owned();
    if let Err(error) = sender.send((ok, text)) {
        eprintln!("mutation callback receiver already closed: {error}");
    }
}

#[test]
fn mutation_callback_tolerates_dropped_receiver() {
    const CHILD: &str = "MAHO_MUTATION_CALLBACK_CHILD";
    if std::env::var_os(CHILD).is_some() {
        let (sender, receiver) = mpsc::channel::<(bool, String)>();
        drop(receiver);
        let data = Box::into_raw(Box::new(sender));
        let payload = CString::new("{}").unwrap();
        unsafe { capture(false, payload.as_ptr(), data.cast()) };
        return;
    }
    let output = std::process::Command::new(std::env::current_exe().unwrap())
        .args(["--exact", "ffi::write_api::mutation_review_tests::mutation_callback_tolerates_dropped_receiver", "--nocapture"])
        .env(CHILD, "1")
        .output().unwrap();
    assert!(output.status.success(), "child callback failed: {}", String::from_utf8_lossy(&output.stderr));
}

#[test]
fn cc02_failed_write_cannot_leave_unqueued_local_intent() {
    let _guard = crate::test_support::global_ctx_guard();
    crate::runtime::runtime().unwrap();
    let pool = crate::test_support::pool_with_seeded_data();
    let ctx = crate::test_support::ctx_arc(pool);
    let conn = ctx.pool.get().unwrap();
    // Credential resolution fails before any network activity. Queue insertion also
    // fails, so neither returning an error nor claiming offline success can lose intent.
    conn.execute("UPDATE accounts SET password=NULL WHERE id='acc1'", [])
        .unwrap();
    conn.execute("UPDATE folders SET uid_validity=1234 WHERE id='fold1'", [])
        .unwrap();
    conn.execute_batch("CREATE TRIGGER reject_queue BEFORE INSERT ON pending_mutations BEGIN SELECT RAISE(ABORT, 'fixture queue failure'); END;").unwrap();
    crate::state::set_ctx(ctx.clone()).unwrap();
    let (sender, receiver) = mpsc::channel::<(bool, String)>();
    let data = Box::into_raw(Box::new(sender)).cast::<c_void>();
    let id = CString::new("em1").unwrap();
    assert!(MahoMailMarkRead(id.as_ptr(), Some(capture), data));
    let (ok, payload) = receiver
        .recv_timeout(Duration::from_secs(15))
        .expect("exact FFI callback");
    assert!(!ok, "failed durable write was accepted: {payload}");
    let is_read: bool = conn
        .query_row("SELECT is_read FROM emails WHERE id='em1'", [], |r| {
            r.get(0)
        })
        .unwrap();
    assert!(
        !is_read,
        "local read intent committed without durable queue"
    );
}

#[test]
fn cc02_failed_unread_cannot_leave_unqueued_local_intent() {
    failed_flag_preserves_local_intent("is_read", true, super::MahoMailMarkUnread);
}

#[test]
fn cc02_failed_star_cannot_leave_unqueued_local_intent() {
    failed_flag_preserves_local_intent("is_starred", false, super::MahoMailToggleStar);
}

fn failed_flag_preserves_local_intent(
    column: &str,
    original: bool,
    invoke: extern "C" fn(*const std::os::raw::c_char, super::MahoMailReadCallback, *mut std::ffi::c_void) -> bool,
) {
    let _guard = crate::test_support::global_ctx_guard();
    crate::runtime::runtime().unwrap();
    let ctx = crate::test_support::ctx_arc(crate::test_support::pool_with_seeded_data());
    let conn = ctx.pool.get().unwrap();
    conn.execute_batch(
        "UPDATE accounts SET password=NULL,oauth2_access_token=NULL WHERE id='acc1';
         DELETE FROM encrypted_credentials;
         DROP TABLE pending_mutations;"
    ).unwrap();
    conn.execute(&format!("UPDATE emails SET {column}=?1 WHERE id='em1'"), [original]).unwrap();
    crate::state::set_ctx(std::sync::Arc::clone(&ctx)).unwrap();
    let (sender, receiver) = std::sync::mpsc::channel::<(bool, String)>();
    let data = Box::into_raw(Box::new(sender));
    let id = std::ffi::CString::new("em1").unwrap();
    let accepted = invoke(id.as_ptr(), Some(capture), data.cast());
    if !accepted {
        drop(unsafe { Box::from_raw(data) });
    }
    let result = receiver.recv_timeout(std::time::Duration::from_secs(8));
    crate::state::clear_ctx_for_test();
    assert!(accepted);
    let (ok, payload) = result.expect("write callback");
    assert!(!ok, "failed durable write was accepted: {payload}");
    let current: bool = conn.query_row(
        &format!("SELECT {column} FROM emails WHERE id='em1'"), [], |row| row.get(0),
    ).unwrap();
    assert_eq!(current, original, "local flag committed without durable intent");
}
