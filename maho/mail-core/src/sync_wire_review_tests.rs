use super::*;
use maho_core::imap_client::ImapClient;
use std::sync::Mutex;

#[path = "../vendor/maho-core/src/imap_review_fixtures/tls.rs"]
mod tls;

fn context() -> Arc<AppCtx> {
    let ctx = crate::test_support::ctx_arc(crate::test_support::pool_with_seeded_data());
    ctx.pool.get().unwrap().execute_batch(
        "DELETE FROM emails; UPDATE folders SET uid_validity=0,last_synced_uid=0 WHERE id='fold1';"
    ).unwrap();
    ctx
}

async fn tick(ctx: &Arc<AppCtx>, mailbox: Arc<Mutex<tls::Mailbox>>) -> Vec<String> {
    tick_with_count(ctx, mailbox).await.0
}

async fn tick_with_count(ctx: &Arc<AppCtx>, mailbox: Arc<Mutex<tls::Mailbox>>) -> (Vec<String>, usize) {
    let conn = ctx.pool.get().unwrap();
    let auth = resolve_account_auth(&conn, &ctx.credential_key, "acc1").unwrap();
    let target = sync_targets(&conn, "acc1").unwrap().into_iter().find(|f| f.id == "fold1").unwrap();
    drop(conn);
    let peer = tls::Peer::new(move |tag, command| mailbox.lock().unwrap().reply(tag, command));
    let (peer_tx, peer_rx) = tokio::sync::oneshot::channel();
    let result = sync_folder_with_connector(Arc::clone(ctx), auth, target, move |_| {
        let mut peer = peer;
        let client = peer.connect();
        assert!(peer_tx.send(peer).is_ok());
        Ok(client)
    }).await;
    let peer = tokio::time::timeout(tls::DEADLINE, peer_rx).await.unwrap().unwrap();
    let commands = peer.finish();
    (commands, result.unwrap().2.len())
}

fn rows(ctx: &AppCtx) -> Vec<(u32, bool, bool)> {
    ctx.pool.get().unwrap().prepare(
        "SELECT uid,is_read,is_starred FROM emails WHERE folder_id='fold1' ORDER BY uid"
    ).unwrap().query_map([], |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?)))
        .unwrap().collect::<std::result::Result<_, _>>().unwrap()
}

#[tokio::test]
async fn sparse_history_converges_without_old_message_notifications() {
    let ctx = context();
    let mut mailbox = tls::Mailbox::seeded(0);
    mailbox.messages = (1..=1001).map(|index| (index * 1000, vec![])).collect();
    let remote = Arc::new(Mutex::new(mailbox));

    assert_eq!(tick_with_count(&ctx, remote.clone()).await.1, 0);
    remote.lock().unwrap().messages.insert(1_002_000, vec![]);
    assert_eq!(tick_with_count(&ctx, remote.clone()).await.1, 1);
    assert_eq!(tick_with_count(&ctx, remote.clone()).await.1, 0);
    assert_eq!(tick_with_count(&ctx, remote.clone()).await.1, 0);

    let cached = rows(&ctx);
    assert_eq!(cached.len(), 1002);
    assert_eq!(cached.first().unwrap().0, 1000);
    assert!(remote.lock().unwrap().fetch_sizes.iter().all(|size| *size <= 500));
}

#[tokio::test]
async fn history_epoch_change_discards_entire_fetched_batch() {
    let ctx = context();
    let mailbox = Arc::new(Mutex::new(tls::Mailbox::seeded(1001)));
    tick(&ctx, mailbox.clone()).await;
    let before = rows(&ctx);
    let db = ctx.pool.get().unwrap();
    let auth = resolve_account_auth(&db, &ctx.credential_key, "acc1").unwrap();
    let target = sync_targets(&db, "acc1").unwrap().into_iter().find(|f| f.id == "fold1").unwrap();
    drop(db);
    let (peer_tx, peer_rx) = tokio::sync::oneshot::channel();
    let result = sync_folder_with_connector(ctx.clone(), auth, target, move |_| {
        let mut selects = 0;
        let mut peer = tls::Peer::new(move |tag, command| {
            let mut mailbox = mailbox.lock().unwrap();
            if command.starts_with("SELECT ") {
                selects += 1;
                if selects == 3 { mailbox.epoch = 5678; }
            }
            mailbox.reply(tag, command)
        });
        let client = peer.connect();
        assert!(peer_tx.send(peer).is_ok());
        Ok(client)
    }).await;
    let peer = tokio::time::timeout(tls::DEADLINE, peer_rx).await.unwrap().unwrap();
    let commands = peer.finish();

    assert!(result.is_err());
    assert_eq!(rows(&ctx), before);
    let state: (u32, u32) = ctx.pool.get().unwrap().query_row(
        "SELECT last_synced_uid, uid_validity FROM folders WHERE id='fold1'", [],
        |row| Ok((row.get(0)?, row.get(1)?)),
    ).unwrap();
    assert_eq!(state, (1001, 1234));
    assert!(commands.iter().any(|command| command == "LOGOUT"));
}

