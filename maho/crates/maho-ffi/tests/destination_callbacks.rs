//! Verifies the cookie/autofill/favicon callback dispatch through
//! `MahoCoreDestination` actually invokes the FFI callback function pointers.
//! Proves the Rust→C++ bridge mechanism end-to-end at the FFI boundary for
//! all 3 wired destination methods (Phase 4 verification).

use std::ffi::{c_char, c_void, CStr};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;

use maho_ffi::import_destination::MahoCoreDestination;
use maho_import::orchestrator::ImportDestination;

struct CallbackCounters {
    cookie_calls: AtomicU32,
    autofill_calls: AtomicU32,
    favicon_calls: AtomicU32,
    last_cookie_host: std::sync::Mutex<String>,
    last_autofill_field: std::sync::Mutex<String>,
    last_favicon_len: AtomicU32,
}

unsafe extern "C" fn cookie_cb(
    host: *const c_char, name: *const c_char, value: *const c_char, path: *const c_char,
    expires: i64, _is_secure: bool, _is_httponly: bool, _same_site: i32,
    user_data: *mut c_void,
) {
    let ctrs = &*(user_data as *const CallbackCounters);
    ctrs.cookie_calls.fetch_add(1, Ordering::SeqCst);
    *ctrs.last_cookie_host.lock().unwrap() =
        CStr::from_ptr(host).to_str().unwrap().to_string();
    assert!(!name.is_null() && !value.is_null() && !path.is_null());
    assert!(expires >= 0);
}

unsafe extern "C" fn autofill_cb(
    field_name: *const c_char, value: *const c_char,
    _times_used: i32, _first_used: i64, _last_used: i64,
    user_data: *mut c_void,
) {
    let ctrs = &*(user_data as *const CallbackCounters);
    ctrs.autofill_calls.fetch_add(1, Ordering::SeqCst);
    *ctrs.last_autofill_field.lock().unwrap() =
        CStr::from_ptr(field_name).to_str().unwrap().to_string();
    assert!(!value.is_null());
}

unsafe extern "C" fn favicon_cb(
    url: *const c_char, png_bytes: *const u8, png_len: usize, user_data: *mut c_void,
) {
    let ctrs = &*(user_data as *const CallbackCounters);
    ctrs.favicon_calls.fetch_add(1, Ordering::SeqCst);
    ctrs.last_favicon_len.store(png_len as u32, Ordering::SeqCst);
    assert!(!url.is_null());
    assert!(!png_bytes.is_null() || png_len == 0);
}

#[test]
fn destination_callbacks_dispatch_through_ffi() {
    let counters = Arc::new(CallbackCounters {
        cookie_calls: AtomicU32::new(0),
        autofill_calls: AtomicU32::new(0),
        favicon_calls: AtomicU32::new(0),
        last_cookie_host: std::sync::Mutex::new(String::new()),
        last_autofill_field: std::sync::Mutex::new(String::new()),
        last_favicon_len: AtomicU32::new(0),
    });
    let counters_ptr = Arc::into_raw(counters.clone()) as *mut c_void;

    let dest = unsafe {
        MahoCoreDestination::new_with_callbacks(
            std::ptr::null_mut(),
            Some(cookie_cb),
            Some(autofill_cb),
            Some(favicon_cb),
            counters_ptr,
        )
    };

    assert!(dest.add_cookie("example.com", "session", "abc", "/", 1_700_000_000, true, true, 1));
    assert!(dest.add_autofill("email", "user@example.com", 5, 1000, 2000));
    let png = vec![0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A];
    assert!(dest.add_favicon("https://example.com/favicon.ico", &png));

    assert_eq!(counters.cookie_calls.load(Ordering::SeqCst), 1);
    assert_eq!(counters.autofill_calls.load(Ordering::SeqCst), 1);
    assert_eq!(counters.favicon_calls.load(Ordering::SeqCst), 1);
    assert_eq!(*counters.last_cookie_host.lock().unwrap(), "example.com");
    assert_eq!(*counters.last_autofill_field.lock().unwrap(), "email");
    assert_eq!(counters.last_favicon_len.load(Ordering::SeqCst), 8);

    unsafe { let _ = Arc::from_raw(counters_ptr as *const CallbackCounters); }
}

#[test]
fn destination_returns_false_when_callbacks_unset() {
    let dest = unsafe {
        MahoCoreDestination::new_with_callbacks(
            std::ptr::null_mut(), None, None, None, std::ptr::null_mut(),
        )
    };
    assert!(!dest.add_cookie("a", "b", "c", "/", 0, false, false, 0));
    assert!(!dest.add_autofill("a", "b", 0, 0, 0));
    assert!(!dest.add_favicon("a", &[]));
}

#[test]
fn destination_handles_null_bytes_in_strings_gracefully() {
    let counters = Arc::new(CallbackCounters {
        cookie_calls: AtomicU32::new(0),
        autofill_calls: AtomicU32::new(0),
        favicon_calls: AtomicU32::new(0),
        last_cookie_host: std::sync::Mutex::new(String::new()),
        last_autofill_field: std::sync::Mutex::new(String::new()),
        last_favicon_len: AtomicU32::new(0),
    });
    let counters_ptr = Arc::into_raw(counters.clone()) as *mut c_void;

    let dest = unsafe {
        MahoCoreDestination::new_with_callbacks(
            std::ptr::null_mut(),
            Some(cookie_cb),
            Some(autofill_cb),
            Some(favicon_cb),
            counters_ptr,
        )
    };

    assert!(!dest.add_cookie("host\0with\0nulls", "n", "v", "/", 0, false, false, 0));
    assert_eq!(counters.cookie_calls.load(Ordering::SeqCst), 0);

    unsafe { let _ = Arc::from_raw(counters_ptr as *const CallbackCounters); }
}
