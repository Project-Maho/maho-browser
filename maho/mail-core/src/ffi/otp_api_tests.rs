use super::*;
use std::ffi::{CStr, CString};
use std::sync::mpsc;
use std::time::Duration;

#[derive(Debug)]
struct Captured {
    ok: bool,
    payload: Vec<u8>,
}

unsafe extern "C" fn capture(ok: bool, payload: *const c_char, data: *mut c_void) {
    // The callback owns the sender, including if the receiver times out.
    let sender = unsafe { Box::from_raw(data.cast::<mpsc::Sender<Captured>>()) };
    let payload = unsafe { CStr::from_ptr(payload) }.to_bytes().to_owned();
    if sender.send(Captured { ok, payload }).is_err() {
        // The receiving test has already failed its bounded callback wait.
        // A late callback must release its sender without unwinding across C.
        return;
    }
}

fn extract(query: &str) -> serde_json::Value {
    let _guard = crate::test_support::global_ctx_guard();
    let pool = crate::test_support::pool_with_seeded_data();
    pool.get().unwrap().execute(
        "UPDATE emails SET subject = 'Verification code 123456', body_text = 'Your verification code is 123456', date = ?1 WHERE id = 'em1'",
        [Utc::now().to_rfc3339()],
    ).unwrap();
    crate::state::set_ctx(crate::test_support::ctx_arc(pool)).unwrap();
    let query = CString::new(query).unwrap();
    let (sender, receiver) = mpsc::channel::<Captured>();
    let data = Box::into_raw(Box::new(sender));
    let accepted = MahoMailExtractOtp(
        std::ptr::null(), std::ptr::null(), query.as_ptr(), 600,
        Some(capture), data.cast(),
    );
    if !accepted {
        // Rejected calls never transfer ownership to a callback.
        drop(unsafe { Box::from_raw(data) });
    }
    assert!(accepted);
    let result = receiver.recv_timeout(Duration::from_secs(10));
    crate::state::clear_ctx_for_test();
    let Captured { ok, payload } = result.expect("OTP callback");
    let payload = String::from_utf8(payload).expect("UTF-8 callback payload");
    assert!(ok, "{payload}");
    serde_json::from_str(&payload).expect("one JSON parse")
}

#[test]
fn otp_ffi_empty_callback_is_array() {
    assert_eq!(extract("no-matching-message"), json!([]));
}

#[test]
fn otp_ffi_matching_callback_is_array() {
    assert_eq!(extract("Verification code"), json!([
        {"field": "otp", "name": "one-time-code", "value": "123456"},
        {"field": "code", "name": "one-time-code", "value": "123456"}
    ]));
}