#[tokio::test]
async fn incomplete_recent_page_preserves_cached_history() {
    let ctx = context();
    let mailbox = Arc::new(Mutex::new(tls::Mailbox::seeded(1001)));
    for _ in 0..3 { tick(&ctx, mailbox.clone()).await; }
    let before = rows(&ctx);
    assert_eq!(before.len(), 1001);
    let db = ctx.pool.get().unwrap();
    let auth = resolve_account_auth(&db, &ctx.credential_key, "acc1").unwrap();
    let target = sync_targets(&db, "acc1").unwrap().into_iter().find(|f| f.id == "fold1").unwrap();
    drop(db);
    let (peer_tx, peer_rx) = tokio::sync::oneshot::channel();
    let result = sync_folder_with_connector(ctx.clone(), auth, target, move |_| {
        let mut peer = tls::Peer::new(move |tag, command| {
            let response = mailbox.lock().unwrap().reply(tag, command);
            if command.starts_with("FETCH ") {
                return response.split_inclusive("\r\n")
                    .filter(|line| !line.starts_with("* 1001 FETCH ")).collect();
            }
            response
        });
        let client = peer.connect();
        assert!(peer_tx.send(peer).is_ok());
        Ok(client)
    }).await;
    let peer = tokio::time::timeout(tls::DEADLINE, peer_rx).await.unwrap().unwrap();
    peer.finish();

    assert_eq!(rows(&ctx), before);
    assert!(result.is_err());
}

#[tokio::test]
async fn older_history_arrives_across_bounded_sync_batches() {
    let ctx = context();
    let mailbox = Arc::new(Mutex::new(tls::Mailbox::seeded(1001)));
    let mut commands = Vec::new();
    // Four explicit sync actions, not timing/polling. Three 500-message batches
    // suffice for this mailbox; the fourth also exercises the exhausted cursor.
    for _ in 0..4 { commands.extend(tick(&ctx, Arc::clone(&mailbox)).await); }
    let uids: Vec<_> = rows(&ctx).into_iter().map(|r| r.0).collect();
    assert_eq!(uids, (1..=1001).collect::<Vec<_>>(), "wire={commands:?}");
    assert!(mailbox.lock().unwrap().fetch_sizes.iter().all(|&count| count <= 500));
}

#[tokio::test]
async fn remote_flags_change_on_existing_older_uid_without_new_mail() {
    let ctx = context();
    let mailbox = Arc::new(Mutex::new(tls::Mailbox::seeded(2)));
    tick(&ctx, Arc::clone(&mailbox)).await;
    assert_eq!(rows(&ctx), [(1, false, false), (2, false, false)]);
    mailbox.lock().unwrap().messages.insert(1, vec!["\\Seen", "\\Flagged"]);
    let commands = tick(&ctx, Arc::clone(&mailbox)).await;
    assert_eq!(rows(&ctx), [(1, true, true), (2, false, false)], "wire={commands:?}");
    mailbox.lock().unwrap().messages.insert(1, vec![]);
    tick(&ctx, mailbox).await;
    assert_eq!(rows(&ctx), [(1, false, false), (2, false, false)]);
}

#[tokio::test]
async fn remote_expunge_removes_only_missing_uid_and_recounts_folder() {
    let ctx = context();
    let mailbox = Arc::new(Mutex::new(tls::Mailbox::seeded(2)));
    tick(&ctx, Arc::clone(&mailbox)).await;
    assert_eq!(rows(&ctx).len(), 2);
    mailbox.lock().unwrap().messages.remove(&1);
    let commands = tick(&ctx, mailbox).await;
    assert_eq!(rows(&ctx), [(2, false, false)], "wire={commands:?}");
    let counts: (i64, i64) = ctx.pool.get().unwrap().query_row(
        "SELECT total_count,unread_count FROM folders WHERE id='fold1'", [],
        |r| Ok((r.get(0)?,r.get(1)?))).unwrap();
    assert_eq!(counts, (1,1));
}

#[tokio::test]
async fn reconciliation_reaches_flags_and_deletion_beyond_newest_window() {
    let ctx = context();
    let mailbox = Arc::new(Mutex::new(tls::Mailbox::seeded(1001)));
    for _ in 0..4 {
        tick(&ctx, Arc::clone(&mailbox)).await;
    }
    assert_eq!(rows(&ctx).len(), 1001, "history must be present before reconciliation");
    {
        let mut remote = mailbox.lock().unwrap();
        remote.messages.insert(1, vec!["\\Seen", "\\Flagged"]);
        remote.messages.remove(&2);
    }
    // Explicit bounded reconciliation cycles, not waiting for a timer.
    for _ in 0..4 {
        tick(&ctx, Arc::clone(&mailbox)).await;
    }
    let cached = rows(&ctx);
    assert_eq!(cached.first(), Some(&(1, true, true)));
    assert!(!cached.iter().any(|row| row.0 == 2));
    assert_eq!(cached.len(), 1000);
    assert!(mailbox.lock().unwrap().fetch_sizes.iter().all(|size| *size <= 500));
}

#[test]
fn tls_fixture_returns_changed_flags_and_actual_uid_membership() {
    let mailbox = Arc::new(Mutex::new(tls::Mailbox::seeded(2)));
    let remote = Arc::clone(&mailbox);
    let mut peer = tls::Peer::new(move |tag, command| remote.lock().unwrap().reply(tag, command));
    let mut client = peer.connect();
    let (before, _) = client.fetch_envelopes_since_uid("INBOX", 0).unwrap();
    assert_eq!(before.len(), 2);
    {
        let mut remote = mailbox.lock().unwrap();
        remote.messages.remove(&1);
        remote.messages.insert(2, vec!["\\Seen", "\\Flagged"]);
    }
    let (after, _) = client.fetch_envelopes_since_uid("INBOX", 0).unwrap();
    client.logout().unwrap();
    peer.finish();
    assert_eq!(after.len(), 1);
    assert_eq!(after[0].uid, 2);
    assert!(after[0].flags.iter().any(|flag| flag.contains("Seen")));
    assert!(after[0].flags.iter().any(|flag| flag.contains("Flagged")));
}
