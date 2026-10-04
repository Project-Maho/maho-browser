// Copyright 2026 Maho Browser. All rights reserved.

#![allow(clippy::disallowed_methods)]

use super::{
    extract_code_from_request, extract_state_from_request, prepare_loopback_oauth, CALLBACK_TIMEOUT,
};
use crate::error::MailFfiError;
use crate::oauth::StartOAuthOptions;
use std::collections::HashMap;
use std::time::Duration;

#[test]
fn extract_code_from_request_valid() {
    let request = "GET /callback?code=abc123 HTTP/1.1\r\nHost: localhost";
    let code = extract_code_from_request(request).expect("code extracted");
    assert_eq!(code, "abc123");
}

#[test]
fn extract_code_from_request_percent_decoded() {
    let request = "GET /?code=4%2F0AX4XfWi&state=s HTTP/1.1\r\n";
    let code = extract_code_from_request(request).expect("code extracted");
    assert_eq!(code, "4/0AX4XfWi");
}

#[test]
fn extract_code_from_request_access_denied() {
    let request = "GET /callback?error=access_denied HTTP/1.1\r\n";
    let err = extract_code_from_request(request).expect_err("denial surfaces error");
    assert!(matches!(err, MailFfiError::OAuth(msg) if msg.contains("access_denied")));
}

#[test]
fn extract_code_from_request_no_code() {
    let request = "GET /callback HTTP/1.1\r\n";
    let err = extract_code_from_request(request).expect_err("missing code errors");
    assert!(matches!(err, MailFfiError::OAuth(msg) if msg.contains("No authorization code")));
}

#[test]
fn extract_state_from_request_valid() {
    let request = "GET /callback?code=abc123&state=xyz789 HTTP/1.1\r\nHost: localhost";
    let state = extract_state_from_request(request).expect("state extracted");
    assert_eq!(state, "xyz789");
}

#[test]
fn extract_state_from_request_missing() {
    let request = "GET /callback?code=abc123 HTTP/1.1\r\nHost: localhost";
    let err = extract_state_from_request(request).expect_err("missing state errors");
    assert!(matches!(err, MailFfiError::OAuth(msg) if msg.contains("No state parameter")));
}

#[test]
fn callback_listener_allows_slow_interactive_sign_in() {
    assert!(CALLBACK_TIMEOUT >= Duration::from_secs(30 * 60));
}

#[test]
fn loopback_start_uses_generated_redirect_and_identity_binding() {
    #[cfg(target_os = "linux")]
    if std::env::var(super::LOOPBACK_LISTENER_FD_ENV).is_err() {
        use std::os::fd::IntoRawFd;
        let test_listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        std::env::set_var(
            super::LOOPBACK_LISTENER_FD_ENV,
            test_listener.into_raw_fd().to_string(),
        );
    }

    let (listener, started) = prepare_loopback_oauth(
        "gmail",
        "test-public-client-id",
        StartOAuthOptions {
            login_hint: Some("bound.user+mail@example.com".to_string()),
            expected_google_sub: Some("google-subject-42".to_string()),
            reauthorize_account_id: None,
        },
    )
    .unwrap();

    let parsed = reqwest::Url::parse(&started.auth_url).unwrap();
    let params: HashMap<_, _> = parsed.query_pairs().into_owned().collect();
    let redirect_uri = params.get("redirect_uri").expect("redirect_uri");
    let redirect = reqwest::Url::parse(redirect_uri).unwrap();
    assert_eq!(redirect.scheme(), "http");
    assert_eq!(redirect.host_str(), Some("127.0.0.1"));
    assert_eq!(redirect.port(), Some(listener.local_addr().unwrap().port()));
    assert_eq!(
        params.get("login_hint").map(String::as_str),
        Some("bound.user+mail@example.com")
    );

    let pending = crate::state::pending_pkce()
        .take(&started.state)
        .expect("pending loopback OAuth state");
    assert_eq!(
        pending.expected_google_sub.as_deref(),
        Some("google-subject-42")
    );
    assert!(pending.client_secret.is_empty());
}

#[test]
fn accept_loop_exits_on_cancel() {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let state = "test-cancel-state";

    let entry = crate::state::PkceEntry {
        code_verifier: "verifier".to_string(),
        provider: "gmail".to_string(),
        client_id: "client-id".to_string(),
        client_secret: String::new(),
        redirect_uri: "http://localhost".to_string(),
        expected_google_sub: None,
        reauthorize_account_id: None,
        created_at: std::time::Instant::now(),
    };
    crate::state::pending_pkce().insert(state.to_string(), entry);

    crate::state::cancellation_store().cancel(state.to_string());

    let result = super::accept_and_complete(&listener, state);
    assert!(result.is_err());
    let err = result.unwrap_err();
    assert!(matches!(err, MailFfiError::OAuth(ref msg) if msg.contains("cancelled")));

    // Note: run_callback_listener does the final state & cancellation cleanup,
    // so here we just manually clean up or verify that accept_and_complete does not clean up itself
    // since run_callback_listener is the owner of the listener thread lifecycle.
    crate::state::pending_pkce().take(state);
    crate::state::cancellation_store().remove(state);
}

