//! Real lettre + loopback TLS + SQLCipher state, with process-local fixture trust.
use super::*;
use std::io::{BufRead, BufReader, Read, Write};
use std::net::{Shutdown, TcpStream};
use tokio::task::JoinHandle;

const CERT: &[u8] = include_bytes!("../../vendor/maho-core/src/imap_review_fixtures/localhost.der");
const KEY: &[u8] =
    include_bytes!("../../vendor/maho-core/src/imap_review_fixtures/localhost-key.der");
const CHILD_CASE: &str = "MAHO_COMPOSE_SMTP_CASE";

// Do not mutate process-global TLS trust while other native tests may be running.
// Re-enter only this exact test in a bounded child with the existing cert loader's
// SSL_CERT_FILE input. No production transport override or certificate bypass.
fn isolated(case: &str) -> bool {
    if std::env::var(CHILD_CASE).as_deref() == Ok(case) {
        return false;
    }
    let trust = FixtureDir::new();
    let encoded = base64::engine::general_purpose::STANDARD.encode(CERT);
    let mut pem = String::from("-----BEGIN CERTIFICATE-----\n");
    for chunk in encoded.as_bytes().chunks(64) {
        pem.push_str(std::str::from_utf8(chunk).unwrap());
        pem.push('\n');
    }
    pem.push_str("-----END CERTIFICATE-----\n");
    let cert_path = trust.0.join("localhost.pem");
    std::fs::write(&cert_path, pem).unwrap();
    let test_name = format!("ffi::compose_api::compose_review_tests::smtp_wire::{case}");
    let output = crate::runtime::runtime().unwrap().block_on(async {
        let mut command = tokio::process::Command::new(std::env::current_exe().unwrap());
        command
            .args(["--exact", &test_name, "--nocapture", "--test-threads=1"])
            .env(CHILD_CASE, case)
            .env("SSL_CERT_FILE", &cert_path)
            .env_remove("SSL_CERT_DIR")
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .kill_on_drop(true);
        tokio::time::timeout(Duration::from_secs(45), command.output())
            .await
            .expect("SMTP test child deadline")
            .unwrap()
    });
    assert!(
        output.status.success(),
        "isolated SMTP case {case}:\n{}\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
    true
}

#[derive(Clone, Copy)]
enum DataOutcome {
    Accepted,
    Rejected,
    Disconnect,
    AuthTemporary,
    AuthPermanent,
}

#[derive(Debug)]
struct Transcript {
    recipients: Vec<String>,
    data: Vec<u8>,
}

type Wire = BufReader<rustls::StreamOwned<rustls::ServerConnection, TcpStream>>;

fn command(wire: &mut Wire) -> String {
    let mut line = String::new();
    assert!(
        wire.take(8193).read_line(&mut line).unwrap() > 0,
        "client disconnected before expected command"
    );
    assert!(line.len() <= 8192, "unbounded SMTP command");
    line
}

fn reply(wire: &mut Wire, response: &[u8]) {
    wire.get_mut().write_all(response).unwrap();
    wire.get_mut().flush().unwrap();
}

fn serve(socket: TcpStream, outcome: DataOutcome) -> Transcript {
    socket
        .set_read_timeout(Some(Duration::from_secs(5)))
        .unwrap();
    socket
        .set_write_timeout(Some(Duration::from_secs(5)))
        .unwrap();
    let provider = Arc::new(rustls::crypto::ring::default_provider());
    let config = rustls::ServerConfig::builder_with_provider(provider)
        .with_safe_default_protocol_versions()
        .unwrap()
        .with_no_client_auth()
        .with_single_cert(
            vec![rustls::pki_types::CertificateDer::from(CERT.to_vec())],
            rustls::pki_types::PrivatePkcs8KeyDer::from(KEY.to_vec()).into(),
        )
        .unwrap();
    let tls = rustls::StreamOwned::new(
        rustls::ServerConnection::new(Arc::new(config)).unwrap(),
        socket,
    );
    let mut wire = BufReader::new(tls);
    reply(&mut wire, b"220 localhost ESMTP\r\n");
    assert!(command(&mut wire).starts_with("EHLO "));
    reply(&mut wire, b"250-localhost\r\n250 AUTH PLAIN\r\n");
    let auth = command(&mut wire);
    let auth = auth.trim_end().strip_prefix("AUTH PLAIN ").unwrap();
    assert_eq!(
        base64::engine::general_purpose::STANDARD
            .decode(auth)
            .unwrap(),
        b"\0user@example.com\0fixture"
    );
    if matches!(outcome, DataOutcome::AuthTemporary | DataOutcome::AuthPermanent) {
        reply(&mut wire, match outcome {
            DataOutcome::AuthTemporary => b"454 4.7.0 Temporary authentication failure\r\n",
            DataOutcome::AuthPermanent => b"535 5.7.8 Authentication rejected\r\n",
            _ => unreachable!(),
        });
        assert_eq!(command(&mut wire), "QUIT\r\n");
        reply(&mut wire, b"221 2.0.0 Bye\r\n");
        return Transcript { recipients: vec![], data: vec![] };
    }
    reply(&mut wire, b"235 2.7.0 Authenticated\r\n");
    assert_eq!(command(&mut wire), "MAIL FROM:<user@example.com>\r\n");
    reply(&mut wire, b"250 2.1.0 Sender accepted\r\n");
    let mut recipients = Vec::new();
    for _ in 0..3 {
        let rcpt = command(&mut wire);
        recipients.push(
            rcpt.trim_end()
                .strip_prefix("RCPT TO:<")
                .unwrap()
                .strip_suffix('>')
                .unwrap()
                .to_owned(),
        );
        reply(&mut wire, b"250 2.1.5 Recipient accepted\r\n");
    }
    assert_eq!(command(&mut wire), "DATA\r\n");
    reply(&mut wire, b"354 Send message\r\n");
    let mut data = Vec::new();
    let mut ended = false;
    for _ in 0..2048 {
        let line = command(&mut wire);
        if line == ".\r\n" {
            ended = true;
            break;
        }
        data.extend_from_slice(line.strip_prefix('.').unwrap_or(&line).as_bytes());
    }
    assert!(ended, "SMTP DATA must terminate within the fixture bound");
    match outcome {
        DataOutcome::AuthTemporary | DataOutcome::AuthPermanent => unreachable!(),
        DataOutcome::Disconnect => wire.get_ref().sock.shutdown(Shutdown::Both).unwrap(),
        DataOutcome::Accepted | DataOutcome::Rejected => {
            reply(
                &mut wire,
                match outcome {
                    DataOutcome::Accepted => b"250 2.0.0 Accepted\r\n",
                    _ => b"554 5.7.1 Rejected\r\n",
                },
            );
            assert_eq!(command(&mut wire), "QUIT\r\n");
            reply(&mut wire, b"221 2.0.0 Bye\r\n");
        }
    }
    Transcript { recipients, data }
}

// Cancelling the async owner must also unblock its spawn_blocking socket worker.
struct SocketGuard(TcpStream);

impl Drop for SocketGuard {
    fn drop(&mut self) {
        if let Err(error) = self.0.shutdown(Shutdown::Both) {
            if error.kind() != std::io::ErrorKind::NotConnected {
                eprintln!("SMTP fixture shutdown failed: {error}");
            }
        }
    }
}

struct SmtpFixture {
    task: Option<JoinHandle<Transcript>>,
}

impl SmtpFixture {
    fn start(ctx: &crate::state::AppCtx, outcome: DataOutcome) -> Self {
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        configure(
            ctx,
            "localhost",
            listener.local_addr().unwrap().port(),
            "Tls",
        );
        listener.set_nonblocking(true).unwrap();
        let task = crate::runtime::runtime().unwrap().spawn(async move {
            let listener = tokio::net::TcpListener::from_std(listener).unwrap();
            let (socket, _) = tokio::time::timeout(Duration::from_secs(10), listener.accept())
                .await
                .expect("SMTP accept deadline")
                .unwrap();
            let socket = socket.into_std().unwrap();
            socket.set_nonblocking(false).unwrap();
            let _socket_guard = SocketGuard(socket.try_clone().unwrap());
            tokio::time::timeout(
                Duration::from_secs(12),
                tokio::task::spawn_blocking(move || serve(socket, outcome)),
            )
            .await
            .expect("SMTP session deadline")
            .unwrap()
        });
        Self { task: Some(task) }
    }

    fn finish(mut self) -> Transcript {
        let mut task = self.task.take().unwrap();
        crate::runtime::runtime().unwrap().block_on(async {
            match tokio::time::timeout(Duration::from_secs(15), &mut task).await {
                Ok(result) => result.unwrap(),
                Err(error) => {
                    task.abort();
                    panic!("SMTP fixture completion deadline: {error}");
                }
            }
        })
    }
}

impl Drop for SmtpFixture {
    fn drop(&mut self) {
        if let Some(task) = self.task.take() {
            task.abort();
        }
    }
}

fn configure(ctx: &crate::state::AppCtx, host: &str, port: u16, encryption: &str) {
    let conn = ctx.pool.get().unwrap();
    conn.execute(
        "UPDATE accounts SET smtp_host=?1,smtp_port=?2,smtp_encryption=?3,password=NULL WHERE id='acc1'",
        rusqlite::params![host, port, encryption],
    )
    .unwrap();
    crate::credentials::store_encrypted_credential(
        &conn,
        &ctx.credential_key,
        "acc1",
        "password",
        "fixture",
    )
    .unwrap();
}

fn send() -> (bool, Value) {
    let raw = CString::new(request().to_string()).unwrap();
    call(|cb, data| MahoMailSendEmail(raw.as_ptr(), cb, data))
}

fn assert_complete_delivery(transcript: &Transcript) {
    let mut recipients = transcript.recipients.clone();
    recipients.sort();
    assert_eq!(
        recipients,
        ["cc@example.test", "hidden@example.test", "to@example.test"]
    );
    let parsed = mail_parser::MessageParser::default()
        .parse(&transcript.data)
        .unwrap();
    assert_eq!(parsed.subject(), Some("subject"));
    assert!(
        parsed.bcc().is_none(),
        "Bcc belongs in the envelope, not message headers"
    );
    assert_eq!(parsed.body_text(0).as_deref(), Some("text"));
    assert_eq!(parsed.body_html(0).as_deref(), Some("<b>html</b>"));
    assert_eq!(parsed.attachment_count(), 1);
    assert_eq!(parsed.attachment(0).unwrap().contents(), b"hello");
}

#[test]
fn cc08_smtp_preconnect_failure_reports_queued() {
    if isolated("cc08_smtp_preconnect_failure_reports_queued") {
        return;
    }
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    // Reserve an address without listening: connection refusal cannot race port reuse.
    let socket = {
        let _runtime = crate::runtime::runtime().unwrap().enter();
        tokio::net::TcpSocket::new_v4().unwrap()
    };
    socket.bind("127.0.0.1:0".parse().unwrap()).unwrap();
    configure(
        &ctx,
        "127.0.0.1",
        socket.local_addr().unwrap().port(),
        "StartTls",
    );
    let (ok, result) = send();
    let status: String = ctx
        .pool
        .get()
        .unwrap()
        .query_row("SELECT status FROM outbox", [], |r| r.get(0))
        .unwrap();
    assert_eq!(status, "queued");
    assert!(ok, "{result}");
    assert_eq!(result["status"], "queued");
}

#[test]
fn cc08_smtp_accepted_reports_sent() {
    if isolated("cc08_smtp_accepted_reports_sent") {
        return;
    }
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    let server = SmtpFixture::start(&ctx, DataOutcome::Accepted);
    let (ok, result) = send();
    assert_complete_delivery(&server.finish());
    let queued: i64 = ctx
        .pool
        .get()
        .unwrap()
        .query_row(
            "SELECT count(*) FROM outbox WHERE status != 'sent'",
            [],
            |r| r.get(0),
        )
        .unwrap();
    assert_eq!(queued, 0);
    assert!(ok, "{result}");
    assert_eq!(result["status"], "sent");
}

#[test]
fn cc08_smtp_data_rejection_is_not_queued() {
    if isolated("cc08_smtp_data_rejection_is_not_queued") {
        return;
    }
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    let server = SmtpFixture::start(&ctx, DataOutcome::Rejected);
    let (ok, result) = send();
    assert_complete_delivery(&server.finish());
    let count: i64 = ctx
        .pool
        .get()
        .unwrap()
        .query_row("SELECT count(*) FROM outbox", [], |r| r.get(0))
        .unwrap();
    assert!(!ok, "{result}");
    assert_eq!(count, 0);
}

#[test]
fn cc08_smtp_temporary_auth_rejection_reports_queued() {
    if isolated("cc08_smtp_temporary_auth_rejection_reports_queued") { return; }
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    let server = SmtpFixture::start(&ctx, DataOutcome::AuthTemporary);
    let (ok, result) = send();
    let transcript = server.finish();
    assert!(transcript.recipients.is_empty());
    assert!(transcript.data.is_empty());
    let count: i64 = ctx.pool.get().unwrap().query_row(
        "SELECT count(*) FROM outbox WHERE status='queued'", [], |row| row.get(0),
    ).unwrap();
    assert!(ok, "{result}");
    assert_eq!(result["status"], "queued");
    assert_eq!(count, 1);
}

#[test]
fn cc08_smtp_permanent_auth_rejection_does_not_queue() {
    if isolated("cc08_smtp_permanent_auth_rejection_does_not_queue") { return; }
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    let server = SmtpFixture::start(&ctx, DataOutcome::AuthPermanent);
    let (ok, result) = send();
    let transcript = server.finish();
    assert!(transcript.recipients.is_empty());
    assert!(transcript.data.is_empty());
    let count: i64 = ctx.pool.get().unwrap().query_row(
        "SELECT count(*) FROM outbox", [], |row| row.get(0),
    ).unwrap();
    assert!(!ok, "{result}");
    assert_eq!(count, 0);
}

#[test]
fn cc06_smtp_lost_data_ack_is_uncertain_not_queued() {
    if isolated("cc06_smtp_lost_data_ack_is_uncertain_not_queued") {
        return;
    }
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    let server = SmtpFixture::start(&ctx, DataOutcome::Disconnect);
    let (ok, result) = send();
    assert_complete_delivery(&server.finish());
    let conn = ctx.pool.get().unwrap();
    let status: String = conn
        .query_row("SELECT status FROM outbox", [], |r| r.get(0))
        .unwrap();
    assert_eq!(
        status, "uncertain",
        "lost final acknowledgement is not safe to replay"
    );
    let (flush_ok, flushed) = call(|cb, data| MahoMailFlushOutbox(cb, data));
    assert!(flush_ok, "{flushed}");
    assert_eq!(flushed, 0);
    let retries: i64 = conn
        .query_row("SELECT retry_count FROM outbox", [], |r| r.get(0))
        .unwrap();
    assert_eq!(retries, 0);
    assert!(ok, "{result}");
    assert_eq!(result["status"], "uncertain");
}

#[test]
fn cc06_outbox_lost_data_ack_is_not_automatically_retried() {
    if isolated("cc06_outbox_lost_data_ack_is_not_automatically_retried") {
        return;
    }
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    let raw = CString::new(request().to_string()).unwrap();
    let (ok, queued) = call(|cb, data| MahoMailQueueEmail(raw.as_ptr(), cb, data));
    assert!(ok, "{queued}");
    let server = SmtpFixture::start(&ctx, DataOutcome::Disconnect);
    let (ok, flushed) = call(|cb, data| MahoMailFlushOutbox(cb, data));
    assert_complete_delivery(&server.finish());
    assert!(ok, "{flushed}");
    assert_eq!(flushed, 0);
    let conn = ctx.pool.get().unwrap();
    let status: String = conn
        .query_row("SELECT status FROM outbox", [], |r| r.get(0))
        .unwrap();
    assert_eq!(status, "uncertain");
    let attempts: i64 = conn
        .query_row("SELECT retry_count FROM outbox", [], |r| r.get(0))
        .unwrap();
    let (ok, flushed) = call(|cb, data| MahoMailFlushOutbox(cb, data));
    assert!(ok, "{flushed}");
    assert_eq!(flushed, 0);
    let current: (String, i64) = conn
        .query_row("SELECT status,retry_count FROM outbox", [], |r| {
            Ok((r.get(0)?, r.get(1)?))
        })
        .unwrap();
    assert_eq!(current, (status, attempts));
}
