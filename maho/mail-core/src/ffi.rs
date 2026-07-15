// Copyright 2026 Maho Browser. All rights reserved.

use std::ffi::CStr;
use std::os::raw::c_char;
use std::path::PathBuf;
use std::sync::Arc;

use crate::backfill::start_backfill_worker;
use crate::credentials::decode_credential_key;
use crate::error::{MailFfiError, Result};
use crate::state::{self, AppCtx, InitInfo};
use crate::sync::start_sync_worker;

pub mod read_api;

#[no_mangle]
pub extern "C" fn MahoMailInitialize(
    version_token: *const c_char,
    profile_path: *const c_char,
) -> bool {
    ffi_bool(|| initialize(version_token, profile_path))
}

#[no_mangle]
pub extern "C" fn MahoMailInjectKeys(
    sqlcipher_key: *const c_char,
    credential_key: *const c_char,
) -> bool {
    ffi_bool(|| inject_keys(sqlcipher_key, credential_key))
}

#[no_mangle]
pub extern "C" fn MahoMailStartSync(account_id: *const c_char) -> bool {
    ffi_bool(|| start_sync(account_id))
}

#[no_mangle]
pub extern "C" fn MahoMailStopSync() -> bool {
    ffi_bool(|| {
        state::registry().stop_all_sync();
        state::registry().stop_all_backfill();
        Ok(())
    })
}

#[no_mangle]
pub extern "C" fn MahoMailStartBackfill(account_id: *const c_char) -> bool {
    ffi_bool(|| start_backfill(account_id))
}

fn ffi_bool(action: impl FnOnce() -> Result<()> + std::panic::UnwindSafe) -> bool {
    match std::panic::catch_unwind(action) {
        Ok(Ok(())) => true,
        Ok(Err(err)) => {
            log::error!("[mail-ffi] FFI call failed: {err}");
            false
        }
        Err(_) => {
            log::error!("[mail-ffi] FFI call panicked");
            false
        }
    }
}

fn initialize(version_token: *const c_char, profile_path: *const c_char) -> Result<()> {
    let version_token = c_string(version_token, "version_token")?;
    let profile_path = PathBuf::from(c_string(profile_path, "profile_path")?);
    let app_data_dir = profile_path.join("MahoMail");
    let db_path = app_data_dir.join("maho_mail.db");
    state::set_init_info(InitInfo {
        version_token,
        profile_path,
        app_data_dir,
        db_path,
    });
    let _ = crate::runtime::runtime()
        .ok_or_else(|| MailFfiError::Internal("failed to initialize tokio runtime".to_string()))?;
    Ok(())
}

fn inject_keys(sqlcipher_key: *const c_char, credential_key: *const c_char) -> Result<()> {
    let sqlcipher_key = c_string(sqlcipher_key, "sqlcipher_key")?;
    let credential_key = decode_credential_key(&c_string(credential_key, "credential_key")?);
    let init = state::init_info().ok_or(MailFfiError::NotInitialized)?;
    let pool = maho_core::db::init_database_pool(&init.db_path, &sqlcipher_key)?;
    state::set_ctx(Arc::new(AppCtx {
        pool,
        db_path: init.db_path.clone(),
        sqlcipher_key,
        credential_key,
    }))
}

fn start_sync(account_id: *const c_char) -> Result<()> {
    let account_id = non_empty_account_id(account_id)?;
    let ctx = state::ctx()?;
    let rt = crate::runtime::runtime()
        .ok_or_else(|| MailFfiError::Internal("tokio runtime unavailable".to_string()))?;
    // Enter the runtime so the worker's `tokio::spawn` runs on our runtime; the
    // FFI thread is the helper's Mojo thread, which has no ambient runtime.
    let _guard = rt.enter();
    let handle = start_sync_worker(ctx, account_id.clone());
    state::registry().insert_sync(account_id, handle);
    Ok(())
}

fn start_backfill(account_id: *const c_char) -> Result<()> {
    let account_id = non_empty_account_id(account_id)?;
    let ctx = state::ctx()?;
    let rt = crate::runtime::runtime()
        .ok_or_else(|| MailFfiError::Internal("tokio runtime unavailable".to_string()))?;
    let _guard = rt.enter();
    let handle = start_backfill_worker(ctx, account_id.clone());
    state::registry().insert_backfill(account_id, handle);
    Ok(())
}

fn non_empty_account_id(account_id: *const c_char) -> Result<String> {
    non_empty(account_id, "account_id")
}

/// Shared with `read_api` (a descendant module) for validated non-empty IDs.
fn non_empty(ptr: *const c_char, name: &'static str) -> Result<String> {
    let value = c_string(ptr, name)?;
    if value.trim().is_empty() {
        return Err(MailFfiError::InvalidArg(name));
    }
    Ok(value)
}

/// Shared with `read_api` (a descendant module) for boundary C-string parsing.
fn c_string(ptr: *const c_char, name: &'static str) -> Result<String> {
    if ptr.is_null() {
        return Err(MailFfiError::InvalidArg(name));
    }
    // SAFETY: [Category 8 — FFI boundary] `ptr` is non-null (checked above) and
    // the C caller guarantees a NUL-terminated string valid for this call; the
    // bytes are copied into owned Rust immediately, so nothing dangles after.
    let value = unsafe { CStr::from_ptr(ptr) };
    value
        .to_str()
        .map(str::to_owned)
        .map_err(|_| MailFfiError::InvalidArg(name))
}

#[cfg(test)]
mod lifecycle_tests;
