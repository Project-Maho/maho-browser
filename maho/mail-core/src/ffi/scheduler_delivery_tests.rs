use super::*;
use base64::Engine;
use std::io::{BufRead, BufReader, Write};
use std::sync::Arc;
use std::time::Duration;

const NOW: &str = "2026-09-05T12:00:00Z";
const CERT: &[u8] = include_bytes!("../../vendor/maho-core/src/imap_review_fixtures/localhost.der");
const KEY: &[u8] =
    include_bytes!("../../vendor/maho-core/src/imap_review_fixtures/localhost-key.der");

struct Fixture {
    ctx: Option<crate::state::AppCtx>,
    path: String,
}
impl Fixture {
    fn new() -> Self {
        let pool = crate::test_support::pool_with_seeded_data();
        let path = pool.get().unwrap().path().unwrap().to_owned();
        Self {
            ctx: Some(crate::test_support::ctx(pool)),
            path,
        }
    }
    fn ctx(&self) -> &crate::state::AppCtx {
        self.ctx.as_ref().unwrap()
    }
    fn seed(&self, id: &str, time: &str) {
        self.ctx().pool.get().unwrap().execute(
            "INSERT INTO send_later(id,account_id,to_addresses,cc_addresses,bcc_addresses,subject,body_text,body_html,read_receipt,attachments_json,in_reply_to,email_references,scheduled_at)
             VALUES (?1,'acc1','[\"to@example.test\"]','[\"cc@example.test\"]','[\"hidden@example.test\"]','subject','text','<b>html</b>',1,'[{\"filename\":\"note.txt\",\"mime_type\":\"text/plain\",\"data\":\"aGVsbG8=\"}]','<parent@example.test>','<root@example.test>',?2)",
            params![id,time]).unwrap();
    }
    fn state(&self, id: &str) -> (String, i64, Option<String>) {
        self.ctx()
            .pool
            .get()
            .unwrap()
            .query_row(
                "SELECT status,retry_count,last_error FROM send_later WHERE id=?1",
                [id],
                |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?)),
            )
            .unwrap()
    }
    fn tick(&self) {
        crate::runtime::runtime()
            .unwrap()
            .block_on(crate::ffi::organize_api::run_scheduler_once(
                self.ctx(),
                chrono::DateTime::parse_from_rfc3339(NOW)
                    .unwrap()
                    .with_timezone(&chrono::Utc),
            ))
            .unwrap();
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        drop(self.ctx.take());
        for suffix in ["", "-wal", "-shm", "-journal"] {
            if let Err(e) = std::fs::remove_file(format!("{}{suffix}", self.path)) {
                if e.kind() != std::io::ErrorKind::NotFound {
                    eprintln!("fixture cleanup: {e}");
                }
            }
        }
    }
}

fn isolated(name: &str) -> bool {
    if std::env::var("MAHO_SCHEDULER_CASE").as_deref() == Ok(name) {
        return false;
    }
    let path = std::env::temp_dir().join(format!("scheduler-{}.pem", uuid::Uuid::new_v4()));
    let mut pem = String::from("-----BEGIN CERTIFICATE-----\n");
    for chunk in base64::engine::general_purpose::STANDARD
        .encode(CERT)
        .as_bytes()
        .chunks(64)
    {
        pem.push_str(std::str::from_utf8(chunk).unwrap());
        pem.push('\n');
    }
    pem.push_str("-----END CERTIFICATE-----\n");
    std::fs::write(&path, pem).unwrap();
    let output = crate::runtime::runtime().unwrap().block_on(async {
        let mut cmd = tokio::process::Command::new(std::env::current_exe().unwrap());
        cmd.args([
            "--exact",
            &format!("ffi::organize_api::scheduler_delivery::tests::{name}"),
            "--nocapture",
        ])
        .env("MAHO_SCHEDULER_CASE", name)
        .env("SSL_CERT_FILE", &path)
        .env_remove("SSL_CERT_DIR")
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped())
        .kill_on_drop(true);
        tokio::time::timeout(Duration::from_secs(40), cmd.output())
            .await
            .unwrap()
            .unwrap()
    });
    std::fs::remove_file(path).unwrap();
    assert!(
        output.status.success(),
        "{}\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
    true
}

