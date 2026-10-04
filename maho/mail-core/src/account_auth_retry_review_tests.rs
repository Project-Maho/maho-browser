use std::io::{BufRead, BufReader, Write};
use std::net::TcpListener;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use rusqlite::Connection;

use maho_core::imap_client::ImapClient;

use crate::account::{
    self, connect_imap_with_retry_fn, is_auth_error, resolve_account_auth, ImapAuth,
};
use crate::credentials::store_encrypted_credential;
use crate::error::{MailFfiError, Result};

#[path = "../vendor/maho-core/src/imap_review_fixtures/tls.rs"]
mod tls;

fn setup_test_db() -> (Connection, [u8; 32]) {
    let conn = Connection::open_in_memory().unwrap();
    maho_core::db::migrations::run_migrations(&conn).unwrap();
    let key = [42u8; 32];
    (conn, key)
}

fn seed_oauth2_account(
    conn: &Connection,
    key: &[u8; 32],
    account_id: &str,
    provider: &str,
    smtp_host: &str,
    access_token: &str,
    refresh_token: &str,
) {
    conn.execute_batch(&format!(
        "INSERT INTO accounts (id, email, display_name, auth_type, imap_host,
         imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption,
         username, oauth2_client_id, oauth2_expires_at, created_at, updated_at)
         VALUES ('{account_id}', 'user@example.com', 'User', 'oauth2_{provider}',
         'imap.example.com', 993, 'tls', '{smtp_host}', 587, 'starttls',
         'user@example.com', 'review-loopback',
         '2099-01-01T00:00:00Z', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z');"
    ))
    .unwrap();

    store_encrypted_credential(conn, key, account_id, "oauth2_access_token", access_token).unwrap();
    store_encrypted_credential(conn, key, account_id, "oauth2_refresh_token", refresh_token).unwrap();
}

fn spawn_mock_token_server(
    status_code: &'static str,
    response_body: String,
) -> (String, std::thread::JoinHandle<()>) {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let addr = format!("http://{}", listener.local_addr().unwrap());
    let handle = std::thread::spawn(move || {
        listener.set_nonblocking(true).unwrap();
        let start = std::time::Instant::now();
        let socket = loop {
            match listener.accept() {
                Ok((s, _)) => break Some(s),
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                    if start.elapsed() > Duration::from_millis(600) {
                        break None;
                    }
                    std::thread::sleep(Duration::from_millis(10));
                }
                Err(e) => panic!("accept error: {e}"),
            }
        };
        let Some(socket) = socket else { return; };
        socket
            .set_read_timeout(Some(Duration::from_secs(5)))
            .unwrap();
        let mut wire = BufReader::new(socket);
        let mut length = 0;
        loop {
            let mut line = String::new();
            if wire.read_line(&mut line).unwrap_or(0) == 0 {
                break;
            }
            if line == "\r\n" {
                break;
            }
            if let Some(n) = line.to_ascii_lowercase().strip_prefix("content-length:") {
                length = n.trim().parse::<usize>().unwrap_or(0);
            }
        }
        let mut body = vec![0; length];
        let _ = std::io::Read::read_exact(&mut wire, &mut body);
        let _ = write!(
            wire.get_mut(),
            "HTTP/1.1 {status_code}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
            response_body.len(),
            response_body
        );
    });
    (addr, handle)
}

#[test]
fn oauth2_authentication_failed_triggers_refresh_and_retry_once() {
    let (conn, key) = setup_test_db();
    let token_payload = r#"{"access_token":"freshly-refreshed-token","refresh_token":"valid-refresh","expires_in":3600}"#.to_string();
    let (token_url, token_worker) = spawn_mock_token_server("200 OK", token_payload);

    seed_oauth2_account(
        &conn,
        &key,
        "acc-gmail",
        "gmail",
        &token_url,
        "stale-access-token",
        "valid-refresh",
    );

    let mut resolved = resolve_account_auth(&conn, &key, "acc-gmail").unwrap();
    match &resolved.auth {
        ImapAuth::OAuth2 { access_token } => assert_eq!(access_token, "stale-access-token"),
        _ => panic!("expected OAuth2 auth"),
    }

    let attempts = Arc::new(AtomicUsize::new(0));
    let attempts_clone = Arc::clone(&attempts);

    let peer = Arc::new(Mutex::new(tls::Peer::new(|tag, command| {
        if command.starts_with("LOGIN") || command.starts_with("AUTHENTICATE") {
            format!("{tag} OK [CAPABILITY IMAP4rev1] Logged in\r\n")
        } else if command.starts_with("LOGOUT") {
            format!("* BYE Logout\r\n{tag} OK Logout completed\r\n")
        } else {
            format!("{tag} OK Completed\r\n")
        }
    })));
    let peer_clone = Arc::clone(&peer);

    let result = connect_imap_with_retry_fn(&conn, &key, &mut resolved, move |auth| {
        let count = attempts_clone.fetch_add(1, Ordering::SeqCst);
        match &auth.auth {
            ImapAuth::OAuth2 { access_token } => {
                if count == 0 {
                    assert_eq!(access_token, "stale-access-token");
                    Err(MailFfiError::Core(maho_core::error::AppError::Auth(
                        "[AUTHENTICATIONFAILED] Invalid credentials (Failure)".to_string(),
                    )))
                } else {
                    assert_eq!(access_token, "freshly-refreshed-token");
                    Ok(peer_clone.lock().unwrap().connect())
                }
            }
            _ => panic!("expected OAuth2 auth"),
        }
    });

    token_worker.join().unwrap();

    assert!(
        result.is_ok(),
        "AUTHENTICATIONFAILED on OAuth2 account must force token refresh and retry"
    );
    assert_eq!(
        attempts.load(Ordering::SeqCst),
        2,
        "should have retried exactly once after token refresh"
    );

    match &resolved.auth {
        ImapAuth::OAuth2 { access_token } => {
            assert_eq!(access_token, "freshly-refreshed-token");
        }
        _ => panic!("expected OAuth2 auth"),
    }

    let loaded = account::load_account(&conn, "acc-gmail").unwrap();
    assert!(
        loaded.oauth2_expires_at.as_deref().unwrap() > "2026",
        "oauth2_expires_at must be updated after successful refresh"
    );
}

