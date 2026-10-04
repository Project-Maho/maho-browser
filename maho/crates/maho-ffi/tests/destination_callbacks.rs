//! Verifies the cookie/autofill/favicon callback dispatch through
//! `MahoCoreDestination` actually invokes the FFI callback function pointers.
//! Proves the Rust→C++ bridge mechanism end-to-end at the FFI boundary for
//! all 3 wired destination methods (Phase 4 verification).

use std::ffi::{c_char, c_void, CStr, CString};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;

use maho_ffi::import_destination::MahoCoreDestination;
use maho_import::orchestrator::ImportDestination;
use serde_json::Value;

struct CallbackCounters {
    cookie_calls: AtomicU32,
    autofill_calls: AtomicU32,
    favicon_calls: AtomicU32,
    last_cookie_host: std::sync::Mutex<String>,
    last_autofill_field: std::sync::Mutex<String>,
    last_favicon_len: AtomicU32,
}

unsafe extern "C" fn cookie_cb(
    host: *const c_char,
    name: *const c_char,
    value: *const c_char,
    path: *const c_char,
    expires: i64,
    _is_secure: bool,
    _is_httponly: bool,
    _same_site: i32,
    user_data: *mut c_void,
) {
    let ctrs = &*(user_data as *const CallbackCounters);
    ctrs.cookie_calls.fetch_add(1, Ordering::SeqCst);
    *ctrs.last_cookie_host.lock().unwrap() = CStr::from_ptr(host).to_str().unwrap().to_string();
    assert!(!name.is_null() && !value.is_null() && !path.is_null());
    assert!(expires >= 0);
}

unsafe extern "C" fn autofill_cb(
    field_name: *const c_char,
    value: *const c_char,
    _times_used: i32,
    _first_used: i64,
    _last_used: i64,
    user_data: *mut c_void,
) {
    let ctrs = &*(user_data as *const CallbackCounters);
    ctrs.autofill_calls.fetch_add(1, Ordering::SeqCst);
    *ctrs.last_autofill_field.lock().unwrap() =
        CStr::from_ptr(field_name).to_str().unwrap().to_string();
    assert!(!value.is_null());
}

unsafe extern "C" fn favicon_cb(
    url: *const c_char,
    png_bytes: *const u8,
    png_len: usize,
    user_data: *mut c_void,
) {
    let ctrs = &*(user_data as *const CallbackCounters);
    ctrs.favicon_calls.fetch_add(1, Ordering::SeqCst);
    ctrs.last_favicon_len
        .store(png_len as u32, Ordering::SeqCst);
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

    assert!(dest.add_cookie(
        "example.com",
        "session",
        "abc",
        "/",
        1_700_000_000,
        true,
        true,
        1
    ));
    assert!(dest.add_autofill("email", "user@example.com", 5, 1000, 2000));
    let png = vec![0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A];
    assert!(dest.add_favicon("https://example.com/favicon.ico", &png));

    assert_eq!(counters.cookie_calls.load(Ordering::SeqCst), 1);
    assert_eq!(counters.autofill_calls.load(Ordering::SeqCst), 1);
    assert_eq!(counters.favicon_calls.load(Ordering::SeqCst), 1);
    assert_eq!(*counters.last_cookie_host.lock().unwrap(), "example.com");
    assert_eq!(*counters.last_autofill_field.lock().unwrap(), "email");
    assert_eq!(counters.last_favicon_len.load(Ordering::SeqCst), 8);

    unsafe {
        let _ = Arc::from_raw(counters_ptr as *const CallbackCounters);
    }
}

#[test]
fn destination_returns_false_when_callbacks_unset() {
    let dest = unsafe {
        MahoCoreDestination::new_with_callbacks(
            std::ptr::null_mut(),
            None,
            None,
            None,
            std::ptr::null_mut(),
        )
    };
    assert!(!dest.add_cookie("a", "b", "c", "/", 0, false, false, 0));
    assert!(!dest.add_autofill("a", "b", 0, 0, 0));
    assert!(!dest.add_favicon("a", &[]));
}