fn smtp_case(name: &str, response: Option<&'static [u8]>, expected: &str) {
    if isolated(name) {
        return;
    }
    let f = Fixture::new();
    f.seed("due", "2026-09-05T13:00:00+02:00");
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let conn = f.ctx().pool.get().unwrap();
    conn.execute("UPDATE accounts SET smtp_host='localhost',smtp_port=?1,smtp_encryption='Tls' WHERE id='acc1'", [listener.local_addr().unwrap().port()]).unwrap();
    crate::credentials::store_encrypted_credential(
        &conn,
        &f.ctx().credential_key,
        "acc1",
        "password",
        "fixture",
    )
    .unwrap();
    listener.set_nonblocking(true).unwrap();
    let rt = crate::runtime::runtime().unwrap();
    // Subscribe before triggering the tick. At DATA, no completion may yet be durable.
    let (at_data, reached_data) = tokio::sync::oneshot::channel();
    let (release, released) = std::sync::mpsc::channel();
    let server = rt.spawn(async move {
        let listener = tokio::net::TcpListener::from_std(listener).unwrap();
        let (socket, _) = tokio::time::timeout(Duration::from_secs(10), listener.accept())
            .await
            .unwrap()
            .unwrap();
        let socket = socket.into_std().unwrap();
        socket.set_nonblocking(false).unwrap();
        tokio::task::spawn_blocking(move || {
            socket
                .set_read_timeout(Some(Duration::from_secs(5)))
                .unwrap();
            socket
                .set_write_timeout(Some(Duration::from_secs(5)))
                .unwrap();
            let config = rustls::ServerConfig::builder_with_provider(Arc::new(
                rustls::crypto::ring::default_provider(),
            ))
            .with_safe_default_protocol_versions()
            .unwrap()
            .with_no_client_auth()
            .with_single_cert(
                vec![rustls::pki_types::CertificateDer::from(CERT.to_vec())],
                rustls::pki_types::PrivatePkcs8KeyDer::from(KEY.to_vec()).into(),
            )
            .unwrap();
            let mut wire = BufReader::new(rustls::StreamOwned::new(
                rustls::ServerConnection::new(Arc::new(config)).unwrap(),
                socket,
            ));
            fn line(w: &mut impl BufRead) -> String {
                let mut s = String::new();
                assert!(w.read_line(&mut s).unwrap() > 0);
                assert!(s.len() < 8193);
                s
            }
            fn reply(
                w: &mut BufReader<
                    rustls::StreamOwned<rustls::ServerConnection, std::net::TcpStream>,
                >,
                s: &[u8],
            ) {
                w.get_mut().write_all(s).unwrap();
                w.get_mut().flush().unwrap();
            }
            reply(&mut wire, b"220 localhost ESMTP\r\n");
            assert!(line(&mut wire).starts_with("EHLO "));
            reply(&mut wire, b"250-localhost\r\n250 AUTH PLAIN\r\n");
            assert!(line(&mut wire).starts_with("AUTH PLAIN "));
            reply(&mut wire, b"235 Authenticated\r\n");
            assert_eq!(line(&mut wire), "MAIL FROM:<user@example.com>\r\n");
            reply(&mut wire, b"250 Sender\r\n");
            let mut recipients = Vec::new();
            for _ in 0..3 {
                recipients.push(line(&mut wire));
                reply(&mut wire, b"250 Recipient\r\n");
            }
            assert_eq!(line(&mut wire), "DATA\r\n");
            reply(&mut wire, b"354 Message\r\n");
            let mut data = String::new();
            let mut ended = false;
            for _ in 0..2048 {
                let s = line(&mut wire);
                if s == ".\r\n" {
                    ended = true;
                    break;
                }
                data.push_str(s.strip_prefix('.').unwrap_or(&s));
            }
            assert!(ended);
            at_data.send(()).unwrap();
            released.recv_timeout(Duration::from_secs(10)).unwrap();
            if let Some(response) = response {
                reply(&mut wire, response);
                assert_eq!(line(&mut wire), "QUIT\r\n");
                reply(&mut wire, b"221 Bye\r\n");
            } else {
                wire.get_ref()
                    .sock
                    .shutdown(std::net::Shutdown::Both)
                    .unwrap();
            }
            (recipients, data)
        })
        .await
        .unwrap()
    });
    let transcript = rt.block_on(async {
        let delivery = crate::ffi::organize_api::run_scheduler_once(
            f.ctx(),
            chrono::DateTime::parse_from_rfc3339(NOW)
                .unwrap()
                .with_timezone(&chrono::Utc),
        );
        let check = async {
            tokio::time::timeout(Duration::from_secs(15), reached_data)
                .await
                .unwrap()
                .unwrap();
            assert_eq!(f.state("due"), ("sending".into(), 1, None));
            assert!(
                claim(&conn, NOW).unwrap().is_none(),
                "live attempt cannot be claimed again"
            );
            release.send(()).unwrap();
        };
        let (result, ()) = tokio::join!(delivery, check);
        result.unwrap();
        tokio::time::timeout(Duration::from_secs(15), server)
            .await
            .unwrap()
            .unwrap()
    });
    let state = f.state("due");
    assert_eq!(state.0, expected);
    assert_eq!(state.1, 1);
    assert_eq!(state.2.is_some(), expected != "sent");
    f.tick();
    assert_eq!(f.state("due"), state);
    let mut recipients = transcript.0;
    recipients.sort();
    assert_eq!(
        recipients,
        [
            "RCPT TO:<cc@example.test>\r\n",
            "RCPT TO:<hidden@example.test>\r\n",
            "RCPT TO:<to@example.test>\r\n"
        ]
    );
    let parsed = mail_parser::MessageParser::default()
        .parse(transcript.1.as_bytes())
        .unwrap();
    assert_eq!(parsed.subject(), Some("subject"));
    assert!(parsed.bcc().is_none());
    assert_eq!(parsed.body_text(0).as_deref(), Some("text"));
    assert_eq!(parsed.body_html(0).as_deref(), Some("<b>html</b>"));
    assert_eq!(parsed.attachment_count(), 1);
    assert_eq!(parsed.attachment(0).unwrap().contents(), b"hello");
    assert!(transcript.1.contains("In-Reply-To: <parent@example.test>"));
    assert!(transcript.1.contains("References: <root@example.test>"));
    assert!(transcript
        .1
        .contains("Disposition-Notification-To: user@example.com"));
}

