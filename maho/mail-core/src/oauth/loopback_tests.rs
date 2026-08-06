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

    let state_owned = state.to_string();
    std::thread::spawn(move || {
        std::thread::sleep(Duration::from_millis(50));
        crate::state::cancellation_store().cancel(state_owned);
    });

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
