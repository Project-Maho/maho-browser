// Copyright 2026 Maho Browser. All rights reserved.

use super::*;
use std::ffi::{CStr, CString};
use std::sync::mpsc;
use std::time::Duration;

#[derive(Debug)]
struct CapturedAccount {
    ok: bool,
    payload: Vec<u8>,
}

unsafe extern "C" fn capture_account(
    ok: bool,
    payload: *const c_char,
    data: *mut std::ffi::c_void,
) {
    // SAFETY: the accepted FFI call owns this boxed sender until its sole callback.
    let sender = unsafe { Box::from_raw(data.cast::<mpsc::Sender<CapturedAccount>>()) };
    // SAFETY: the FFI callback supplies a NUL-terminated string valid during this call.
    let payload = unsafe { CStr::from_ptr(payload) }.to_bytes().to_vec();
    if sender.send(CapturedAccount { ok, payload }).is_err() {
        // The receiver already failed its bounded wait; never unwind across C.
        return;
    }
}

#[test]
fn get_account_callback_is_object_after_one_json_parse() {
    // Given a persisted account with provider identity and a private password.
    let _guard = crate::test_support::global_ctx_guard();
    let pool = crate::test_support::pool_with_seeded_data();
    pool.get().unwrap().execute(
        "UPDATE accounts SET auth_type='oauth2_outlook', password='ACCOUNT_PRIVATE_SENTINEL' WHERE id='acc1'",
        [],
    ).unwrap();
    crate::state::set_ctx(crate::test_support::ctx_arc(pool)).unwrap();
    let account_id = CString::new("acc1").unwrap();
    let (sender, receiver) = mpsc::channel::<CapturedAccount>();
    let data = Box::into_raw(Box::new(sender));

    // When the actual export completes through the production callback machinery.
    let accepted = MahoMailGetAccount(account_id.as_ptr(), Some(capture_account), data.cast());
    if !accepted {
        // SAFETY: a rejected call never takes callback ownership.
        drop(unsafe { Box::from_raw(data) });
    }
    let response = if accepted {
        Some(receiver.recv_timeout(Duration::from_secs(10)))
    } else {
        None
    };
    crate::state::clear_ctx_for_test();

    // Then one JSON parse yields the provider-bound account, without credentials.
    assert!(accepted);
    let CapturedAccount { ok, payload } = response.unwrap().expect("GetAccount callback");
    let payload = String::from_utf8(payload).expect("GetAccount UTF-8");
    assert!(ok, "{payload}");
    let value: serde_json::Value = serde_json::from_str(&payload).expect("GetAccount JSON");
    assert!(value.is_object(), "GetAccount must serialize exactly once: {value}");
    assert_eq!(value["id"], "acc1");
    assert_eq!(value["auth_type"], "oauth2_outlook");
    assert!(value.get("password").is_none());
    assert!(!payload.contains("ACCOUNT_PRIVATE_SENTINEL"));
}

