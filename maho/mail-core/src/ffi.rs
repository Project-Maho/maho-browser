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

pub mod ai_api;
pub mod calendar_api;
pub mod compose_api;
pub mod crypto_api;
pub mod data_api;
pub mod io_api;
pub mod onboarding_api;
pub mod organize_api;
pub mod otp_api;
pub mod read_api;
pub mod write_api;

struct StderrLogger;
impl log::Log for StderrLogger {
    fn enabled(&self, _metadata: &log::Metadata) -> bool {
        true
    }
    fn log(&self, record: &log::Record) {
        eprintln!("[mail-core:{}] {}", record.level(), record.args());
    }
    fn flush(&self) {}
}

static LOGGER_INIT: std::sync::Once = std::sync::Once::new();
static STDERR_LOGGER: StderrLogger = StderrLogger;

fn init_logging() {
    LOGGER_INIT.call_once(|| {
        if log::set_logger(&STDERR_LOGGER).is_ok() {
            log::set_max_level(log::LevelFilter::Debug);
        }
    });
}

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

#[no_mangle]
pub extern "C" fn MahoMailStartAllAccountSync() -> bool {
    ffi_bool(|| {
        let ctx = state::ctx()?;
        let rt = crate::runtime::runtime()
            .ok_or_else(|| MailFfiError::Internal("tokio runtime unavailable".to_string()))?;
        start_all_account_sync(&ctx, rt).map(|_| ())
    })
}

#[no_mangle]
pub extern "C" fn MahoMailSetSyncIntervalMinutes(minutes: u64) -> bool {
    crate::sync::set_sync_interval_minutes(minutes)
}

/// Guarded auto-start of sync + backfill for every known account. Per-account
/// work goes through the idempotent [`state::start_account_workers`] guard so a
/// second call never double-spawns. Enumeration is the only fallible step;
/// individual accounts cannot abort the loop (HC5). Returns the number of
/// accounts for which a new sync worker was spawned.
fn start_all_account_sync(ctx: &Arc<AppCtx>, rt: &tokio::runtime::Runtime) -> Result<usize> {
    let accounts = crate::read::list_accounts(ctx)?;
    log::info!(
        "[mail-ffi] start_all_account_sync: {} account(s)",
        accounts.len()
    );
    let mut spawned = 0usize;
    for account in accounts {
        let (sync, _backfill) = state::start_account_workers(rt, ctx, &account.id);
        log::info!(
            "[mail-ffi] worker start account={} sync={}",
            account.id,
            sync
        );
        if sync {
            spawned += 1;
        }
    }
    Ok(spawned)
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
    init_logging();
    let _ = rustls::crypto::ring::default_provider().install_default();
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
    let credential_key = decode_credential_key(&c_string(credential_key, "credential_key")?)?;
    let init = state::init_info().ok_or(MailFfiError::NotInitialized)?;
    let pool = maho_core::db::init_database_pool(&init.db_path)?;
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
pub type MahoMailEventCallback = Option<
    unsafe extern "C" fn(
        event_type: *const c_char,
        account_id: *const c_char,
        provider: *const c_char,
        reason: *const c_char,
    ),
>;

pub type MahoMailNewMailCallback = Option<
    unsafe extern "C" fn(
        account_id: *const c_char,
        email_id: *const c_char,
        message_id: *const c_char,
        sender: *const c_char,
        subject: *const c_char,
        cursor: u64,
        epoch: u64,
    ),
>;

type EventCallback = unsafe extern "C" fn(
    event_type: *const c_char,
    account_id: *const c_char,
    provider: *const c_char,
    reason: *const c_char,
);

type NewMailCallback = unsafe extern "C" fn(
    account_id: *const c_char,
    email_id: *const c_char,
    message_id: *const c_char,
    sender: *const c_char,
    subject: *const c_char,
    cursor: u64,
    epoch: u64,
);

static EVENT_CALLBACK: std::sync::OnceLock<EventCallback> = std::sync::OnceLock::new();
static NEW_MAIL_CALLBACK: std::sync::OnceLock<NewMailCallback> = std::sync::OnceLock::new();

#[no_mangle]
pub extern "C" fn MahoMailRegisterEventCallback(callback: MahoMailEventCallback) -> bool {
    let Some(callback) = callback else {
        return false;
    };
    EVENT_CALLBACK.set(callback).is_ok()
}

#[no_mangle]
pub extern "C" fn MahoMailRegisterNewMailCallback(callback: MahoMailNewMailCallback) -> bool {
    let Some(callback) = callback else {
        return false;
    };
    NEW_MAIL_CALLBACK.set(callback).is_ok()
}

fn safe_c_string(s: &str) -> std::ffi::CString {
    let mut bytes = Vec::new();
    for &b in s.as_bytes() {
        if b != 0 {
            bytes.push(b);
        }
    }
    // SAFETY: bytes has no NUL (0) bytes, making unchecked construction safe.
    unsafe { std::ffi::CString::from_vec_unchecked(bytes) }
}

pub fn emit_event(event_type: &str, account_id: &str, provider: &str, reason: &str) {
    if let Some(callback) = EVENT_CALLBACK.get() {
        let event_type_c = safe_c_string(event_type);
        let account_id_c = safe_c_string(account_id);
        let provider_c = safe_c_string(provider);
        let reason_c = safe_c_string(reason);
        unsafe {
            callback(
                event_type_c.as_ptr(),
                account_id_c.as_ptr(),
                provider_c.as_ptr(),
                reason_c.as_ptr(),
            );
        }
    }
}

pub fn emit_reauth_event(account_id: &str, provider: &str, reason: &str) {
    emit_event("auth-reauth-required", account_id, provider, reason);
}

pub fn emit_refresh_succeeded_event(account_id: &str) {
    emit_event("auth-refresh-succeeded", account_id, "", "");
}

pub fn emit_accounts_changed_event(account_id: &str) {
    emit_event("accounts-changed", account_id, "", "");
}

pub fn emit_new_mail_event(
    account_id: &str,
    email_id: &str,
    message_id: &str,
    sender: &str,
    subject: &str,
    cursor: u64,
    epoch: u64,
) {
    if let Some(callback) = NEW_MAIL_CALLBACK.get() {
        let account_id = safe_c_string(account_id);
        let email_id = safe_c_string(email_id);
        let message_id = safe_c_string(message_id);
        let sender = safe_c_string(sender);
        let subject = safe_c_string(subject);
        unsafe {
            callback(
                account_id.as_ptr(),
                email_id.as_ptr(),
                message_id.as_ptr(),
                sender.as_ptr(),
                subject.as_ptr(),
                cursor,
                epoch,
            );
        }
    }
}

#[cfg(test)]
mod lifecycle_tests;
