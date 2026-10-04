use std::borrow::Cow;
use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::{c_char, c_void, CStr, CString};
use std::path::Path;
use std::ptr;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc as FfiArc, Condvar as FfiCondvar, Mutex as FfiMutex, OnceLock};

use maho_core::content_blocker::normalize_site_exception_key;
use maho_core::maho_core::MahoCore;
use maho_core::workspace_manager::WorkspaceManager;
use maho_types::keyboard::KeyCombo;

use crate::common::{
    cstr_to_str, to_c_string, to_json_cstring, ACTIVE_CALLBACK_TOKENS, DEFERRED_CORE_UPDATES,
    FFI_SERIALIZATION_DEPTH, CallbackLease, NEXT_CALLBACK_TOKEN,
};
use crate::ffi_safe;
use crate::import_gate;
use crate::skills::remove_routine_run_registry;


#[derive(serde::Serialize, Clone)]
#[serde(rename_all = "camelCase")]
struct ContentBlockerMutationError {
    code: String,
    message: String,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct ContentBlockerMutationResult {
    success: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    error: Option<ContentBlockerMutationError>,
    state: Option<maho_types::content_blocking::ContentBlockerStateDto>,
    compile_required: bool,
}

fn content_blocker_result(
    core: Option<&MahoCore>,
    success: bool,
    error: Option<ContentBlockerMutationError>,
    compile_required: bool,
) -> ContentBlockerMutationResult {
    ContentBlockerMutationResult {
        success,
        error,
        state: core.map(MahoCore::get_content_blocker_state_dto),
        compile_required,
    }
}

fn content_blocker_error(code: &str, message: impl Into<String>) -> ContentBlockerMutationError {
    ContentBlockerMutationError {
        code: code.to_string(),
        message: message.into(),
    }
}

fn content_blocker_error_from_core(
    error: &maho_types::content_blocking::ContentBlockingError,
) -> ContentBlockerMutationError {
    content_blocker_error(error.code(), error.to_string())
}

fn content_blocker_error_from_update(error: String) -> ContentBlockerMutationError {
    let code = if error.starts_with("invalid update body:") {
        "invalid_update_body"
    } else if error.starts_with("filter body too large:") {
        "filter_body_too_large"
    } else if error.starts_with("HTTP status ") {
        "update_http_status"
    } else {
        "update_rejected"
    };
    content_blocker_error(code, error)
}

fn add_filter_list_result(
    core: &mut MahoCore,
    id: String,
    name: String,
    url: String,
) -> ContentBlockerMutationResult {
    let updates = core.handle_event(maho_types::events::shell_event::ShellEvent::AddFilterList {
        id,
        name,
        url,
    });
    let error = updates.iter().find_map(|update| match update {
        maho_types::events::core_update::CoreUpdate::ContentBlockerStateChanged(change) => change
            .last_error
            .as_ref()
            .map(content_blocker_error_from_core),
        _ => None,
    });
    content_blocker_result(Some(core), error.is_none(), error.clone(), error.is_none())
}

fn toggle_filter_list_result(
    core: &mut MahoCore,
    id: String,
    enabled: bool,
) -> ContentBlockerMutationResult {
    let state = core.get_content_blocker_state_dto();
    let Some(list) = state.lists.iter().find(|list| list.id == id) else {
        return content_blocker_result(
            Some(core),
            false,
            Some(content_blocker_error(
                "filter_list_not_found",
                "filter list was not found",
            )),
            false,
        );
    };
    let changed = list.enabled != enabled;
    core.handle_event(
        maho_types::events::shell_event::ShellEvent::ToggleFilterList { id, enabled },
    );
    content_blocker_result(Some(core), true, None, changed)
}

fn remove_filter_list_result(core: &mut MahoCore, id: String) -> ContentBlockerMutationResult {
    let state = core.get_content_blocker_state_dto();
    if !state.lists.iter().any(|list| list.id == id) {
        return content_blocker_result(
            Some(core),
            false,
            Some(content_blocker_error(
                "filter_list_not_found",
                "filter list was not found",
            )),
            false,
        );
    }
    core.handle_event(maho_types::events::shell_event::ShellEvent::RemoveFilterList { id });
    content_blocker_result(Some(core), true, None, true)
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct PasswordEntryView {
    id: String,
    domain: String,
    username: String,
    created_at: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    last_used: Option<String>,
}

#[no_mangle]
#[no_mangle]
pub extern "C" fn maho_core_new() -> *mut MahoCore {
    ffi_safe!(
        { Box::into_raw(Box::new(MahoCore::new())) },
        ptr::null_mut()
    )
}

/// Returns whether core initialization completed successfully.
///
/// # Safety
/// `core` must be null or a valid pointer returned by `maho_core_new*`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_is_ready(core: *mut MahoCore) -> bool {
    ffi_safe!({ !core.is_null() && (&*core).is_ready() }, false)
}

/// Returns the stable readiness code: 0 = uninitialized, 1 = initializing,
/// 2 = ready, 3 = failed. Null pointers return 0.
///
/// # Safety
/// `core` must be null or a valid pointer returned by `maho_core_new*`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_readiness_status(core: *mut MahoCore) -> u8 {
    ffi_safe!(
        {
            if core.is_null() {
                return 0;
            }
            (&*core).readiness_status().code()
        },
        0
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_free(ptr: *mut MahoCore) {
    ffi_safe!(
        {
            if !ptr.is_null() {
                remove_routine_run_registry(ptr);
                let mut core = Box::from_raw(ptr);
                core.handle_event(maho_types::events::shell_event::ShellEvent::AppWillTerminate);
                drop(core);
            }
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `event_json` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_handle_event(
    ptr: *mut MahoCore,
    event_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || event_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let json_str = match CStr::from_ptr(event_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let event: maho_types::events::shell_event::ShellEvent =
                match serde_json::from_str(json_str) {
                    Ok(e) => e,
                    Err(e) => {
                        // Loud parse failure: return a structured error payload instead of a
                        // silent null so callers can surface the problem. Mirrors the
                        // error-payload/C-string convention used elsewhere in this crate.
                        let json = serde_json::json!({
                            "error": {
                                "kind": "parse",
                                "detail": e.to_string(),
                            }
                        });
                        return CString::new(json.to_string())
                            .map(CString::into_raw)
                            .unwrap_or(ptr::null_mut());
                    }
                };
            let updates = core.handle_event(event);
            process_split_callbacks_for_updates(&updates);
            to_json_cstring(&updates)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.

#[no_mangle]
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_account_state(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            to_json_cstring(&core.get_account_state_json())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_auth_state(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            to_json_cstring(&core.get_auth_state_json())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_sign_in(
    ptr: *mut MahoCore,
    event_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || event_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let json_str = match CStr::from_ptr(event_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let event: maho_types::events::shell_event::ShellEvent =
                match serde_json::from_str(json_str) {
                    Ok(e) => e,
                    Err(_) => return ptr::null_mut(),
                };
            let updates = core.handle_event(event);
            to_json_cstring(&updates)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_dispatch_shell_event(
    ptr: *mut MahoCore,
    event_json: *const c_char,
) -> *mut c_char {
    maho_core_sign_in(ptr, event_json)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_tick(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let updates = core.tick();
            process_split_callbacks_for_updates(&updates);
            to_json_cstring(&updates)
        },
        ptr::null_mut()
    )
}

/// Deterministic sibling of `maho_core_tick`: drives `MahoCore::tick_at` with
/// a caller-supplied timestamp instead of the wall clock. This is the test
/// seam the native scheduler hook (`MahoRoutinesScheduler::RunTickForTesting`)
/// uses to exercise Vault inactivity auto-lock without real time.
///
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_tick_at(ptr: *mut MahoCore, now_sec: i64) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let Some(now) = chrono::DateTime::<chrono::Utc>::from_timestamp(now_sec, 0) else {
                return ptr::null_mut();
            };
            let core = &mut *ptr;
            let updates = core.tick_at(now);
            process_split_callbacks_for_updates(&updates);
            to_json_cstring(&updates)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid null-terminated C string containing a SpaceId JSON.

/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_boost_view_models(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            to_json_cstring(&core.get_boost_view_models())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_css_mods(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            to_json_cstring(&core.css_mod_manager().get_all_mods())
        },
        ptr::null_mut()
    )
}

/// Returns the combined CSS string of all enabled mods.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Returns null on null
/// input, empty CSS, or if an import is in progress.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_combined_css(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return std::ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let css = core.css_mod_manager().generate_combined_css();
            if css.is_empty() {
                return std::ptr::null_mut();
            }
            match CString::new(css) {
                Ok(s) => s.into_raw(),
                Err(_) => std::ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_installed_extensions(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            to_json_cstring(&core.get_installed_extensions())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_key` must be a valid null-terminated UTF-8 string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_installed_extensions_for_profile(
    ptr: *mut MahoCore,
    profile_key: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || profile_key.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let profile_key = match CStr::from_ptr(profile_key).to_str() {
                Ok(value) => value,
                Err(_) => return ptr::null_mut(),
            };
            to_json_cstring(&core.get_installed_extensions_for_profile(profile_key))
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_clear_installed_extensions(ptr: *mut MahoCore) {
    ffi_safe!(
        {
            if ptr.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            core.clear_installed_extensions();
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_key` must be a valid null-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_clear_installed_extensions_for_profile(
    ptr: *mut MahoCore,
    profile_key: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || profile_key.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let profile_key = match CStr::from_ptr(profile_key).to_str() {
                Ok(value) => value,
                Err(_) => return,
            };
            core.clear_installed_extensions_for_profile(profile_key);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `extensions_json` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_installed_extensions(
    ptr: *mut MahoCore,
    extensions_json: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || extensions_json.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let json_str = match CStr::from_ptr(extensions_json).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let extensions = match maho_core::extension_bridge::parse_extension_payload(json_str) {
                Ok(exts) => exts,
                Err(e) => {
                    eprintln!("[ffi] set_installed_extensions: invalid extension payload: {e}");
                    return;
                }
            };
            core.set_installed_extensions(extensions);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_key` and `extensions_json` must be valid null-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_installed_extensions_for_profile(
    ptr: *mut MahoCore,
    profile_key: *const c_char,
    extensions_json: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || profile_key.is_null() || extensions_json.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let profile_key = match CStr::from_ptr(profile_key).to_str() {
                Ok(value) => value,
                Err(_) => return,
            };
            let json_str = match CStr::from_ptr(extensions_json).to_str() {
                Ok(value) => value,
                Err(_) => return,
            };
            let extensions = match maho_core::extension_bridge::parse_extension_payload(json_str) {
                Ok(exts) => exts,
                Err(e) => {
                    eprintln!(
                        "[ffi] set_installed_extensions_for_profile: invalid extension payload: {e}"
                    );
                    return;
                }
            };
            core.set_installed_extensions_for_profile(profile_key, extensions);
        },
        ()
    )
}

pub type MahoExtensionSyncCallback = extern "C" fn(
    user_data: *mut c_void,
    extension_id: *const c_char,
    enabled: bool,
    deleted: bool,
);

#[no_mangle]
pub unsafe extern "C" fn maho_core_register_extension_sync_callback(
    ptr: *mut MahoCore,
    callback: Option<extern "C" fn(*mut c_void, *const c_char, bool, bool)>,
    user_data: *mut c_void,
) {
    ffi_safe!(
        {
            if ptr.is_null() {
                return;
            }
            // SAFETY(H8): gate on import to avoid a data race on MahoCore, matching
            // the other &mut *ptr FFIs.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            if let Some(cb) = callback {
                let ud_raw = user_data as usize;
                core.register_extension_sync_callback(Some(Box::new(
                    move |id, enabled, deleted| {
                        // SAFETY(H8): the sync callback is invoked on the UI/sync
                        // sequence that owns `user_data`; it must not run after the
                        // C++ owner is destroyed. Skip (never panic across FFI) if the
                        // id contains an interior NUL.
                        let Ok(id_c) = CString::new(id) else {
                            return;
                        };
                        let ud = ud_raw as *mut c_void;
                        cb(ud, id_c.as_ptr(), enabled, deleted);
                    },
                )));
            } else {
                core.register_extension_sync_callback(None);
            }
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_key` must be a valid null-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_register_extension_sync_callback_for_profile(
    ptr: *mut MahoCore,
    profile_key: *const c_char,
    callback: Option<extern "C" fn(*mut c_void, *const c_char, bool, bool)>,
    user_data: *mut c_void,
) {
    ffi_safe!(
        {
            if ptr.is_null() || profile_key.is_null() {
                return;
            }
            // SAFETY(H8): gate on import to avoid a data race on MahoCore, matching
            // the other &mut *ptr FFIs.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let profile_key = match CStr::from_ptr(profile_key).to_str() {
                Ok(value) => value,
                Err(_) => return,
            };
            if let Some(cb) = callback {
                let ud_raw = user_data as usize;
                core.register_extension_sync_callback_for_profile(
                    profile_key,
                    Some(Box::new(move |id, enabled, deleted| {
                        // SAFETY(H8): the sync callback is invoked on the UI/sync
                        // sequence that owns `user_data`; it must not run after the
                        // C++ owner is destroyed. Skip (never panic across FFI) if the
                        // id contains an interior NUL.
                        let Ok(id_c) = CString::new(id) else {
                            return;
                        };
                        let ud = ud_raw as *mut c_void;
                        cb(ud, id_c.as_ptr(), enabled, deleted);
                    })),
                );
            } else {
                core.register_extension_sync_callback_for_profile(profile_key, None);
            }
        },
        ()
    )
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_register_side_panel(
    ptr: *mut MahoCore,
    extension_id: *const c_char,
    path: *const c_char,
    layout: *const c_char,
    default_width: i32,
) {
    ffi_safe!(
        {
            if ptr.is_null() || extension_id.is_null() || path.is_null() || layout.is_null() {
                return;
            }
            let core = &mut *ptr;
            let ext_id = CStr::from_ptr(extension_id).to_string_lossy();
            let p = CStr::from_ptr(path).to_string_lossy();
            let lay_str = CStr::from_ptr(layout).to_string_lossy();
            let layout_enum = if lay_str == "left" {
                maho_core::extension_bridge::SidePanelLayout::Left
            } else {
                maho_core::extension_bridge::SidePanelLayout::Right
            };
            core.register_side_panel(
                &ext_id,
                maho_core::extension_bridge::SidePanelOptions {
                    path: p.to_string(),
                    layout: layout_enum,
                    default_width,
                },
            );
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_key`, `extension_id`, `path`, and `layout` must be valid null-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_register_side_panel_for_profile(
    ptr: *mut MahoCore,
    profile_key: *const c_char,
    extension_id: *const c_char,
    path: *const c_char,
    layout: *const c_char,
    default_width: i32,
) {
    ffi_safe!(
        {
            if ptr.is_null()
                || profile_key.is_null()
                || extension_id.is_null()
                || path.is_null()
                || layout.is_null()
            {
                return;
            }
            let core = &mut *ptr;
            let profile_key = match CStr::from_ptr(profile_key).to_str() {
                Ok(value) => value,
                Err(_) => return,
            };
            let ext_id = CStr::from_ptr(extension_id).to_string_lossy();
            let p = CStr::from_ptr(path).to_string_lossy();
            let lay_str = CStr::from_ptr(layout).to_string_lossy();
            let layout_enum = if lay_str == "left" {
                maho_core::extension_bridge::SidePanelLayout::Left
            } else {
                maho_core::extension_bridge::SidePanelLayout::Right
            };
            core.register_side_panel_for_profile(
                profile_key,
                &ext_id,
                maho_core::extension_bridge::SidePanelOptions {
                    path: p.to_string(),
                    layout: layout_enum,
                    default_width,
                },
            );
        },
        ()
    )
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_unregister_side_panel(
    ptr: *mut MahoCore,
    extension_id: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || extension_id.is_null() {
                return;
            }
            let core = &mut *ptr;
            let ext_id = CStr::from_ptr(extension_id).to_string_lossy();
            core.unregister_side_panel(&ext_id);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_key` and `extension_id` must be valid null-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_unregister_side_panel_for_profile(
    ptr: *mut MahoCore,
    profile_key: *const c_char,
    extension_id: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || profile_key.is_null() || extension_id.is_null() {
                return;
            }
            let core = &mut *ptr;
            let profile_key = match CStr::from_ptr(profile_key).to_str() {
                Ok(value) => value,
                Err(_) => return,
            };
            let ext_id = CStr::from_ptr(extension_id).to_string_lossy();
            core.unregister_side_panel_for_profile(profile_key, &ext_id);
        },
        ()
    )
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_get_side_panel_options(
    ptr: *mut MahoCore,
    extension_id: *const c_char,
    out_path: *mut *mut c_char,
    out_layout: *mut *mut c_char,
    out_default_width: *mut i32,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || extension_id.is_null() {
                return false;
            }
            let core = &*ptr;
            let ext_id = CStr::from_ptr(extension_id).to_string_lossy();
            if let Some(opt) = core.get_side_panel_options(ext_id.as_ref()) {
                if !out_path.is_null() {
                    if let Ok(p_cstr) = CString::new(opt.path.clone()) {
                        *out_path = p_cstr.into_raw();
                    }
                }
                if !out_layout.is_null() {
                    let lay_str = match opt.layout {
                        maho_core::extension_bridge::SidePanelLayout::Left => "left",
                        maho_core::extension_bridge::SidePanelLayout::Right => "right",
                    };
                    if let Ok(lay_cstr) = CString::new(lay_str) {
                        *out_layout = lay_cstr.into_raw();
                    }
                }
                if !out_default_width.is_null() {
                    *out_default_width = opt.default_width;
                }
                true
            } else {
                false
            }
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_key` and `extension_id` must be valid null-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_side_panel_options_for_profile(
    ptr: *mut MahoCore,
    profile_key: *const c_char,
    extension_id: *const c_char,
    out_path: *mut *mut c_char,
    out_layout: *mut *mut c_char,
    out_default_width: *mut i32,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || profile_key.is_null() || extension_id.is_null() {
                return false;
            }
            let core = &*ptr;
            let profile_key = match CStr::from_ptr(profile_key).to_str() {
                Ok(value) => value,
                Err(_) => return false,
            };
            let ext_id = CStr::from_ptr(extension_id).to_string_lossy();
            if let Some(opt) = core.get_side_panel_options_for_profile(profile_key, ext_id.as_ref())
            {
                if !out_path.is_null() {
                    if let Ok(p_cstr) = CString::new(opt.path.clone()) {
                        *out_path = p_cstr.into_raw();
                    }
                }
                if !out_layout.is_null() {
                    let lay_str = match opt.layout {
                        maho_core::extension_bridge::SidePanelLayout::Left => "left",
                        maho_core::extension_bridge::SidePanelLayout::Right => "right",
                    };
                    if let Ok(lay_cstr) = CString::new(lay_str) {
                        *out_layout = lay_cstr.into_raw();
                    }
                }
                if !out_default_width.is_null() {
                    *out_default_width = opt.default_width;
                }
                true
            } else {
                false
            }
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free` if not null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_boosts_for_url(
    ptr: *mut MahoCore,
    url: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || url.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let url_str = match CStr::from_ptr(url).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            to_json_cstring(&core.get_boosts_for_url(url_str))
        },
        ptr::null_mut()
    )
}

/// Returns JSON array of Boost objects for the given domain (Phase 1 API).
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `domain` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_list_for_domain(
    ptr: *mut MahoCore,
    domain: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || domain.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let domain_str = match CStr::from_ptr(domain).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            to_json_cstring(&core.list_boosts_for_domain(domain_str))
        },
        ptr::null_mut()
    )
}

/// Returns JSON Boost object for the given boost ID, or null if not found.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `boost_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_get(
    ptr: *mut MahoCore,
    boost_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || boost_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let id_str = match CStr::from_ptr(boost_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
            match core.get_boost_by_id(&bid) {
                Some(boost) => to_json_cstring(&boost),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Creates a new Boost for the given domain and name. Returns the created Boost as JSON.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `domain` and `name` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_create(
    ptr: *mut MahoCore,
    domain: *const c_char,
    name: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || domain.is_null() || name.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let domain_str = match CStr::from_ptr(domain).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let name_str = match CStr::from_ptr(name).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let boost = core.create_boost_persisted(domain_str.to_string(), name_str.to_string());
            to_json_cstring(&boost)
        },
        ptr::null_mut()
    )
}

/// Updates a Boost by ID with partial changes (JSON BoostUpdate patch).
/// Returns the updated Boost as JSON, or null if not found.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `boost_id` and `changes_json` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_update(
    ptr: *mut MahoCore,
    boost_id: *const c_char,
    changes_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || boost_id.is_null() || changes_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(boost_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let changes_str = match CStr::from_ptr(changes_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let changes: maho_types::boost::BoostUpdate = match serde_json::from_str(changes_str) {
                Ok(c) => c,
                Err(e) => {
                    eprintln!(
                        "[MahoBoost] maho_core_boost_update: serde failed: {e} json={changes_str}"
                    );
                    return ptr::null_mut();
                }
            };
            let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
            core.update_boost_persisted(&bid, changes)
                .map(|b| to_json_cstring(&b))
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// Deletes a Boost by ID. Returns true if deleted, false if not found.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `boost_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_delete(
    ptr: *mut MahoCore,
    boost_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || boost_id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(boost_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
            core.delete_boost_persisted(&bid).is_some()
        },
        false
    )
}

/// Sets the active Boost for a domain. Pass null `boost_id` to clear the active boost.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `domain` must be a valid null-terminated C string.
/// `boost_id` may be null to clear.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_set_active(
    ptr: *mut MahoCore,
    domain: *const c_char,
    boost_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || domain.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let domain_str = match CStr::from_ptr(domain).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let active_boost_id = if boost_id.is_null() {
                None
            } else {
                match CStr::from_ptr(boost_id).to_str() {
                    Ok(s) => Some(maho_types::identifiers::BoostId::new(s.to_string())),
                    Err(_) => return false,
                }
            };
            core.set_active_boost_persisted(domain_str, active_boost_id)
        },
        false
    )
}

/// Returns the active Boost for a domain as JSON, or null if none is set.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `domain` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_get_active(
    ptr: *mut MahoCore,
    domain: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || domain.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let domain_str = match CStr::from_ptr(domain).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.get_active_boost_for_domain(domain_str) {
                Some(boost) => to_json_cstring(&boost),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Returns the composed CSS string for the given Boost ID, or null if not found.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `boost_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_compose_css(
    ptr: *mut MahoCore,
    boost_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || boost_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let id_str = match CStr::from_ptr(boost_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
            match core.compose_css_for_boost(&bid, None) {
                Some(css) if !css.is_empty() => match CString::new(css) {
                    Ok(s) => s.into_raw(),
                    Err(_) => ptr::null_mut(),
                },
                _ => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Creates a temporary boost for the given domain, sets it active, and persists.
/// Returns the created Boost as JSON. Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `domain` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_create_temp(
    ptr: *mut MahoCore,
    domain: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || domain.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let domain_str = match CStr::from_ptr(domain).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let boost = core.create_temp_boost_persisted(domain_str.to_string());
            to_json_cstring(&boost)
        },
        ptr::null_mut()
    )
}

/// Commits a temporary boost (marks it permanent). Returns the committed Boost as JSON.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `boost_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_commit(
    ptr: *mut MahoCore,
    boost_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || boost_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(boost_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
            match core.commit_boost_persisted(&bid) {
                Some(boost) => to_json_cstring(&boost),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Discards a temporary boost. Returns the previously-active boost ID as a JSON string,
/// or null if none. Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `boost_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_discard(
    ptr: *mut MahoCore,
    boost_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || boost_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(boost_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
            match core.discard_boost_persisted(&bid) {
                Some(prev_id) => {
                    let id_string = prev_id
                        .as_ref()
                        .map_or_else(String::new, |id| id.as_ref().to_string());
                    match CString::new(id_string) {
                        Ok(s) => s.into_raw(),
                        Err(_) => ptr::null_mut(),
                    }
                }
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Shuffles boost colors randomly. Returns the updated Boost as JSON.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `boost_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_shuffle(
    ptr: *mut MahoCore,
    boost_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || boost_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(boost_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
            match core.shuffle_boost_persisted(&bid) {
                Some(boost) => to_json_cstring(&boost),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Resets a boost to defaults. Returns the updated Boost as JSON.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `boost_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_reset(
    ptr: *mut MahoCore,
    boost_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || boost_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(boost_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
            match core.reset_boost_persisted(&bid) {
                Some(boost) => to_json_cstring(&boost),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Exports a boost as a JSON string for sharing/backup.
/// Returns the export JSON string, or null if not found.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `boost_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_export(
    ptr: *mut MahoCore,
    boost_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || boost_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let id_str = match CStr::from_ptr(boost_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
            match core.export_boost(&bid) {
                Some(json) => match CString::new(json) {
                    Ok(s) => s.into_raw(),
                    Err(_) => ptr::null_mut(),
                },
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Imports a boost from a JSON string into the given domain.
/// Returns the newly-created Boost as JSON, or null on failure.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `domain` and `json` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_import(
    ptr: *mut MahoCore,
    domain: *const c_char,
    json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || domain.is_null() || json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let domain_str = match CStr::from_ptr(domain).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let json_str = match CStr::from_ptr(json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.import_boost_persisted(domain_str.to_string(), json_str) {
                Some(boost) => to_json_cstring(&boost),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Appends a zap selector to a boost. Returns the updated Boost as JSON.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `boost_id` and `selector` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_append_zap(
    ptr: *mut MahoCore,
    boost_id: *const c_char,
    selector: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || boost_id.is_null() || selector.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(boost_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let sel_str = match CStr::from_ptr(selector).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
            match core.append_zap_selector_persisted(&bid, sel_str.to_string()) {
                Some(boost) => to_json_cstring(&boost),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Removes a zap selector from a boost. Returns the updated Boost as JSON.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `boost_id` and `selector` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_boost_remove_zap(
    ptr: *mut MahoCore,
    boost_id: *const c_char,
    selector: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || boost_id.is_null() || selector.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(boost_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let sel_str = match CStr::from_ptr(selector).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
            match core.remove_zap_selector_persisted(&bid, sel_str) {
                Some(boost) => to_json_cstring(&boost),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_note_view_models(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            to_json_cstring(&core.get_note_view_models())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_download_view_models(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            to_json_cstring(&core.get_download_view_models())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_settings(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            to_json_cstring(core.get_settings())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_settings_view_model(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            to_json_cstring(&core.get_settings_view_model())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_find_bar_state(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            to_json_cstring(&core.get_find_bar_state())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid null-terminated C string containing a SpaceId JSON.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_folder_view_models(
    ptr: *mut MahoCore,
    space_id_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || space_id_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let json_str = match CStr::from_ptr(space_id_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let space_id = match serde_json::from_str(json_str) {
                Ok(id) => id,
                Err(_) => return ptr::null_mut(),
            };
            to_json_cstring(&core.get_folder_view_models(&space_id))
        },
        ptr::null_mut()
    )
}

/// # Safety
/// All pointers must be valid C strings. `ptr` must be from `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_should_block_request(
    ptr: *mut MahoCore,
    url: *const c_char,
    source_url: *const c_char,
    request_type: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || url.is_null() || source_url.is_null() || request_type.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &*ptr;
            let url_str = match CStr::from_ptr(url).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let source_str = match CStr::from_ptr(source_url).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let type_str = match CStr::from_ptr(request_type).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.should_block_request(url_str, source_str, type_str)
        },
        false
    )
}

/// Outcome of a content-blocking check, returned by value.
///
/// `redirect` and `rewritten_url` are null when absent or empty. When non-null
/// the caller owns them and must release each with `maho_string_free`.
///
/// Deliberately a POD struct rather than JSON: this check runs on the browser
/// UI thread for EVERY subresource request, and a JSON round trip there cost a
/// serialize in Rust, a heap `CString`, a `base::JSONReader` parse in C++ and a
/// free — per request. The overwhelmingly common outcome (allowed, no rewrite)
/// now allocates nothing at all.
#[repr(C)]
pub struct MahoBlockResult {
    pub blocked: bool,
    pub redirect: *mut c_char,
    pub rewritten_url: *mut c_char,
}

impl MahoBlockResult {
    /// Allow-with-no-rewrite. Also the fail-open value for every error path, so
    /// a malformed request or an in-progress import never blocks a page load.
    fn allow() -> Self {
        Self {
            blocked: false,
            redirect: ptr::null_mut(),
            rewritten_url: ptr::null_mut(),
        }
    }
}

/// `None` and empty strings both become null; a NUL-containing string degrades
/// to null rather than failing the request.
fn optional_cstring_raw(value: Option<&str>) -> *mut c_char {
    match value {
        Some(s) if !s.is_empty() => CString::new(s)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        _ => ptr::null_mut(),
    }
}

/// # Safety
/// All pointers must be valid C strings. `ptr` must be from `maho_core_new`.
/// See `MahoBlockResult` for ownership of the returned strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_check_request(
    ptr: *mut MahoCore,
    url: *const c_char,
    source_url: *const c_char,
    request_type: *const c_char,
) -> MahoBlockResult {
    ffi_safe!(
        {
            if ptr.is_null() || url.is_null() || source_url.is_null() || request_type.is_null() {
                return MahoBlockResult::allow();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return MahoBlockResult::allow();
            }
            let core = &*ptr;
            let url_str = match CStr::from_ptr(url).to_str() {
                Ok(s) => s,
                Err(_) => return MahoBlockResult::allow(),
            };
            let source_str = match CStr::from_ptr(source_url).to_str() {
                Ok(s) => s,
                Err(_) => return MahoBlockResult::allow(),
            };
            let type_str = match CStr::from_ptr(request_type).to_str() {
                Ok(s) => s,
                Err(_) => return MahoBlockResult::allow(),
            };
            let result = core.check_request(url_str, source_str, type_str);
            MahoBlockResult {
                blocked: result.blocked,
                redirect: optional_cstring_raw(result.redirect.as_deref()),
                rewritten_url: optional_cstring_raw(result.rewritten_url.as_deref()),
            }
        },
        MahoBlockResult::allow()
    )
}

/// # Safety
/// `input` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_normalize_site_exception_key(input: *const c_char) -> *mut c_char {
    ffi_safe!(
        {
            if input.is_null() {
                return ptr::null_mut();
            }
            // SAFETY: the public FFI entrypoint requires `input` to be a live C string.
            let input_str = match unsafe { CStr::from_ptr(input) }.to_str() {
                Ok(value) => value,
                Err(_) => return ptr::null_mut(),
            };
            match CString::new(normalize_site_exception_key(input_str)) {
                Ok(value) => value.into_raw(),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `origin` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_site_exception(ptr: *mut MahoCore, origin: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() || origin.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let origin_str = match CStr::from_ptr(origin).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.add_content_blocker_site_exception(origin_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `origin` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_site_exception(
    ptr: *mut MahoCore,
    origin: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || origin.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let origin_str = match CStr::from_ptr(origin).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.remove_content_blocker_site_exception(origin_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_site_exceptions(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let exceptions = core.get_content_blocker_site_exceptions();
            to_json_cstring(&exceptions)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_content_rules(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let rules = core.get_content_rules();
            match CString::new(rules) {
                Ok(c_str) => c_str.into_raw(),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Returns cosmetic filter resources for a URL as JSON.
/// The JSON has fields: hideSelectors (string[]), injectedScript (string|null), generichide (bool).
///
/// # Safety
/// All pointers must be valid C strings. `ptr` must be from `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_cosmetic_resources(
    ptr: *mut MahoCore,
    url: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || url.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let url_str = match CStr::from_ptr(url).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let resources = core.get_cosmetic_resources(url_str);
            to_json_cstring(&resources)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be from `maho_core_new`. `path` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_save_content_engine_cache(
    ptr: *mut MahoCore,
    path: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || path.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &*ptr;
            let path_str = match CStr::from_ptr(path).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.save_content_engine_cache(path_str).is_ok()
        },
        false
    )
}

/// # Safety
/// `ptr` must be from `maho_core_new`. `path` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_load_content_engine_cache(
    ptr: *mut MahoCore,
    path: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || path.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let path_str = match CStr::from_ptr(path).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.load_content_engine_cache(path_str).is_ok()
        },
        false
    )
}

/// Returns current content blocking mode:
/// `0` = Native, `1` = Extension, `2` = Disabled,
/// `3` = Unknown (an unrecognized/forward-incompatible persisted mode; treated
/// as non-native so native blocking is never silently enabled),
/// `-1` = Error (null core or import in progress).
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_content_blocking_mode(ptr: *mut MahoCore) -> i32 {
    ffi_safe!(
        {
            if ptr.is_null() || import_gate::is_active() {
                return -1;
            }
            let core = &*ptr;
            match core.get_content_blocker_mode() {
                maho_types::content_blocking::ContentBlockingMode::Native => 0,
                maho_types::content_blocking::ContentBlockingMode::Extension => 1,
                maho_types::content_blocking::ContentBlockingMode::Disabled => 2,
                maho_types::content_blocking::ContentBlockingMode::Unknown => 3,
            }
        },
        -1
    )
}

/// Sets content blocking mode: 0 = Native, 1 = Extension, 2 = Disabled.
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_content_blocking_mode(
    ptr: *mut MahoCore,
    mode_code: i32,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || import_gate::is_active() {
                return false;
            }
            let mode = match mode_code {
                0 => maho_types::content_blocking::ContentBlockingMode::Native,
                1 => maho_types::content_blocking::ContentBlockingMode::Extension,
                2 => maho_types::content_blocking::ContentBlockingMode::Disabled,
                _ => return false,
            };
            let core = &mut *ptr;
            core.set_content_blocking_mode(mode);
            true
        },
        false
    )
}

/// Returns full ContentBlockerStateDto JSON.
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_content_blocker_state_json(
    ptr: *mut MahoCore,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let dto = core.get_content_blocker_state_dto();
            to_json_cstring(&dto)
        },
        ptr::null_mut()
    )
}

/// Applies a FilterListUpdateResponse JSON to the content blocker.
///
/// Returns `true` when a rebuild/compile is needed: a 200 response produced a
/// pending candidate that the caller must compile and install via
/// `maho_core_create_compile_snapshot` + `maho_compile_engine_from_snapshot` +
/// `maho_content_engine_install`. Returns `false` for a 304/unchanged poll (no
/// work to do) and on any rejection (null args, import active, invalid UTF-8,
/// invalid JSON, oversized/failed update). A pending candidate is never active
/// until a matching compiled engine installs.
/// # Safety
/// `ptr` must be from `maho_core_new`. `update_json` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_apply_filter_list_update_json(
    ptr: *mut MahoCore,
    update_json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            let result = apply_filter_list_update_result(ptr, update_json);
            result.success && result.compile_required
        },
        false
    )
}

fn apply_filter_list_update_result(
    ptr: *mut MahoCore,
    update_json: *const c_char,
) -> ContentBlockerMutationResult {
    if ptr.is_null() || update_json.is_null() {
        return content_blocker_result(
            None,
            false,
            Some(content_blocker_error(
                "invalid_argument",
                "core and update must be non-null",
            )),
            false,
        );
    }
    if import_gate::is_active() {
        return content_blocker_result(
            None,
            false,
            Some(content_blocker_error(
                "import_in_progress",
                "content blocker is unavailable during import",
            )),
            false,
        );
    }
    // SAFETY: the public FFI entrypoint requires `update_json` to be a live C string.
    let json_str = match unsafe { CStr::from_ptr(update_json) }.to_str() {
        Ok(value) => value,
        Err(_) => {
            // SAFETY: `ptr` was checked non-null and is borrowed only for this result.
            let core = unsafe { &*ptr };
            return content_blocker_result(
                Some(core),
                false,
                Some(content_blocker_error(
                    "invalid_utf8",
                    "update payload is not valid UTF-8",
                )),
                false,
            );
        }
    };
    let response: maho_types::content_blocking::FilterListUpdateResponse =
        match serde_json::from_str(json_str) {
            Ok(value) => value,
            Err(error) => {
                // SAFETY: `ptr` was checked non-null and is borrowed only for this result.
                let core = unsafe { &*ptr };
                return content_blocker_result(
                    Some(core),
                    false,
                    Some(content_blocker_error("invalid_json", error.to_string())),
                    false,
                );
            }
        };
    // SAFETY: the FFI contract requires exclusive ownership of the live core handle.
    let core = unsafe { &mut *ptr };
    match core.apply_filter_list_update_response(response) {
        Ok(compile_required) => content_blocker_result(Some(core), true, None, compile_required),
        Err(error) => content_blocker_result(
            Some(core),
            false,
            Some(content_blocker_error_from_update(error)),
            false,
        ),
    }
}

/// Applies a candidate update and returns the authoritative content-blocker state.
///
/// # Safety
/// `ptr` must be from `maho_core_new`. `update_json` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_apply_filter_list_update_result_json(
    ptr: *mut MahoCore,
    update_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        { to_json_cstring(&apply_filter_list_update_result(ptr, update_json)) },
        ptr::null_mut()
    )
}

pub type OpaqueCompiledEngineHandle = maho_core::content_blocker::OpaqueCompiledEngine;
pub type OpaqueCompileSnapshotHandle = maho_core::content_blocker::CompileInputSnapshot;

/// Creates a light compile snapshot on UI thread.
/// # Safety
/// `ptr` must be from `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_compile_snapshot(
    ptr: *mut MahoCore,
) -> *mut OpaqueCompileSnapshotHandle {
    ffi_safe!(
        {
            if ptr.is_null() || import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let snapshot = core.create_content_blocker_compile_snapshot();
            Box::into_raw(Box::new(snapshot))
        },
        ptr::null_mut()
    )
}

/// Compiles an OpaqueCompiledEngine handle off-thread from a snapshot. Frees snapshot handle.
/// # Safety
/// `snapshot` must be from `maho_core_create_compile_snapshot`.
#[no_mangle]
pub unsafe extern "C" fn maho_compile_engine_from_snapshot(
    snapshot: *mut OpaqueCompileSnapshotHandle,
) -> *mut OpaqueCompiledEngineHandle {
    ffi_safe!(
        {
            if snapshot.is_null() {
                return ptr::null_mut();
            }
            let snap = *Box::from_raw(snapshot);
            let compiled = maho_core::content_blocker::compile_engine_snapshot(snap);
            Box::into_raw(Box::new(compiled))
        },
        ptr::null_mut()
    )
}

/// Frees an uncompiled snapshot handle.
/// # Safety
/// `snapshot` must be from `maho_core_create_compile_snapshot` or null.
#[no_mangle]
pub unsafe extern "C" fn maho_compile_snapshot_free(snapshot: *mut OpaqueCompileSnapshotHandle) {
    ffi_safe!(
        {
            if !snapshot.is_null() {
                let _ = Box::from_raw(snapshot);
            }
        },
        ()
    )
}

/// Creates a snapshot on UI and compiles an OpaqueCompiledEngine handle.
/// # Safety
/// `ptr` must be from `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_content_engine_compile_snapshot(
    ptr: *mut MahoCore,
) -> *mut OpaqueCompiledEngineHandle {
    ffi_safe!(
        {
            if ptr.is_null() || import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let snapshot = core.create_content_blocker_compile_snapshot();
            let compiled = maho_core::content_blocker::compile_engine_snapshot(snapshot);
            Box::into_raw(Box::new(compiled))
        },
        ptr::null_mut()
    )
}

/// Installs an OpaqueCompiledEngine handle into MahoCore if generation and hash match. Frees handle.
/// # Safety
/// `ptr` must be from `maho_core_new`. `handle` must be from `maho_content_engine_compile_snapshot`.
#[no_mangle]
pub unsafe extern "C" fn maho_content_engine_install(
    ptr: *mut MahoCore,
    handle: *mut OpaqueCompiledEngineHandle,
) -> bool {
    ffi_safe!(
        { install_content_engine_result(ptr, handle).success },
        false
    )
}

fn install_content_engine_result(
    ptr: *mut MahoCore,
    handle: *mut OpaqueCompiledEngineHandle,
) -> ContentBlockerMutationResult {
    if handle.is_null() {
        return content_blocker_result(
            None,
            false,
            Some(content_blocker_error(
                "invalid_argument",
                "compiled engine handle must be non-null",
            )),
            false,
        );
    }
    // SAFETY: ownership of the non-null opaque handle is transferred to this call.
    let compiled = unsafe { *Box::from_raw(handle) };
    if ptr.is_null() {
        return content_blocker_result(
            None,
            false,
            Some(content_blocker_error(
                "invalid_argument",
                "core must be non-null",
            )),
            false,
        );
    }
    if import_gate::is_active() {
        // SAFETY: `ptr` was checked non-null and is borrowed only for this result.
        let core = unsafe { &*ptr };
        return content_blocker_result(
            Some(core),
            false,
            Some(content_blocker_error(
                "import_in_progress",
                "content blocker is unavailable during import",
            )),
            false,
        );
    }
    // SAFETY: the FFI contract requires exclusive ownership of the live core handle.
    let core = unsafe { &mut *ptr };
    let installed = core.install_content_blocker_compiled_engine(compiled);
    let error = (!installed).then(|| {
        content_blocker_error(
            "install_rejected",
            "compiled engine was stale or could not be persisted",
        )
    });
    content_blocker_result(Some(core), installed, error, false)
}

/// Installs an opaque compiled engine and returns the authoritative promotion result.
/// The handle is consumed exactly once even when install is rejected.
///
/// # Safety
/// `ptr` must be from `maho_core_new`; `handle` must be a live compiled-engine handle.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_content_engine_install_result_json(
    ptr: *mut MahoCore,
    handle: *mut OpaqueCompiledEngineHandle,
) -> *mut c_char {
    ffi_safe!(
        { to_json_cstring(&install_content_engine_result(ptr, handle)) },
        ptr::null_mut()
    )
}

/// Frees an uninstalled OpaqueCompiledEngine handle.
/// # Safety
/// `handle` must be from `maho_content_engine_compile_snapshot` or null.
#[no_mangle]
pub unsafe extern "C" fn maho_content_engine_free(handle: *mut OpaqueCompiledEngineHandle) {
    ffi_safe!(
        {
            if !handle.is_null() {
                let _ = Box::from_raw(handle);
            }
        },
        ()
    )
}

/// # Safety
/// `url` and `title` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_history_entry(
    ptr: *mut MahoCore,
    url: *const c_char,
    title: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || url.is_null() || title.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let url_str = match CStr::from_ptr(url).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let title_str = match CStr::from_ptr(title).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.add_history_entry(url_str, title_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_search_history(
    ptr: *mut MahoCore,
    query: *const c_char,
    limit: usize,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || query.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let query_str = match CStr::from_ptr(query).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let results = core.search_history(query_str, limit);
            to_json_cstring(&results)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated C string.
/// `tabs_json` must be a valid null-terminated C string containing a JSON array of
/// `[{"id":"...", "title":"...", "url":"..."}]`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_command_bar_search(
    ptr: *mut MahoCore,
    query: *const c_char,
    mode: *const c_char,
    tabs_json: *const c_char,
    is_incognito: bool,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || query.is_null() || tabs_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let query_str = match CStr::from_ptr(query).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let mode_owned = if mode.is_null() {
                None
            } else {
                match CStr::from_ptr(mode).to_str() {
                    Ok(s) if !s.is_empty() => Some(s.to_owned()),
                    _ => None,
                }
            };
            let mode_str = mode_owned.as_deref();
            let tabs_str = match CStr::from_ptr(tabs_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };

            #[derive(serde::Deserialize)]
            struct SimpleTab {
                id: String,
                title: String,
                url: String,
                #[serde(default)]
                #[serde(alias = "icon")]
                favicon: Option<maho_types::common::ImageData>,
            }

            let simple_tabs: Vec<SimpleTab> = serde_json::from_str(tabs_str).unwrap_or_default();
            let tabs = simple_tabs
                .into_iter()
                .map(|t| {
                    use maho_types::common::DateTime;
                    use maho_types::identifiers::{SpaceId, TabId};
                    use maho_types::traits::shell_renderer::TabViewModel;
                    TabViewModel {
                        id: TabId::new(&t.id),
                        space_id: SpaceId::default(),
                        title: t.title,
                        custom_title: None,
                        custom_icon: None,
                        pinned_url: None,
                        url: t.url,
                        favicon: t.favicon.clone(),
                        is_loading: false,
                        is_pinned: false,
                        is_favorite: false, // L3-EXEMPT
                        favorite_order: None,
                        role: maho_types::tab::TabRole::Normal,
                        is_private: false,
                        is_muted: false,
                        is_playing_audio: false,
                        lifecycle_state: "active".to_string(),
                        children: Vec::new(),
                        created_at: DateTime::now(),
                        last_active_at: DateTime::now(),
                    }
                })
                .collect();

            let results =
                core.command_bar_search_with_tabs(query_str, mode_str, tabs, is_incognito);
            to_json_cstring(&results)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_clear_history(ptr: *mut MahoCore) {
    ffi_safe!(
        {
            if ptr.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            core.clear_history();
        },
        ()
    )
}

/// # Safety
/// `key_hex` must be a valid null-terminated C string (hex-encoded key).
/// Call once, before `maho_core_new_with_storage`.
#[no_mangle]

#[no_mangle]
pub unsafe extern "C" fn maho_core_new_with_storage(path: *const c_char) -> *mut MahoCore {
    ffi_safe!(
        {
            if path.is_null() {
                return ptr::null_mut();
            }
            let path_str = match CStr::from_ptr(path).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let base_path = Path::new(path_str);

            let lmdb_path = base_path.join("state");
            if std::fs::create_dir_all(&lmdb_path).is_err() {
                return ptr::null_mut();
            }

            let sqlite_path = base_path.join("maho.db");
            let sqlite_path_str = match sqlite_path.to_str() {
                Some(s) => s,
                None => return ptr::null_mut(),
            };

            let mut core = MahoCore::new()
                .with_storage(sqlite_path_str)
                .with_lmdb_storage(&lmdb_path);

            let skills_dir = base_path.join("maho_config").join("skills");
            if let Err(e) = core.load_skills_from_dir(&skills_dir) {
                tracing::warn!("[maho_core_new_with_storage] failed to load skills: {e}");
            }

            Box::into_raw(Box::new(core))
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new` or
/// `maho_core_new_with_storage`. `path` must be a valid null-terminated C string
/// pointing to a verified JSON config blob.
#[no_mangle]
pub unsafe extern "C" fn maho_core_apply_config(core: *mut MahoCore, path: *const c_char) -> bool {
    ffi_safe!(
        {
            if core.is_null() || path.is_null() {
                return false;
            }
            if import_gate::is_active() {
                return false;
            }

            let core = &mut *core;
            let path_str = match CStr::from_ptr(path).to_str() {
                Ok(s) => s,
                Err(e) => {
                    tracing::warn!("[maho_core_apply_config] config path is not valid UTF-8: {e}");
                    return false;
                }
            };
            let config_path = Path::new(path_str);
            if !config_path.is_file() {
                tracing::warn!(
                    "[maho_core_apply_config] config path is not a file: {config_path:?}"
                );
                return false;
            }

            match core.load_skills_from_dir(config_path) {
                Ok(()) => true,
                Err(e) => {
                    tracing::warn!("[maho_core_apply_config] failed to apply config: {e}");
                    false
                }
            }
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
/// Returns 1 on success, 0 on error.
#[no_mangle]
pub unsafe extern "C" fn maho_core_save_state(ptr: *mut MahoCore) -> i32 {
    ffi_safe!(
        {
            if ptr.is_null() {
                return 0;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return 0;
            }
            let core = &*ptr;
            match core.save_state() {
                Ok(_) => 1,
                Err(_) => 0,
            }
        },
        0
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
/// Returns 1 on success, 0 on error.
#[no_mangle]
pub unsafe extern "C" fn maho_core_load_state(ptr: *mut MahoCore) -> i32 {
    ffi_safe!(
        {
            if ptr.is_null() {
                return 0;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return 0;
            }
            let core = &mut *ptr;
            match core.load_state() {
                Ok(_) => 1,
                Err(_) => 0,
            }
        },
        0
    )
}


#[no_mangle]
pub unsafe extern "C" fn maho_core_add_bookmark(
    ptr: *mut MahoCore,
    url: *const c_char,
    title: *const c_char,
    folder_id: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || url.is_null() || title.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let url_str = match CStr::from_ptr(url).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let title_str = match CStr::from_ptr(title).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let folder_id_opt = if folder_id.is_null() {
                None
            } else {
                CStr::from_ptr(folder_id).to_str().ok()
            };
            core.add_bookmark(url_str, title_str, folder_id_opt);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `bookmark_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_bookmark(ptr: *mut MahoCore, bookmark_id: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() || bookmark_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let bookmark_id_str = match CStr::from_ptr(bookmark_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.remove_bookmark(bookmark_id_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_search_bookmarks(
    ptr: *mut MahoCore,
    query: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || query.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let query_str = match CStr::from_ptr(query).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            to_json_cstring(&core.search_bookmarks(query_str))
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `bookmark_id` and `folder_id` must be valid null-terminated C strings.
/// `folder_id` may be null to move to root.
#[no_mangle]
pub unsafe extern "C" fn maho_core_move_bookmark(
    ptr: *mut MahoCore,
    bookmark_id: *const c_char,
    folder_id: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || bookmark_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let bookmark_id_str = match CStr::from_ptr(bookmark_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let folder_id_opt = if folder_id.is_null() {
                None
            } else {
                CStr::from_ptr(folder_id).to_str().ok()
            };
            core.move_bookmark(bookmark_id_str, folder_id_opt);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `origin` and `permission` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_grant_permission(
    ptr: *mut MahoCore,
    origin: *const c_char,
    permission: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || origin.is_null() || permission.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let origin_str = match CStr::from_ptr(origin).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let permission_str = match CStr::from_ptr(permission).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.grant_permission(origin_str, permission_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `origin` and `permission` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_revoke_permission(
    ptr: *mut MahoCore,
    origin: *const c_char,
    permission: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || origin.is_null() || permission.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let origin_str = match CStr::from_ptr(origin).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let permission_str = match CStr::from_ptr(permission).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.revoke_permission(origin_str, permission_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `origin` and `permission` must be valid null-terminated C strings.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_query_permission(
    ptr: *mut MahoCore,
    origin: *const c_char,
    permission: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || origin.is_null() || permission.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let origin_str = match CStr::from_ptr(origin).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let permission_str = match CStr::from_ptr(permission).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            to_json_cstring(&core.query_permission(origin_str, permission_str))
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_pause_download(ptr: *mut MahoCore, download_id: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() || download_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let download_id_str = match CStr::from_ptr(download_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.pause_download(download_id_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_resume_download(ptr: *mut MahoCore, download_id: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() || download_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let download_id_str = match CStr::from_ptr(download_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.resume_download(download_id_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_cancel_download(ptr: *mut MahoCore, download_id: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() || download_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let download_id_str = match CStr::from_ptr(download_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.cancel_download(download_id_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_download(ptr: *mut MahoCore, download_id: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() || download_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let download_id_str = match CStr::from_ptr(download_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.remove_download(download_id_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `entry_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_history_entry(
    ptr: *mut MahoCore,
    entry_id: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || entry_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let entry_id_str = match CStr::from_ptr(entry_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.delete_history_entry(entry_id_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.

#[no_mangle]
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_all_bookmarks(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let bookmarks: Vec<_> = core
                .get_all_bookmark_entries()
                .iter()
                .map(|b| {
                    serde_json::json!({
                        "id": b.id.0,
                        "title": b.title,
                        "url": b.url,
                        "folderId": b.folder_id,
                        "favicon": b.favicon,
                        "createdAt": b.created_at.to_string(),
                    })
                })
                .collect();
            to_json_cstring(&bookmarks)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `name` must be a valid null-terminated C string.
/// `parent_id` may be null to create a root-level folder.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_bookmark_folder(
    ptr: *mut MahoCore,
    name: *const c_char,
    parent_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || name.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let name_str = match CStr::from_ptr(name).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let parent_id_opt = if parent_id.is_null() {
                None
            } else {
                CStr::from_ptr(parent_id).to_str().ok().map(String::from)
            };
            let folder = core.create_bookmark_folder(name_str.to_string(), parent_id_opt);
            let result = serde_json::json!({
                "id": folder.id,
                "name": folder.name,
                "parentId": folder.parent_id,
                "createdAt": folder.created_at.to_string(),
            });
            to_json_cstring(&result)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `json` must be a valid null-terminated C string containing JSON with
/// `filename`, `url`, `total_bytes`, and optionally `file_path` and `mime_type`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_start_download(
    ptr: *mut MahoCore,
    json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let json_str = match CStr::from_ptr(json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let parsed: serde_json::Value = match serde_json::from_str(json_str) {
                Ok(v) => v,
                Err(_) => return ptr::null_mut(),
            };
            let filename = match parsed["filename"].as_str() {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            let url = match parsed["url"].as_str() {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            let total_bytes = parsed["total_bytes"]
                .as_u64()
                .or_else(|| parsed["total_bytes"].as_f64().map(|f| f as u64))
                .unwrap_or(0);
            let file_path = parsed["file_path"].as_str();
            let mime_type = parsed["mime_type"].as_str();
            let chromium_guid = parsed["chromium_guid"].as_str();
            let download_id = core.start_download(
                filename,
                url,
                total_bytes,
                file_path,
                mime_type,
                chromium_guid,
            );
            to_json_cstring(&download_id)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_download_progress(
    ptr: *mut MahoCore,
    download_id: *const c_char,
    received_bytes: u64,
) {
    ffi_safe!(
        {
            if ptr.is_null() || download_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let download_id_str = match CStr::from_ptr(download_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.update_download_progress(download_id_str, received_bytes);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` and `json` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_download_metadata(
    ptr: *mut MahoCore,
    download_id: *const c_char,
    json: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || download_id.is_null() || json.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent a data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let download_id_str = match CStr::from_ptr(download_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let json_str = match CStr::from_ptr(json).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let parsed: serde_json::Value = match serde_json::from_str(json_str) {
                Ok(v) => v,
                Err(_) => return,
            };
            let total_bytes = parsed["total_bytes"]
                .as_u64()
                .or_else(|| parsed["total_bytes"].as_f64().map(|f| f as u64));
            core.update_download_metadata(
                download_id_str,
                parsed["filename"].as_str(),
                parsed["file_path"].as_str(),
                parsed["mime_type"].as_str(),
                total_bytes,
            );
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `download_id` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_complete_download(
    ptr: *mut MahoCore,
    download_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || download_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let download_id_str = match CStr::from_ptr(download_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let updates = core.complete_download(download_id_str);
            to_json_cstring(&updates)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `guid` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free` if not null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_find_download_by_chromium_guid(
    ptr: *mut MahoCore,
    guid: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || guid.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let guid_str = match CStr::from_ptr(guid).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            if let Some(id_str) = core.find_download_by_chromium_guid(guid_str) {
                to_json_cstring(&id_str)
            } else {
                ptr::null_mut()
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `download_id` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_restore_download_name(
    ptr: *mut MahoCore,
    download_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || download_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let download_id_str = match CStr::from_ptr(download_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            if let Some(update) = core.restore_download_name(download_id_str) {
                to_json_cstring(&vec![update])
            } else {
                to_json_cstring(&Vec::<maho_types::events::core_update::CoreUpdate>::new())
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Caller must free
/// the returned string with `maho_string_free`. Returns null on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_filter_lists(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return std::ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let json = core.get_filter_lists_json();
            match CString::new(json) {
                Ok(s) => s.into_raw(),
                Err(_) => std::ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `id`, `name`, and `url`
/// must be valid NUL-terminated C strings. No-op on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_filter_list(
    ptr: *mut MahoCore,
    id: *const c_char,
    name: *const c_char,
    url: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || id.is_null() || name.is_null() || url.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return,
            };
            let name_str = match CStr::from_ptr(name).to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return,
            };
            let url_str = match CStr::from_ptr(url).to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return,
            };
            let core = &mut *ptr;
            let _ = add_filter_list_result(core, id_str, name_str, url_str);
        },
        ()
    )
}

/// Adds a filter list through the authoritative core mutation path.
///
/// # Safety
/// `ptr` must be from `maho_core_new`; strings must be valid null-terminated UTF-8.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_filter_list_result_json(
    ptr: *mut MahoCore,
    id: *const c_char,
    name: *const c_char,
    url: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || id.is_null() || name.is_null() || url.is_null() {
                return to_json_cstring(&content_blocker_result(
                    None,
                    false,
                    Some(content_blocker_error(
                        "invalid_argument",
                        "core and list fields must be non-null",
                    )),
                    false,
                ));
            }
            if import_gate::is_active() {
                return to_json_cstring(&content_blocker_result(
                    None,
                    false,
                    Some(content_blocker_error(
                        "import_in_progress",
                        "content blocker is unavailable during import",
                    )),
                    false,
                ));
            }
            let id = match CStr::from_ptr(id).to_str() {
                Ok(value) => value.to_string(),
                Err(_) => return ptr::null_mut(),
            };
            let name = match CStr::from_ptr(name).to_str() {
                Ok(value) => value.to_string(),
                Err(_) => return ptr::null_mut(),
            };
            let url = match CStr::from_ptr(url).to_str() {
                Ok(value) => value.to_string(),
                Err(_) => return ptr::null_mut(),
            };
            let core = &mut *ptr;
            to_json_cstring(&add_filter_list_result(core, id, name, url))
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `id` must be a valid
/// NUL-terminated C string. No-op on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_filter_list(
    ptr: *mut MahoCore,
    id: *const c_char,
    enabled: bool,
) {
    ffi_safe!(
        {
            if ptr.is_null() || id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return,
            };
            let core = &mut *ptr;
            let _ = toggle_filter_list_result(core, id_str, enabled);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be from `maho_core_new`; `id` must be a valid null-terminated UTF-8 string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_filter_list_result_json(
    ptr: *mut MahoCore,
    id: *const c_char,
    enabled: bool,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || id.is_null() || import_gate::is_active() {
                return ptr::null_mut();
            }
            let id = match CStr::from_ptr(id).to_str() {
                Ok(value) => value.to_string(),
                Err(_) => return ptr::null_mut(),
            };
            let core = &mut *ptr;
            to_json_cstring(&toggle_filter_list_result(core, id, enabled))
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `id` must be a valid
/// NUL-terminated C string. No-op on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_filter_list(ptr: *mut MahoCore, id: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() || id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return,
            };
            let core = &mut *ptr;
            let _ = remove_filter_list_result(core, id_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be from `maho_core_new`; `id` must be a valid null-terminated UTF-8 string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_filter_list_result_json(
    ptr: *mut MahoCore,
    id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || id.is_null() || import_gate::is_active() {
                return ptr::null_mut();
            }
            let id = match CStr::from_ptr(id).to_str() {
                Ok(value) => value.to_string(),
                Err(_) => return ptr::null_mut(),
            };
            let core = &mut *ptr;
            to_json_cstring(&remove_filter_list_result(core, id))
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `id` and `content` must
/// be valid NUL-terminated C strings. No-op on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_filter_list_content(
    ptr: *mut MahoCore,
    id: *const c_char,
    content: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || id.is_null() || content.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            // Legacy ABI only. Raw content replacement cannot preserve the
            // candidate/promotion contract, so callers must use the typed
            // update-response API and schedule worker compilation from its result.
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. No-op on null input
/// or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_rebuild_content_rules(ptr: *mut MahoCore) {
    ffi_safe!(
        {
            if ptr.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            // Legacy ABI only. Synchronous rebuild would bypass candidate
            // promotion and must remain a no-op.
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Returns `0` on null
/// input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_content_rule_count(ptr: *mut MahoCore) -> usize {
    ffi_safe!(
        {
            if ptr.is_null() {
                return 0;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return 0;
            }
            let core = &*ptr;
            core.get_content_rule_count()
        },
        0
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `extension_id` must be a
/// valid NUL-terminated C string. Returns `false` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_extension(
    ptr: *mut MahoCore,
    extension_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || extension_id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(extension_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.toggle_extension(id_str).is_some()
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`;
/// `profile_key` and `extension_id` must be valid NUL-terminated UTF-8 strings.
/// Returns `false` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_extension_for_profile(
    ptr: *mut MahoCore,
    profile_key: *const c_char,
    extension_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || profile_key.is_null() || extension_id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let profile_key = match CStr::from_ptr(profile_key).to_str() {
                Ok(value) => value,
                Err(_) => return false,
            };
            let id_str = match CStr::from_ptr(extension_id).to_str() {
                Ok(value) => value,
                Err(_) => return false,
            };
            core.toggle_extension_for_profile(profile_key, id_str)
                .is_some()
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `extension_id` must be a
/// valid NUL-terminated C string. Returns `false` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_extension(
    ptr: *mut MahoCore,
    extension_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || extension_id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(extension_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.remove_extension(id_str).is_some()
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`;
/// `profile_key` and `extension_id` must be valid NUL-terminated UTF-8 strings.
/// Returns `false` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_extension_for_profile(
    ptr: *mut MahoCore,
    profile_key: *const c_char,
    extension_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || profile_key.is_null() || extension_id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let profile_key = match CStr::from_ptr(profile_key).to_str() {
                Ok(value) => value,
                Err(_) => return false,
            };
            let id_str = match CStr::from_ptr(extension_id).to_str() {
                Ok(value) => value,
                Err(_) => return false,
            };
            core.remove_extension_for_profile(profile_key, id_str)
                .is_some()
        },
        false
    )
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_search_url(
    core: *mut MahoCore,
    query: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let core = match core.as_ref() {
                Some(c) => c,
                None => return std::ptr::null_mut(),
            };
            if query.is_null() {
                return std::ptr::null_mut();
            }
            let query_str = match CStr::from_ptr(query).to_str() {
                Ok(s) => s,
                Err(_) => return std::ptr::null_mut(),
            };
            let url = core.get_search_url(query_str);
            match CString::new(url) {
                Ok(s) => s.into_raw(),
                Err(_) => std::ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_search_engines(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let core = unsafe {
                if core.is_null() {
                    return std::ptr::null_mut();
                }
                &*core
            };
            let engines = core.get_search_engines();
            match serde_json::to_string(&engines) {
                Ok(json) => match CString::new(json) {
                    Ok(s) => s.into_raw(),
                    Err(_) => std::ptr::null_mut(),
                },
                Err(_) => std::ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_site_search_entries(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let core = match core.as_ref() {
                Some(c) => c,
                None => return std::ptr::null_mut(),
            };
            let entries = &core.get_settings().general.site_search_entries;
            to_json_cstring(entries)
        },
        ptr::null_mut()
    )
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `json` must be a valid null-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_site_search_entries(
    core: *mut MahoCore,
    json: *const c_char,
) {
    ffi_safe!(
        {
            let core = match core.as_mut() {
                Some(c) => c,
                None => return,
            };
            let json_str = match CStr::from_ptr(json).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            if let Ok(entries) =
                serde_json::from_str::<Vec<maho_types::settings::SiteSearchEntry>>(json_str)
            {
                let update = maho_types::settings::SettingsUpdate {
                    general: Some(maho_types::settings::GeneralSettingsUpdate {
                        site_search_entries: Some(entries),
                        ..Default::default()
                    }),
                    ..Default::default()
                };
                core.update_settings(update);
            }
        },
        ()
    )
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_recent_searches(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let core = unsafe {
                if core.is_null() {
                    return std::ptr::null_mut();
                }
                &*core
            };
            let searches = core.get_recent_searches();
            match serde_json::to_string(&searches) {
                Ok(json) => match CString::new(json) {
                    Ok(s) => s.into_raw(),
                    Err(_) => std::ptr::null_mut(),
                },
                Err(_) => std::ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_save_search(core: *mut MahoCore, query: *const c_char) {
    ffi_safe!(
        {
            let core = unsafe {
                if core.is_null() {
                    return;
                }
                &mut *core
            };
            let query_str = unsafe {
                if query.is_null() {
                    return;
                }
                match CStr::from_ptr(query).to_str() {
                    Ok(s) => s.to_string(),
                    Err(_) => return,
                }
            };
            core.save_search(query_str);
        },
        ()
    )
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_notifications(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let core = unsafe {
                if core.is_null() {
                    return std::ptr::null_mut();
                }
                &*core
            };
            let notifications = core.get_notifications();
            match serde_json::to_string(&notifications) {
                Ok(json) => match CString::new(json) {
                    Ok(s) => s.into_raw(),
                    Err(_) => std::ptr::null_mut(),
                },
                Err(_) => std::ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `notification_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_dismiss_notification(
    core: *mut MahoCore,
    notification_id: *const c_char,
) {
    ffi_safe!(
        {
            let core = unsafe {
                if core.is_null() {
                    return;
                }
                &mut *core
            };
            let id = unsafe {
                if notification_id.is_null() {
                    return;
                }
                match CStr::from_ptr(notification_id).to_str() {
                    Ok(s) => s.to_string(),
                    Err(_) => return,
                }
            };
            core.dismiss_notification(&id);
        },
        ()
    )
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_dismiss_all_notifications(core: *mut MahoCore) {
    ffi_safe!(
        {
            let core = unsafe {
                if core.is_null() {
                    return;
                }
                &mut *core
            };
            core.dismiss_all_notifications();
        },
        ()
    )
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `origin` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_notification_filter(
    core: *mut MahoCore,
    origin: *const c_char,
    allowed: bool,
) {
    ffi_safe!(
        {
            let core = unsafe {
                if core.is_null() {
                    return;
                }
                &mut *core
            };
            let origin_str = unsafe {
                if origin.is_null() {
                    return;
                }
                match CStr::from_ptr(origin).to_str() {
                    Ok(s) => s.to_string(),
                    Err(_) => return,
                }
            };
            core.set_notification_filter(origin_str, allowed);
        },
        ()
    )
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `site` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_zoom(core: *mut MahoCore, site: *const c_char) -> f64 {
    ffi_safe!(
        {
            let core = unsafe {
                if core.is_null() {
                    return 1.0;
                }
                &*core
            };
            let site_str = unsafe {
                if site.is_null() {
                    return 1.0;
                }
                match CStr::from_ptr(site).to_str() {
                    Ok(s) => s,
                    Err(_) => return 1.0,
                }
            };
            core.get_zoom_for_site(site_str)
        },
        1.0
    )
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `site` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_zoom(core: *mut MahoCore, site: *const c_char, zoom: f64) {
    ffi_safe!(
        {
            let core = unsafe {
                if core.is_null() {
                    return;
                }
                &mut *core
            };
            let site_str = unsafe {
                if site.is_null() {
                    return;
                }
                match CStr::from_ptr(site).to_str() {
                    Ok(s) => s.to_string(),
                    Err(_) => return,
                }
            };
            core.set_zoom_for_site(site_str, zoom);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_reader_mode(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || tab_id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.toggle_reader_mode(tab_id_str)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_is_reader_mode(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || tab_id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.is_reader_mode(tab_id_str)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `item_key` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_record_usage(ptr: *mut MahoCore, item_key: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() || item_key.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let key = match CStr::from_ptr(item_key).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.record_command_bar_usage(key);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Returns a JSON array of [key, count] pairs. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_top_used(ptr: *mut MahoCore, limit: usize) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let results = core.get_top_used_commands(limit);
            to_json_cstring(&results)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be valid. `note_id` and `tab_id` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_link_note_to_tab(
    ptr: *mut MahoCore,
    note_id: *const c_char,
    tab_id: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || note_id.is_null() || tab_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let nid = match CStr::from_ptr(note_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let tid = match CStr::from_ptr(tab_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.link_note_to_tab(nid, tid);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be valid. `note_id` and `url` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_link_note_to_url(
    ptr: *mut MahoCore,
    note_id: *const c_char,
    url: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || note_id.is_null() || url.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let nid = match CStr::from_ptr(note_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let u = match CStr::from_ptr(url).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.link_note_to_url(nid, u);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be valid. `note_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_unlink_note_from_tab(
    ptr: *mut MahoCore,
    note_id: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || note_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let nid = match CStr::from_ptr(note_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.unlink_note_from_tab(nid);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be valid. `note_id` must be a valid null-terminated C string.
/// Returns the linked tab ID or null. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_linked_tab_id(
    ptr: *mut MahoCore,
    note_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || note_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let nid = match CStr::from_ptr(note_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.get_linked_tab_id(nid) {
                Some(tid) => CString::new(tid)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be valid. `format` must be a valid null-terminated C string ("markdown", "html", "json").
/// Returns exported string. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_export_notes(
    ptr: *mut MahoCore,
    format: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || format.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let fmt = match CStr::from_ptr(format).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let result = core.export_notes(fmt);
            CString::new(result)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be valid. `note_id` and `format` must be valid null-terminated C strings.
/// Returns exported string or null. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_export_single_note(
    ptr: *mut MahoCore,
    note_id: *const c_char,
    format: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || note_id.is_null() || format.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let nid = match CStr::from_ptr(note_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let fmt = match CStr::from_ptr(format).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.export_single_note(nid, fmt) {
                Some(result) => CString::new(result)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be valid. `query` must be a valid null-terminated C string.
/// Returns JSON array of [note_id, rank] pairs. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_search_notes_fts(
    ptr: *mut MahoCore,
    query: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || query.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let q = match CStr::from_ptr(query).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let results = core.search_notes_fts(q);
            to_json_cstring(&results)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be valid.
/// Returns JSON reader settings. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_reader_settings(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let json = core.get_reader_settings_json();
            CString::new(json)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `config_json` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_backup(
    ptr: *mut MahoCore,
    config_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || config_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let cfg = match CStr::from_ptr(config_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.create_backup(cfg) {
                Ok(bytes) => {
                    let encoded = base64_encode(&bytes);
                    CString::new(encoded)
                        .map(CString::into_raw)
                        .unwrap_or(ptr::null_mut())
                }
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `backup_b64` must be a
/// valid NUL-terminated C string. `password` may be null (treated as no password). Caller must
/// free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_restore_backup(
    ptr: *mut MahoCore,
    backup_b64: *const c_char,
    password: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || backup_b64.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let b64 = match CStr::from_ptr(backup_b64).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let pw = if password.is_null() {
                None
            } else {
                CStr::from_ptr(password).to_str().ok()
            };
            let bytes = match base64_decode(b64) {
                Some(b) => b,
                None => return ptr::null_mut(),
            };
            match core.restore_backup(&bytes, pw) {
                Ok(json) => CString::new(json)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Caller must free
/// the returned string with `maho_string_free`. Returns null on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_backup_history(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let json = core.get_backup_history_json();
            CString::new(json)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `url` and `title` must
/// be valid NUL-terminated C strings. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_to_reading_list(
    ptr: *mut MahoCore,
    url: *const c_char,
    title: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || url.is_null() || title.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let u = match CStr::from_ptr(url).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let t = match CStr::from_ptr(title).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let json = core.add_to_reading_list(u, t);
            CString::new(json)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `item_id` must be a
/// valid NUL-terminated C string. Returns `false` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_from_reading_list(
    ptr: *mut MahoCore,
    item_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || item_id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let id = match CStr::from_ptr(item_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.remove_from_reading_list(id)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `item_id` must be a
/// valid NUL-terminated C string. Returns `-1` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_reading_list_read(
    ptr: *mut MahoCore,
    item_id: *const c_char,
) -> i8 {
    ffi_safe!(
        {
            if ptr.is_null() || item_id.is_null() {
                return -1;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return -1;
            }
            let core = &mut *ptr;
            let id = match CStr::from_ptr(item_id).to_str() {
                Ok(s) => s,
                Err(_) => return -1,
            };
            match core.toggle_reading_list_read(id) {
                Some(true) => 1,
                Some(false) => 0,
                None => -1,
            }
        },
        0
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Caller must free
/// the returned string with `maho_string_free`. Returns null on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_reading_list(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let json = core.get_reading_list_json();
            CString::new(json)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Returns `0` on null
/// input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_unread_count(ptr: *mut MahoCore) -> usize {
    ffi_safe!(
        {
            if ptr.is_null() {
                return 0;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return 0;
            }
            let core = &*ptr;
            core.get_unread_count()
        },
        0
    )
}

/// # Safety
/// No pointer arguments. Internally safe; uses only platform APIs. Caller must free the returned
/// string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_detect_browser_profiles() -> *mut c_char {
    ffi_safe!(
        {
            let json = MahoCore::detect_browser_profiles_json();
            CString::new(json)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `profile_path` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_import_chrome_bookmarks(
    ptr: *mut MahoCore,
    profile_path: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || profile_path.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let path = match CStr::from_ptr(profile_path).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.import_chrome_bookmarks(path) {
                Ok(json) => CString::new(json)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `profile_path` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_import_firefox_bookmarks(
    ptr: *mut MahoCore,
    profile_path: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || profile_path.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let path = match CStr::from_ptr(profile_path).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.import_firefox_bookmarks(path) {
                Ok(json) => CString::new(json)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

fn base64_encode(data: &[u8]) -> String {
    const CHARS: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut result = String::with_capacity(data.len().div_ceil(3) * 4);
    for chunk in data.chunks(3) {
        let b0 = chunk[0] as u32;
        let b1 = if chunk.len() > 1 { chunk[1] as u32 } else { 0 };
        let b2 = if chunk.len() > 2 { chunk[2] as u32 } else { 0 };
        let triple = (b0 << 16) | (b1 << 8) | b2;
        result.push(CHARS[((triple >> 18) & 0x3F) as usize] as char);
        result.push(CHARS[((triple >> 12) & 0x3F) as usize] as char);
        if chunk.len() > 1 {
            result.push(CHARS[((triple >> 6) & 0x3F) as usize] as char);
        } else {
            result.push('=');
        }
        if chunk.len() > 2 {
            result.push(CHARS[(triple & 0x3F) as usize] as char);
        } else {
            result.push('=');
        }
    }
    result
}

fn base64_decode(input: &str) -> Option<Vec<u8>> {
    fn char_to_val(c: u8) -> Option<u32> {
        match c {
            b'A'..=b'Z' => Some((c - b'A') as u32),
            b'a'..=b'z' => Some((c - b'a' + 26) as u32),
            b'0'..=b'9' => Some((c - b'0' + 52) as u32),
            b'+' => Some(62),
            b'/' => Some(63),
            b'=' => Some(0),
            _ => None,
        }
    }
    let bytes = input.as_bytes();
    if !bytes.len().is_multiple_of(4) {
        return None;
    }
    let mut result = Vec::with_capacity(bytes.len() / 4 * 3);
    for chunk in bytes.chunks(4) {
        let a = char_to_val(chunk[0])?;
        let b = char_to_val(chunk[1])?;
        let c = char_to_val(chunk[2])?;
        let d = char_to_val(chunk[3])?;
        let triple = (a << 18) | (b << 12) | (c << 6) | d;
        result.push(((triple >> 16) & 0xFF) as u8);
        if chunk[2] != b'=' {
            result.push(((triple >> 8) & 0xFF) as u8);
        }
        if chunk[3] != b'=' {
            result.push((triple & 0xFF) as u8);
        }
    }
    Some(result)
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Caller must free
/// the returned string with `maho_string_free`. Returns null on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_shortcuts(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let json = core.get_all_shortcuts_json();
            CString::new(json)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `action` and
/// `key_combo_json` must be valid NUL-terminated C strings. Caller must free the returned string
/// with `maho_string_free`. Returns null on null input, invalid UTF-8, or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_shortcut(
    ptr: *mut MahoCore,
    action: *const c_char,
    key_combo_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || action.is_null() || key_combo_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let action_str = match CStr::from_ptr(action).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let combo_str = match CStr::from_ptr(key_combo_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let key_combo: KeyCombo = match serde_json::from_str(combo_str) {
                Ok(combo) => combo,
                Err(_) => return ptr::null_mut(),
            };
            match core.set_shortcut_persisted(action_str, key_combo) {
                Ok(()) => CString::new(r#"{"success":true}"#)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
                Err(err) => {
                    let json = match err {
                        maho_types::keyboard::SetShortcutError::Conflict(ref c) => {
                            serde_json::json!({
                                "error": {
                                    "existingAction": c.existing_action,
                                    "conflict": c
                                }
                            })
                        }
                        maho_types::keyboard::SetShortcutError::Reserved => {
                            serde_json::json!({
                                "error": {
                                    "existingAction": "reserved",
                                    "reserved": true
                                }
                            })
                        }
                        maho_types::keyboard::SetShortcutError::Invalid => {
                            serde_json::json!({
                                "error": {
                                    "existingAction": "invalid",
                                    "invalid": true
                                }
                            })
                        }
                    };
                    CString::new(json.to_string())
                        .map(CString::into_raw)
                        .unwrap_or(ptr::null_mut())
                }
            }
        },
        ptr::null_mut()
    )
}
/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `key_combo_json` must be
/// a valid NUL-terminated C string. Caller must free returned string with `maho_string_free`.
/// Returns null on null input, invalid UTF-8, if import is active, or if no conflict is found.
#[no_mangle]
pub unsafe extern "C" fn maho_core_check_conflict(
    ptr: *mut MahoCore,
    key_combo_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || key_combo_json.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let combo_str = match CStr::from_ptr(key_combo_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.check_shortcut_conflict_json(combo_str) {
                Some(action) => CString::new(action)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `action` must be a
/// valid NUL-terminated C string. No-op on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_reset_shortcut(ptr: *mut MahoCore, action: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() || action.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            if let Ok(s) = CStr::from_ptr(action).to_str() {
                core.reset_shortcut_persisted(s);
            }
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. No-op on null input
/// or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_reset_all_shortcuts(ptr: *mut MahoCore) {
    ffi_safe!(
        {
            if ptr.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            core.reset_all_shortcuts_persisted();
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `action` must be a
/// valid NUL-terminated C string. No-op on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_shortcut(
    ptr: *mut MahoCore,
    action: *const c_char,
    enabled: bool,
) {
    ffi_safe!(
        {
            if ptr.is_null() || action.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            if let Ok(s) = CStr::from_ptr(action).to_str() {
                core.toggle_shortcut_persisted(s, enabled);
            }
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Caller must free the returned string
/// with `maho_string_free`. Returns null on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_export_shortcuts(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let json = core.export_shortcuts_json();
            CString::new(json)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `json_data` must be a
/// valid NUL-terminated C string. Returns true on success, false on failure or null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_import_shortcuts(
    ptr: *mut MahoCore,
    json_data: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || json_data.is_null() {
                return false;
            }
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let json_str = match CStr::from_ptr(json_data).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.import_shortcuts_json(json_str).is_ok()
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `key_combo_json` must be
/// a valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_resolve_shortcut(
    ptr: *mut MahoCore,
    key_combo_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || key_combo_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let combo_str = match CStr::from_ptr(key_combo_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.resolve_shortcut_json(combo_str) {
                Some(action) => CString::new(action)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `density` must be a
/// valid NUL-terminated C string. No-op on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_density(ptr: *mut MahoCore, density: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() || density.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            if let Ok(s) = CStr::from_ptr(density).to_str() {
                core.set_density(s);
            }
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `css` may be null
/// (treated as clearing the CSS) or a valid NUL-terminated C string. No-op if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_custom_chrome_css(ptr: *mut MahoCore, css: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let css_value = if css.is_null() {
                None
            } else {
                CStr::from_ptr(css).to_str().ok().map(|s| s.to_string())
            };
            core.set_custom_chrome_css(css_value);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. No-op on null input
/// or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_window_transparency(ptr: *mut MahoCore, enabled: bool) {
    ffi_safe!(
        {
            if ptr.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            core.set_window_transparency(enabled);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Caller must free
/// the returned string with `maho_string_free`. Returns null on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_toolbar_items(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let json = core.get_toolbar_items_json();
            CString::new(json)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `items_json` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_toolbar_items(
    ptr: *mut MahoCore,
    items_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || items_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let json_str = match CStr::from_ptr(items_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.set_toolbar_items_json(json_str) {
                Ok(()) => CString::new(r#"{"success":true}"#)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
                Err(e) => CString::new(format!(r#"{{"error":"{}"}}"#, e))
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `path` may be null
/// (clears icon) or a valid NUL-terminated C string. Caller must free returned string with
/// `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_app_icon(
    ptr: *mut MahoCore,
    path: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            if path.is_null() {
                core.set_app_icon(None);
            } else {
                match CStr::from_ptr(path).to_str() {
                    Ok(s) => {
                        if s.is_empty() || s == "null" {
                            core.set_app_icon(None);
                        } else {
                            core.set_app_icon(Some(s.to_string()));
                        }
                    }
                    Err(_) => return ptr::null_mut(),
                }
            }
            CString::new(r#"{"ok":true}"#)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Caller must free
/// the returned string with `maho_string_free`. Returns null on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_default_toolbar_items(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let json = core.get_default_toolbar_items_json();
            CString::new(json)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `url` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_handle_url_scheme(
    ptr: *mut MahoCore,
    url: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || url.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let url_str = match CStr::from_ptr(url).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let result = core.handle_url_scheme(url_str);
            CString::new(result)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_searchable_items(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() {
                return std::ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return std::ptr::null_mut();
            }
            let core = &*core;
            let result = core.get_searchable_items();
            CString::new(result)
                .map(|s| s.into_raw())
                .unwrap_or(std::ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` and `query` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_start_find(
    core: *mut MahoCore,
    tab_id: *const c_char,
    query: *const c_char,
    case_sensitive: bool,
    whole_word: bool,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || tab_id.is_null() || query.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *core;
            let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let query_str = match CStr::from_ptr(query).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let tid = maho_types::identifiers::TabId::new(tab_id_str);
            let session = core.start_find(tid, query_str.to_string(), case_sensitive, whole_word);
            to_json_cstring(&session)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_find_next(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *core;
            let result = core.find_next();
            to_json_cstring(&result)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_find_previous(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *core;
            let result = core.find_previous();
            to_json_cstring(&result)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_dismiss_find(core: *mut MahoCore) {
    ffi_safe!(
        {
            if core.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *core;
            core.dismiss_find();
        },
        ()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `settings_json` must be a valid null-terminated C string containing JSON.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_settings(
    core: *mut MahoCore,
    settings_json: *const c_char,
) {
    ffi_safe!(
        {
            if core.is_null() || settings_json.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *core;
            let json_str = match CStr::from_ptr(settings_json).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let update: maho_types::settings::SettingsUpdate = match serde_json::from_str(json_str)
            {
                Ok(u) => u,
                Err(_) => return,
            };
            core.update_settings(update);
        },
        ()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_atc_rules(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*core;
            let rules = core.get_atc_rules();
            to_json_cstring(&rules)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `rule_json` must be a valid null-terminated C string containing JSON.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_atc_rule(
    core: *mut MahoCore,
    rule_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || rule_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *core;
            let json_str = match CStr::from_ptr(rule_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let rule: maho_core::atc_manager::ATCRule = match serde_json::from_str(json_str) {
                Ok(r) => r,
                Err(_) => return ptr::null_mut(),
            };
            let id = core.add_atc_rule(rule);
            CString::new(id)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `rule_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_atc_rule(
    core: *mut MahoCore,
    rule_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || rule_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *core;
            let rule_id_str = match CStr::from_ptr(rule_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let result = core.remove_atc_rule(rule_id_str);
            to_json_cstring(&result)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `rule_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_atc_rule(
    core: *mut MahoCore,
    rule_id: *const c_char,
    enabled: bool,
) {
    ffi_safe!(
        {
            if core.is_null() || rule_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *core;
            let rule_id_str = match CStr::from_ptr(rule_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.toggle_atc_rule(rule_id_str, enabled);
        },
        ()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `rule_json` must be a valid null-terminated C string containing TrafficRule JSON.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_traffic_rule(
    core: *mut MahoCore,
    rule_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || rule_json.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *core;
            let json_str = match CStr::from_ptr(rule_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let rule: maho_types::air_traffic::TrafficRule = match serde_json::from_str(json_str) {
                Ok(r) => r,
                Err(_) => return ptr::null_mut(),
            };
            let created = core.create_traffic_rule_persisted(rule);
            CString::new(created.id)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `rule_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_traffic_rule_persisted(
    core: *mut MahoCore,
    rule_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || rule_id.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *core;
            let rule_id_str = match CStr::from_ptr(rule_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let result = core.delete_traffic_rule_persisted(rule_id_str);
            to_json_cstring(&result)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `rule_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_traffic_rule_persisted(
    core: *mut MahoCore,
    rule_id: *const c_char,
    enabled: bool,
) {
    ffi_safe!(
        {
            if core.is_null() || rule_id.is_null() {
                return;
            }
            if import_gate::is_active() {
                return;
            }
            let core = &mut *core;
            let rule_id_str = match CStr::from_ptr(rule_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.toggle_traffic_rule_persisted(rule_id_str, enabled);
        },
        ()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `url` must be a valid null-terminated C string.
/// `space_rules_json` must be a valid null-terminated C string containing JSON space rules or null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_decide_link_destination(
    core: *mut MahoCore,
    url: *const c_char,
    is_external: bool,
    space_rules_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || url.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*core;
            let url_str = match CStr::from_ptr(url).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };

            #[derive(serde::Deserialize)]
            struct SpaceRulesInput {
                space_id: maho_types::identifiers::SpaceId,
                rules: Vec<maho_types::space::ATCRule>,
            }

            let space_rules = if space_rules_json.is_null() {
                core.space_manager().get_all_atc_rules()
            } else {
                let space_rules_str = match CStr::from_ptr(space_rules_json).to_str() {
                    Ok(s) => s,
                    Err(_) => return ptr::null_mut(),
                };
                let parsed: Vec<SpaceRulesInput> = match serde_json::from_str(space_rules_str) {
                    Ok(p) => p,
                    Err(_) => return ptr::null_mut(),
                };
                parsed
                    .into_iter()
                    .map(|item| (item.space_id, item.rules))
                    .collect()
            };

            let destination = core.decide_link_destination(url_str, is_external, &space_rules);
            to_json_cstring(&destination)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_open_external_links_in_maho_mini(
    core: *mut MahoCore,
    enabled: bool,
) {
    ffi_safe!(
        {
            if core.is_null() {
                return;
            }
            if import_gate::is_active() {
                return;
            }
            let core = &mut *core;
            core.set_open_external_links_in_maho_mini_persisted(enabled);
        },
        ()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_open_external_links_in_maho_mini(
    core: *mut MahoCore,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() {
                return false;
            }
            if import_gate::is_active() {
                return false;
            }
            let core = &*core;
            core.get_open_external_links_in_maho_mini()
        },
        false
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_search_history_paginated(
    core: *mut MahoCore,
    query: *const c_char,
    limit: usize,
    offset: usize,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || query.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*core;
            let query_str = match CStr::from_ptr(query).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let results = core.search_history_with_offset(query_str, limit, offset);
            to_json_cstring(&results)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_history_grouped_by_date(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*core;
            let results = core.get_history_grouped_by_date();
            to_json_cstring(&results)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `folder_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_bookmark_folder(
    core: *mut MahoCore,
    folder_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || folder_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *core;
            let folder_id_str = match CStr::from_ptr(folder_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let result = core.delete_bookmark_folder(folder_id_str);
            to_json_cstring(&result)
        },
        ptr::null_mut()
    )
}

/// All pointer args except `core` may be null (treated as None/empty).
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `id` must be a valid
/// NUL-terminated C string; other string args may be null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_conversation(
    core: *mut MahoCore,
    id: *const c_char,
    title: *const c_char,
    space_id: *const c_char,
    model: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *core;
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let title_opt = if title.is_null() {
                None
            } else {
                CStr::from_ptr(title).to_str().ok()
            };
            let space_opt = if space_id.is_null() {
                None
            } else {
                CStr::from_ptr(space_id).to_str().ok()
            };
            let model_opt = if model.is_null() {
                None
            } else {
                CStr::from_ptr(model).to_str().ok()
            };
            core.create_conversation_persisted(id_str, title_opt, space_opt, model_opt)
        },
        false
    )
}

/// List conversations. Returns JSON array string (caller must free via maho_string_free).
/// Returns "[]" on null core or missing storage.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`, or null (returns "[]").
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_list_conversations(
    core: *mut MahoCore,
    limit: usize,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() {
                return to_c_string("[]");
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*core;
            let conversations = if let Some(storage) = core.storage_ref() {
                storage
                    .list_conversations(maho_types::chat::ConversationListState::Active, limit)
                    .unwrap_or_default()
            } else {
                return to_c_string("[]");
            };
            let ptr = to_json_cstring(&conversations);
            if ptr.is_null() {
                to_c_string("[]")
            } else {
                ptr
            }
        },
        ptr::null_mut()
    )
}

/// Get messages for a conversation session. Returns JSON array string.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `session_id` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_conversation_messages(
    core: *mut MahoCore,
    session_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || session_id.is_null() {
                return to_c_string("[]");
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*core;
            let sid = match CStr::from_ptr(session_id).to_str() {
                Ok(s) => s,
                Err(_) => return to_c_string("[]"),
            };
            let messages = if let Some(storage) = core.storage_ref() {
                storage.get_conversation_messages(sid).unwrap_or_default()
            } else {
                return to_c_string("[]");
            };
            let ptr = to_json_cstring(&messages);
            if ptr.is_null() {
                to_c_string("[]")
            } else {
                ptr
            }
        },
        ptr::null_mut()
    )
}

/// Save a message to a conversation. Returns true on success.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `session_id`, `role`,
/// and `content` must be valid NUL-terminated C strings; `url_context` may be null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_save_conversation_message(
    core: *mut MahoCore,
    session_id: *const c_char,
    role: *const c_char,
    content: *const c_char,
    url_context: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || session_id.is_null() || role.is_null() || content.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *core;
            let sid = match CStr::from_ptr(session_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let role_str = match CStr::from_ptr(role).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let content_str = match CStr::from_ptr(content).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let url_ctx = if url_context.is_null() {
                None
            } else {
                CStr::from_ptr(url_context).to_str().ok()
            };
            core.save_conversation_message_persisted(sid, role_str, content_str, url_ctx)
        },
        false
    )
}

/// Delete a conversation by id. Returns true if deleted.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `id` must be a valid
/// NUL-terminated C string. Returns `false` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_conversation(
    core: *mut MahoCore,
    id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *core;
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.delete_conversation_persisted(id_str)
        },
        false
    )
}

/// Rename a conversation. Returns true on success.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `id` and `new_title`
/// must be valid NUL-terminated C strings. Returns `false` on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_rename_conversation(
    core: *mut MahoCore,
    id: *const c_char,
    new_title: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() || new_title.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *core;
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let title_str = match CStr::from_ptr(new_title).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.rename_conversation_persisted(id_str, title_str)
        },
        false
    )
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct ConversationListQuery {
    #[serde(default)]
    state: maho_types::chat::ConversationListState,
    #[serde(default = "default_conversation_limit")]
    limit: usize,
}
fn default_conversation_limit() -> usize {
    100
}
#[derive(serde::Deserialize)]
struct ConversationBulkRequest {
    op: maho_types::chat::ConversationBulkOperation,
    ids: Vec<String>,
}
#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct ConversationMoveRequest {
    ids: Vec<String>,
    project_id: Option<String>,
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_list_conversations_v2(
    core: *mut MahoCore,
    query_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || query_json.is_null() || import_gate::is_active() {
                return ptr::null_mut();
            }
            let query = match CStr::from_ptr(query_json)
                .to_str()
                .ok()
                .and_then(|s| serde_json::from_str::<ConversationListQuery>(s).ok())
            {
                Some(q) if q.limit > 0 => q,
                _ => return ptr::null_mut(),
            };
            let rows = match (&*core).list_conversations(query.state, query.limit) {
                Ok(v) => v,
                Err(_) => return ptr::null_mut(),
            };
            CString::new(serde_json::to_string(&rows).unwrap_or_else(|_| "[]".into()))
                .map_or(ptr::null_mut(), CString::into_raw)
        },
        ptr::null_mut()
    )
}
#[no_mangle]
pub unsafe extern "C" fn maho_core_archive_conversation(
    core: *mut MahoCore,
    id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() || import_gate::is_active() {
                return false;
            }
            CStr::from_ptr(id)
                .to_str()
                .ok()
                .is_some_and(|id| (&mut *core).archive_conversation_persisted(id))
        },
        false
    )
}
#[no_mangle]
pub unsafe extern "C" fn maho_core_unarchive_conversation(
    core: *mut MahoCore,
    id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() || import_gate::is_active() {
                return false;
            }
            CStr::from_ptr(id)
                .to_str()
                .ok()
                .is_some_and(|id| (&mut *core).unarchive_conversation_persisted(id))
        },
        false
    )
}
#[no_mangle]
pub unsafe extern "C" fn maho_core_active_conversation_ids(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || import_gate::is_active() {
                return ptr::null_mut();
            }
            to_json_cstring(&(&*core).active_conversation_ids())
        },
        ptr::null_mut()
    )
}
#[no_mangle]
pub unsafe extern "C" fn maho_core_apply_conversation_bulk_operation(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || request_json.is_null() || import_gate::is_active() {
                return ptr::null_mut();
            }
            let req = match CStr::from_ptr(request_json)
                .to_str()
                .ok()
                .and_then(|s| serde_json::from_str::<ConversationBulkRequest>(s).ok())
            {
                Some(v) => v,
                None => return ptr::null_mut(),
            };
            let now = chrono::Utc::now().to_rfc3339();
            let value = match (&mut *core).apply_conversation_bulk_operation(req.op, req.ids, &now)
            {
                Ok(v) => serde_json::to_value(v).unwrap_or(serde_json::Value::Null),
                Err(v) => v,
            };
            to_c_string(&value.to_string())
        },
        ptr::null_mut()
    )
}
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_conversation_auto_archive_policy(
    core: *mut MahoCore,
) -> i32 {
    ffi_safe!(
        {
            if core.is_null() || import_gate::is_active() {
                return -1;
            }
            (&*core)
                .get_conversation_auto_archive_policy()
                .unwrap_or(-1)
        },
        -1
    )
}
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_conversation_auto_archive_policy(
    core: *mut MahoCore,
    days: i32,
) -> bool {
    ffi_safe!(
        {
            !core.is_null()
                && !import_gate::is_active()
                && (&*core).set_conversation_auto_archive_policy(days)
        },
        false
    )
}
#[no_mangle]
pub unsafe extern "C" fn maho_core_auto_archive_conversations(
    core: *mut MahoCore,
    now_sec: i64,
) -> i32 {
    ffi_safe!(
        {
            if core.is_null() || import_gate::is_active() {
                return -1;
            }
            match (&mut *core).auto_archive_conversations(now_sec) {
                Ok(Some(n)) => i32::try_from(n).unwrap_or(-1),
                Ok(None) => -2,
                Err(_) => -1,
            }
        },
        -1
    )
}
#[no_mangle]
pub unsafe extern "C" fn maho_core_list_conversation_projects(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || import_gate::is_active() {
                return ptr::null_mut();
            }
            to_json_cstring(&(&*core).list_conversation_projects())
        },
        ptr::null_mut()
    )
}
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_conversation_project(
    core: *mut MahoCore,
    name: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || name.is_null() || import_gate::is_active() {
                return ptr::null_mut();
            }
            let p = CStr::from_ptr(name)
                .to_str()
                .ok()
                .and_then(|n| (&mut *core).create_conversation_project_persisted(n));
            p.and_then(|p| CString::new(serde_json::to_string(&p).ok()?).ok())
                .map_or(ptr::null_mut(), CString::into_raw)
        },
        ptr::null_mut()
    )
}
#[no_mangle]
pub unsafe extern "C" fn maho_core_rename_conversation_project(
    core: *mut MahoCore,
    id: *const c_char,
    name: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() || name.is_null() || import_gate::is_active() {
                return false;
            }
            match (CStr::from_ptr(id).to_str(), CStr::from_ptr(name).to_str()) {
                (Ok(id), Ok(name)) => (&mut *core).rename_conversation_project_persisted(id, name),
                _ => false,
            }
        },
        false
    )
}
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_conversation_project(
    core: *mut MahoCore,
    id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() || import_gate::is_active() {
                return false;
            }
            CStr::from_ptr(id)
                .to_str()
                .ok()
                .is_some_and(|id| (&mut *core).delete_conversation_project_persisted(id))
        },
        false
    )
}
#[no_mangle]
pub unsafe extern "C" fn maho_core_move_conversations_to_project(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || request_json.is_null() || import_gate::is_active() {
                return ptr::null_mut();
            }
            let req = match CStr::from_ptr(request_json)
                .to_str()
                .ok()
                .and_then(|s| serde_json::from_str::<ConversationMoveRequest>(s).ok())
            {
                Some(v) => v,
                None => return ptr::null_mut(),
            };
            match (&mut *core)
                .move_conversations_to_project_persisted(req.ids, req.project_id.as_deref())
            {
                Ok(v) => to_json_cstring(&v),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

// ============================================================
// Composer draft FFI functions
// Drafts are device/profile-local (settings table), never synced.
// Scope JSON: {"kind":"new_task"} | {"kind":"conversation","conversationId":"<id>"}
// ============================================================

/// Read the composer draft for `scope_json`.
///
/// Returns a JSON string `{"version":1,"text":...,"updatedAt":<RFC3339>}` on success, or
/// null when the scope is malformed, no draft exists, the stored value is corrupt, or the
/// scope names a conversation that no longer exists (the orphan row is purged in that case).
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`, or null (returns null);
/// `scope_json` must be a valid NUL-terminated C string or null (returns null).
/// Caller must free the returned string with `maho_string_free`.

#[no_mangle]
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_composer_draft(
    core: *mut MahoCore,
    scope_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || scope_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*core;
            let scope = match CStr::from_ptr(scope_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.get_composer_draft(scope) {
                Some(json) => match CString::new(json) {
                    Ok(c) => c.into_raw(),
                    Err(_) => ptr::null_mut(),
                },
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Write the composer draft for `scope_json`. Empty `text` deletes the draft.
/// Text is stored verbatim; the timestamp is assigned natively. Returns true on success,
/// false on malformed scope, null input, missing storage, or storage error.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `scope_json` and `text`
/// must be valid NUL-terminated C strings (null returns false).
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_composer_draft(
    core: *mut MahoCore,
    scope_json: *const c_char,
    text: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || scope_json.is_null() || text.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &*core;
            let scope = match CStr::from_ptr(scope_json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let text_str = match CStr::from_ptr(text).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.set_composer_draft(scope, text_str)
        },
        false
    )
}

/// Delete the composer draft for `scope_json`. Deleting an absent draft succeeds.
/// Returns false on malformed scope, null input, missing storage, or storage error.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `scope_json` must be a
/// valid NUL-terminated C string (null returns false).
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_composer_draft(
    core: *mut MahoCore,
    scope_json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || scope_json.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &*core;
            let scope = match CStr::from_ptr(scope_json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.delete_composer_draft(scope)
        },
        false
    )
}

// ============================================================
// BYOK (Bring Your Own Key) FFI functions
// BYOK keys are stored via settings key "byok:<provider>"
// ============================================================

/// Set a BYOK API key for the given provider.
///
/// # Safety

// ============================================================

/// Returns the user's subscription tier as an integer:
/// 0=Free, 1=Pro, 2=Max, -1=unauthenticated/unknown.
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_account_get_tier(core: *mut MahoCore) -> i32 {
    ffi_safe!(
        {
            if core.is_null() {
                return -1;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return -1;
            }
            let core = &*core;
            match core.get_account_tier() {
                Some(maho_types::account::UserTier::Free) => 0,
                Some(maho_types::account::UserTier::Pro) => 1,
                Some(maho_types::account::UserTier::Max) => 2,
                None => -1,
            }
        },
        -1
    )
}

/// `core` must be a valid pointer returned by `maho_core_new`.
/// `window_id` must be a valid null-terminated C string.
/// The caller must free the returned JSON string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_split_view_config(
    core: *mut MahoCore,
    window_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || window_id.is_null() {
                return ptr::null_mut();
            }
            let win_str = match CStr::from_ptr(window_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let c = &*core;
            let win_id = maho_types::identifiers::WindowId::new(win_str);
            if let Some(config) = c.get_split_view_config(&win_id) {
                to_json_cstring(&config)
            } else {
                ptr::null_mut()
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `window_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_window_closed(core: *mut MahoCore, window_id: *const c_char) {
    ffi_safe!(
        {
            if core.is_null() || window_id.is_null() {
                return;
            }
            let win_str = match CStr::from_ptr(window_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let c = &mut *core;
            let win_id = maho_types::identifiers::WindowId::new(win_str);
            c.window_closed(&win_id);
        },
        ()
    )
}

#[derive(Clone)]
struct SplitChangeCallback {
    token: u64,
    callback: extern "C" fn(*const c_char),
    lease: FfiArc<CallbackLease>,
}

static SPLIT_CALLBACKS: OnceLock<FfiMutex<Vec<SplitChangeCallback>>> = OnceLock::new();

type ToolAvailCb = unsafe extern "C" fn(*mut c_void, *const c_char);

#[derive(Clone)]
struct ToolAvailabilityCallback {
    token: u64,
    user_data: usize,
    callback: ToolAvailCb,
    lease: FfiArc<CallbackLease>,
}

static TOOL_AVAIL_CALLBACKS: OnceLock<FfiMutex<Vec<ToolAvailabilityCallback>>> = OnceLock::new();

type SettingsChangeCb = unsafe extern "C" fn(*mut c_void, *const c_char);

#[derive(Clone)]
struct SettingsChangeCallback {
    token: u64,
    user_data: usize,
    callback: SettingsChangeCb,
    lease: FfiArc<CallbackLease>,
}

static SETTINGS_CHANGE_CALLBACKS: OnceLock<FfiMutex<Vec<SettingsChangeCallback>>> = OnceLock::new();

fn get_split_callbacks() -> &'static FfiMutex<Vec<SplitChangeCallback>> {
    SPLIT_CALLBACKS.get_or_init(|| FfiMutex::new(Vec::new()))
}

fn get_tool_avail_callbacks() -> &'static FfiMutex<Vec<ToolAvailabilityCallback>> {
    TOOL_AVAIL_CALLBACKS.get_or_init(|| FfiMutex::new(Vec::new()))
}

fn get_settings_change_callbacks() -> &'static FfiMutex<Vec<SettingsChangeCallback>> {
    SETTINGS_CHANGE_CALLBACKS.get_or_init(|| FfiMutex::new(Vec::new()))
}

pub(crate) fn process_split_callbacks_for_updates(updates: &[maho_types::events::core_update::CoreUpdate]) {
    let inside_serialized_ffi = FFI_SERIALIZATION_DEPTH.with(|depth| depth.get() != 0);
    if inside_serialized_ffi {
        DEFERRED_CORE_UPDATES.with(|pending| pending.borrow_mut().extend_from_slice(updates));
    } else {
        dispatch_core_callbacks_for_updates(updates);
    }
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_register_split_change_callback(
    core: *mut MahoCore,
    callback: extern "C" fn(*const c_char),
) -> u64 {
    ffi_safe!(
        {
            if core.is_null() {
                return 0;
            }
            maho_types::events::core_update::set_core_update_callback(|update| {
                process_split_callbacks_for_updates(&[update]);
            });
            let token = NEXT_CALLBACK_TOKEN.fetch_add(1, Ordering::SeqCst);
            if let Ok(mut cbs) = get_split_callbacks().lock() {
                cbs.push(SplitChangeCallback {
                    token,
                    callback,
                    lease: FfiArc::new(CallbackLease::default()),
                });
            }
            token
        },
        0
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_unregister_split_change_callback(
    _core: *mut MahoCore,
    token: u64,
) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let removed = get_split_callbacks().lock().ok().and_then(|mut cbs| {
            cbs.iter()
                .position(|entry| entry.token == token)
                .map(|index| cbs.remove(index))
        });
        if let Some(entry) = removed {
            entry.lease.remove_and_wait(token);
        }
    }));
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `user_data` may be null; if non-null it must outlive the callback registration.
/// `callback` must not panic and must be safe to call from arbitrary threads.
#[no_mangle]
pub unsafe extern "C" fn maho_core_register_tool_availability_callback(
    core: *mut MahoCore,
    user_data: *mut c_void,
    callback: unsafe extern "C" fn(*mut c_void, *const c_char),
) -> u64 {
    ffi_safe!(
        {
            if core.is_null() {
                return 0;
            }
            maho_types::events::core_update::set_core_update_callback(|update| {
                process_split_callbacks_for_updates(&[update]);
            });
            let token = NEXT_CALLBACK_TOKEN.fetch_add(1, Ordering::SeqCst);
            if let Ok(mut cbs) = get_tool_avail_callbacks().lock() {
                cbs.push(ToolAvailabilityCallback {
                    token,
                    user_data: user_data as usize,
                    callback,
                    lease: FfiArc::new(CallbackLease::default()),
                });
            }
            token
        },
        0
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_unregister_tool_availability_callback(
    _core: *mut MahoCore,
    token: u64,
) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let removed = get_tool_avail_callbacks().lock().ok().and_then(|mut cbs| {
            cbs.iter()
                .position(|entry| entry.token == token)
                .map(|index| cbs.remove(index))
        });
        if let Some(entry) = removed {
            entry.lease.remove_and_wait(token);
        }
    }));
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `user_data` may be null; if non-null it must outlive the callback registration.
/// `callback` must not panic and must be safe to call from arbitrary threads. It receives
/// the current settings JSON (`*const c_char`), which may be null; callers must tolerate null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_register_settings_change_callback(
    core: *mut MahoCore,
    user_data: *mut c_void,
    callback: unsafe extern "C" fn(*mut c_void, *const c_char),
) -> u64 {
    ffi_safe!(
        {
            if core.is_null() {
                return 0;
            }
            maho_types::events::core_update::set_core_update_callback(|update| {
                process_split_callbacks_for_updates(&[update]);
            });
            let token = NEXT_CALLBACK_TOKEN.fetch_add(1, Ordering::SeqCst);
            if let Ok(mut cbs) = get_settings_change_callbacks().lock() {
                cbs.push(SettingsChangeCallback {
                    token,
                    user_data: user_data as usize,
                    callback,
                    lease: FfiArc::new(CallbackLease::default()),
                });
            }
            token
        },
        0
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_unregister_settings_change_callback(
    _core: *mut MahoCore,
    token: u64,
) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let removed = get_settings_change_callbacks()
            .lock()
            .ok()
            .and_then(|mut cbs| {
                cbs.iter()
                    .position(|entry| entry.token == token)
                    .map(|index| cbs.remove(index))
            });
        if let Some(entry) = removed {
            entry.lease.remove_and_wait(token);
        }
    }));
}

pub(crate) fn dispatch_core_callbacks_for_updates(updates: &[maho_types::events::core_update::CoreUpdate]) {
    for update in updates {
        match update {
            maho_types::events::core_update::CoreUpdate::SplitViewChanged { .. } => {
                let callbacks = get_split_callbacks()
                    .lock()
                    .map(|callbacks| callbacks.clone())
                    .unwrap_or_default();
                let Some(c_json) = serde_json::to_string(update)
                    .ok()
                    .and_then(|s| CString::new(s).ok())
                else {
                    continue;
                };
                for entry in callbacks {
                    let Some(_invocation) = entry.lease.enter(entry.token) else {
                        continue;
                    };
                    (entry.callback)(c_json.as_ptr());
                }
            }
            maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged { .. } => {
                let callbacks = get_tool_avail_callbacks()
                    .lock()
                    .map(|callbacks| callbacks.clone())
                    .unwrap_or_default();
                let Some(c_json) = serde_json::to_string(update)
                    .ok()
                    .and_then(|s| CString::new(s).ok())
                else {
                    continue;
                };
                for entry in callbacks {
                    let Some(_invocation) = entry.lease.enter(entry.token) else {
                        continue;
                    };
                    unsafe {
                        (entry.callback)(entry.user_data as *mut c_void, c_json.as_ptr());
                    }
                }
            }
            maho_types::events::core_update::CoreUpdate::SettingsChanged { settings } => {
                let callbacks = get_settings_change_callbacks()
                    .lock()
                    .map(|callbacks| callbacks.clone())
                    .unwrap_or_default();
                if callbacks.is_empty() {
                    continue;
                }
                let c_json = serde_json::to_string(settings)
                    .ok()
                    .and_then(|json| CString::new(json).ok());
                let json_ptr = c_json.as_ref().map_or(ptr::null(), |c| c.as_ptr());
                for entry in callbacks {
                    let Some(_invocation) = entry.lease.enter(entry.token) else {
                        continue;
                    };
                    unsafe {
                        (entry.callback)(entry.user_data as *mut c_void, json_ptr);
                    }
                }
            }
            _ => {}
        }
    }
}
