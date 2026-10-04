use super::*;
use maho_core::imap_client::ImapClient;

#[path = "../vendor/maho-core/src/imap_review_fixtures/tls.rs"]
mod tls;

#[test]
fn reader_persists_body_when_server_and_local_epoch_match() {
    let ctx = crate::test_support::ctx(crate::test_support::pool_with_seeded_data());
    ctx.pool.get().unwrap().execute_batch(
        "UPDATE emails SET body_text=NULL,body_html=NULL,body_fetched_at=NULL WHERE id='em1';
         UPDATE folders SET uid_validity=1234 WHERE id='fold1';"
    ).unwrap();
    let mut mailbox = tls::Mailbox::seeded(2);
    let mut peer = tls::Peer::new(move |tag, command| mailbox.reply(tag, command));

    let result = get_email_with_connector(&ctx, "em1", |_| Ok(peer.connect())).unwrap();
    let commands = peer.finish();

    let stored: (Option<String>, Option<String>) = ctx.pool.get().unwrap().query_row(
        "SELECT body_text,body_fetched_at FROM emails WHERE id='em1'", [],
        |row| Ok((row.get(0)?, row.get(1)?)),
    ).unwrap();
    assert!(stored.0.is_some());
    assert_eq!(result.email.body_text, stored.0);
    assert!(stored.1.is_some());
    assert!(commands.iter().any(|command| command.starts_with("UID FETCH ")));
    assert!(commands.iter().any(|command| command == "LOGOUT"));
}

#[test]
fn reader_discards_body_when_epoch_changes_during_fetch() {
    reader_epoch_change(false);
}

#[test]
fn reader_discards_missing_body_when_epoch_changes_during_fetch() {
    reader_epoch_change(true);
}

fn reader_epoch_change(missing: bool) {
    let ctx = crate::test_support::ctx(crate::test_support::pool_with_seeded_data());
    ctx.pool.get().unwrap().execute_batch(
        "UPDATE emails SET body_text=NULL,body_html=NULL,body_fetched_at=NULL WHERE id='em1';
         UPDATE folders SET uid_validity=1234 WHERE id='fold1';"
    ).unwrap();
    let pool = ctx.pool.clone();
    let mut mailbox = tls::Mailbox::seeded(2);
    let mut peer = tls::Peer::new(move |tag, command| {
        if command.starts_with("UID FETCH ") {
            // Exact wire barrier: the old-epoch request is already in flight,
            // but its body has not been returned to the production reader.
            pool.get().unwrap().execute(
                "UPDATE folders SET uid_validity=5678 WHERE id='fold1'", [],
            ).unwrap();
            if missing {
                return format!("{tag} OK no matching UID\r\n");
            }
        }
        mailbox.reply(tag, command)
    });
    let result = get_email_with_connector(&ctx, "em1", |_| Ok(peer.connect()));
    let commands = peer.finish();
    assert!(commands.iter().any(|c| c.starts_with("UID FETCH ")), "wire={commands:?}");
    let stored: (Option<String>, Option<String>) = ctx.pool.get().unwrap().query_row(
        "SELECT body_text,body_fetched_at FROM emails WHERE id='em1'", [],
        |row| Ok((row.get(0)?, row.get(1)?)),
    ).unwrap();
    assert_eq!(stored, (None, None), "stale body persisted: wire={commands:?}");
    assert!(result.is_err(), "stale body must not reach caller");
}

#[test]
fn reader_refuses_reused_uid_from_different_server_epoch() {
    let ctx = crate::test_support::ctx(crate::test_support::pool_with_seeded_data());
    ctx.pool.get().unwrap().execute_batch(
        "UPDATE emails SET body_fetched_at=NULL WHERE id='em1';
         UPDATE folders SET uid_validity=1234 WHERE id='fold1';"
    ).unwrap();
    let mut mailbox = tls::Mailbox::seeded(2);
    mailbox.epoch = 5678;
    let mut peer = tls::Peer::new(move |tag, command| mailbox.reply(tag, command));
    let result = get_email_with_connector(&ctx, "em1", |_| Ok(peer.connect()));
    let commands = peer.finish();
    let stored: Option<String> = ctx.pool.get().unwrap().query_row(
        "SELECT body_text FROM emails WHERE id='em1'", [], |r| r.get(0),
    ).unwrap();
    assert!(stored.is_none(), "foreign epoch body persisted: {stored:?}; wire={commands:?}");
    assert!(result.is_err(), "foreign epoch body must not reach caller");
    assert!(!commands.iter().any(|c| c.starts_with("UID FETCH ")), "wire={commands:?}");
}
