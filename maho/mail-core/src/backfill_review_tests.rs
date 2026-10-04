use super::*;
use maho_core::imap_client::ImapClient;
use std::sync::mpsc;

#[path = "../vendor/maho-core/src/imap_review_fixtures/tls.rs"]
mod tls;

#[tokio::test]
async fn stopped_backfill_cannot_commit_body_after_inflight_fetch() {
    backfill_response(false, true).await;
}

#[tokio::test]
async fn stopped_backfill_cannot_mark_missing_body_fetched() {
    backfill_response(true, true).await;
}

#[tokio::test]
async fn live_backfill_commits_body_after_inflight_fetch() {
    backfill_response(false, false).await;
}

#[tokio::test]
async fn live_backfill_marks_missing_body_fetched() {
    backfill_response(true, false).await;
}

async fn backfill_response(missing: bool, stop_before_response: bool) {
    let ctx = crate::test_support::ctx_arc(crate::test_support::pool_with_seeded_data());
    ctx.pool.get().unwrap().execute(
        "UPDATE folders SET uid_validity=1234 WHERE id='fold1'", [],
    ).unwrap();
    ctx.pool.get().unwrap().execute(
        "UPDATE emails SET body_text=NULL,body_html=NULL,body_fetched_at=NULL WHERE id='em1'", [],
    ).unwrap();
    let (fetch_tx, fetch_rx) = tokio::sync::oneshot::channel();
    let (release_tx, release_rx) = mpsc::channel();
    let mut fetch_tx = Some(fetch_tx);
    let mut mailbox = tls::Mailbox::seeded(2);
    let mut peer = tls::Peer::new(move |tag, command| {
        if command.starts_with("UID FETCH ") {
            fetch_tx.take().unwrap().send(()).unwrap();
            release_rx.recv_timeout(tls::DEADLINE).unwrap();
            if missing {
                return format!("{tag} OK no matching UID\r\n");
            }
        }
        mailbox.reply(tag, command)
    });
    let (complete_tx, complete_rx) = tokio::sync::oneshot::channel();
    let work_ctx = Arc::clone(&ctx);
    let registry = Arc::new(crate::state::SyncRegistry::default());
    let work_registry = Arc::clone(&registry);
    let stop = Arc::new(AtomicBool::new(false));
    let work_stop = Arc::clone(&stop);
    let (start_tx, start_rx) = tokio::sync::oneshot::channel();
    // Completion belongs to the blocking closure, not its abortable supervisor.
    let task = tokio::spawn(async move {
        start_rx.await.unwrap();
        tokio::task::spawn_blocking(move || {
            let worker = BackfillWorker {
                registry: &work_registry,
                account_id: "acc1",
                stop: &work_stop,
            };
            let result = backfill_batch_with_connector(&work_ctx, &worker, |_| Ok(peer.connect()));
            let commands = peer.finish();
            complete_tx.send((result, commands)).unwrap();
        }).await.unwrap();
    });
    registry.insert_backfill("acc1".into(), WorkerHandle {
        stop, tasks: vec![task], wake: None,
    });
    start_tx.send(()).unwrap();
    tokio::time::timeout(tls::DEADLINE, fetch_rx).await.unwrap().unwrap();
    if stop_before_response {
        registry.stop_account("acc1");
    }
    release_tx.send(()).unwrap();
    let (result, commands) = tokio::time::timeout(tls::DEADLINE * 2, complete_rx).await.unwrap().unwrap();
    registry.stop_account("acc1");
    // Cancellation may be reported as an error or an empty batch; DB state is
    // authoritative. Successful download after stop must not persist anything.
    let stored: (Option<String>, Option<String>) = ctx.pool.get().unwrap().query_row(
        "SELECT body_text,body_fetched_at FROM emails WHERE id='em1'", [],
        |row| Ok((row.get(0)?, row.get(1)?)),
    ).unwrap();
    if stop_before_response {
        assert_eq!(stored, (None, None), "result={result:?}, wire={commands:?}");
    } else {
        assert_eq!(result.unwrap(), 1);
        assert_eq!(stored.0, (!missing).then(|| "body for UID 1\r\n".to_string()));
        assert!(stored.1.is_some());
    }
    assert!(commands.iter().any(|command| command == "LOGOUT"));
}

#[test]
fn backfill_refuses_body_from_changed_server_epoch() {
    backfill_epoch_response(false, false, Some(1234), 5678);
}

#[test]
fn backfill_discards_body_when_local_epoch_changes_during_fetch() {
    backfill_epoch_response(false, true, Some(1234), 1234);
}

#[test]
fn backfill_discards_missing_marker_when_local_epoch_changes_during_fetch() {
    backfill_epoch_response(true, true, Some(1234), 1234);
}

