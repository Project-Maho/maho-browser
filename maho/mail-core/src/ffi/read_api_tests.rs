// Copyright 2026 Maho Browser. All rights reserved.

#![allow(clippy::disallowed_methods)]

use std::ffi::{c_void, CStr, CString};
use std::os::raw::c_char;
use std::sync::atomic::{AtomicPtr, AtomicUsize, Ordering};
use std::sync::{Condvar, Mutex};
use std::time::{Duration, Instant};

use super::*;

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

// 1. null callback -> rejected, never invoked.
#[test]
fn null_callback_is_rejected_without_invocation() {
    let capture = Capture::new();
    assert!(!MahoMailListAccounts(None, capture.as_user_data()));
    assert!(capture.peek().is_none());
    assert_eq!(capture.invocations(), 0);
}

// 2. null required string -> rejected, never invoked.
#[test]
fn null_account_id_is_rejected_without_invocation() {
    let capture = Capture::new();
    assert!(!MahoMailListFolders(
        std::ptr::null(),
        Some(capture_cb),
        capture.as_user_data()
    ));
    assert!(capture.peek().is_none());
    assert_eq!(capture.invocations(), 0);
}

// 2b. malformed search JSON -> rejected at the typed boundary, never invoked.
#[test]
fn malformed_search_json_is_rejected_without_invocation() {
    let capture = Capture::new();
    let bad = CString::new("{ not valid json").unwrap();
    assert!(!MahoMailSearchEmails(
        bad.as_ptr(),
        Some(capture_cb),
        capture.as_user_data()
    ));
    assert!(capture.peek().is_none());
    assert_eq!(capture.invocations(), 0);
}

// 3. not initialized (global ctx unset) -> rejected, never invoked.
#[test]
fn uninitialized_state_is_rejected_without_invocation() {
    let _guard = crate::test_support::global_ctx_guard();
    crate::state::clear_ctx_for_test();
    let capture = Capture::new();
    assert!(!MahoMailListAccounts(
        Some(capture_cb),
        capture.as_user_data()
    ));
    assert!(capture.peek().is_none());
    assert_eq!(capture.invocations(), 0);
}

// Acceptance-stage panic (before dispatch) -> false, callback never invoked.
#[test]
fn acceptance_stage_panic_returns_false_without_callback() {
    let capture = Capture::new();
    let user_data = capture.as_user_data();
    let accepted = accept_read_call(move || {
        let _ = user_data;
        panic!("injected acceptance panic");
    });
    assert!(!accepted);
    assert!(capture.peek().is_none());
    assert_eq!(capture.invocations(), 0);
}

// 4. accepted happy path -> callback fires exactly once with parseable JSON.
#[test]
fn accepted_read_invokes_callback_once_with_json() {
    let rt = crate::runtime::runtime().expect("runtime");
    let capture = Capture::new();
    let user_data = AtomicPtr::new(capture.as_user_data());
    let pool = crate::test_support::pool_with_seeded_data();
    let ctx = crate::test_support::ctx_arc(pool);
    let fut = blocking_json(move || read::list_accounts(&ctx));
    spawn_read_task(rt, capture_cb, user_data, fut);

    let (ok, json) = capture.wait(10_000).expect("callback fired");
    assert!(ok);
    assert_eq!(capture.invocations(), 1);
    let parsed: serde_json::Value = serde_json::from_str(&json).expect("parseable JSON");
    assert!(parsed.is_array());
}

// 5. read/domain error -> error callback fires exactly once with ok=false.
#[test]
fn read_error_invokes_error_callback_once() {
    let rt = crate::runtime::runtime().expect("runtime");
    let capture = Capture::new();
    let user_data = AtomicPtr::new(capture.as_user_data());
    let fut: ReadFuture =
        Box::pin(async move { Err(MailFfiError::Internal("simulated read error".to_string())) });
    spawn_read_task(rt, capture_cb, user_data, fut);

    let (ok, message) = capture.wait(10_000).expect("callback fired");
    assert!(!ok);
    assert_eq!(capture.invocations(), 1);
    assert!(message.contains("simulated read error"));
}

// 6. injected worker panic -> error callback exactly once, no unwind across FFI.
#[test]
fn worker_panic_invokes_error_callback_without_unwind() {
    let rt = crate::runtime::runtime().expect("runtime");
    let capture = Capture::new();
    let user_data = AtomicPtr::new(capture.as_user_data());
    let fut: ReadFuture = Box::pin(async move {
        panic!("injected worker panic");
    });
    spawn_read_task(rt, capture_cb, user_data, fut);

    let (ok, message) = capture.wait(10_000).expect("callback fired");
    assert!(!ok);
    assert_eq!(capture.invocations(), 1);
    assert!(message.contains("panicked"));
}

// Strict-provenance round trip (Miri focus): carry a real pointer through an
// AtomicPtr, recover it in `invoke_read_callback`, and confirm delivery.
#[test]
fn provenance_roundtrip_delivers_json_to_original_user_data() {
    let capture = Capture::new();
    let user_data = AtomicPtr::new(capture.as_user_data());
    invoke_read_callback(capture_cb, user_data, Ok("[]".to_string()));
    let (ok, json) = capture.peek().expect("callback fired synchronously");
    assert!(ok);
    assert_eq!(capture.invocations(), 1);
    assert_eq!(json, "[]");
}

// Interior-NUL payload downgrades to a single ok=false error callback.
#[test]
fn interior_nul_payload_downgrades_to_error_callback() {
    let capture = Capture::new();
    let user_data = AtomicPtr::new(capture.as_user_data());
    invoke_read_callback(capture_cb, user_data, Ok("bad\0json".to_string()));
    let (ok, message) = capture.peek().expect("callback fired synchronously");
    assert!(!ok);
    assert_eq!(capture.invocations(), 1);
    assert!(message.contains("interior NUL"));
}
