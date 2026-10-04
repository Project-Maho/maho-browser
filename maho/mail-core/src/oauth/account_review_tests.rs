// Copyright 2026 Maho Browser. All rights reserved.

#![allow(clippy::disallowed_methods)]

use super::oauth_tests::{insert_pending, migrated_conn, FakeTransport};
use super::*;

#[test]
fn acc01_outlook_completion_replaces_existing_account_and_preserves_folder() {
    let _guard = crate::test_support::global_ctx_guard();
    let mut conn = migrated_conn();
    let key = [72; 32];
    conn.execute_batch("INSERT INTO accounts (id, email, display_name, auth_type, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
        VALUES ('acc01-existing', 'user@example.invalid', 'Existing', 'oauth2_outlook', 'outlook.office365.com', 993, 'tls', 'smtp.office365.com', 587, 'starttls', 'user@example.invalid');
        INSERT INTO folders (id, account_id, name, path, folder_type) VALUES ('acc01-folder', 'acc01-existing', 'INBOX', 'INBOX', 'inbox');").unwrap();
    state::pending_pkce().insert(
        "acc01-outlook".into(),
        PkceEntry {
            provider: "outlook".into(),
            code_verifier: "verifier".into(),
            client_id: "cid".into(),
            client_secret: String::new(),
            redirect_uri: "http://localhost".into(),
            expected_google_sub: None,
            reauthorize_account_id: Some("acc01-existing".into()),
            created_at: Instant::now(),
        },
    );
    let fake = FakeTransport::new("user@example.invalid", Some("new-refresh"));
    let response = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &fake,
    }
    .complete("acc01-outlook", "code")
    .unwrap();
    assert_eq!(response.id, "acc01-existing");
    assert_eq!(response.auth_type, "oauth2_outlook");
    assert_eq!(
        conn.query_row("SELECT COUNT(*) FROM accounts", [], |r| r.get::<_, i64>(0))
            .unwrap(),
        1
    );
    assert_eq!(
        conn.query_row(
            "SELECT account_id FROM folders WHERE id = 'acc01-folder'",
            [],
            |r| r.get::<_, String>(0)
        )
        .unwrap(),
        response.id
    );
    assert_eq!(
        crate::credentials::get_encrypted_credential(
            &conn,
            &key,
            &response.id,
            "oauth2_refresh_token"
        )
        .unwrap()
        .as_deref(),
        Some("new-refresh")
    );
}

#[test]
fn acc02_accepted_cancel_prevents_completion_and_all_persistence() {
    let _guard = crate::test_support::global_ctx_guard();
    let mut conn = migrated_conn();
    let key = [73; 32];
    let state_token = "acc02-accepted-cancel";
    insert_pending(state_token, "gmail");
    let c_state = std::ffi::CString::new(state_token).unwrap();
    assert!(crate::ffi::onboarding_api::MahoMailOAuthCancel(
        c_state.as_ptr()
    ));
    let fake = FakeTransport::new("cancelled@example.invalid", Some("must-not-persist"));
    let result = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &fake,
    }
    .complete(state_token, "code");
    state::pending_pkce().take(state_token);
    state::cancellation_store().remove(state_token);
    let accounts: i64 = conn
        .query_row("SELECT COUNT(*) FROM accounts", [], |r| r.get(0))
        .unwrap();
    let credentials: i64 = conn
        .query_row("SELECT COUNT(*) FROM encrypted_credentials", [], |r| {
            r.get(0)
        })
        .unwrap();
    assert_eq!(
        (accounts, credentials),
        (0, 0),
        "accepted cancellation committed account data"
    );
    assert!(result.is_err());
    assert_eq!(fake.exchange_calls.get(), 0);
}

// The transport only supplies provider responses and an exact userinfo barrier;
// cancellation, replay rejection and SQLite persistence remain production logic.
struct BarrierTransport {
    entered: std::sync::mpsc::Sender<()>,
    release: std::sync::mpsc::Receiver<()>,
}

impl transport::OAuthTransport for BarrierTransport {
    fn exchange_code(
        &self,
        _request: &TokenExchangeRequest<'_>,
    ) -> Result<transport::TokenExchange> {
        Ok(transport::TokenExchange {
            access_token: "barrier-access".into(),
            refresh_token: Some("barrier-refresh".into()),
            expires_at: None,
        })
    }

    fn fetch_userinfo(&self, _url: &str, _token: &str) -> Result<transport::UserInfo> {
        self.entered.send(()).unwrap();
        self.release
            .recv_timeout(std::time::Duration::from_secs(5))
            .unwrap();
        Ok(transport::UserInfo {
            subject: None,
            email: "barrier@example.invalid".into(),
            name: "Barrier".into(),
        })
    }
}