#[test]
fn backfill_refuses_network_without_known_epoch() {
    backfill_epoch_response(false, false, Some(0), 1234);
}

#[test]
fn backfill_skips_local_only_and_foreign_folder_candidates() {
    let ctx = crate::test_support::ctx_arc(crate::test_support::pool_with_seeded_data());
    ctx.pool.get().unwrap().execute_batch(
        "UPDATE folders SET uid_validity=1234 WHERE id='fold1';
         UPDATE emails SET body_fetched_at=NULL WHERE id='em1';
         INSERT INTO folders(id,account_id,name,path,folder_type,uid_validity)
         VALUES('foreign','acc2','Foreign','Foreign','inbox',1234);
         INSERT INTO emails(id,account_id,folder_id,uid,message_id,subject,from_address,to_addresses,date,snippet,is_draft)
         VALUES('local','acc1','fold1',0,'local','local','a@b.test','[]','2030-01-01','',1),
               ('foreign-message','acc1','foreign',3,'foreign','foreign','a@b.test','[]','2030-01-02','',0);"
    ).unwrap();
    let mut mailbox = tls::Mailbox::seeded(2);
    let mut peer = tls::Peer::new(move |tag, command| mailbox.reply(tag, command));
    let registry = SyncRegistry::default();
    let stop = Arc::new(AtomicBool::new(false));
    registry.insert_backfill("acc1".into(), WorkerHandle { stop: Arc::clone(&stop), tasks: vec![], wake: None });
    let worker = BackfillWorker { registry: &registry, account_id: "acc1", stop: &stop };
    let mut connected = false;
    let result = backfill_batch_with_connector(&ctx, &worker, |_| {
        connected = true;
        Ok(peer.connect())
    });
    let commands = if connected { peer.finish() } else { vec![] };
    assert_eq!(result.unwrap(), 1);
    assert_eq!(commands.iter().filter(|command| command.starts_with("UID FETCH ")).count(), 1);
    let db = ctx.pool.get().unwrap();
    let untouched: i64 = db.query_row(
        "SELECT count(*) FROM emails WHERE id IN ('local','foreign-message')
         AND body_text IS NULL AND body_fetched_at IS NULL", [], |row| row.get(0),
    ).unwrap();
    assert_eq!(untouched, 2);
    let fetched: bool = db.query_row(
        "SELECT body_text IS NOT NULL AND body_fetched_at IS NOT NULL FROM emails WHERE id='em1'", [], |row| row.get(0),
    ).unwrap();
    assert!(fetched);
}

fn backfill_epoch_response(missing: bool, change_local: bool, local_epoch: Option<u32>, server_epoch: u32) {
    let ctx = crate::test_support::ctx_arc(crate::test_support::pool_with_seeded_data());
    ctx.pool.get().unwrap().execute_batch(
        "UPDATE emails SET body_text=NULL,body_html=NULL,body_fetched_at=NULL WHERE id='em1';"
    ).unwrap();
    ctx.pool.get().unwrap().execute(
        "UPDATE folders SET uid_validity=?1 WHERE id='fold1'", [local_epoch],
    ).unwrap();
    let pool = ctx.pool.clone();
    let mut mailbox = tls::Mailbox::seeded(2);
    mailbox.epoch = server_epoch;
    let mut peer = tls::Peer::new(move |tag, command| {
        if command.starts_with("UID FETCH ") {
            if change_local {
                pool.get().unwrap().execute(
                    "UPDATE folders SET uid_validity=5678 WHERE id='fold1'", [],
                ).unwrap();
            }
            if missing { return format!("{tag} OK absent\r\n"); }
        }
        mailbox.reply(tag, command)
    });
    let registry = SyncRegistry::default();
    let stop = Arc::new(AtomicBool::new(false));
    registry.insert_backfill("acc1".into(), WorkerHandle { stop: Arc::clone(&stop), tasks: vec![], wake: None });
    let worker = BackfillWorker { registry: &registry, account_id: "acc1", stop: &stop };
    let mut connected = false;
    let result = backfill_batch_with_connector(&ctx, &worker, |_| {
        connected = true;
        Ok(peer.connect())
    });
    let commands = if connected { peer.finish() } else { vec![] };
    let stored: (Option<String>, Option<String>) = ctx.pool.get().unwrap().query_row(
        "SELECT body_text,body_fetched_at FROM emails WHERE id='em1'", [],
        |row| Ok((row.get(0)?, row.get(1)?)),
    ).unwrap();
    assert_eq!(stored, (None, None), "result={result:?}, wire={commands:?}");
    assert!(result.is_err(), "foreign or unknown mailbox epoch accepted");
    if local_epoch == Some(0) { assert!(!connected); }
    if !change_local {
        assert!(!commands.iter().any(|command| command.starts_with("UID FETCH ")));
    }
}
