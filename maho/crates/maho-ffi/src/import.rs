//! FFI surface for browser data import.
//!
//! Phase 2 surface (post-cleanup): 5 functions
//! - `maho_import_detect_browsers` — enumerate installed browsers
//! - `maho_import_orchestrator_start` — kick off the async import session
//! - `maho_import_orchestrator_cancel` — cooperative cancel
//! - `maho_import_orchestrator_free` — free session handle after terminal callback
//! - `maho_import_string_free` — free strings returned by detect_browsers

use std::ffi::{c_char, CStr, CString};
use std::ptr;

use maho_import::detect::detect_installed_browsers;

fn to_json_cstring<T: serde::Serialize>(val: &T) -> *mut c_char {
    match serde_json::to_string(val) {
        Ok(json) => CString::new(json)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        Err(_) => ptr::null_mut(),
    }
}

/// Detect installed browsers on the system. Returns a JSON array; caller MUST
/// free the returned string via `maho_import_string_free`.
///
/// # Safety
/// No pointer arguments. Returns a heap-allocated JSON string; caller must free it via
/// `maho_import_string_free`. Returns null on allocation failure.
#[no_mangle]
pub unsafe extern "C" fn maho_import_detect_browsers() -> *mut c_char {
    crate::ffi_safe!({

    let browsers = detect_installed_browsers();
    to_json_cstring(&browsers)

    }, ptr::null_mut())
}

/// Free a string returned by `maho_import_detect_browsers`.
///
/// # Safety
/// `s` must be a pointer returned by `maho_import_detect_browsers`, or null (no-op). Must not
/// be called more than once per pointer.
#[no_mangle]
pub unsafe extern "C" fn maho_import_string_free(s: *mut c_char) {
    crate::ffi_safe!({

    if !s.is_null() {
        drop(CString::from_raw(s));
    }

    }, ())
}

use std::ffi::c_void;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::sync::Arc;
use std::thread;

use maho_core::maho_core::MahoCore;
use maho_import::orchestrator::{ImportProgress, ImportType, Orchestrator, IMPORT_ESSENTIAL_BIT};
use maho_import::{DetectedBrowser, ImportServices};

use crate::import_destination::MahoCoreDestination;

/// Opaque handle for an in-flight import session. Created by
/// `maho_import_orchestrator_start`, freed by `maho_import_orchestrator_free`.
/// The caller MUST call `free` after receiving the terminal callback
/// (`AllComplete` or `Error`).
pub struct MahoImportSession {
    cancel_token: Arc<AtomicBool>,
    thread: Option<thread::JoinHandle<()>>,
}

/// Progress callback type invoked from the import background thread.
///
/// Parameters:
///  - `kind`:  0=Starting, 1=Update, 2=TypeComplete, 3=AllComplete, 4=Error
///  - `import_type`: bitmask bit identifying the import type
///       (1=History, 2=Bookmarks, 4=Passwords, 8=Autofill, 16=Cookies, 32=Workspaces, 64=Favicons, 128=Essential)
///  - `count`: item count (for Update/TypeComplete) or 0
///  - `message`: null-terminated status message or NULL
///  - `user_data`: opaque pointer passed back unchanged
///
/// SAFETY: callback must be safe to invoke from a non-main thread. `message`
/// pointer is only valid for the duration of the callback call. The caller is
/// responsible for keeping `user_data` valid until the terminal event fires.
pub type MahoImportProgressCallback = unsafe extern "C" fn(
    kind: u32,
    import_type: u32,
    count: i32,
    message: *const c_char,
    user_data: *mut c_void,
);

pub type MahoImportCookieCallback = unsafe extern "C" fn(
    host: *const c_char,
    name: *const c_char,
    value: *const c_char,
    path: *const c_char,
    expires: i64,
    is_secure: bool,
    is_httponly: bool,
    same_site: i32,
    user_data: *mut c_void,
);

pub type MahoImportAutofillCallback = unsafe extern "C" fn(
    field_name: *const c_char,
    value: *const c_char,
    times_used: i32,
    first_used: i64,
    last_used: i64,
    user_data: *mut c_void,
);

pub type MahoImportFaviconCallback = unsafe extern "C" fn(
    url: *const c_char,
    png_bytes: *const u8,
    png_len: usize,
    user_data: *mut c_void,
);

