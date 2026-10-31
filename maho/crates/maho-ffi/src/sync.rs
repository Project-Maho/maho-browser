use std::ffi::{c_char, CStr, CString};
use std::ptr;

use maho_core::maho_core::MahoCore;

use crate::common::{cstr_to_str, to_c_string, to_json_cstring};
use crate::ffi_safe;
use crate::import_gate;

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
pub unsafe extern "C" fn maho_core_get_sync_room_id(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            core.get_sync_room_id()
                .and_then(|room_id| CString::new(room_id).ok())
                .map_or(ptr::null_mut(), CString::into_raw)
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
/// `envelope_json` must be a valid C string containing a V2 sync envelope.
#[no_mangle]
pub unsafe extern "C" fn maho_core_handle_incoming_sync_envelope(
    ptr: *mut MahoCore,
    envelope_json: *const c_char,
) -> i32 {
    ffi_safe!(
        {
            if ptr.is_null() || envelope_json.is_null() || import_gate::is_active() {
                return -1;
            }
            let envelope_json = match CStr::from_ptr(envelope_json).to_str() {
                Ok(value) => value,
                Err(_) => return -1,
            };
            match (&mut *ptr).handle_incoming_sync_envelope(envelope_json) {
                Ok(()) => 0,
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
pub unsafe extern "C" fn maho_core_export_sync_snapshot(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            match core.export_sync_snapshot() {
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
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `snapshot_json` must be a valid C string containing snapshot JSON.
#[no_mangle]
pub unsafe extern "C" fn maho_core_apply_sync_snapshot(
    ptr: *mut MahoCore,
    snapshot_json: *const c_char,
) -> i32 {
    ffi_safe!(
        {
            if ptr.is_null() || snapshot_json.is_null() || import_gate::is_active() {
                return -1;
            }
            let snapshot_json = match CStr::from_ptr(snapshot_json).to_str() {
                Ok(value) => value,
                Err(_) => return -1,
            };
            match (&mut *ptr).apply_sync_snapshot(snapshot_json) {
                Ok(count) => count as i32,
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
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_generate_sync_bootstrap(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            CString::new(core.generate_sync_bootstrap())
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
                })
                .to_string(),
                Err(e) => serde_json::json!({
                    "success": false,
                    "error": e
                })
                .to_string(),
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
/// `server_url` and `bootstrap_seed` must be valid null-terminated C strings.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_configure_sync_bootstrap(
    ptr: *mut MahoCore,
    server_url: *const c_char,
    bootstrap_seed: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || server_url.is_null() || bootstrap_seed.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let url = match CStr::from_ptr(server_url).to_str() {
                Ok(value) => value,
                Err(_) => return ptr::null_mut(),
            };
            let seed = match CStr::from_ptr(bootstrap_seed).to_str() {
                Ok(value) => value,
                Err(_) => return ptr::null_mut(),
            };
            let json = match core.configure_sync_encryption_from_bootstrap(url, seed) {
                Ok(room_id) => serde_json::json!({
                    "success": true,
                    "roomId": room_id,
                    "deviceId": core.sync_device_id(),
                    "vault": core.last_vault_account_outcome_code().unwrap_or("none")
                }),
                Err(error) => serde_json::json!({
                    "success": false,
                    "error": error
                }),
            };
            CString::new(json.to_string())
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `server_url` and `bootstrap_seed` must be valid null-terminated C strings.
/// `escrow_kdf_params_b64` and `escrow_wrapped_key_b64` may be null (no account
/// escrow record) or valid null-terminated base64 strings.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_configure_sync_bootstrap_with_escrow(
    ptr: *mut MahoCore,
    server_url: *const c_char,
    bootstrap_seed: *const c_char,
    escrow_kdf_params_b64: *const c_char,
    escrow_wrapped_key_b64: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || server_url.is_null() || bootstrap_seed.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let url = match CStr::from_ptr(server_url).to_str() {
                Ok(value) => value,
                Err(_) => return ptr::null_mut(),
            };
            let seed = match CStr::from_ptr(bootstrap_seed).to_str() {
                Ok(value) => value,
                Err(_) => return ptr::null_mut(),
            };
            let escrow_kdf = if escrow_kdf_params_b64.is_null() {
                None
            } else {
                match CStr::from_ptr(escrow_kdf_params_b64).to_str() {
                    Ok(value) => Some(value),
                    Err(_) => return ptr::null_mut(),
                }
            };
            let escrow_wrapped = if escrow_wrapped_key_b64.is_null() {
                None
            } else {
                match CStr::from_ptr(escrow_wrapped_key_b64).to_str() {
                    Ok(value) => Some(value),
                    Err(_) => return ptr::null_mut(),
                }
            };
            let json = match core.configure_sync_encryption_from_bootstrap_with_escrow(
                url,
                seed,
                escrow_kdf,
                escrow_wrapped,
            ) {
                Ok(room_id) => serde_json::json!({
                    "success": true,
                    "roomId": room_id,
                    "deviceId": core.sync_device_id(),
                    "vault": core.last_vault_account_outcome_code().unwrap_or("none")
                }),
                Err(error) => serde_json::json!({
                    "success": false,
                    "error": error
                }),
            };
            CString::new(json.to_string())
                .map(CString::into_raw)
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// Exports the account-escrow vault key slots (base64) for publication to the
/// account bootstrap record. `success: true` without the slots means the vault
/// currently has no account wrap.
///
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_export_vault_account_escrow(
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
            let core = &mut *ptr;
            let escrow = core.export_vault_account_escrow_b64().unwrap_or(None);
            let json = match escrow {
                Some((kdf_params, wrapped_account_key)) => serde_json::json!({
                    "success": true,
                    "kdfParams": kdf_params,
                    "wrappedAccountKey": wrapped_account_key
                }),
                None => serde_json::json!({"success": true}),
            };
            CString::new(json.to_string())
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

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `delivery_id` must be a valid C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_ack_sync_delivery(
    ptr: *mut MahoCore,
    delivery_id: *const c_char,
    relay_seq: u64,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || delivery_id.is_null() || import_gate::is_active() {
                return false;
            }
            let delivery_id = match CStr::from_ptr(delivery_id).to_str() {
                Ok(value) => value,
                Err(_) => return false,
            };
            (&*ptr)
                .ack_sync_delivery(delivery_id, relay_seq)
                .unwrap_or(false)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `ack_json` must be a valid C string containing a relay V2 ACK.
#[no_mangle]
pub unsafe extern "C" fn maho_core_accept_sync_ack(
    ptr: *mut MahoCore,
    ack_json: *const c_char,
) -> i32 {
    ffi_safe!(
        {
            if ptr.is_null() || ack_json.is_null() || import_gate::is_active() {
                return -1;
            }
            let ack_json = match CStr::from_ptr(ack_json).to_str() {
                Ok(value) => value,
                Err(_) => return -1,
            };
            match (&*ptr).accept_sync_ack(ack_json) {
                Ok(true) => 0,
                Ok(false) | Err(_) => -1,
            }
        },
        -1
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `room_id` must be a valid C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_get_sync_receive_cursor(
    ptr: *mut MahoCore,
    room_id: *const c_char,
) -> u64 {
    ffi_safe!(
        {
            if ptr.is_null() || room_id.is_null() || import_gate::is_active() {
                return 0;
            }
            let room_id = match CStr::from_ptr(room_id).to_str() {
                Ok(value) => value,
                Err(_) => return 0,
            };
            (&*ptr).get_sync_receive_cursor(room_id).unwrap_or(0)
        },
        0
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

/// # Safety
/// `ptr` must be valid.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_drain_outgoing_envelopes(ptr: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || import_gate::is_active() {
                return ptr::null_mut();
            }
            let envelopes = match (&mut *ptr).drain_sync_outgoing_envelopes() {
                Ok(envelopes) => envelopes,
                Err(_) => return ptr::null_mut(),
            };
            to_json_cstring(&envelopes)
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
