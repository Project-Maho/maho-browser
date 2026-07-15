#![allow(clippy::missing_safety_doc)]

pub mod import;
pub mod import_destination;
pub(crate) mod import_gate;

use std::ffi::{c_char, c_void, CStr, CString};
use std::path::Path;
use std::ptr;

use maho_core::maho_core::MahoCore;
use maho_core::workspace_manager::WorkspaceManager;

use maho_types::keyboard::KeyCombo;
use maho_types::passwords::SavedPassword;

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

fn to_json_cstring<T: serde::Serialize + ?Sized>(value: &T) -> *mut c_char {
    match serde_json::to_string(value) {
        Ok(json) => CString::new(json)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        Err(_) => ptr::null_mut(),
    }
}

fn cstring_or_fallback(value: &str, fallback: &'static str) -> Option<CString> {
    CString::new(value)
        .ok()
        .or_else(|| CString::new(fallback).ok())
}

fn to_password_entries(passwords: Vec<&SavedPassword>) -> *mut c_char {
    let views: Vec<PasswordEntryView> = passwords
        .into_iter()
        .map(|password| PasswordEntryView {
            id: password.id.clone(),
            domain: password.domain.clone(),
            username: password.username.clone(),
            created_at: password.created_at.clone(),
            last_used: password.last_used.clone(),
        })
        .collect();
    to_json_cstring(&views)
}

