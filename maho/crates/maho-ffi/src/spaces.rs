use std::ffi::{c_char, CStr, CString};
use std::path::Path;
use std::ptr;

use maho_core::maho_core::MahoCore;
use maho_types::space::SpaceAIConfig;

use crate::common::{cstr_to_str, to_c_string, to_json_cstring};
use crate::ffi_safe;
use crate::import_gate;

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

fn profile_update_result_json(
    result: Result<maho_types::profile::ProfileConfig, maho_core::profile_manager::ProfileError>,
) -> *mut c_char {
    use maho_core::profile_manager::ProfileError;

    match result {
        Ok(profile) => to_json_cstring(&serde_json::json!({
            "ok": true,
            "profile": profile,
            "error": serde_json::Value::Null,
        })),
        Err(error) => {
            let code = match &error {
                ProfileError::EmptyName => "PROFILE_NAME_EMPTY",
                ProfileError::DuplicateName(_) => "PROFILE_NAME_DUPLICATE",
                ProfileError::NotFound => "PROFILE_NOT_FOUND",
                ProfileError::InvalidAvatarColor(_) => "PROFILE_AVATAR_COLOR_INVALID",
                ProfileError::InvalidArchiveTimeout => "PROFILE_ARCHIVE_TIMEOUT_INVALID",
                ProfileError::PersistenceFailed(_) => "PROFILE_PERSISTENCE_FAILED",
                ProfileError::LimitReached(_) | ProfileError::InternalError => {
                    "PROFILE_UPDATE_FAILED"
                }
            };
            to_json_cstring(&serde_json::json!({
                "ok": false,
                "profile": serde_json::Value::Null,
                "error": {
                    "code": code,
                    "message": error.to_string(),
                },
            }))
        }
    }
}

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
            let profile = match core.create_profile_persisted(name_str) {
                Ok(p) => p,
                Err(_) => return ptr::null_mut(),
            };
            to_json_cstring(&profile)
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `profile_id` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_profile(
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
            let core = &mut *ptr;
            let id_str = match CStr::from_ptr(profile_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let id = maho_types::identifiers::ProfileId::new(id_str);
            let outcome = core.delete_profile_detailed(&id);
            let result = serde_json::json!({
                "success": outcome.is_deleted(),
                "profile_id": if outcome.is_deleted() { Some(id_str) } else { None },
                "outcome": outcome,
                "error_code": outcome.error_code(),
            });
            to_json_cstring(&result)
        },
        ptr::null_mut()
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

/// Updates the profile identified by `profile_id` without switching the active profile.
/// The returned JSON is `{ok, profile, error}` and preserves validation and lookup errors.
///
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new*`.
/// `profile_id`, `name`, and `avatar_color` must be valid null-terminated C strings.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_profile(
    ptr: *mut MahoCore,
    profile_id: *const c_char,
    name: *const c_char,
    avatar_color: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || profile_id.is_null() || name.is_null() || avatar_color.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let id_str = match CStr::from_ptr(profile_id).to_str() {
                Ok(value) => value,
                Err(_) => return ptr::null_mut(),
            };
            let name = match CStr::from_ptr(name).to_str() {
                Ok(value) => value.to_string(),
                Err(_) => return ptr::null_mut(),
            };
            let avatar_color = match CStr::from_ptr(avatar_color).to_str() {
                Ok(value) => value.to_string(),
                Err(_) => return ptr::null_mut(),
            };
            let id = maho_types::identifiers::ProfileId::new(id_str);
            profile_update_result_json((&mut *ptr).update_profile_persisted(
                &id,
                Some(name),
                Some(avatar_color),
                None,
                None,
            ))
        },
        ptr::null_mut()
    )
}

/// Returns the profile archive timeout as `{ok, timeout_hours, error}`.
/// `timeout_hours` is `0` when per-profile auto-archive is disabled.
///
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new*`.
/// `profile_id` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_profile_archive_timeout(
    ptr: *mut MahoCore,
    profile_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || profile_id.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let id_str = match CStr::from_ptr(profile_id).to_str() {
                Ok(value) => value,
                Err(_) => return ptr::null_mut(),
            };
            let id = maho_types::identifiers::ProfileId::new(id_str);
            match (&*ptr)
                .list_profiles()
                .iter()
                .find(|profile| profile.id == id)
            {
                Some(profile) => to_json_cstring(&serde_json::json!({
                    "ok": true,
                    "timeout_hours": profile.archive_timeout_hours.unwrap_or(0.0),
                    "error": serde_json::Value::Null,
                })),
                None => to_json_cstring(&serde_json::json!({
                    "ok": false,
                    "timeout_hours": serde_json::Value::Null,
                    "error": {
                        "code": "PROFILE_NOT_FOUND",
                        "message": "Profile not found",
                    },
                })),
            }
        },
        ptr::null_mut()
    )
}

/// Sets the profile archive timeout without switching the active profile.
/// `timeout_hours == 0` disables per-profile auto-archive.
///
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new*`.
/// `profile_id` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_profile_archive_timeout(
    ptr: *mut MahoCore,
    profile_id: *const c_char,
    timeout_hours: i32,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || profile_id.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let id_str = match CStr::from_ptr(profile_id).to_str() {
                Ok(value) => value,
                Err(_) => return ptr::null_mut(),
            };
            let id = maho_types::identifiers::ProfileId::new(id_str);
            let archive_timeout = if timeout_hours == 0 {
                None
            } else {
                Some(timeout_hours as f64)
            };
            profile_update_result_json((&mut *ptr).update_profile_persisted(
                &id,
                None,
                None,
                None,
                Some(archive_timeout),
            ))
        },
        ptr::null_mut()
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