/// Start an import orchestrator session on a background thread.
///
/// # Parameters
/// - `core_ptr`: valid `MahoCore*` (from `maho_core_new_with_storage`). Must
///   remain valid and unaccessed by other threads until the terminal callback.
/// - `browser_json`: JSON-serialized `DetectedBrowser` entry.
/// - `items_bitmask`: which import types to run (OR of `ImportServices` bits).
/// - `essentials_json`: JSON array of URL strings for essential import, or NULL.
/// - `callback`: progress callback invoked from the background thread.
/// - `cookie_cb`: callback invoked when a cookie is imported.
/// - `autofill_cb`: callback invoked when an autofill entry is imported.
/// - `favicon_cb`: callback invoked when a favicon is imported.
/// - `user_data`: opaque pointer forwarded to callback.
///
/// # Safety
/// All pointer parameters (except `user_data`) must be valid for the duration
/// of this call. `core_ptr` must remain valid until the terminal callback fires.
#[no_mangle]
pub unsafe extern "C" fn maho_import_orchestrator_start(
    core_ptr: *mut MahoCore,
    browser_json: *const c_char,
    items_bitmask: u32,
    essentials_json: *const c_char,
    callback: MahoImportProgressCallback,
    cookie_cb: Option<unsafe extern "C" fn(
        host: *const c_char,
        name: *const c_char,
        value: *const c_char,
        path: *const c_char,
        expires: i64,
        is_secure: bool,
        is_httponly: bool,
        same_site: i32,
        user_data: *mut c_void,
    )>,
    autofill_cb: Option<unsafe extern "C" fn(
        field_name: *const c_char,
        value: *const c_char,
        times_used: i32,
        first_used: i64,
        last_used: i64,
        user_data: *mut c_void,
    )>,
    favicon_cb: Option<unsafe extern "C" fn(
        url: *const c_char,
        png_bytes: *const u8,
        png_len: usize,
        user_data: *mut c_void,
    )>,
    user_data: *mut c_void,
) -> *mut MahoImportSession {
    crate::ffi_safe!({

    if core_ptr.is_null() || browser_json.is_null() {
        return ptr::null_mut();
    }

    // SAFETY(H15): Acquire the import gate so concurrent FFI calls on MahoCore
    // are rejected for the duration of this import session.
    if !crate::import_gate::enter() {
        // Another import is already active.
        return ptr::null_mut();
    }

    let browser_str = match CStr::from_ptr(browser_json).to_str() {
        Ok(s) => s,
        Err(_) => { crate::import_gate::exit(); return ptr::null_mut(); }
    };
    let browser: DetectedBrowser = match serde_json::from_str(browser_str) {
        Ok(b) => b,
        Err(_) => { crate::import_gate::exit(); return ptr::null_mut(); }
    };

    let essentials: Vec<String> = if essentials_json.is_null() {
        Vec::new()
    } else {
        match CStr::from_ptr(essentials_json).to_str() {
            Ok(s) => serde_json::from_str(s).unwrap_or_default(),
            Err(_) => Vec::new(),
        }
    };

    let destination = Arc::new(MahoCoreDestination::new_with_callbacks(
        core_ptr,
        cookie_cb,
        autofill_cb,
        favicon_cb,
        user_data,
    ));
    let cancel_token = Arc::new(AtomicBool::new(false));
    let cancel_clone = cancel_token.clone();
    let ctx = CallbackContext { callback, user_data };

    let handle = thread::Builder::new()
        .name("maho-import-orchestrator".into())
        .spawn(move || {
            let _gate = crate::import_gate::GateGuard;
            let mut orchestrator = Orchestrator::new(destination);
            if !essentials.is_empty() {
                orchestrator.set_selected_essentials(essentials);
            }
            let orchestrator = Arc::new(orchestrator);
            let orchestrator_worker = orchestrator.clone();

            let (tx, rx) = mpsc::channel();
            let browser_for_worker = browser.clone();

            let worker = thread::Builder::new()
                .name("maho-import-worker".into())
                .spawn(move || {
                    let _ = orchestrator_worker.start_import(&browser_for_worker, items_bitmask, tx);
                })
                .ok();

            loop {
                if cancel_clone.load(Ordering::SeqCst) {
                    orchestrator.cancel();
                    ctx.invoke(3, 0, 0, ptr::null());
                    break;
                }
                match rx.recv_timeout(std::time::Duration::from_millis(100)) {
                    Ok(progress) => {
                        ctx.forward_progress(&progress);
                    }
                    Err(mpsc::RecvTimeoutError::Timeout) => {
                        // Loop again to check cancellation
                    }
                    Err(mpsc::RecvTimeoutError::Disconnected) => {
                        // Drain remaining events and exit
                        for progress in rx.try_iter() {
                            ctx.forward_progress(&progress);
                        }
                        break;
                    }
                }
            }

            if let Some(h) = worker {
                let _ = h.join();
            }
        })
        .ok();

    match handle {
        Some(h) => {
            let session = Box::new(MahoImportSession {
                cancel_token,
                thread: Some(h),
            });
            Box::into_raw(session)
        }
        None => { crate::import_gate::exit(); ptr::null_mut() }
    }

    }, ptr::null_mut())
}

