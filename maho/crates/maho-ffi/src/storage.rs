use std::ffi::{c_char, CStr, CString};
use std::ptr;

use maho_core::maho_core::MahoCore;

use crate::common::{cstr_to_str, to_c_string, to_json_cstring};
use crate::ffi_safe;
use crate::import_gate;

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
