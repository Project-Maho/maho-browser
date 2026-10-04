use super::*;
use maho_core::imap_client::ImapClient;
use std::future::Future;
use std::sync::atomic::AtomicBool;
use std::task::Poll;

#[path = "../vendor/maho-core/src/imap_review_fixtures/tls.rs"]
mod tls;

struct RegisteredBackfill;
impl Drop for RegisteredBackfill {
    fn drop(&mut self) { crate::state::registry().stop_account("acc1"); }
}

#[tokio::test]
async fn sync_commit_wakes_captured_backfill_generation() {
    wake_after_sync(false, false).await;
}

#[tokio::test]
async fn sync_commit_failure_does_not_wake_backfill() {
    wake_after_sync(true, false).await;
}

#[tokio::test]
async fn replaced_backfill_is_not_woken_by_old_sync_completion() {
    wake_after_sync(false, true).await;
}

async fn wake_after_sync(fail_commit: bool, replace: bool) {
    let _context_guard = crate::test_support::global_ctx_guard();
    let _registered = RegisteredBackfill;
    let ctx = crate::test_support::ctx_arc(crate::test_support::pool_with_seeded_data());
    let conn = ctx.pool.get().unwrap();
    conn.execute_batch("DELETE FROM emails; UPDATE folders SET uid_validity=1234,last_synced_uid=10 WHERE id='fold1';").unwrap();
    if fail_commit {
        conn.execute_batch("CREATE TRIGGER reject_sync BEFORE INSERT ON emails BEGIN SELECT RAISE(ABORT,'fixture'); END;").unwrap();
    }
    let auth = resolve_account_auth(&conn, &ctx.credential_key, "acc1").unwrap();
    let target = sync_targets(&conn, "acc1").unwrap().into_iter().find(|folder| folder.id == "fold1").unwrap();
    drop(conn);
    let old = Arc::new(tokio::sync::Notify::new());
    let replacement = Arc::new(tokio::sync::Notify::new());
    crate::state::registry().insert_backfill("acc1".into(), WorkerHandle {
        stop: Arc::new(AtomicBool::new(false)), tasks: vec![], wake: Some(Arc::clone(&old)),
    });
    let replacement_in_fetch = Arc::clone(&replacement);
    let mut replaced = false;
    let mut mailbox = tls::Mailbox::seeded(11);
    let peer = tls::Peer::new(move |tag, command| {
        if replace && !replaced && command.starts_with("UID FETCH ") {
            replaced = true;
            crate::state::registry().insert_backfill("acc1".into(), WorkerHandle {
                stop: Arc::new(AtomicBool::new(false)), tasks: vec![], wake: Some(Arc::clone(&replacement_in_fetch)),
            });
        }
        mailbox.reply(tag, command)
    });
    let (peer_tx, peer_rx) = tokio::sync::oneshot::channel();
    let result = sync_folder_with_connector(Arc::clone(&ctx), auth, target, move |_| {
        let mut peer = peer;
        let client = peer.connect();
        assert!(peer_tx.send(peer).is_ok());
        Ok(client)
    }).await;
    let peer = tokio::time::timeout(tls::DEADLINE, peer_rx).await.unwrap().unwrap();
    let commands = peer.finish();
    assert!(commands.iter().any(|command| command.starts_with("UID FETCH ")));
    assert_eq!(result.is_err(), fail_commit);
    if !fail_commit && !replace {
        tokio::time::timeout(Duration::from_secs(1), old.notified()).await.unwrap();
    } else {
        let notify = if replace { replacement } else { old };
        let mut future = Box::pin(notify.notified());
        std::future::poll_fn(|cx| {
            assert!(future.as_mut().poll(cx).is_pending(), "invalid commit woke a worker");
            Poll::Ready(())
        }).await;
    }
}
