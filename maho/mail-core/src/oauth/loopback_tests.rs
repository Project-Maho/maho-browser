// Copyright 2026 Maho Browser. All rights reserved.

#![allow(clippy::disallowed_methods)]

use std::time::Duration;
use super::{extract_code_from_request, extract_state_from_request};
use crate::error::MailFfiError;

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
fn accept_loop_exits_on_cancel() {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let state = "test-cancel-state";
    
    let entry = crate::state::PkceEntry {
        code_verifier: "verifier".to_string(),
        provider: "gmail".to_string(),
        client_id: "client-id".to_string(),
        client_secret: String::new(),
        redirect_uri: "http://localhost".to_string(),
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