#[test]
fn scheduler_accepted_complete() {
    smtp_case(
        "scheduler_accepted_complete",
        Some(b"250 Accepted\r\n"),
        "sent",
    );
}
#[test]
fn scheduler_uncertain_no_retry() {
    smtp_case("scheduler_uncertain_no_retry", None, "uncertain");
}
#[test]
fn scheduler_failure_retained() {
    smtp_case(
        "scheduler_failure_retained",
        Some(b"554 Rejected\r\n"),
        "failed",
    );
}

#[test]
fn review_scheduler_expired_oauth_records_failure_without_killing_task() {
    let name = "review_scheduler_expired_oauth_records_failure_without_killing_task";
    if isolated(name) { return; }
    // This isolated process cannot contact a provider; the proxy fails locally.
    std::env::set_var("HTTPS_PROXY", "http://127.0.0.1:0");
    std::env::set_var("NO_PROXY", "");
    let f = Fixture::new();
    f.seed("expired", NOW);
    f.ctx().pool.get().unwrap().execute_batch(
        "UPDATE accounts SET auth_type='oauth2_gmail', oauth2_client_id='fixture',
         oauth2_refresh_token='fixture', oauth2_access_token=NULL,
         oauth2_expires_at='2000-01-01T00:00:00Z' WHERE id='acc1';"
    ).unwrap();
    let rt = crate::runtime::runtime().unwrap();
    let ctx = crate::test_support::ctx_arc(f.ctx().pool.clone());
    let outcome = rt.block_on(async { tokio::spawn(async move { run(&ctx, NOW).await }).await });
    assert!(outcome.is_ok(), "expired OAuth panicked the scheduler task: {outcome:?}");
    outcome.unwrap().unwrap();
    let state = f.state("expired");
    assert_eq!(state.0, "failed");
    assert!(state.2.is_some());
}

#[test]
fn scheduler_atomic_claim() {
    let f = Fixture::new();
    f.seed("due", "2026-09-05T12:00:00Z");
    f.seed("future", "2026-09-05T12:00:00.750Z");
    f.seed("invalid", "bad timestamp");
    let barrier = Arc::new(std::sync::Barrier::new(2));
    let pool = f.ctx().pool.clone();
    let other = barrier.clone();
    let worker = std::thread::spawn(move || {
        let conn = pool.get().unwrap();
        other.wait();
        claim(&conn, NOW).unwrap()
    });
    let conn = f.ctx().pool.get().unwrap();
    barrier.wait();
    let first = claim(&conn, NOW).unwrap();
    let second = worker.join().unwrap();
    assert_eq!(
        usize::from(first.is_some()) + usize::from(second.is_some()),
        1
    );
    assert_eq!(f.state("due"), ("sending".into(), 1, None));
    f.tick();
    assert_eq!(f.state("due"), ("sending".into(), 1, None));
    assert_eq!(f.state("future"), ("pending".into(), 0, None));
    assert_eq!(f.state("invalid"), ("pending".into(), 0, None));
}
