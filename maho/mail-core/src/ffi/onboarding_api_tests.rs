// Copyright 2026 Maho Browser. All rights reserved.

#![allow(clippy::disallowed_methods)]

use std::ffi::{c_void, CStr, CString};
use std::os::raw::c_char;
use std::sync::atomic::{AtomicPtr, AtomicUsize, Ordering};
use std::sync::{Condvar, Mutex};
use std::time::{Duration, Instant};

use maho_core::models::account::{CreateAccountRequest, Encryption};

use super::*;
use crate::error::MailFfiError;
use crate::ffi::read_api::{blocking_json, spawn_read_task, ReadFuture};

struct Capture {
    slot: Mutex<Option<(bool, String)>>,
    count: AtomicUsize,
    cv: Condvar,
}

impl Capture {
    fn new() -> Self {
        Self {
            slot: Mutex::new(None),
            count: AtomicUsize::new(0),
            cv: Condvar::new(),
        }
    }

    fn wait(&self, ms: u64) -> Option<(bool, String)> {
        let deadline = Instant::now() + Duration::from_millis(ms);
        let mut guard = self.slot.lock().unwrap();
        while guard.is_none() {
            let now = Instant::now();
            if now >= deadline {
                break;
            }
            let (next, _) = self.cv.wait_timeout(guard, deadline - now).unwrap();
            guard = next;
        }
        guard.clone()
    }

    fn peek(&self) -> Option<(bool, String)> {
        self.slot.lock().unwrap().clone()
    }

    fn invocations(&self) -> usize {
        self.count.load(Ordering::SeqCst)
    }

    fn as_user_data(&self) -> *mut c_void {
        std::ptr::from_ref(self).cast::<c_void>().cast_mut()
    }
}

unsafe extern "C" fn capture_cb(ok: bool, json: *const c_char, user_data: *mut c_void) {
    // SAFETY: tests always pass a `&Capture` that outlives the callback.
    let capture = unsafe { &*user_data.cast::<Capture>() };
    let payload = if json.is_null() {
        String::new()
    } else {
        // SAFETY: `json` is a Rust-owned NUL-terminated string valid for this call.
        unsafe { CStr::from_ptr(json) }
            .to_string_lossy()
            .into_owned()
    };
    capture.count.fetch_add(1, Ordering::SeqCst);
    *capture.slot.lock().unwrap() = Some((ok, payload));
    capture.cv.notify_all();
}

// AddAccount: null request pointer -> rejected, callback never invoked.
#[test]
fn add_account_null_request_is_rejected_without_invocation() {
    let capture = Capture::new();
    assert!(!MahoMailAddAccount(
        std::ptr::null(),
        Some(capture_cb),
        capture.as_user_data()
    ));
    assert!(capture.peek().is_none());
    assert_eq!(capture.invocations(), 0);
}

// AddAccount: malformed JSON -> rejected at the typed boundary, never invoked.
#[test]
fn add_account_malformed_json_is_rejected_without_invocation() {
    let capture = Capture::new();
    let bad = CString::new("{ not valid json").unwrap();
    assert!(!MahoMailAddAccount(
        bad.as_ptr(),
        Some(capture_cb),
        capture.as_user_data()
    ));
    assert!(capture.peek().is_none());
    assert_eq!(capture.invocations(), 0);
}

