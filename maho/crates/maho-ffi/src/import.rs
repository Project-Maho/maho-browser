//! FFI surface for browser data import.
//!
//! Phase 1 exposes one parser per browser source. The C++ orchestrator owns
//! the import flow and calls these to get parsed JSON, which it then feeds
//! into existing C++ workers (`importers/*_importer.cc`).

use std::ffi::{c_char, CStr, CString};
use std::path::PathBuf;
use std::ptr;

use maho_import::parsers::arc::sidebar::parse_arc_sidebar;
use maho_import::parsers::chromium::autofill::parse_chromium_autofill;
use maho_import::parsers::chromium::bookmarks::parse_chromium_bookmarks;
use maho_import::parsers::chromium::cookies::parse_chromium_cookies;
use maho_import::parsers::chromium::favicons::parse_chromium_favicons;
use maho_import::parsers::chromium::history::parse_chromium_history;
use maho_import::parsers::firefox::autofill::parse_firefox_autofill;
use maho_import::parsers::firefox::bookmarks::parse_firefox_bookmarks;
use maho_import::parsers::firefox::cookies::parse_firefox_cookies;
use maho_import::parsers::firefox::history::parse_firefox_history;
use maho_import::parsers::safari::bookmarks::parse_safari_bookmarks;
use maho_import::parsers::safari::history::parse_safari_history;
use maho_import::parsers::zen::workspaces::parse_zen_workspaces;

use maho_import::detect::detect_installed_browsers;

// ─── View types for serialization ───────────────────────────────────────────

#[derive(serde::Serialize)]
struct ArcParseResultView {
    workspaces: Vec<maho_import::ParsedWorkspace>,
}

#[derive(serde::Serialize)]
struct ZenParseResultView {
    active_workspace_uuid: String,
    workspaces: Vec<maho_import::ParsedWorkspace>,
}

// ─── Helper ─────────────────────────────────────────────────────────────────

/// Serialize a value to JSON and return as a raw C string, or null on failure.
fn to_json_cstring<T: serde::Serialize>(val: &T) -> *mut c_char {
    match serde_json::to_string(val) {
        Ok(json) => CString::new(json)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        Err(_) => ptr::null_mut(),
    }
}

/// Extract a `&Path` from a nullable C string, returning None on null/bad UTF-8.
unsafe fn path_from_c(ptr: *const c_char) -> Option<PathBuf> {
    if ptr.is_null() {
        return None;
    }
    CStr::from_ptr(ptr).to_str().ok().map(PathBuf::from)
}

// ─── Arc ────────────────────────────────────────────────────────────────────

/// Parse Arc sidebar at `<arc_user_data_dir>/StorableSidebar.json`.
/// Returns a JSON string `{"workspaces":[...]}` on success, NULL on error.
/// Caller MUST free the returned string with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_arc(
    arc_user_data_dir: *const c_char,
) -> *mut c_char {
    let path = match path_from_c(arc_user_data_dir) {
        Some(p) => p,
        None => return ptr::null_mut(),
    };
    let result = match parse_arc_sidebar(&path) {
        Ok(r) => r,
        Err(_) => return ptr::null_mut(),
    };
    let view = ArcParseResultView {
        workspaces: result.workspaces,
    };
    to_json_cstring(&view)
}

// ─── Chromium parsers ───────────────────────────────────────────────────────

/// Parse Chromium history from a profile directory.
/// Returns JSON array of `[{"url","title","visit_time","visit_count"},...]`.
/// Caller MUST free with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_chromium_history(
    profile_dir: *const c_char,
) -> *mut c_char {
    let path = match path_from_c(profile_dir) {
        Some(p) => p,
        None => return ptr::null_mut(),
    };
    match parse_chromium_history(&path) {
        Ok(entries) => to_json_cstring(&entries),
        Err(_) => ptr::null_mut(),
    }
}

/// Parse Chromium bookmarks from a profile directory.
/// Returns JSON array of bookmark tree nodes.
/// Caller MUST free with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_chromium_bookmarks(
    profile_dir: *const c_char,
) -> *mut c_char {
    let path = match path_from_c(profile_dir) {
        Some(p) => p,
        None => return ptr::null_mut(),
    };
    match parse_chromium_bookmarks(&path) {
        Ok(entries) => to_json_cstring(&entries),
        Err(_) => ptr::null_mut(),
    }
}

/// Parse Chromium autofill data from a profile directory.
/// Returns JSON array of `[{"field_name","value","times_used","first_used","last_used"},...]`.
/// Caller MUST free with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_chromium_autofill(
    profile_dir: *const c_char,
) -> *mut c_char {
    let path = match path_from_c(profile_dir) {
        Some(p) => p,
        None => return ptr::null_mut(),
    };
    match parse_chromium_autofill(&path) {
        Ok(entries) => to_json_cstring(&entries),
        Err(_) => ptr::null_mut(),
    }
}

