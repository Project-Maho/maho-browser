//! FFI-boundary coverage for the sync E2EE origin path (F1 fix).
//!
//! `maho_core_configure_sync_encryption` is the production wiring the
//! "start new sync -> Continue" flow calls through the settings/sync WebUI
//! page handlers. The core derivation logic is unit-tested in
//! `maho-core/tests/persistence.rs`; these tests lock the *FFI contract* that
//! the C++ layer depends on:
//!   - a valid recovery phrase configures encryption and reports
//!     `{"success": true, "deviceId": ...}`,
//!   - invalid input reports a structured `{"success": false, "error": ...}`,
//!   - null inputs return null without crossing the boundary.

use std::ffi::{CStr, CString};
use std::os::raw::c_char;

use serde_json::Value;

const TEST_RELAY_URL: &str = "ws://127.0.0.1:9";

/// Parse a `*mut c_char` JSON payload returned by an FFI call and free it.
fn take_json(ptr: *mut c_char) -> Value {
    assert!(!ptr.is_null(), "FFI returned a null JSON pointer");
    let owned = unsafe { CStr::from_ptr(ptr) }
        .to_str()
        .expect("FFI JSON is valid UTF-8")
        .to_owned();
    unsafe { maho_ffi::maho_string_free(ptr) };
    serde_json::from_str(&owned).expect("FFI returned parseable JSON")
}

fn recovery_phrase_from(key_info: &Value) -> String {
    let phrase = key_info["recoveryPhrase"]
        .as_str()
        .expect("generate_sync_key returns recoveryPhrase")
        .to_owned();
    assert!(!phrase.is_empty(), "recovery phrase must not be empty");
    phrase
}

#[test]
fn configure_sync_encryption_with_generated_phrase_succeeds() {
    let core = maho_ffi::maho_core_new();
    assert!(!core.is_null());

    let key_info = take_json(unsafe { maho_ffi::maho_core_generate_sync_key(core) });
    let phrase = recovery_phrase_from(&key_info);
    let url = CString::new(TEST_RELAY_URL).unwrap();
    let c_phrase = CString::new(phrase).unwrap();

    let result = take_json(unsafe {
        maho_ffi::maho_core_configure_sync_encryption(core, url.as_ptr(), c_phrase.as_ptr())
    });

    assert_eq!(
        result["success"],
        Value::Bool(true),
        "origin path must configure encryption: {result}"
    );
    assert!(
        result["deviceId"].is_string(),
        "success response must carry a deviceId: {result}"
    );

    unsafe { maho_ffi::maho_core_free(core) };
}

#[test]
fn configure_sync_encryption_is_deterministic_across_cores() {
    // The origin device and a device joining with the SAME phrase must both
    // configure successfully. This proves the FFI wiring feeds identical
    // derivation input on both sides without needing a live relay.
    let origin = maho_ffi::maho_core_new();
    let key_info = take_json(unsafe { maho_ffi::maho_core_generate_sync_key(origin) });
    let phrase = recovery_phrase_from(&key_info);

    let url = CString::new(TEST_RELAY_URL).unwrap();
    let c_phrase = CString::new(phrase).unwrap();

    let origin_res = take_json(unsafe {
        maho_ffi::maho_core_configure_sync_encryption(origin, url.as_ptr(), c_phrase.as_ptr())
    });
    assert_eq!(
        origin_res["success"],
        Value::Bool(true),
        "origin: {origin_res}"
    );

    let joiner = maho_ffi::maho_core_new();
    let joiner_res = take_json(unsafe {
        maho_ffi::maho_core_configure_sync_encryption(joiner, url.as_ptr(), c_phrase.as_ptr())
    });
    assert_eq!(
        joiner_res["success"],
        Value::Bool(true),
        "joiner: {joiner_res}"
    );

    unsafe {
        maho_ffi::maho_core_free(origin);
        maho_ffi::maho_core_free(joiner);
    }
}

#[test]
fn configure_sync_bootstrap_is_deterministic_and_fail_closed() {
    let origin = maho_ffi::maho_core_new();
    let joiner = maho_ffi::maho_core_new();
    let url = CString::new(TEST_RELAY_URL).unwrap();
    let generated = take_json(unsafe { maho_ffi::maho_core_generate_sync_bootstrap(origin) });
    let bootstrap = CString::new(
        generated["seed"]
            .as_str()
            .expect("generated bootstrap contains a seed"),
    )
    .unwrap();

    let origin_result = take_json(unsafe {
        maho_ffi::maho_core_configure_sync_bootstrap(origin, url.as_ptr(), bootstrap.as_ptr())
    });
    let joiner_result = take_json(unsafe {
        maho_ffi::maho_core_configure_sync_bootstrap(joiner, url.as_ptr(), bootstrap.as_ptr())
    });

    assert_eq!(origin_result["success"], Value::Bool(true));
    assert_eq!(origin_result["roomId"], joiner_result["roomId"]);

    let malformed = CString::new("not-base64").unwrap();
    let rejected = take_json(unsafe {
        maho_ffi::maho_core_configure_sync_bootstrap(joiner, url.as_ptr(), malformed.as_ptr())
    });
    assert_eq!(rejected["success"], Value::Bool(false));
    assert!(rejected["error"].is_string());

    unsafe {
        maho_ffi::maho_core_free(origin);
        maho_ffi::maho_core_free(joiner);
    }
}

#[test]
fn configure_sync_encryption_rejects_invalid_phrase() {
    let core = maho_ffi::maho_core_new();
    let url = CString::new(TEST_RELAY_URL).unwrap();
    let bad = CString::new("not a real recovery phrase").unwrap();

    let result = take_json(unsafe {
        maho_ffi::maho_core_configure_sync_encryption(core, url.as_ptr(), bad.as_ptr())
    });

    assert_eq!(
        result["success"],
        Value::Bool(false),
        "an invalid phrase must not silently succeed: {result}"
    );
    assert!(
        result["error"].is_string(),
        "failure response must carry a structured error: {result}"
    );

    unsafe { maho_ffi::maho_core_free(core) };
}

#[test]
fn configure_sync_encryption_null_inputs_return_null() {
    let url = CString::new(TEST_RELAY_URL).unwrap();
    let phrase = CString::new("whatever").unwrap();

    // Null core pointer must be rejected before any work.
    let r_null_core = unsafe {
        maho_ffi::maho_core_configure_sync_encryption(
            std::ptr::null_mut(),
            url.as_ptr(),
            phrase.as_ptr(),
        )
    };
    assert!(r_null_core.is_null(), "null core must return null");

    let core = maho_ffi::maho_core_new();

    let r_null_url = unsafe {
        maho_ffi::maho_core_configure_sync_encryption(core, std::ptr::null(), phrase.as_ptr())
    };
    assert!(r_null_url.is_null(), "null server_url must return null");

    let r_null_phrase = unsafe {
        maho_ffi::maho_core_configure_sync_encryption(core, url.as_ptr(), std::ptr::null())
    };
    assert!(
        r_null_phrase.is_null(),
        "null recovery_phrase must return null"
    );

    unsafe { maho_ffi::maho_core_free(core) };
}