// AddAccount: null callback -> rejected, never invoked.
#[test]
fn add_account_null_callback_is_rejected() {
    let capture = Capture::new();
    let request = CString::new(r#"{"email":"a@b.com","display_name":"A","imap_host":"imap.b.com","imap_port":993,"imap_encryption":"Tls","smtp_host":"smtp.b.com","smtp_port":587,"smtp_encryption":"StartTls","username":"a@b.com","password":"pw"}"#).unwrap();
    assert!(!MahoMailAddAccount(
        request.as_ptr(),
        None,
        capture.as_user_data()
    ));
    assert_eq!(capture.invocations(), 0);
}

// DeleteAccount: null id -> rejected, never invoked.
#[test]
fn delete_account_null_id_is_rejected_without_invocation() {
    let capture = Capture::new();
    assert!(!MahoMailDeleteAccount(
        std::ptr::null(),
        Some(capture_cb),
        capture.as_user_data()
    ));
    assert!(capture.peek().is_none());
    assert_eq!(capture.invocations(), 0);
}

// TestConnection: malformed JSON -> rejected, never invoked.
#[test]
fn test_connection_malformed_json_is_rejected_without_invocation() {
    let capture = Capture::new();
    let bad = CString::new("{ not valid json").unwrap();
    assert!(!MahoMailTestConnection(
        bad.as_ptr(),
        Some(capture_cb),
        capture.as_user_data()
    ));
    assert!(capture.peek().is_none());
    assert_eq!(capture.invocations(), 0);
}

// AddAccount happy path: through the shared machinery a create_account future
// delivers exactly one ok=true callback carrying the secret-free response JSON.
#[test]
fn add_account_success_invokes_callback_once_with_response() {
    let rt = crate::runtime::runtime().expect("runtime");
    let capture = Capture::new();
    let user_data = AtomicPtr::new(capture.as_user_data());

    let mut conn = rusqlite::Connection::open_in_memory().expect("conn");
    maho_core::db::migrations::run_migrations(&conn).expect("migrations");
    let key = [21u8; 32];
    let request = CreateAccountRequest {
        email: "new@example.com".to_string(),
        display_name: "New User".to_string(),
        auth_type: Some("password".to_string()),
        imap_host: "imap.example.com".to_string(),
        imap_port: 993,
        imap_encryption: Encryption::Tls,
        smtp_host: "smtp.example.com".to_string(),
        smtp_port: 587,
        smtp_encryption: Encryption::StartTls,
        username: "new@example.com".to_string(),
        password: Some("hunter2".to_string()),
        oauth2_client_id: None,
        oauth2_client_secret: None,
        oauth2_access_token: None,
        oauth2_refresh_token: None,
        oauth2_expires_at: None,
    };
    let fut = blocking_json(move || account::create_account(&mut conn, &key, request));
    spawn_read_task(rt, capture_cb, user_data, fut);

    let (ok, json) = capture.wait(10_000).expect("callback fired");
    assert!(ok);
    assert_eq!(capture.invocations(), 1);
    let parsed: serde_json::Value = serde_json::from_str(&json).expect("parseable JSON");
    assert_eq!(parsed["email"], "new@example.com");
    assert!(
        parsed.get("password").is_none(),
        "response must not carry secrets"
    );
}

// Domain error -> exactly one ok=false callback.
#[test]
fn onboarding_error_invokes_error_callback_once() {
    let rt = crate::runtime::runtime().expect("runtime");
    let capture = Capture::new();
    let user_data = AtomicPtr::new(capture.as_user_data());
    let fut: ReadFuture = Box::pin(async move {
        Err(MailFfiError::InvalidRequest(
            "simulated onboarding error".to_string(),
        ))
    });
    spawn_read_task(rt, capture_cb, user_data, fut);

    let (ok, message) = capture.wait(10_000).expect("callback fired");
    assert!(!ok);
    assert_eq!(capture.invocations(), 1);
    assert!(message.contains("simulated onboarding error"));
}

// Worker panic -> exactly one ok=false callback, no unwind across FFI.
#[test]
fn onboarding_worker_panic_invokes_error_callback_without_unwind() {
    let rt = crate::runtime::runtime().expect("runtime");
    let capture = Capture::new();
    let user_data = AtomicPtr::new(capture.as_user_data());
    let fut: ReadFuture = Box::pin(async move {
        panic!("injected onboarding worker panic");
    });
    spawn_read_task(rt, capture_cb, user_data, fut);

    let (ok, message) = capture.wait(10_000).expect("callback fired");
    assert!(!ok);
    assert_eq!(capture.invocations(), 1);
    assert!(message.contains("panicked"));
}

// OAuthStartUrl: null provider -> rejected, callback never invoked.
#[test]
fn oauth_start_url_null_provider_is_rejected_without_invocation() {
    let capture = Capture::new();
    let cid = CString::new("client-1").unwrap();
    let redirect = CString::new("http://localhost").unwrap();
    assert!(!MahoMailOAuthStartUrl(
        std::ptr::null(),
        cid.as_ptr(),
        redirect.as_ptr(),
        Some(capture_cb),
        capture.as_user_data()
    ));
    assert!(capture.peek().is_none());
    assert_eq!(capture.invocations(), 0);
}

#[test]
fn oauth_start_options_json_whitespace_expected_subject_returns_typed_error() {
    let options: oauth::StartOAuthOptions =
        serde_json::from_str(r#"{"expected_google_sub":"   \t "}"#).unwrap();
    let result =
        oauth::start_oauth_with_options("gmail", "client-1", "http://localhost:9999", options);
    assert!(
        matches!(result, Err(MailFfiError::InvalidRequest(message)) if message.contains("expected_google_sub"))
    );
}

// OAuthComplete: null state -> rejected, never invoked.
#[test]
fn oauth_complete_null_state_is_rejected_without_invocation() {
    let capture = Capture::new();
    let code = CString::new("authcode").unwrap();
    assert!(!MahoMailOAuthComplete(
        std::ptr::null(),
        code.as_ptr(),
        Some(capture_cb),
        capture.as_user_data()
    ));
    assert!(capture.peek().is_none());
    assert_eq!(capture.invocations(), 0);
}

// OAuthComplete: null code -> rejected, never invoked.
#[test]
fn oauth_complete_null_code_is_rejected_without_invocation() {
    let capture = Capture::new();
    let state = CString::new("state-1").unwrap();
    assert!(!MahoMailOAuthComplete(
        state.as_ptr(),
        std::ptr::null(),
        Some(capture_cb),
        capture.as_user_data()
    ));
    assert!(capture.peek().is_none());
    assert_eq!(capture.invocations(), 0);
}

#[test]
fn oauth_complete_options_json_preserves_whitespace_expected_subject_as_supplied() {
    let options: oauth::CompleteOAuthOptions =
        serde_json::from_str(r#"{"expected_google_sub":"  \n "}"#).unwrap();
    assert_eq!(options.expected_google_sub.as_deref(), Some("  \n "));
}

// ReconnectAccount: null id -> rejected, never invoked.
#[test]
fn reconnect_account_null_id_is_rejected_without_invocation() {
    let capture = Capture::new();
    assert!(!MahoMailReconnectAccount(
        std::ptr::null(),
        Some(capture_cb),
        capture.as_user_data()
    ));
    assert!(capture.peek().is_none());
    assert_eq!(capture.invocations(), 0);
}

// OAuthStartUrl happy path: through the shared machinery the start_oauth future
// delivers exactly one ok=true callback carrying {state, auth_url}.
#[test]
fn oauth_start_url_success_invokes_callback_once_with_url() {
    let rt = crate::runtime::runtime().expect("runtime");
    let capture = Capture::new();
    let user_data = AtomicPtr::new(capture.as_user_data());
    let fut = blocking_json(move || {
        crate::oauth::start_oauth("gmail", "client-xyz", "http://localhost:9999")
    });
    spawn_read_task(rt, capture_cb, user_data, fut);

    let (ok, json) = capture.wait(10_000).expect("callback fired");
    assert!(ok);
    assert_eq!(capture.invocations(), 1);
    let parsed: serde_json::Value = serde_json::from_str(&json).expect("parseable JSON");
    assert!(parsed["state"].as_str().is_some_and(|s| !s.is_empty()));
    assert!(parsed["auth_url"]
        .as_str()
        .is_some_and(|u| u.contains("client_id=") && u.contains("code_challenge_method=S256")));
}

#[test]
fn import_migration_archive_null_json_is_rejected_without_invocation() {
    let capture = Capture::new();
    assert!(!MahoMailImportMigrationArchive(
        std::ptr::null(),
        Some(capture_cb),
        capture.as_user_data()
    ));
    assert!(capture.peek().is_none());
    assert_eq!(capture.invocations(), 0);
}

#[test]
fn oauth_cancel_ffi_test() {
    let state = "test-cancel-state-ffi";

    let c_state_unknown = std::ffi::CString::new(state).unwrap();
    assert!(!MahoMailOAuthCancel(c_state_unknown.as_ptr()));

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

    let c_state_pending = std::ffi::CString::new(state).unwrap();
    assert!(MahoMailOAuthCancel(c_state_pending.as_ptr()));
    assert!(crate::state::cancellation_store().is_cancelled(state));

    crate::state::pending_pkce().take(state);
    crate::state::cancellation_store().remove(state);
}