/// Parse Chromium cookies from a profile directory.
/// Returns JSON array of ChromiumCookieEntry objects.
/// Caller MUST free with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_chromium_cookies(
    profile_dir: *const c_char,
) -> *mut c_char {
    let path = match path_from_c(profile_dir) {
        Some(p) => p,
        None => return ptr::null_mut(),
    };
    match parse_chromium_cookies(&path) {
        Ok(entries) => to_json_cstring(&entries),
        Err(_) => ptr::null_mut(),
    }
}

/// Parse Chromium favicons from a profile directory.
/// Returns JSON array of `[{"favicon_url","page_urls","image_data"},...]`.
/// Note: `image_data` is a JSON array of bytes.
/// Caller MUST free with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_chromium_favicons(
    profile_dir: *const c_char,
) -> *mut c_char {
    let path = match path_from_c(profile_dir) {
        Some(p) => p,
        None => return ptr::null_mut(),
    };
    match parse_chromium_favicons(&path) {
        Ok(entries) => to_json_cstring(&entries),
        Err(_) => ptr::null_mut(),
    }
}

// ─── Firefox parsers ────────────────────────────────────────────────────────

/// Parse Firefox bookmarks from a profile directory (reads places.sqlite).
/// Returns JSON array of bookmark tree nodes.
/// Caller MUST free with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_firefox_bookmarks(
    profile_dir: *const c_char,
) -> *mut c_char {
    let path = match path_from_c(profile_dir) {
        Some(p) => p,
        None => return ptr::null_mut(),
    };
    match parse_firefox_bookmarks(&path) {
        Ok(entries) => to_json_cstring(&entries),
        Err(_) => ptr::null_mut(),
    }
}

/// Parse Firefox history from a profile directory (reads places.sqlite).
/// Returns JSON array of `[{"url","title","visit_time","visit_count"},...]`.
/// Caller MUST free with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_firefox_history(
    profile_dir: *const c_char,
) -> *mut c_char {
    let path = match path_from_c(profile_dir) {
        Some(p) => p,
        None => return ptr::null_mut(),
    };
    match parse_firefox_history(&path) {
        Ok(entries) => to_json_cstring(&entries),
        Err(_) => ptr::null_mut(),
    }
}

/// Parse Firefox cookies from a profile directory (reads cookies.sqlite).
/// Returns JSON array of `[{"host","name","value","path","expires","is_secure","is_httponly","same_site"},...]`.
/// Caller MUST free with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_firefox_cookies(
    profile_dir: *const c_char,
) -> *mut c_char {
    let path = match path_from_c(profile_dir) {
        Some(p) => p,
        None => return ptr::null_mut(),
    };
    match parse_firefox_cookies(&path) {
        Ok(entries) => to_json_cstring(&entries),
        Err(_) => ptr::null_mut(),
    }
}

/// Parse Firefox autofill from a profile directory (reads formhistory.sqlite).
/// Returns JSON array of `[{"field_name","value","times_used","first_used","last_used"},...]`.
/// Caller MUST free with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_firefox_autofill(
    profile_dir: *const c_char,
) -> *mut c_char {
    let path = match path_from_c(profile_dir) {
        Some(p) => p,
        None => return ptr::null_mut(),
    };
    match parse_firefox_autofill(&path) {
        Ok(entries) => to_json_cstring(&entries),
        Err(_) => ptr::null_mut(),
    }
}

// ─── Safari parsers ─────────────────────────────────────────────────────────

/// Parse Safari bookmarks from the Safari data directory.
/// Returns JSON array of bookmark tree nodes.
/// Caller MUST free with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_safari_bookmarks(
    safari_dir: *const c_char,
) -> *mut c_char {
    let path = match path_from_c(safari_dir) {
        Some(p) => p,
        None => return ptr::null_mut(),
    };
    match parse_safari_bookmarks(&path) {
        Ok(entries) => to_json_cstring(&entries),
        Err(_) => ptr::null_mut(),
    }
}

/// Parse Safari history from the Safari data directory.
/// Returns JSON array of `[{"url","title","visit_time","visit_count"},...]`.
/// Caller MUST free with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_safari_history(
    safari_dir: *const c_char,
) -> *mut c_char {
    let path = match path_from_c(safari_dir) {
        Some(p) => p,
        None => return ptr::null_mut(),
    };
    match parse_safari_history(&path) {
        Ok(entries) => to_json_cstring(&entries),
        Err(_) => ptr::null_mut(),
    }
}

// ─── Zen parser ─────────────────────────────────────────────────────────────

/// Parse Zen workspaces from a Zen profile directory.
/// Returns JSON `{"active_workspace_uuid":"...","workspaces":[...]}`.
/// Caller MUST free with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_zen_workspaces(
    profile_dir: *const c_char,
) -> *mut c_char {
    let path = match path_from_c(profile_dir) {
        Some(p) => p,
        None => return ptr::null_mut(),
    };
    match parse_zen_workspaces(&path) {
        Ok(result) => {
            let view = ZenParseResultView {
                active_workspace_uuid: result.active_workspace_uuid,
                workspaces: result.workspaces,
            };
            to_json_cstring(&view)
        }
        Err(_) => ptr::null_mut(),
    }
}

