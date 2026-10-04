use std::ffi::{c_char, CStr, CString};
use std::ptr;

use maho_core::maho_core::MahoCore;
use maho_types::common::TabSnapshot;

use crate::common::{base64_encode, cstr_to_str, to_c_string, to_json_cstring};
use crate::ffi_safe;
use crate::import_gate;

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
            let core = &mut *ptr;
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
            let core = &mut *ptr;
            let tab_id_str = CStr::from_ptr(tab_id).to_str().unwrap_or("");
            match core.get_tab_snapshot_json(&maho_types::identifiers::TabId::new(tab_id_str)) {
                Some(json) => CString::new(json)
                    .map(|c| c.into_raw())
                    .unwrap_or(ptr::null_mut()),
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
            let core = &mut *ptr;
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
#[no_mangle]
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