#[test]
fn oauth2_authentication_failed_does_not_retry_more_than_once_if_retry_still_fails() {
    let (conn, key) = setup_test_db();
    let token_payload = r#"{"access_token":"still-bad-token","refresh_token":"valid-refresh","expires_in":3600}"#.to_string();
    let (token_url, token_worker) = spawn_mock_token_server("200 OK", token_payload);

    seed_oauth2_account(
        &conn,
        &key,
        "acc-failing",
        "gmail",
        &token_url,
        "stale-access-token",
        "valid-refresh",
    );

    let mut resolved = resolve_account_auth(&conn, &key, "acc-failing").unwrap();
    let attempts = Arc::new(AtomicUsize::new(0));
    let attempts_clone = Arc::clone(&attempts);

    let result = connect_imap_with_retry_fn(&conn, &key, &mut resolved, move |_auth| {
        attempts_clone.fetch_add(1, Ordering::SeqCst);
        Err(MailFfiError::Core(maho_core::error::AppError::Auth(
            "[AUTHENTICATIONFAILED] Invalid credentials (Failure)".to_string(),
        )))
    });

    token_worker.join().unwrap();

    let err = match result {
        Err(e) => e,
        Ok(_) => panic!("expected Err"),
    };
    assert!(is_auth_error(&err));
    assert_eq!(
        attempts.load(Ordering::SeqCst),
        2,
        "must not retry more than once when retry still fails"
    );
}

#[test]
fn oauth2_authentication_failed_aborts_when_token_refresh_fails() {
    let (conn, key) = setup_test_db();
    let error_payload = r#"{"error":"invalid_grant","error_description":"Token has been expired or revoked."}"#.to_string();
    let (token_url, token_worker) = spawn_mock_token_server("400 Bad Request", error_payload);

    seed_oauth2_account(
        &conn,
        &key,
        "acc-revoked",
        "gmail",
        &token_url,
        "stale-access-token",
        "revoked-refresh",
    );

    let mut resolved = resolve_account_auth(&conn, &key, "acc-revoked").unwrap();
    let attempts = Arc::new(AtomicUsize::new(0));
    let attempts_clone = Arc::clone(&attempts);

    let result = connect_imap_with_retry_fn(&conn, &key, &mut resolved, move |_auth| {
        attempts_clone.fetch_add(1, Ordering::SeqCst);
        Err(MailFfiError::Core(maho_core::error::AppError::Auth(
            "[AUTHENTICATIONFAILED] Invalid credentials (Failure)".to_string(),
        )))
    });

    token_worker.join().unwrap();

    let err = match result {
        Err(e) => e,
        Ok(_) => panic!("expected Err"),
    };
    assert!(
        matches!(err, MailFfiError::OAuth2Refresh(ref msg) if msg.contains("invalid_grant")),
        "failed refresh must bubble invalid_grant error: {err:?}"
    );
    assert_eq!(
        attempts.load(Ordering::SeqCst),
        1,
        "must not attempt second IMAP connection when token refresh failed"
    );
}

#[test]
fn password_account_authentication_failed_is_not_retried() {
    let (conn, key) = setup_test_db();
    conn.execute_batch(
        "INSERT INTO accounts (id, email, display_name, auth_type, imap_host,
         imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption,
         username, created_at, updated_at)
         VALUES ('acc-password', 'user@example.com', 'User', 'password',
         'imap.example.com', 993, 'tls', 'smtp.example.com', 587, 'starttls',
         'user@example.com', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z');"
    )
    .unwrap();
    store_encrypted_credential(&conn, &key, "acc-password", "password", "secret").unwrap();

    let mut resolved = resolve_account_auth(&conn, &key, "acc-password").unwrap();
    let attempts = Arc::new(AtomicUsize::new(0));
    let attempts_clone = Arc::clone(&attempts);

    let result = connect_imap_with_retry_fn(&conn, &key, &mut resolved, move |_auth| {
        attempts_clone.fetch_add(1, Ordering::SeqCst);
        Err(MailFfiError::Core(maho_core::error::AppError::Auth(
            "[AUTHENTICATIONFAILED] Invalid credentials (Failure)".to_string(),
        )))
    });

    assert!(result.is_err());
    assert_eq!(
        attempts.load(Ordering::SeqCst),
        1,
        "password accounts must never attempt token refresh"
    );
}