/// Cancel an in-flight import. Safe to call from any thread. The cancellation
/// is cooperative — the orchestrator checks the flag between import types.
///
/// # Safety
/// `handle` must be a valid pointer returned by `maho_import_orchestrator_start`,
/// or NULL (no-op).
#[no_mangle]
pub unsafe extern "C" fn maho_import_orchestrator_cancel(handle: *mut MahoImportSession) {
    crate::ffi_safe!({

    if handle.is_null() {
        return;
    }
    let session = &*handle;
    session.cancel_token.store(true, Ordering::SeqCst);

    }, ())
}

/// Free the import session handle. MUST be called after the terminal callback
/// (`AllComplete` or `Error`) has fired. Blocks until the background thread
/// finishes (should be near-instant after terminal event).
///
/// # Safety
/// `handle` must be a valid pointer returned by `maho_import_orchestrator_start`,
/// or NULL (no-op). Must not be called more than once per handle.
#[no_mangle]
pub unsafe extern "C" fn maho_import_orchestrator_free(handle: *mut MahoImportSession) {
    crate::ffi_safe!({

    if handle.is_null() {
        return;
    }
    let mut session = Box::from_raw(handle);
    if let Some(thread) = session.thread.take() {
        let _ = thread.join();
    }

    }, ())
}

struct CallbackContext {
    callback: MahoImportProgressCallback,
    user_data: *mut c_void,
}

// SAFETY: caller guarantees `user_data` validity until the terminal event.
// Function pointers are trivially thread-safe.
unsafe impl Send for CallbackContext {}

impl CallbackContext {
    fn invoke(&self, kind: u32, import_type: u32, count: i32, message: *const c_char) {
        unsafe {
            (self.callback)(kind, import_type, count, message, self.user_data);
        }
    }

    fn forward_progress(&self, progress: &ImportProgress) {
        match progress {
            ImportProgress::Starting { import_type } => {
                self.invoke(0, import_type_to_bit(*import_type), 0, ptr::null());
            }
            ImportProgress::Update {
                import_type,
                imported,
                status,
            } => {
                let msg = CString::new(status.as_str()).ok();
                let msg_ptr = msg.as_ref().map(|m| m.as_ptr()).unwrap_or(ptr::null());
                self.invoke(1, import_type_to_bit(*import_type), *imported as i32, msg_ptr);
            }
            ImportProgress::TypeComplete { import_type, count } => {
                self.invoke(2, import_type_to_bit(*import_type), *count as i32, ptr::null());
            }
            ImportProgress::AllComplete => {
                self.invoke(3, 0, 0, ptr::null());
            }
            ImportProgress::Error { message } => {
                let msg = CString::new(message.as_str()).ok();
                let msg_ptr = msg.as_ref().map(|m| m.as_ptr()).unwrap_or(ptr::null());
                self.invoke(4, 0, 0, msg_ptr);
            }
        }
    }
}

fn import_type_to_bit(t: ImportType) -> u32 {
    match t {
        ImportType::History => ImportServices::HISTORY,
        ImportType::Bookmarks => ImportServices::BOOKMARKS,
        ImportType::Cookies => ImportServices::COOKIES,
        ImportType::Autofill => ImportServices::AUTOFILL,
        ImportType::Favicons => ImportServices::FAVICONS,
        ImportType::Workspaces => ImportServices::WORKSPACES,
        ImportType::Essential => IMPORT_ESSENTIAL_BIT,
    }
}
