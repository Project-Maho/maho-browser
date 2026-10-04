// Copyright 2026 Maho Browser. All rights reserved.

use std::ffi::CStr;
use std::os::raw::c_char;
use std::path::Path;

#[no_mangle]
pub unsafe extern "C" fn MahoMailInitialize(
    version_token: *const c_char,
    profile_path: *const c_char,
) -> bool {
    if version_token.is_null() || profile_path.is_null() {
        return false;
    }
    let _version = match CStr::from_ptr(version_token).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let profile = match CStr::from_ptr(profile_path).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };

    let app_data = Path::new(profile).join("MahoMail");
    let db_path = app_data.join("maho-mail.db");

    // Initialize credential store & DB path
    if let Err(e) = crate::credential_store::init_with_key(
        &app_data,
        &db_path,
        "default-cipher-key".to_string(),
    ) {
        log::error!("[FFI] Failed to init credential store: {}", e);
        return false;
    }

    true
}

#[no_mangle]
pub unsafe extern "C" fn MahoMailInjectKeys(
    sqlcipher_key: *const c_char,
    credential_key: *const c_char,
) -> bool {
    if sqlcipher_key.is_null() || credential_key.is_null() {
        return false;
    }
    let _sql_key = match CStr::from_ptr(sqlcipher_key).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let _cred_key = match CStr::from_ptr(credential_key).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };

    true
}

#[no_mangle]
pub unsafe extern "C" fn MahoMailStartSync(account_id: *const c_char) -> bool {
    if account_id.is_null() {
        return false;
    }
    let _acc_id = match CStr::from_ptr(account_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    true
}

#[no_mangle]
pub unsafe extern "C" fn MahoMailStopSync() -> bool {
    true
}

#[no_mangle]
pub unsafe extern "C" fn MahoMailStartBackfill(account_id: *const c_char) -> bool {
    if account_id.is_null() {
        return false;
    }
    let _acc_id = match CStr::from_ptr(account_id).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    true
}