// The loopback tests replace only account/network completion, leaving the real
// socket reader, state validation, accept loop, and HTTP response intact.
thread_local! {
    static CALLBACK_COMPLETION: std::cell::Cell<bool> = const { std::cell::Cell::new(false) };
    static EXPECTED_CALLBACK_STATE: std::cell::RefCell<String> =
        const { std::cell::RefCell::new(String::new()) };
}

pub(super) fn complete_callback(state: &str, code: &str) -> Option<crate::error::Result<String>> {
    CALLBACK_COMPLETION.with(|enabled| enabled.get().then(|| {
        assert_eq!(state, EXPECTED_CALLBACK_STATE.with(|slot| slot.borrow().clone()).as_str());
        assert_eq!(code, "browser-code");
        Ok("loopback@example.com".to_string())
    }))
}

fn completes_after_unrelated_connection(first_request: Option<&str>) {
    use std::io::{Read, Write};
    use std::net::{Shutdown, TcpListener, TcpStream};
    use std::sync::mpsc;

    // Linux demuxes concurrent flows through a process-global registry keyed by
    // state; each invocation must own a unique key or parallel tests overwrite
    // each other's sender and the channel reports disconnected.
    static STATE_SEQ: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
    let state = format!(
        "loopback-regression-state-{}",
        STATE_SEQ.fetch_add(1, std::sync::atomic::Ordering::Relaxed)
    );
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let address = listener.local_addr().unwrap();
    let (done_tx, done_rx) = mpsc::channel();
    let state_for_worker = state.clone();
    let worker = std::thread::spawn(move || {
        CALLBACK_COMPLETION.with(|enabled| enabled.set(true));
        EXPECTED_CALLBACK_STATE.with(|slot| *slot.borrow_mut() = state_for_worker.clone());
        let result = super::accept_and_complete(&listener, &state_for_worker);
        done_tx.send(result).unwrap();
    });

    let mut stray = TcpStream::connect(address).unwrap();
    stray.set_read_timeout(Some(Duration::from_secs(10))).unwrap();
    if let Some(request) = first_request {
        stray.write_all(request.as_bytes()).unwrap();
    }
    // EOF models a preconnect that sends nothing. Reading the response/EOF is
    // the exact signal that the first connection was processed; no sleeps.
    stray.shutdown(Shutdown::Write).unwrap();
    let mut rejected = String::new();
    stray.read_to_string(&mut rejected).unwrap();
    drop(stray);

    let browser_response = (|| -> std::io::Result<String> {
        let mut browser = TcpStream::connect(address)?;
        browser.set_read_timeout(Some(Duration::from_secs(10)))?;
        browser.write_all(
            format!("GET /?code=browser-code&state={state} HTTP/1.1\r\nHost: localhost\r\n\r\n").as_bytes(),
        )?;
        let mut response = String::new();
        browser.read_to_string(&mut response)?;
        Ok(response)
    })();
    let result = done_rx.recv_timeout(Duration::from_secs(10)).expect("completion signal");
    worker.join().unwrap();
    assert_eq!(result.expect("stray connection must not end sign-in"), "loopback@example.com");
    assert!(rejected.starts_with("HTTP/1.1 400"), "{rejected}");
    assert!(browser_response.unwrap().starts_with("HTTP/1.1 200"));
}

#[test]
fn oauth_loopback_completes_after_empty_preconnect() {
    completes_after_unrelated_connection(None);
}

#[test]
fn oauth_loopback_completes_after_mismatched_state() {
    completes_after_unrelated_connection(Some(
        "GET /?code=forged&state=wrong-state HTTP/1.1\r\nHost: localhost\r\n\r\n",
    ));
}

#[test]
fn oauth_loopback_completes_after_favicon_request() {
    completes_after_unrelated_connection(Some(
        "GET /favicon.ico HTTP/1.1\r\nHost: localhost\r\n\r\n",
    ));
}

#[test]
fn central_dispatch_routes_to_registered_flow() {
    let registry: super::DispatchRegistry<String> = super::DispatchRegistry::new();
    let (tx1, rx1) = std::sync::mpsc::channel();
    let (tx2, rx2) = std::sync::mpsc::channel();

    registry.register("state_a".to_string(), tx1);
    registry.register("state_b".to_string(), tx2);

    // Deliver request meant for state_b
    let res = registry.dispatch("state_b", "payload_for_b".to_string());
    assert!(res.is_ok());

    // Assert state_b owner received it
    assert_eq!(rx2.try_recv().unwrap(), "payload_for_b");
    // Assert state_a did not receive it
    assert!(rx1.try_recv().is_err());

    // Unknown state returns Err with the payload
    let unknown = registry.dispatch("state_unknown", "payload_unknown".to_string());
    assert_eq!(unknown.unwrap_err(), "payload_unknown");

    // Unregister state_a and ensure subsequent dispatch fails
    registry.unregister("state_a");
    let res2 = registry.dispatch("state_a", "payload_for_a".to_string());
    assert_eq!(res2.unwrap_err(), "payload_for_a");
}