fn in_flight_completion(state_token: &'static str, cancel: bool) {
    let _guard = crate::test_support::global_ctx_guard();
    insert_pending(state_token, "gmail");
    let (entered_tx, entered_rx) = std::sync::mpsc::channel();
    let (release_tx, release_rx) = std::sync::mpsc::channel();
    let (done_tx, done_rx) = std::sync::mpsc::channel();
    let worker = std::thread::spawn(move || {
        let mut conn = migrated_conn();
        let transport = BarrierTransport {
            entered: entered_tx,
            release: release_rx,
        };
        let result = OAuthCompletion {
            conn: &mut conn,
            credential_key: &[74; 32],
            transport: &transport,
        }
        .complete(state_token, "first-code");
        done_tx.send((conn, result)).unwrap();
    });
    entered_rx
        .recv_timeout(std::time::Duration::from_secs(5))
        .unwrap();
    // Userinfo is now in flight. Replay must not consume or invalidate its owner.
    let mut replay_conn = migrated_conn();
    let replay_transport = FakeTransport::new("replay@example.invalid", Some("replay"));
    let replay = OAuthCompletion {
        conn: &mut replay_conn,
        credential_key: &[74; 32],
        transport: &replay_transport,
    }
    .complete(state_token, "replayed-code");
    let c_state = std::ffi::CString::new(state_token).unwrap();
    let accepted = cancel && crate::ffi::onboarding_api::MahoMailOAuthCancel(c_state.as_ptr());
    let notified = state::cancellation_store().is_cancelled(state_token);
    // Listener cleanup only acknowledges the notification, not the decision.
    if cancel {
        state::cancellation_store().remove(state_token);
    }
    release_tx.send(()).unwrap();
    let (conn, result) = done_rx
        .recv_timeout(std::time::Duration::from_secs(5))
        .unwrap();
    worker.join().unwrap();
    let late_cancel = crate::ffi::onboarding_api::MahoMailOAuthCancel(c_state.as_ptr());
    state::pending_pkce().take(state_token);
    state::cancellation_store().remove(state_token);
    assert!(replay.is_err());
    assert_eq!(replay_transport.exchange_calls.get(), 0);
    assert!(
        !late_cancel,
        "completed/cancelled attempt still accepts cancellation"
    );
    let counts = (
        conn.query_row("SELECT COUNT(*) FROM accounts", [], |r| r.get::<_, i64>(0))
            .unwrap(),
        conn.query_row("SELECT COUNT(*) FROM encrypted_credentials", [], |r| {
            r.get::<_, i64>(0)
        })
        .unwrap(),
    );
    if cancel {
        assert!(
            accepted,
            "in-flight cancellation was rejected before commit"
        );
        assert!(
            notified,
            "accepted cancellation did not notify the loopback listener"
        );
        assert!(result.is_err());
        assert_eq!(counts, (0, 0));
    } else {
        assert!(!notified);
        assert!(result.is_ok());
        assert_eq!(counts, (1, 2));
    }
}

#[test]
fn acc02_in_flight_cancel_prevents_commit_after_userinfo_barrier() {
    in_flight_completion("acc02-in-flight-cancel", true);
}

#[test]
fn acc02_in_flight_replay_preserves_owner_and_completed_cancel_is_too_late() {
    in_flight_completion("acc02-in-flight-replay", false);
}

#[test]
fn acc06_exchange_error_redacts_provider_fields_over_real_http() {
    use std::net::TcpListener;
    use std::time::Duration;
    use tokio::io::{AsyncBufReadExt, AsyncReadExt, AsyncWriteExt, BufReader};

    let runtime = crate::runtime::runtime().unwrap();
    for body in [
        r#"{"error":"invalid_grant","debug":"ACCOUNT_SECRET_SENTINEL"}"#.to_string(),
        r#"{"error":"invalid_grant","error_description":"ACCOUNT_SECRET_SENTINEL"}"#.to_string(),
        format!("ACCOUNT_SECRET_SENTINEL{}", "x".repeat(4096)),
    ] {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let endpoint = format!("http://{}/token", listener.local_addr().unwrap());
        listener.set_nonblocking(true).unwrap();
        let (ready_tx, ready_rx) = std::sync::mpsc::channel();
        let mut worker = runtime.spawn(async move {
            let listener = tokio::net::TcpListener::from_std(listener).unwrap();
            ready_tx.send(()).unwrap();
            // One deadline covers accept plus the entire request/response, including
            // clients that never connect or send incomplete headers.
            tokio::time::timeout(Duration::from_secs(5), async move {
                let (mut stream, _) = listener.accept().await.unwrap();
                let mut reader = BufReader::new(&mut stream);
                let mut length = None;
                loop {
                    let mut line = String::new();
                    assert!(reader.read_line(&mut line).await.unwrap() > 0);
                    if line == "\r\n" {
                        break;
                    }
                    if let Some(value) = line.to_ascii_lowercase().strip_prefix("content-length:") {
                        length = Some(value.trim().parse::<usize>().unwrap());
                    }
                }
                let mut request_body = vec![0; length.unwrap()];
                reader.read_exact(&mut request_body).await.unwrap();
                assert!(String::from_utf8(request_body)
                    .unwrap()
                    .contains("grant_type=authorization_code"));
                let response = format!(
                    "HTTP/1.1 400 Bad Request\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
                    body.len(),
                    body
                );
                stream.write_all(response.as_bytes()).await.unwrap();
            })
            .await
        });
        if let Err(error) = ready_rx.recv_timeout(Duration::from_secs(5)) {
            worker.abort();
            panic!("HTTP fixture readiness failed: {error}");
        }
        let result = ReqwestTransport.exchange_code(&TokenExchangeRequest {
            token_url: &endpoint,
            client_id: "fixture",
            client_secret: "",
            redirect_uri: "http://localhost",
            code: "fixture-code",
            verifier: "fixture-verifier",
        });
        // Bound task completion too; abort on timeout rather than leaving a
        // detached fixture or waiting indefinitely on a thread join.
        let completed = runtime
            .block_on(async { tokio::time::timeout(Duration::from_secs(5), &mut worker).await });
        if completed.is_err() {
            worker.abort();
        }
        completed
            .expect("HTTP fixture task did not finish")
            .expect("HTTP fixture task panicked")
            .expect("HTTP fixture accept/request deadline elapsed");
        let error = match result {
            Err(error) => error.to_string(),
            Ok(_) => panic!("HTTP 400 accepted"),
        };
        assert!(
            !error.contains("ACCOUNT_SECRET_SENTINEL"),
            "provider diagnostic leaked: {error}"
        );
        assert!(
            error.len() <= 300,
            "unbounded provider error: {} bytes",
            error.len()
        );
    }
}