// ─── Browser detector ───────────────────────────────────────────────────────

/// Detect installed browsers on the system.
/// Returns JSON array of `[{"browser_type","display_name","profile_path","services_supported","requires_full_disk_access"},...]`.
/// Caller MUST free with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_detect_browsers() -> *mut c_char {
    let browsers = detect_installed_browsers();
    to_json_cstring(&browsers)
}

// ─── Phase 3: Password parsers ──────────────────────────────────────────────

/// FFI-safe serialization wrapper for [`maho_import::PasswordEntry`].
///
/// SAFETY: This struct deliberately includes the plaintext password field for
/// the FFI boundary. The C++ side consumes the buffer immediately and passes
/// passwords directly to maho-core's password store — never logged or persisted
/// to disk in plaintext form.
#[derive(serde::Serialize)]
struct PasswordEntryFfi {
    origin_url: String,
    action_url: String,
    username: String,
    password: String,
}

impl From<maho_import::PasswordEntry> for PasswordEntryFfi {
    fn from(e: maho_import::PasswordEntry) -> Self {
        Self {
            origin_url: e.origin_url,
            action_url: e.action_url,
            username: e.username,
            password: e.password,
        }
    }
}

/// Parse Chromium passwords from a profile directory.
///
/// `profile_dir` must contain `Login Data` SQLite database.
/// `keychain_service` is the macOS Keychain service name (e.g. "Chrome Safe Storage").
/// `keychain_account` is the macOS Keychain account name (e.g. "Chrome").
///
/// Returns JSON `[{origin_url, action_url, username, password}, ...]` on success,
/// NULL on failure (e.g. Keychain access denied, database not found).
/// Caller MUST free the returned string with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_chromium_passwords(
    profile_dir: *const c_char,
    keychain_service: *const c_char,
    keychain_account: *const c_char,
) -> *mut c_char {
    if profile_dir.is_null() || keychain_service.is_null() || keychain_account.is_null() {
        return ptr::null_mut();
    }

    let dir_str = match CStr::from_ptr(profile_dir).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let service_str = match CStr::from_ptr(keychain_service).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let account_str = match CStr::from_ptr(keychain_account).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };

    let path = PathBuf::from(dir_str);

    // Fetch the Keychain password and derive the AES-128 key.
    #[cfg(target_os = "macos")]
    let key = {
        use maho_import::decrypt::chromium_keychain::{fetch_and_derive_key, MacOsKeychain};
        let keychain = MacOsKeychain;
        match fetch_and_derive_key(&keychain, service_str, account_str) {
            Ok(k) => k,
            Err(_) => return ptr::null_mut(),
        }
    };

    #[cfg(not(target_os = "macos"))]
    {
        // Password import from Chromium Keychain is macOS-only.
        let _ = (path, service_str, account_str);
        return ptr::null_mut();
    }

    #[cfg(target_os = "macos")]
    {
        let entries = match maho_import::parsers::chromium::passwords::parse_chromium_passwords(
            &path, &key,
        ) {
            Ok(e) => e,
            Err(_) => return ptr::null_mut(),
        };

        let ffi_entries: Vec<PasswordEntryFfi> = entries.into_iter().map(Into::into).collect();
        to_json_cstring(&ffi_entries)
    }
}

/// Parse Firefox passwords from a profile directory.
///
/// `profile_dir` must contain `key4.db` and `logins.json`.
/// `master_password` is the user's master password (empty string if none set).
///
/// Returns JSON `[{origin_url, action_url, username, password}, ...]` on success,
/// NULL on failure (wrong master password, missing files, etc.).
/// Caller MUST free the returned string with `maho_import_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_parse_firefox_passwords(
    profile_dir: *const c_char,
    master_password: *const c_char,
) -> *mut c_char {
    if profile_dir.is_null() || master_password.is_null() {
        return ptr::null_mut();
    }

    let dir_str = match CStr::from_ptr(profile_dir).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let pw_str = match CStr::from_ptr(master_password).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };

    let path = PathBuf::from(dir_str);

    let entries =
        match maho_import::parsers::firefox::passwords::parse_firefox_passwords(&path, pw_str) {
            Ok(e) => e,
            Err(_) => return ptr::null_mut(),
        };

    let ffi_entries: Vec<PasswordEntryFfi> = entries.into_iter().map(Into::into).collect();
    to_json_cstring(&ffi_entries)
}

// ─── String free ────────────────────────────────────────────────────────────

/// Free a string returned by `maho_import_parse_*` or `maho_import_detect_*`.
#[no_mangle]
pub unsafe extern "C" fn maho_import_string_free(s: *mut c_char) {
    if !s.is_null() {
        drop(CString::from_raw(s));
    }
}
