//! Calendar replay contract through real SMTP/TLS and migration-backed SQLCipher.
use super::*;
use base64::Engine as _;
use std::ffi::{CStr, CString};
use std::io::{BufRead, BufReader, Read, Write};
use std::net::{Shutdown, TcpStream};
use std::sync::{mpsc, Arc};
use std::time::Duration;

const CERT: &[u8] = include_bytes!("../../vendor/maho-core/src/imap_review_fixtures/localhost.der");
const KEY: &[u8] = include_bytes!("../../vendor/maho-core/src/imap_review_fixtures/localhost-key.der");
const CHILD: &str = "MAHO_CALENDAR_REPLAY_CASE";

fn isolated(case: &str) -> bool {
    if std::env::var(CHILD).as_deref() == Ok(case) { return false; }
    let path = std::env::temp_dir().join(format!("calendar-replay-{}.pem", uuid::Uuid::new_v4()));
    let encoded = base64::engine::general_purpose::STANDARD.encode(CERT);
    let mut pem = String::from("-----BEGIN CERTIFICATE-----\n");
    for chunk in encoded.as_bytes().chunks(64) {
        pem.push_str(std::str::from_utf8(chunk).unwrap());
        pem.push('\n');
    }
    pem.push_str("-----END CERTIFICATE-----\n");
    std::fs::write(&path, pem).unwrap();
    let output = crate::runtime::runtime().unwrap().block_on(async {
        let mut command = tokio::process::Command::new(std::env::current_exe().unwrap());
        command.args(["--exact", &format!("ffi::calendar_api::calendar_replay_tests::{case}"), "--nocapture", "--test-threads=1"])
            .env(CHILD, case).env("SSL_CERT_FILE", &path).env_remove("SSL_CERT_DIR")
            .stdout(std::process::Stdio::piped()).stderr(std::process::Stdio::piped()).kill_on_drop(true);
        tokio::time::timeout(Duration::from_secs(40), command.output()).await.expect("child deadline").unwrap()
    });
    std::fs::remove_file(path).unwrap();
    assert!(output.status.success(), "{}\n{}", String::from_utf8_lossy(&output.stdout), String::from_utf8_lossy(&output.stderr));
    true
}