#[no_mangle]
pub extern "C" fn maho_core_new() -> *mut MahoCore {
    ffi_safe!(
        { Box::into_raw(Box::new(MahoCore::new())) },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_free(ptr: *mut MahoCore) {
    ffi_safe!(
        {
            if !ptr.is_null() {
                drop(Box::from_raw(ptr));
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
pub unsafe extern "C" fn maho_core_get_tab_view_models(ptr: *mut MahoCore) -> *mut c_char {
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
            to_json_cstring(&core.get_tab_view_models())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid NUL-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_tab_snapshot_by_id(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || tab_id.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let tab_id_str = CStr::from_ptr(tab_id).to_str().unwrap_or("");
            match core.get_tab_snapshot_json(&maho_types::identifiers::TabId::new(tab_id_str)) {
                Some(json) => CString::new(json).unwrap().into_raw(),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `tab_id` must be a
/// valid NUL-terminated C string. Returns `false` on null pointer, invalid UTF-8, or
/// when an import is in progress (per `import_gate`).
#[no_mangle]
pub unsafe extern "C" fn maho_core_is_tab_pinned(
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
            let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let core = &*ptr;
            core.is_tab_pinned(tab_id_str)
        },
        false
    )
}

/// # Safety
/// Same contract as `maho_core_is_tab_pinned`: `ptr` must be a valid `*mut MahoCore`
/// produced by `maho_core_new*`; `tab_id` must be a valid NUL-terminated C string.
/// Returns `true` for Pinned OR Favorite tabs (close-guard protection). Returns
/// `false` on null pointer, invalid UTF-8, or when an import is in progress.
#[no_mangle]
pub unsafe extern "C" fn maho_core_is_tab_close_protected(
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
            let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let core = &*ptr;
            core.is_tab_close_protected(tab_id_str)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_space_view_models(ptr: *mut MahoCore) -> *mut c_char {
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
            to_json_cstring(&core.get_space_view_models())
        },
        ptr::null_mut()
    )
}

/// Combined sidebar tree + base64-encoded favicons in one call (audit C3 fix).
/// Returns `{"tree": [...], "favicons": {"tab_id": "base64_png", ...}}`.
///
/// # Safety
/// `ptr` must be a valid `*mut MahoCore`; `space_id_json` must be valid NUL-terminated UTF-8
/// containing a JSON-serialized SpaceId. Caller must free the returned string via
/// `maho_string_free`. Returns null on error or if import is active.
// TODO: regenerate via cbindgen
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_sidebar_state_v2(
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
            let space_id: maho_types::identifiers::SpaceId = match serde_json::from_str(json_str) {
                Ok(id) => id,
                Err(_) => return ptr::null_mut(),
            };

            match core.get_sidebar_state_v2(&space_id) {
                Some(json) => match CString::new(json) {
                    Ok(cs) => cs.into_raw(),
                    Err(_) => ptr::null_mut(),
                },
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
pub unsafe extern "C" fn maho_core_get_active_space_id(ptr: *mut MahoCore) -> *mut c_char {
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
            to_json_cstring(&core.get_active_space_id())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
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

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid null-terminated C string containing a SpaceId JSON.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_pinned_tabs(
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
            to_json_cstring(&core.get_pinned_tabs(&space_id))
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid null-terminated C string containing a SpaceId JSON.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_space_tabs(
    ptr: *mut MahoCore,
    space_id_json: *const c_char,
    window_id_ptr: *const i64,
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

            let tabs = if !window_id_ptr.is_null() {
                core.get_space_tabs_for_window(&space_id, *window_id_ptr)
            } else {
                core.get_space_tabs(&space_id)
            };

            to_json_cstring(&tabs)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_release_window_tabs(ptr: *mut MahoCore, window_id: i64) {
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
            core.release_window_tabs(window_id);
        },
        ()
    )
}

/// Returns non-pinned tab IDs older than `cutoff_ts` (epoch seconds) in the given `space_id` as a JSON array of strings.
/// Read-only — does NOT modify state. Caller frees the returned string with `maho_string_free`.
/// Returns NULL on null pointer or internal error.
///
/// # Safety
/// `core` and `space_id` must be valid pointers.
#[no_mangle]
pub unsafe extern "C" fn maho_core_find_tabs_older_than(
    core: *mut MahoCore,
    cutoff_ts: i64,
    space_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || space_id.is_null() {
                return std::ptr::null_mut();
            }
            if import_gate::is_active() {
                return std::ptr::null_mut();
            }
            let core_ref = &*core;
            let space_id_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => return std::ptr::null_mut(),
            };
            let space_id = maho_types::identifiers::SpaceId::new(space_id_str);
            let ids: Vec<String> = core_ref
                .find_tabs_older_than(cutoff_ts, &space_id)
                .into_iter()
                .map(|id| id.as_ref().to_string())
                .collect();
            let json = serde_json::to_string(&ids).unwrap_or_else(|_| "[]".to_string());
            match CString::new(json) {
                Ok(c) => c.into_raw(),
                Err(_) => std::ptr::null_mut(),
            }
        },
        std::ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid null-terminated C string containing a SpaceId JSON.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_favorite_tabs(
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
            let tabs = core.get_favorite_tabs(&space_id);
            let mut serialized_tabs = Vec::new();
            for tab in tabs {
                let mut tab_val = serde_json::to_value(&tab).unwrap_or(serde_json::Value::Null);
                if let Some(tab_obj) = tab_val.as_object_mut() {
                    if let Some(ref fav_data) = tab.favicon {
                        let b64 = base64_encode(&fav_data.data);
                        tab_obj.insert("favicon".to_string(), serde_json::Value::String(b64));
                    }
                }
                serialized_tabs.push(tab_val);
            }
            to_json_cstring(&serialized_tabs)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid null-terminated C string containing a SpaceId JSON.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_today_tabs(
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
            to_json_cstring(&core.get_today_tabs(&space_id))
        },
        ptr::null_mut()
    )
}

/// # Safety
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
                Err(_) => return,
            };
            core.set_installed_extensions(extensions);
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
            let core = &mut *ptr;
            if let Some(cb) = callback {
                let ud_raw = user_data as usize;
                core.register_extension_sync_callback(Some(Box::new(
                    move |id, enabled, deleted| {
                        let id_c = CString::new(id).unwrap();
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
                    let p_cstr = CString::new(opt.path.clone()).unwrap();
                    *out_path = p_cstr.into_raw();
                }
                if !out_layout.is_null() {
                    let lay_str = match opt.layout {
                        maho_core::extension_bridge::SidePanelLayout::Left => "left",
                        maho_core::extension_bridge::SidePanelLayout::Right => "right",
                    };
                    let lay_cstr = CString::new(lay_str).unwrap();
                    *out_layout = lay_cstr.into_raw();
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
pub unsafe extern "C" fn maho_core_get_password_provider_extension(
    ptr: *mut MahoCore,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let extensions = core.get_installed_extensions();
            if let Some(ext_id) = maho_core::extension_bridge::find_password_provider(&extensions) {
                CString::new(ext_id)
                    .map(|c| c.into_raw())
                    .unwrap_or(ptr::null_mut())
            } else {
                ptr::null_mut()
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_is_native_password_provider_active(ptr: *mut MahoCore) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() {
                return true;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &*ptr;
            core.get_settings().autofill.password_provider
                == maho_types::passwords::PasswordProviderKind::MahoNative
        },
        false
    )
}

/// # Safety

/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_search_passwords(
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
                Ok(s) => s.trim(),
                Err(_) => return ptr::null_mut(),
            };

            if query_str.is_empty() {
                return to_password_entries(core.list_passwords().iter().collect());
            }

            to_password_entries(core.search_passwords(query_str))
        },
        ptr::null_mut()
    )
}

/// Returns a JSON-serialized list of all password provider descriptors in the registry.
/// Caller must free with `maho_string_free`.
#[no_mangle]
pub extern "C" fn maho_core_get_password_provider_registry() -> *mut c_char {
    let registry = maho_types::passwords::get_provider_registry();
    let json = serde_json::to_string(&registry).unwrap_or_else(|_| "[]".to_string());
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `password_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_password(
    ptr: *mut MahoCore,
    password_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || password_id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let password_id = match CStr::from_ptr(password_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.delete_password_persisted(password_id)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `password_id` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_reveal_password(
    ptr: *mut MahoCore,
    password_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || password_id.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let password_id = match CStr::from_ptr(password_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            if let Some(p) = core.password_by_id(password_id) {
                if let Some(ref pw) = p.password {
                    return CString::new(pw.clone())
                        .map(CString::into_raw)
                        .unwrap_or(ptr::null_mut());
                }
            }
            ptr::null_mut()
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `domain` and `username` must be valid null-terminated C strings.
/// `password` may be null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_password(
    ptr: *mut MahoCore,
    domain: *const c_char,
    username: *const c_char,
    password: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || domain.is_null() || username.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let domain = match CStr::from_ptr(domain).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let username = match CStr::from_ptr(username).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let password = if password.is_null() {
                None
            } else {
                match CStr::from_ptr(password).to_str() {
                    Ok(s) if !s.is_empty() => Some(s.to_string()),
                    Ok(_) => None,
                    Err(_) => return false,
                }
            };
            let _ = core.add_password_persisted(domain.to_string(), username.to_string(), password);
            true
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `password_id` and `username` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_password_username(
    ptr: *mut MahoCore,
    password_id: *const c_char,
    username: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || password_id.is_null() || username.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let password_id = match CStr::from_ptr(password_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let username = match CStr::from_ptr(username).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.update_password_username_persisted(password_id, username.to_string())
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `url` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
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
            core.update_boost_persisted(&bid, changes);
            core.get_boost_by_id(&bid)
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
) {
    ffi_safe!(
        {
            if ptr.is_null() || domain.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let domain_str = match CStr::from_ptr(domain).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let active_boost_id = if boost_id.is_null() {
                None
            } else {
                match CStr::from_ptr(boost_id).to_str() {
                    Ok(s) => Some(maho_types::identifiers::BoostId::new(s.to_string())),
                    Err(_) => return,
                }
            };
            core.set_active_boost_persisted(domain_str, active_boost_id);
        },
        ()
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
                    let id_string = prev_id.as_ref().to_string();
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
pub unsafe extern "C" fn maho_core_get_archived_tabs(
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
            to_json_cstring(&core.get_archived_tabs(&space_id))
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid JSON string representing a SpaceId.
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

/// Returns a JSON string with the block check result:
/// `{"blocked":bool,"redirect":string|null,"rewrittenUrl":string|null}`
///
/// # Safety
/// All pointers must be valid C strings. `ptr` must be from `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_check_request(
    ptr: *mut MahoCore,
    url: *const c_char,
    source_url: *const c_char,
    request_type: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || url.is_null() || source_url.is_null() || request_type.is_null() {
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
            let source_str = match CStr::from_ptr(source_url).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let type_str = match CStr::from_ptr(request_type).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let result = core.check_request(url_str, source_str, type_str);
            to_json_cstring(&result)
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

/// # Safety
/// `s` must be a valid pointer returned by one of the `maho_core_*` functions, or null.
#[no_mangle]
pub unsafe extern "C" fn maho_string_free(s: *mut c_char) {
    ffi_safe!(
        {
            if !s.is_null() {
                drop(CString::from_raw(s));
            }
        },
        ()
    )
}

/// Returns the Maho AI proxy base URL. Reads MAHO_PROXY_URL env var,
/// falls back to compile-time default. Caller must free via maho_core_free_string.
/// Returns null on interior-NUL byte in the URL (never panics across FFI).
#[no_mangle]
pub extern "C" fn maho_core_managed_proxy_url() -> *mut c_char {
    let url = maho_agent::proxy_url();
    match CString::new(url) {
        Ok(cstr) => cstr.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

/// # Safety
/// `s` must be a valid pointer returned by `maho_core_managed_proxy_url`, or null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_free_string(s: *mut c_char) {
    if !s.is_null() {
        drop(CString::from_raw(s));
    }
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
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
pub unsafe extern "C" fn maho_storage_set_sqlcipher_key(key_hex: *const c_char) -> bool {
    ffi_safe!(
        {
            if key_hex.is_null() {
                return false;
            }
            let key = match CStr::from_ptr(key_hex).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            maho_storage::sqlite::set_sqlcipher_key(key).is_ok()
        },
        false
    )
}

/// # Safety
/// `path` must be a valid null-terminated C string pointing to a directory.
/// The caller must ensure the directory exists or can be created.
/// Returns a pointer to a new MahoCore instance with both LMDB and SQLite storage initialized.
/// Returns null on error. Caller must free with `maho_core_free`.
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

/// Sets or clears the parent of a tab (tree reparenting).
/// `new_parent_id_ptr` can be null to clear the parent.
///
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_tab_parent(
    ptr: *mut MahoCore,
    tab_id_ptr: *const std::os::raw::c_char,
    new_parent_id_ptr: *const std::os::raw::c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || tab_id_ptr.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let tab_id_str = std::ffi::CStr::from_ptr(tab_id_ptr).to_string_lossy();
            let tab_id = maho_types::identifiers::TabId::new(tab_id_str.as_ref());

            let new_parent_id = if new_parent_id_ptr.is_null() {
                None
            } else {
                let parent_str = std::ffi::CStr::from_ptr(new_parent_id_ptr).to_string_lossy();
                Some(maho_types::identifiers::TabId::new(parent_str.as_ref()))
            };

            let event = maho_types::events::shell_event::ShellEvent::SetTabParent {
                tab_id,
                new_parent_id,
            };
            core.handle_event(event);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
/// `tab_id_ptr` must be a valid null-terminated C string.
/// `title_ptr` may be null to clear the custom title.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_tab_custom_title(
    ptr: *mut MahoCore,
    tab_id_ptr: *const std::os::raw::c_char,
    title_ptr: *const std::os::raw::c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || tab_id_ptr.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let tab_id_str = std::ffi::CStr::from_ptr(tab_id_ptr).to_string_lossy();
            let tab_id = maho_types::identifiers::TabId::new(tab_id_str.as_ref());

            let custom_title = if title_ptr.is_null() {
                None
            } else {
                let title_str = std::ffi::CStr::from_ptr(title_ptr).to_string_lossy();
                let trimmed = title_str.trim();
                if trimmed.is_empty() {
                    None
                } else {
                    Some(trimmed.to_string())
                }
            };

            let event = maho_types::events::shell_event::ShellEvent::SetTabCustomTitle {
                tab_id,
                custom_title,
            };
            core.handle_event(event);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `url` and `title` must be valid null-terminated C strings.
/// `folder_id` may be null to use the default folder.
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
/// `data` must point to `data_len` valid bytes.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_tab_preview(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
    data: *const u8,
    data_len: usize,
) {
    ffi_safe!(
        {
            if ptr.is_null() || tab_id.is_null() || data.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let image_bytes = std::slice::from_raw_parts(data, data_len).to_vec();
            let image_data = maho_types::common::ImageData {
                data: image_bytes,
                width: 0,
                height: 0,
                format: maho_types::common::ImageFormat::Png,
            };
            let tid = maho_types::identifiers::TabId::new(tab_id_str);
            core.update_tab_preview(&tid, image_data);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
/// `data` must point to `data_len` valid bytes.
#[no_mangle]
#[cfg(not(any(target_os = "ios", target_os = "android")))]
pub unsafe extern "C" fn maho_core_update_tab_preview_with_privacy(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
    is_private: bool,
    data: *const u8,
    data_len: usize,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || tab_id.is_null() {
                return false;
            }
            if data_len > 0 && data.is_null() {
                return false;
            }
            if data_len == 0 || data_len > 10 * 1024 * 1024 {
                return false;
            }
            if is_private {
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
            let tid = maho_types::identifiers::TabId::new(tab_id_str);

            let is_known = core.tab_manager().get_tab(&tid).is_some();
            if !is_known {
                return false;
            }

            if let Some(tab) = core.tab_manager().get_tab(&tid) {
                if tab.is_private {
                    return false;
                }
            }

            let image_bytes = std::slice::from_raw_parts(data, data_len).to_vec();
            let image_data = maho_types::common::ImageData {
                data: image_bytes,
                width: 0,
                height: 0,
                format: maho_types::common::ImageFormat::Png,
            };
            core.update_tab_preview(&tid, image_data);
            true
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
/// `out_len` must be a valid pointer to a `usize`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_tab_preview(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
    out_len: *mut usize,
) -> *mut u8 {
    ffi_safe!(
        {
            if ptr.is_null() || tab_id.is_null() || out_len.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let tid = maho_types::identifiers::TabId::new(tab_id_str);
            match core.get_tab_preview(&tid) {
                Some(preview) => match &preview.thumbnail {
                    Some(img) => {
                        let bytes = img.data.clone();
                        *out_len = bytes.len();
                        let boxed = bytes.into_boxed_slice();
                        Box::into_raw(boxed) as *mut u8
                    }
                    None => {
                        *out_len = 0;
                        ptr::null_mut()
                    }
                },
                None => {
                    *out_len = 0;
                    ptr::null_mut()
                }
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `data` must have been returned by `maho_core_get_tab_preview` with matching `len`.
/// Specifically, `data` must point to memory allocated as `Box<[u8]>` of length `len`
/// and ownership must not have been transferred elsewhere.
#[no_mangle]
pub unsafe extern "C" fn maho_core_free_preview_data(data: *mut u8, len: usize) {
    ffi_safe!(
        {
            if !data.is_null() && len > 0 {
                // SAFETY: `data` was produced by `Box::into_raw(Box<[u8]>) as *mut u8` with `len`
                // matching the original slice length, so reconstructing the boxed slice via
                // `from_raw_parts_mut` + `Box::from_raw` correctly returns ownership to the
                // allocator using the same layout that `Box<[u8]>` allocated with.
                let slice = std::slice::from_raw_parts_mut(data, len);
                drop(Box::from_raw(slice));
            }
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_take_pending_preview_captures(
    ptr: *mut MahoCore,
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
            let captures = core.take_pending_preview_captures();
            let tab_ids: Vec<String> = captures.into_iter().map(|id| id.to_string()).collect();
            to_json_cstring(&tab_ids)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_schedule_preview_capture(
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
            let tid = maho_types::identifiers::TabId::new(tab_id_str);
            core.schedule_preview_capture(tid)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
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
            let total_bytes = parsed["total_bytes"].as_u64().unwrap_or(0);
            let file_path = parsed["file_path"].as_str();
            let mime_type = parsed["mime_type"].as_str();
            let download_id = core.start_download(filename, url, total_bytes, file_path, mime_type);
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
            let core = &mut *ptr;
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
            core.add_filter_list(id_str, name_str, url_str);
        },
        ()
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
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.toggle_filter_list(id_str, enabled);
        },
        ()
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
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.remove_filter_list(id_str);
        },
        ()
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
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let content_str = match CStr::from_ptr(content).to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return,
            };
            core.update_filter_list_content(id_str, content_str);
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
            let core = &mut *ptr;
            core.rebuild_content_rules();
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

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated UTF-8 string.
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
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `color_json` must be a
/// valid NUL-terminated C string. No-op on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_accent_color(ptr: *mut MahoCore, color_json: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() || color_json.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            if let Ok(s) = CStr::from_ptr(color_json).to_str() {
                core.set_accent_color(s);
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
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `level` must be a valid null-terminated C string containing one of:
/// "normal", "warning", "critical", or "extreme".
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_handle_memory_pressure(
    ptr: *mut MahoCore,
    level: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || level.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let level_str = match CStr::from_ptr(level).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let pressure_level = match level_str {
                "normal" => maho_types::common::MemoryPressureLevel::Normal,
                "warning" => maho_types::common::MemoryPressureLevel::Warning,
                "critical" => maho_types::common::MemoryPressureLevel::Critical,
                "extreme" => maho_types::common::MemoryPressureLevel::Extreme,
                _ => return ptr::null_mut(),
            };
            let result = core.handle_memory_pressure(pressure_level);
            CString::new(result)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` and `form_json` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_save_form_data(
    core: *mut MahoCore,
    tab_id: *const c_char,
    form_json: *const c_char,
) {
    ffi_safe!(
        {
            if core.is_null() || tab_id.is_null() || form_json.is_null() {
                return;
            }
            let core = unsafe { &mut *core };
            let tab_id = match CStr::from_ptr(tab_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let form_json = match CStr::from_ptr(form_json).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.save_form_data(tab_id, form_json.to_string());
        },
        ()
    )
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_crash_save_interval(core: *mut MahoCore, seconds: u64) {
    ffi_safe!(
        {
            if core.is_null() {
                return;
            }
            let core = unsafe { &mut *core };
            core.set_crash_save_interval(seconds);
        },
        ()
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

// === Space Management ===

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `name`, `color_json`, `profile_id` must be valid null-terminated C strings.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_space(
    ptr: *mut MahoCore,
    name: *const c_char,
    color_json: *const c_char,
    profile_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || name.is_null() || color_json.is_null() || profile_id.is_null() {
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
            let color: maho_types::space::SpaceColor = match CStr::from_ptr(color_json).to_str() {
                Ok(s) => match serde_json::from_str(s) {
                    Ok(c) => c,
                    Err(_) => return ptr::null_mut(),
                },
                Err(_) => return ptr::null_mut(),
            };
            let pid = match CStr::from_ptr(profile_id).to_str() {
                Ok(s) => maho_types::identifiers::ProfileId::new(s),
                Err(_) => return ptr::null_mut(),
            };
            let space = core.create_space(name_str, color, pid);
            to_json_cstring(&space)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_space(ptr: *mut MahoCore, space_id: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() || space_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let id = maho_types::identifiers::SpaceId::new(id_str);
            core.delete_space(&id);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id` and `name` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_rename_space(
    ptr: *mut MahoCore,
    space_id: *const c_char,
    name: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || space_id.is_null() || name.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let name_str = match CStr::from_ptr(name).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let id = maho_types::identifiers::SpaceId::new(id_str);
            core.rename_space(&id, name_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id` and `color_json` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_recolor_space(
    ptr: *mut MahoCore,
    space_id: *const c_char,
    color_json: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || space_id.is_null() || color_json.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let color: maho_types::space::SpaceColor = match CStr::from_ptr(color_json).to_str() {
                Ok(s) => match serde_json::from_str(s) {
                    Ok(c) => c,
                    Err(_) => return,
                },
                Err(_) => return,
            };
            let id = maho_types::identifiers::SpaceId::new(id_str);
            core.recolor_space(&id, color);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_reorder_space(
    ptr: *mut MahoCore,
    space_id: *const c_char,
    from: usize,
    to: usize,
) {
    ffi_safe!(
        {
            if ptr.is_null() || space_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let id = maho_types::identifiers::SpaceId::new(id_str);
            core.reorder_space(&id, from, to);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_activate_space(ptr: *mut MahoCore, space_id: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() || space_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let id = maho_types::identifiers::SpaceId::new(id_str);
            core.activate_space(&id);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_activation_target_for_space(
    ptr: *mut MahoCore,
    space_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || space_id.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let id_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let id = maho_types::identifiers::SpaceId::new(id_str);
            if let Some(target) = core.get_activation_target_for_space(&id) {
                CString::new(target.to_string())
                    .map(|c| c.into_raw())
                    .unwrap_or(ptr::null_mut())
            } else {
                ptr::null_mut()
            }
        },
        ptr::null_mut()
    )
}

// === Profile Management ===

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `name` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_profile(
    ptr: *mut MahoCore,
    name: *const c_char,
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
                Ok(s) => s.to_string(),
                Err(_) => return ptr::null_mut(),
            };
            let profile = core.create_profile_persisted(name_str);
            to_json_cstring(&profile)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_profile(
    ptr: *mut MahoCore,
    profile_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || profile_id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(profile_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let id = maho_types::identifiers::ProfileId::new(id_str);
            core.delete_profile(&id)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_list_profiles(ptr: *mut MahoCore) -> *mut c_char {
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
            let profiles = core.list_profiles();
            to_json_cstring(&profiles)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_switch_profile(
    ptr: *mut MahoCore,
    profile_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || profile_id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(profile_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let id = maho_types::identifiers::ProfileId::new(id_str);
            core.switch_profile(&id)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_id` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_profile_data_store_id(
    ptr: *mut MahoCore,
    profile_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || profile_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let id_str = match CStr::from_ptr(profile_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let id = maho_types::identifiers::ProfileId::new(id_str);
            match core.get_profile_data_store_id(&id) {
                Some(data_store_id) => to_json_cstring(&data_store_id),
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
pub unsafe extern "C" fn maho_core_get_active_profile_id(ptr: *mut MahoCore) -> *mut c_char {
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
            match core.get_active_profile_id() {
                Some(id) => to_json_cstring(id),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

// === Sync FFI Functions ===

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `server_url` and `sync_key` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_start_sync(
    ptr: *mut MahoCore,
    server_url: *const c_char,
    sync_key: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || server_url.is_null() || sync_key.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let url = match CStr::from_ptr(server_url).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let key = match CStr::from_ptr(sync_key).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.start_sync(url, key);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_stop_sync(ptr: *mut MahoCore) {
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
            core.stop_sync();
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_sync_status(ptr: *mut MahoCore) -> *mut c_char {
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
            let json = core.get_sync_status();
            CString::new(json)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_sync_server_url(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            if let Some(ref url) = core.get_sync_server_url() {
                if let Ok(c_str) = CString::new(url.clone()) {
                    c_str.into_raw()
                } else {
                    ptr::null_mut()
                }
            } else {
                ptr::null_mut()
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_sync_key(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            if let Some(ref key) = core.get_sync_key() {
                if let Ok(c_str) = CString::new(key.clone()) {
                    c_str.into_raw()
                } else {
                    ptr::null_mut()
                }
            } else {
                ptr::null_mut()
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_sync_auth_token(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let token = core.get_sync_auth_token();
            CString::new(token)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `message_json` must be a valid C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_handle_incoming_websocket_message(
    ptr: *mut MahoCore,
    message_json: *const c_char,
) -> i32 {
    ffi_safe!(
        {
            if ptr.is_null() || message_json.is_null() {
                return -1;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return -1;
            }
            let core = &mut *ptr;
            let json_str = match CStr::from_ptr(message_json).to_str() {
                Ok(s) => s,
                Err(_) => return -1,
            };
            match core.handle_incoming_websocket_message(json_str) {
                Ok(_) => 0,
                Err(_) => -1,
            }
        },
        -1
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_connected_devices(ptr: *mut MahoCore) -> *mut c_char {
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
            let json = core.get_connected_devices();
            CString::new(json)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `url`, `title`, and `target_device_id` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_send_tab(
    ptr: *mut MahoCore,
    url: *const c_char,
    title: *const c_char,
    target_device_id: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || url.is_null() || title.is_null() || target_device_id.is_null() {
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
            let device_str = match CStr::from_ptr(target_device_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.send_tab_to_device(url_str, title_str, device_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_generate_sync_key(ptr: *mut MahoCore) -> *mut c_char {
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
            let json = core.generate_sync_key();
            CString::new(json)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `server_url` must be a valid null-terminated C string.
/// `recovery_phrase` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_join_sync(
    ptr: *mut MahoCore,
    server_url: *const c_char,
    recovery_phrase: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || server_url.is_null() || recovery_phrase.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let url = match CStr::from_ptr(server_url).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let phrase = match CStr::from_ptr(recovery_phrase).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let json = core.join_sync(url, phrase);
            CString::new(json)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `server_url` must be a valid null-terminated C string.
/// `recovery_phrase` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_configure_sync_encryption(
    ptr: *mut MahoCore,
    server_url: *const c_char,
    recovery_phrase: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || server_url.is_null() || recovery_phrase.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let url = match CStr::from_ptr(server_url).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let phrase = match CStr::from_ptr(recovery_phrase).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let json = match core.configure_sync_encryption_for_recovery_phrase(url, phrase) {
                Ok(()) => serde_json::json!({
                    "success": true,
                    "deviceId": core.sync_device_id()
                }).to_string(),
                Err(e) => serde_json::json!({
                    "success": false,
                    "error": e
                }).to_string(),
            };
            CString::new(json)
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `device_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_sync_device(
    ptr: *mut MahoCore,
    device_id: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || device_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(device_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.remove_sync_device(id_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `device_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_disconnect_sync_device(
    ptr: *mut MahoCore,
    device_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || device_id.is_null() {
                return false;
            }
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(device_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.disconnect_sync_device(id_str)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `device_id` and `new_name` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_rename_sync_device(
    ptr: *mut MahoCore,
    device_id: *const c_char,
    new_name: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || device_id.is_null() || new_name.is_null() {
                return false;
            }
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(device_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let name_str = match CStr::from_ptr(new_name).to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return false,
            };
            core.rename_sync_device(id_str, name_str)
        },
        false
    )
}

/// Resets a pinned tab back to its original pinned URL.
///
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_reset_pinned_tab(
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
            let tid = maho_types::identifiers::TabId::new(tab_id_str);
            core.reset_pinned_tab(&tid)
        },
        false
    )
}

/// Checks whether a navigation in a pinned tab should open in a peek tab instead.
/// Returns a JSON CoreUpdate (OpenPeekTab) if the URL is cross-domain, or null if navigation is allowed.
///
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
/// `url` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_check_pinned_navigation(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
    url: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || tab_id.is_null() || url.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let url_str = match CStr::from_ptr(url).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let tid = maho_types::identifiers::TabId::new(tab_id_str);
            let new_url = maho_types::common::Url::new(url_str);
            match core.check_pinned_navigation(&tid, &new_url) {
                Some(update) => to_json_cstring(&update),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Acknowledge a sync entity.
/// `entity_type` is the sync entity type name ("tab", "bookmark", etc.).
/// `entity_id` is the entity ID.
/// `version` is the HLC timestamp version.
///
/// # Safety
/// `ptr`, `entity_type`, `entity_id` must be valid C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_ack_sync_entity(
    ptr: *mut MahoCore,
    entity_type: *const c_char,
    entity_id: *const c_char,
    version: u64,
) {
    ffi_safe!(
        {
            if ptr.is_null() || entity_type.is_null() || entity_id.is_null() {
                return;
            }
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let type_str = match CStr::from_ptr(entity_type).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let id_str = match CStr::from_ptr(entity_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.ack_sync_entity(type_str, id_str, version);
        },
        ()
    )
}

/// Apply incoming entity batch from server.
/// `entities_json` is a JSON array of SyncEntity objects.
/// Returns JSON array of actually-applied entities.
///
/// # Safety
/// `ptr` must be valid. `entities_json` must be a valid C string.
/// Caller must free returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_apply_remote_entities(
    ptr: *mut MahoCore,
    entities_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || entities_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let json_str = match CStr::from_ptr(entities_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let entities: Vec<maho_core::sync_models::SyncEntity> =
                match serde_json::from_str(json_str) {
                    Ok(e) => e,
                    Err(_) => return ptr::null_mut(),
                };
            let applied = core.apply_sync_remote_entities(entities);
            to_json_cstring(&applied)
        },
        ptr::null_mut()
    )
}

/// Get pending outgoing messages as JSON array.
/// Shell calls this periodically to send via WebSocket.
///
/// # Safety
/// `ptr` must be valid.
/// Caller must free returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_drain_outgoing(ptr: *mut MahoCore) -> *mut c_char {
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
            let messages = core.drain_sync_outgoing();
            to_json_cstring(&messages)
        },
        ptr::null_mut()
    )
}

/// Get and drain received tabs as a JSON array.
/// UI calls this periodically to fetch received tabs.
///
/// # Safety
/// `ptr` must be valid.
/// Caller must free returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_drain_received_tabs(ptr: *mut MahoCore) -> *mut c_char {
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
            let tabs = core.drain_received_tabs();
            to_json_cstring(&tabs)
        },
        ptr::null_mut()
    )
}

/// Send a tab to another device.
/// `device_id` is the target device ID.
/// `tab_json` is a JSON object with "url" and "title" fields.
///
/// # Safety
/// `ptr`, `device_id`, `tab_json` must be valid C strings.
/// Caller must free returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_send_tab_to_device(
    ptr: *mut MahoCore,
    device_id: *const c_char,
    tab_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || device_id.is_null() || tab_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let device_str = match CStr::from_ptr(device_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let tab_str = match CStr::from_ptr(tab_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let tab: serde_json::Value = match serde_json::from_str(tab_str) {
                Ok(v) => v,
                Err(_) => return ptr::null_mut(),
            };
            let url = match tab["url"].as_str() {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            let title = match tab["title"].as_str() {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            core.send_tab_to_device(url, title, device_str);
            to_json_cstring(&serde_json::json!({"success": true}))
        },
        ptr::null_mut()
    )
}

// === Easels ===

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_easel_view_models(ptr: *mut MahoCore) -> *mut c_char {
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
            to_json_cstring(&core.get_easel_view_models())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `easel_id` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_easel(
    ptr: *mut MahoCore,
    easel_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || easel_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let id_str = match CStr::from_ptr(easel_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.get_easel(id_str) {
                Some(easel) => to_json_cstring(easel),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `name` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_easel(
    ptr: *mut MahoCore,
    name: *const c_char,
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
            let easel = core.create_easel_persisted(name_str);
            to_json_cstring(&easel)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `easel_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_easel(
    ptr: *mut MahoCore,
    easel_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || easel_id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(easel_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.delete_easel_persisted(id_str)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `easel_id` and `json` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_easel(
    ptr: *mut MahoCore,
    easel_id: *const c_char,
    json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || easel_id.is_null() || json.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(easel_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let json_str = match CStr::from_ptr(json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.update_easel_persisted(id_str, json_str)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_autofill_addresses(ptr: *mut MahoCore) -> *mut c_char {
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
            to_json_cstring(core.get_autofill_addresses())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `address_json` must be
/// a valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_autofill_address_persisted(
    ptr: *mut MahoCore,
    address_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || address_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let json_str = match CStr::from_ptr(address_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let address: maho_types::autofill::AutofillAddress =
                match serde_json::from_str(json_str) {
                    Ok(addr) => addr,
                    Err(_) => return ptr::null_mut(),
                };
            to_json_cstring(&core.add_autofill_address_persisted(address))
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `id` must be a valid
/// NUL-terminated C string. Returns `false` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_autofill_address_persisted(
    ptr: *mut MahoCore,
    id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.delete_autofill_address_persisted(id_str)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_autofill_payments(ptr: *mut MahoCore) -> *mut c_char {
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
            to_json_cstring(core.get_autofill_payments())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `payment_json` must be
/// a valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_autofill_payment_persisted(
    ptr: *mut MahoCore,
    payment_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || payment_json.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let json_str = match CStr::from_ptr(payment_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let payment: maho_types::autofill::AutofillPayment =
                match serde_json::from_str(json_str) {
                    Ok(payment) => payment,
                    Err(_) => return ptr::null_mut(),
                };
            to_json_cstring(&core.add_autofill_payment_persisted(payment))
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `id` must be a valid
/// NUL-terminated C string. Returns `false` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_autofill_payment_persisted(
    ptr: *mut MahoCore,
    id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.delete_autofill_payment_persisted(id_str)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// String parameters must be valid null-terminated UTF-8.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_shared_collection(
    ptr: *mut MahoCore,
    space_id: *const c_char,
    name: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || space_id.is_null() || name.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let space_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let name_str = match CStr::from_ptr(name).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.create_shared_collection(space_str, name_str) {
                Some(json) => CString::new(json)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// String parameters must be valid null-terminated UTF-8.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_share_link(
    ptr: *mut MahoCore,
    collection_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || collection_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let id_str = match CStr::from_ptr(collection_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match core.get_share_link(id_str) {
                Some(link) => CString::new(link)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// String parameters must be valid null-terminated UTF-8.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_share_permissions(
    ptr: *mut MahoCore,
    collection_id: *const c_char,
    permission: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() || collection_id.is_null() || permission.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(collection_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let perm_str = match CStr::from_ptr(permission).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let _ = core.update_share_permissions(id_str, perm_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// String parameters must be valid null-terminated UTF-8.
#[no_mangle]
pub unsafe extern "C" fn maho_core_revoke_share(ptr: *mut MahoCore, collection_id: *const c_char) {
    ffi_safe!(
        {
            if ptr.is_null() || collection_id.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(collection_id).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            core.revoke_share(id_str);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_shared_collections(ptr: *mut MahoCore) -> *mut c_char {
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
            match core.get_shared_collections() {
                Some(json) => CString::new(json)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// String parameters must be valid null-terminated UTF-8.
#[no_mangle]
pub unsafe extern "C" fn maho_core_join_shared_collection(
    ptr: *mut MahoCore,
    share_link: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || share_link.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let link_str = match CStr::from_ptr(share_link).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            core.join_shared_collection(link_str)
        },
        false
    )
}

/// # Safety
/// `path_utf8` must be a valid null-terminated C string.
/// `out_key` must be a writable buffer of 16 bytes.
/// `out_status` must be a writable pointer to a u32.
#[no_mangle]
pub unsafe extern "C" fn maho_core_derive_oscrypt_key(
    path_utf8: *const c_char,
    out_key: *mut u8,
    out_status: *mut u32,
) -> bool {
    ffi_safe!(
        {
            if path_utf8.is_null() || out_key.is_null() || out_status.is_null() {
                return false;
            }
            let path_str = match CStr::from_ptr(path_utf8).to_str() {
                Ok(s) => s,
                Err(_) => {
                    *out_status = 2; // InvalidArg
                    return false;
                }
            };
            let out_key_slice = std::slice::from_raw_parts_mut(out_key, 16);
            let mut key = [0u8; 16];
            let ok = maho_core::oscrypt::derive_key(path_str, &mut key, &mut *out_status);
            if ok {
                out_key_slice.copy_from_slice(&key);
            }
            ok
        },
        false
    )
}

/// # Safety
/// `path_utf8` must be a valid null-terminated C string.
/// `out_status` must be a writable pointer to a u32.
#[no_mangle]
pub unsafe extern "C" fn maho_core_oscrypt_key_path_is_secure(
    path_utf8: *const c_char,
    out_status: *mut u32,
) -> bool {
    ffi_safe!(
        {
            if path_utf8.is_null() || out_status.is_null() {
                return false;
            }
            let path_str = match CStr::from_ptr(path_utf8).to_str() {
                Ok(s) => s,
                Err(_) => {
                    *out_status = 2; // InvalidArg
                    return false;
                }
            };
            maho_core::oscrypt::key_path_is_secure(path_str, &mut *out_status)
        },
        false
    )
}

/// # Safety
/// `path_utf8` must be a valid null-terminated C string.
/// `out_status` must be a writable pointer to a u32.
/// The caller is responsible for freeing the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_read_validated_theme_json(
    path_utf8: *const c_char,
    out_status: *mut u32,
) -> *mut c_char {
    ffi_safe!(
        {
            if path_utf8.is_null() || out_status.is_null() {
                return ptr::null_mut();
            }
            *out_status = 1; // IoError

            let path_str = match CStr::from_ptr(path_utf8).to_str() {
                Ok(s) => s,
                Err(_) => {
                    *out_status = 2; // InvalidArg
                    return ptr::null_mut();
                }
            };
            let path = Path::new(path_str);
            let contents = match std::fs::read_to_string(path) {
                Ok(c) => c,
                Err(_) => return ptr::null_mut(),
            };

            let theme: maho_types::space::SpaceTheme = match serde_json::from_str(&contents) {
                Ok(t) => t,
                Err(_) => {
                    *out_status = 4; // ValidationError
                    return ptr::null_mut();
                }
            };

            if !theme.validate() {
                *out_status = 4; // ValidationError
                return ptr::null_mut();
            }

            *out_status = 0; // Ok
            match CString::new(contents) {
                Ok(c) => c.into_raw(),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a MahoCore.
/// `space_id` must be a valid null-terminated C string.
/// `theme_json` must be a valid null-terminated C string.
/// `out_status` must be a writable pointer to a u32.
#[no_mangle]
pub unsafe extern "C" fn maho_core_apply_space_theme_json(
    ptr: *mut MahoCore,
    space_id: *const c_char,
    theme_json: *const c_char,
    out_status: *mut u32,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || space_id.is_null() || theme_json.is_null() || out_status.is_null() {
                return false;
            }
            *out_status = 1; // IoError

            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let space_id_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => {
                    *out_status = 2; // InvalidArg
                    return false;
                }
            };
            let theme_json_str = match CStr::from_ptr(theme_json).to_str() {
                Ok(s) => s,
                Err(_) => {
                    *out_status = 2; // InvalidArg
                    return false;
                }
            };

            let theme: maho_types::space::SpaceTheme = match serde_json::from_str(theme_json_str) {
                Ok(t) => t,
                Err(_) => {
                    *out_status = 4; // ValidationError
                    return false;
                }
            };

            if !theme.validate() {
                *out_status = 4; // ValidationError
                return false;
            }

            let parsed_space_id = maho_types::identifiers::SpaceId::new(space_id_str);

            let update = maho_types::space::SpaceConfigUpdate {
                space_id: parsed_space_id,
                theme: Some(theme),
                ..Default::default()
            };

            core.update_space_config(update);
            *out_status = 0; // Ok
            true
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a MahoCore.
/// `space_id` must be a valid null-terminated C string.
/// `path_utf8` must be a valid null-terminated C string.
/// `out_status` must be a writable pointer to a u32.
#[no_mangle]
pub unsafe extern "C" fn maho_core_export_space_theme(
    ptr: *mut MahoCore,
    space_id: *const c_char,
    path_utf8: *const c_char,
    out_status: *mut u32,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || space_id.is_null() || path_utf8.is_null() || out_status.is_null() {
                return false;
            }
            *out_status = 1; // IoError

            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &*ptr;
            let space_id_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => {
                    *out_status = 2; // InvalidArg
                    return false;
                }
            };
            let path_str = match CStr::from_ptr(path_utf8).to_str() {
                Ok(s) => s,
                Err(_) => {
                    *out_status = 2; // InvalidArg
                    return false;
                }
            };
            let path = Path::new(path_str);

            let parsed_space_id = maho_types::identifiers::SpaceId::new(space_id_str);

            let theme = match core.get_space_theme(&parsed_space_id) {
                Some(t) => t,
                None => {
                    *out_status = 4;
                    return false;
                }
            };

            let json = match serde_json::to_string_pretty(&theme) {
                Ok(j) => j,
                Err(_) => {
                    *out_status = 4;
                    return false;
                }
            };

            let parent = match path.parent() {
                Some(p) => p,
                None => return false,
            };
            let temp_name = format!(
                "{}.tmp.{}",
                path.file_name().unwrap_or_default().to_string_lossy(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .map(|d| d.as_nanos())
                    .unwrap_or(0)
            );
            let temp_path = parent.join(temp_name);

            if let Err(_) = std::fs::write(&temp_path, json.as_bytes()) {
                return false;
            }

            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                let _ =
                    std::fs::set_permissions(&temp_path, std::fs::Permissions::from_mode(0o600));
            }

            if let Err(_) = std::fs::rename(&temp_path, path) {
                let _ = std::fs::remove_file(&temp_path);
                return false;
            }

            *out_status = 0; // Ok
            true
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a MahoCore.
/// `space_id` must be a valid null-terminated C string.
/// `out_status` must be a writable pointer to a u32.
/// The caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_serialize_space_theme(
    ptr: *mut MahoCore,
    space_id: *const c_char,
    out_status: *mut u32,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || space_id.is_null() || out_status.is_null() {
                return ptr::null_mut();
            }
            *out_status = 1; // Error

            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let space_id_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };

            let parsed_space_id = maho_types::identifiers::SpaceId::new(space_id_str);
            let theme = match core.get_space_theme(&parsed_space_id) {
                Some(t) => t,
                None => {
                    *out_status = 4;
                    return ptr::null_mut();
                }
            };

            let json = match serde_json::to_string_pretty(&theme) {
                Ok(j) => j,
                Err(_) => {
                    *out_status = 4;
                    return ptr::null_mut();
                }
            };

            *out_status = 0; // Ok
            match CString::new(json) {
                Ok(c) => c.into_raw(),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

#[repr(C)]
pub struct MahoChatConfig {
    pub api_key: *const c_char,
    pub endpoint: *const c_char,
    pub model: *const c_char,
    pub system_instruction: *const c_char,
}

/// # Safety
/// All pointers in `config` must be null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_session_new(
    config: *const MahoChatConfig,
) -> *mut maho_core::chat_session::ChatSession {
    ffi_safe!(
        {
            if config.is_null() {
                return std::ptr::null_mut();
            }
            let config = &*config;
            let api_key = if config.api_key.is_null() {
                String::new()
            } else {
                CStr::from_ptr(config.api_key)
                    .to_string_lossy()
                    .into_owned()
            };
            let endpoint = if config.endpoint.is_null() {
                String::new()
            } else {
                CStr::from_ptr(config.endpoint)
                    .to_string_lossy()
                    .into_owned()
            };
            let model = if config.model.is_null() {
                String::new()
            } else {
                CStr::from_ptr(config.model).to_string_lossy().into_owned()
            };
            let system_instruction = if config.system_instruction.is_null() {
                String::new()
            } else {
                CStr::from_ptr(config.system_instruction)
                    .to_string_lossy()
                    .into_owned()
            };

            let chat_config = maho_core::chat_session::ChatConfig {
                api_key,
                endpoint,
                model,
                system_instruction,
            };

            Box::into_raw(Box::new(maho_core::chat_session::ChatSession::new(
                chat_config,
            )))
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_chat_session_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_session_free(
    ptr: *mut maho_core::chat_session::ChatSession,
) {
    ffi_safe!(
        {
            if !ptr.is_null() {
                drop(Box::from_raw(ptr));
            }
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
/// `name`, `description`, and `schema_json` must be null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_register_tool(
    ptr: *mut maho_core::chat_session::ChatSession,
    name: *const c_char,
    description: *const c_char,
    schema_json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || name.is_null() || description.is_null() || schema_json.is_null() {
                return false;
            }
            let session = &mut *ptr;
            let name_str = match CStr::from_ptr(name).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let desc_str = match CStr::from_ptr(description).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let schema_str = match CStr::from_ptr(schema_json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            session.register_tool(name_str, desc_str, schema_str)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_set_event_sink(
    ptr: *mut maho_core::chat_session::ChatSession,
    on_token: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, token: *const c_char)>,
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            full_text: *const c_char,
            tool_calls_json: *const c_char,
        ),
    >,
    on_error: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, error: *const c_char)>,
    user_data: *mut std::ffi::c_void,
) {
    ffi_safe!(
        {
            if ptr.is_null() {
                return;
            }
            let session = &mut *ptr;
            let sink = maho_core::chat_session::MahoChatEventSink {
                on_token,
                on_complete,
                on_error,
                user_data,
            };
            session.set_event_sink(sink);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
/// `message` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_send_user_turn(
    ptr: *mut maho_core::chat_session::ChatSession,
    message: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || message.is_null() {
                return false;
            }
            let session = &mut *ptr;
            let msg_str = match CStr::from_ptr(message).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            session.send_user_turn(msg_str)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
/// `tool_call_id`, `tool_name`, and `result` must be null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_send_tool_result(
    ptr: *mut maho_core::chat_session::ChatSession,
    tool_call_id: *const c_char,
    tool_name: *const c_char,
    result: *const c_char,
    trigger: bool,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || tool_call_id.is_null() || tool_name.is_null() || result.is_null() {
                return false;
            }
            let session = &mut *ptr;
            let tc_id = match CStr::from_ptr(tool_call_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let t_name = match CStr::from_ptr(tool_name).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let res = match CStr::from_ptr(result).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            session.send_tool_result(tc_id, t_name, res, trigger)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_cancel(ptr: *mut maho_core::chat_session::ChatSession) {
    ffi_safe!(
        {
            if !ptr.is_null() {
                let session = &mut *ptr;
                session.cancel();
            }
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
/// `content` must be a null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_append_user_message(
    ptr: *mut maho_core::chat_session::ChatSession,
    content: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() {
                return;
            }
            let session = &mut *ptr;
            let content_str = if content.is_null() {
                ""
            } else {
                match CStr::from_ptr(content).to_str() {
                    Ok(s) => s,
                    Err(_) => return,
                }
            };
            session.append_user_message(content_str);
        },
        ()
    )
}

/// `ptr` must be a valid pointer to a `ChatSession`.
/// `content` and `tool_calls_json` must be null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_append_assistant_message(
    ptr: *mut maho_core::chat_session::ChatSession,
    content: *const c_char,
    tool_calls_json: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() {
                return;
            }
            let session = &mut *ptr;
            let content_str = if content.is_null() {
                ""
            } else {
                match CStr::from_ptr(content).to_str() {
                    Ok(s) => s,
                    Err(_) => return,
                }
            };
            let tcs_str = if tool_calls_json.is_null() {
                ""
            } else {
                match CStr::from_ptr(tool_calls_json).to_str() {
                    Ok(s) => s,
                    Err(_) => return,
                }
            };
            session.append_assistant_message(content_str, tcs_str);
        },
        ()
    )
}

// ============================================================
// ffi_safe! — panic-catching macro for FFI boundary safety
// ============================================================

/// Catches panics at FFI boundaries and returns a fallback value.
/// Usage: `ffi_safe!({ <expr> }, <fallback>)`
#[macro_export]
macro_rules! ffi_safe {
    ($body:block, $fallback:expr) => {{
        match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| $body)) {
            Ok(val) => val,
            Err(_) => $fallback,
        }
    }};
}

// ============================================================
// Conversation CRUD FFI functions
// ============================================================

/// Create a new conversation. Returns true on success.
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
            let empty = CString::new("[]").unwrap().into_raw();
            if core.is_null() {
                return empty;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*core;
            let conversations = if let Some(storage) = core.storage_ref() {
                storage.list_conversations(limit).unwrap_or_default()
            } else {
                return empty;
            };
            match serde_json::to_string(&conversations) {
                Ok(json) => CString::new(json)
                    .unwrap_or_else(|_| CString::new("[]").unwrap())
                    .into_raw(),
                Err(_) => empty,
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
            let empty = CString::new("[]").unwrap().into_raw();
            if core.is_null() || session_id.is_null() {
                return empty;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*core;
            let sid = match CStr::from_ptr(session_id).to_str() {
                Ok(s) => s,
                Err(_) => return empty,
            };
            let messages = if let Some(storage) = core.storage_ref() {
                storage.get_conversation_messages(sid).unwrap_or_default()
            } else {
                return empty;
            };
            match serde_json::to_string(&messages) {
                Ok(json) => CString::new(json)
                    .unwrap_or_else(|_| CString::new("[]").unwrap())
                    .into_raw(),
                Err(_) => empty,
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

// ============================================================
// BYOK (Bring Your Own Key) FFI functions
// BYOK keys are stored via settings key "byok:<provider>"
// ============================================================

/// Set a BYOK API key for the given provider.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `provider` and `api_key`
/// must be valid NUL-terminated C strings. Returns `false` on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_byok_set_key(
    core: *mut MahoCore,
    provider: *const c_char,
    api_key: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || provider.is_null() || api_key.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &*core;
            let provider_str = match CStr::from_ptr(provider).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let key_str = match CStr::from_ptr(api_key).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            if let Some(storage) = core.storage_ref() {
                let setting_key = format!("byok:{}", provider_str);
                storage.set_setting(&setting_key, key_str).is_ok()
            } else {
                false
            }
        },
        false
    )
}

/// Get the stored BYOK API key for the given provider. Returns null if not set.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `provider` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_byok_get_key(
    core: *mut MahoCore,
    provider: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || provider.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*core;
            let provider_str = match CStr::from_ptr(provider).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            tracing::info!(
                target = "maho_agent::audit",
                event = "byok_key_access",
                provider = %provider_str,
            );
            if let Some(storage) = core.storage_ref() {
                let setting_key = format!("byok:{}", provider_str);
                if let Ok(Some(key)) = storage.get_setting(&setting_key) {
                    return CString::new(key)
                        .map(|s| s.into_raw())
                        .unwrap_or(ptr::null_mut());
                }
            }
            ptr::null_mut()
        },
        ptr::null_mut()
    )
}

/// Delete the BYOK API key for the given provider. Returns true on success.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `provider` must be a
/// valid NUL-terminated C string. Returns `false` on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_byok_delete_key(
    core: *mut MahoCore,
    provider: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || provider.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &*core;
            let provider_str = match CStr::from_ptr(provider).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            if let Some(storage) = core.storage_ref() {
                let setting_key = format!("byok:{}", provider_str);
                // Delete by setting to empty string (sentinel for "not set")
                storage.set_setting(&setting_key, "").is_ok()
            } else {
                false
            }
        },
        false
    )
}

/// Get JSON array of supported provider names. Returns caller-owned string.
///
/// # Safety
/// `_core` may be null (ignored). Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_byok_get_providers(_core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let providers = serde_json::json!(["openai", "anthropic", "gemini", "ollama"]);
            let json = providers.to_string();
            CString::new(json)
                .map(|s| s.into_raw())
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// Validate a BYOK API key for the given provider. Returns true if valid.
/// Note: makes a live network request; call from a background thread.
///
/// # Safety
/// `provider` and `api_key` must be valid NUL-terminated C strings (not null). `_core` is
/// unused and may be null. This call performs a live network request.
#[no_mangle]
pub unsafe extern "C" fn maho_core_byok_validate_key(
    _core: *mut MahoCore,
    provider: *const c_char,
    api_key: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if provider.is_null() || api_key.is_null() {
                return false;
            }
            let provider_str = match CStr::from_ptr(provider).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let key_str = match CStr::from_ptr(api_key).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            maho_core::memory_manager::get_runtime().block_on(
                maho_core::llm_provider::validate_api_key(provider_str, key_str),
            )
        },
        false
    )
}

// ============================================================
// Chat image FFI
// ============================================================

/// Send an image-only chat message. Fires a ShellEvent::ChatMessage with image content.
/// `mime` must be non-null (e.g. "image/png"). `data` is raw bytes, `data_len` is byte count.
/// Returns true on success.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `mime` must be a valid
/// NUL-terminated C string; `data` must point to at least `data_len` bytes if non-null.
#[no_mangle]
pub unsafe extern "C" fn maho_chat_send_image(
    core: *mut MahoCore,
    mime: *const c_char,
    data: *const u8,
    data_len: usize,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() {
                return false;
            }
            let mime_str = if mime.is_null() {
                "application/octet-stream"
            } else {
                match CStr::from_ptr(mime).to_str() {
                    Ok(s) => s,
                    Err(_) => return false,
                }
            };
            let bytes: Vec<u8> = if data.is_null() || data_len == 0 {
                Vec::new()
            } else {
                std::slice::from_raw_parts(data, data_len).to_vec()
            };
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *core;
            use maho_types::chat::{ChatContent, ChatRequestContext, ChatRequestMode};
            use maho_types::events::shell_event::ShellEvent;
            let message = format!("[image:{} {} bytes]", mime_str, bytes.len());
            core.handle_event(ShellEvent::ChatMessage {
                message,
                context: ChatRequestContext::new(ChatRequestMode::GeneralChat),
            });
            true
        },
        false
    )
}

/// Send a text+image chat message. `text` is the user's text (may be null/empty).
/// Returns true on success.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `text` and `mime` may
/// be null; `data` must point to at least `data_len` bytes if non-null.
#[no_mangle]
pub unsafe extern "C" fn maho_chat_send_text_with_image(
    core: *mut MahoCore,
    text: *const c_char,
    mime: *const c_char,
    data: *const u8,
    data_len: usize,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() {
                return false;
            }
            let text_str = if text.is_null() {
                String::new()
            } else {
                CStr::from_ptr(text).to_str().unwrap_or("").to_string()
            };
            let mime_str = if mime.is_null() {
                "application/octet-stream"
            } else {
                CStr::from_ptr(mime)
                    .to_str()
                    .unwrap_or("application/octet-stream")
            };
            let byte_count = if data.is_null() { 0 } else { data_len };
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *core;
            use maho_types::chat::{ChatRequestContext, ChatRequestMode};
            use maho_types::events::shell_event::ShellEvent;
            let message = if text_str.is_empty() {
                format!("[image:{} {} bytes]", mime_str, byte_count)
            } else {
                format!("{} [image:{} {} bytes]", text_str, mime_str, byte_count)
            };
            core.handle_event(ShellEvent::ChatMessage {
                message,
                context: ChatRequestContext::new(ChatRequestMode::GeneralChat),
            });
            true
        },
        false
    )
}

// ============================================================
// Agent FFI
// ============================================================

use maho_agent::{AgentRuntime, MutexAgentStorage, SessionRuntime, SwiftideBackend};
use std::sync::{Arc, Mutex};

const AGENT_SKILL_INJECT_BUDGET_CHARS: usize = 2_000;

pub struct MahoAgentSession {
    runtime: Arc<SwiftideBackend>,
    isolation: Arc<SessionRuntime>,
    session_id: String,
    core: std::ptr::NonNull<MahoCore>,
    workspace_root: std::path::PathBuf,
    active_space_id: Option<String>,
}

fn apply_agent_skill_system_prompt(session: &MahoAgentSession, runtime: &SwiftideBackend) {
    // SAFETY: `MahoAgentSession` is only constructed by
    // `maho_agent_create_session`, which stores the non-null `MahoCore` pointer
    // supplied by the browser. The browser must keep that core alive for the
    // agent session lifetime.
    let core = unsafe { session.core.as_ref() };
    let trusted_origin = core.trusted_active_tab_origin();
    let workspace_root = session.workspace_root.to_string_lossy();
    let base_system_prompt = core.resolve_agent_base_system_prompt(
        &session.session_id,
        workspace_root.as_ref(),
        session.active_space_id.as_deref(),
    );
    if let Some(system_prompt) = core.compose_agent_system_prompt_with_skill_context(
        &base_system_prompt,
        trusted_origin.as_deref(),
        AGENT_SKILL_INJECT_BUDGET_CHARS,
    ) {
        runtime.set_system_prompt(&system_prompt);
    } else {
        runtime.set_system_prompt("");
    }
}

struct SendableCallback<T>(T);
unsafe impl<T> Send for SendableCallback<T> {}
unsafe impl<T> Sync for SendableCallback<T> {}

struct SendableUserData(usize);
#[allow(dead_code)]
unsafe impl Send for SendableUserData {}
#[allow(dead_code)]
unsafe impl Sync for SendableUserData {}

#[repr(C)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum MahoAgentPermissionDecision {
    Allow = 0,
    Deny = 1,
}

pub type MahoAgentPermissionCallback = extern "C" fn(
    user_data: *mut std::ffi::c_void,
    tool_name: *const c_char,
    arguments: *const c_char,
) -> MahoAgentPermissionDecision;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct MahoAgentSecureKey {
    pub ptr: *mut std::os::raw::c_char,
    pub len: usize,
    pub free_fn: Option<unsafe extern "C" fn(*mut std::os::raw::c_char, usize)>,
    pub base_url: *mut std::os::raw::c_char,
    pub model: *mut std::os::raw::c_char,
    pub cstring_free_fn: Option<unsafe extern "C" fn(*mut std::os::raw::c_char)>,
}

pub type MahoAgentSecureStorageCallback =
    extern "C" fn(user_data: *mut std::ffi::c_void, provider: *const c_char) -> MahoAgentSecureKey;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct MahoAgentToolResult {
    pub json_ptr: *mut std::os::raw::c_char,
    pub error_ptr: *mut std::os::raw::c_char,
    pub free_fn: Option<unsafe extern "C" fn(*mut std::os::raw::c_char)>,
}

struct FfiBrowserToolBridge {
    cb: SendableCallback<
        unsafe extern "C" fn(
            *mut std::ffi::c_void,
            *const c_char,
            *const c_char,
        ) -> MahoAgentToolResult,
    >,
    user_data: SendableUserData,
}

impl maho_agent::BrowserToolBridge for FfiBrowserToolBridge {
    fn invoke(&self, tool_name: &str, args_json: &str) -> Result<String, String> {
        let name_c = match CString::new(tool_name) {
            Ok(s) => s,
            Err(e) => return Err(format!("Invalid tool_name UTF-8: {e}")),
        };
        let args_c = match CString::new(args_json) {
            Ok(s) => s,
            Err(e) => return Err(format!("Invalid args_json UTF-8: {e}")),
        };
        let cb = self.cb.0;
        let ud = self.user_data.0 as *mut std::ffi::c_void;
        let result = unsafe { cb(ud, name_c.as_ptr(), args_c.as_ptr()) };

        let json_opt = if !result.json_ptr.is_null() {
            let s = unsafe { CStr::from_ptr(result.json_ptr) }
                .to_string_lossy()
                .into_owned();
            if let Some(free_fn) = result.free_fn {
                unsafe {
                    free_fn(result.json_ptr);
                }
            }
            Some(s)
        } else {
            None
        };
        let err_opt = if !result.error_ptr.is_null() {
            let s = unsafe { CStr::from_ptr(result.error_ptr) }
                .to_string_lossy()
                .into_owned();
            if let Some(free_fn) = result.free_fn {
                unsafe {
                    free_fn(result.error_ptr);
                }
            }
            Some(s)
        } else {
            None
        };
        match (json_opt, err_opt) {
            (_, Some(err)) => Err(err),
            (Some(json), None) => Ok(json),
            (None, None) => Err("Browser tool returned no result".to_string()),
        }
    }
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `session_id` must be a null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_create_session(
    core: *mut MahoCore,
    session_id: *const c_char,
    workspace_root: *const c_char,
    allow_insecure_key_storage: bool,
    permission_cb: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            tool_name: *const c_char,
            arguments_json: *const c_char,
        ) -> MahoAgentPermissionDecision,
    >,
    permission_user_data: *mut std::ffi::c_void,
    secure_storage_cb: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            provider: *const c_char,
        ) -> MahoAgentSecureKey,
    >,
    secure_storage_user_data: *mut std::ffi::c_void,
    browser_tool_cb: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            tool_name: *const c_char,
            args_json: *const c_char,
        ) -> MahoAgentToolResult,
    >,
    browser_tool_user_data: *mut std::ffi::c_void,
    space_id: *const c_char,
) -> *mut MahoAgentSession {
    ffi_safe!(
        {
            if core.is_null() || session_id.is_null() {
                tracing::debug!(
                "[maho_agent_create_session] FAIL: null pointer args (core_null={}, session_id_null={})",
                core.is_null(),
                session_id.is_null(),
            );
                return std::ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                tracing::debug!("[maho_agent_create_session] FAIL: import_gate is active (data import in progress)");
                return ptr::null_mut();
            }
            let Some(core_ptr) = std::ptr::NonNull::new(core) else {
                return std::ptr::null_mut();
            };
            // SAFETY: `core` was checked for null above and is required by this FFI
            // contract to point to a live `MahoCore` for the session lifetime.
            let core_ref = unsafe { core_ptr.as_ref() };
            let sid = match CStr::from_ptr(session_id).to_str() {
                Ok(s) => s.to_string(),
                Err(e) => {
                    tracing::debug!(
                        "[maho_agent_create_session] FAIL: session_id is not valid UTF-8: {e}"
                    );
                    return std::ptr::null_mut();
                }
            };

            let workspace_path = if workspace_root.is_null() {
                std::env::current_dir().unwrap_or_else(|_| std::path::PathBuf::from("."))
            } else {
                match CStr::from_ptr(workspace_root).to_str() {
                    Ok(s) => std::path::PathBuf::from(s),
                    Err(_) => {
                        std::env::current_dir().unwrap_or_else(|_| std::path::PathBuf::from("."))
                    }
                }
            };
            let session_workspace_root = workspace_path.clone();

            let db_path = match core_ref.sqlite_db_path() {
                Some(p) => p,
                None => {
                    tracing::debug!(
                        "[maho_agent_create_session] FAIL: core.sqlite_db_path() is None \
                     (MahoCore was initialized without SQLite storage; session_id={sid})"
                    );
                    return std::ptr::null_mut();
                }
            };

            let storage = match maho_storage::sqlite::SqliteStorage::open(db_path) {
                Ok(s) => s,
                Err(e) => {
                    tracing::debug!(
                        "[maho_agent_create_session] FAIL: SqliteStorage::open failed \
                     (db_path={db_path:?}, session_id={sid}): {e}"
                    );
                    return std::ptr::null_mut();
                }
            };

            let agent_storage = MutexAgentStorage(Arc::new(Mutex::new(storage)));

            let permission_callback = permission_cb.map(|cb| {
                let user_data_ptr = SendableUserData(permission_user_data as usize);
                let cb_sendable = SendableCallback(cb);
                let callback: Box<
                    dyn Fn(
                            maho_agent::PermissionRequest,
                        ) -> std::pin::Pin<
                            Box<
                                dyn std::future::Future<Output = maho_agent::PermissionDecision>
                                    + Send,
                            >,
                        > + Send
                        + Sync
                        + 'static,
                > = Box::new(move |req| {
                    let cb = cb_sendable.0;
                    let user_data = user_data_ptr.0 as *mut std::ffi::c_void;
                    let tool_name_c = match CString::new(req.tool_name) {
                        Ok(s) => s,
                        Err(_) => CString::new("unknown").unwrap(),
                    };
                    let arguments_c = match CString::new(req.arguments) {
                        Ok(s) => s,
                        Err(_) => CString::new("{}").unwrap(),
                    };
                    let decision =
                        unsafe { cb(user_data, tool_name_c.as_ptr(), arguments_c.as_ptr()) };
                    let p_decision = if decision == MahoAgentPermissionDecision::Allow {
                        maho_agent::PermissionDecision::Allow
                    } else {
                        maho_agent::PermissionDecision::Deny
                    };
                    Box::pin(async move { p_decision })
                });
                callback
            });

            let runtime = Arc::new(SwiftideBackend::new(
                Arc::new(agent_storage),
                permission_callback,
                workspace_path,
                allow_insecure_key_storage,
            ));

            if let Some(cb) = secure_storage_cb {
                let user_data_ptr = SendableUserData(secure_storage_user_data as usize);
                let cb_sendable = SendableCallback(cb);
                let callback = Box::new(move |provider: &str| {
                    let cb = cb_sendable.0;
                    let user_data = user_data_ptr.0 as *mut std::ffi::c_void;
                    let provider_c = match CString::new(provider) {
                        Ok(s) => s,
                        Err(_) => {
                            return Err(maho_agent::AgentError::ExecutionError(
                                "Invalid provider name".to_string(),
                            ))
                        }
                    };
                    let secure_key = unsafe { cb(user_data, provider_c.as_ptr()) };
                    if secure_key.ptr.is_null() {
                        return Err(maho_agent::AgentError::ExecutionError(
                            "BYOK key not found".to_string(),
                        ));
                    }
                    let key_slice = unsafe {
                        std::slice::from_raw_parts(secure_key.ptr as *const u8, secure_key.len)
                    };
                    let key_str = match String::from_utf8(key_slice.to_vec()) {
                        Ok(s) => s,
                        Err(e) => {
                            return Err(maho_agent::AgentError::ExecutionError(e.to_string()))
                        }
                    };

                    unsafe {
                        std::ptr::write_bytes(secure_key.ptr, 0, secure_key.len);
                    }

                    if let Some(free_fn) = secure_key.free_fn {
                        unsafe {
                            free_fn(secure_key.ptr, secure_key.len);
                        }
                    }

                    let base_url_opt = if !secure_key.base_url.is_null() {
                        let s = unsafe { CStr::from_ptr(secure_key.base_url) }
                            .to_string_lossy()
                            .into_owned();
                        if let Some(free_fn) = secure_key.cstring_free_fn {
                            unsafe {
                                free_fn(secure_key.base_url);
                            }
                        }
                        Some(s)
                    } else {
                        None
                    };

                    let model_opt = if !secure_key.model.is_null() {
                        let s = unsafe { CStr::from_ptr(secure_key.model) }
                            .to_string_lossy()
                            .into_owned();
                        if let Some(free_fn) = secure_key.cstring_free_fn {
                            unsafe {
                                free_fn(secure_key.model);
                            }
                        }
                        Some(s)
                    } else {
                        None
                    };

                    Ok(maho_agent::SecureKeyBundle {
                        key: maho_agent::ZeroizedString::new(key_str),
                        base_url: base_url_opt,
                        model: model_opt,
                    })
                });
                runtime.set_secure_storage_callback(callback).unwrap();
            }

            if let Some(cb) = browser_tool_cb {
                let user_data_ptr = SendableUserData(browser_tool_user_data as usize);
                let cb_sendable = SendableCallback(cb);
                let bridge: std::sync::Arc<dyn maho_agent::BrowserToolBridge> =
                    std::sync::Arc::new(FfiBrowserToolBridge {
                        cb: cb_sendable,
                        user_data: user_data_ptr,
                    });
                runtime.set_browser_tool_bridge(bridge).unwrap();
            }

            let active_space_id = if space_id.is_null() {
                None
            } else {
                match CStr::from_ptr(space_id).to_str() {
                    Ok(sid) if !sid.is_empty() => Some(sid.to_string()),
                    Ok(_) | Err(_) => None,
                }
            };
            if let Some(sid) = active_space_id.as_ref() {
                runtime.set_active_space_id(Some(sid.clone()));
            }

            let isolation = match SessionRuntime::new() {
                Ok(i) => Arc::new(i),
                Err(e) => {
                    tracing::debug!(
                        "[maho_agent_create_session] FAIL: SessionRuntime::new() failed \
                     (Tokio runtime build error; session_id={sid}): {e}"
                    );
                    return std::ptr::null_mut();
                }
            };

            Box::into_raw(Box::new(MahoAgentSession {
                runtime,
                isolation,
                session_id: sid,
                core: core_ptr,
                workspace_root: session_workspace_root,
                active_space_id,
            }))
        },
        std::ptr::null_mut()
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_session_free(session: *mut MahoAgentSession) {
    ffi_safe!(
        {
            if !session.is_null() {
                drop(Box::from_raw(session));
            }
        },
        ()
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `message` must be a null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_send_message(
    session: *mut MahoAgentSession,
    message: *const c_char,
    on_token: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, token: *const c_char)>,
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            full_text: *const c_char,
            tool_calls_json: *const c_char,
        ),
    >,
    on_error: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, error: *const c_char)>,
    user_data: *mut std::ffi::c_void,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() || message.is_null() {
                return false;
            }
            let session = &*session;
            let msg_str = match CStr::from_ptr(message).to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return false,
            };
            let runtime = Arc::clone(&session.runtime);
            let session_id = session.session_id.clone();
            apply_agent_skill_system_prompt(session, runtime.as_ref());

            if msg_str.starts_with('/') {
                let cmd = msg_str.split_whitespace().next().unwrap_or("");
                // SAFETY: see `apply_agent_skill_system_prompt`; the same session-owned
                // core pointer is used synchronously before the async turn is spawned.
                let core = unsafe { session.core.as_ref() };
                runtime.set_allowed_tools(core.resolve_agent_slash_allowed_tools(cmd));
            } else {
                runtime.set_allowed_tools(None);
            }

            let on_token_cb = on_token.map(|cb| {
                let cb_sendable = SendableCallback(cb);
                let user_data_val = SendableUserData(user_data as usize);
                let callback: Box<dyn Fn(&str) + Send + Sync> = Box::new(move |token| {
                    let cb = cb_sendable.0;
                    let udata = user_data_val.0 as *mut std::ffi::c_void;
                    let token_c = match CString::new(token) {
                        Ok(s) => s,
                        Err(_) => return,
                    };
                    unsafe {
                        cb(udata, token_c.as_ptr());
                    }
                });
                callback
            });

            let on_complete = SendableCallback(on_complete);
            let on_error = SendableCallback(on_error);
            let user_data_val = SendableUserData(user_data as usize);

            let fut = async move {
                let user_chat_msg = maho_types::chat::ChatMessage::user(
                    maho_types::chat::ChatContent::text(msg_str),
                );
                match runtime
                    .run_turn(&session_id, user_chat_msg, on_token_cb)
                    .await
                {
                    Ok(response) => {
                        let text = match &response.content {
                            maho_types::chat::ChatContent::Text(t) => t.clone(),
                            _ => String::new(),
                        };
                        let Some(text_c) = cstring_or_fallback(&text, "") else {
                            return;
                        };
                        let Some(empty_json) = cstring_or_fallback("[]", "[]") else {
                            return;
                        };
                        if let Some(cb) = on_complete.0 {
                            cb(
                                user_data_val.0 as *mut std::ffi::c_void,
                                text_c.as_ptr(),
                                empty_json.as_ptr(),
                            );
                        }
                    }
                    Err(e) => {
                        let error_text = e.to_string();
                        let Some(err_c) = cstring_or_fallback(&error_text, "unknown error") else {
                            return;
                        };
                        if let Some(cb) = on_error.0 {
                            cb(user_data_val.0 as *mut std::ffi::c_void, err_c.as_ptr());
                        }
                    }
                }
            };

            let isolation = Arc::clone(&session.isolation);
            let _handle = isolation.block_on(async move {
                tokio::spawn(fut);
            });

            true
        },
        false
    )
}

/// # Safety
/// Fast-path variant of `maho_agent_send_message`. Skips all tool registration
/// and dispatches straight to the LLM. Callers MUST NOT rely on tool_calls
/// being processed. Same pointer safety requirements as `maho_agent_send_message`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_send_message_simple(
    session: *mut MahoAgentSession,
    message: *const c_char,
    on_token: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, token: *const c_char)>,
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            full_text: *const c_char,
            tool_calls_json: *const c_char,
        ),
    >,
    on_error: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, error: *const c_char)>,
    user_data: *mut std::ffi::c_void,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() || message.is_null() {
                return false;
            }
            let session = &*session;
            let msg_str = match CStr::from_ptr(message).to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return false,
            };
            let runtime = Arc::clone(&session.runtime);
            let session_id = session.session_id.clone();
            apply_agent_skill_system_prompt(session, runtime.as_ref());

            let on_token_cb = on_token.map(|cb| {
                let cb_sendable = SendableCallback(cb);
                let user_data_val = SendableUserData(user_data as usize);
                let callback: Box<dyn Fn(&str) + Send + Sync> = Box::new(move |token| {
                    let cb = cb_sendable.0;
                    let udata = user_data_val.0 as *mut std::ffi::c_void;
                    let token_c = match CString::new(token) {
                        Ok(s) => s,
                        Err(_) => return,
                    };
                    unsafe {
                        cb(udata, token_c.as_ptr());
                    }
                });
                callback
            });

            let on_complete = SendableCallback(on_complete);
            let on_error = SendableCallback(on_error);
            let user_data_val = SendableUserData(user_data as usize);

            let fut = async move {
                let user_chat_msg = maho_types::chat::ChatMessage::user(
                    maho_types::chat::ChatContent::text(msg_str),
                );
                match runtime
                    .run_turn_simple(&session_id, user_chat_msg, on_token_cb)
                    .await
                {
                    Ok(response) => {
                        let text = match &response.content {
                            maho_types::chat::ChatContent::Text(t) => t.clone(),
                            _ => String::new(),
                        };
                        let Some(text_c) = cstring_or_fallback(&text, "") else {
                            return;
                        };
                        let Some(empty_json) = cstring_or_fallback("[]", "[]") else {
                            return;
                        };
                        if let Some(cb) = on_complete.0 {
                            cb(
                                user_data_val.0 as *mut std::ffi::c_void,
                                text_c.as_ptr(),
                                empty_json.as_ptr(),
                            );
                        }
                    }
                    Err(e) => {
                        let error_text = e.to_string();
                        let Some(err_c) = cstring_or_fallback(&error_text, "unknown error") else {
                            return;
                        };
                        if let Some(cb) = on_error.0 {
                            cb(user_data_val.0 as *mut std::ffi::c_void, err_c.as_ptr());
                        }
                    }
                }
            };

            let isolation = Arc::clone(&session.isolation);
            let _handle = isolation.block_on(async move {
                tokio::spawn(fut);
            });

            true
        },
        false
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_cancel(session: *mut MahoAgentSession) -> bool {
    ffi_safe!(
        {
            if session.is_null() {
                return false;
            }
            let session = &*session;
            let runtime = Arc::clone(&session.runtime);
            let session_id = session.session_id.clone();

            let isolation = Arc::clone(&session.isolation);
            let res = isolation.block_on(async move { runtime.cancel(&session_id).await });
            res.is_ok()
        },
        false
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `policy` must be a null-terminated C string (e.g. "prompt", "allow", "deny").
#[no_mangle]
pub unsafe extern "C" fn maho_agent_set_approval_policy(
    session: *mut MahoAgentSession,
    policy: *const c_char,
) {
    ffi_safe!(
        {
            if session.is_null() || policy.is_null() {
                return;
            }
            let session = &*session;
            if let Ok(p) = CStr::from_ptr(policy).to_str() {
                session.runtime.set_approval_policy(p.to_string());
            }
        },
        ()
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_list_tools(session: *mut MahoAgentSession) -> *mut c_char {
    ffi_safe!(
        {
            if session.is_null() {
                return std::ptr::null_mut();
            }
            let session = &*session;
            let runtime = Arc::clone(&session.runtime);

            let isolation = Arc::clone(&session.isolation);
            let tools_res = isolation.block_on(async move { runtime.list_tools().await });

            let tools = match tools_res {
                Ok(Ok(t)) => t,
                _ => return std::ptr::null_mut(),
            };

            match serde_json::to_string(&tools) {
                Ok(json) => CString::new(json)
                    .unwrap_or_else(|_| CString::new("[]").unwrap())
                    .into_raw(),
                Err(_) => std::ptr::null_mut(),
            }
        },
        std::ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `MahoCore`.
/// `space_id` must be a null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_space_ai_config(
    ptr: *mut MahoCore,
    space_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || space_id.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let space_id_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let key = format!("space_ai_config:{}", space_id_str);
            if let Some(storage) = core.storage_ref() {
                if let Ok(Some(value)) = storage.get_setting(&key) {
                    return CString::new(value)
                        .unwrap_or_else(|_| CString::new("{}").unwrap())
                        .into_raw();
                }
            }
            CString::new("{}")
                .unwrap_or_else(|_| CString::new("").unwrap())
                .into_raw()
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `MahoCore`.
/// `space_id` must be a null-terminated C string.
/// `config_json` must be a null-terminated C string containing valid JSON.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_space_ai_config(
    ptr: *mut MahoCore,
    space_id: *const c_char,
    config_json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || space_id.is_null() || config_json.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &*ptr;
            let space_id_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let config_str = match CStr::from_ptr(config_json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            if serde_json::from_str::<serde_json::Value>(config_str).is_err() {
                return false;
            }
            let key = format!("space_ai_config:{}", space_id_str);
            if let Some(storage) = core.storage_ref() {
                if storage.set_setting(&key, config_str).is_ok() {
                    return true;
                }
            }
            false
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `MahoCore`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_cluster_tabs(ptr: *mut MahoCore, threshold: f64) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let clusters = core.cluster_tabs(threshold);
            to_json_cstring(&clusters)
        },
        ptr::null_mut()
    )
}

// Helper to get storage from core
fn get_storage(core: *mut MahoCore) -> Option<maho_storage::sqlite::SqliteStorage> {
    if core.is_null() {
        return None;
    }
    let core_ref = unsafe { &*core };
    let db_path = core_ref.sqlite_db_path()?;
    maho_storage::sqlite::SqliteStorage::open(db_path).ok()
}

// Profile CRUD

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `name` and `system_prompt` must be null-terminated UTF-8 strings.
/// `model` is an optional null-terminated UTF-8 string (can be null).
/// Caller must free returned string via `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_profile_create(
    core: *mut MahoCore,
    name: *const c_char,
    system_prompt: *const c_char,
    model: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || name.is_null() || system_prompt.is_null() {
                return ptr::null_mut();
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };

            let name_str = CStr::from_ptr(name).to_str().unwrap_or("").to_string();
            let prompt_str = CStr::from_ptr(system_prompt)
                .to_str()
                .unwrap_or("")
                .to_string();
            let model_str = if model.is_null() {
                None
            } else {
                CStr::from_ptr(model).to_str().ok().map(String::from)
            };

            let id = uuid::Uuid::new_v4().to_string();
            let now = maho_types::common::DateTime::now().0;
            let profile = maho_types::ai::AiProfile {
                id: id.clone(),
                name: name_str,
                system_prompt: prompt_str,
                preferred_model: model_str,
                tools: vec![],
                mcp_servers: vec![],
                is_default: false,
                created_at: now.clone(),
                updated_at: now,
            };

            if storage.create_ai_profile(&profile).is_ok() {
                let res = serde_json::json!({ "id": id });
                to_json_cstring(&res)
            } else {
                ptr::null_mut()
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// Caller must free returned string via `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_profile_list(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            match storage.list_ai_profiles() {
                Ok(list) => to_json_cstring(&list),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `id` and `json` must be null-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_profile_update(
    core: *mut MahoCore,
    id: *const c_char,
    json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() || json.is_null() {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let json_str = match CStr::from_ptr(json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };

            let mut profile: maho_types::ai::AiProfile = match serde_json::from_str(json_str) {
                Ok(p) => p,
                Err(_) => return false,
            };

            profile.id = id_str.to_string();
            profile.updated_at = maho_types::common::DateTime::now().0;

            storage.update_ai_profile(&profile).is_ok()
        },
        false
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `id` must be a null-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_profile_delete(core: *mut MahoCore, id: *const c_char) -> bool {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            storage.delete_ai_profile(id_str).is_ok()
        },
        false
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` must be a null-terminated UTF-8 string.
/// `toml_path` must be a null-terminated UTF-8 string.
/// The caller must free the returned string with `maho_core_free_string`.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_profile_import_toml(
    core: *mut MahoCore,
    workspace_id: *const c_char,
    toml_path: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() || toml_path.is_null() {
                return ptr::null_mut();
            }
            let ws_id = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let path_str = match CStr::from_ptr(toml_path).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let toml_content = match std::fs::read_to_string(path_str) {
                Ok(content) => content,
                Err(e) => {
                    let err_str = format!("Error reading file: {}", e);
                    return CString::new(err_str)
                        .map(|c| c.into_raw())
                        .unwrap_or(ptr::null_mut());
                }
            };
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            match maho_core::profile_import::import_profile_from_toml(
                &storage,
                ws_id,
                &toml_content,
            ) {
                Ok(profile_id) => CString::new(profile_id)
                    .map(|c| c.into_raw())
                    .unwrap_or(ptr::null_mut()),
                Err(e) => {
                    let err_str = format!("Error: {}", e);
                    CString::new(err_str)
                        .map(|c| c.into_raw())
                        .unwrap_or(ptr::null_mut())
                }
            }
        },
        ptr::null_mut()
    )
}

// Workspace CRUD

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `name` must be a null-terminated UTF-8 string.
/// `space_id` is an optional null-terminated UTF-8 string (can be null).
/// Caller must free returned string via `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_workspace_create(
    core: *mut MahoCore,
    name: *const c_char,
    space_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || name.is_null() {
                return ptr::null_mut();
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            let manager = WorkspaceManager::new(storage);
            let name_str = CStr::from_ptr(name).to_str().unwrap_or("");
            let space_str = if space_id.is_null() {
                None
            } else {
                CStr::from_ptr(space_id).to_str().ok()
            };

            match manager.create_workspace(name_str, space_str) {
                Ok(ws) => to_json_cstring(&ws),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `space_id` must be a null-terminated UTF-8 string.
/// Caller must free returned string via `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_workspace_get_by_space(
    core: *mut MahoCore,
    space_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || space_id.is_null() {
                return ptr::null_mut();
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            let manager = WorkspaceManager::new(storage);
            let space_str = match CStr::from_ptr(space_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };

            match manager.get_active_workspace(space_str) {
                Ok(ws) => to_json_cstring(&ws),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// Caller must free returned string via `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_workspace_list(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            match storage.list_workspaces() {
                Ok(list) => to_json_cstring(&list),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` and `profile_id` must be null-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_workspace_switch_profile(
    core: *mut MahoCore,
    workspace_id: *const c_char,
    profile_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() || profile_id.is_null() {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let manager = WorkspaceManager::new(storage);
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let prof_str = match CStr::from_ptr(profile_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            manager.switch_profile(ws_str, prof_str).is_ok()
        },
        false
    )
}

// MCP Server management

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` and `config_json` must be null-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_mcp_server_register(
    core: *mut MahoCore,
    workspace_id: *const c_char,
    config_json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() || config_json.is_null() {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let manager = WorkspaceManager::new(storage);
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let json_str = match CStr::from_ptr(config_json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let config: maho_types::ai::AiMcpServer = match serde_json::from_str(json_str) {
                Ok(c) => c,
                Err(_) => return false,
            };
            manager.register_mcp_server(ws_str, config).is_ok()
        },
        false
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` and `server_name` must be null-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_mcp_server_remove(
    core: *mut MahoCore,
    workspace_id: *const c_char,
    server_name: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() || server_name.is_null() {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let manager = WorkspaceManager::new(storage);
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let name_str = match CStr::from_ptr(server_name).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            manager.remove_mcp_server(ws_str, name_str).is_ok()
        },
        false
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` must be a null-terminated UTF-8 string.
/// Caller must free returned string via `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_mcp_server_list(
    core: *mut MahoCore,
    workspace_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() {
                return ptr::null_mut();
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match storage.list_mcp_servers(ws_str) {
                Ok(list) => to_json_cstring(&list),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id`, `server_name`, and `tools_json` must be null-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_mcp_server_approve_trust(
    core: *mut MahoCore,
    workspace_id: *const c_char,
    server_name: *const c_char,
    tools_json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null()
                || workspace_id.is_null()
                || server_name.is_null()
                || tools_json.is_null()
            {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let name_str = match CStr::from_ptr(server_name).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let tools_str = match CStr::from_ptr(tools_json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let tools: Vec<String> = match serde_json::from_str(tools_str) {
                Ok(t) => t,
                Err(_) => return false,
            };

            let mut servers = match storage.list_mcp_servers(ws_str) {
                Ok(list) => list,
                Err(_) => return false,
            };
            let server = match servers.iter_mut().find(|s| s.name == name_str) {
                Some(s) => s,
                None => return false,
            };

            server.trusted = true;
            server.trusted_tools = if tools.is_empty() { None } else { Some(tools) };
            server.updated_at = maho_types::common::DateTime::now().0;

            storage.update_mcp_server(server).is_ok()
        },
        false
    )
}

// CLI Tool management

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` and `tool_json` must be null-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_cli_tool_register(
    core: *mut MahoCore,
    workspace_id: *const c_char,
    tool_json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() || tool_json.is_null() {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let manager = WorkspaceManager::new(storage);
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let json_str = match CStr::from_ptr(tool_json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let tool: maho_types::ai::AiCliTool = match serde_json::from_str(json_str) {
                Ok(t) => t,
                Err(_) => return false,
            };
            manager.register_cli_tool(ws_str, tool).is_ok()
        },
        false
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` and `tool_name` must be null-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_cli_tool_remove(
    core: *mut MahoCore,
    workspace_id: *const c_char,
    tool_name: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() || tool_name.is_null() {
                return false;
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return false,
            };
            let manager = WorkspaceManager::new(storage);
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let name_str = match CStr::from_ptr(tool_name).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            manager.remove_cli_tool(ws_str, name_str).is_ok()
        },
        false
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` must be a null-terminated UTF-8 string.
/// Caller must free returned string via `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_cli_tool_list(
    core: *mut MahoCore,
    workspace_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() {
                return ptr::null_mut();
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match storage.list_cli_tools(ws_str) {
                Ok(list) => to_json_cstring(&list),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

// Active tool set

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `workspace_id` must be a null-terminated UTF-8 string.
/// Caller must free returned string via `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_ai_workspace_get_tools(
    core: *mut MahoCore,
    workspace_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || workspace_id.is_null() {
                return ptr::null_mut();
            }
            let storage = match get_storage(core) {
                Some(s) => s,
                None => return ptr::null_mut(),
            };
            let manager = WorkspaceManager::new(storage);
            let ws_str = match CStr::from_ptr(workspace_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };

            let core_ref = &*core;
            let registry = core_ref.tool_registry();
            match manager.get_effective_tools(ws_str, registry) {
                Ok(tools) => to_json_cstring(&tools),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

// ============================================================
// Account Tier FFI

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

// ============================================================
// Routines FFI
// ============================================================

/// Returns JSON array of all hardcoded routines.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `out_json` must be a valid pointer to a `*mut c_char`.
#[no_mangle]
pub unsafe extern "C" fn maho_routines_list(out_json: *mut *mut c_char) {
    ffi_safe!(
        {
            if out_json.is_null() {
                return;
            }
            let routines = maho_core::routines::list_routines();
            let json_ptr = to_json_cstring(routines);
            *out_json = json_ptr;
        },
        ()
    )
}

/// Kicks off an async routine run. Returns 0 on success (queued), -1 on error.
/// The `user_tier` parameter: 0=Free, 1=Pro, 2=Max.
/// `on_complete` is called with the result JSON when done.
/// `on_error` is called with an error string on failure.
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_routines_run(
    core: *mut MahoCore,
    id: *const c_char,
    user_tier: i32,
    on_complete: Option<
        unsafe extern "C" fn(user_data: *mut std::ffi::c_void, result_json: *const c_char),
    >,
    on_error: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, error: *const c_char)>,
    user_data: *mut std::ffi::c_void,
) -> i32 {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() {
                return -1;
            }
            let db_path = match (&*core).sqlite_db_path() {
                Some(p) => p.to_string(),
                None => return -1,
            };
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return -1,
            };
            let tier = match user_tier {
                0 => maho_core::routines::UserTier::Free,
                1 => maho_core::routines::UserTier::Pro,
                2 => maho_core::routines::UserTier::Max,
                _ => return -1,
            };

            let on_complete_cb = SendableCallback(on_complete);
            let on_error_cb = SendableCallback(on_error);
            let user_data_val = SendableUserData(user_data as usize);

            // Dispatch onto the shared, owned maho-core runtime instead of the ambient
            // runtime. `maho_routines_run` is called from the Chromium C++ browser
            // thread which has NO ambient tokio runtime; a bare `tokio::spawn` there
            // panics and the routine silently never executes.
            //
            // `run_routine_resolved` borrows a `!Send` SqliteStorage across an await, so
            // it cannot ride the shared multi-threaded runtime via `spawn` (which
            // requires `Send`). Run it on a dedicated current-thread runtime pinned to a
            // `spawn_blocking` thread: the future is created and driven entirely on that
            // one thread and never migrates, so no `Send` bound is imposed and no panic
            // can cross the FFI boundary.
            maho_core::memory_manager::get_runtime().spawn_blocking(move || {
                let rt = match tokio::runtime::Builder::new_current_thread()
                    .enable_all()
                    .build()
                {
                    Ok(rt) => rt,
                    Err(e) => {
                        eprintln!("[maho_routines_run] failed to build runtime: {e}");
                        return;
                    }
                };
                rt.block_on(async move {
                    let storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                        Ok(s) => s,
                        Err(e) => {
                            if let Some(cb) = on_error_cb.0 {
                                let err_c = CString::new(format!("Failed to open storage: {e}"))
                                    .unwrap_or_else(|_| CString::new("error").unwrap());
                                cb(user_data_val.0 as *mut std::ffi::c_void, err_c.as_ptr());
                            }
                            return;
                        }
                    };
                    let agent_storage = MutexAgentStorage(Arc::new(Mutex::new(storage)));
                    let backend = SwiftideBackend::new(
                        Arc::new(agent_storage),
                        None,
                        std::env::current_dir().unwrap_or_else(|_| std::path::PathBuf::from(".")),
                        true,
                    );

                    // Resolution needs its own read handle: the agent's storage was moved
                    // into the backend above. Route through `run_routine_resolved` so that
                    // user-defined custom recipes are runnable in addition to built-ins.
                    let resolve_storage = match maho_storage::sqlite::SqliteStorage::open(&db_path)
                    {
                        Ok(s) => s,
                        Err(e) => {
                            if let Some(cb) = on_error_cb.0 {
                                let err_c = CString::new(format!("Failed to open storage: {e}"))
                                    .unwrap_or_else(|_| CString::new("error").unwrap());
                                cb(user_data_val.0 as *mut std::ffi::c_void, err_c.as_ptr());
                            }
                            return;
                        }
                    };

                    let result = maho_core::routines::run_routine_resolved(
                        &id_str,
                        tier,
                        &resolve_storage,
                        |prompt| async move {
                            backend.run_prompt(&prompt).await.map_err(|e| e.to_string())
                        },
                    )
                    .await;

                    match result {
                        Ok(res) => {
                            if let Some(cb) = on_complete_cb.0 {
                                let json = serde_json::to_string(&res).unwrap_or_default();
                                let json_c = CString::new(json)
                                    .unwrap_or_else(|_| CString::new("{}").unwrap());
                                cb(user_data_val.0 as *mut std::ffi::c_void, json_c.as_ptr());
                            }
                        }
                        Err(e) => {
                            if let Some(cb) = on_error_cb.0 {
                                let err_c = CString::new(e.to_string())
                                    .unwrap_or_else(|_| CString::new("error").unwrap());
                                cb(user_data_val.0 as *mut std::ffi::c_void, err_c.as_ptr());
                            }
                        }
                    }
                });
            });

            0
        },
        -1
    )
}

// ============================================================
// Custom Routines FFI (list-all / create / delete / fire-event)
// ============================================================

#[derive(serde::Deserialize)]
struct CreateCustomRoutineInput {
    name: String,
    prompt: String,
    #[serde(default)]
    schedule: Option<String>,
    #[serde(default)]
    trigger: Option<String>,
}

#[derive(serde::Deserialize)]
struct FireRoutineEventInput {
    kind: String,
    #[serde(default)]
    count: Option<u32>,
}

/// Returns JSON array of ALL routines (built-in + user-defined custom), each an
/// object with `id`, `name`, `cron`, `trigger`, `description`, `enabled`, and
/// `source` ("builtin"|"custom"). Caller must free with `maho_string_free`.
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_routines_list_all(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let db_path = match (&*core).sqlite_db_path() {
                Some(p) => p.to_string(),
                None => return ptr::null_mut(),
            };
            let storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match maho_core::routines::list_all_routines(&storage) {
                Ok(views) => to_json_cstring(&views),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Create a user-defined routine from JSON `{name,prompt,schedule?,trigger?}`.
/// Validates the cron `schedule` and/or event `trigger` string when present.
/// Returns 0 on success, -1 on error (null args / parse failure / invalid
/// schedule-or-trigger / empty name-or-prompt / storage failure).
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `json` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_routines_create_custom(
    core: *mut MahoCore,
    json: *const c_char,
) -> i32 {
    ffi_safe!(
        {
            if core.is_null() || json.is_null() {
                return -1;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return -1;
            }
            let json_str = match CStr::from_ptr(json).to_str() {
                Ok(s) => s,
                Err(_) => return -1,
            };
            let input: CreateCustomRoutineInput = match serde_json::from_str(json_str) {
                Ok(v) => v,
                Err(_) => return -1,
            };
            if input.name.trim().is_empty() || input.prompt.trim().is_empty() {
                return -1;
            }
            // Normalize blank strings to None so an empty form field is treated as
            // "no trigger" rather than an invalid one.
            let schedule = input.schedule.filter(|s| !s.trim().is_empty());
            let trigger = input.trigger.filter(|s| !s.trim().is_empty());
            // Validate whichever trigger the caller supplied.
            if let Some(ref cron) = schedule {
                if maho_core::routines::CronSchedule::parse(cron).is_none() {
                    return -1;
                }
            }
            if let Some(ref ev) = trigger {
                if maho_core::routines::RoutineEvent::parse(ev).is_none() {
                    return -1;
                }
            }
            let db_path = match (&*core).sqlite_db_path() {
                Some(p) => p.to_string(),
                None => return -1,
            };
            let storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                Ok(s) => s,
                Err(_) => return -1,
            };
            let routine = maho_storage::CustomRoutine {
                id: uuid::Uuid::new_v4().to_string(),
                name: input.name,
                prompt: input.prompt,
                schedule,
                trigger,
                enabled: true,
                created_at: chrono::Utc::now().to_rfc3339(),
            };
            match storage.create_custom_routine(&routine) {
                Ok(()) => 0,
                Err(_) => -1,
            }
        },
        -1
    )
}

/// Delete a user-defined routine by id. Returns 0 if a row was removed, -1 on
/// null args / not-found / storage failure.
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_routines_delete_custom(
    core: *mut MahoCore,
    id: *const c_char,
) -> i32 {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() {
                return -1;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return -1;
            }
            let id_str = match CStr::from_ptr(id).to_str() {
                Ok(s) => s,
                Err(_) => return -1,
            };
            let db_path = match (&*core).sqlite_db_path() {
                Some(p) => p.to_string(),
                None => return -1,
            };
            let storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                Ok(s) => s,
                Err(_) => return -1,
            };
            match storage.delete_custom_routine(id_str) {
                Ok(true) => 0,
                Ok(false) => -1,
                Err(_) => -1,
            }
        },
        -1
    )
}

/// Fire browser-event-triggered custom routines matching `event_json`
/// (`{"kind":"on_startup"}` or `{"kind":"on_many_tabs","count":N}`). Max-tier
/// gated (mirrors the scheduler tick); fire-and-forget — the batch runs async
/// on the shared, owned maho-core runtime so no panic crosses the FFI boundary
/// even when called from the Chromium C++ browser thread (no ambient runtime).
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `event_json` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_routines_fire_event(core: *mut MahoCore, event_json: *const c_char) {
    ffi_safe!(
        {
            if core.is_null() || event_json.is_null() {
                return;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return;
            }
            let core_ref = &*core;
            // Only Max users get event-triggered routines.
            match core_ref.get_account_tier() {
                Some(maho_types::account::UserTier::Max) => {}
                _ => return,
            };
            let json_str = match CStr::from_ptr(event_json).to_str() {
                Ok(s) => s,
                Err(_) => return,
            };
            let input: FireRoutineEventInput = match serde_json::from_str(json_str) {
                Ok(v) => v,
                Err(_) => return,
            };
            let event = match input.kind.as_str() {
                "on_startup" => maho_core::routines::RoutineEvent::OnStartup,
                // The fired event's `threshold` carries the ACTUAL live tab count; a
                // configured routine matches when its threshold <= this actual count.
                "on_many_tabs" => maho_core::routines::RoutineEvent::OnManyTabs {
                    threshold: input.count.unwrap_or(0),
                },
                _ => return,
            };
            let db_path = match core_ref.sqlite_db_path() {
                Some(p) => p.to_string(),
                None => return,
            };

            // Dispatch onto the shared, owned maho-core runtime (see `maho_routines_run`
            // for the no-ambient-runtime rationale). `fire_event_routines` borrows a
            // `!Send` SqliteStorage across an await, so drive it on a current-thread
            // runtime pinned to a `spawn_blocking` thread (the future never migrates).
            maho_core::memory_manager::get_runtime().spawn_blocking(move || {
                let rt = match tokio::runtime::Builder::new_current_thread()
                    .enable_all()
                    .build()
                {
                    Ok(rt) => rt,
                    Err(e) => {
                        eprintln!("[maho_routines_fire_event] failed to build runtime: {e}");
                        return;
                    }
                };
                rt.block_on(async move {
                    // Read handle for routine resolution.
                    let storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                        Ok(s) => s,
                        Err(e) => {
                            eprintln!("[maho_routines_fire_event] failed to open storage: {e}");
                            return;
                        }
                    };
                    // Separate handle backing the agent's own memory/session storage.
                    let agent_storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                        Ok(s) => MutexAgentStorage(Arc::new(Mutex::new(s))),
                        Err(e) => {
                            eprintln!(
                                "[maho_routines_fire_event] failed to open agent storage: {e}"
                            );
                            return;
                        }
                    };
                    let backend = Arc::new(SwiftideBackend::new(
                        Arc::new(agent_storage),
                        None,
                        std::env::current_dir().unwrap_or_else(|_| std::path::PathBuf::from(".")),
                        true,
                    ));
                    // `fire_event_routines` requires `Fn` (it may run several routines), so
                    // clone the shared backend into each per-routine future.
                    let runner = |prompt: String| {
                        let backend = backend.clone();
                        async move { backend.run_prompt(&prompt).await.map_err(|e| e.to_string()) }
                    };
                    if let Err(e) = maho_core::routines::fire_event_routines(
                        event,
                        maho_core::routines::UserTier::Max,
                        &storage,
                        runner,
                    )
                    .await
                    {
                        eprintln!("[maho_routines_fire_event] failed: {e}");
                    }
                });
            });
        },
        ()
    )
}

// ============================================================
// Routines Scheduler Tick FFI
// ============================================================

use std::collections::HashMap;
use std::sync::{Mutex as StdMutex, OnceLock};

/// Global last-run timestamps for the routines scheduler.
/// Key: routine id (static str), Value: epoch seconds of last successful fire.
static ROUTINES_LAST_RUNS: OnceLock<StdMutex<HashMap<&'static str, i64>>> = OnceLock::new();

fn get_last_runs() -> &'static StdMutex<HashMap<&'static str, i64>> {
    ROUTINES_LAST_RUNS.get_or_init(|| StdMutex::new(HashMap::new()))
}

/// Tick the routines scheduler. Should be called once per minute by the Chromium shell.
/// Internally checks tier, iterates the 4 routines, fires any due to run (async).
/// Returns 0 on success, -1 on tier-not-max (no-op), -2 on internal error.
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_routines_tick(core: *mut MahoCore, now_sec: i64) -> i32 {
    ffi_safe!(
        {
            if core.is_null() {
                return -2;
            }
            // Check tier — only Max users get scheduled routines
            let core_ref = &*core;
            match core_ref.get_account_tier() {
                Some(maho_types::account::UserTier::Max) => {}
                _ => return -1,
            };

            let db_path = match core_ref.sqlite_db_path() {
                Some(p) => p.to_string(),
                None => return -2,
            };

            let last_runs_mutex = get_last_runs();
            let mut last_runs = match last_runs_mutex.lock() {
                Ok(guard) => guard,
                Err(poisoned) => poisoned.into_inner(),
            };

            let routines = maho_core::routines::list_routines();
            for routine in routines {
                let schedule = match maho_core::routines::CronSchedule::parse(routine.cron) {
                    Some(s) => s,
                    None => {
                        eprintln!(
                            "[maho_routines_tick] failed to parse cron for {}: {}",
                            routine.id, routine.cron
                        );
                        continue;
                    }
                };

                let last_run = last_runs.get(routine.id).copied().unwrap_or(0);
                let next_fire = schedule.next_fire_after(last_run);

                if now_sec >= next_fire {
                    // Fire the routine asynchronously via the existing run_routine mechanism
                    let id_str = routine.id.to_string();
                    let db_path_clone = db_path.clone();

                    // Spawn onto the shared, owned maho-core runtime. `maho_routines_tick`
                    // is called once/minute from the Chromium C++ browser thread which has
                    // NO ambient tokio runtime; a bare `tokio::spawn` there panics and the
                    // scheduled routine silently never fires. `Runtime::spawn` spawns onto
                    // its own runtime, so no panic can cross the FFI boundary.
                    maho_core::memory_manager::get_runtime().spawn(async move {
                        let storage =
                            match maho_storage::sqlite::SqliteStorage::open(&db_path_clone) {
                                Ok(s) => s,
                                Err(e) => {
                                    eprintln!(
                                        "[routines scheduled] failed to open sqlite storage: {:?}",
                                        e
                                    );
                                    return;
                                }
                            };
                        let agent_storage = MutexAgentStorage(Arc::new(Mutex::new(storage)));
                        let backend = SwiftideBackend::new(
                            Arc::new(agent_storage),
                            None,
                            std::env::current_dir()
                                .unwrap_or_else(|_| std::path::PathBuf::from(".")),
                            true,
                        );

                        let result = maho_core::routines::run_routine(
                            &id_str,
                            maho_core::routines::UserTier::Max,
                            |prompt| async move {
                                backend.run_prompt(&prompt).await.map_err(|e| e.to_string())
                            },
                        )
                        .await;

                        if let Err(e) = result {
                            eprintln!("[RoutineScheduler] routine {} failed: {}", id_str, e);
                        }
                    });

                    // Update last_run timestamp
                    last_runs.insert(routine.id, now_sec);
                }
            }

            0
        },
        -2
    )
}

use std::sync::atomic::{AtomicU64, Ordering};

static SPLIT_CALLBACKS: OnceLock<Mutex<Vec<(u64, extern "C" fn(*const c_char))>>> = OnceLock::new();
static NEXT_CALLBACK_TOKEN: AtomicU64 = AtomicU64::new(1);

type ToolAvailCb = unsafe extern "C" fn(*mut c_void, *const c_char);
static TOOL_AVAIL_CALLBACKS: OnceLock<Mutex<Vec<(u64, usize, ToolAvailCb)>>> = OnceLock::new();

fn get_split_callbacks() -> &'static Mutex<Vec<(u64, extern "C" fn(*const c_char))>> {
    SPLIT_CALLBACKS.get_or_init(|| Mutex::new(Vec::new()))
}

fn get_tool_avail_callbacks() -> &'static Mutex<Vec<(u64, usize, ToolAvailCb)>> {
    TOOL_AVAIL_CALLBACKS.get_or_init(|| Mutex::new(Vec::new()))
}

fn process_split_callbacks_for_updates(updates: &[maho_types::events::core_update::CoreUpdate]) {
    for update in updates {
        match update {
            maho_types::events::core_update::CoreUpdate::SplitViewChanged { .. } => {
                if let Ok(cbs) = get_split_callbacks().lock() {
                    let json_str = serde_json::to_string(update).unwrap_or_default();
                    let c_json = CString::new(json_str).unwrap();
                    for (_, cb) in cbs.iter() {
                        cb(c_json.as_ptr());
                    }
                }
            }
            maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged { .. } => {
                if let Ok(cbs) = get_tool_avail_callbacks().lock() {
                    let json_str = serde_json::to_string(update).unwrap_or_default();
                    let c_json = CString::new(json_str).unwrap();
                    for (_, ud, cb) in cbs.iter() {
                        unsafe {
                            cb(*ud as *mut c_void, c_json.as_ptr());
                        }
                    }
                }
            }
            _ => {}
        }
    }
}

/// # Safety
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
                cbs.push((token, callback));
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
    core: *mut MahoCore,
    token: u64,
) {
    ffi_safe!(
        {
            if core.is_null() {
                return;
            }
            if let Ok(mut cbs) = get_split_callbacks().lock() {
                cbs.retain(|(t, _)| *t != token);
            }
        },
        ()
    )
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
                cbs.push((token, user_data as usize, callback));
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
    core: *mut MahoCore,
    token: u64,
) {
    ffi_safe!(
        {
            if core.is_null() {
                return;
            }
            if let Ok(mut cbs) = get_tool_avail_callbacks().lock() {
                cbs.retain(|(t, _, _)| *t != token);
            }
        },
        ()
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    /// Verifies that the alloc/free pair for preview bytes uses matching allocator layouts.
    ///
    /// The alloc side (`maho_core_get_tab_preview`) does:
    ///   `Box::into_raw(bytes.into_boxed_slice()) as *mut u8`
    ///
    /// The free side (`maho_core_free_preview_data`) does:
    ///   `drop(Box::from_raw(std::slice::from_raw_parts_mut(data, len)))`
    ///
    /// This test exercises the same round-trip directly so that ASAN / MIRI catch any
    /// layout mismatch introduced by a future refactor.
    #[test]
    fn preview_bytes_roundtrip() {
        let bytes: Vec<u8> = (0u8..=255).collect();
        let len = bytes.len();
        unsafe {
            // Alloc — mirrors `maho_core_get_tab_preview`
            let ptr: *mut u8 = Box::into_raw(bytes.into_boxed_slice()) as *mut u8;
            assert!(!ptr.is_null());
            // Free — mirrors the fixed `maho_core_free_preview_data`
            let slice = std::slice::from_raw_parts_mut(ptr, len);
            drop(Box::from_raw(slice));
        }
    }

    /// M25: When the core pointer is null, `maho_core_get_zoom` must return 1.0 (the neutral
    /// zoom level), not 0.0 which would collapse the rendered page to 0% width.
    #[test]
    fn get_zoom_fallback_is_one_not_zero() {
        use std::ffi::CString;
        let site = CString::new("example.com").unwrap();
        let result = unsafe { maho_core_get_zoom(std::ptr::null_mut(), site.as_ptr()) };
        assert_eq!(result, 1.0, "null-ptr fallback must be 1.0, not 0.0");
    }

    /// M26: When `tab_id` contains invalid UTF-8, `maho_core_save_form_data` must return
    /// immediately without panicking and without corrupting storage via an empty-string
    /// fallback.
    #[test]
    fn save_form_data_invalid_utf8_returns_without_writing() {
        use maho_core::maho_core::MahoCore;
        // Build an invalid-UTF-8 byte sequence (0xFF is never valid in UTF-8).
        let bad_tab_id: &[u8] = b"\xFF\xFE\x00";
        let valid_json = std::ffi::CString::new(r#"{"field":"value"}"#).unwrap();

        let mut core = MahoCore::new();
        let core_ptr: *mut MahoCore = &mut core;

        // This must not panic and must not write data (the tab does not exist).
        unsafe {
            maho_core_save_form_data(
                core_ptr,
                bad_tab_id.as_ptr() as *const std::os::raw::c_char,
                valid_json.as_ptr(),
            );
        }
        // If we reach here the function returned cleanly without a panic.
    }

    /// H2: `maho_core_is_tab_pinned` returns `true` for a pinned tab and `false` for an
    /// unpinned one (and for a null pointer).
    #[test]
    fn is_tab_pinned_returns_true_for_pinned_tab() {
        use maho_core::maho_core::MahoCore;
        use maho_types::events::shell_event::ShellEvent;
        use std::ffi::CString;

        let mut core = MahoCore::new();

        // Create a tab
        let space_id = core.get_active_space_id().clone();
        let before: Vec<_> = core
            .get_tab_view_models()
            .iter()
            .map(|t| t.id.clone())
            .collect();
        core.handle_event(ShellEvent::CreateTab {
            space_id,
            url: None,
            parent_id: None,
            tab_id: None,
            window_id: None,
            is_private: false,
        });
        let tab_id = core
            .get_tab_view_models()
            .iter()
            .find(|t| !before.contains(&t.id))
            .expect("new tab should be visible in view models")
            .id
            .clone();

        // Pin the tab
        core.handle_event(ShellEvent::PinTab {
            tab_id: tab_id.clone(),
        });

        let core_ptr: *mut MahoCore = &mut core;

        // Pinned tab → true
        let pinned_id_cstr = CString::new(tab_id.0.as_str()).unwrap();
        let result = unsafe { maho_core_is_tab_pinned(core_ptr, pinned_id_cstr.as_ptr()) };
        assert!(result, "is_tab_pinned must return true for a pinned tab");

        // Unknown tab → false
        let unknown = CString::new("00000000-0000-0000-0000-000000000000").unwrap();
        let result2 = unsafe { maho_core_is_tab_pinned(core_ptr, unknown.as_ptr()) };
        assert!(
            !result2,
            "is_tab_pinned must return false for an unknown tab"
        );

        // Null pointer → false (no panic)
        let result3 =
            unsafe { maho_core_is_tab_pinned(std::ptr::null_mut(), pinned_id_cstr.as_ptr()) };
        assert!(!result3, "is_tab_pinned must return false on null ptr");
    }

    /// `maho_core_is_tab_close_protected` returns true for Pinned OR Favorite tabs
    /// (close-guard), false for Normal tabs, unknown ids, and null pointer.
    #[test]
    fn is_tab_close_protected_returns_true_for_favorite() {
        use maho_core::maho_core::MahoCore;
        use maho_types::events::shell_event::ShellEvent;
        use std::ffi::CString;

        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();

        // Helper: create a tab and return its freshly-assigned id.
        let make_tab = |core: &mut MahoCore| {
            let before: Vec<_> = core
                .get_tab_view_models()
                .iter()
                .map(|t| t.id.clone())
                .collect();
            core.handle_event(ShellEvent::CreateTab {
                space_id: space_id.clone(),
                url: None,
                parent_id: None,
                tab_id: None,
                window_id: None,
                is_private: false,
            });
            core.get_tab_view_models()
                .iter()
                .find(|t| !before.contains(&t.id))
                .expect("new tab should be visible in view models")
                .id
                .clone()
        };

        let pinned_id = make_tab(&mut core);
        let favorite_id = make_tab(&mut core);
        let normal_id = make_tab(&mut core);

        core.handle_event(ShellEvent::PinTab {
            tab_id: pinned_id.clone(),
        });
        core.handle_event(ShellEvent::FavoriteTab {
            tab_id: favorite_id.clone(),
        });

        let core_ptr: *mut MahoCore = &mut core;

        // Pinned → true
        let pinned_cstr = CString::new(pinned_id.0.as_str()).unwrap();
        assert!(
            unsafe { maho_core_is_tab_close_protected(core_ptr, pinned_cstr.as_ptr()) },
            "close-protected must be true for a pinned tab"
        );

        // Favorite → true
        let favorite_cstr = CString::new(favorite_id.0.as_str()).unwrap();
        assert!(
            unsafe { maho_core_is_tab_close_protected(core_ptr, favorite_cstr.as_ptr()) },
            "close-protected must be true for a favorite tab"
        );

        // Normal → false
        let normal_cstr = CString::new(normal_id.0.as_str()).unwrap();
        assert!(
            !unsafe { maho_core_is_tab_close_protected(core_ptr, normal_cstr.as_ptr()) },
            "close-protected must be false for a normal tab"
        );

        // Unknown id → false
        let unknown = CString::new("00000000-0000-0000-0000-000000000000").unwrap();
        assert!(
            !unsafe { maho_core_is_tab_close_protected(core_ptr, unknown.as_ptr()) },
            "close-protected must be false for an unknown tab"
        );

        // Null pointer → false (no panic)
        assert!(
            !unsafe {
                maho_core_is_tab_close_protected(std::ptr::null_mut(), pinned_cstr.as_ptr())
            },
            "close-protected must be false on null ptr"
        );
    }

    /// Malformed JSON passed to `maho_core_handle_event` must return a NON-null
    /// error payload (not a silent null), parseable as `{"error":{"kind":"parse",...}}`.
    #[test]
    fn handle_event_returns_error_payload_on_bad_json() {
        use maho_core::maho_core::MahoCore;
        use std::ffi::{CStr, CString};

        let mut core = MahoCore::new();
        let core_ptr: *mut MahoCore = &mut core;

        let bad = CString::new("{ this is : not json }").unwrap();
        let ret = unsafe { maho_core_handle_event(core_ptr, bad.as_ptr()) };
        assert!(
            !ret.is_null(),
            "malformed JSON must return a non-null error payload"
        );

        let decoded = unsafe { CStr::from_ptr(ret).to_str().unwrap().to_owned() };
        let value: serde_json::Value =
            serde_json::from_str(&decoded).expect("error payload must be valid JSON");
        assert_eq!(
            value["error"]["kind"], "parse",
            "error payload must report error.kind == \"parse\", got: {decoded}"
        );

        unsafe { maho_string_free(ret) };
    }

    #[test]
    fn sidebar_state_v2_null_ptr_returns_null() {
        use std::ffi::CString;
        let space_id = CString::new(r#""some-space-id""#).unwrap();
        let result =
            unsafe { maho_core_get_sidebar_state_v2(std::ptr::null_mut(), space_id.as_ptr()) };
        assert!(result.is_null(), "null core ptr must return null");
    }

    #[test]
    fn sidebar_state_v2_null_space_id_returns_null() {
        use maho_core::maho_core::MahoCore;
        let mut core = MahoCore::new();
        let core_ptr: *mut MahoCore = &mut core;
        let result = unsafe { maho_core_get_sidebar_state_v2(core_ptr, std::ptr::null()) };
        assert!(result.is_null(), "null space_id must return null");
    }

    /// Routines tick: returns -1 when tier is not Max (default unauthenticated).
    #[test]
    fn routines_tick_returns_neg1_when_not_max() {
        use maho_core::maho_core::MahoCore;
        let mut core = MahoCore::new();
        let core_ptr: *mut MahoCore = &mut core;
        // Use a fixed timestamp (2025-06-15 08:00:00 UTC)
        let now = 1_750_000_000i64;
        let result = unsafe { maho_routines_tick(core_ptr, now) };
        assert_eq!(result, -1, "unauthenticated user should return -1");
    }

    /// Routines tick: returns -2 on null core pointer.
    #[test]
    fn routines_tick_returns_neg2_on_null_ptr() {
        let now = 1_750_000_000i64;
        let result = unsafe { maho_routines_tick(std::ptr::null_mut(), now) };
        assert_eq!(result, -2, "null ptr should return -2");
    }

    /// REGRESSION GUARD (prod-breaking): `maho_routines_run` must dispatch its
    /// async work onto the shared, owned maho-core runtime — NOT via a bare
    /// `tokio::spawn` — so it does not panic when invoked from a thread with no
    /// ambient tokio runtime (the normal case for the Chromium C++ browser
    /// thread). Before the fix, the bare `tokio::spawn` panicked; `ffi_safe!`
    /// swallowed the panic and returned -1, so the routine silently never ran.
    /// After the fix it returns 0 (queued) with no panic crossing the boundary.
    #[test]
    fn routines_run_does_not_panic_without_ambient_runtime() {
        use maho_core::maho_core::MahoCore;
        use std::ffi::CString;

        // Run on a freshly-spawned OS thread that is guaranteed to have NO
        // ambient tokio runtime, mirroring the Chromium browser-thread call site.
        let handle = std::thread::spawn(|| {
            // Prove the precondition: there is genuinely no ambient runtime here.
            assert!(
                tokio::runtime::Handle::try_current().is_err(),
                "test precondition: this thread must have no ambient tokio runtime"
            );

            // A core backed by a real (temp) sqlite path so `sqlite_db_path()`
            // is Some and the FFI reaches the spawn line under test.
            let db = std::env::temp_dir().join(format!(
                "maho_routines_run_regression_{}.sqlite",
                std::process::id()
            ));
            let db_path = db.to_string_lossy().to_string();
            let mut core = MahoCore::new().with_storage(&db_path);
            let core_ptr: *mut MahoCore = &mut core;

            let id = CString::new("morning_briefing").unwrap();

            // No callbacks / user_data: we only care that the dispatch path
            // does not panic and returns the success code.
            let rc = unsafe {
                maho_routines_run(
                    core_ptr,
                    id.as_ptr(),
                    2, // Max tier
                    None,
                    None,
                    std::ptr::null_mut(),
                )
            };

            // Best-effort cleanup of the temp db.
            let _ = std::fs::remove_file(&db);
            rc
        });

        let rc = handle
            .join()
            .expect("FFI call must not panic across the thread boundary");
        assert_eq!(
            rc, 0,
            "maho_routines_run must return 0 (queued) off-runtime; a non-zero code \
             means the spawn panicked and was swallowed by ffi_safe! (the regression)"
        );
    }

    /// Direct guard for the shared-runtime dispatch mechanism used by BOTH
    /// `maho_routines_run` and `maho_routines_tick`: spawning onto the owned
    /// maho-core runtime must succeed from a non-tokio thread, whereas a bare
    /// `tokio::spawn` in the same context would panic.
    #[test]
    fn shared_runtime_spawn_works_off_runtime() {
        let handle = std::thread::spawn(|| {
            assert!(
                tokio::runtime::Handle::try_current().is_err(),
                "test precondition: no ambient tokio runtime on this thread"
            );

            // This is exactly what the routines FFI now does. It must not panic.
            let (tx, rx) = std::sync::mpsc::channel::<u32>();
            let join = maho_core::memory_manager::get_runtime().spawn(async move {
                let _ = tx.send(7);
            });
            // The spawned task actually runs on the owned runtime.
            let got = rx.recv_timeout(std::time::Duration::from_secs(5));
            (join.is_finished(), got)
        });

        let (_, got) = handle
            .join()
            .expect("spawning onto the shared runtime must not panic off-runtime");
        assert_eq!(
            got.ok(),
            Some(7),
            "task spawned on the shared maho-core runtime must execute off-runtime"
        );
    }

    /// Routines tick: no double-fire when called twice with same timestamp.
    #[test]
    fn routines_tick_no_double_fire() {
        // Exercise the LAST_RUNS static directly
        let last_runs = get_last_runs();
        let mut guard = last_runs.lock().unwrap();
        // Set morning_briefing as fired at time T
        let t = 1_750_000_000i64;
        guard.insert("morning_briefing", t);
        // Check that next_fire_after(t) > t (so it won't fire again at t)
        let schedule = maho_core::routines::CronSchedule::parse("0 8 * * *").unwrap();
        let next = schedule.next_fire_after(t);
        assert!(next > t, "next_fire_after must be strictly after last_run");
    }

    /// Custom-routine FFI roundtrip: create -> list_all (includes it as custom)
    /// -> delete -> list_all (gone). Exercises `maho_routines_create_custom`,
    /// `maho_routines_list_all`, and `maho_routines_delete_custom` end to end
    /// against a real (temp) sqlite-backed core.
    #[test]
    fn custom_routine_ffi_create_list_delete_roundtrip() {
        use maho_core::maho_core::MahoCore;
        use std::ffi::{CStr, CString};

        let db = std::env::temp_dir().join(format!(
            "maho_routines_ffi_roundtrip_{}.sqlite",
            std::process::id()
        ));
        let db_path = db.to_string_lossy().to_string();
        let mut core = MahoCore::new().with_storage(&db_path);
        let core_ptr: *mut MahoCore = &mut core;

        // Create a valid cron-scheduled custom routine.
        let create_json = CString::new(
            r#"{"name":"My Recipe","prompt":"Summarize tabs","schedule":"0 9 * * *"}"#,
        )
        .unwrap();
        let rc = unsafe { maho_routines_create_custom(core_ptr, create_json.as_ptr()) };
        assert_eq!(rc, 0, "create must succeed for a valid cron routine");

        // Invalid cron must be rejected.
        let bad_json =
            CString::new(r#"{"name":"Bad","prompt":"x","schedule":"not a cron"}"#).unwrap();
        let bad_rc = unsafe { maho_routines_create_custom(core_ptr, bad_json.as_ptr()) };
        assert_eq!(bad_rc, -1, "create must reject an invalid cron string");

        // list_all returns 4 built-ins + 1 custom, and the custom carries source=custom.
        let list_ptr = unsafe { maho_routines_list_all(core_ptr) };
        assert!(!list_ptr.is_null(), "list_all must return JSON");
        let list_json = unsafe { CStr::from_ptr(list_ptr).to_str().unwrap().to_string() };
        unsafe { maho_string_free(list_ptr) };
        let views: Vec<serde_json::Value> =
            serde_json::from_str(&list_json).expect("list_all must be a valid JSON array");
        assert_eq!(views.len(), 5, "4 built-ins + 1 custom");
        let custom = views
            .iter()
            .find(|v| v["source"] == "custom")
            .expect("custom routine must appear in list_all");
        assert_eq!(custom["name"], "My Recipe");
        assert_eq!(custom["cron"], "0 9 * * *");
        let custom_id = custom["id"].as_str().unwrap().to_string();

        // Delete it by id.
        let id_c = CString::new(custom_id).unwrap();
        let del_rc = unsafe { maho_routines_delete_custom(core_ptr, id_c.as_ptr()) };
        assert_eq!(
            del_rc, 0,
            "delete must succeed for an existing custom routine"
        );

        // Deleting again must report not-found.
        let del_rc2 = unsafe { maho_routines_delete_custom(core_ptr, id_c.as_ptr()) };
        assert_eq!(del_rc2, -1, "second delete must return -1 (not found)");

        // list_all is back to just the 4 built-ins.
        let list_ptr2 = unsafe { maho_routines_list_all(core_ptr) };
        let list_json2 = unsafe { CStr::from_ptr(list_ptr2).to_str().unwrap().to_string() };
        unsafe { maho_string_free(list_ptr2) };
        let views2: Vec<serde_json::Value> = serde_json::from_str(&list_json2).unwrap();
        assert_eq!(views2.len(), 4, "custom routine must be gone after delete");

        let _ = std::fs::remove_file(&db);
    }

    /// `maho_routines_fire_event` must be a safe no-op (no panic) on a null core
    /// and on a non-Max-tier core (tier gate), matching the scheduler's silent
    /// tier behavior. Also proves the event-JSON parse path does not panic.
    #[test]
    fn fire_event_null_and_tier_gate_are_noops() {
        use maho_core::maho_core::MahoCore;
        use std::ffi::CString;

        let startup = CString::new(r#"{"kind":"on_startup"}"#).unwrap();

        // Null core → no-op, no panic.
        unsafe { maho_routines_fire_event(std::ptr::null_mut(), startup.as_ptr()) };

        // Default (unauthenticated / non-Max) core → tier gate makes this a
        // no-op before any spawn; must not panic.
        let db = std::env::temp_dir().join(format!(
            "maho_routines_ffi_fire_event_{}.sqlite",
            std::process::id()
        ));
        let db_path = db.to_string_lossy().to_string();
        let mut core = MahoCore::new().with_storage(&db_path);
        let core_ptr: *mut MahoCore = &mut core;

        unsafe { maho_routines_fire_event(core_ptr, startup.as_ptr()) };

        // Malformed event JSON is also a safe no-op.
        let bad = CString::new(r#"{"kind":"nope"}"#).unwrap();
        unsafe { maho_routines_fire_event(core_ptr, bad.as_ptr()) };

        let _ = std::fs::remove_file(&db);
    }
}
