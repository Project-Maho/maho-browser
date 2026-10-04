//! Loopback TLS wire tests: real IMAP parser/session, no provider traffic.
use super::*;
#[path = "imap_review_fixtures/tls.rs"]
mod tls;

fn fixture(run: impl FnOnce(&mut ImapClient)) -> Vec<String> {
    let mut mailbox = tls::Mailbox::seeded(1001);
    let mut peer = tls::Peer::new(move |tag, command| mailbox.reply(tag, command));
    let mut client = peer.connect();
    run(&mut client);
    client.logout().unwrap();
    peer.finish()
}

#[test]
fn passive_body_uses_peek() {
    let commands = fixture(|client| { assert!(client.fetch_body_by_uid("INBOX", 7).unwrap().is_some()); });
    assert!(commands.iter().any(|c| c == "UID FETCH 7 BODY.PEEK[]"), "{commands:?}");
}

#[test]
fn passive_mdn_uses_peek() {
    let commands = fixture(|client| { assert!(client.fetch_body_with_mdn_by_uid("INBOX", 7).unwrap().is_some()); });
    assert!(commands.iter().any(|c| c == "UID FETCH 7 BODY.PEEK[]"), "{commands:?}");
}

#[test]
fn passive_raw_uses_peek() {
    let commands = fixture(|client| { assert!(!client.fetch_email_body("INBOX", 7).unwrap().is_empty()); });
    assert!(commands.iter().any(|c| c == "UID FETCH 7 BODY.PEEK[]"), "{commands:?}");
}

#[test]
fn passive_attachment_uses_peek() {
    // Missing BODYSTRUCTURE yields NotFound, but the actual request is still observable.
    let commands = fixture(|client| { assert!(client.fetch_attachment("INBOX", 7, "2").is_err()); });
    assert!(commands.iter().any(|c| c == "UID FETCH 7 (BODYSTRUCTURE BODY.PEEK[2])"), "{commands:?}");
}

#[test]
fn move_is_atomic_uid_move_without_global_expunge() {
    let commands = fixture(|client| { client.move_email("INBOX", 7, "Trash").unwrap(); });
    assert!(commands.iter().any(|c| c.starts_with("UID MOVE 7 ")), "{commands:?}");
    assert!(!commands.iter().any(|c| c == "EXPUNGE" || c.starts_with("UID COPY")), "{commands:?}");
}

#[test]
fn move_without_capability_has_no_destructive_fallback() {
    // Given a real TLS session whose server does not advertise MOVE.
    let mut mailbox = tls::Mailbox::seeded(20);
    let mut peer = tls::Peer::new(move |tag, command| {
        if command == "CAPABILITY" {
            format!("* CAPABILITY IMAP4rev1\r\n{tag} OK complete\r\n")
        } else {
            mailbox.reply(tag, command)
        }
    });
    let mut client = peer.connect();

    // When moving one message, without any destructive fallback authorization.
    let result = client.move_email("INBOX", 7, "Trash");
    client.logout().unwrap();
    let commands = peer.finish();

    // Then the operation fails before any mutation reaches the wire.
    assert!(result.is_err(), "unsupported move accepted: {commands:?}");
    assert!(!commands.iter().any(|command| {
        command.starts_with("UID COPY") || command.starts_with("UID STORE")
            || command.starts_with("UID MOVE") || command.contains("EXPUNGE")
    }), "{commands:?}");
}

#[test]
fn move_rejection_has_no_destructive_fallback() {
    // Given a MOVE-capable server that rejects this specific message operation.
    let mut mailbox = tls::Mailbox::seeded(20);
    let mut peer = tls::Peer::new(move |tag, command| {
        if command.starts_with("UID MOVE ") {
            format!("{tag} NO move denied\r\n")
        } else {
            mailbox.reply(tag, command)
        }
    });
    let mut client = peer.connect();

    // When the server rejects the requested move.
    let result = client.move_email("INBOX", 7, "Trash");
    client.logout().unwrap();
    let commands = peer.finish();

    // Then the rejection is returned without COPY, STORE or EXPUNGE fallback.
    assert!(result.is_err(), "rejected move accepted: {commands:?}");
    assert!(commands.iter().any(|command| command.starts_with("UID MOVE 7 ")));
    assert!(!commands.iter().any(|command| {
        command.starts_with("UID COPY") || command.starts_with("UID STORE")
            || command.contains("EXPUNGE")
    }), "{commands:?}");
}

#[test]
fn untargeted_expunge_refuses_to_remove_unrelated_deleted_messages() {
    let mut refused = false;
    let commands = fixture(|client| { refused = client.expunge().is_err(); });
    assert!(refused, "untargeted deletion must fail closed: {commands:?}");
    assert!(!commands.iter().any(|c| c == "EXPUNGE"), "{commands:?}");
}

#[test]
fn bootstrap_batch_is_at_most_500_messages() {
    let commands = fixture(|client| { client.fetch_envelopes_since_uid("INBOX", 0).unwrap(); });
    let request = commands.iter().find(|c| c.starts_with("FETCH ")).unwrap();
    let range = request.split_whitespace().nth(1).unwrap();
    let (start, end) = range.split_once(':').unwrap();
    assert!(end.parse::<u32>().unwrap() - start.parse::<u32>().unwrap() + 1 <= 500, "{commands:?}");
}
