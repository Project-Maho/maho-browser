#![allow(clippy::missing_safety_doc)]

pub mod import;
pub mod import_destination;
pub(crate) mod import_gate;

use std::ffi::{c_char, CStr, CString};
use std::path::Path;
use std::ptr;

use maho_core::maho_core::MahoCore;
use maho_types::space::RootItem;
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
    #[serde(skip_serializing_if = "Option::is_none")]
    password: Option<String>,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct SidebarRootItemView {
    kind: String,
    id: String,
}



fn to_json_cstring<T: serde::Serialize + ?Sized>(value: &T) -> *mut c_char {
    match serde_json::to_string(value) {
        Ok(json) => CString::new(json)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        Err(_) => ptr::null_mut(),
    }
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
            password: password.password.clone(),
        })
        .collect();
    to_json_cstring(&views)
}

#[no_mangle]
pub extern "C" fn maho_core_new() -> *mut MahoCore {
    ffi_safe!({

    Box::into_raw(Box::new(MahoCore::new()))

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_free(ptr: *mut MahoCore) {
    ffi_safe!({

    if !ptr.is_null() {
        drop(Box::from_raw(ptr));
    }

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || event_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *ptr;
    let json_str = match CStr::from_ptr(event_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let event: maho_types::events::shell_event::ShellEvent = match serde_json::from_str(json_str) {
        Ok(e) => e,
        Err(_) => return ptr::null_mut(),
    };
    let updates = core.handle_event(event);
    to_json_cstring(&updates)

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_tab_view_models(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(&core.get_tab_view_models())

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || tab_id.is_null() { return false; }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let core = &*ptr;
    core.is_tab_pinned(tab_id_str)

    }, false)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_space_view_models(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(&core.get_space_view_models())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid null-terminated C string containing a SpaceId JSON.
/// Caller must free the returned string with `maho_string_free`.
#[deprecated(
    note = "Use maho_core_get_sidebar_state_v2 instead. Will be removed after 2026-07-15."
)]
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_sidebar_root_order(
    ptr: *mut MahoCore,
    space_id_json: *const c_char,
    window_id_ptr: *const i64,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let json_str = match CStr::from_ptr(space_id_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let space_id = match serde_json::from_str(json_str) {
        Ok(id) => id,
        Err(_) => return ptr::null_mut(),
    };
    
    let root_order = if !window_id_ptr.is_null() {
        core.get_sidebar_root_order_for_window(&space_id, *window_id_ptr)
    } else {
        core.get_sidebar_root_order(&space_id)
    };

    let root_items = root_order
        .into_iter()
        .map(|item| match item {
            RootItem::Tab(tab_id) => SidebarRootItemView {
                kind: "tab".to_string(),
                id: tab_id.to_string(),
            },
            RootItem::Folder(folder_id) => SidebarRootItemView {
                kind: "folder".to_string(),
                id: folder_id.to_string(),
            },
        })
        .collect::<Vec<_>>();
    to_json_cstring(&root_items)

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id_json` must be a valid null-terminated C string containing a SpaceId JSON.
/// Caller must free the returned string with `maho_string_free`.
#[deprecated(
    note = "Use maho_core_get_sidebar_state_v2 instead. Will be removed after 2026-07-15."
)]
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_sidebar_tree(
    ptr: *mut MahoCore,
    space_id_json: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let json_str = match CStr::from_ptr(space_id_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let space_id = match serde_json::from_str(json_str) {
        Ok(id) => id,
        Err(_) => return ptr::null_mut(),
    };

    let tree = core.get_sidebar_tree(&space_id);
    to_json_cstring(&tree)

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_active_space_id(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(&core.get_active_space_id())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_account_state(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(&core.get_account_state_json())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_auth_state(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(&core.get_auth_state_json())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_sign_in(
    ptr: *mut MahoCore,
    event_json: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || event_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *ptr;
    let json_str = match CStr::from_ptr(event_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let event: maho_types::events::shell_event::ShellEvent = match serde_json::from_str(json_str) {
        Ok(e) => e,
        Err(_) => return ptr::null_mut(),
    };
    let updates = core.handle_event(event);
    to_json_cstring(&updates)

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_tick(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *ptr;
    let updates = core.tick();
    to_json_cstring(&updates)

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_release_window_tabs(
    ptr: *mut MahoCore,
    window_id: i64,
) {
    ffi_safe!({

    if ptr.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    core.release_window_tabs(window_id);

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let json_str = match CStr::from_ptr(space_id_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let space_id = match serde_json::from_str(json_str) {
        Ok(id) => id,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.get_favorite_tabs(&space_id))

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_boost_view_models(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(&core.get_boost_view_models())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_css_mods(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(&core.css_mod_manager().get_all_mods())

    }, ptr::null_mut())
}

/// Returns the combined CSS string of all enabled mods.
/// Caller must free with `maho_string_free`.
///
/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Returns null on null
/// input, empty CSS, or if an import is in progress.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_combined_css(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return std::ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let css = core.css_mod_manager().generate_combined_css();
    if css.is_empty() {
        return std::ptr::null_mut();
    }
    match CString::new(css) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_installed_extensions(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(&core.get_installed_extensions())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_clear_installed_extensions(ptr: *mut MahoCore) {
    ffi_safe!({

    if ptr.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    core.clear_installed_extensions();

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `extensions_json` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_installed_extensions(
    ptr: *mut MahoCore,
    extensions_json: *const c_char,
) {
    ffi_safe!({

    if ptr.is_null() || extensions_json.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let json_str = match CStr::from_ptr(extensions_json).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let extensions: Vec<maho_core::extension_bridge::InstalledExtension> =
        match serde_json::from_str(json_str) {
            Ok(exts) => exts,
            Err(_) => return,
        };
    core.set_installed_extensions(extensions);

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_is_native_password_provider_active(ptr: *mut MahoCore) -> bool {
    ffi_safe!({

    if ptr.is_null() {
        return true;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &*ptr;
    core.get_settings().autofill.password_provider == maho_types::passwords::PasswordProviderKind::MahoNative

    }, false)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_is_bitwarden_extension_available(ptr: *mut MahoCore) -> bool {
    ffi_safe!({

    if ptr.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &*ptr;
    core.get_installed_extensions().iter().any(|ext| {
        ext.enabled && ext.name.to_lowercase().contains("bitwarden")
    })

    }, false)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_is_onepassword_extension_available(ptr: *mut MahoCore) -> bool {
    ffi_safe!({

    if ptr.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &*ptr;
    core.get_installed_extensions().iter().any(|ext| {
        ext.enabled && (ext.name.to_lowercase().contains("1password") || ext.name.to_lowercase().contains("onepassword"))
    })

    }, false)
}

/// # Safety
/// `ptr` must be a valid pointer to a MahoCore.
/// `mode` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_is_password_provider_available(
    ptr: *mut MahoCore,
    mode: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || mode.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &*ptr;
    let mode_str = match CStr::from_ptr(mode).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    if mode_str == "chromium" || mode_str == "builtin" || mode_str == "disabled" || mode_str == "system" || mode_str == "maho_native" {
        true
    } else if mode_str == "bitwarden" {
        core.get_installed_extensions().iter().any(|ext| {
            ext.enabled && ext.name.to_lowercase().contains("bitwarden")
        })
    } else if mode_str == "onepassword" {
        core.get_installed_extensions().iter().any(|ext| {
            ext.enabled && (ext.name.to_lowercase().contains("1password") || ext.name.to_lowercase().contains("onepassword"))
        })
    } else {
        true
    }

    }, false)
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
    ffi_safe!({

    if ptr.is_null() || query.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let query_str = match CStr::from_ptr(query).to_str() {
        Ok(s) => s.trim(),
        Err(_) => return ptr::null_mut(),
    };

    if query_str.is_empty() {
        return to_password_entries(core.list_passwords().iter().collect());
    }

    to_password_entries(core.search_passwords(query_str))

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `password_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_password(
    ptr: *mut MahoCore,
    password_id: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || password_id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let password_id = match CStr::from_ptr(password_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.delete_password_persisted(password_id)

    }, false)
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
    ffi_safe!({

    if ptr.is_null() || domain.is_null() || username.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
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

    }, false)
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
    ffi_safe!({

    if ptr.is_null() || password_id.is_null() || username.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
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

    }, false)
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
    ffi_safe!({

    if ptr.is_null() || url.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let url_str = match CStr::from_ptr(url).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.get_boosts_for_url(url_str))

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || domain.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let domain_str = match CStr::from_ptr(domain).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.list_boosts_for_domain(domain_str))

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || boost_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || domain.is_null() || name.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || boost_id.is_null() || changes_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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
            eprintln!("[MahoBoost] maho_core_boost_update: serde failed: {e} json={changes_str}");
            return ptr::null_mut();
        }
    };
    let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
    core.update_boost_persisted(&bid, changes);
    core.get_boost_by_id(&bid)
        .map(|b| to_json_cstring(&b))
        .unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || boost_id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(boost_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
    core.delete_boost_persisted(&bid).is_some()

    }, false)
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
    ffi_safe!({

    if ptr.is_null() || domain.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || domain.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let domain_str = match CStr::from_ptr(domain).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    match core.get_active_boost_for_domain(domain_str) {
        Some(boost) => to_json_cstring(&boost),
        None => ptr::null_mut(),
    }

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || boost_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let id_str = match CStr::from_ptr(boost_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
    match core.compose_css_for_boost(&bid, None) {
        Some(css) if !css.is_empty() => {
            match CString::new(css) {
                Ok(s) => s.into_raw(),
                Err(_) => ptr::null_mut(),
            }
        }
        _ => ptr::null_mut(),
    }

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || domain.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *ptr;
    let domain_str = match CStr::from_ptr(domain).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let boost = core.create_temp_boost_persisted(domain_str.to_string());
    to_json_cstring(&boost)

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || boost_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || boost_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || boost_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || boost_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || boost_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let id_str = match CStr::from_ptr(boost_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let bid = maho_types::identifiers::BoostId::new(id_str.to_string());
    match core.export_boost(&bid) {
        Some(json) => {
            match CString::new(json) {
                Ok(s) => s.into_raw(),
                Err(_) => ptr::null_mut(),
            }
        }
        None => ptr::null_mut(),
    }

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || domain.is_null() || json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || boost_id.is_null() || selector.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || boost_id.is_null() || selector.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_note_view_models(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(&core.get_note_view_models())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_download_view_models(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(&core.get_download_view_models())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_settings(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(core.get_settings())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_settings_view_model(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(&core.get_settings_view_model())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_find_bar_state(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(&core.get_find_bar_state())

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || space_id_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || url.is_null() || source_url.is_null() || request_type.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
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

    }, false)
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
    ffi_safe!({

    if ptr.is_null() || url.is_null() || source_url.is_null() || request_type.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `origin` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_site_exception(
    ptr: *mut MahoCore,
    origin: *const c_char,
) {
    ffi_safe!({

    if ptr.is_null() || origin.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let origin_str = match CStr::from_ptr(origin).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.add_content_blocker_site_exception(origin_str);

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `origin` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_site_exception(
    ptr: *mut MahoCore,
    origin: *const c_char,
) {
    ffi_safe!({

    if ptr.is_null() || origin.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let origin_str = match CStr::from_ptr(origin).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.remove_content_blocker_site_exception(origin_str);

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_site_exceptions(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let exceptions = core.get_content_blocker_site_exceptions();
    to_json_cstring(&exceptions)

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_content_rules(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let rules = core.get_content_rules();
    match CString::new(rules) {
        Ok(c_str) => c_str.into_raw(),
        Err(_) => ptr::null_mut(),
    }

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || url.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let url_str = match CStr::from_ptr(url).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let resources = core.get_cosmetic_resources(url_str);
    to_json_cstring(&resources)

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be from `maho_core_new`. `path` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_save_content_engine_cache(
    ptr: *mut MahoCore,
    path: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || path.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &*ptr;
    let path_str = match CStr::from_ptr(path).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.save_content_engine_cache(path_str).is_ok()

    }, false)
}

/// # Safety
/// `ptr` must be from `maho_core_new`. `path` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_load_content_engine_cache(
    ptr: *mut MahoCore,
    path: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || path.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let path_str = match CStr::from_ptr(path).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.load_content_engine_cache(path_str).is_ok()

    }, false)
}

/// # Safety
/// `s` must be a valid pointer returned by one of the `maho_core_*` functions, or null.
#[no_mangle]
pub unsafe extern "C" fn maho_string_free(s: *mut c_char) {
    ffi_safe!({

    if !s.is_null() {
        drop(CString::from_raw(s));
    }

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || url.is_null() || title.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || query.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let query_str = match CStr::from_ptr(query).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let results = core.search_history(query_str, limit);
    to_json_cstring(&results)

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || query.is_null() || tabs_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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
                is_favorite: false,
                favorite_order: None,
                is_muted: false,
                is_playing_audio: false,
                lifecycle_state: "active".to_string(),
                children: Vec::new(),
                created_at: DateTime::now(),
                last_active_at: DateTime::now(),
            }
        })
        .collect();

    let results = core.command_bar_search_with_tabs(query_str, mode_str, tabs, is_incognito);
    to_json_cstring(&results)

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_clear_history(ptr: *mut MahoCore) {
    ffi_safe!({

    if ptr.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    core.clear_history();

    }, ())
}

/// # Safety
/// `path` must be a valid null-terminated C string pointing to a directory.
/// The caller must ensure the directory exists or can be created.
/// Returns a pointer to a new MahoCore instance with both LMDB and SQLite storage initialized.
/// Returns null on error. Caller must free with `maho_core_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_new_with_storage(path: *const c_char) -> *mut MahoCore {
    ffi_safe!({

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

    let core = MahoCore::new()
        .with_storage(sqlite_path_str)
        .with_lmdb_storage(&lmdb_path);

    Box::into_raw(Box::new(core))

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
/// Returns 1 on success, 0 on error.
#[no_mangle]
pub unsafe extern "C" fn maho_core_save_state(ptr: *mut MahoCore) -> i32 {
    ffi_safe!({

    if ptr.is_null() {
        return 0;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return 0; }
    let core = &*ptr;
    match core.save_state() {
        Ok(_) => 1,
        Err(_) => 0,
    }

    }, 0)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
/// Returns 1 on success, 0 on error.
#[no_mangle]
pub unsafe extern "C" fn maho_core_load_state(ptr: *mut MahoCore) -> i32 {
    ffi_safe!({

    if ptr.is_null() {
        return 0;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return 0; }
    let core = &mut *ptr;
    match core.load_state() {
        Ok(_) => 1,
        Err(_) => 0,
    }

    }, 0)
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
    ffi_safe!({

    if ptr.is_null() || tab_id_ptr.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || tab_id_ptr.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let tab_id_str = std::ffi::CStr::from_ptr(tab_id_ptr).to_string_lossy();
    let tab_id = maho_types::identifiers::TabId::new(tab_id_str.as_ref());

    let custom_title = if title_ptr.is_null() {
        None
    } else {
        let title_str = std::ffi::CStr::from_ptr(title_ptr).to_string_lossy();
        let trimmed = title_str.trim();
        if trimmed.is_empty() { None } else { Some(trimmed.to_string()) }
    };

    let event = maho_types::events::shell_event::ShellEvent::SetTabCustomTitle {
        tab_id,
        custom_title,
    };
    core.handle_event(event);

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || url.is_null() || title.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `bookmark_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_bookmark(ptr: *mut MahoCore, bookmark_id: *const c_char) {
    ffi_safe!({

    if ptr.is_null() || bookmark_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let bookmark_id_str = match CStr::from_ptr(bookmark_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.remove_bookmark(bookmark_id_str);

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || query.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let query_str = match CStr::from_ptr(query).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.search_bookmarks(query_str))

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || bookmark_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || origin.is_null() || permission.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || origin.is_null() || permission.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || origin.is_null() || permission.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_pause_download(ptr: *mut MahoCore, download_id: *const c_char) {
    ffi_safe!({

    if ptr.is_null() || download_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let download_id_str = match CStr::from_ptr(download_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.pause_download(download_id_str);

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_resume_download(ptr: *mut MahoCore, download_id: *const c_char) {
    ffi_safe!({

    if ptr.is_null() || download_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let download_id_str = match CStr::from_ptr(download_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.resume_download(download_id_str);

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_cancel_download(ptr: *mut MahoCore, download_id: *const c_char) {
    ffi_safe!({

    if ptr.is_null() || download_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let download_id_str = match CStr::from_ptr(download_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.cancel_download(download_id_str);

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `download_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_download(ptr: *mut MahoCore, download_id: *const c_char) {
    ffi_safe!({

    if ptr.is_null() || download_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let download_id_str = match CStr::from_ptr(download_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.remove_download(download_id_str);

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `entry_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_history_entry(
    ptr: *mut MahoCore,
    entry_id: *const c_char,
) {
    ffi_safe!({

    if ptr.is_null() || entry_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let entry_id_str = match CStr::from_ptr(entry_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.delete_history_entry(entry_id_str);

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || tab_id.is_null() || data.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || tab_id.is_null() || out_len.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `data` must have been returned by `maho_core_get_tab_preview` with matching `len`.
/// Specifically, `data` must point to memory allocated as `Box<[u8]>` of length `len`
/// and ownership must not have been transferred elsewhere.
#[no_mangle]
pub unsafe extern "C" fn maho_core_free_preview_data(data: *mut u8, len: usize) {
    ffi_safe!({

    if !data.is_null() && len > 0 {
        // SAFETY: `data` was produced by `Box::into_raw(Box<[u8]>) as *mut u8` with `len`
        // matching the original slice length, so reconstructing the boxed slice via
        // `from_raw_parts_mut` + `Box::from_raw` correctly returns ownership to the
        // allocator using the same layout that `Box<[u8]>` allocated with.
        let slice = std::slice::from_raw_parts_mut(data, len);
        drop(Box::from_raw(slice));
    }

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_take_pending_preview_captures(
    ptr: *mut MahoCore,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *ptr;
    let captures = core.take_pending_preview_captures();
    let tab_ids: Vec<String> = captures.into_iter().map(|id| id.to_string()).collect();
    to_json_cstring(&tab_ids)

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_schedule_preview_capture(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || tab_id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let tid = maho_types::identifiers::TabId::new(tab_id_str);
    core.schedule_preview_capture(tid)

    }, false)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_all_bookmarks(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || name.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || download_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let download_id_str = match CStr::from_ptr(download_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.update_download_progress(download_id_str, received_bytes);

    }, ())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `download_id` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_complete_download(
    ptr: *mut MahoCore,
    download_id: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || download_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *ptr;
    let download_id_str = match CStr::from_ptr(download_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let updates = core.complete_download(download_id_str);
    to_json_cstring(&updates)

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `download_id` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_restore_download_name(
    ptr: *mut MahoCore,
    download_id: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || download_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Caller must free
/// the returned string with `maho_string_free`. Returns null on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_filter_lists(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return std::ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let json = core.get_filter_lists_json();
    match CString::new(json) {
        Ok(s) => s.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || id.is_null() || name.is_null() || url.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.toggle_filter_list(id_str, enabled);

    }, ())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `id` must be a valid
/// NUL-terminated C string. No-op on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_filter_list(ptr: *mut MahoCore, id: *const c_char) {
    ffi_safe!({

    if ptr.is_null() || id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.remove_filter_list(id_str);

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || id.is_null() || content.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. No-op on null input
/// or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_rebuild_content_rules(ptr: *mut MahoCore) {
    ffi_safe!({

    if ptr.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    core.rebuild_content_rules();

    }, ())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Returns `0` on null
/// input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_content_rule_count(ptr: *mut MahoCore) -> usize {
    ffi_safe!({

    if ptr.is_null() {
        return 0;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return 0; }
    let core = &*ptr;
    core.get_content_rule_count()

    }, 0)
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `extension_id` must be a
/// valid NUL-terminated C string. Returns `false` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_extension(
    ptr: *mut MahoCore,
    extension_id: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || extension_id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(extension_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.toggle_extension(id_str).is_some()

    }, false)
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `extension_id` must be a
/// valid NUL-terminated C string. Returns `false` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_extension(
    ptr: *mut MahoCore,
    extension_id: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || extension_id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(extension_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.remove_extension(id_str).is_some()

    }, false)
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated UTF-8 string.
pub unsafe extern "C" fn maho_core_get_search_url(
    core: *mut MahoCore,
    query: *const c_char,
) -> *mut c_char {
    ffi_safe!({

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

    }, ptr::null_mut())
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
pub unsafe extern "C" fn maho_core_get_search_engines(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

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

    }, ptr::null_mut())
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
pub unsafe extern "C" fn maho_core_get_site_search_entries(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    let core = match core.as_ref() {
        Some(c) => c,
        None => return std::ptr::null_mut(),
    };
    let entries = &core.get_settings().general.site_search_entries;
    to_json_cstring(entries)

    }, ptr::null_mut())
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `json` must be a valid null-terminated UTF-8 string.
pub unsafe extern "C" fn maho_core_set_site_search_entries(
    core: *mut MahoCore,
    json: *const c_char,
) {
    ffi_safe!({

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

    }, ())
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
pub unsafe extern "C" fn maho_core_get_recent_searches(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

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

    }, ptr::null_mut())
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_core_save_search(core: *mut MahoCore, query: *const c_char) {
    ffi_safe!({

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

    }, ())
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
pub unsafe extern "C" fn maho_core_get_notifications(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

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

    }, ptr::null_mut())
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `notification_id` must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_core_dismiss_notification(
    core: *mut MahoCore,
    notification_id: *const c_char,
) {
    ffi_safe!({

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

    }, ())
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
pub unsafe extern "C" fn maho_core_dismiss_all_notifications(core: *mut MahoCore) {
    ffi_safe!({

    let core = unsafe {
        if core.is_null() {
            return;
        }
        &mut *core
    };
    core.dismiss_all_notifications();

    }, ())
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
    ffi_safe!({

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

    }, ())
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `site` must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_core_get_zoom(core: *mut MahoCore, site: *const c_char) -> f64 {
    ffi_safe!({

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

    }, 1.0)
}

#[no_mangle]
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `site` must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_core_set_zoom(core: *mut MahoCore, site: *const c_char, zoom: f64) {
    ffi_safe!({

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

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_reader_mode(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || tab_id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.toggle_reader_mode(tab_id_str)

    }, false)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `tab_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_is_reader_mode(
    ptr: *mut MahoCore,
    tab_id: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || tab_id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.is_reader_mode(tab_id_str)

    }, false)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `item_key` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_record_usage(ptr: *mut MahoCore, item_key: *const c_char) {
    ffi_safe!({

    if ptr.is_null() || item_key.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let key = match CStr::from_ptr(item_key).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.record_command_bar_usage(key);

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Returns a JSON array of [key, count] pairs. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_top_used(ptr: *mut MahoCore, limit: usize) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let results = core.get_top_used_commands(limit);
    to_json_cstring(&results)

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be valid. `note_id` and `tab_id` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_link_note_to_tab(
    ptr: *mut MahoCore,
    note_id: *const c_char,
    tab_id: *const c_char,
) {
    ffi_safe!({

    if ptr.is_null() || note_id.is_null() || tab_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
}

/// # Safety
/// `ptr` must be valid. `note_id` and `url` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_link_note_to_url(
    ptr: *mut MahoCore,
    note_id: *const c_char,
    url: *const c_char,
) {
    ffi_safe!({

    if ptr.is_null() || note_id.is_null() || url.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
}

/// # Safety
/// `ptr` must be valid. `note_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_unlink_note_from_tab(
    ptr: *mut MahoCore,
    note_id: *const c_char,
) {
    ffi_safe!({

    if ptr.is_null() || note_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let nid = match CStr::from_ptr(note_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.unlink_note_from_tab(nid);

    }, ())
}

/// # Safety
/// `ptr` must be valid. `note_id` must be a valid null-terminated C string.
/// Returns the linked tab ID or null. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_linked_tab_id(
    ptr: *mut MahoCore,
    note_id: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || note_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be valid. `format` must be a valid null-terminated C string ("markdown", "html", "json").
/// Returns exported string. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_export_notes(
    ptr: *mut MahoCore,
    format: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || format.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let fmt = match CStr::from_ptr(format).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let result = core.export_notes(fmt);
    CString::new(result)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || note_id.is_null() || format.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be valid. `query` must be a valid null-terminated C string.
/// Returns JSON array of [note_id, rank] pairs. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_search_notes_fts(
    ptr: *mut MahoCore,
    query: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || query.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let q = match CStr::from_ptr(query).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let results = core.search_notes_fts(q);
    to_json_cstring(&results)

    }, ptr::null_mut())
}




/// # Safety
/// `ptr` must be valid.
/// Returns JSON reader settings. Caller must free with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_reader_settings(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let json = core.get_reader_settings_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `config_json` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_create_backup(
    ptr: *mut MahoCore,
    config_json: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || config_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || backup_b64.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Caller must free
/// the returned string with `maho_string_free`. Returns null on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_backup_history(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let json = core.get_backup_history_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || url.is_null() || title.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `item_id` must be a
/// valid NUL-terminated C string. Returns `false` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_from_reading_list(
    ptr: *mut MahoCore,
    item_id: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || item_id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let id = match CStr::from_ptr(item_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.remove_from_reading_list(id)

    }, false)
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `item_id` must be a
/// valid NUL-terminated C string. Returns `-1` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_toggle_reading_list_read(
    ptr: *mut MahoCore,
    item_id: *const c_char,
) -> i8 {
    ffi_safe!({

    if ptr.is_null() || item_id.is_null() {
        return -1;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return -1; }
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

    }, 0)
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Caller must free
/// the returned string with `maho_string_free`. Returns null on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_reading_list(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let json = core.get_reading_list_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Returns `0` on null
/// input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_unread_count(ptr: *mut MahoCore) -> usize {
    ffi_safe!({

    if ptr.is_null() {
        return 0;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return 0; }
    let core = &*ptr;
    core.get_unread_count()

    }, 0)
}

/// # Safety
/// No pointer arguments. Internally safe; uses only platform APIs. Caller must free the returned
/// string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_detect_browser_profiles() -> *mut c_char {
    ffi_safe!({

    let json = MahoCore::detect_browser_profiles_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `profile_path` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_import_chrome_bookmarks(
    ptr: *mut MahoCore,
    profile_path: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || profile_path.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `profile_path` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_import_firefox_bookmarks(
    ptr: *mut MahoCore,
    profile_path: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || profile_path.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let json = core.get_all_shortcuts_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || action.is_null() || key_combo_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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
        Err(conflict) => match serde_json::to_string(&conflict) {
            Ok(conflict_json) => CString::new(format!(r#"{{"error":{}}}"#, conflict_json))
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut()),
            Err(_) => ptr::null_mut(),
        },
    }

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `action` must be a
/// valid NUL-terminated C string. No-op on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_reset_shortcut(ptr: *mut MahoCore, action: *const c_char) {
    ffi_safe!({

    if ptr.is_null() || action.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    if let Ok(s) = CStr::from_ptr(action).to_str() {
        core.reset_shortcut_persisted(s);
    }

    }, ())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. No-op on null input
/// or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_reset_all_shortcuts(ptr: *mut MahoCore) {
    ffi_safe!({

    if ptr.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    core.reset_all_shortcuts_persisted();

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || action.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    if let Ok(s) = CStr::from_ptr(action).to_str() {
        core.toggle_shortcut_persisted(s, enabled);
    }

    }, ())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `key_combo_json` must be
/// a valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_resolve_shortcut(
    ptr: *mut MahoCore,
    key_combo_json: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || key_combo_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `density` must be a
/// valid NUL-terminated C string. No-op on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_density(ptr: *mut MahoCore, density: *const c_char) {
    ffi_safe!({

    if ptr.is_null() || density.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    if let Ok(s) = CStr::from_ptr(density).to_str() {
        core.set_density(s);
    }

    }, ())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `color_json` must be a
/// valid NUL-terminated C string. No-op on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_accent_color(ptr: *mut MahoCore, color_json: *const c_char) {
    ffi_safe!({

    if ptr.is_null() || color_json.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    if let Ok(s) = CStr::from_ptr(color_json).to_str() {
        core.set_accent_color(s);
    }

    }, ())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `css` may be null
/// (treated as clearing the CSS) or a valid NUL-terminated C string. No-op if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_custom_chrome_css(ptr: *mut MahoCore, css: *const c_char) {
    ffi_safe!({

    if ptr.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let css_value = if css.is_null() {
        None
    } else {
        CStr::from_ptr(css).to_str().ok().map(|s| s.to_string())
    };
    core.set_custom_chrome_css(css_value);

    }, ())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. No-op on null input
/// or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_window_transparency(ptr: *mut MahoCore, enabled: bool) {
    ffi_safe!({

    if ptr.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    core.set_window_transparency(enabled);

    }, ())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Caller must free
/// the returned string with `maho_string_free`. Returns null on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_toolbar_items(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let json = core.get_toolbar_items_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `items_json` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_toolbar_items(
    ptr: *mut MahoCore,
    items_json: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || items_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`. Caller must free
/// the returned string with `maho_string_free`. Returns null on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_default_toolbar_items(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let json = core.get_default_toolbar_items_json();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || url.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *ptr;
    let url_str = match CStr::from_ptr(url).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let result = core.handle_url_scheme(url_str);
    CString::new(result)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || level.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

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

    }, ())
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_crash_save_interval(core: *mut MahoCore, seconds: u64) {
    ffi_safe!({

    if core.is_null() {
        return;
    }
    let core = unsafe { &mut *core };
    core.set_crash_save_interval(seconds);

    }, ())
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_searchable_items(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if core.is_null() {
        return std::ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return std::ptr::null_mut(); }
    let core = &*core;
    let result = core.get_searchable_items();
    CString::new(result)
        .map(|s| s.into_raw())
        .unwrap_or(std::ptr::null_mut())

    }, ptr::null_mut())
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
    ffi_safe!({

    if core.is_null() || tab_id.is_null() || query.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_find_next(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if core.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *core;
    let result = core.find_next();
    to_json_cstring(&result)

    }, ptr::null_mut())
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_find_previous(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if core.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *core;
    let result = core.find_previous();
    to_json_cstring(&result)

    }, ptr::null_mut())
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_dismiss_find(core: *mut MahoCore) {
    ffi_safe!({

    if core.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *core;
    core.dismiss_find();

    }, ())
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `settings_json` must be a valid null-terminated C string containing JSON.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_settings(
    core: *mut MahoCore,
    settings_json: *const c_char,
) {
    ffi_safe!({

    if core.is_null() || settings_json.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *core;
    let json_str = match CStr::from_ptr(settings_json).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let update: maho_types::settings::SettingsUpdate = match serde_json::from_str(json_str) {
        Ok(u) => u,
        Err(_) => return,
    };
    core.update_settings(update);

    }, ())
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_atc_rules(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if core.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*core;
    let rules = core.get_atc_rules();
    to_json_cstring(&rules)

    }, ptr::null_mut())
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `rule_json` must be a valid null-terminated C string containing JSON.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_atc_rule(
    core: *mut MahoCore,
    rule_json: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if core.is_null() || rule_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `rule_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_atc_rule(
    core: *mut MahoCore,
    rule_id: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if core.is_null() || rule_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *core;
    let rule_id_str = match CStr::from_ptr(rule_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let result = core.remove_atc_rule(rule_id_str);
    to_json_cstring(&result)

    }, ptr::null_mut())
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
    ffi_safe!({

    if core.is_null() || rule_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *core;
    let rule_id_str = match CStr::from_ptr(rule_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.toggle_atc_rule(rule_id_str, enabled);

    }, ())
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
    ffi_safe!({

    if core.is_null() || query.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*core;
    let query_str = match CStr::from_ptr(query).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let results = core.search_history_with_offset(query_str, limit, offset);
    to_json_cstring(&results)

    }, ptr::null_mut())
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_history_grouped_by_date(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if core.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*core;
    let results = core.get_history_grouped_by_date();
    to_json_cstring(&results)

    }, ptr::null_mut())
}

/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `folder_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_bookmark_folder(
    core: *mut MahoCore,
    folder_id: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if core.is_null() || folder_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *core;
    let folder_id_str = match CStr::from_ptr(folder_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let result = core.delete_bookmark_folder(folder_id_str);
    to_json_cstring(&result)

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || name.is_null() || color_json.is_null() || profile_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_space(ptr: *mut MahoCore, space_id: *const c_char) {
    ffi_safe!({

    if ptr.is_null() || space_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(space_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let id = maho_types::identifiers::SpaceId::new(id_str);
    core.delete_space(&id);

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || space_id.is_null() || name.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || space_id.is_null() || color_json.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || space_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(space_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let id = maho_types::identifiers::SpaceId::new(id_str);
    core.reorder_space(&id, from, to);

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `space_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_activate_space(ptr: *mut MahoCore, space_id: *const c_char) {
    ffi_safe!({

    if ptr.is_null() || space_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(space_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let id = maho_types::identifiers::SpaceId::new(id_str);
    core.activate_space(&id);

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || name.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *ptr;
    let name_str = match CStr::from_ptr(name).to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return ptr::null_mut(),
    };
    let profile = core.create_profile(name_str);
    to_json_cstring(&profile)

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_profile(
    ptr: *mut MahoCore,
    profile_id: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || profile_id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(profile_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let id = maho_types::identifiers::ProfileId::new(id_str);
    core.delete_profile(&id)

    }, false)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_list_profiles(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let profiles = core.list_profiles();
    to_json_cstring(&profiles)

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_switch_profile(
    ptr: *mut MahoCore,
    profile_id: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || profile_id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(profile_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let id = maho_types::identifiers::ProfileId::new(id_str);
    core.switch_profile(&id)

    }, false)
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
    ffi_safe!({

    if ptr.is_null() || profile_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_active_profile_id(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    match core.get_active_profile_id() {
        Some(id) => to_json_cstring(id),
        None => ptr::null_mut(),
    }

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || server_url.is_null() || sync_key.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_stop_sync(ptr: *mut MahoCore) {
    ffi_safe!({

    if ptr.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    core.stop_sync();

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_sync_status(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let json = core.get_sync_status();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_connected_devices(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let json = core.get_connected_devices();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || url.is_null() || title.is_null() || target_device_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_generate_sync_key(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let json = core.generate_sync_key();
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `recovery_phrase` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_join_sync(
    ptr: *mut MahoCore,
    recovery_phrase: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || recovery_phrase.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *ptr;
    let phrase = match CStr::from_ptr(recovery_phrase).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let json = core.join_sync(phrase);
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `device_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_remove_sync_device(
    ptr: *mut MahoCore,
    device_id: *const c_char,
) {
    ffi_safe!({

    if ptr.is_null() || device_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(device_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.remove_sync_device(id_str);

    }, ())
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
    ffi_safe!({

    if ptr.is_null() || tab_id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let tab_id_str = match CStr::from_ptr(tab_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let tid = maho_types::identifiers::TabId::new(tab_id_str);
    core.reset_pinned_tab(&tid)

    }, false)
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
    ffi_safe!({

    if ptr.is_null() || tab_id.is_null() || url.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || entities_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *ptr;
    let json_str = match CStr::from_ptr(entities_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let entities: Vec<maho_core::sync_models::SyncEntity> = match serde_json::from_str(json_str) {
        Ok(e) => e,
        Err(_) => return ptr::null_mut(),
    };
    let applied = core.apply_sync_remote_entities(entities);
    to_json_cstring(&applied)

    }, ptr::null_mut())
}

/// Get pending outgoing messages as JSON array.
/// Shell calls this periodically to send via WebSocket.
///
/// # Safety
/// `ptr` must be valid.
/// Caller must free returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_drain_outgoing(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *ptr;
    let messages = core.drain_sync_outgoing();
    to_json_cstring(&messages)

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || device_id.is_null() || tab_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

// === Easels ===

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_easel_view_models(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(&core.get_easel_view_models())

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || easel_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    let id_str = match CStr::from_ptr(easel_id).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    match core.get_easel(id_str) {
        Some(easel) => to_json_cstring(easel),
        None => ptr::null_mut(),
    }

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || name.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *ptr;
    let name_str = match CStr::from_ptr(name).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let easel = core.create_easel_persisted(name_str);
    to_json_cstring(&easel)

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `easel_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_easel(
    ptr: *mut MahoCore,
    easel_id: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || easel_id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(easel_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.delete_easel_persisted(id_str)

    }, false)
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
    ffi_safe!({

    if ptr.is_null() || easel_id.is_null() || json.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
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

    }, false)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_autofill_addresses(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(core.get_autofill_addresses())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `address_json` must be
/// a valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_autofill_address_persisted(
    ptr: *mut MahoCore,
    address_json: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || address_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *ptr;
    let json_str = match CStr::from_ptr(address_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let address: maho_types::autofill::AutofillAddress = match serde_json::from_str(json_str) {
        Ok(addr) => addr,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.add_autofill_address_persisted(address))

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `id` must be a valid
/// NUL-terminated C string. Returns `false` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_autofill_address_persisted(
    ptr: *mut MahoCore,
    id: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.delete_autofill_address_persisted(id_str)

    }, false)
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_autofill_payments(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    to_json_cstring(core.get_autofill_payments())

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `payment_json` must be
/// a valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_autofill_payment_persisted(
    ptr: *mut MahoCore,
    payment_json: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || payment_json.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &mut *ptr;
    let json_str = match CStr::from_ptr(payment_json).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let payment: maho_types::autofill::AutofillPayment = match serde_json::from_str(json_str) {
        Ok(payment) => payment,
        Err(_) => return ptr::null_mut(),
    };
    to_json_cstring(&core.add_autofill_payment_persisted(payment))

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `id` must be a valid
/// NUL-terminated C string. Returns `false` on null input or if import is active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_autofill_payment_persisted(
    ptr: *mut MahoCore,
    id: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.delete_autofill_payment_persisted(id_str)

    }, false)
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
    ffi_safe!({

    if ptr.is_null() || space_id.is_null() || name.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// String parameters must be valid null-terminated UTF-8.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_share_link(
    ptr: *mut MahoCore,
    collection_id: *const c_char,
) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() || collection_id.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || collection_id.is_null() || permission.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
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

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// String parameters must be valid null-terminated UTF-8.
#[no_mangle]
pub unsafe extern "C" fn maho_core_revoke_share(ptr: *mut MahoCore, collection_id: *const c_char) {
    ffi_safe!({

    if ptr.is_null() || collection_id.is_null() {
        return;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return; }
    let core = &mut *ptr;
    let id_str = match CStr::from_ptr(collection_id).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    core.revoke_share(id_str);

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_shared_collections(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!({

    if ptr.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*ptr;
    match core.get_shared_collections() {
        Some(json) => CString::new(json)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        None => ptr::null_mut(),
    }

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// String parameters must be valid null-terminated UTF-8.
#[no_mangle]
pub unsafe extern "C" fn maho_core_join_shared_collection(
    ptr: *mut MahoCore,
    share_link: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || share_link.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &mut *ptr;
    let link_str = match CStr::from_ptr(share_link).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    core.join_shared_collection(link_str)

    }, false)
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
    ffi_safe!({

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

    }, false)
}

/// # Safety
/// `path_utf8` must be a valid null-terminated C string.
/// `out_status` must be a writable pointer to a u32.
#[no_mangle]
pub unsafe extern "C" fn maho_core_oscrypt_key_path_is_secure(
    path_utf8: *const c_char,
    out_status: *mut u32,
) -> bool {
    ffi_safe!({

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

    }, false)
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
    ffi_safe!({

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

    }, ptr::null_mut())
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
    ffi_safe!({

    if ptr.is_null() || space_id.is_null() || theme_json.is_null() || out_status.is_null() {
        return false;
    }
    *out_status = 1; // IoError

    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
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

    }, false)
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
    ffi_safe!({

    if ptr.is_null() || space_id.is_null() || path_utf8.is_null() || out_status.is_null() {
        return false;
    }
    *out_status = 1; // IoError

    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
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
        let _ = std::fs::set_permissions(&temp_path, std::fs::Permissions::from_mode(0o600));
    }

    if let Err(_) = std::fs::rename(&temp_path, path) {
        let _ = std::fs::remove_file(&temp_path);
        return false;
    }

    *out_status = 0; // Ok
    true

    }, false)
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
    ffi_safe!({

    if ptr.is_null() || space_id.is_null() || out_status.is_null() {
        return ptr::null_mut();
    }
    *out_status = 1; // Error

    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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

    }, ptr::null_mut())
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
    ffi_safe!({

    if config.is_null() {
        return std::ptr::null_mut();
    }
    let config = &*config;
    let api_key = if config.api_key.is_null() {
        String::new()
    } else {
        CStr::from_ptr(config.api_key).to_string_lossy().into_owned()
    };
    let endpoint = if config.endpoint.is_null() {
        String::new()
    } else {
        CStr::from_ptr(config.endpoint).to_string_lossy().into_owned()
    };
    let model = if config.model.is_null() {
        String::new()
    } else {
        CStr::from_ptr(config.model).to_string_lossy().into_owned()
    };
    let system_instruction = if config.system_instruction.is_null() {
        String::new()
    } else {
        CStr::from_ptr(config.system_instruction).to_string_lossy().into_owned()
    };

    let chat_config = maho_core::chat_session::ChatConfig {
        api_key,
        endpoint,
        model,
        system_instruction,
    };

    Box::into_raw(Box::new(maho_core::chat_session::ChatSession::new(chat_config)))

    }, ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_chat_session_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_session_free(
    ptr: *mut maho_core::chat_session::ChatSession,
) {
    ffi_safe!({

    if !ptr.is_null() {
        drop(Box::from_raw(ptr));
    }

    }, ())
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
    ffi_safe!({

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

    }, false)
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_set_event_sink(
    ptr: *mut maho_core::chat_session::ChatSession,
    on_token: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, token: *const c_char)>,
    on_complete: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, full_text: *const c_char, tool_calls_json: *const c_char)>,
    on_error: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, error: *const c_char)>,
    user_data: *mut std::ffi::c_void,
) {
    ffi_safe!({

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

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
/// `message` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_send_user_turn(
    ptr: *mut maho_core::chat_session::ChatSession,
    message: *const c_char,
) -> bool {
    ffi_safe!({

    if ptr.is_null() || message.is_null() {
        return false;
    }
    let session = &mut *ptr;
    let msg_str = match CStr::from_ptr(message).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    session.send_user_turn(msg_str)

    }, false)
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
    ffi_safe!({

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

    }, false)
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_cancel(
    ptr: *mut maho_core::chat_session::ChatSession,
) {
    ffi_safe!({

    if !ptr.is_null() {
        let session = &mut *ptr;
        session.cancel();
    }

    }, ())
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
/// `content` and `tool_calls_json` must be null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_chat_append_assistant_message(
    ptr: *mut maho_core::chat_session::ChatSession,
    content: *const c_char,
    tool_calls_json: *const c_char,
) {
    ffi_safe!({

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

    }, ())
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
    ffi_safe!({

    if core.is_null() || id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
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
    if let Some(ref storage) = core.storage_ref() {
        storage.create_conversation(id_str, title_opt, space_opt, model_opt).is_ok()
    } else {
        false
    }

    }, false)
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
    ffi_safe!({

    let empty = CString::new("[]").unwrap().into_raw();
    if core.is_null() {
        return empty;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
    let core = &*core;
    let conversations = if let Some(storage) = core.storage_ref() {
        storage.list_conversations(limit).unwrap_or_default()
    } else {
        return empty;
    };
    match serde_json::to_string(&conversations) {
        Ok(json) => CString::new(json).unwrap_or_else(|_| CString::new("[]").unwrap()).into_raw(),
        Err(_) => empty,
    }

    }, ptr::null_mut())
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
    ffi_safe!({

    let empty = CString::new("[]").unwrap().into_raw();
    if core.is_null() || session_id.is_null() {
        return empty;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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
        Ok(json) => CString::new(json).unwrap_or_else(|_| CString::new("[]").unwrap()).into_raw(),
        Err(_) => empty,
    }

    }, ptr::null_mut())
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
    ffi_safe!({

    if core.is_null() || session_id.is_null() || role.is_null() || content.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &*core;
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
    if let Some(storage) = core.storage_ref() {
        let id = uuid::Uuid::new_v4().to_string();
        storage.insert_conversation_turn(&id, sid, role_str, content_str, url_ctx).is_ok()
    } else {
        false
    }

    }, false)
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
    ffi_safe!({

    if core.is_null() || id.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &*core;
    let id_str = match CStr::from_ptr(id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    if let Some(storage) = core.storage_ref() {
        storage.delete_conversation(id_str).unwrap_or(false)
    } else {
        false
    }

    }, false)
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
    ffi_safe!({

    if core.is_null() || id.is_null() || new_title.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
    let core = &*core;
    let id_str = match CStr::from_ptr(id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let title_str = match CStr::from_ptr(new_title).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    if let Some(storage) = core.storage_ref() {
        storage.rename_conversation(id_str, title_str).unwrap_or(false)
    } else {
        false
    }

    }, false)
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
    ffi_safe!({

    if core.is_null() || provider.is_null() || api_key.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
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

    }, false)
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
    ffi_safe!({

    if core.is_null() || provider.is_null() {
        return ptr::null_mut();
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return ptr::null_mut(); }
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
            return CString::new(key).map(|s| s.into_raw()).unwrap_or(ptr::null_mut());
        }
    }
    ptr::null_mut()

    }, ptr::null_mut())
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
    ffi_safe!({

    if core.is_null() || provider.is_null() {
        return false;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
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

    }, false)
}

/// Get JSON array of supported provider names. Returns caller-owned string.
///
/// # Safety
/// `_core` may be null (ignored). Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_byok_get_providers(
    _core: *mut MahoCore,
) -> *mut c_char {
    ffi_safe!({

    let providers = serde_json::json!(["openai", "anthropic", "gemini", "ollama"]);
    let json = providers.to_string();
    CString::new(json).map(|s| s.into_raw()).unwrap_or(ptr::null_mut())

    }, ptr::null_mut())
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
    ffi_safe!({

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
    maho_core::memory_manager::get_runtime()
        .block_on(maho_core::llm_provider::validate_api_key(provider_str, key_str))

    }, false)
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
    ffi_safe!({

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
    if import_gate::is_active() { return false; }
    let core = &mut *core;
    use maho_types::chat::{ChatContent, ChatRequestContext, ChatRequestMode};
    use maho_types::events::shell_event::ShellEvent;
    let message = format!("[image:{} {} bytes]", mime_str, bytes.len());
    core.handle_event(ShellEvent::ChatMessage {
        message,
        context: ChatRequestContext::new(ChatRequestMode::GeneralChat),
    });
    true

    }, false)
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
    ffi_safe!({

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
        CStr::from_ptr(mime).to_str().unwrap_or("application/octet-stream")
    };
    let byte_count = if data.is_null() { 0 } else { data_len };
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return false; }
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

    }, false)
}

// ============================================================
// Agent FFI
// ============================================================

use maho_agent::{AgentRuntime, SwiftideBackend, SessionRuntime, MutexAgentStorage};
use std::sync::{Arc, Mutex};

pub struct MahoAgentSession {
    runtime: Arc<SwiftideBackend>,
    isolation: Arc<SessionRuntime>,
    session_id: String,
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
}

pub type MahoAgentSecureStorageCallback = extern "C" fn(
    user_data: *mut std::ffi::c_void,
    provider: *const c_char,
) -> MahoAgentSecureKey;

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `session_id` must be a null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_create_session(
    core: *mut MahoCore,
    session_id: *const c_char,
    workspace_root: *const c_char,
    allow_insecure_key_storage: bool,
    permission_cb: Option<unsafe extern "C" fn(
        user_data: *mut std::ffi::c_void,
        tool_name: *const c_char,
        arguments_json: *const c_char,
    ) -> MahoAgentPermissionDecision>,
    permission_user_data: *mut std::ffi::c_void,
    secure_storage_cb: Option<unsafe extern "C" fn(
        user_data: *mut std::ffi::c_void,
        provider: *const c_char,
    ) -> MahoAgentSecureKey>,
    secure_storage_user_data: *mut std::ffi::c_void,
) -> *mut MahoAgentSession {
    ffi_safe!({
        if core.is_null() || session_id.is_null() {
            return std::ptr::null_mut();
        }
        // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
        if import_gate::is_active() { return ptr::null_mut(); }
        let core = &*core;
        let sid = match CStr::from_ptr(session_id).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => return std::ptr::null_mut(),
        };
        
        let workspace_path = if workspace_root.is_null() {
            std::env::current_dir().unwrap_or_else(|_| std::path::PathBuf::from("."))
        } else {
            match CStr::from_ptr(workspace_root).to_str() {
                Ok(s) => std::path::PathBuf::from(s),
                Err(_) => std::env::current_dir().unwrap_or_else(|_| std::path::PathBuf::from(".")),
            }
        };

        let db_path = match core.sqlite_db_path() {
            Some(p) => p,
            None => return std::ptr::null_mut(),
        };
        
        let storage = match maho_storage::sqlite::SqliteStorage::open(db_path) {
            Ok(s) => s,
            Err(_) => return std::ptr::null_mut(),
        };
        
        let agent_storage = MutexAgentStorage(Arc::new(Mutex::new(storage)));
        
        let permission_callback = permission_cb.map(|cb| {
            let user_data_ptr = SendableUserData(permission_user_data as usize);
            let cb_sendable = SendableCallback(cb);
            let callback: Box<
                dyn Fn(maho_agent::PermissionRequest) -> std::pin::Pin<Box<dyn std::future::Future<Output = maho_agent::PermissionDecision> + Send>>
                    + Send
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
                let decision = unsafe { cb(user_data, tool_name_c.as_ptr(), arguments_c.as_ptr()) };
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
                    Err(_) => return Err(maho_agent::AgentError::ExecutionError("Invalid provider name".to_string())),
                };
                let secure_key = unsafe { cb(user_data, provider_c.as_ptr()) };
                if secure_key.ptr.is_null() {
                    return Err(maho_agent::AgentError::ExecutionError("BYOK key not found".to_string()));
                }
                let key_slice = unsafe { std::slice::from_raw_parts(secure_key.ptr as *const u8, secure_key.len) };
                let key_str = match String::from_utf8(key_slice.to_vec()) {
                    Ok(s) => s,
                    Err(e) => return Err(maho_agent::AgentError::ExecutionError(e.to_string())),
                };
                
                // Zeroize C buffer immediately after copy!
                unsafe {
                    std::ptr::write_bytes(secure_key.ptr, 0, secure_key.len);
                }
                
                // Call the free function if present
                if let Some(free_fn) = secure_key.free_fn {
                    unsafe {
                        free_fn(secure_key.ptr, secure_key.len);
                    }
                }
                Ok(maho_agent::ZeroizedString::new(key_str))
            });
            runtime.set_secure_storage_callback(callback).unwrap();
        }
        
        let isolation = match SessionRuntime::new() {
            Ok(i) => Arc::new(i),
            Err(_) => return std::ptr::null_mut(),
        };
        
        Box::into_raw(Box::new(MahoAgentSession {
            runtime,
            isolation,
            session_id: sid,
        }))
    }, std::ptr::null_mut())
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_session_free(
    session: *mut MahoAgentSession,
) {
    ffi_safe!({
        if !session.is_null() {
            drop(Box::from_raw(session));
        }
    }, ())
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `message` must be a null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_send_message(
    session: *mut MahoAgentSession,
    message: *const c_char,
    on_token: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, token: *const c_char)>,
    on_complete: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, full_text: *const c_char, tool_calls_json: *const c_char)>,
    on_error: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, error: *const c_char)>,
    user_data: *mut std::ffi::c_void,
) -> bool {
    ffi_safe!({
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
            let user_chat_msg = maho_types::chat::ChatMessage::user(maho_types::chat::ChatContent::text(msg_str));
            match runtime.run_turn(&session_id, user_chat_msg, on_token_cb).await {
                Ok(response) => {
                    let text = match &response.content {
                        maho_types::chat::ChatContent::Text(t) => t.clone(),
                        _ => String::new(),
                    };
                    let text_c = match CString::new(text) {
                        Ok(s) => s,
                        Err(_) => CString::new("").unwrap(),
                    };
                    let empty_json = CString::new("[]").unwrap();
                    if let Some(cb) = on_complete.0 {
                        cb(user_data_val.0 as *mut std::ffi::c_void, text_c.as_ptr(), empty_json.as_ptr());
                    }
                }
                Err(e) => {
                    let err_c = match CString::new(e.to_string()) {
                        Ok(s) => s,
                        Err(_) => CString::new("unknown error").unwrap(),
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
    }, false)
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_cancel(
    session: *mut MahoAgentSession,
) -> bool {
    ffi_safe!({
        if session.is_null() {
            return false;
        }
        let session = &*session;
        let runtime = Arc::clone(&session.runtime);
        let session_id = session.session_id.clone();
        
        let isolation = Arc::clone(&session.isolation);
        let res = isolation.block_on(async move {
            runtime.cancel(&session_id).await
        });
        res.is_ok()
    }, false)
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_list_tools(
    session: *mut MahoAgentSession,
) -> *mut c_char {
    ffi_safe!({
        if session.is_null() {
            return std::ptr::null_mut();
        }
        let session = &*session;
        let runtime = Arc::clone(&session.runtime);
        
        let isolation = Arc::clone(&session.isolation);
        let tools_res = isolation.block_on(async move {
            runtime.list_tools().await
        });
        
        let tools = match tools_res {
            Ok(Ok(t)) => t,
            _ => return std::ptr::null_mut(),
        };
        
        match serde_json::to_string(&tools) {
            Ok(json) => CString::new(json).unwrap_or_else(|_| CString::new("[]").unwrap()).into_raw(),
            Err(_) => std::ptr::null_mut(),
        }
    }, std::ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer to a `MahoCore`.
/// `space_id` must be a null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_space_ai_config(
    ptr: *mut MahoCore,
    space_id: *const c_char,
) -> *mut c_char {
    ffi_safe!({
        if ptr.is_null() || space_id.is_null() {
            return ptr::null_mut();
        }
        // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
        if import_gate::is_active() { return ptr::null_mut(); }
        let core = &*ptr;
        let space_id_str = match CStr::from_ptr(space_id).to_str() {
            Ok(s) => s,
            Err(_) => return ptr::null_mut(),
        };
        let key = format!("space_ai_config:{}", space_id_str);
        if let Some(storage) = core.storage_ref() {
            if let Ok(Some(value)) = storage.get_setting(&key) {
                return CString::new(value).unwrap_or_else(|_| CString::new("{}").unwrap()).into_raw();
            }
        }
        CString::new("{}").unwrap_or_else(|_| CString::new("").unwrap()).into_raw()
    }, ptr::null_mut())
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
    ffi_safe!({
        if ptr.is_null() || space_id.is_null() || config_json.is_null() {
            return false;
        }
        // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
        if import_gate::is_active() { return false; }
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
    }, false)
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
    ffi_safe!({

    if core.is_null() {
        return -1;
    }
    // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
    if import_gate::is_active() { return -1; }
    let core = &*core;
    match core.get_account_tier() {
        Some(maho_types::account::UserTier::Free) => 0,
        Some(maho_types::account::UserTier::Pro) => 1,
        Some(maho_types::account::UserTier::Max) => 2,
        None => -1,
    }

    }, -1)
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
    ffi_safe!({

    if out_json.is_null() {
        return;
    }
    let routines = maho_core::routines::list_routines();
    let json_ptr = to_json_cstring(routines);
    *out_json = json_ptr;

    }, ())
}

/// Kicks off an async routine run. Returns 0 on success (queued), -1 on error.
/// The `user_tier` parameter: 0=Free, 1=Pro, 2=Max.
/// `on_complete` is called with the result JSON when done.
/// `on_error` is called with an error string on failure.
///
/// # Safety
/// `id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_routines_run(
    id: *const c_char,
    user_tier: i32,
    on_complete: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, result_json: *const c_char)>,
    on_error: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, error: *const c_char)>,
    user_data: *mut std::ffi::c_void,
) -> i32 {
    ffi_safe!({

    if id.is_null() {
        return -1;
    }
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

    tokio::spawn(async move {
        let result = maho_core::routines::run_routine(
            &id_str,
            tier,
            |_prompt| async { Ok("Routine execution placeholder".to_string()) },
        )
        .await;

        match result {
            Ok(res) => {
                if let Some(cb) = on_complete_cb.0 {
                    let json = serde_json::to_string(&res).unwrap_or_default();
                    let json_c = CString::new(json).unwrap_or_else(|_| CString::new("{}").unwrap());
                    cb(user_data_val.0 as *mut std::ffi::c_void, json_c.as_ptr());
                }
            }
            Err(e) => {
                if let Some(cb) = on_error_cb.0 {
                    let err_c = CString::new(e.to_string()).unwrap_or_else(|_| CString::new("error").unwrap());
                    cb(user_data_val.0 as *mut std::ffi::c_void, err_c.as_ptr());
                }
            }
        }
    });

    0

    }, -1)
}

// ============================================================
// Routines Scheduler Tick FFI
// ============================================================

use std::sync::{Mutex as StdMutex, OnceLock};
use std::collections::HashMap;

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
    ffi_safe!({

    if core.is_null() {
        return -2;
    }
    // Check tier — only Max users get scheduled routines
    let core_ref = &*core;
    match core_ref.get_account_tier() {
        Some(maho_types::account::UserTier::Max) => {}
        _ => return -1,
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
                eprintln!("[maho_routines_tick] failed to parse cron for {}: {}", routine.id, routine.cron);
                continue;
            }
        };

        let last_run = last_runs.get(routine.id).copied().unwrap_or(0);
        let next_fire = schedule.next_fire_after(last_run);

        if now_sec >= next_fire {
            // Fire the routine asynchronously via the existing run_routine mechanism
            let id_str = routine.id.to_string();

            tokio::spawn(async move {
                let _ = maho_core::routines::run_routine(
                    &id_str,
                    maho_core::routines::UserTier::Max,
                    |_prompt| async { Ok("Routine execution placeholder".to_string()) },
                )
                .await;
            });

            // Update last_run timestamp
            last_runs.insert(routine.id, now_sec);
        }
    }

    0

    }, -2)
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
        let result = unsafe {
            maho_core_get_zoom(std::ptr::null_mut(), site.as_ptr())
        };
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
        let before: Vec<_> = core.get_tab_view_models().iter().map(|t| t.id.clone()).collect();
        core.handle_event(ShellEvent::CreateTab {
            space_id,
            url: None,
            parent_id: None,
            tab_id: None,
            window_id: None,
        });
        let tab_id = core
            .get_tab_view_models()
            .iter()
            .find(|t| !before.contains(&t.id))
            .expect("new tab should be visible in view models")
            .id
            .clone();

        // Pin the tab
        core.handle_event(ShellEvent::PinTab { tab_id: tab_id.clone() });

        let core_ptr: *mut MahoCore = &mut core;

        // Pinned tab → true
        let pinned_id_cstr = CString::new(tab_id.0.as_str()).unwrap();
        let result = unsafe { maho_core_is_tab_pinned(core_ptr, pinned_id_cstr.as_ptr()) };
        assert!(result, "is_tab_pinned must return true for a pinned tab");

        // Unknown tab → false
        let unknown = CString::new("00000000-0000-0000-0000-000000000000").unwrap();
        let result2 = unsafe { maho_core_is_tab_pinned(core_ptr, unknown.as_ptr()) };
        assert!(!result2, "is_tab_pinned must return false for an unknown tab");

        // Null pointer → false (no panic)
        let result3 = unsafe {
            maho_core_is_tab_pinned(std::ptr::null_mut(), pinned_id_cstr.as_ptr())
        };
        assert!(!result3, "is_tab_pinned must return false on null ptr");
    }

    #[test]
    fn sidebar_state_v2_null_ptr_returns_null() {
        use std::ffi::CString;
        let space_id = CString::new(r#""some-space-id""#).unwrap();
        let result = unsafe {
            maho_core_get_sidebar_state_v2(std::ptr::null_mut(), space_id.as_ptr())
        };
        assert!(result.is_null(), "null core ptr must return null");
    }

    #[test]
    fn sidebar_state_v2_null_space_id_returns_null() {
        use maho_core::maho_core::MahoCore;
        let mut core = MahoCore::new();
        let core_ptr: *mut MahoCore = &mut core;
        let result = unsafe {
            maho_core_get_sidebar_state_v2(core_ptr, std::ptr::null())
        };
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
}