#[test]
fn destination_add_password_writes_to_vault() {
    let key =
        CString::new("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef").unwrap();
    assert!(unsafe { maho_ffi::maho_storage_set_sqlcipher_key(key.as_ptr()) });
    let dir = std::env::temp_dir().join(format!(
        "maho-dest-password-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    std::fs::create_dir_all(&dir).unwrap();
    let db_path = CString::new(
        dir.join("destination-password.sqlite")
            .to_string_lossy()
            .as_bytes(),
    )
    .unwrap();
    let core = unsafe { maho_ffi::maho_core_new_with_storage(db_path.as_ptr()) };
    assert!(!core.is_null());
    let init = CString::new(
        r#"{"masterPassphrase":"correct passphrase","recoverySecret":"recovery key"}"#,
    )
    .unwrap();
    let init_ptr = unsafe { maho_ffi::maho_vault_initialize_json(core, init.as_ptr()) };
    assert!(!init_ptr.is_null());
    unsafe { maho_ffi::maho_string_free(init_ptr) };

    let dest = unsafe {
        MahoCoreDestination::new_with_callbacks(core, None, None, None, std::ptr::null_mut())
    };
    assert!(dest.add_password(maho_import::PasswordEntry {
        origin_url: "https://import.example".to_string(),
        action_url: "https://import.example/login".to_string(),
        username: "importer@example.com".to_string(),
        password: "import-secret".to_string(),
    }));

    let query = CString::new("import.example").unwrap();
    let list_ptr = unsafe { maho_ffi::maho_core_search_passwords(core, query.as_ptr()) };
    assert!(!list_ptr.is_null());
    let list_json = unsafe { CStr::from_ptr(list_ptr).to_str().unwrap() };
    assert!(!list_json.contains("import-secret"));
    let list: Vec<Value> = serde_json::from_str(list_json).unwrap();
    unsafe { maho_ffi::maho_string_free(list_ptr) };
    assert_eq!(list.len(), 1);
    assert_eq!(
        list[0].get("domain").and_then(Value::as_str),
        Some("https://import.example")
    );

    unsafe { maho_ffi::maho_core_free(core) };
    std::fs::remove_dir_all(dir).unwrap();
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

    unsafe {
        let _ = Arc::from_raw(counters_ptr as *const CallbackCounters);
    }
}

#[test]
fn destination_persists_nonempty_tab_title_after_reopen() {
    let storage = tempfile::tempdir().unwrap();
    let path = CString::new(storage.path().to_string_lossy().as_bytes()).unwrap();
    let core = unsafe { maho_ffi::maho_core_new_with_storage(path.as_ptr()) };
    assert!(!core.is_null());

    let imported_title = "Fictional Observatory Notes";
    let tab_id = {
        let destination = unsafe { MahoCoreDestination::new(core) };
        let space_id = destination.get_active_space_id().unwrap();
        destination
            .create_tab(
                &space_id,
                "https://observatory.invalid/notes",
                imported_title,
            )
            .unwrap()
    };

    let created_title = unsafe {
        (&*core)
            .get_tab_view_models()
            .into_iter()
            .find(|tab| tab.id.0 == tab_id)
            .unwrap()
            .title
    };
    assert_eq!(created_title, imported_title);
    unsafe { maho_ffi::maho_core_free(core) };

    let reopened = unsafe { maho_ffi::maho_core_new_with_storage(path.as_ptr()) };
    assert!(!reopened.is_null());
    unsafe { (&mut *reopened).load_state().unwrap() };
    let loaded_title = unsafe {
        (&*reopened)
            .get_tab_view_models()
            .into_iter()
            .find(|tab| tab.id.0 == tab_id)
            .unwrap()
            .title
    };
    assert_eq!(loaded_title, imported_title);
    unsafe { maho_ffi::maho_core_free(reopened) };
}

#[test]
fn destination_preserves_default_title_for_empty_imported_title() {
    let storage = tempfile::tempdir().unwrap();
    let path = CString::new(storage.path().to_string_lossy().as_bytes()).unwrap();
    let core = unsafe { maho_ffi::maho_core_new_with_storage(path.as_ptr()) };
    assert!(!core.is_null());

    let tab_id = {
        let destination = unsafe { MahoCoreDestination::new(core) };
        let space_id = destination.get_active_space_id().unwrap();
        destination
            .create_tab(&space_id, "https://fallback.invalid/untitled", "")
            .unwrap()
    };

    let created_title = unsafe {
        (&*core)
            .get_tab_view_models()
            .into_iter()
            .find(|tab| tab.id.0 == tab_id)
            .unwrap()
            .title
    };
    assert_eq!(created_title, "New Tab");
    unsafe { maho_ffi::maho_core_free(core) };
}

#[test]
fn destination_persists_foldered_tab_title_after_reopen() {
    let storage = tempfile::tempdir().unwrap();
    let path = CString::new(storage.path().to_string_lossy().as_bytes()).unwrap();
    let core = unsafe { maho_ffi::maho_core_new_with_storage(path.as_ptr()) };
    assert!(!core.is_null());

    let imported_title = "Fictional Foldered Notes";
    let tab_id = {
        let destination = unsafe { MahoCoreDestination::new(core) };
        let space_id = destination.get_active_space_id().unwrap();
        let folder_id = destination
            .create_folder(&space_id, "Fictional Folder", "")
            .unwrap();
        destination
            .create_tab_in_folder(
                &space_id,
                "https://foldered.invalid/notes",
                imported_title,
                &folder_id,
            )
            .unwrap()
    };

    unsafe { maho_ffi::maho_core_free(core) };
    let reopened = unsafe { maho_ffi::maho_core_new_with_storage(path.as_ptr()) };
    assert!(!reopened.is_null());
    unsafe { (&mut *reopened).load_state().unwrap() };
    let loaded_title = unsafe {
        (&*reopened)
            .get_tab_view_models()
            .into_iter()
            .find(|tab| tab.id.0 == tab_id)
            .unwrap()
            .title
    };
    assert_eq!(loaded_title, imported_title);
    unsafe { maho_ffi::maho_core_free(reopened) };
}

// Importers hand over whatever the source browser stored: Arc keeps icon
// *names* ("bulb") next to real emoji. The destination must apply the icon and
// maho-core must canonicalize names, or every shell renders the raw word.
#[test]
fn destination_create_space_applies_canonical_icon() {
    let storage = tempfile::tempdir().unwrap();
    let path = CString::new(storage.path().to_string_lossy().as_bytes()).unwrap();
    let core = unsafe { maho_ffi::maho_core_new_with_storage(path.as_ptr()) };
    assert!(!core.is_null());

    let (emoji_space, named_space) = {
        let destination = unsafe { MahoCoreDestination::new(core) };
        (
            destination
                .create_space("Fictional Emoji Space", "#336699", "\u{1F680}")
                .unwrap(),
            destination
                .create_space("Fictional Named Space", "#336699", "bulb")
                .unwrap(),
        )
    };

    let icons: std::collections::HashMap<String, Option<String>> = unsafe {
        (&*core)
            .get_space_view_models()
            .into_iter()
            .map(|space| (space.id.0, space.icon))
            .collect()
    };
    assert_eq!(icons[&emoji_space].as_deref(), Some("\u{1F680}"));
    assert_eq!(icons[&named_space].as_deref(), Some("\u{1F4A1}"));
    unsafe { maho_ffi::maho_core_free(core) };
}