struct Fixture { ctx: Arc<AppCtx>, _guard: std::sync::MutexGuard<'static, ()> }
impl Fixture {
    fn new() -> Self {
        let guard = crate::test_support::global_ctx_guard();
        let ctx = crate::test_support::ctx_arc(crate::test_support::pool_with_seeded_data());
        crate::state::set_ctx(ctx.clone()).unwrap();
        Self { ctx, _guard: guard }
    }
    fn queue(&self, kind: &str, event: &str) -> maho_core::services::offline_queue::PendingMutation {
        let db = self.ctx.pool.get().unwrap();
        let payload = serde_json::json!({"attendee_email":"user@example.com", "organizer_email":"organizer@example.invalid", "subject":"Accepted: Replay", "ics_text":"BEGIN:VCALENDAR\r\nMETHOD:REPLY\r\nEND:VCALENDAR\r\n"}).to_string();
        maho_core::services::offline_queue::queue_calendar_mutation(&db, "acc1", kind, event, Some(&payload)).unwrap();
        // DbPool is a single Mutex<Connection>; release it before rows() re-locks.
        drop(db);
        self.rows().into_iter().find(|m| m.calendar_event_id.as_deref() == Some(event)).unwrap()
    }
    fn rows(&self) -> Vec<maho_core::services::offline_queue::PendingMutation> {
        maho_core::services::offline_queue::list_pending_mutations(&self.ctx.pool.get().unwrap(), "acc1").unwrap()
    }
    fn flush(&self) -> (bool, String) {
        let (tx, rx) = mpsc::channel::<(bool, String)>();
        let data = Box::into_raw(Box::new(tx));
        let account = CString::new("acc1").unwrap();
        let accepted = crate::ffi::write_api::MahoMailFlushPendingMutations(account.as_ptr(), Some(capture), data.cast());
        if !accepted { drop(unsafe { Box::from_raw(data) }); }
        assert!(accepted);
        rx.recv_timeout(Duration::from_secs(15)).expect("flush callback")
    }
}
impl Drop for Fixture { fn drop(&mut self) { crate::state::clear_ctx_for_test(); } }
unsafe extern "C" fn capture(ok: bool, payload: *const c_char, data: *mut c_void) {
    let tx = unsafe { Box::from_raw(data.cast::<mpsc::Sender<(bool, String)>>()) };
    let text = unsafe { CStr::from_ptr(payload) }.to_string_lossy().into_owned();
    if let Err(error) = tx.send((ok, text)) { eprintln!("callback receiver closed: {error}"); }
}

#[derive(Clone, Copy)]
enum Outcome { Accepted, Rejected, LostAck }
type Wire = BufReader<rustls::StreamOwned<rustls::ServerConnection, TcpStream>>;
fn line(wire: &mut Wire) -> String {
    let mut line = String::new();
    assert!(wire.take(8193).read_line(&mut line).unwrap() > 0);
    assert!(line.len() <= 8192);
    line
}
fn reply(wire: &mut Wire, text: &[u8]) {
    wire.get_mut().write_all(text).unwrap();
    wire.get_mut().flush().unwrap();
}
struct Server {
    task: tokio::task::JoinHandle<Vec<u8>>,
    data_received: Option<tokio::sync::oneshot::Receiver<()>>,
    release: Option<mpsc::Sender<()>>,
}
impl Server {
    fn start(ctx: &AppCtx, outcome: Outcome) -> Self {
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let db = ctx.pool.get().unwrap();
        db.execute("UPDATE accounts SET smtp_host='localhost', smtp_port=?1, smtp_encryption='Tls', password=NULL WHERE id='acc1'", [listener.local_addr().unwrap().port()]).unwrap();
        crate::credentials::store_encrypted_credential(&db, &ctx.credential_key, "acc1", "password", "fixture").unwrap();
        listener.set_nonblocking(true).unwrap();
        let (data_tx, data_rx) = tokio::sync::oneshot::channel();
        let (release_tx, release_rx) = mpsc::channel();
        let task = crate::runtime::runtime().unwrap().spawn(async move {
            let listener = tokio::net::TcpListener::from_std(listener).unwrap();
            let (socket, _) = tokio::time::timeout(Duration::from_secs(10), listener.accept()).await.expect("accept deadline").unwrap();
            // Keep the listener alive until the one permitted delivery finishes.
            let data = tokio::task::spawn_blocking(move || {
                let socket = socket.into_std().unwrap();
                socket.set_nonblocking(false).unwrap();
                socket.set_read_timeout(Some(Duration::from_secs(5))).unwrap();
                socket.set_write_timeout(Some(Duration::from_secs(5))).unwrap();
                let config = rustls::ServerConfig::builder_with_provider(Arc::new(rustls::crypto::ring::default_provider()))
                    .with_safe_default_protocol_versions().unwrap().with_no_client_auth()
                    .with_single_cert(vec![rustls::pki_types::CertificateDer::from(CERT.to_vec())], rustls::pki_types::PrivatePkcs8KeyDer::from(KEY.to_vec()).into()).unwrap();
                let mut wire = BufReader::new(rustls::StreamOwned::new(rustls::ServerConnection::new(Arc::new(config)).unwrap(), socket));
                reply(&mut wire, b"220 localhost ESMTP\r\n");
                assert!(line(&mut wire).starts_with("EHLO "));
                reply(&mut wire, b"250-localhost\r\n250 AUTH PLAIN\r\n");
                assert!(line(&mut wire).starts_with("AUTH PLAIN "));
                reply(&mut wire, b"235 Authenticated\r\n");
                assert_eq!(line(&mut wire), "MAIL FROM:<user@example.com>\r\n");
                reply(&mut wire, b"250 Sender accepted\r\n");
                assert_eq!(line(&mut wire), "RCPT TO:<organizer@example.invalid>\r\n");
                reply(&mut wire, b"250 Recipient accepted\r\n");
                assert_eq!(line(&mut wire), "DATA\r\n");
                reply(&mut wire, b"354 Send message\r\n");
                let mut data = Vec::new();
                loop {
                    let text = line(&mut wire);
                    if text == ".\r\n" { break; }
                    data.extend_from_slice(text.as_bytes());
                    assert!(data.len() < 128 * 1024);
                }
                data_tx.send(()).expect("DATA observer");
                release_rx.recv_timeout(Duration::from_secs(10)).expect("release DATA ack");
                match outcome {
                    Outcome::LostAck => wire.get_ref().sock.shutdown(Shutdown::Both).unwrap(),
                    _ => {
                        reply(&mut wire, match outcome { Outcome::Accepted => b"250 Accepted\r\n", _ => b"554 Rejected\r\n" });
                        assert_eq!(line(&mut wire), "QUIT\r\n");
                        reply(&mut wire, b"221 Bye\r\n");
                    }
                }
                data
            }).await.unwrap();
            drop(listener);
            data
        });
        Self { task, data_received: Some(data_rx), release: Some(release_tx) }
    }
    fn release(&mut self) { self.release.take().unwrap().send(()).unwrap(); }
    fn finish(self) -> Vec<u8> {
        crate::runtime::runtime().unwrap().block_on(async {
            tokio::time::timeout(Duration::from_secs(15), self.task).await.expect("server deadline").unwrap()
        })
    }
}

#[test]
fn replay_tls_success_deletes_exactly_one_row() {
    if isolated("replay_tls_success_deletes_exactly_one_row") { return; }
    let f = Fixture::new();
    let sent = f.queue("calendar_rsvp_reply", "reply");
    // An unsupported row must not be cleared even after a preceding delivery.
    let other = f.queue("calendar_update", "unsupported");
    f.ctx.pool.get().unwrap().execute("UPDATE pending_mutations SET created_at='2000-01-01' WHERE id=?1", [&sent.id]).unwrap();
    let mut server = Server::start(&f.ctx, Outcome::Accepted);
    server.release();
    let result = f.flush();
    assert!(!result.0, "unsupported remainder must be an error: {result:?}");
    assert_eq!(f.rows().iter().map(|m| &m.id).collect::<Vec<_>>(), vec![&other.id]);
    assert!(!server.finish().is_empty());
}

#[test]
fn mail_fix_mixed_delete_and_rsvp_continue_after_error() {
    if isolated("mail_fix_mixed_delete_and_rsvp_continue_after_error") { return; }
    let f = Fixture::new();
    let delete = f.queue("calendar_delete", "deleted");
    f.queue("calendar_rsvp_reply", "reply");
    f.ctx.pool.get().unwrap().execute(
        "UPDATE pending_mutations SET created_at='2000-01-01', payload_json=?1 WHERE id=?2",
        rusqlite::params![serde_json::json!({"calendar_id":"primary", "external_id":"remote"}).to_string(), delete.id],
    ).unwrap();
    let mut server = Server::start(&f.ctx, Outcome::Accepted);
    server.release();
    let result = f.flush();
    assert!(!result.0, "password account cannot deliver a Google deletion");
    assert_eq!(f.rows().len(), 1, "RSVP must still be delivered after the delete error: {result:?}");
    assert!(!result.1.contains("Unsupported calendar replay"), "delete must reach its own delivery handler: {result:?}");
    assert!(!server.finish().is_empty());
}

#[test]
fn mail_fix_unsupported_first_does_not_block_rsvp() {
    if isolated("mail_fix_unsupported_first_does_not_block_rsvp") { return; }
    let f = Fixture::new();
    let unsupported = f.queue("calendar_update", "unsupported");
    f.queue("calendar_rsvp_reply", "reply");
    f.ctx.pool.get().unwrap().execute("UPDATE pending_mutations SET created_at='2000-01-01' WHERE id=?1", [&unsupported.id]).unwrap();
    let mut server = Server::start(&f.ctx, Outcome::Accepted);
    server.release();
    assert!(!f.flush().0);
    assert_eq!(f.rows().len(), 1);
    assert!(!server.finish().is_empty());
}

#[test]
fn replay_tls_success_returns_delivered_count() {
    if isolated("replay_tls_success_returns_delivered_count") { return; }
    let f = Fixture::new();
    f.queue("calendar_rsvp_reply", "reply");
    let mut server = Server::start(&f.ctx, Outcome::Accepted);
    server.release();
    assert_eq!(f.flush(), (true, "1".into()));
    assert!(f.rows().is_empty());
    assert!(!server.finish().is_empty());
}

#[test]
fn replay_tls_failure_retains_row() {
    if isolated("replay_tls_failure_retains_row") { return; }
    let f = Fixture::new();
    let queued = f.queue("calendar_rsvp_reply", "reply");
    let mut server = Server::start(&f.ctx, Outcome::Rejected);
    server.release();
    assert!(!f.flush().0);
    assert_eq!(f.rows()[0].id, queued.id);
    assert!(!server.finish().is_empty());
}

#[test]
fn replay_lost_ack_is_durable_and_never_replayed() {
    if isolated("replay_lost_ack_is_durable_and_never_replayed") { return; }
    let f = Fixture::new();
    let queued = f.queue("calendar_rsvp_reply", "reply");
    let mut server = Server::start(&f.ctx, Outcome::LostAck);
    server.release();
    // Exercise the existing delivery helper directly so the stub cannot mask
    // the uncertainty RED. It must persist its outcome before returning.
    let result = crate::runtime::runtime().unwrap().block_on(replay_calendar_mutation(&f.ctx, &queued));
    assert!(matches!(result, Err(MailFfiError::Core(maho_core::error::AppError::DeliveryUncertain(_)))));
    assert!(!server.finish().is_empty());
    let conn = rusqlite::Connection::open(f.ctx.pool.get().unwrap().path().unwrap()).unwrap();
    conn.pragma_update(None, "key", &f.ctx.sqlcipher_key).unwrap();
    let durable = maho_core::services::offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
    assert_eq!(durable.len(), 1);
    let payload: serde_json::Value = serde_json::from_str(durable[0].payload_json.as_deref().unwrap()).unwrap();
    assert_eq!(payload["delivery_state"], "uncertain");
    // Closed server: a transport attempt would produce Network, not the
    // persisted DeliveryUncertain result required here, even with a stale row.
    let retry = crate::runtime::runtime().unwrap().block_on(replay_calendar_mutation(&f.ctx, &queued));
    assert!(matches!(retry, Err(MailFfiError::Core(maho_core::error::AppError::DeliveryUncertain(_)))));
    assert!(!f.flush().0);
    assert_eq!(f.rows()[0].id, queued.id);
}

#[test]
fn replay_concurrent_calls_do_not_duplicate_delivery() {
    if isolated("replay_concurrent_calls_do_not_duplicate_delivery") { return; }
    let f = Fixture::new();
    let queued = f.queue("calendar_rsvp_reply", "reply");
    let mut server = Server::start(&f.ctx, Outcome::Accepted);
    let received = server.data_received.take().unwrap();
    crate::runtime::runtime().unwrap().block_on(async {
        let first = replay_calendar_mutation(&f.ctx, &queued);
        let second = async {
            tokio::time::timeout(Duration::from_secs(10), received).await.unwrap().unwrap();
            let mut replay = Box::pin(replay_calendar_mutation(&f.ctx, &queued));
            // Poll the second call while first DATA is unacknowledged. This is
            // exact overlap, not a scheduling sleep or timing-luck assertion.
            std::future::poll_fn(|cx| {
                assert!(std::future::Future::poll(replay.as_mut(), cx).is_pending());
                std::task::Poll::Ready(())
            }).await;
            server.release();
            replay.await
        };
        let (a, b) = tokio::time::timeout(Duration::from_secs(15), async { tokio::join!(first, second) }).await.unwrap();
        assert!(a.is_ok(), "{a:?}");
        assert!(b.is_ok(), "already-acknowledged row must not send again: {b:?}");
    });
    assert!(f.rows().is_empty());
    assert!(!server.finish().is_empty());
}

#[test]
fn replay_unsupported_row_is_untouched_and_errors() {
    let f = Fixture::new();
    let queued = f.queue("calendar_update", "unsupported");
    assert!(!f.flush().0);
    let rows = f.rows();
    assert_eq!(rows.len(), 1);
    assert_eq!(serde_json::to_value(&rows[0]).unwrap(), serde_json::to_value(queued).unwrap());
}
