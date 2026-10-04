//! Deterministic independent-connection contention at audit-head selection.

use std::cell::RefCell;
use std::sync::mpsc::{self, Receiver, SyncSender};
use std::time::Duration;

use super::{SqliteStorage, VaultAuditRow};

const DEADLINE: Duration = Duration::from_secs(10);

#[derive(Debug, PartialEq, Eq)]
enum WriterEvent {
    Contended,
    HeadSelected,
}

thread_local! {
    static BUSY_SIGNAL: RefCell<Option<(SyncSender<WriterEvent>, Receiver<()>)>> = const { RefCell::new(None) };
}

fn await_first_commit(_: i32) -> bool {
    BUSY_SIGNAL.with(|signal| {
        let signal = signal.borrow();
        let (events, released) = signal.as_ref().expect("worker busy signal installed");
        events
            .send(WriterEvent::Contended)
            .expect("signal actual SQLite lock contention");
        released.recv_timeout(DEADLINE).is_ok()
    })
}

fn row() -> VaultAuditRow {
    VaultAuditRow {
        id: "first".to_string(),
        schema_version: 1,
        seq: 1,
        timestamp: "2026-09-06T00:00:00Z".to_string(),
        session_id: None,
        task_id: None,
        profile_id: "profile".to_string(),
        workspace_id: "workspace".to_string(),
        top_origin: None,
        frame_origin: None,
        item_id: None,
        item_alias: None,
        operation: "fill".to_string(),
        policy: None,
        decision: "allowed".to_string(),
        reason: None,
        device_name: "test device".to_string(),
        prev_hash: None,
        // Storage treats hashes as opaque bytes; core owns hash computation.
        entry_hash: vec![1; 32],
    }
}

#[test]
fn review_stale_audit_is_rejected_without_rewriting_authenticated_fields() {
    let db = SqliteStorage::open_in_memory_with_key("review-audit").unwrap();
    let first = row();
    db.vault_audit_transaction(|tx| tx.append_audit(&first)).unwrap();
    let stale = VaultAuditRow { id: "550e8400-e29b-41d4-a716-446655440000".into(), ..row() };
    assert!(db.vault_audit_transaction(|tx| tx.append_audit(&stale)).is_err());
    assert_eq!(db.list_vault_audit_events().unwrap(), vec![first.clone()]);
    let wrong_predecessor = VaultAuditRow { seq: 2, ..stale.clone() };
    assert!(db.vault_audit_transaction(|tx| tx.append_audit(&wrong_predecessor)).is_err());
    let next = VaultAuditRow { prev_hash: Some(first.entry_hash), ..wrong_predecessor };
    db.vault_audit_transaction(|tx| tx.append_audit(&next)).unwrap();
    assert_eq!(db.list_vault_audit_events().unwrap()[1], next);
}

#[test]
fn audit_head_is_selected_after_competing_transaction_commits() {
    // Given independent connections, with the first holding an uncommitted
    // audit append. A real SQLite busy callback supplies the contention event.
    let dir = tempfile::tempdir().expect("tempdir");
    let path = dir.path().join("audit-concurrency.sqlite");
    let path = path.to_str().expect("UTF-8 path");
    let first =
        SqliteStorage::open_with_key(path, "audit-concurrency-key").expect("first connection");
    let second =
        SqliteStorage::open_with_key(path, "audit-concurrency-key").expect("second connection");
    let (events, observed) = mpsc::sync_channel(2);
    let (release, released) = mpsc::sync_channel(1);
    let (completed, completion) = mpsc::sync_channel(1);
    let first_row = row();

    let worker = first
        .vault_audit_transaction(|transaction| {
            assert!(transaction.latest_audit_event()?.is_none());
            transaction.append_audit(&first_row)?;
            let worker = std::thread::spawn(move || {
                BUSY_SIGNAL.with(|signal| *signal.borrow_mut() = Some((events.clone(), released)));
                second
                    .conn
                    .busy_handler(Some(await_first_commit))
                    .expect("install busy handler");
                // When the second writer starts its real audited transaction.
                let result = second.vault_audit_transaction(|transaction| {
                    let head = transaction
                        .latest_audit_event()?
                        .expect("first committed audit");
                    events
                        .send(WriterEvent::HeadSelected)
                        .expect("signal head selection");
                    let next = VaultAuditRow {
                        id: "second".to_string(),
                        seq: head.seq.checked_add(1).expect("sequence fits"),
                        prev_hash: Some(head.entry_hash),
                        entry_hash: vec![2; 32],
                        ..row()
                    };
                    transaction.append_audit(&next)
                });
                assert!(
                    completed.send(result).is_ok(),
                    "signal second commit result"
                );
                BUSY_SIGNAL.with(|signal| *signal.borrow_mut() = None);
            });
            assert_eq!(
                observed.recv_timeout(DEADLINE).expect("writer event"),
                WriterEvent::Contended,
                "the second writer must acquire its write transaction before selecting the head"
            );
            Ok(worker)
        })
        .expect("first audit committed");
    release.send(()).expect("release contender after commit");
    assert_eq!(
        observed.recv_timeout(DEADLINE).expect("head selected"),
        WriterEvent::HeadSelected
    );
    completion
        .recv_timeout(DEADLINE)
        .expect("second writer completed")
        .expect("second audit committed");
    worker.join().expect("worker joined");

    // Then allocation uses the committed predecessor, not the stale empty head.
    let rows = first.list_vault_audit_events().expect("audit chain");
    assert_eq!(rows.len(), 2);
    assert_eq!(rows[0], first_row);
    assert_eq!(rows[1].seq, 2);
    assert_eq!(
        rows[1].prev_hash.as_deref(),
        Some(rows[0].entry_hash.as_slice())
    );
}
