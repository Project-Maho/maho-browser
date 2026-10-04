// Copyright 2026 Maho Browser. All rights reserved.
// allow: SIZE_OK — comprehensive test fixture and 5 required regression scenarios for U10 batch mail regressions

//! Regression tests for U10: Mail batch flag grouping, single recount per folder,
//! pool release before network, retry queue on failed store, and outbox generation tracking.

use std::ffi::{c_char, c_void, CStr, CString};
use std::io::{BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{mpsc, Arc, Mutex};
use std::time::Duration;

use serde_json::json;

use super::*;
use crate::test_support::{ctx_arc, global_ctx_guard, pool_with_seeded_data};

unsafe extern "C" fn capture(ok: bool, payload: *const c_char, data: *mut c_void) {
    let sender = unsafe { Box::from_raw(data.cast::<mpsc::Sender<(bool, String)>>()) };
    let text = if payload.is_null() {
        String::new()
    } else {
        unsafe { CStr::from_ptr(payload) }
            .to_string_lossy()
            .into_owned()
    };
    let _ = sender.send((ok, text));
}

fn invoke_batch_mark_read(email_ids: &[&str]) -> (bool, String) {
    let req = json!({ "email_ids": email_ids });
    let req_raw = CString::new(req.to_string()).expect("valid CString");
    let (sender, receiver) = mpsc::channel::<(bool, String)>();
    let data = Box::into_raw(Box::new(sender)).cast::<c_void>();
    let accepted = MahoMailBatchMarkRead(req_raw.as_ptr(), Some(capture), data);
    if !accepted {
        drop(unsafe { Box::from_raw(data.cast::<mpsc::Sender<(bool, String)>>()) });
        return (false, "synchronous rejection".to_string());
    }
    receiver
        .recv_timeout(Duration::from_secs(10))
        .expect("batch callback within timeout")
}

fn invoke_single(invoke: extern "C" fn(*const c_char, MahoMailReadCallback, *mut c_void) -> bool, id: &str) -> (bool, String) {
    let id = CString::new(id).unwrap();
    let (sender, receiver) = mpsc::channel::<(bool, String)>();
    let data = Box::into_raw(Box::new(sender)).cast::<c_void>();
    assert!(invoke(id.as_ptr(), Some(capture), data));
    receiver.recv_timeout(Duration::from_secs(10)).expect("exact mutation callback")
}

#[test]
fn review_flush_dispatches_mail_and_acknowledges_only_success() {
    let _guard = global_ctx_guard();
    crate::runtime::runtime().unwrap();
    let server = LoopbackImapServer::new(None, None);
    let ctx = ctx_arc(pool_with_seeded_data());
    {
        let conn = ctx.pool.get().unwrap();
        conn.execute("UPDATE accounts SET imap_host='localhost',imap_port=?1 WHERE id='acc1'", [server.port]).unwrap();
        conn.execute("UPDATE folders SET uid_validity=1234", []).unwrap();
        maho_core::services::offline_queue::queue_mutation(&conn, "acc1", 1, "INBOX", "mark_read", None).unwrap();
    }
    crate::state::set_ctx(ctx.clone()).unwrap();
    let (ok, payload) = invoke_single(MahoMailFlushPendingMutations, "acc1");
    crate::state::clear_ctx_for_test();
    assert!(ok, "mail intent reached wrong domain handler: {payload}");
    assert_eq!(payload, "1");
    assert_eq!(server.stats.lock().unwrap().store_commands.len(), 1);
    let conn = ctx.pool.get().unwrap();
    assert!(maho_core::services::offline_queue::list_pending_mutations(&conn, "acc1").unwrap().is_empty());
}

#[test]
fn review_calendar_replay_is_not_starved_by_mail_failures() {
    let ctx = ctx_arc(pool_with_seeded_data());
    let calendar_id: String = {
        let conn = ctx.pool.get().unwrap();
        for uid in 1..=50 {
            maho_core::services::offline_queue::queue_mutation(&conn, "acc1", uid, "INBOX", "mark_read", None).unwrap();
        }
        maho_core::services::offline_queue::queue_calendar_mutation(&conn, "acc1", "calendar_rsvp_reply", "calendar-after-mail", Some(r#"{"delivery_state":"uncertain"}"#)).unwrap();
        conn.query_row("SELECT id FROM pending_mutations WHERE calendar_event_id='calendar-after-mail'", [], |r| r.get(0)).unwrap()
    };
    let result = crate::runtime::runtime().unwrap().block_on(crate::ffi::calendar_api::calendar_replay::flush(&ctx, "acc1"));
    let error = result.unwrap_err().to_string();
    assert!(error.contains(&calendar_id), "calendar intent beyond the mail page was never considered: {error}");
}

#[test]
fn review_single_delete_and_replay_reject_changed_epoch() {
    let _guard = global_ctx_guard();
    crate::runtime::runtime().unwrap();
    let server = LoopbackImapServer::new(None, None);
    let ctx = ctx_arc(pool_with_seeded_data());
    {
        let conn = ctx.pool.get().unwrap();
        conn.execute("UPDATE accounts SET imap_host='localhost',imap_port=?1 WHERE id='acc1'", [server.port]).unwrap();
        conn.execute("UPDATE folders SET uid_validity=999", []).unwrap();
    }
    crate::state::set_ctx(ctx.clone()).unwrap();
    let deleted = invoke_single(MahoMailDeleteEmail, "em1");
    assert!(deleted.0, "deletion intent should queue: {}", deleted.1);
    // Even after local sync has adopted the new epoch, the queued identity is old.
    ctx.pool.get().unwrap().execute("UPDATE folders SET uid_validity=1234", []).unwrap();
    let replay = invoke_single(MahoMailFlushPendingMutations, "acc1");
    crate::state::clear_ctx_for_test();
    assert!(!replay.0, "old queued deletion was replayed against a reused UID");
    assert!(server.stats.lock().unwrap().store_commands.is_empty());
    let conn = ctx.pool.get().unwrap();
    assert_eq!(maho_core::services::offline_queue::list_pending_mutations(&conn, "acc1").unwrap().len(), 1);
}

#[test]
fn review_batch_result_is_a_json_object_on_the_wire() {
    let _guard = global_ctx_guard();
    crate::runtime::runtime().unwrap();
    let ctx = ctx_arc(pool_with_seeded_data());
    crate::state::set_ctx(ctx).unwrap();
    let (ok, payload) = invoke_batch_mark_read(&["em1"]);
    crate::state::clear_ctx_for_test();
    assert!(ok, "{payload}");
    let value: serde_json::Value = serde_json::from_str(&payload).unwrap();
    assert_eq!(value["batched"], true, "callback JSON is not a batch result object: {value}");
}

#[test]
fn review_large_batch_respects_sqlite_variable_limit() {
    let _guard = global_ctx_guard();
    let ctx = ctx_arc(pool_with_seeded_data());
    let ids: Vec<String> = (10..610).map(|uid| format!("large-{uid}")).collect();
    {
        let conn = ctx.pool.get().unwrap();
        let tx = conn.unchecked_transaction().unwrap();
        for (n, id) in ids.iter().enumerate() {
            tx.execute("INSERT INTO emails(id,account_id,folder_id,uid,subject,from_address,to_addresses,date) VALUES(?1,'acc1','fold1',?2,'bulk','sender@test','[]','2026-09-14')", rusqlite::params![id, n as i64 + 10]).unwrap();
        }
        tx.commit().unwrap();
        // The helper serves every query from one connection, so the actual
        // SQLite runtime limit the batch will bind against is set on it.
        unsafe { rusqlite::ffi::sqlite3_limit(conn.handle(), rusqlite::ffi::SQLITE_LIMIT_VARIABLE_NUMBER, 500); }
    }
    let result = batch_mark_read_impl(&ctx, ids);
    assert!(result.is_ok(), "valid selection exceeded SQLite bind limit: {result:?}");
    let unread: i64 = ctx.pool.get().unwrap().query_row("SELECT count(*) FROM emails WHERE id LIKE 'large-%' AND is_read=0", [], |r| r.get(0)).unwrap();
    assert_eq!(unread, 0);
}

#[test]
fn review_delete_uses_uid_expunge_and_retains_failed_store() {
    let _guard = global_ctx_guard();
    crate::runtime::runtime().unwrap();
    for fail_store in [false, true] {
        let replies: Arc<dyn Fn(&str) -> Option<String> + Send + Sync> = Arc::new(move |command| {
            (fail_store && command.starts_with("UID STORE ")).then(|| "NO rejected\r\n".into())
        });
        let server = LoopbackImapServer::new(None, Some(replies));
        let ctx = ctx_arc(pool_with_seeded_data());
        {
            let conn = ctx.pool.get().unwrap();
            conn.execute("UPDATE accounts SET imap_host='localhost',imap_port=?1 WHERE id='acc1'", [server.port]).unwrap();
            conn.execute("UPDATE folders SET uid_validity=1234", []).unwrap();
        }
        crate::state::set_ctx(ctx.clone()).unwrap();
        let (ok, payload) = invoke_single(MahoMailDeleteEmail, "em1");
        crate::state::clear_ctx_for_test();
        assert!(ok, "delete should succeed remotely or durably queue: {payload}");
        let conn = ctx.pool.get().unwrap();
        let pending = maho_core::services::offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
        if fail_store {
            assert_eq!(pending.len(), 1, "failed UID STORE lost deletion intent");
        } else {
            assert!(server.stats.lock().unwrap().all_commands.iter().any(|c| c.ends_with("UID EXPUNGE 1")), "successful delete never expunged its explicit UID");
            assert!(pending.is_empty());
        }
    }
}

#[derive(Default)]
struct ServerStats {
    connections: usize,
    logins: Vec<String>,
    select_commands: Vec<String>,
    store_commands: Vec<String>,
    logouts: usize,
    all_commands: Vec<String>,
}

struct GateHook {
    matcher: Box<dyn Fn(&str) -> bool + Send + Sync>,
    signal_tx: mpsc::Sender<()>,
    release_rx: Arc<Mutex<mpsc::Receiver<()>>>,
}

struct LoopbackImapServer {
    port: u16,
    stats: Arc<Mutex<ServerStats>>,
    shutdown: Arc<AtomicBool>,
    handle: Option<std::thread::JoinHandle<()>>,
}

impl LoopbackImapServer {
    fn new(
        gate: Option<GateHook>,
        custom_replies: Option<Arc<dyn Fn(&str) -> Option<String> + Send + Sync>>,
    ) -> Self {
        let _ = rustls::crypto::ring::default_provider().install_default();
        let cert = rustls::pki_types::CertificateDer::from(
            include_bytes!("../../vendor/maho-core/src/imap_review_fixtures/localhost.der").to_vec(),
        );
        let key = rustls::pki_types::PrivatePkcs8KeyDer::from(
            include_bytes!("../../vendor/maho-core/src/imap_review_fixtures/localhost-key.der").to_vec(),
        );
        let server_config = rustls::ServerConfig::builder()
            .with_no_client_auth()
            .with_single_cert(vec![cert.clone()], key.into())
            .expect("valid server TLS configuration");

        let mut roots = rustls::RootCertStore::empty();
        roots.add(cert).expect("add localhost cert");
        maho_core::imap_client::set_test_root_certs_for_fixture(Some(roots));

        let listener = TcpListener::bind("127.0.0.1:0").expect("bind ephemeral port");
        let port = listener.local_addr().expect("local addr").port();
        let stats = Arc::new(Mutex::new(ServerStats::default()));
        let stats_clone = Arc::clone(&stats);
        let shutdown = Arc::new(AtomicBool::new(false));
        let shutdown_clone = Arc::clone(&shutdown);

        let gate = Arc::new(gate);
        let custom_replies = Arc::new(custom_replies);

        let handle = std::thread::spawn(move || {
            let server_config = Arc::new(server_config);
            while !shutdown_clone.load(Ordering::Relaxed) {
                let Ok((socket, _)) = listener.accept() else {
                    break;
                };
                if shutdown_clone.load(Ordering::Relaxed) {
                    break;
                }
                stats_clone.lock().unwrap().connections += 1;

                let server_config = Arc::clone(&server_config);
                let stats = Arc::clone(&stats_clone);
                let gate = Arc::clone(&gate);
                let custom_replies = Arc::clone(&custom_replies);

                let Ok(server_conn) = rustls::ServerConnection::new(server_config) else {
                    continue;
                };
                let tls = rustls::StreamOwned::new(server_conn, socket);
                let mut wire = BufReader::new(tls);

                if wire.get_mut().write_all(b"* OK IMAP4rev1 server ready\r\n").is_err() {
                    continue;
                }
                let _ = wire.get_mut().flush();

                for _ in 0..256 {
                    let mut line = String::new();
                    if wire.read_line(&mut line).unwrap_or(0) == 0 {
                        break;
                    }
                    let trimmed = line.trim_end();
                    if trimmed.is_empty() {
                        continue;
                    }
                    let (tag, command) = match trimmed.split_once(' ') {
                        Some(pair) => pair,
                        None => (trimmed, ""),
                    };
                    let upper = command.to_uppercase();

                    {
                        let mut s = stats.lock().unwrap();
                        s.all_commands.push(trimmed.to_string());
                        if upper.starts_with("LOGIN ") {
                            s.logins.push(command.to_string());
                        } else if upper.starts_with("SELECT ") {
                            s.select_commands.push(command.to_string());
                        } else if upper.starts_with("UID STORE ") {
                            s.store_commands.push(command.to_string());
                        } else if upper == "LOGOUT" {
                            s.logouts += 1;
                        }
                    }

                    if let Some(ref g) = *gate {
                        if (g.matcher)(command) {
                            let _ = g.signal_tx.send(());
                            if let Ok(rx) = g.release_rx.lock() {
                                let _ = rx.recv_timeout(Duration::from_secs(5));
                            }
                        }
                    }

                    let response = if let Some(ref custom_fn) = *custom_replies {
                        if let Some(reply) = (custom_fn)(command) {
                            format!("{tag} {reply}")
                        } else {
                            generate_default_reply(tag, &upper)
                        }
                    } else {
                        generate_default_reply(tag, &upper)
                    };

                    let _ = wire.get_mut().write_all(response.as_bytes());
                    let _ = wire.get_mut().flush();

                    if upper == "LOGOUT" {
                        break;
                    }
                }
            }
        });

        Self {
            port,
            stats,
            shutdown,
            handle: Some(handle),
        }
    }
}

fn generate_default_reply(tag: &str, upper_command: &str) -> String {
    if upper_command.starts_with("SELECT ") {
        format!("* 100 EXISTS\r\n* OK [UIDVALIDITY 1234] epoch\r\n* OK [UIDNEXT 1000] next\r\n{tag} OK [READ-WRITE] complete\r\n")
    } else if upper_command.starts_with("CAPABILITY") {
        format!("* CAPABILITY IMAP4rev1 MOVE UIDPLUS\r\n{tag} OK complete\r\n")
    } else if upper_command.starts_with("UID STORE ") {
        format!("* 1 FETCH (FLAGS (\\Seen))\r\n{tag} OK complete\r\n")
    } else if upper_command == "LOGOUT" {
        format!("* BYE done\r\n{tag} OK complete\r\n")
    } else {
        format!("{tag} OK complete\r\n")
    }
}

impl Drop for LoopbackImapServer {
    fn drop(&mut self) {
        self.shutdown.store(true, Ordering::Relaxed);
        let _ = TcpStream::connect(("127.0.0.1", self.port));
        if let Some(handle) = self.handle.take() {
            let _ = handle.join();
        }
        maho_core::imap_client::set_test_root_certs_for_fixture(None);
    }
}

#[test]
fn review_batch_refresh_is_durable_before_mutation_transaction() {
    let _guard = global_ctx_guard();
    crate::runtime::runtime().unwrap();
    let server = LoopbackImapServer::new(None, None);
    let token_server = TcpListener::bind("127.0.0.1:0").unwrap();
    let url = format!("http://{}", token_server.local_addr().unwrap());
    let token_worker = std::thread::spawn(move || {
        let (socket, _) = token_server.accept().unwrap();
        socket.set_read_timeout(Some(Duration::from_secs(5))).unwrap();
        let mut wire = BufReader::new(socket);
        let mut length = 0;
        loop {
            let mut line = String::new();
            assert!(wire.read_line(&mut line).unwrap() > 0);
            if line == "\r\n" { break; }
            if let Some(n) = line.to_ascii_lowercase().strip_prefix("content-length:") { length = n.trim().parse::<usize>().unwrap(); }
        }
        let mut body = vec![0; length];
        std::io::Read::read_exact(&mut wire, &mut body).unwrap();
        assert!(String::from_utf8(body).unwrap().contains("grant_type=refresh_token"));
        let payload = r#"{"access_token":"new-access","refresh_token":"rotated-refresh","expires_in":3600}"#;
        write!(wire.get_mut(), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", payload.len(), payload).unwrap();
    });
    let ctx = ctx_arc(pool_with_seeded_data());
    let conn = ctx.pool.get().unwrap();
    conn.execute("UPDATE accounts SET imap_host='localhost', imap_port=?1, smtp_host=?2, auth_type='oauth2_gmail', oauth2_client_id='review-loopback', oauth2_refresh_token='old-refresh', oauth2_access_token=NULL, oauth2_expires_at='2000-01-01T00:00:00Z' WHERE id='acc1'", rusqlite::params![server.port, url]).unwrap();
    conn.execute("UPDATE folders SET uid_validity=1234", []).unwrap();
    crate::state::set_ctx(ctx.clone()).unwrap();
    let (ok, payload) = invoke_batch_mark_read(&["em1"]);
    crate::state::clear_ctx_for_test();
    token_worker.join().unwrap();
    assert!(ok, "successful token refresh must not fail the mail action: {payload}");
    assert_eq!(crate::credentials::get_encrypted_credential(&conn, &ctx.credential_key, "acc1", "oauth2_refresh_token").unwrap().as_deref(), Some("rotated-refresh"));
}

#[test]
fn review_epoch_mismatch_never_mutates_reused_uids() {
    let _guard = global_ctx_guard();
    crate::runtime::runtime().unwrap();
    let server = LoopbackImapServer::new(None, None);
    let ctx = ctx_arc(pool_with_seeded_data());
    let conn = ctx.pool.get().unwrap();
    conn.execute("UPDATE accounts SET imap_host='localhost', imap_port=?1 WHERE id='acc1'", [server.port]).unwrap();
    conn.execute("UPDATE folders SET uid_validity=999 WHERE id='fold1'", []).unwrap();
    crate::state::set_ctx(ctx.clone()).unwrap();
    let (ok, payload) = invoke_batch_mark_read(&["em1"]);
    crate::state::clear_ctx_for_test();
    assert!(ok, "offline intent should remain durable: {payload}");
    assert!(server.stats.lock().unwrap().store_commands.is_empty(), "stale UID was sent to UID STORE in a new mailbox epoch");
    let pending = maho_core::services::offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
    assert_eq!(pending.len(), 1);
    let identity: serde_json::Value = serde_json::from_str(pending[0].payload_json.as_deref().unwrap_or("null")).unwrap();
    assert_eq!(identity["uid_validity"], 999, "retry must retain the original epoch");
}

#[test]
fn memory_thread_batch_groups_accounts_and_folders() {
    let _guard = global_ctx_guard();
    crate::runtime::runtime().unwrap();

    let server = LoopbackImapServer::new(None, None);
    let pool = pool_with_seeded_data();
    let ctx = ctx_arc(pool.clone());
    let conn = pool.get().unwrap();

    // Given: 2 accounts across 3 distinct folders and 7 emails.
    // acc1: fold1 (em1, em2, em3), fold2 (em4, em5)
    // acc2: fold3 (em6, em7)
    conn.execute(
        "UPDATE accounts SET imap_host='localhost', imap_port=?1, password='secret_password_123', auth_type='password' WHERE id='acc1'",
        [server.port],
    ).unwrap();
    conn.execute(
        "UPDATE accounts SET imap_host='localhost', imap_port=?1, password='secret_password_123', auth_type='password' WHERE id='acc2'",
        [server.port],
    ).unwrap();
    conn.execute(
        "INSERT INTO folders (id, account_id, name, path, folder_type) VALUES ('fold3', 'acc2', 'INBOX', 'INBOX', 'inbox')",
        [],
    ).unwrap();

    conn.execute(
        "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, to_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, raw_size)
         VALUES ('em3', 'acc1', 'fold1', 3, '<msg3@example.com>', 'Test 3', 'sender@example.com', '[\"user@example.com\"]', '2024-01-15T12:00:00Z', 'Snippet', 0, 0, 0, 0, 100)",
        [],
    ).unwrap();
    conn.execute(
        "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, to_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, raw_size)
         VALUES ('em4', 'acc1', 'fold2', 4, '<msg4@example.com>', 'Test 4', 'sender@example.com', '[\"user@example.com\"]', '2024-01-15T13:00:00Z', 'Snippet', 0, 0, 0, 0, 100)",
        [],
    ).unwrap();
    conn.execute(
        "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, to_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, raw_size)
         VALUES ('em5', 'acc1', 'fold2', 5, '<msg5@example.com>', 'Test 5', 'sender@example.com', '[\"user@example.com\"]', '2024-01-15T14:00:00Z', 'Snippet', 0, 0, 0, 0, 100)",
        [],
    ).unwrap();
    conn.execute(
        "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, to_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, raw_size)
         VALUES ('em6', 'acc2', 'fold3', 6, '<msg6@example.com>', 'Test 6', 'sender@example.com', '[\"user@example.com\"]', '2024-01-15T15:00:00Z', 'Snippet', 0, 0, 0, 0, 100)",
        [],
    ).unwrap();
    conn.execute(
        "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, to_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, raw_size)
         VALUES ('em7', 'acc2', 'fold3', 7, '<msg7@example.com>', 'Test 7', 'sender@example.com', '[\"user@example.com\"]', '2024-01-15T16:00:00Z', 'Snippet', 0, 0, 0, 0, 100)",
        [],
    ).unwrap();

    conn.execute("UPDATE folders SET uid_validity=1234 WHERE uid_validity=0", []).unwrap();
    drop(conn);
    crate::state::set_ctx(ctx).unwrap();

    // When: batch mark read on all 7 emails across accounts and folders
    let (ok, _) = invoke_batch_mark_read(&["em1", "em2", "em3", "em4", "em5", "em6", "em7"]);
    crate::state::clear_ctx_for_test();
    assert!(ok, "batch mark read callback must succeed");

    // Then: IMAP connections must be grouped by account (<= 2), folders selected once (<= 3),
    // and UID STORE commands chunked (<= 3 commands) instead of 7 per-item roundtrips.
    let stats = server.stats.lock().unwrap();
    assert!(
        stats.connections <= 2,
        "must establish at most 1 IMAP session per account, but made {} connections",
        stats.connections
    );
    assert!(
        stats.select_commands.len() <= 3,
        "must select each touched folder at most once, but executed {} selects: {:?}",
        stats.select_commands.len(),
        stats.select_commands
    );
    assert!(
        stats.store_commands.len() <= 3,
        "must chunk UID STORE per folder, but executed {} stores: {:?}",
        stats.store_commands.len(),
        stats.store_commands
    );
}

#[test]
fn memory_thread_batch_recounts_each_folder_once() {
    let _guard = global_ctx_guard();
    crate::runtime::runtime().unwrap();

    let server = LoopbackImapServer::new(None, None);
    let pool = pool_with_seeded_data();
    let ctx = ctx_arc(pool.clone());
    let conn = pool.get().unwrap();

    // Given: fold1 with 3 unread emails, fold2 with 3 unread emails.
    conn.execute(
        "UPDATE accounts SET imap_host='localhost', imap_port=?1, password='secret_password_123', auth_type='password' WHERE id='acc1'",
        [server.port],
    ).unwrap();
    conn.execute("UPDATE emails SET is_read=0 WHERE id IN ('em1', 'em2')", []).unwrap();
    conn.execute(
        "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, to_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, raw_size)
         VALUES ('em3', 'acc1', 'fold1', 3, '<msg3@example.com>', 'Test 3', 'sender@example.com', '[\"user@example.com\"]', '2024-01-15T12:00:00Z', 'Snippet', 0, 0, 0, 0, 100)",
        [],
    ).unwrap();
    conn.execute(
        "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, to_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, raw_size)
         VALUES ('em4', 'acc1', 'fold2', 4, '<msg4@example.com>', 'Test 4', 'sender@example.com', '[\"user@example.com\"]', '2024-01-15T13:00:00Z', 'Snippet', 0, 0, 0, 0, 100)",
        [],
    ).unwrap();
    conn.execute(
        "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, to_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, raw_size)
         VALUES ('em5', 'acc1', 'fold2', 5, '<msg5@example.com>', 'Test 5', 'sender@example.com', '[\"user@example.com\"]', '2024-01-15T14:00:00Z', 'Snippet', 0, 0, 0, 0, 100)",
        [],
    ).unwrap();
    conn.execute(
        "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, to_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, raw_size)
         VALUES ('em6', 'acc1', 'fold2', 6, '<msg6@example.com>', 'Test 6', 'sender@example.com', '[\"user@example.com\"]', '2024-01-15T15:00:00Z', 'Snippet', 0, 0, 0, 0, 100)",
        [],
    ).unwrap();

    conn.execute("UPDATE folders SET unread_count=3 WHERE id='fold1'", []).unwrap();
    conn.execute("UPDATE folders SET unread_count=3 WHERE id='fold2'", []).unwrap();

    // Trigger to log unread_count update queries on folders table
    conn.execute_batch(
        "CREATE TABLE folder_recount_audit (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            folder_id TEXT NOT NULL,
            unread_count INTEGER NOT NULL
        );
        CREATE TRIGGER audit_folder_recount
        AFTER UPDATE OF unread_count ON folders
        BEGIN
            INSERT INTO folder_recount_audit (folder_id, unread_count)
            VALUES (NEW.id, NEW.unread_count);
        END;"
    ).unwrap();

    conn.execute("UPDATE folders SET uid_validity=1234 WHERE uid_validity=0", []).unwrap();
    drop(conn);
    crate::state::set_ctx(ctx).unwrap();

    // When: batch mark read on all 6 emails across fold1 and fold2
    let (ok, _) = invoke_batch_mark_read(&["em1", "em2", "em3", "em4", "em5", "em6"]);
    crate::state::clear_ctx_for_test();
    assert!(ok, "batch mark read callback must succeed");

    // Then: each touched folder must have its unread count updated exactly once for the batch
    let conn = pool.get().unwrap();
    let fold1_recounts: i64 = conn.query_row(
        "SELECT COUNT(*) FROM folder_recount_audit WHERE folder_id = 'fold1'",
        [],
        |r| r.get(0),
    ).unwrap();
    let fold2_recounts: i64 = conn.query_row(
        "SELECT COUNT(*) FROM folder_recount_audit WHERE folder_id = 'fold2'",
        [],
        |r| r.get(0),
    ).unwrap();

    assert_eq!(
        fold1_recounts, 1,
        "fold1 must be recounted exactly once for the batch, but was updated {} times",
        fold1_recounts
    );
    assert_eq!(
        fold2_recounts, 1,
        "fold2 must be recounted exactly once for the batch, but was updated {} times",
        fold2_recounts
    );
}

#[test]
fn memory_thread_batch_releases_pool_before_network() {
    let _guard = global_ctx_guard();
    crate::runtime::runtime().unwrap();

    // Given: loopback IMAP server gated on UID STORE
    let (signal_tx, signal_rx) = mpsc::channel::<()>();
    let (release_tx, release_rx) = mpsc::channel::<()>();
    let release_rx = Arc::new(Mutex::new(release_rx));

    let gate = GateHook {
        matcher: Box::new(|cmd: &str| cmd.to_uppercase().starts_with("UID STORE ")),
        signal_tx,
        release_rx,
    };

    let server = LoopbackImapServer::new(Some(gate), None);
    let pool = pool_with_seeded_data();
    let ctx = ctx_arc(pool.clone());
    {
        let conn = pool.get().unwrap();
        conn.execute(
            "UPDATE accounts SET imap_host='localhost', imap_port=?1, password='secret_password_123', auth_type='password' WHERE id='acc1'",
            [server.port],
        ).unwrap();
        conn.execute("UPDATE emails SET is_read=0 WHERE id='em1'", []).unwrap();
        conn.execute("UPDATE folders SET uid_validity=1234 WHERE uid_validity=0", []).unwrap();
    }

    crate::state::set_ctx(ctx).unwrap();

    // When: trigger batch mark read asynchronously
    let (sender, receiver) = mpsc::channel::<(bool, String)>();
    let data = Box::into_raw(Box::new(sender)).cast::<c_void>();
    let req = CString::new(json!({ "email_ids": ["em1"] }).to_string()).unwrap();
    assert!(MahoMailBatchMarkRead(req.as_ptr(), Some(capture), data));

    // Wait until IMAP server receives UID STORE and holds the response
    signal_rx
        .recv_timeout(Duration::from_secs(5))
        .expect("server must receive UID STORE command");

    // While the network command is in flight, the single shared connection must
    // be free: a batch that holds it across the network would block every other
    // mail operation in the process for the length of the round trip.
    let (checkout_tx, checkout_rx) = mpsc::channel::<()>();
    let checkout_pool = pool.clone();
    let checkout = std::thread::spawn(move || {
        let conn = checkout_pool.get().expect("shared connection");
        let _: i64 = conn
            .query_row("SELECT count(*) FROM emails", [], |row| row.get(0))
            .expect("query on the shared connection");
        let _ = checkout_tx.send(());
    });
    let served_during_network = checkout_rx.recv_timeout(Duration::from_secs(2)).is_ok();

    // Cleanup: release the server gate
    let _ = release_tx.send(());

    let (ok, _) = receiver
        .recv_timeout(Duration::from_secs(5))
        .expect("batch callback after release");
    crate::state::clear_ctx_for_test();
    checkout.join().expect("checkout thread");
    assert!(ok, "batch mark read callback must succeed");

    assert!(
        served_during_network,
        "the shared database connection must be released before network waits"
    );
}

#[test]
fn memory_thread_batch_failed_store_keeps_retry() {
    let _guard = global_ctx_guard();
    crate::runtime::runtime().unwrap();

    // Given: server that rejects UID STORE for acc1 with NO [TEMPFAIL], but succeeds for acc2
    let custom_replies: Arc<dyn Fn(&str) -> Option<String> + Send + Sync> = Arc::new(|cmd: &str| {
        let upper = cmd.to_uppercase();
        if upper.starts_with("UID STORE 1 ")
            || upper.starts_with("UID STORE 1,")
            || upper == "UID STORE 1"
        {
            Some("NO [TEMPFAIL] store failed on server\r\n".to_string())
        } else {
            None
        }
    });

    let server = LoopbackImapServer::new(None, Some(custom_replies));
    let pool = pool_with_seeded_data();
    let ctx = ctx_arc(pool.clone());
    let conn = pool.get().unwrap();

    conn.execute(
        "UPDATE accounts SET imap_host='localhost', imap_port=?1, password='secret_password_123', auth_type='password' WHERE id='acc1'",
        [server.port],
    ).unwrap();
    conn.execute(
        "UPDATE accounts SET imap_host='localhost', imap_port=?1, password='secret_password_123', auth_type='password' WHERE id='acc2'",
        [server.port],
    ).unwrap();
    conn.execute(
        "INSERT INTO folders (id, account_id, name, path, folder_type) VALUES ('fold3', 'acc2', 'INBOX', 'INBOX', 'inbox')",
        [],
    ).unwrap();
    conn.execute("UPDATE emails SET is_read=0, uid=1 WHERE id='em1'", []).unwrap();
    conn.execute(
        "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, to_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, raw_size)
         VALUES ('em6', 'acc2', 'fold3', 6, '<msg6@example.com>', 'Test 6', 'sender@example.com', '[\"user@example.com\"]', '2024-01-15T15:00:00Z', 'Snippet', 0, 0, 0, 0, 100)",
        [],
    ).unwrap();

    conn.execute("UPDATE folders SET uid_validity=1234 WHERE uid_validity=0", []).unwrap();
    drop(conn);
    crate::state::set_ctx(ctx).unwrap();

    // When: batch mark read on em1 (fails STORE) and em6 (succeeds STORE)
    let _ = invoke_batch_mark_read(&["em1", "em6"]);
    crate::state::clear_ctx_for_test();

    let conn = pool.get().unwrap();
    let em1_read: bool = conn
        .query_row("SELECT is_read FROM emails WHERE id='em1'", [], |r| r.get(0))
        .unwrap();
    let em6_read: bool = conn
        .query_row("SELECT is_read FROM emails WHERE id='em6'", [], |r| r.get(0))
        .unwrap();
    assert!(em1_read, "em1 local read state must be applied");
    assert!(em6_read, "em6 local read state must be applied");

    // Then: failed group (acc1) must keep a retry record in pending_mutations,
    // while successful group (acc2) must not leave retry records.
    let acc1_mutations = maho_core::services::offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
    let acc2_mutations = maho_core::services::offline_queue::list_pending_mutations(&conn, "acc2").unwrap();

    assert!(
        !acc1_mutations.is_empty(),
        "failed STORE for acc1 must leave retry record in pending_mutations, but got 0"
    );
    assert_eq!(acc1_mutations[0].mutation_type, "mark_read");
    assert_eq!(acc1_mutations[0].email_uid, Some(1));

    assert!(
        acc2_mutations.is_empty(),
        "successful group acc2 must not leave retry records, but got: {:?}",
        acc2_mutations
    );
}

#[test]
fn memory_thread_batch_newer_op_survives_old_ack() {
    let _guard = global_ctx_guard();
    crate::runtime::runtime().unwrap();

    // Given: loopback IMAP server gated on UID STORE to pause older operation mid-network
    let (signal_tx, signal_rx) = mpsc::channel::<()>();
    let (release_tx, release_rx) = mpsc::channel::<()>();
    let release_rx = Arc::new(Mutex::new(release_rx));

    let gate = GateHook {
        matcher: Box::new(|cmd: &str| cmd.to_uppercase().starts_with("UID STORE ")),
        signal_tx,
        release_rx,
    };

    let server = LoopbackImapServer::new(Some(gate), None);
    let pool = pool_with_seeded_data();
    let ctx = ctx_arc(pool.clone());
    let conn = pool.get().unwrap();

    conn.execute(
        "UPDATE accounts SET imap_host='localhost', imap_port=?1, password='secret_password_123', auth_type='password' WHERE id='acc1'",
        [server.port],
    ).unwrap();
    conn.execute("UPDATE emails SET is_read=0, uid=1 WHERE id='em1'", []).unwrap();
    conn.execute("UPDATE folders SET uid_validity=1234 WHERE uid_validity=0", []).unwrap();
    drop(conn);

    crate::state::set_ctx(ctx).unwrap();

    // When: trigger older batch mark read on em1
    let (sender1, receiver1) = mpsc::channel::<(bool, String)>();
    let data1 = Box::into_raw(Box::new(sender1)).cast::<c_void>();
    let req1 = CString::new(json!({ "email_ids": ["em1"] }).to_string()).unwrap();
    assert!(MahoMailBatchMarkRead(req1.as_ptr(), Some(capture), data1));

    // Wait until older mark_read command is in-flight on the IMAP server
    signal_rx
        .recv_timeout(Duration::from_secs(5))
        .expect("server must receive older UID STORE command");

    // 1. Verify outbox contract: batch operation must persist outbox before network attempts
    let conn = pool.get().unwrap();
    let outbox_before_ack = maho_core::services::offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
    assert!(
        !outbox_before_ack.is_empty(),
        "batch operation must persist outbox mutation before network attempts, but pending_mutations is empty"
    );

    // 2. While older operation is held in-flight, execute newer opposite operation (mark_unread)
    // with its own outbox generation and update local email state to unread
    maho_core::services::email::mark_unread(&conn, "em1").unwrap();
    maho_core::services::offline_queue::queue_mutation(&conn, "acc1", 1, "INBOX", "mark_unread", None).unwrap();
    conn.execute("UPDATE folders SET uid_validity=1234 WHERE uid_validity=0", []).unwrap();
    drop(conn);

    // 3. Release older mark_read operation's remote network response
    let _ = release_tx.send(());

    let (ok1, _) = receiver1
        .recv_timeout(Duration::from_secs(5))
        .expect("older batch callback completes");
    crate::state::clear_ctx_for_test();
    assert!(ok1);

    // Then: remote ack of older mark_read must acknowledge only its matching generation;
    // it must NOT clear the newer opposite outbox operation (mark_unread) or clobber is_read.
    let conn = pool.get().unwrap();
    let is_read: bool = conn
        .query_row("SELECT is_read FROM emails WHERE id='em1'", [], |r| r.get(0))
        .unwrap();
    assert!(
        !is_read,
        "older mark_read ack must not clobber newer mark_unread state"
    );

    let mutations = maho_core::services::offline_queue::list_pending_mutations(&conn, "acc1").unwrap();
    let has_newer_unread = mutations
        .iter()
        .any(|m| m.mutation_type == "mark_unread" && m.email_uid == Some(1));
    assert!(
        has_newer_unread,
        "newer opposite outbox mutation (mark_unread) must survive older ack, but pending mutations are: {:?}",
        mutations
    );
}
