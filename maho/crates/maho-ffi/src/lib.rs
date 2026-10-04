#![allow(clippy::missing_safety_doc)]

pub mod import;
pub mod import_destination;
pub(crate) mod import_gate;
pub mod vault_backend;

use std::borrow::Cow;
use std::cell::{Cell, RefCell};
use std::ffi::{c_char, c_void, CStr, CString};
use std::future::Future;
use std::path::Path;
use std::pin::Pin;
use std::ptr;
use std::str::FromStr;
use std::sync::{Arc as FfiArc, Condvar as FfiCondvar, Mutex as FfiMutex, MutexGuard};
use zeroize::{Zeroize, Zeroizing};

use maho_core::content_blocker::normalize_site_exception_key;
use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::VaultPreflightState;
use maho_core::workspace_manager::WorkspaceManager;

use maho_types::keyboard::KeyCombo;
use maho_types::vault::{
    CredentialOrigin, VaultItemId, VaultItemKind, VaultItemListRequest, VaultItemPublicDto,
    VaultItemPublicMetadata, VaultSchemaVersion,
};

static FFI_SERIALIZATION_GATE: FfiMutex<()> = FfiMutex::new(());

thread_local! {
    static FFI_SERIALIZATION_DEPTH: Cell<usize> = const { Cell::new(0) };
    static DEFERRED_CORE_UPDATES: RefCell<Vec<maho_types::events::core_update::CoreUpdate>> =
        const { RefCell::new(Vec::new()) };
    static ACTIVE_CALLBACK_TOKENS: RefCell<Vec<u64>> = const { RefCell::new(Vec::new()) };
}

struct FfiSerializationScope {
    _guard: Option<MutexGuard<'static, ()>>,
}

impl FfiSerializationScope {
    fn enter() -> Self {
        let nested = FFI_SERIALIZATION_DEPTH.with(|depth| {
            let current = depth.get();
            depth.set(current + 1);
            current > 0
        });
        let guard = (!nested).then(lock_ffi_serialization_gate);
        Self { _guard: guard }
    }
}

#[cfg(not(test))]
fn lock_ffi_serialization_gate() -> MutexGuard<'static, ()> {
    FFI_SERIALIZATION_GATE
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
}

/// Test-build-only variant: identical exclusion, but records the calling
/// thread in [`gate_probe`] while it is genuinely parked on the gate, so tests
/// can await that exact event instead of guessing with a sleep.
#[cfg(test)]
fn lock_ffi_serialization_gate() -> MutexGuard<'static, ()> {
    match FFI_SERIALIZATION_GATE.try_lock() {
        Ok(guard) => guard,
        Err(std::sync::TryLockError::Poisoned(poisoned)) => poisoned.into_inner(),
        Err(std::sync::TryLockError::WouldBlock) => {
            gate_probe::mark_blocked();
            let guard = FFI_SERIALIZATION_GATE
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            gate_probe::clear_blocked();
            guard
        }
    }
}

/// Observes which threads are currently blocked on `FFI_SERIALIZATION_GATE`.
/// Keyed by `ThreadId` so a test only ever waits on its own thread and cannot
/// be satisfied by unrelated contention from a parallel test.
#[cfg(test)]
pub(crate) mod gate_probe {
    use std::collections::HashSet;
    use std::sync::{Condvar, Mutex, OnceLock};
    use std::thread::ThreadId;
    use std::time::{Duration, Instant};

    #[derive(Default)]
    struct Probe {
        blocked: Mutex<HashSet<ThreadId>>,
        changed: Condvar,
    }

    fn probe() -> &'static Probe {
        static PROBE: OnceLock<Probe> = OnceLock::new();
        PROBE.get_or_init(Probe::default)
    }

    fn with_blocked(mutate: impl FnOnce(&mut HashSet<ThreadId>)) {
        let probe = probe();
        let mut blocked = probe
            .blocked
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        mutate(&mut blocked);
        drop(blocked);
        probe.changed.notify_all();
    }

    pub(crate) fn mark_blocked() {
        let id = std::thread::current().id();
        with_blocked(|blocked| {
            blocked.insert(id);
        });
    }

    pub(crate) fn clear_blocked() {
        let id = std::thread::current().id();
        with_blocked(|blocked| {
            blocked.remove(&id);
        });
    }

    /// Waits until `id` is parked on the FFI serialization gate. Returns false
    /// if `timeout` elapses first, which means the gate did not serialize.
    pub(crate) fn wait_until_blocked(id: ThreadId, timeout: Duration) -> bool {
        let probe = probe();
        let deadline = Instant::now() + timeout;
        let mut blocked = probe
            .blocked
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        while !blocked.contains(&id) {
            let Some(remaining) = deadline.checked_duration_since(Instant::now()) else {
                return false;
            };
            let (next, timed_out) = probe
                .changed
                .wait_timeout(blocked, remaining)
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            blocked = next;
            if timed_out.timed_out() && !blocked.contains(&id) {
                return false;
            }
        }
        true
    }
}

impl Drop for FfiSerializationScope {
    fn drop(&mut self) {
        FFI_SERIALIZATION_DEPTH.with(|depth| depth.set(depth.get().saturating_sub(1)));
    }
}

#[doc(hidden)]
pub fn with_ffi_serialization<T>(operation: impl FnOnce() -> T) -> T {
    let scope = FfiSerializationScope::enter();
    let is_outermost = scope._guard.is_some();
    let result = operation();
    drop(scope);
    if is_outermost {
        let updates = DEFERRED_CORE_UPDATES.with(|updates| updates.take());
        dispatch_split_callbacks_for_updates(&updates);
    }
    result
}

#[no_mangle]
pub extern "C" fn maho_ffi_serialization_scope_enter() -> *mut c_void {
    match std::panic::catch_unwind(FfiSerializationScope::enter) {
        Ok(scope) => Box::into_raw(Box::new(scope)).cast(),
        Err(_) => ptr::null_mut(),
    }
}

/// # Safety
/// `scope` must be null or returned by `maho_ffi_serialization_scope_enter`
/// on the same thread.
#[no_mangle]
pub unsafe extern "C" fn maho_ffi_serialization_scope_exit(scope: *mut c_void) {
    if !scope.is_null() {
        let scope = unsafe { Box::from_raw(scope.cast::<FfiSerializationScope>()) };
        let is_outermost = scope._guard.is_some();
        drop(scope);
        if is_outermost {
            let updates = DEFERRED_CORE_UPDATES.with(|updates| updates.take());
            dispatch_split_callbacks_for_updates(&updates);
        }
    }
}

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

fn credential_origin_from_domain(domain: &str) -> Option<CredentialOrigin> {
    let origin = if domain.starts_with("http://") || domain.starts_with("https://") {
        domain.to_string()
    } else {
        format!("https://{domain}")
    };
    CredentialOrigin::try_from(origin).ok()
}

fn saved_password_from_vault_item(item: &VaultItemPublicDto) -> Option<PasswordEntryView> {
    if item.item_kind != VaultItemKind::Login {
        return None;
    }
    let domain = item.origins.first()?.as_str().to_string();
    Some(PasswordEntryView {
        id: item.id.to_string(),
        domain,
        username: item.username_hint.clone(),
        created_at: item.created_at.to_rfc3339(),
        last_used: item.last_used_at.map(|value| value.to_rfc3339()),
    })
}

fn to_vault_password_entries(items: Vec<VaultItemPublicDto>) -> *mut c_char {
    let views: Vec<PasswordEntryView> = items
        .iter()
        .filter_map(saved_password_from_vault_item)
        .collect();
    to_json_cstring(&views)
}

fn vault_login_items(core: &MahoCore) -> Vec<VaultItemPublicDto> {
    let request = VaultItemListRequest {
        trash: Default::default(),
        favorites_only: false,
        schema_version: VaultSchemaVersion::CURRENT,
        provider: Some(maho_types::passwords::PasswordProviderKind::MahoNative),
        kinds: vec![VaultItemKind::Login],
        cursor: None,
        limit: 0,
    };
    core.vault_list_items(&request).unwrap_or_default()
}

fn vault_login_item_by_id(core: &MahoCore, id: VaultItemId) -> Option<VaultItemPublicDto> {
    vault_login_items(core)
        .into_iter()
        .find(|item| item.id == id)
}

/// Stable integer values returned by `maho_core_vault_preflight_state`.
#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MahoVaultPreflightState {
    Healthy = 0,
    Locked = 1,
    UnrecoverableKey = 2,
    StructuralCorruption = 3,
    PlaintextResidue = 4,
}

const MAHO_VAULT_PREFLIGHT_STATE_INVALID: i32 = -1;

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct VaultPreflightStatePayload {
    state: &'static str,
    value: i32,
}

fn encode_vault_preflight_state(
    state: VaultPreflightState,
    out_state_json: *mut *mut c_char,
) -> i32 {
    let (state, state_name) = match state {
        VaultPreflightState::Healthy => (MahoVaultPreflightState::Healthy, "healthy"),
        VaultPreflightState::Locked => (MahoVaultPreflightState::Locked, "locked"),
        VaultPreflightState::UnrecoverableKey => (
            MahoVaultPreflightState::UnrecoverableKey,
            "unrecoverableKey",
        ),
        VaultPreflightState::StructuralCorruption => (
            MahoVaultPreflightState::StructuralCorruption,
            "structuralCorruption",
        ),
        VaultPreflightState::PlaintextResidue => (
            MahoVaultPreflightState::PlaintextResidue,
            "plaintextResidue",
        ),
    };
    let value = state as i32;
    if !out_state_json.is_null() {
        // SAFETY: The caller supplied a non-null out pointer. Ownership of the
        // allocated string transfers to the caller and is released by
        // `maho_string_free`, matching the rest of this C ABI.
        unsafe {
            *out_state_json = to_json_cstring(&VaultPreflightStatePayload {
                state: state_name,
                value,
            });
        }
    }
    value
}

/// Performs a read-only Vault database preflight.
///
/// Stable return values are defined by `MahoVaultPreflightState`; `-1` means
/// invalid input, an active import, or a caught panic. When `out_state_json` is
/// non-null, successful calls also return `{"state": ..., "value": ...}` and
/// the caller must free that string with `maho_string_free`.
///
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `database_path` must be valid null-terminated UTF-8. When
/// `key_store_key_available` is true, `database_key` must also be valid
/// null-terminated UTF-8. `out_state_json` may be null or point to writable
/// storage for one C string pointer.
#[no_mangle]
pub unsafe extern "C" fn maho_core_vault_preflight_state(
    ptr: *mut MahoCore,
    database_path: *const c_char,
    database_key: *const c_char,
    key_store_key_available: bool,
    out_state_json: *mut *mut c_char,
) -> i32 {
    ffi_safe!(
        {
            if !out_state_json.is_null() {
                *out_state_json = ptr::null_mut();
            }
            if ptr.is_null() || database_path.is_null() {
                return MAHO_VAULT_PREFLIGHT_STATE_INVALID;
            }
            if import_gate::is_active() {
                return MAHO_VAULT_PREFLIGHT_STATE_INVALID;
            }
            let database_path = match CStr::from_ptr(database_path).to_str() {
                Ok(path) => Path::new(path),
                Err(_) => return MAHO_VAULT_PREFLIGHT_STATE_INVALID,
            };
            let database_key = if key_store_key_available {
                if database_key.is_null() {
                    return MAHO_VAULT_PREFLIGHT_STATE_INVALID;
                }
                match CStr::from_ptr(database_key).to_str() {
                    Ok(key) => maho_storage::sqlite::VaultDatabaseKey::Available(key),
                    Err(_) => return MAHO_VAULT_PREFLIGHT_STATE_INVALID,
                }
            } else {
                maho_storage::sqlite::VaultDatabaseKey::Unrecoverable
            };
            let state = (&*ptr).vault_preflight_state(database_path, database_key);
            encode_vault_preflight_state(state, out_state_json)
        },
        MAHO_VAULT_PREFLIGHT_STATE_INVALID
    )
}

#[cfg(test)]
mod vault_preflight_ffi_tests {
    use super::*;
    use chrono::{TimeZone, Utc};
    use maho_core::vault_manager::VaultManager;
    use maho_storage::sqlite::{SqliteStorage, VaultMetadataRow};

    const DATABASE_KEY: &str = "vault-preflight-ffi-database-key";
    const MASTER_PASSPHRASE: &[u8] = b"correct horse battery staple";
    const RECOVERY_SECRET: &[u8] = b"deterministic recovery secret";

    fn create_encrypted_database(path: &Path) {
        drop(
            SqliteStorage::open_with_key(path.to_str().expect("UTF-8 test path"), DATABASE_KEY)
                .expect("create encrypted profile database"),
        );
    }

    fn call_preflight_export(
        core: &mut MahoCore,
        path: &Path,
        key: Option<&str>,
    ) -> (i32, serde_json::Value) {
        let path = CString::new(path.to_str().expect("UTF-8 test path")).expect("path CString");
        let key = key.map(|value| CString::new(value).expect("key CString"));
        let mut state_json = ptr::null_mut();
        // SAFETY: All pointers remain valid for the duration of this direct ABI
        // call, and the returned CString is consumed below exactly once.
        let value = unsafe {
            maho_core_vault_preflight_state(
                core,
                path.as_ptr(),
                key.as_ref().map_or(ptr::null(), |value| value.as_ptr()),
                key.is_some(),
                &mut state_json,
            )
        };
        assert!(!state_json.is_null());
        // SAFETY: The ABI returned ownership of one Rust CString.
        let state_json = unsafe { CString::from_raw(state_json) };
        let payload = serde_json::from_str(state_json.to_str().expect("UTF-8 state JSON"))
            .expect("valid state JSON");
        (value, payload)
    }

    #[test]
    fn vault_preflight_export_preserves_every_typed_state() {
        let dir = tempfile::tempdir().expect("tempdir");
        let healthy_path = dir.path().join("healthy.db");
        create_encrypted_database(&healthy_path);

        let locked_path = dir.path().join("locked.db");
        let storage = SqliteStorage::open_with_key(
            locked_path.to_str().expect("UTF-8 test path"),
            DATABASE_KEY,
        )
        .expect("create locked profile database");
        let mut manager = VaultManager::new();
        let now = Utc.with_ymd_and_hms(2026, 8, 12, 12, 0, 0).unwrap();
        let slots = manager
            .initialize(MASTER_PASSPHRASE, RECOVERY_SECRET, now)
            .expect("create valid wrapped-key slots");
        for (slot, payload) in [
            ("kdf_params", slots.kdf_params),
            ("wrapped_user_key", slots.wrapped_user_key),
            ("wrapped_recovery_key", slots.wrapped_recovery_key),
        ] {
            storage
                .save_vault_metadata(&VaultMetadataRow {
                    slot: slot.to_string(),
                    schema_version: 1,
                    payload,
                    updated_at: now.to_rfc3339(),
                })
                .expect("persist wrapped-key slot");
        }
        drop(storage);

        let lost_key_path = dir.path().join("lost-key.db");
        create_encrypted_database(&lost_key_path);

        let corrupt_path = dir.path().join("corrupt.db");
        std::fs::write(&corrupt_path, b"not a sqlite or sqlcipher database")
            .expect("write corrupt database fixture");

        let residue_path = dir.path().join("residue.db");
        create_encrypted_database(&residue_path);
        std::fs::write(
            residue_path.with_extension("db.plain_backup"),
            b"stale plaintext backup residue",
        )
        .expect("write plaintext residue marker");

        let mut core = MahoCore::new();
        for (path, key, expected_value, expected_name) in [
            (healthy_path.as_path(), Some(DATABASE_KEY), 0, "healthy"),
            (locked_path.as_path(), Some(DATABASE_KEY), 1, "locked"),
            (lost_key_path.as_path(), None, 2, "unrecoverableKey"),
            (
                corrupt_path.as_path(),
                Some(DATABASE_KEY),
                3,
                "structuralCorruption",
            ),
            (
                residue_path.as_path(),
                Some(DATABASE_KEY),
                4,
                "plaintextResidue",
            ),
        ] {
            let (value, payload) = call_preflight_export(&mut core, path, key);
            assert_eq!(value, expected_value);
            assert_eq!(payload["state"], expected_name);
            assert_eq!(payload["value"], expected_value);
        }
    }
}

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

            let mut items = vault_login_items(core);
            if !query_str.is_empty() {
                let query_lower = query_str.to_lowercase();
                items.retain(|item| {
                    item.title.to_lowercase().contains(&query_lower)
                        || item.username_hint.to_lowercase().contains(&query_lower)
                        || item
                            .origins
                            .iter()
                            .any(|origin| origin.as_str().to_lowercase().contains(&query_lower))
                });
            }
            to_vault_password_entries(items)
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
            let Ok(item_id) = VaultItemId::from_str(password_id) else {
                return false;
            };
            let Some(item) = vault_login_item_by_id(core, item_id) else {
                return false;
            };
            core.vault_delete_item(item_id, item.revision).is_ok()
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
            let core = &mut *ptr;
            let password_id = match CStr::from_ptr(password_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let Ok(item_id) = VaultItemId::from_str(password_id) else {
                return ptr::null_mut();
            };
            match core.vault_use_login_password(item_id) {
                Ok(password) => CString::new(password.as_str())
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
            let pass = password.unwrap_or_default();
            if let Some(origin) = credential_origin_from_domain(domain) {
                let input = maho_core::vault_manager::VaultLoginInput {
                    metadata: maho_types::vault::VaultItemPublicMetadata {
                        favorite: false,
                        trashed_at: None,
                        has_notes: false,
                        title: domain.to_string(),
                        origins: vec![origin],
                        username_hint: username.to_string(),
                        item_kind: maho_types::vault::VaultItemKind::Login,
                        totp: None,
                        passkey: None,
                    },
                    username: username.to_string(),
                    password: zeroize::Zeroizing::new(pass),
                    form_details: None,
                };
                core.vault_add_login(input).is_ok()
            } else {
                false
            }
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
            let Ok(item_id) = VaultItemId::from_str(password_id) else {
                return false;
            };
            let Some(item) = vault_login_item_by_id(core, item_id) else {
                return false;
            };
            let metadata = VaultItemPublicMetadata {
                favorite: false,
                trashed_at: None,
                has_notes: false,
                title: item.title,
                origins: item.origins,
                username_hint: username.to_string(),
                item_kind: VaultItemKind::Login,
                totp: item.totp,
                passkey: item.passkey,
            };
            let update = maho_core::vault_manager::VaultLoginUpdate {
                notes: None,
                metadata,
                username: username.to_string(),
                password: None,
                form_details: None,
            };
            core.vault_update_login(item_id, item.revision, update)
                .is_ok()
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
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
/// `tab_id_ptr` must be a valid null-terminated C string.
/// `icon_ptr` may be null (or an empty/whitespace string) to clear the custom icon.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_tab_custom_icon(
    ptr: *mut MahoCore,
    tab_id_ptr: *const std::os::raw::c_char,
    icon_ptr: *const std::os::raw::c_char,
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

            let custom_icon = if icon_ptr.is_null() {
                None
            } else {
                let icon_str = std::ffi::CStr::from_ptr(icon_ptr).to_string_lossy();
                let trimmed = icon_str.trim();
                if trimmed.is_empty() {
                    None
                } else {
                    Some(trimmed.to_string())
                }
            };

            let event = maho_types::events::shell_event::ShellEvent::SetTabCustomIcon {
                tab_id,
                custom_icon,
            };
            core.handle_event(event);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new` or `maho_core_new_with_storage`.
/// `tab_id_ptr` and `url_ptr` must be valid null-terminated C strings.
/// Returns true when the tab exists and its home (pinned) URL was replaced.
#[no_mangle]
pub unsafe extern "C" fn maho_core_set_tab_pinned_url(
    ptr: *mut MahoCore,
    tab_id_ptr: *const std::os::raw::c_char,
    url_ptr: *const std::os::raw::c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || tab_id_ptr.is_null() || url_ptr.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let tab_id_str = std::ffi::CStr::from_ptr(tab_id_ptr).to_string_lossy();
            let tab_id = maho_types::identifiers::TabId::new(tab_id_str.as_ref());
            let url_str = std::ffi::CStr::from_ptr(url_ptr).to_string_lossy();
            let trimmed = url_str.trim();
            if trimmed.is_empty() {
                return false;
            }

            core.set_tab_pinned_url(&tab_id, maho_types::common::Url::new(trimmed))
        },
        false
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

/// Adds a bookmark and returns it as JSON (`{"id","url","title","folder"}`),
/// so callers can answer with the stored entry instead of guessing its id.
///
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `url` and `title` must be valid null-terminated C strings.
/// `folder_id` may be null to use the default folder.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_bookmark_json(
    ptr: *mut MahoCore,
    url: *const c_char,
    title: *const c_char,
    folder_id: *const c_char,
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
            let url_str = match CStr::from_ptr(url).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let title_str = match CStr::from_ptr(title).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let folder = if folder_id.is_null() {
                None
            } else {
                match CStr::from_ptr(folder_id).to_str() {
                    Ok(s) if !s.is_empty() => Some(s),
                    Ok(_) => None,
                    Err(_) => return ptr::null_mut(),
                }
            };
            match core.add_bookmark_returning_id(url_str, title_str, folder) {
                Some(id) => to_json_cstring(&serde_json::json!({
                    "id": id,
                    "url": url_str,
                    "title": title_str,
                    "folder": folder,
                })),
                None => ptr::null_mut(),
            }
        },
        ptr::null_mut()
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
/// Caller must free the returned string with `maho_core_free_string`.
#[no_mangle]
pub unsafe extern "C" fn maho_memory_get_l1_briefing(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = unsafe { &*core };
            match core.get_l1_briefing() {
                Some(content) => CString::new(content)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
                None => ptr::null_mut(),
            }
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
                    "deviceId": core.sync_device_id()
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
    on_thinking: Option<
        unsafe extern "C" fn(user_data: *mut std::ffi::c_void, thinking: *const c_char),
    >,
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
                on_thinking,
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
        $crate::with_ffi_serialization(|| {
            match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| $body)) {
                Ok(val) => val,
                Err(_) => $fallback,
            }
        })
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
                storage
                    .list_conversations(maho_types::chat::ConversationListState::Active, limit)
                    .unwrap_or_default()
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
            CString::new(serde_json::to_string(&(&*core).active_conversation_ids()).unwrap())
                .unwrap()
                .into_raw()
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
                Ok(v) => serde_json::to_value(v).unwrap(),
                Err(v) => v,
            };
            CString::new(value.to_string()).unwrap().into_raw()
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
            CString::new(serde_json::to_string(&(&*core).list_conversation_projects()).unwrap())
                .unwrap()
                .into_raw()
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
                Ok(v) => CString::new(serde_json::to_string(&v).unwrap())
                    .unwrap()
                    .into_raw(),
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
            use maho_types::chat::{ChatRequestContext, ChatRequestMode};
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

use maho_agent::{AgentRuntime, MutexAgentStorage, SessionRuntime};
use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex};

use crate::agent::callback_lease::UnifiedEventCallbackRegistration;
use crate::agent::session::{
    load_runtime_config_from_store, persist_runtime_config_to_store, AgentRuntimeConfig,
};

const AGENT_SKILL_INJECT_BUDGET_CHARS: usize = 2_000;

const fn credential_error_envelope(error: maho_agent::CredentialError) -> &'static str {
    match error {
        maho_agent::CredentialError::ProviderNotConfigured => {
            r#"{"version":1,"kind":"credential_error","code":"provider_not_configured"}"#
        }
        maho_agent::CredentialError::CredentialUnusable => {
            r#"{"version":1,"kind":"credential_error","code":"credential_unusable"}"#
        }
        maho_agent::CredentialError::SecureStoreUnavailable => {
            r#"{"version":1,"kind":"credential_error","code":"secure_store_unavailable"}"#
        }
        maho_agent::CredentialError::CredentialDecryptFailed => {
            r#"{"version":1,"kind":"credential_error","code":"credential_decrypt_failed"}"#
        }
        maho_agent::CredentialError::ManagedAuthUnavailable => {
            r#"{"version":1,"kind":"credential_error","code":"managed_auth_unavailable"}"#
        }
        maho_agent::CredentialError::UnsupportedProvider => {
            r#"{"version":1,"kind":"credential_error","code":"unsupported_provider"}"#
        }
    }
}

fn agent_error_callback_text(error: &maho_agent::AgentError) -> Cow<'static, str> {
    match error {
        maho_agent::AgentError::Credential(credential_error) => {
            Cow::Borrowed(credential_error_envelope(*credential_error))
        }
        _ => Cow::Owned(error.to_string()),
    }
}

pub struct MahoAgentSession {
    pub runtime: Arc<dyn AgentRuntime>,
    isolation: Arc<SessionRuntime>,
    callback_lease: Option<Arc<SessionCallbackLease>>,
    accepted_leased_turns: Arc<AcceptedLeasedTurns>,
    permission_callback: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            tool_name: *const c_char,
            arguments_json: *const c_char,
        ) -> MahoAgentPermissionDecision,
    >,
    browser_tool_callback: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            tool_name: *const c_char,
            args_json: *const c_char,
        ) -> MahoAgentToolResult,
    >,
    session_id: String,
    active_conversation_registry: Arc<Mutex<std::collections::HashMap<String, usize>>>,
    core: std::ptr::NonNull<MahoCore>,
    workspace_root: std::path::PathBuf,
    active_space_id: Option<String>,
    // Artifact-created delivery: installed post-construction via
    // `maho_agent_set_artifact_created_callback` (ABI-stable setter, not a
    // create/send param). Read during turn setup to add the ArtifactCreated
    // event arm. `user_data` stored as usize (raw pointer is not Send).
    artifact_created_cb: Option<
        unsafe extern "C" fn(user_data: *mut std::ffi::c_void, artifact_json: *const c_char),
    >,
    artifact_created_user_data: usize,
    // Unified runtime-event plumbing (agent::events FFI seam). These MUST live
    // on this struct — the only `MahoAgentSession` the exported create/send
    // symbols construct — so every exported session-taking symbol locks the
    // same field at the same offset. Before the F3-R4 unification these fields
    // existed only on a second, module-local struct definition with a
    // different layout, and the event-side FFI functions locked String/fn-
    // pointer bytes of this struct as pthread mutexes (EINVAL -> panic=abort).
    pub(crate) unified_event_registration: Arc<Mutex<Option<UnifiedEventCallbackRegistration>>>,
    pub turn_controller: Arc<Mutex<maho_agent::turn_control::SessionTurnController>>,
    pub(crate) wait_registry: Arc<Mutex<maho_agent::event_wait::WaitRegistry>>,
    pub(crate) event_seq_counter: Arc<AtomicU64>,
    pub(crate) notification_permission_granted: Arc<AtomicBool>,
    runtime_config: Mutex<AgentRuntimeConfig>,
}

struct ActiveConversationGuard {
    registry: Arc<Mutex<std::collections::HashMap<String, usize>>>,
    session_id: String,
}

impl ActiveConversationGuard {
    fn new(
        registry: Arc<Mutex<std::collections::HashMap<String, usize>>>,
        session_id: String,
    ) -> Self {
        let mut sessions = registry.lock().unwrap_or_else(|e| e.into_inner());
        *sessions.entry(session_id.clone()).or_insert(0) += 1;
        drop(sessions);
        Self {
            registry,
            session_id,
        }
    }
}

impl Drop for ActiveConversationGuard {
    fn drop(&mut self) {
        let mut sessions = self.registry.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(count) = sessions.get_mut(&self.session_id) {
            *count -= 1;
            if *count == 0 {
                sessions.remove(&self.session_id);
            }
        }
    }
}

fn apply_agent_skill_system_prompt(session: &MahoAgentSession, runtime: &dyn AgentRuntime) {
    // SAFETY: `MahoAgentSession` is only constructed by
    // `maho_agent_create_session`, which stores the non-null `MahoCore` pointer
    // supplied by the browser. The browser must keep that core alive for the
    // agent session lifetime.
    let core = unsafe { session.core.as_ref() };
    let trusted_origin = core.trusted_active_tab_origin();
    let workspace_root = session.workspace_root.to_string_lossy();
    let mut base_system_prompt = core.resolve_agent_base_system_prompt(
        &session.session_id,
        workspace_root.as_ref(),
        session.active_space_id.as_deref(),
    );
    if base_system_prompt.trim().is_empty() {
        base_system_prompt = maho_agent::system_prompt::MAHO_BROWSER_SYSTEM_PROMPT.to_string();
    }
    let system_prompt = core
        .compose_agent_system_prompt_with_skill_context(
            &base_system_prompt,
            trusted_origin.as_deref(),
            AGENT_SKILL_INJECT_BUDGET_CHARS,
        )
        .unwrap_or(base_system_prompt);
    runtime.set_system_prompt(&system_prompt);
}

// THREAD-SAFETY CONTRACT FOR user_data:
// In the Maho C-FFI boundary, `user_data` raw pointers (`*mut c_void` / `usize`) are passed
// alongside C function callbacks. By transferring these callbacks and their associated
// `user_data` across the FFI boundary into asynchronous Rust runtimes/threads:
// 1. The caller/embedding environment (Chromium / iOS / Android shell) guarantees that the
//    underlying object pointed to by `user_data` remains valid and is safe to access
//    concurrently or sequentially across threads for the active duration of the callback
//    lease (i.e. until the registered `on_release` callback is invoked or the session is freed).
// 2. The release callback (when provided) is guaranteed by the Rust runtime to be invoked
//    at most once after all active dispatches referencing `user_data` have finished and all
//    clones of the lease have dropped. If no release callback is provided (`None`), dropping
//    the lease safely skips invoking any callback.
// 3. To avoid blanket unsafety, `SendableCallback<T>` is strictly constrained to implement
//    `Send` and `Sync` only for concrete C function pointer wrapper types used in the agent FFI.

struct SendableCallback<T>(T);

unsafe impl Send for SendableCallback<MahoAgentReleaseCallback> {}
unsafe impl Sync for SendableCallback<MahoAgentReleaseCallback> {}

unsafe impl Send for SendableCallback<Option<unsafe extern "C" fn(*mut c_void, *const c_char)>> {}
unsafe impl Sync for SendableCallback<Option<unsafe extern "C" fn(*mut c_void, *const c_char)>> {}

unsafe impl Send for SendableCallback<unsafe extern "C" fn(*mut c_void, *const c_char)> {}
unsafe impl Sync for SendableCallback<unsafe extern "C" fn(*mut c_void, *const c_char)> {}

unsafe impl Send
    for SendableCallback<Option<unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char)>>
{
}
unsafe impl Sync
    for SendableCallback<Option<unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char)>>
{
}

unsafe impl Send
    for SendableCallback<
        Option<unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char, *const c_char)>,
    >
{
}
unsafe impl Sync
    for SendableCallback<
        Option<unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char, *const c_char)>,
    >
{
}

unsafe impl Send
    for SendableCallback<
        Option<
            unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char, *const c_char, bool),
        >,
    >
{
}
unsafe impl Sync
    for SendableCallback<
        Option<
            unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char, *const c_char, bool),
        >,
    >
{
}

unsafe impl Send
    for SendableCallback<
        unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char) -> MahoAgentToolResult,
    >
{
}
unsafe impl Sync
    for SendableCallback<
        unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char) -> MahoAgentToolResult,
    >
{
}

unsafe impl Send
    for SendableCallback<
        Option<
            unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char) -> MahoAgentToolResult,
        >,
    >
{
}
unsafe impl Sync
    for SendableCallback<
        Option<
            unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char) -> MahoAgentToolResult,
        >,
    >
{
}

unsafe impl Send for SendableCallback<MahoAgentPermissionCallback> {}
unsafe impl Sync for SendableCallback<MahoAgentPermissionCallback> {}

unsafe impl Send for SendableCallback<Option<MahoAgentPermissionCallback>> {}
unsafe impl Sync for SendableCallback<Option<MahoAgentPermissionCallback>> {}

unsafe impl Send for SendableCallback<MahoAgentSecureStorageCallback> {}
unsafe impl Sync for SendableCallback<MahoAgentSecureStorageCallback> {}

unsafe impl Send for SendableCallback<Option<MahoAgentSecureStorageCallback>> {}
unsafe impl Sync for SendableCallback<Option<MahoAgentSecureStorageCallback>> {}

struct SendableUserData(usize);
#[allow(dead_code)]
unsafe impl Send for SendableUserData {}
#[allow(dead_code)]
unsafe impl Sync for SendableUserData {}

pub use crate::agent::browser_bridge::MahoAgentToolResult;
use crate::agent::callback_lease::{
    clone_callback_lease, invoke_with_turn_callback_lease, AcceptedLeasedTurns,
    CallbackReleasePolicy, SessionCallbackLease, TurnCallbackLease,
};
pub use crate::agent::secure_storage::{MahoAgentPermissionDecision, MahoAgentSecureKey};

// F3-R4 single-source ABI types: the lease/decision/key/result types below are
// the canonical definitions in `agent::*`. The crate previously carried
// byte-identical DUPLICATE definitions here, and two parallel
// `MahoAgentSession` structs — the exported create path built one while the
// exported event symbols locked fields of the other at mismatched offsets
// (EINVAL "failed to lock mutex" -> panic=abort SIGABRT at agent turn start).
// Keep ONE definition per ABI type; alias or import, never re-define.
pub use crate::agent::callback_lease::MahoAgentReleaseCallback;
pub type MahoAgentPermissionCallback = crate::agent::secure_storage::MahoAgentPermissionCallback;
pub type MahoAgentSecureStorageCallback =
    crate::agent::secure_storage::MahoAgentSecureStorageCallback;

struct FfiBrowserToolBridge {
    cb: SendableCallback<
        unsafe extern "C" fn(
            *mut std::ffi::c_void,
            *const c_char,
            *const c_char,
        ) -> MahoAgentToolResult,
    >,
    user_data: SendableUserData,
    _session_callback_lease: Option<Arc<SessionCallbackLease>>,
    _turn_callback_lease: Option<Arc<TurnCallbackLease>>,
}

impl FfiBrowserToolBridge {
    fn call_browser_callback(
        &self,
        request: &str,
        args_json: &str,
    ) -> Result<String, maho_agent::BrowserToolBridgeError> {
        let request = CString::new(request).map_err(|error| {
            maho_agent::BrowserToolBridgeError::new(
                "invalid_request",
                format!("browser callback request contains NUL: {error}"),
                false,
            )
        })?;
        let args = CString::new(args_json).map_err(|error| {
            maho_agent::BrowserToolBridgeError::new(
                "invalid_arguments",
                format!("browser callback arguments contain NUL: {error}"),
                false,
            )
        })?;
        let callback = self.cb.0;
        let user_data = self.user_data.0 as *mut std::ffi::c_void;
        // SAFETY: the callback and user data remain owned by the session/turn
        // lease, and both C strings remain live for the duration of this call.
        let result = unsafe { callback(user_data, request.as_ptr(), args.as_ptr()) };

        let read_and_free = |pointer: *mut c_char| {
            if pointer.is_null() {
                return None;
            }
            // SAFETY: the embedder transfers one NUL-terminated allocation in
            // each non-null result field. Copy it before invoking its matching
            // deallocator exactly once.
            let value = unsafe { CStr::from_ptr(pointer) }
                .to_string_lossy()
                .into_owned();
            if let Some(free_fn) = result.free_fn {
                // SAFETY: `free_fn` is the deallocator supplied for this result.
                unsafe { free_fn(pointer) };
            }
            Some(value)
        };
        let json = read_and_free(result.json_ptr);
        let error = read_and_free(result.error_ptr);
        let error_code = read_and_free(result.error_code_ptr);

        match (json, error) {
            (_, Some(message)) => Err(maho_agent::BrowserToolBridgeError::new(
                error_code.as_deref().unwrap_or("callback_error"),
                message,
                error_code.is_none() || result.error_retryable,
            )),
            (Some(json), None) => Ok(json),
            (None, None) => Err(maho_agent::BrowserToolBridgeError::new(
                "bridge_offline",
                "browser callback returned no result",
                true,
            )),
        }
    }
}

fn decode_ffi_browser_descriptors(
    response_json: &str,
) -> Result<Vec<maho_agent::BrowserToolDescriptor>, maho_agent::BrowserToolBridgeError> {
    let response: serde_json::Value = serde_json::from_str(response_json).map_err(|error| {
        maho_agent::BrowserToolBridgeError::new(
            "malformed_discovery",
            format!("browser tools/list returned invalid JSON: {error}"),
            false,
        )
    })?;
    let tools = response.get("tools").cloned().ok_or_else(|| {
        maho_agent::BrowserToolBridgeError::new(
            "malformed_discovery",
            "browser tools/list response is missing typed tools",
            false,
        )
    })?;
    serde_json::from_value(tools).map_err(|error| {
        maho_agent::BrowserToolBridgeError::new(
            "malformed_discovery",
            format!("browser tools/list returned invalid descriptors: {error}"),
            false,
        )
    })
}

fn decode_ffi_browser_execution(
    capability_id: &str,
    response_json: &str,
) -> Result<maho_agent::BrowserToolExecution, maho_agent::BrowserToolBridgeError> {
    let result: serde_json::Value = serde_json::from_str(response_json).map_err(|error| {
        maho_agent::BrowserToolBridgeError::new(
            "malformed_execution",
            format!("browser execution returned invalid JSON: {error}"),
            false,
        )
    })?;
    let is_typed_envelope = result
        .as_object()
        .is_some_and(|object| object.contains_key("outputJson") || object.contains_key("receipt"));
    if is_typed_envelope {
        let execution = serde_json::from_value::<maho_agent::BrowserToolExecution>(result)
            .map_err(|error| {
                maho_agent::BrowserToolBridgeError::new(
                    "malformed_execution",
                    format!("browser execution returned an invalid typed receipt: {error}"),
                    false,
                )
            })?;
        if execution.receipt.capability_id != capability_id {
            return Err(maho_agent::BrowserToolBridgeError::new(
                "receipt_mismatch",
                "browser execution receipt capability does not match the requested capability",
                false,
            ));
        }
        if execution.receipt.execution_id.trim().is_empty() {
            return Err(maho_agent::BrowserToolBridgeError::new(
                "malformed_execution",
                "browser execution receipt is missing an execution ID",
                false,
            ));
        }
        return Ok(execution);
    }

    let output_json = serde_json::to_string(&result).map_err(|error| {
        maho_agent::BrowserToolBridgeError::new(
            "malformed_execution",
            format!("browser execution result could not be serialized: {error}"),
            false,
        )
    })?;
    Ok(maho_agent::BrowserToolExecution {
        output_json,
        receipt: maho_agent::BrowserToolExecutionReceipt {
            capability_id: capability_id.to_string(),
            execution_id: uuid::Uuid::new_v4().to_string(),
            metadata: serde_json::json!({"source": "maho-desktop-ffi-legacy-result"}),
        },
    })
}

#[async_trait::async_trait]
impl maho_agent::BrowserToolBridge for FfiBrowserToolBridge {
    async fn list_tool_descriptors(
        &self,
    ) -> Result<Vec<maho_agent::BrowserToolDescriptor>, maho_agent::BrowserToolBridgeError> {
        let response = self.call_browser_callback("tools/list", "{}")?;
        decode_ffi_browser_descriptors(&response)
    }

    async fn execute_tool(
        &self,
        capability_id: &str,
        args_json: &str,
    ) -> Result<maho_agent::BrowserToolExecution, maho_agent::BrowserToolBridgeError> {
        serde_json::from_str::<serde_json::Value>(args_json).map_err(|error| {
            maho_agent::BrowserToolBridgeError::new(
                "invalid_arguments",
                format!("browser tool arguments are not valid JSON: {error}"),
                false,
            )
        })?;
        let response = self.call_browser_callback(capability_id, args_json)?;
        decode_ffi_browser_execution(capability_id, &response)
    }
}

#[cfg(test)]
mod ffi_browser_tool_bridge_tests {
    use super::*;
    use maho_agent::BrowserToolBridge;
    use std::collections::VecDeque;

    #[derive(Default)]
    struct FakeBrowserCallback {
        responses: std::sync::Mutex<VecDeque<Result<String, String>>>,
        calls: std::sync::Mutex<Vec<(String, String)>>,
    }

    unsafe extern "C" fn free_fake_result(pointer: *mut c_char) {
        if !pointer.is_null() {
            // SAFETY: every fake result pointer is created by CString::into_raw
            // below and returned to the bridge exactly once.
            drop(unsafe { CString::from_raw(pointer) });
        }
    }

    unsafe extern "C" fn fake_browser_callback(
        user_data: *mut c_void,
        request: *const c_char,
        args_json: *const c_char,
    ) -> MahoAgentToolResult {
        if user_data.is_null() || request.is_null() || args_json.is_null() {
            return MahoAgentToolResult {
                json_ptr: ptr::null_mut(),
                error_ptr: ptr::null_mut(),
                free_fn: Some(free_fake_result),
                error_code_ptr: ptr::null_mut(),
                error_retryable: false,
            };
        }
        // SAFETY: the test bridge passes this context and live C strings.
        let context = unsafe { &*(user_data as *const FakeBrowserCallback) };
        let request = unsafe { CStr::from_ptr(request) }
            .to_string_lossy()
            .into_owned();
        let args_json = unsafe { CStr::from_ptr(args_json) }
            .to_string_lossy()
            .into_owned();
        context
            .calls
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .push((request, args_json));
        let response = context
            .responses
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .pop_front();
        let mut result = MahoAgentToolResult {
            json_ptr: ptr::null_mut(),
            error_ptr: ptr::null_mut(),
            free_fn: Some(free_fake_result),
            error_code_ptr: ptr::null_mut(),
            error_retryable: false,
        };
        match response {
            Some(Ok(json)) => {
                result.json_ptr = CString::new(json)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut());
            }
            Some(Err(error)) => {
                result.error_ptr = CString::new(error)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut());
            }
            None => {}
        }
        result
    }

    fn bridge_with_responses(
        responses: impl IntoIterator<Item = Result<serde_json::Value, String>>,
    ) -> (FfiBrowserToolBridge, Box<FakeBrowserCallback>) {
        let context = Box::new(FakeBrowserCallback {
            responses: std::sync::Mutex::new(
                responses
                    .into_iter()
                    .map(|response| response.map(|value| value.to_string()))
                    .collect(),
            ),
            calls: std::sync::Mutex::new(Vec::new()),
        });
        let bridge = FfiBrowserToolBridge {
            cb: SendableCallback(fake_browser_callback),
            user_data: SendableUserData((&*context as *const FakeBrowserCallback) as usize),
            _session_callback_lease: None,
            _turn_callback_lease: None,
        };
        (bridge, context)
    }

    fn descriptor(capability_id: &str, name: &str) -> serde_json::Value {
        serde_json::json!({
            "capabilityId": capability_id,
            "name": name,
            "description": format!("Desktop {name}"),
            "inputSchema": {"type": "object", "additionalProperties": false},
            "schemaVersion": 1,
            "policy": {"sensitive": false, "permission": "auto_approve"}
        })
    }

    #[tokio::test]
    async fn desktop_discovery_requests_tools_list_and_decodes_two_descriptors() {
        let (bridge, context) = bridge_with_responses([Ok(serde_json::json!({
            "tools": [
                descriptor("browser.tabs.list", "browser_list_tabs"),
                descriptor("browser.page.read", "browser_read_page")
            ]
        }))]);

        let descriptors = bridge
            .list_tool_descriptors()
            .await
            .expect("typed discovery succeeds");
        assert_eq!(descriptors.len(), 2);
        assert_eq!(descriptors[0].capability_id, "browser.tabs.list");
        assert_eq!(descriptors[1].name, "browser_read_page");
        assert_eq!(
            *context
                .calls
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner),
            vec![("tools/list".to_string(), "{}".to_string())]
        );
    }

    #[tokio::test]
    async fn desktop_discovery_fails_closed_on_malformed_or_missing_metadata() {
        for malformed in [
            serde_json::json!({"tools": [descriptor("browser.tabs.list", "browser_list_tabs"), {"name": "missing_metadata"}]}),
            serde_json::json!({"notTools": []}),
        ] {
            let (bridge, _context) = bridge_with_responses([Ok(malformed)]);
            let error = bridge
                .list_tool_descriptors()
                .await
                .expect_err("malformed discovery is rejected");
            assert_eq!(error.code, "malformed_discovery");
            assert!(!error.retryable);
        }
    }

    #[tokio::test]
    async fn desktop_execution_uses_canonical_capability_id_and_preserves_typed_receipt() {
        let (bridge, context) = bridge_with_responses([Ok(serde_json::json!({
            "outputJson": "{\"ok\":true}",
            "receipt": {
                "capabilityId": "browser.tabs.list",
                "executionId": "desktop-execution-1",
                "metadata": {"source": "desktop"}
            }
        }))]);

        let execution = bridge
            .execute_tool("browser.tabs.list", r#"{"windowId":7}"#)
            .await
            .expect("typed execution succeeds");
        assert_eq!(execution.receipt.execution_id, "desktop-execution-1");
        assert_eq!(
            *context
                .calls
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner),
            vec![(
                "browser.tabs.list".to_string(),
                r#"{"windowId":7}"#.to_string()
            )]
        );
    }

    #[tokio::test]
    async fn desktop_execution_rejects_receipt_mismatch_and_malformed_typed_metadata() {
        for (response, expected_code) in [
            (
                serde_json::json!({
                    "outputJson": "{}",
                    "receipt": {
                        "capabilityId": "browser.page.read",
                        "executionId": "wrong-capability",
                        "metadata": {}
                    }
                }),
                "receipt_mismatch",
            ),
            (
                serde_json::json!({
                    "outputJson": "{}",
                    "receipt": {
                        "capabilityId": "browser.tabs.list",
                        "executionId": "missing-metadata"
                    }
                }),
                "malformed_execution",
            ),
        ] {
            let (bridge, _context) = bridge_with_responses([Ok(response)]);
            let error = bridge
                .execute_tool("browser.tabs.list", "{}")
                .await
                .expect_err("invalid typed receipt is rejected");
            assert_eq!(error.code, expected_code);
        }
    }

    #[tokio::test]
    async fn desktop_execution_wraps_legacy_raw_result_with_safe_boundary_receipt() {
        let (bridge, _context) =
            bridge_with_responses([Ok(serde_json::json!({"tabs": [{"id": 7}]}))]);
        let execution = bridge
            .execute_tool("browser.tabs.list", "{}")
            .await
            .expect("legacy result remains compatible");

        assert_eq!(execution.receipt.capability_id, "browser.tabs.list");
        assert!(!execution.receipt.execution_id.is_empty());
        assert_eq!(
            execution.receipt.metadata,
            serde_json::json!({"source": "maho-desktop-ffi-legacy-result"})
        );
        assert_eq!(execution.output_json, r#"{"tabs":[{"id":7}]}"#);
    }

    static TYPED_ERROR_FREE_COUNT: std::sync::atomic::AtomicUsize =
        std::sync::atomic::AtomicUsize::new(0);

    unsafe extern "C" fn free_typed_error_result(pointer: *mut c_char) {
        if !pointer.is_null() {
            TYPED_ERROR_FREE_COUNT.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
            // SAFETY: each field is independently allocated with CString::into_raw.
            drop(unsafe { CString::from_raw(pointer) });
        }
    }

    unsafe extern "C" fn typed_policy_error_browser_callback(
        _user_data: *mut c_void,
        _request: *const c_char,
        _args_json: *const c_char,
    ) -> MahoAgentToolResult {
        MahoAgentToolResult {
            json_ptr: ptr::null_mut(),
            error_ptr: CString::new("Browser action denied by approval policy")
                .expect("test error contains no NUL")
                .into_raw(),
            free_fn: Some(free_typed_error_result),
            error_code_ptr: CString::new("policy_denied")
                .expect("test code contains no NUL")
                .into_raw(),
            error_retryable: false,
        }
    }

    unsafe extern "C" fn typed_transport_error_browser_callback(
        _user_data: *mut c_void,
        _request: *const c_char,
        _args_json: *const c_char,
    ) -> MahoAgentToolResult {
        MahoAgentToolResult {
            json_ptr: ptr::null_mut(),
            error_ptr: CString::new("Browser callback transport is unavailable")
                .expect("test error contains no NUL")
                .into_raw(),
            free_fn: Some(free_typed_error_result),
            error_code_ptr: CString::new("transport_unavailable")
                .expect("test code contains no NUL")
                .into_raw(),
            error_retryable: true,
        }
    }

    #[tokio::test]
    async fn desktop_callback_preserves_typed_error_kind_and_frees_every_field() {
        for (callback, expected_code, expected_retryable) in [
            (
                typed_policy_error_browser_callback
                    as unsafe extern "C" fn(
                        *mut c_void,
                        *const c_char,
                        *const c_char,
                    ) -> MahoAgentToolResult,
                "policy_denied",
                false,
            ),
            (
                typed_transport_error_browser_callback
                    as unsafe extern "C" fn(
                        *mut c_void,
                        *const c_char,
                        *const c_char,
                    ) -> MahoAgentToolResult,
                "transport_unavailable",
                true,
            ),
        ] {
            TYPED_ERROR_FREE_COUNT.store(0, std::sync::atomic::Ordering::SeqCst);
            let bridge = FfiBrowserToolBridge {
                cb: SendableCallback(callback),
                user_data: SendableUserData(0),
                _session_callback_lease: None,
                _turn_callback_lease: None,
            };

            let error = bridge
                .execute_tool("browser.click", "{}")
                .await
                .expect_err("typed callback error propagates");
            assert_eq!(error.code, expected_code);
            assert_eq!(error.retryable, expected_retryable);
            assert_eq!(
                TYPED_ERROR_FREE_COUNT.load(std::sync::atomic::Ordering::SeqCst),
                2,
                "message and code allocations must each be freed exactly once"
            );
        }
    }

    #[tokio::test]
    async fn desktop_callback_error_and_empty_result_fail_closed() {
        let (error_bridge, _context) =
            bridge_with_responses([Err("desktop callback unavailable".to_string())]);
        let error = error_bridge
            .list_tool_descriptors()
            .await
            .expect_err("callback error propagates");
        assert_eq!(error.code, "callback_error");
        assert!(error.retryable);

        let (offline_bridge, _context) = bridge_with_responses([]);
        let error = offline_bridge
            .list_tool_descriptors()
            .await
            .expect_err("empty callback result is offline");
        assert_eq!(error.code, "bridge_offline");
        assert!(error.retryable);
    }

    #[test]
    fn missing_desktop_callback_installs_no_browser_bridge() {
        assert!(leased_browser_tool_bridge(None, ptr::null_mut(), &None).is_none());
    }
}

fn leased_permission_callback(
    callback: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            tool_name: *const c_char,
            arguments_json: *const c_char,
        ) -> MahoAgentPermissionDecision,
    >,
    callback_user_data: *mut std::ffi::c_void,
    turn_callback_lease: &Option<Arc<TurnCallbackLease>>,
) -> Option<maho_agent::PermissionCallback> {
    callback.map(|callback| {
        let callback = SendableCallback(callback);
        let callback_user_data = SendableUserData(callback_user_data as usize);
        let turn_callback_lease = clone_callback_lease(turn_callback_lease);
        Box::new(move |request: maho_agent::PermissionRequest| {
            let _ = &turn_callback_lease;
            let decision = match (
                CString::new(request.tool_name),
                CString::new(request.arguments),
            ) {
                (Ok(tool_name), Ok(arguments)) => {
                    // SAFETY: the leased FFI turn owns `callback_user_data` through
                    // `turn_callback_lease`, and C-string arguments remain live for this call.
                    unsafe {
                        (callback.0)(
                            callback_user_data.0 as *mut std::ffi::c_void,
                            tool_name.as_ptr(),
                            arguments.as_ptr(),
                        )
                    }
                }
                _ => MahoAgentPermissionDecision::Deny,
            };
            let decision = match decision {
                MahoAgentPermissionDecision::Allow => maho_agent::PermissionDecision::Allow,
                MahoAgentPermissionDecision::Deny => maho_agent::PermissionDecision::Deny,
            };
            Box::pin(async move { decision })
                as std::pin::Pin<
                    Box<dyn std::future::Future<Output = maho_agent::PermissionDecision> + Send>,
                >
        }) as maho_agent::PermissionCallback
    })
}

fn leased_browser_tool_bridge(
    callback: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            tool_name: *const c_char,
            args_json: *const c_char,
        ) -> MahoAgentToolResult,
    >,
    callback_user_data: *mut std::ffi::c_void,
    turn_callback_lease: &Option<Arc<TurnCallbackLease>>,
) -> Option<Arc<dyn maho_agent::BrowserToolBridge>> {
    callback.map(|callback| {
        Arc::new(FfiBrowserToolBridge {
            cb: SendableCallback(callback),
            user_data: SendableUserData(callback_user_data as usize),
            _session_callback_lease: None,
            _turn_callback_lease: clone_callback_lease(turn_callback_lease),
        }) as Arc<dyn maho_agent::BrowserToolBridge>
    })
}

unsafe fn maho_agent_create_session_impl(
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
    callback_release_policy: Option<CallbackReleasePolicy>,
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
                tracing::debug!(
                    "[maho_agent_create_session] FAIL: import_gate is active (data import in progress)"
                );
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

            let workspace_path = if workspace_root.is_null() {
                let cwd = std::env::current_dir().unwrap_or_else(|_| std::path::PathBuf::from("."));
                if cwd == std::path::Path::new("/") || cwd.as_os_str().is_empty() {
                    std::path::Path::new(db_path)
                        .parent()
                        .map(|p| p.to_path_buf())
                        .unwrap_or_else(|| {
                            std::env::var("HOME").map(std::path::PathBuf::from).unwrap_or(cwd)
                        })
                } else {
                    cwd
                }
            } else {
                match CStr::from_ptr(workspace_root).to_str() {
                    Ok(s) => std::path::PathBuf::from(s),
                    Err(_) => {
                        std::env::current_dir().unwrap_or_else(|_| std::path::PathBuf::from("."))
                    }
                }
            };
            let session_workspace_root = workspace_path.clone();

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

            // Wave 1A session-open load: restore the persisted runtime-config
            // row into the initial Mutex value. Absent row or storage failure
            // falls back to the plumbing defaults (never blocks session open).
            let runtime_config = load_runtime_config_from_store(&storage, &sid);

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

            let agent_storage = MutexAgentStorage(Arc::new(Mutex::new(storage)));
            let callback_lease = callback_release_policy
                .map(|policy| SessionCallbackLease::new(Some(policy.callback), policy.user_data));

            let session_permission_callback = permission_cb;
            let permission_callback = session_permission_callback.map(|cb| {
                let user_data_ptr = SendableUserData(permission_user_data as usize);
                let cb_sendable = SendableCallback(cb);
                let callback_lease = clone_callback_lease(&callback_lease);
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
                    let _ = &callback_lease;
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

            let runtime = maho_agent::omo::factory::create_agent_runtime(
                Arc::new(agent_storage),
                permission_callback,
                workspace_path.clone(),
                allow_insecure_key_storage,
            );
            // Automatically attach durable session journal with disk write-through persistence
            runtime.attach_session_journal(sid.as_str(), &workspace_path);

            // Wave 1D panel-side tier binding: bind the loaded (or fail-closed
            // default "guard") tier plus the R-N2 single-source whitelist
            // roots so panel sessions enforce the same kernel dispatch gate
            // the CLI binds, keeping cross-surface verdicts identical (SC8).
            // Matches the broker's documented "guard" default.
            runtime.set_runtime_tier(Some(runtime_config.permission_tier.clone()));
            runtime.set_fs_whitelist_roots(maho_agent::permission::default_fs_whitelist_roots(
                &session_workspace_root,
            ));

            if let Some(cb) = secure_storage_cb {
                let user_data_ptr = SendableUserData(secure_storage_user_data as usize);
                let cb_sendable = SendableCallback(cb);
                let callback_lease = clone_callback_lease(&callback_lease);
                let callback = Box::new(move |provider: &str| {
                    let _ = &callback_lease;
                    let cb = cb_sendable.0;
                    let user_data = user_data_ptr.0 as *mut std::ffi::c_void;
                    let provider_c = match CString::new(provider) {
                        Ok(s) => s,
                        Err(_) => {
                            return Err(maho_agent::AgentError::ExecutionError(
                                "Invalid provider name".to_string(),
                            ));
                        }
                    };
                    let secure_key = unsafe { cb(user_data, provider_c.as_ptr()) };
                    if secure_key.ptr.is_null() {
                        return Err(maho_agent::CredentialError::ProviderNotConfigured.into());
                    }
                    if secure_key.len == 0 {
                        if let Some(free_fn) = secure_key.free_fn {
                            // SAFETY: the secure-storage callback transferred this non-null
                            // allocation and its declared length to the FFI adapter.
                            unsafe { free_fn(secure_key.ptr, secure_key.len) };
                        }
                        return Err(maho_agent::CredentialError::CredentialUnusable.into());
                    }
                    let key_slice = unsafe {
                        std::slice::from_raw_parts(secure_key.ptr as *const u8, secure_key.len)
                    };
                    let key_bytes = key_slice.to_vec();

                    unsafe {
                        std::ptr::write_bytes(secure_key.ptr, 0, secure_key.len);
                    }

                    if let Some(free_fn) = secure_key.free_fn {
                        unsafe {
                            free_fn(secure_key.ptr, secure_key.len);
                        }
                    }

                    let key_str = match String::from_utf8(key_bytes) {
                        Ok(s) => s,
                        Err(error) => {
                            let mut bytes = error.into_bytes();
                            bytes.zeroize();
                            return Err(maho_agent::CredentialError::CredentialDecryptFailed.into());
                        }
                    };

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

            let session_browser_tool_callback = browser_tool_cb;
            if let Some(cb) = session_browser_tool_callback {
                let user_data_ptr = SendableUserData(browser_tool_user_data as usize);
                let cb_sendable = SendableCallback(cb);
                let bridge: std::sync::Arc<dyn maho_agent::BrowserToolBridge> =
                    std::sync::Arc::new(FfiBrowserToolBridge {
                        cb: cb_sendable,
                        user_data: user_data_ptr,
                        _session_callback_lease: clone_callback_lease(&callback_lease),
                        _turn_callback_lease: None,
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

            core_ref.mark_conversation_session_active(&sid);
            let session = Box::new(MahoAgentSession {
                runtime,
                isolation,
                callback_lease,
                accepted_leased_turns: Arc::new(AcceptedLeasedTurns {
                    current: Mutex::new(None),
                }),
                permission_callback: session_permission_callback,
                browser_tool_callback: session_browser_tool_callback,
                session_id: sid.clone(),
                active_conversation_registry: core_ref.active_conversation_registry(),
                core: core_ptr,
                workspace_root: session_workspace_root,
                active_space_id,
                artifact_created_cb: None,
                artifact_created_user_data: 0,
                unified_event_registration: Arc::new(Mutex::new(None)),
                turn_controller: Arc::new(Mutex::new(
                    maho_agent::turn_control::SessionTurnController::new(&sid),
                )),
                wait_registry: Arc::new(Mutex::new(maho_agent::event_wait::WaitRegistry::new())),
                event_seq_counter: Arc::new(AtomicU64::new(1)),
                notification_permission_granted: Arc::new(AtomicBool::new(true)),
                runtime_config: Mutex::new(runtime_config),
            });
            if let Some(callback_lease) = session.callback_lease.as_ref() {
                callback_lease.arm();
            }
            Box::into_raw(session)
        },
        std::ptr::null_mut()
    )
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
    // SAFETY: this legacy ABI forwards its validated caller contract without a lease policy.
    unsafe {
        maho_agent_create_session_impl(
            core,
            session_id,
            workspace_root,
            allow_insecure_key_storage,
            permission_cb,
            permission_user_data,
            secure_storage_cb,
            secure_storage_user_data,
            browser_tool_cb,
            browser_tool_user_data,
            space_id,
            None,
        )
    }
}

/// # Safety
/// Same pointer requirements as `maho_agent_create_session`. When this function returns a
/// non-null session, `on_session_release` receives `session_release_user_data` exactly once after
/// the session and every runtime-held callback closure release their ownership. The callback must
/// not unwind.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_create_session_leased(
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
    on_session_release: MahoAgentReleaseCallback,
    session_release_user_data: *mut c_void,
) -> *mut MahoAgentSession {
    // SAFETY: the leased ABI transfers the dedicated release context only after the private
    // implementation accepts the session and creates its session callback lease.
    unsafe {
        maho_agent_create_session_impl(
            core,
            session_id,
            workspace_root,
            allow_insecure_key_storage,
            permission_cb,
            permission_user_data,
            secure_storage_cb,
            secure_storage_user_data,
            browser_tool_cb,
            browser_tool_user_data,
            space_id,
            Some(CallbackReleasePolicy {
                callback: on_session_release,
                user_data: session_release_user_data,
            }),
        )
    }
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_session_free(session: *mut MahoAgentSession) {
    ffi_safe!(
        {
            if !session.is_null() {
                let session_ref = &*session;
                session_ref.accepted_leased_turns.signal_current();
                // SAFETY: the core pointer is required to outlive every agent session.
                unsafe { session_ref.core.as_ref() }
                    .mark_conversation_session_inactive(&session_ref.session_id);
                let runtime = Arc::clone(&session_ref.runtime);
                let session_id = session_ref.session_id.clone();
                let isolation = Arc::clone(&session_ref.isolation);
                let _ = isolation.block_on(async move { runtime.cancel(&session_id).await });
                drop(Box::from_raw(session));
            }
        },
        ()
    )
}

unsafe fn maho_agent_send_message_impl(
    session: *mut MahoAgentSession,
    message: *const c_char,
    on_token: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, token: *const c_char)>,
    on_thinking: Option<
        unsafe extern "C" fn(user_data: *mut std::ffi::c_void, thinking: *const c_char),
    >,
    on_tool_call: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            id: *const c_char,
            name: *const c_char,
            args: *const c_char,
        ),
    >,
    on_tool_result: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            id: *const c_char,
            name: *const c_char,
            result: *const c_char,
            success: bool,
        ),
    >,
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            full_text: *const c_char,
            tool_calls_json: *const c_char,
        ),
    >,
    on_error: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, error: *const c_char)>,
    user_data: *mut std::ffi::c_void,
    callback_release_policy: Option<CallbackReleasePolicy>,
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
            let turn_callback_lease = callback_release_policy
                .map(|policy| Arc::new(TurnCallbackLease::new(policy.callback, policy.user_data)));
            let session_callback_lease = clone_callback_lease(&session.callback_lease);

            let on_token_cb = on_token.map(|cb| {
                let cb_sendable = SendableCallback(cb);
                let user_data_val = SendableUserData(user_data as usize);
                let turn_callback_lease = clone_callback_lease(&turn_callback_lease);
                let session_callback_lease = clone_callback_lease(&session_callback_lease);
                let callback: Box<dyn Fn(&str) + Send + Sync> = Box::new(move |token| {
                    let _ = (&turn_callback_lease, &session_callback_lease);
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

            let on_event_cb = if on_tool_call.is_some()
                || on_tool_result.is_some()
                || on_thinking.is_some()
                || session.artifact_created_cb.is_some()
            {
                let on_tool_call_sendable = SendableCallback(on_tool_call);
                let on_tool_result_sendable = SendableCallback(on_tool_result);
                let on_thinking_sendable = SendableCallback(on_thinking);
                let artifact_cb_sendable = SendableCallback(session.artifact_created_cb);
                let artifact_udata = SendableUserData(session.artifact_created_user_data);
                let user_data_val = SendableUserData(user_data as usize);
                let turn_callback_lease = clone_callback_lease(&turn_callback_lease);
                let session_callback_lease = clone_callback_lease(&session_callback_lease);
                let callback = move |event: maho_agent::AgentStreamEvent| {
                    let _ = (&turn_callback_lease, &session_callback_lease);
                    let udata = user_data_val.0 as *mut std::ffi::c_void;
                    match event {
                        maho_agent::AgentStreamEvent::Thinking(thinking) => {
                            if let Some(cb) = on_thinking_sendable.0 {
                                if let Ok(thinking_c) = CString::new(thinking) {
                                    unsafe {
                                        cb(udata, thinking_c.as_ptr());
                                    }
                                }
                            }
                        }
                        maho_agent::AgentStreamEvent::ToolCall { id, name, args } => {
                            if let Some(cb) = on_tool_call_sendable.0 {
                                if let (Ok(id_c), Ok(name_c), Ok(args_c)) =
                                    (CString::new(id), CString::new(name), CString::new(args))
                                {
                                    unsafe {
                                        cb(udata, id_c.as_ptr(), name_c.as_ptr(), args_c.as_ptr());
                                    }
                                }
                            }
                        }
                        maho_agent::AgentStreamEvent::ToolResult {
                            id,
                            name,
                            result,
                            succeeded,
                        } => {
                            if let Some(cb) = on_tool_result_sendable.0 {
                                if let (Ok(id_c), Ok(name_c), Ok(result_c)) =
                                    (CString::new(id), CString::new(name), CString::new(result))
                                {
                                    unsafe {
                                        cb(
                                            udata,
                                            id_c.as_ptr(),
                                            name_c.as_ptr(),
                                            result_c.as_ptr(),
                                            succeeded,
                                        );
                                    }
                                }
                            }
                        }
                        maho_agent::AgentStreamEvent::ArtifactCreated { artifact } => {
                            if let Some(cb) = artifact_cb_sendable.0 {
                                let audata = artifact_udata.0 as *mut std::ffi::c_void;
                                if let Ok(json) = serde_json::to_string(&artifact) {
                                    if let Ok(json_c) = CString::new(json) {
                                        unsafe {
                                            cb(audata, json_c.as_ptr());
                                        }
                                    }
                                }
                            }
                        }
                        _ => {}
                    }
                };
                Some(Arc::new(callback)
                    as Arc<
                        dyn Fn(maho_agent::AgentStreamEvent) + Send + Sync + 'static,
                    >)
            } else {
                None
            };

            let on_complete = SendableCallback(on_complete);
            let on_error = SendableCallback(on_error);
            let user_data_val = SendableUserData(user_data as usize);

            if let Some(callback_lease) = turn_callback_lease.as_ref() {
                callback_lease.arm();
            }

            let active_guard = ActiveConversationGuard::new(
                Arc::clone(&session.active_conversation_registry),
                session_id.clone(),
            );

            let fut = async move {
                let _active_guard = active_guard;
                let user_chat_msg = maho_types::chat::ChatMessage::user(
                    maho_types::chat::ChatContent::text(msg_str),
                );
                let result = runtime
                    .run_turn(&session_id, user_chat_msg, on_token_cb, on_event_cb)
                    .await;
                invoke_with_turn_callback_lease(turn_callback_lease, || match result {
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
                        let error_text = agent_error_callback_text(&e);
                        let Some(err_c) = cstring_or_fallback(&error_text, "unknown error") else {
                            return;
                        };
                        if let Some(cb) = on_error.0 {
                            cb(user_data_val.0 as *mut std::ffi::c_void, err_c.as_ptr());
                        }
                    }
                });
                drop(on_complete);
                drop(on_error);
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
/// `message` must be a null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_send_message(
    session: *mut MahoAgentSession,
    message: *const c_char,
    on_token: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, token: *const c_char)>,
    on_thinking: Option<
        unsafe extern "C" fn(user_data: *mut std::ffi::c_void, thinking: *const c_char),
    >,
    on_tool_call: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            id: *const c_char,
            name: *const c_char,
            args: *const c_char,
        ),
    >,
    on_tool_result: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            id: *const c_char,
            name: *const c_char,
            result: *const c_char,
            success: bool,
        ),
    >,
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
    // SAFETY: this legacy ABI forwards its validated caller contract without a lease policy.
    unsafe {
        maho_agent_send_message_impl(
            session,
            message,
            on_token,
            on_thinking,
            on_tool_call,
            on_tool_result,
            on_complete,
            on_error,
            user_data,
            None,
        )
    }
}

/// # Safety
/// Same pointer requirements as `maho_agent_send_message`. When this function returns `true`,
/// `on_release` receives `release_user_data` exactly once after the terminal callback and every
/// turn callback closure are dropped. The callback must not unwind.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_send_message_leased(
    session: *mut MahoAgentSession,
    message: *const c_char,
    on_token: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, token: *const c_char)>,
    on_thinking: Option<
        unsafe extern "C" fn(user_data: *mut std::ffi::c_void, thinking: *const c_char),
    >,
    on_tool_call: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            id: *const c_char,
            name: *const c_char,
            args: *const c_char,
        ),
    >,
    on_tool_result: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            id: *const c_char,
            name: *const c_char,
            result: *const c_char,
            success: bool,
        ),
    >,
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            full_text: *const c_char,
            tool_calls_json: *const c_char,
        ),
    >,
    on_error: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, error: *const c_char)>,
    callback_user_data: *mut std::ffi::c_void,
    on_release: MahoAgentReleaseCallback,
    release_user_data: *mut c_void,
) -> bool {
    // SAFETY: the leased ABI transfers the dedicated release context only after the private
    // implementation accepts the turn and creates its turn callback lease.
    unsafe {
        maho_agent_send_message_impl(
            session,
            message,
            on_token,
            on_thinking,
            on_tool_call,
            on_tool_result,
            on_complete,
            on_error,
            callback_user_data,
            Some(CallbackReleasePolicy {
                callback: on_release,
                user_data: release_user_data,
            }),
        )
    }
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
    on_thinking: Option<
        unsafe extern "C" fn(user_data: *mut std::ffi::c_void, thinking: *const c_char),
    >,
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

            let on_thinking_sendable = SendableCallback(on_thinking);
            let on_event_cb = if on_thinking.is_some() {
                let user_data_val = SendableUserData(user_data as usize);
                let callback = move |event: maho_agent::AgentStreamEvent| {
                    let udata = user_data_val.0 as *mut std::ffi::c_void;
                    match event {
                        maho_agent::AgentStreamEvent::Thinking(thinking) => {
                            if let Some(cb) = on_thinking_sendable.0 {
                                if let Ok(thinking_c) = CString::new(thinking) {
                                    unsafe {
                                        cb(udata, thinking_c.as_ptr());
                                    }
                                }
                            }
                        }
                        _ => {}
                    }
                };
                Some(Arc::new(callback)
                    as Arc<
                        dyn Fn(maho_agent::AgentStreamEvent) + Send + Sync + 'static,
                    >)
            } else {
                None
            };

            let on_complete = SendableCallback(on_complete);
            let on_error = SendableCallback(on_error);
            let user_data_val = SendableUserData(user_data as usize);

            let active_guard = ActiveConversationGuard::new(
                Arc::clone(&session.active_conversation_registry),
                session_id.clone(),
            );

            let fut = async move {
                let _active_guard = active_guard;
                let user_chat_msg = maho_types::chat::ChatMessage::user(
                    maho_types::chat::ChatContent::text(msg_str),
                );
                match runtime
                    .run_turn_simple(&session_id, user_chat_msg, on_token_cb, on_event_cb)
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
                        let error_text = agent_error_callback_text(&e);
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
            let accepted_leased_turn = session.accepted_leased_turns.signal_current();
            let runtime = Arc::clone(&session.runtime);
            let session_id = session.session_id.clone();

            let isolation = Arc::clone(&session.isolation);
            let res = isolation.block_on(async move { runtime.cancel(&session_id).await });
            accepted_leased_turn || res.is_ok()
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
/// `permission_tier` must be null or a null-terminated UTF-8 C string
/// ("read_only", "guard", "full_access"); unknown values fail closed to
/// "guard". Plumbing only: no enforcement behavior changes in this row.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_set_runtime_config(
    session: *mut MahoAgentSession,
    permission_tier: *const c_char,
    final_confirm: bool,
    proactive_mode: bool,
) {
    ffi_safe!(
        {
            if session.is_null() {
                return;
            }
            let session = &*session;
            let raw_tier = if permission_tier.is_null() {
                ""
            } else {
                CStr::from_ptr(permission_tier).to_str().unwrap_or_default()
            };
            let persisted = {
                let mut config = session
                    .runtime_config
                    .lock()
                    .unwrap_or_else(|e| e.into_inner());
                config.permission_tier = AgentRuntimeConfig::normalize_tier(raw_tier);
                config.final_confirm = final_confirm;
                config.proactive_mode = proactive_mode;
                config.clone()
            };
            // Wave 1A write-through: persist the normalized triple keyed by
            // the FFI session id so a later session open restores it.
            // Best-effort (see `persist_runtime_config_to_store`): persistence
            // failure never breaks the setter.
            // SAFETY: `session.core` points to the live `MahoCore` that owns
            // the session (see the create-path SAFETY notes).
            let core_ref = unsafe { session.core.as_ref() };
            let db_path = core_ref.sqlite_db_path();
            persist_runtime_config_to_store(db_path, &session.session_id, &persisted);
            // Wave 1D panel-side tier binding (D9 live rebind): mirror the
            // CLI's kernel-gate wiring (maho-cli main.rs) so a mid-session
            // tier change takes effect on the next kernel tool call without
            // reopening the session. Roots come from the shared provisioning
            // source (plan R-N2) — never an ad-hoc list.
            session
                .runtime
                .set_runtime_tier(Some(persisted.permission_tier.clone()));
            session.runtime.set_fs_whitelist_roots(
                maho_agent::permission::default_fs_whitelist_roots(&session.workspace_root),
            );
        },
        ()
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_set_mail_authorization_state(
    session: *mut MahoAgentSession,
    feature_enabled: bool,
    helper_ready: bool,
    helper_starting: bool,
    read_allowed: bool,
) {
    ffi_safe!(
        {
            if session.is_null() {
                return;
            }
            let session = &*session;
            session
                .runtime
                .set_mail_authorization_state(maho_agent::MailAuthorizationState {
                    feature_enabled,
                    helper_ready,
                    helper_starting,
                    read_allowed,
                });
        },
        ()
    )
}

/// # Safety
/// `session` must be a valid pointer returned by a `maho_agent_create_session*`
/// function. `cb` (nullable) receives a JSON-serialized `ArtifactInfo` string
/// for each artifact created during a turn; `user_data` is passed back verbatim.
/// ABI-stable: callers that never install this still stream token/tool/complete
/// events unchanged (the artifact arm is only added when a callback is set).
#[no_mangle]
pub unsafe extern "C" fn maho_agent_set_artifact_created_callback(
    session: *mut MahoAgentSession,
    cb: Option<
        unsafe extern "C" fn(user_data: *mut std::ffi::c_void, artifact_json: *const c_char),
    >,
    user_data: *mut std::ffi::c_void,
) {
    ffi_safe!(
        {
            if session.is_null() {
                return;
            }
            let session = &mut *session;
            session.artifact_created_cb = cb;
            session.artifact_created_user_data = user_data as usize;
        },
        ()
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `provider` may be null to clear the explicit provider selection; otherwise it
/// must be a null-terminated UTF-8 string naming an allowlisted provider.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_session_set_preferred_provider(
    session: *mut MahoAgentSession,
    provider: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() {
                return false;
            }
            let provider = if provider.is_null() {
                None
            } else {
                match CStr::from_ptr(provider).to_str() {
                    Ok(s) => Some(s.to_owned()),
                    Err(_) => return false,
                }
            };
            let session = &*session;
            session.runtime.set_preferred_provider(provider);
            true
        },
        false
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
/// `session` must be a valid pointer returned by a `maho_agent_create_session*`
/// function. `path` must be a null-terminated UTF-8 string, or null to clear.
/// Sets the session's artifact storage root; the fs_write tool and artifact
/// index resolve relative paths against it.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_set_artifact_root(
    session: *mut MahoAgentSession,
    path: *const c_char,
) {
    ffi_safe!(
        {
            if session.is_null() {
                return;
            }
            let session = &*session;
            let root = if path.is_null() {
                None
            } else {
                match CStr::from_ptr(path).to_str() {
                    Ok(s) => Some(std::path::PathBuf::from(s)),
                    Err(_) => return,
                }
            };
            session.runtime.set_artifact_root(root);
        },
        ()
    )
}

/// # Safety
/// `session` must be a valid pointer returned by a `maho_agent_create_session*`
/// function. Returns a newly allocated JSON array of ArtifactInfo for the
/// session (caller frees via `maho_free_string`), or null on error.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_list_artifacts(session: *mut MahoAgentSession) -> *mut c_char {
    ffi_safe!(
        {
            if session.is_null() {
                return std::ptr::null_mut();
            }
            let session = &*session;
            let runtime = Arc::clone(&session.runtime);
            let session_id = session.session_id.clone();
            let isolation = Arc::clone(&session.isolation);
            let res = isolation.block_on(async move { runtime.list_artifacts(&session_id).await });
            let artifacts = match res {
                Ok(Ok(a)) => a,
                _ => return std::ptr::null_mut(),
            };
            match serde_json::to_string(&artifacts) {
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
/// `session` must be a valid pointer returned by a `maho_agent_create_session*`
/// function. `artifact_id` must be a null-terminated UTF-8 string. Returns a
/// newly allocated root-relative path string (caller frees via
/// `maho_free_string`), or null for an unknown id or error. Opaque ids cross
/// the boundary; the resolved path is only produced here.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_artifact_path(
    session: *mut MahoAgentSession,
    artifact_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if session.is_null() || artifact_id.is_null() {
                return std::ptr::null_mut();
            }
            let session = &*session;
            let artifact_id = match CStr::from_ptr(artifact_id).to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return std::ptr::null_mut(),
            };
            let runtime = Arc::clone(&session.runtime);
            let session_id = session.session_id.clone();
            let isolation = Arc::clone(&session.isolation);
            let res = isolation.block_on(async move {
                runtime
                    .artifact_storage_rel_path(&session_id, &artifact_id)
                    .await
            });
            match res {
                Ok(Ok(Some(path))) => CString::new(path)
                    .map(|c| c.into_raw())
                    .unwrap_or(std::ptr::null_mut()),
                _ => std::ptr::null_mut(),
            }
        },
        std::ptr::null_mut()
    )
}

#[path = "common.rs"]
pub(crate) mod common;

pub mod agent;

pub mod skills;

pub use crate::agent::events::{
    maho_agent_get_events_after, maho_agent_get_last_compaction_info,
    maho_agent_interaction_cancel, maho_agent_interaction_resolve, maho_agent_interaction_timeout,
    maho_agent_register_unified_event_callback, maho_agent_register_unified_event_callback_leased,
    maho_agent_resolve_model, maho_agent_turn_queue_depth, maho_agent_turn_submit,
    maho_agent_unregister_unified_event_callback, maho_agent_wait_notification_permission,
    maho_agent_wait_register, maho_agent_wait_wake, MahoAgentEventCallbackV2, MahoAgentEventKindV2,
    MahoAgentEventV2, MahoAgentUnifiedEventCallback, MahoAgentUnifiedEventEnvelope,
    MahoUnifiedAgentEventCallback, MahoUnifiedAgentEventEnvelope,
};

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

type RoutineStatusCb = unsafe extern "C" fn(*mut c_void, *const c_char);

#[derive(Clone)]
struct RoutineStatusCallback {
    token: u64,
    core: usize,
    user_data: usize,
    callback: RoutineStatusCb,
    lease: FfiArc<CallbackLease>,
}

static ROUTINE_RUN_REGISTRIES: OnceLock<
    StdMutex<HashMap<usize, FfiArc<maho_core::routine_runs::RoutineRunRegistry>>>,
> = OnceLock::new();
static ROUTINE_STATUS_CALLBACKS: OnceLock<StdMutex<Vec<RoutineStatusCallback>>> = OnceLock::new();

fn routine_status_callbacks() -> &'static StdMutex<Vec<RoutineStatusCallback>> {
    ROUTINE_STATUS_CALLBACKS.get_or_init(|| StdMutex::new(Vec::new()))
}

fn routine_run_registry(
    core: *mut MahoCore,
) -> FfiArc<maho_core::routine_runs::RoutineRunRegistry> {
    let key = core as usize;
    let registries = ROUTINE_RUN_REGISTRIES.get_or_init(|| StdMutex::new(HashMap::new()));
    let mut registries = registries
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner);
    if let Some(registry) = registries.get(&key) {
        return FfiArc::clone(registry);
    }
    let registry = FfiArc::new(maho_core::routine_runs::RoutineRunRegistry::default());
    registry.add_observer(FfiArc::new(move |status| {
        let json = match serde_json::to_string(status)
            .ok()
            .and_then(|json| CString::new(json).ok())
        {
            Some(json) => json,
            None => return,
        };
        let callbacks = routine_status_callbacks()
            .lock()
            .map(|callbacks| {
                callbacks
                    .iter()
                    .filter(|entry| entry.core == key)
                    .cloned()
                    .collect::<Vec<_>>()
            })
            .unwrap_or_default();
        for entry in callbacks {
            let Some(_invocation) = entry.lease.enter(entry.token) else {
                continue;
            };
            unsafe {
                (entry.callback)(entry.user_data as *mut c_void, json.as_ptr());
            }
        }
    }));
    registries.insert(key, FfiArc::clone(&registry));
    registry
}

fn remove_routine_run_registry(core: *mut MahoCore) {
    let key = core as usize;
    if let Some(registries) = ROUTINE_RUN_REGISTRIES.get() {
        registries
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .remove(&key);
    }
    let removed = routine_status_callbacks()
        .lock()
        .map(|mut callbacks| {
            let mut removed = Vec::new();
            let mut index = 0;
            while index < callbacks.len() {
                if callbacks[index].core == key {
                    removed.push(callbacks.remove(index));
                } else {
                    index += 1;
                }
            }
            removed
        })
        .unwrap_or_default();
    for entry in removed {
        entry.lease.remove_and_wait(entry.token);
    }
}

fn validate_routine_start(
    core: *mut MahoCore,
    id: &str,
    tier: maho_core::routines::UserTier,
) -> Result<String, String> {
    if tier != maho_core::routines::UserTier::Max {
        return Err("Routines are available on Max tier only".to_string());
    }
    let db_path = unsafe { &*core }
        .sqlite_db_path()
        .map(str::to_string)
        .ok_or_else(|| "Routine storage unavailable".to_string())?;
    let storage = maho_storage::sqlite::SqliteStorage::open(&db_path)
        .map_err(|error| format!("Failed to open storage: {error}"))?;
    let exists = maho_core::routines::list_all_routines(&storage)
        .map_err(|error| error.to_string())?
        .iter()
        .any(|routine| routine.id == id);
    if !exists {
        return Err(format!("No routine found with id: {id}"));
    }
    Ok(db_path)
}

fn spawn_tracked_routine(
    core: *mut MahoCore,
    id: String,
    tier: maho_core::routines::UserTier,
    source: maho_core::routines::RoutineRunSource,
    on_complete: SendableCallback<Option<unsafe extern "C" fn(*mut c_void, *const c_char)>>,
    on_error: SendableCallback<Option<unsafe extern "C" fn(*mut c_void, *const c_char)>>,
    user_data: SendableUserData,
) -> Result<maho_core::routine_runs::RoutineRunStatus, String> {
    let db_path = validate_routine_start(core, &id, tier)?;
    let registry = routine_run_registry(core);
    let queued = registry.begin(&id, source);
    let queued_for_worker = queued.clone();
    maho_core::memory_manager::get_runtime().spawn_blocking(move || {
        let rt = match tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
        {
            Ok(rt) => rt,
            Err(error) => {
                let message = format!("Failed to build runtime: {error}");
                let _ = registry.mark_setup_failed(&queued_for_worker.run_id, message.clone());
                if let Some(callback) = on_error.0 {
                    if let Ok(error) = CString::new(message) {
                        unsafe { callback(user_data.0 as *mut c_void, error.as_ptr()) };
                    }
                }
                return;
            }
        };
        rt.block_on(async move {
            let storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                Ok(storage) => storage,
                Err(error) => {
                    let message = format!("Failed to open storage: {error}");
                    let _ = registry.mark_setup_failed(&queued_for_worker.run_id, message.clone());
                    if let Some(callback) = on_error.0 {
                        if let Ok(error) = CString::new(message) {
                            unsafe { callback(user_data.0 as *mut c_void, error.as_ptr()) };
                        }
                    }
                    return;
                }
            };
            let agent_storage = MutexAgentStorage(Arc::new(Mutex::new(storage)));
            let approval_registry = FfiArc::clone(&registry);
            let approval_run_id = queued_for_worker.run_id.clone();
            let permission_callback: maho_agent::PermissionCallback = Box::new(move |request| {
                let registry = FfiArc::clone(&approval_registry);
                let run_id = approval_run_id.clone();
                Box::pin(async move {
                    if request.sensitivity != maho_agent::ToolSensitivity::Sensitive {
                        return maho_agent::PermissionDecision::Allow;
                    }
                    match registry.request_approval(
                        &run_id,
                        request.tool_name,
                        request.sensitivity.as_str().to_string(),
                    ) {
                        Ok(waiter) => {
                            if waiter.await {
                                maho_agent::PermissionDecision::Allow
                            } else {
                                maho_agent::PermissionDecision::Deny
                            }
                        }
                        Err(_) => maho_agent::PermissionDecision::Deny,
                    }
                })
            });
            let backend = maho_agent::omo::factory::create_agent_runtime(
                Arc::new(agent_storage),
                Some(permission_callback),
                std::env::current_dir().unwrap_or_else(|_| std::path::PathBuf::from(".")),
                true,
            );
            let resolve_storage = match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                Ok(storage) => storage,
                Err(error) => {
                    let message = format!("Failed to open storage: {error}");
                    let _ = registry.mark_setup_failed(&queued_for_worker.run_id, message.clone());
                    if let Some(callback) = on_error.0 {
                        if let Ok(error) = CString::new(message) {
                            unsafe { callback(user_data.0 as *mut c_void, error.as_ptr()) };
                        }
                    }
                    return;
                }
            };
            let result = maho_core::routines::run_routine_and_record_tracked(
                &id,
                tier,
                &resolve_storage,
                source,
                |prompt| async move { backend.run_prompt(&prompt).await.map_err(|e| e.to_string()) },
                &registry,
                queued_for_worker,
            )
            .await;
            match result {
                Ok(result) => {
                    if let Some(callback) = on_complete.0 {
                        if let Some(json) = serde_json::to_string(&result)
                            .ok()
                            .and_then(|json| CString::new(json).ok())
                        {
                            unsafe { callback(user_data.0 as *mut c_void, json.as_ptr()) };
                        }
                    }
                }
                Err(error) => {
                    if let Some(callback) = on_error.0 {
                        if let Ok(error) = CString::new(error.to_string()) {
                            unsafe { callback(user_data.0 as *mut c_void, error.as_ptr()) };
                        }
                    }
                }
            }
        });
    });
    Ok(queued)
}

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
            let id = match CStr::from_ptr(id).to_str() {
                Ok(id) => id.to_string(),
                Err(_) => return -1,
            };
            let tier = match user_tier {
                0 => maho_core::routines::UserTier::Free,
                1 => maho_core::routines::UserTier::Pro,
                2 => maho_core::routines::UserTier::Max,
                _ => return -1,
            };
            match spawn_tracked_routine(
                core,
                id,
                tier,
                maho_core::routines::RoutineRunSource::Manual,
                SendableCallback(on_complete),
                SendableCallback(on_error),
                SendableUserData(user_data as usize),
            ) {
                Ok(_) => 0,
                Err(_) => -1,
            }
        },
        -1
    )
}

/// Start a manual routine and return its queued status JSON immediately.
/// Caller owns the returned string and must free it with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_routines_start(
    core: *mut MahoCore,
    id: *const c_char,
    user_tier: i32,
    source: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() || source.is_null() {
                return ptr::null_mut();
            }
            let id = match CStr::from_ptr(id).to_str() {
                Ok(id) => id.to_string(),
                Err(_) => return ptr::null_mut(),
            };
            let source = match CStr::from_ptr(source).to_str() {
                Ok("manual") => maho_core::routines::RoutineRunSource::Manual,
                Ok("scheduled") => maho_core::routines::RoutineRunSource::Scheduled,
                Ok("event") => maho_core::routines::RoutineRunSource::Event,
                _ => return ptr::null_mut(),
            };
            let tier = match user_tier {
                0 => maho_core::routines::UserTier::Free,
                1 => maho_core::routines::UserTier::Pro,
                2 => maho_core::routines::UserTier::Max,
                _ => return ptr::null_mut(),
            };
            match spawn_tracked_routine(
                core,
                id,
                tier,
                source,
                SendableCallback(None),
                SendableCallback(None),
                SendableUserData(0),
            ) {
                Ok(status) => to_json_cstring(&status),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Return every routine run known to this browser process, including terminal
/// runs. This is the initial snapshot paired with status callbacks.
#[no_mangle]
pub unsafe extern "C" fn maho_routines_respond_to_approval(
    core: *mut MahoCore,
    run_id: *const c_char,
    approval_id: *const c_char,
    approved: bool,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || run_id.is_null() || approval_id.is_null() {
                return false;
            }
            let run_id = match CStr::from_ptr(run_id).to_str() {
                Ok(value) => value,
                Err(_) => return false,
            };
            let approval_id = match CStr::from_ptr(approval_id).to_str() {
                Ok(value) => value,
                Err(_) => return false,
            };
            routine_run_registry(core).respond_to_approval(run_id, approval_id, approved)
        },
        false
    )
}

#[no_mangle]
pub unsafe extern "C" fn maho_routines_active_runs(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() {
                return ptr::null_mut();
            }
            to_json_cstring(&routine_run_registry(core).snapshot())
        },
        ptr::null_mut()
    )
}

#[no_mangle]
pub unsafe extern "C" fn maho_routines_register_status_callback(
    core: *mut MahoCore,
    user_data: *mut c_void,
    callback: RoutineStatusCb,
) -> u64 {
    ffi_safe!(
        {
            if core.is_null() {
                return 0;
            }
            let _ = routine_run_registry(core);
            let token = NEXT_CALLBACK_TOKEN.fetch_add(1, Ordering::SeqCst);
            routine_status_callbacks()
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner)
                .push(RoutineStatusCallback {
                    token,
                    core: core as usize,
                    user_data: user_data as usize,
                    callback,
                    lease: FfiArc::new(CallbackLease::default()),
                });
            token
        },
        0
    )
}

#[no_mangle]
pub unsafe extern "C" fn maho_routines_unregister_status_callback(core: *mut MahoCore, token: u64) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if core.is_null() || token == 0 {
            return;
        }
        let core_key = core as usize;
        let removed = routine_status_callbacks()
            .lock()
            .ok()
            .and_then(|mut callbacks| {
                callbacks
                    .iter()
                    .position(|entry| entry.core == core_key && entry.token == token)
                    .map(|index| callbacks.remove(index))
            });
        if let Some(entry) = removed {
            entry.lease.remove_and_wait(token);
        }
    }));
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

/// Partial-update payload for `maho_routines_update_custom`. Absent fields
/// keep the stored value. When `schedule` (cron) is supplied any event
/// `trigger` is cleared, and when `trigger` (an event string) is supplied the
/// cron `schedule` is cleared, so a routine carries at most one trigger kind.
#[derive(serde::Deserialize)]
struct UpdateCustomRoutineInput {
    #[serde(default)]
    name: Option<String>,
    #[serde(default)]
    prompt: Option<String>,
    #[serde(default)]
    schedule: Option<String>,
    #[serde(default)]
    trigger: Option<String>,
    #[serde(default)]
    enabled: Option<bool>,
}

#[derive(serde::Deserialize)]
struct FireRoutineEventInput {
    kind: String,
    #[serde(default)]
    count: Option<u32>,
    #[serde(default)]
    channel: Option<String>,
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

/// Update fields of a user-defined routine identified by `id`. JSON payload
/// `{name?,prompt?,schedule?,trigger?,enabled?}` where absent fields keep the
/// stored value; supplying `schedule` clears any event `trigger` and vice
/// versa. Built-in routine ids are never updatable. Returns 0 on success, -1
/// on error (null args / parse failure / invalid schedule-or-trigger / empty
/// name-or-prompt / storage failure), -2 when no custom routine exists with
/// `id`.
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`.
/// `id` and `json` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_routines_update_custom(
    core: *mut MahoCore,
    id: *const c_char,
    json: *const c_char,
) -> i32 {
    ffi_safe!(
        {
            if core.is_null() || id.is_null() || json.is_null() {
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
            if id_str.trim().is_empty() {
                return -1;
            }
            let json_str = match CStr::from_ptr(json).to_str() {
                Ok(s) => s,
                Err(_) => return -1,
            };
            let input: UpdateCustomRoutineInput = match serde_json::from_str(json_str) {
                Ok(v) => v,
                Err(_) => return -1,
            };
            if input.name.as_ref().is_some_and(|n| n.trim().is_empty())
                || input.prompt.as_ref().is_some_and(|p| p.trim().is_empty())
            {
                return -1;
            }
            // Normalize blank strings to None so an empty form field is treated as
            // "no trigger change" rather than an invalid one.
            let schedule = input.schedule.filter(|s| !s.trim().is_empty());
            let trigger = input.trigger.filter(|s| !s.trim().is_empty());
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
            let existing = match storage.get_custom_routine(id_str) {
                Ok(Some(existing)) => existing,
                Ok(None) => return -2,
                Err(_) => return -1,
            };
            // A routine carries at most one trigger kind: supplying one clears
            // the other instead of letting both linger.
            let (schedule, trigger) = match (&schedule, &trigger) {
                (Some(_), _) => (schedule, None),
                (None, Some(_)) => (None, trigger),
                (None, None) => (existing.schedule.clone(), existing.trigger.clone()),
            };
            let routine = maho_storage::CustomRoutine {
                id: id_str.to_string(),
                name: input.name.unwrap_or(existing.name),
                prompt: input.prompt.unwrap_or(existing.prompt),
                schedule,
                trigger,
                enabled: input.enabled.unwrap_or(existing.enabled),
                created_at: existing.created_at,
            };
            match storage.create_custom_routine(&routine) {
                Ok(()) => 0,
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
                // Deferred-wiring parity triggers: reachable here so an emitter can
                // fire them once wired, but no browser/mail adapter emits them yet.
                "on_notification" => maho_core::routines::RoutineEvent::OnNotification {
                    channel: input.channel.unwrap_or_default(),
                },
                "on_inbox_heartbeat" => maho_core::routines::RoutineEvent::OnInboxHeartbeat {
                    min_unread: input.count.unwrap_or(0),
                },
                _ => return,
            };
            let db_path = match core_ref.sqlite_db_path() {
                Some(p) => p.to_string(),
                None => return,
            };
            let registry = routine_run_registry(core);

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
                    // `fire_event_routines` may run several routines, so each run owns a
                    // backend whose permission callback is keyed to that run id.
                    let runner = |prompt: String, run_id: String| {
                        let agent_storage =
                            match maho_storage::sqlite::SqliteStorage::open(&db_path) {
                                Ok(storage) => MutexAgentStorage(Arc::new(Mutex::new(storage))),
                                Err(error) => {
                                    return Box::pin(async move { Err(error.to_string()) })
                                        as Pin<Box<dyn Future<Output = Result<String, String>>>>;
                                }
                            };
                        let approval_registry = FfiArc::clone(&registry);
                        let permission_callback: maho_agent::PermissionCallback =
                            Box::new(move |request| {
                                let registry = FfiArc::clone(&approval_registry);
                                let run_id = run_id.clone();
                                Box::pin(async move {
                                    if request.sensitivity != maho_agent::ToolSensitivity::Sensitive
                                    {
                                        return maho_agent::PermissionDecision::Allow;
                                    }
                                    match registry.request_approval(
                                        &run_id,
                                        request.tool_name,
                                        request.sensitivity.as_str().to_string(),
                                    ) {
                                        Ok(waiter) => {
                                            if waiter.await {
                                                maho_agent::PermissionDecision::Allow
                                            } else {
                                                maho_agent::PermissionDecision::Deny
                                            }
                                        }
                                        Err(_) => maho_agent::PermissionDecision::Deny,
                                    }
                                })
                            });
                        let backend = maho_agent::omo::factory::create_agent_runtime(
                            Arc::new(agent_storage),
                            Some(permission_callback),
                            std::env::current_dir()
                                .unwrap_or_else(|_| std::path::PathBuf::from(".")),
                            true,
                        );
                        Box::pin(async move {
                            backend.run_prompt(&prompt).await.map_err(|e| e.to_string())
                        })
                            as Pin<Box<dyn Future<Output = Result<String, String>>>>
                    };
                    if let Err(e) = maho_core::routines::fire_event_routines_tracked(
                        event,
                        maho_core::routines::UserTier::Max,
                        &storage,
                        runner,
                        &registry,
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
                    let registry = routine_run_registry(core);
                    let queued =
                        registry.begin(&id_str, maho_core::routines::RoutineRunSource::Scheduled);

                    // Spawn onto the shared, owned maho-core runtime. `maho_routines_tick`
                    // is called once/minute from the Chromium C++ browser thread which has
                    // NO ambient tokio runtime; a bare `tokio::spawn` there panics and the
                    // scheduled routine silently never fires. `Runtime::spawn` spawns onto
                    // its own runtime, so no panic can cross the FFI boundary.
                    // Spawn onto the shared, owned maho-core runtime. `maho_routines_tick`
                    // is called once/minute from the Chromium C++ browser thread which has
                    // NO ambient tokio runtime; a bare `tokio::spawn` there panics and the
                    // scheduled routine silently never fires. `run_routine_and_record`
                    // borrows a `!Send` SqliteStorage across an await, so drive it on a
                    // current-thread runtime pinned to a `spawn_blocking` thread (the future
                    // never migrates) — mirroring `maho_routines_run`.
                    maho_core::memory_manager::get_runtime().spawn_blocking(move || {
                        let rt = match tokio::runtime::Builder::new_current_thread()
                            .enable_all()
                            .build()
                        {
                            Ok(rt) => rt,
                            Err(e) => {
                                let message = format!("Failed to build runtime: {e}");
                                let _ = registry.mark_setup_failed(&queued.run_id, message.clone());
                                eprintln!("[routines scheduled] {message}");
                                return;
                            }
                        };
                        rt.block_on(async move {
                            let storage =
                                match maho_storage::sqlite::SqliteStorage::open(&db_path_clone) {
                                    Ok(s) => s,
                                    Err(e) => {
                                        let message = format!("Failed to open sqlite storage: {e}");
                                        let _ = registry
                                            .mark_setup_failed(&queued.run_id, message.clone());
                                        eprintln!("[routines scheduled] {message}");
                                        return;
                                    }
                                };
                            // Separate handle for resolving the routine and recording its
                            // result into the durable inbox; the agent moves its own handle.
                            let record_storage =
                                match maho_storage::sqlite::SqliteStorage::open(&db_path_clone) {
                                    Ok(s) => s,
                                    Err(e) => {
                                        let message = format!("Failed to open record storage: {e}");
                                        let _ = registry
                                            .mark_setup_failed(&queued.run_id, message.clone());
                                        eprintln!("[routines scheduled] {message}");
                                        return;
                                    }
                                };
                            let agent_storage = MutexAgentStorage(Arc::new(Mutex::new(storage)));
                            let approval_registry = FfiArc::clone(&registry);
                            let approval_run_id = queued.run_id.clone();
                            let permission_callback: maho_agent::PermissionCallback =
                                Box::new(move |request| {
                                    let registry = FfiArc::clone(&approval_registry);
                                    let run_id = approval_run_id.clone();
                                    Box::pin(async move {
                                        if request.sensitivity
                                            != maho_agent::ToolSensitivity::Sensitive
                                        {
                                            return maho_agent::PermissionDecision::Allow;
                                        }
                                        match registry.request_approval(
                                            &run_id,
                                            request.tool_name,
                                            request.sensitivity.as_str().to_string(),
                                        ) {
                                            Ok(waiter) => {
                                                if waiter.await {
                                                    maho_agent::PermissionDecision::Allow
                                                } else {
                                                    maho_agent::PermissionDecision::Deny
                                                }
                                            }
                                            Err(_) => maho_agent::PermissionDecision::Deny,
                                        }
                                    })
                                });
                            let backend = maho_agent::omo::factory::create_agent_runtime(
                                Arc::new(agent_storage),
                                Some(permission_callback),
                                std::env::current_dir()
                                    .unwrap_or_else(|_| std::path::PathBuf::from(".")),
                                true,
                            );

                            let result = maho_core::routines::run_routine_and_record_tracked(
                                &id_str,
                                maho_core::routines::UserTier::Max,
                                &record_storage,
                                maho_core::routines::RoutineRunSource::Scheduled,
                                |prompt| async move {
                                    backend.run_prompt(&prompt).await.map_err(|e| e.to_string())
                                },
                                &registry,
                                queued,
                            )
                            .await;

                            if let Err(e) = result {
                                eprintln!("[RoutineScheduler] routine {} failed: {}", id_str, e);
                            }
                        });
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

// ============================================================
// Routine Inbox FFI (durable run-result history)
// ============================================================

/// Returns JSON array of recorded routine results (the routine inbox), newest
/// first. If `routine_id` is non-null, only that routine's history is returned;
/// pass null for all routines. `limit` caps the number of rows (0 is treated as
/// a sane default of 50). Caller must free with `maho_string_free`.
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`. `routine_id`, if
/// non-null, must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_routines_history(
    core: *mut MahoCore,
    routine_id: *const c_char,
    limit: u32,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() {
                return ptr::null_mut();
            }
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
            let capped = if limit == 0 { 50 } else { limit };
            let results = if routine_id.is_null() {
                storage.list_routine_results(capped)
            } else {
                let id_str = match CStr::from_ptr(routine_id).to_str() {
                    Ok(s) => s,
                    Err(_) => return ptr::null_mut(),
                };
                storage.list_routine_results_for(id_str, capped)
            };
            match results {
                Ok(records) => to_json_cstring(&records),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// Returns the single most recent routine result for `routine_id` as JSON, or
/// null if the routine has never run. Caller must free with `maho_string_free`.
///
/// # Safety
/// `core` must be a valid pointer returned by `maho_core_new`. `routine_id`
/// must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_routines_latest(
    core: *mut MahoCore,
    routine_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || routine_id.is_null() {
                return ptr::null_mut();
            }
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
            let id_str = match CStr::from_ptr(routine_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            match storage.latest_routine_result(id_str) {
                Ok(Some(record)) => to_json_cstring(&record),
                Ok(None) => ptr::null_mut(),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

// ---------------------------------------------------------------------------
// Semantic Trace v3 Recorder FFI
// ---------------------------------------------------------------------------

pub struct MahoTraceRecorder(pub maho_agent::trace_recorder::TraceRecorder<fn() -> std::time::Instant>);

/// Creates a new heap-allocated TraceRecorder.
///
/// # Safety
/// Caller must free with `maho_trace_recorder_destroy` or `maho_trace_recorder_finish_json`.
#[no_mangle]
pub unsafe extern "C" fn maho_trace_recorder_create() -> *mut MahoTraceRecorder {
    Box::into_raw(Box::new(MahoTraceRecorder(
        maho_agent::trace_recorder::TraceRecorder::new(),
    )))
}

/// Destroys a `MahoTraceRecorder` without finishing.
///
/// # Safety
/// `recorder` may be null or must be a valid pointer from `maho_trace_recorder_create`.
#[no_mangle]
pub unsafe extern "C" fn maho_trace_recorder_destroy(recorder: *mut MahoTraceRecorder) {
    if !recorder.is_null() {
        drop(Box::from_raw(recorder));
    }
}

/// Records a click step.
///
/// # Safety
/// `recorder` must be a valid pointer from `maho_trace_recorder_create`.
/// `target_ref` may be null or a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_trace_recorder_record_click(
    recorder: *mut MahoTraceRecorder,
    target_ref: *const c_char,
    x: f64,
    y: f64,
) {
    if recorder.is_null() {
        return;
    }
    let target = if target_ref.is_null() {
        None
    } else {
        CStr::from_ptr(target_ref)
            .to_str()
            .ok()
            .map(|s| s.to_string())
    };
    (*recorder).0.record_click(target, x, y);
}

/// Records typed text into a fill step (coalesced within 400ms window).
///
/// # Safety
/// `recorder` must be a valid pointer from `maho_trace_recorder_create`.
/// `text` must be a valid null-terminated C string.
/// `selector` may be null or a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_trace_recorder_record_fill(
    recorder: *mut MahoTraceRecorder,
    text: *const c_char,
    selector: *const c_char,
) {
    if recorder.is_null() || text.is_null() {
        return;
    }
    let text_str = match CStr::from_ptr(text).to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return,
    };
    let sel = if selector.is_null() {
        None
    } else {
        CStr::from_ptr(selector)
            .to_str()
            .ok()
            .map(|s| s.to_string())
    };
    (*recorder).0.record_fill(text_str, sel);
}

/// Records page navigation.
///
/// # Safety
/// `recorder` must be a valid pointer from `maho_trace_recorder_create`.
/// `url` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_trace_recorder_record_navigate(
    recorder: *mut MahoTraceRecorder,
    url: *const c_char,
) {
    if recorder.is_null() || url.is_null() {
        return;
    }
    let url_str = match CStr::from_ptr(url).to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return,
    };
    (*recorder).0.record_navigate(url_str);
}

/// Records a hover step.
///
/// # Safety
/// `recorder` must be a valid pointer from `maho_trace_recorder_create`.
/// `target_ref` may be null or a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_trace_recorder_record_hover(
    recorder: *mut MahoTraceRecorder,
    target_ref: *const c_char,
) {
    if recorder.is_null() {
        return;
    }
    let target = if target_ref.is_null() {
        None
    } else {
        CStr::from_ptr(target_ref)
            .to_str()
            .ok()
            .map(|s| s.to_string())
    };
    (*recorder).0.record_hover(target);
}

/// Finishes recording and returns the TraceV3 JSON string.
///
/// # Safety
/// Deallocates `recorder`. Caller owns the returned string and must free it via `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_trace_recorder_finish_json(
    recorder: *mut MahoTraceRecorder,
) -> *mut c_char {
    if recorder.is_null() {
        return ptr::null_mut();
    }
    let rec = Box::from_raw(recorder);
    let trace = rec.0.finish();
    match trace.to_json() {
        Ok(json) => match CString::new(json) {
            Ok(c) => c.into_raw(),
            Err(_) => ptr::null_mut(),
        },
        Err(_) => ptr::null_mut(),
    }
}

/// Sets the available browser capabilities for skill activation and filtering.
///
/// Queries discoverable skills matching the supplied capability list (plus any
/// user-handoff skills) and updates the internal capability filter on `MahoCore`.
/// Returns a JSON array of `SkillInfo` structs representing discoverable skills.
///
/// # Safety
/// `core` must be a valid non-null pointer to a `MahoCore`.
/// `capabilities_json` must be a valid null-terminated UTF-8 JSON string array.
/// Caller must free returned JSON string via `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_skills_set_available_capabilities(
    core: *mut MahoCore,
    capabilities_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || capabilities_json.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core_ref = unsafe { &mut *core };
            let json_str = match unsafe { CStr::from_ptr(capabilities_json) }.to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let capabilities: Vec<String> = match serde_json::from_str(json_str) {
                Ok(caps) => caps,
                Err(_) => return ptr::null_mut(),
            };
            let cap_refs: Vec<&str> = capabilities.iter().map(|s| s.as_str()).collect();
            let discovered = core_ref.discover_skills(&cap_refs);
            let skill_infos: Vec<maho_types::events::core_update::SkillInfo> =
                discovered.into_iter().map(|s| s.into()).collect();
            core_ref.set_skills_available_capabilities(capabilities);
            to_json_cstring(&skill_infos)
        },
        ptr::null_mut()
    )
}

use std::sync::atomic::{AtomicU64, Ordering};

#[derive(Clone)]
struct SplitChangeCallback {
    token: u64,
    callback: extern "C" fn(*const c_char),
    lease: FfiArc<CallbackLease>,
}

static SPLIT_CALLBACKS: OnceLock<Mutex<Vec<SplitChangeCallback>>> = OnceLock::new();
static NEXT_CALLBACK_TOKEN: AtomicU64 = AtomicU64::new(1);

type ToolAvailCb = unsafe extern "C" fn(*mut c_void, *const c_char);

#[derive(Default)]
struct CallbackLeaseState {
    removed: bool,
    in_flight: usize,
}

#[derive(Default)]
struct CallbackLease {
    state: FfiMutex<CallbackLeaseState>,
    idle: FfiCondvar,
}

impl CallbackLease {
    fn enter(self: &FfiArc<Self>, token: u64) -> Option<CallbackInvocation> {
        let mut state = self
            .state
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        if state.removed {
            return None;
        }
        state.in_flight += 1;
        drop(state);
        ACTIVE_CALLBACK_TOKENS.with(|tokens| tokens.borrow_mut().push(token));
        Some(CallbackInvocation {
            token,
            lease: FfiArc::clone(self),
        })
    }

    fn remove_and_wait(&self, token: u64) {
        let remaining_self_invocations = ACTIVE_CALLBACK_TOKENS.with(|tokens| {
            tokens
                .borrow()
                .iter()
                .filter(|active| **active == token)
                .count()
        });
        let mut state = self
            .state
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        state.removed = true;
        while state.in_flight > remaining_self_invocations {
            state = self
                .idle
                .wait(state)
                .unwrap_or_else(std::sync::PoisonError::into_inner);
        }
    }
}

struct CallbackInvocation {
    token: u64,
    lease: FfiArc<CallbackLease>,
}

impl Drop for CallbackInvocation {
    fn drop(&mut self) {
        ACTIVE_CALLBACK_TOKENS.with(|tokens| {
            let mut tokens = tokens.borrow_mut();
            if let Some(index) = tokens.iter().rposition(|token| *token == self.token) {
                tokens.remove(index);
            }
        });
        let mut state = self
            .lease
            .state
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        state.in_flight = state.in_flight.saturating_sub(1);
        self.lease.idle.notify_all();
    }
}

#[derive(Clone)]
struct ToolAvailabilityCallback {
    token: u64,
    user_data: usize,
    callback: ToolAvailCb,
    lease: FfiArc<CallbackLease>,
}

static TOOL_AVAIL_CALLBACKS: OnceLock<Mutex<Vec<ToolAvailabilityCallback>>> = OnceLock::new();

type SettingsChangeCb = unsafe extern "C" fn(*mut c_void, *const c_char);

#[derive(Clone)]
struct SettingsChangeCallback {
    token: u64,
    user_data: usize,
    callback: SettingsChangeCb,
    lease: FfiArc<CallbackLease>,
}

static SETTINGS_CHANGE_CALLBACKS: OnceLock<Mutex<Vec<SettingsChangeCallback>>> = OnceLock::new();

fn get_split_callbacks() -> &'static Mutex<Vec<SplitChangeCallback>> {
    SPLIT_CALLBACKS.get_or_init(|| Mutex::new(Vec::new()))
}

fn get_tool_avail_callbacks() -> &'static Mutex<Vec<ToolAvailabilityCallback>> {
    TOOL_AVAIL_CALLBACKS.get_or_init(|| Mutex::new(Vec::new()))
}

fn get_settings_change_callbacks() -> &'static Mutex<Vec<SettingsChangeCallback>> {
    SETTINGS_CHANGE_CALLBACKS.get_or_init(|| Mutex::new(Vec::new()))
}

fn process_split_callbacks_for_updates(updates: &[maho_types::events::core_update::CoreUpdate]) {
    let inside_serialized_ffi = FFI_SERIALIZATION_DEPTH.with(|depth| depth.get() != 0);
    if inside_serialized_ffi {
        DEFERRED_CORE_UPDATES.with(|pending| pending.borrow_mut().extend_from_slice(updates));
    } else {
        dispatch_split_callbacks_for_updates(updates);
    }
}

fn dispatch_split_callbacks_for_updates(updates: &[maho_types::events::core_update::CoreUpdate]) {
    for update in updates {
        match update {
            maho_types::events::core_update::CoreUpdate::SplitViewChanged { .. } => {
                let callbacks = get_split_callbacks()
                    .lock()
                    .map(|callbacks| callbacks.clone())
                    .unwrap_or_default();
                let json_str = serde_json::to_string(update).unwrap_or_default();
                let c_json = CString::new(json_str).unwrap();
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
                let json_str = serde_json::to_string(update).unwrap_or_default();
                let c_json = CString::new(json_str).unwrap();
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
                // Contract C2: marshal the `settings` payload (not the update wrapper);
                // pass null on failure since the C side is documented null-tolerant.
                let c_json = serde_json::to_string(settings)
                    .ok()
                    .and_then(|json| CString::new(json).ok());
                let json_ptr = c_json.as_ref().map_or(ptr::null(), |c| c.as_ptr());
                for entry in callbacks {
                    let Some(_invocation) = entry.lease.enter(entry.token) else {
                        continue;
                    };
                    // SAFETY: the function pointer and user data were copied while the
                    // registry was locked. The C string outlives this callback invocation.
                    unsafe {
                        (entry.callback)(entry.user_data as *mut c_void, json_ptr);
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

/// Starts Google Sign-In and returns `{"state","authUrl"}` JSON, or null on
/// failure. The redirect is caught on a background thread; `on_complete` then
/// fires with the `id_token` + `nonce` the caller must POST to the relay.
///
/// # Safety
/// `client_id` must be a null-terminated C string (may be empty to use the
/// build-time client id). `user_data` must remain valid until `on_complete`
/// fires. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_google_sign_in_start(
    client_id: *const c_char,
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut c_void,
            id_token: *const c_char,
            nonce: *const c_char,
            error: *const c_char,
        ),
    >,
    user_data: *mut c_void,
) -> *mut c_char {
    ffi_safe!(
        {
            let client_id_str = if client_id.is_null() {
                ""
            } else {
                match CStr::from_ptr(client_id).to_str() {
                    Ok(s) => s,
                    Err(_) => return ptr::null_mut(),
                }
            };

            let sink = GoogleSignInSink {
                on_complete,
                user_data,
            };
            let started = match maho_core::google_identity::start_google_sign_in(
                client_id_str,
                move |result| sink.deliver(result),
            ) {
                Ok(started) => started,
                Err(_) => return ptr::null_mut(),
            };

            to_json_cstring(&serde_json::json!({
                "state": started.state,
                "authUrl": started.auth_url,
            }))
        },
        ptr::null_mut()
    )
}

/// Cancels an in-flight Google Sign-In identified by `state` (the value from the
/// JSON returned by `maho_google_sign_in_start`). Lets the C++ layer end a flow
/// deterministically when the OAuth popup is closed before the redirect, or when
/// a new sign-in supersedes a stale one, freeing the listener thread + bound
/// loopback port instead of leaking them until the backstop timeout. Returns
/// true when a matching pending flow was found; idempotent.
///
/// # Safety
/// `state` must be a null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_google_sign_in_cancel(state: *const c_char) -> bool {
    ffi_safe!(
        {
            if state.is_null() {
                return false;
            }
            let state_str = match CStr::from_ptr(state).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            maho_core::google_identity::cancel_google_sign_in(state_str)
        },
        false
    )
}

/// Starts an AI provider OAuth sign-in described by `config_json`
/// (`ProviderOAuthConfig`) and returns `{"state","authUrl"}` JSON, or null on
/// failure. `on_complete` fires once with the token JSON, or with `error` set.
///
/// # Safety
/// `config_json` must be a null-terminated C string. `user_data` must remain
/// valid until `on_complete` fires. Caller must free the returned string with
/// `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_provider_oauth_start(
    config_json: *const c_char,
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut c_void,
            tokens_json: *const c_char,
            error: *const c_char,
        ),
    >,
    user_data: *mut c_void,
) -> *mut c_char {
    ffi_safe!(
        {
            if config_json.is_null() {
                return ptr::null_mut();
            }
            let config_str = match CStr::from_ptr(config_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let config: maho_core::provider_oauth::ProviderOAuthConfig =
                match serde_json::from_str(config_str) {
                    Ok(config) => config,
                    Err(_) => return ptr::null_mut(),
                };

            let sink = ProviderOAuthSink {
                on_complete,
                user_data,
            };
            let started =
                match maho_core::provider_oauth::start_provider_oauth(config, move |result| {
                    sink.deliver(result)
                }) {
                    Ok(started) => started,
                    Err(_) => return ptr::null_mut(),
                };

            to_json_cstring(&serde_json::json!({
                "state": started.state,
                "authUrl": started.auth_url,
            }))
        },
        ptr::null_mut()
    )
}

/// Cancels the in-flight provider sign-in identified by `state`, freeing the
/// listener thread and its bound loopback port. Idempotent.
///
/// # Safety
/// `state` must be a null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_provider_oauth_cancel(state: *const c_char) -> bool {
    ffi_safe!(
        {
            if state.is_null() {
                return false;
            }
            let state_str = match CStr::from_ptr(state).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            maho_core::provider_oauth::cancel_provider_oauth(state_str)
        },
        false
    )
}

/// Exchanges a stored refresh token for fresh provider tokens. Returns token
/// JSON, or `{"error":"..."}`. Blocks on network I/O; call off the UI thread.
///
/// # Safety
/// `config_json` and `refresh_token` must be null-terminated C strings. Caller
/// must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_provider_oauth_refresh(
    config_json: *const c_char,
    refresh_token: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if config_json.is_null() || refresh_token.is_null() {
                return ptr::null_mut();
            }
            let config_str = match CStr::from_ptr(config_json).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let refresh_str = match CStr::from_ptr(refresh_token).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let config: maho_core::provider_oauth::ProviderOAuthConfig =
                match serde_json::from_str(config_str) {
                    Ok(config) => config,
                    Err(e) => {
                        return to_json_cstring(&serde_json::json!({
                            "error": format!("invalid OAuth configuration: {e}"),
                        }))
                    }
                };

            match maho_core::provider_oauth::refresh_provider_oauth(&config, refresh_str) {
                Ok(tokens) => to_json_cstring(&tokens),
                Err(e) => to_json_cstring(&serde_json::json!({ "error": e.to_string() })),
            }
        },
        ptr::null_mut()
    )
}

struct ProviderOAuthSink {
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut c_void,
            tokens_json: *const c_char,
            error: *const c_char,
        ),
    >,
    user_data: *mut c_void,
}

// SAFETY: the C++ owner guarantees `user_data` outlives the single callback
// invocation; see the safety contract on `maho_provider_oauth_start`.
unsafe impl Send for ProviderOAuthSink {}

impl ProviderOAuthSink {
    fn deliver(
        &self,
        result: std::result::Result<
            maho_core::provider_oauth::ProviderOAuthTokens,
            maho_core::error::CoreError,
        >,
    ) {
        let Some(callback) = self.on_complete else {
            return;
        };
        let payload = match result {
            Ok(tokens) => serde_json::to_string(&tokens).map_err(|e| e.to_string()),
            Err(err) => Err(err.to_string()),
        };
        match payload {
            Ok(json) => {
                let Ok(json) = CString::new(json) else {
                    return;
                };
                // SAFETY: `json` outlives the call; error is null on success.
                unsafe {
                    callback(self.user_data, json.as_ptr(), ptr::null());
                }
            }
            Err(message) => {
                let Ok(message) = CString::new(message) else {
                    return;
                };
                // SAFETY: `message` outlives the call; tokens are null on error.
                unsafe {
                    callback(self.user_data, ptr::null(), message.as_ptr());
                }
            }
        }
    }
}

/// Carries the C callback across the sign-in thread boundary. The raw
/// `user_data` pointer is not `Send`, so the unsafe impl asserts the C++ owner
/// keeps it alive until `on_complete` fires (documented on the FFI entry point).
struct GoogleSignInSink {
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut c_void,
            id_token: *const c_char,
            nonce: *const c_char,
            error: *const c_char,
        ),
    >,
    user_data: *mut c_void,
}

// SAFETY: the C++ owner guarantees `user_data` outlives the single callback
// invocation; see the safety contract on `maho_google_sign_in_start`.
unsafe impl Send for GoogleSignInSink {}

impl GoogleSignInSink {
    fn deliver(
        &self,
        result: std::result::Result<
            maho_core::google_identity::GoogleSignInResult,
            maho_core::error::CoreError,
        >,
    ) {
        let Some(callback) = self.on_complete else {
            return;
        };
        match result {
            Ok(success) => {
                let Ok(id_token) = CString::new(success.id_token) else {
                    return;
                };
                let Ok(nonce) = CString::new(success.nonce) else {
                    return;
                };
                // SAFETY: both CStrings outlive the call; error is null on success.
                unsafe {
                    callback(
                        self.user_data,
                        id_token.as_ptr(),
                        nonce.as_ptr(),
                        ptr::null(),
                    );
                }
            }
            Err(err) => {
                let Ok(message) = CString::new(err.to_string()) else {
                    return;
                };
                // SAFETY: `message` outlives the call; token fields are null on error.
                unsafe {
                    callback(self.user_data, ptr::null(), ptr::null(), message.as_ptr());
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::Ordering as AtomicOrdering;

    struct RoutineStatusTestContext {
        events: std::sync::Mutex<Vec<serde_json::Value>>,
    }

    unsafe extern "C" fn record_routine_status(user_data: *mut c_void, json: *const c_char) {
        let context = unsafe { &*(user_data as *const RoutineStatusTestContext) };
        let json = unsafe { CStr::from_ptr(json) }
            .to_str()
            .expect("status utf8");
        context
            .events
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .push(serde_json::from_str(json).expect("status json"));
    }

    #[test]
    fn test_maho_skills_set_available_capabilities_ffi() {
        let _gate_idle = crate::common::import_gate_idle();
        let core = maho_core_new();
        assert!(!core.is_null());

        // Call FFI with empty capability list: only user_handoff skills returned
        let empty_caps = CString::new("[]").unwrap();
        let handoff_ptr =
            unsafe { maho_skills_set_available_capabilities(core, empty_caps.as_ptr()) };
        assert!(!handoff_ptr.is_null());
        let handoff_str = unsafe { CStr::from_ptr(handoff_ptr) }.to_str().unwrap();
        let handoff_skills: Vec<maho_types::events::core_update::SkillInfo> =
            serde_json::from_str(handoff_str).unwrap();
        unsafe { maho_string_free(handoff_ptr) };

        assert!(
            !handoff_skills.is_empty(),
            "User handoff skills should be returned"
        );

        // Call FFI with specific capabilities
        let caps = CString::new(r#"["web_search", "browser_history_search_desktop"]"#).unwrap();
        let caps_ptr = unsafe { maho_skills_set_available_capabilities(core, caps.as_ptr()) };
        assert!(!caps_ptr.is_null());
        let caps_str = unsafe { CStr::from_ptr(caps_ptr) }.to_str().unwrap();
        let skills: Vec<maho_types::events::core_update::SkillInfo> =
            serde_json::from_str(caps_str).unwrap();
        unsafe { maho_string_free(caps_ptr) };

        assert!(skills.len() >= handoff_skills.len());

        // Null core or null json fails safely
        let null_res =
            unsafe { maho_skills_set_available_capabilities(std::ptr::null_mut(), caps.as_ptr()) };
        assert!(null_res.is_null());

        let null_json = unsafe { maho_skills_set_available_capabilities(core, std::ptr::null()) };
        assert!(null_json.is_null());

        unsafe { maho_core_free(core) };
    }

    #[test]
    fn test_set_tab_custom_icon_and_pinned_url_ffi() {
        use maho_types::events::shell_event::ShellEvent;

        fn view_models_json(core: *mut MahoCore) -> String {
            // SAFETY: |core| is a valid MahoCore for the test's lifetime; the
            // returned string is copied and freed before this helper returns.
            let ptr = unsafe { maho_core_get_tab_view_models(core) };
            assert!(!ptr.is_null());
            let text = unsafe { CStr::from_ptr(ptr) }.to_str().unwrap().to_string();
            unsafe { maho_string_free(ptr) };
            text
        }

        let _gate_idle = crate::common::import_gate_idle();
        let core = maho_core_new();
        assert!(!core.is_null());
        // SAFETY: |core| comes from maho_core_new and outlives every use below.
        let core_ref = unsafe { &mut *core };

        // Create and favorite a tab.
        let space_id = core_ref.get_active_space_id().clone();
        let before: Vec<_> = core_ref
            .get_tab_view_models()
            .iter()
            .map(|t| t.id.clone())
            .collect();
        core_ref.handle_event(ShellEvent::CreateTab {
            space_id,
            url: None,
            parent_id: None,
            tab_id: None,
            window_id: None,
            is_private: false,
        });
        let tab_id = core_ref
            .get_tab_view_models()
            .iter()
            .find(|t| !before.contains(&t.id))
            .expect("new tab should be visible in view models")
            .id
            .clone();
        core_ref.handle_event(ShellEvent::FavoriteTab {
            tab_id: tab_id.clone(),
        });

        let tab_cstr = CString::new(tab_id.0.as_str()).unwrap();

        // Setting a custom icon persists and round-trips through the JSON.
        let icon = CString::new("\u{1f680}").unwrap();
        // SAFETY: valid core pointer + NUL-terminated C strings owned by this test.
        unsafe { maho_core_set_tab_custom_icon(core, tab_cstr.as_ptr(), icon.as_ptr()) };
        let json = view_models_json(core);
        assert!(
            json.contains("customIcon"),
            "set icon must surface in the view-model JSON, got: {json}"
        );

        // Whitespace-only icon clears the override.
        let blank_icon = CString::new("   ").unwrap();
        // SAFETY: valid core pointer + NUL-terminated C strings owned by this test.
        unsafe { maho_core_set_tab_custom_icon(core, tab_cstr.as_ptr(), blank_icon.as_ptr()) };
        let json = view_models_json(core);
        assert!(
            !json.contains("customIcon"),
            "whitespace icon must clear the override, got: {json}"
        );

        // Null icon clears as well (no crash, stays cleared).
        // SAFETY: valid core pointer; null icon is the documented clear input.
        unsafe { maho_core_set_tab_custom_icon(core, tab_cstr.as_ptr(), std::ptr::null()) };
        let json = view_models_json(core);
        assert!(!json.contains("customIcon"));

        // Pinned URL: unknown tab and blank input must be rejected.
        let unknown = CString::new("00000000-0000-0000-0000-000000000000").unwrap();
        let url = CString::new("https://home.example.com").unwrap();
        let blank_url = CString::new("   ").unwrap();
        // SAFETY: valid core pointer + NUL-terminated C strings owned by this test.
        assert!(!unsafe {
            maho_core_set_tab_pinned_url(core, unknown.as_ptr(), url.as_ptr())
        });
        assert!(!unsafe {
            maho_core_set_tab_pinned_url(core, tab_cstr.as_ptr(), blank_url.as_ptr())
        });

        // Known tab + valid URL replaces the home URL.
        // SAFETY: valid core pointer + NUL-terminated C strings owned by this test.
        assert!(unsafe {
            maho_core_set_tab_pinned_url(core, tab_cstr.as_ptr(), url.as_ptr())
        });
        let json = view_models_json(core);
        assert!(
            json.contains("https://home.example.com"),
            "set pinned url must surface in the view-model JSON, got: {json}"
        );

        // SAFETY: |core| was created by maho_core_new in this test.
        unsafe { maho_core_free(core) };
    }

    #[test]
    fn routine_status_callback_unregister_stops_delivery_and_is_core_scoped() {
        let core = maho_core_new();
        let other_core = maho_core_new();
        assert!(!core.is_null());
        assert!(!other_core.is_null());
        let context = Box::new(RoutineStatusTestContext {
            events: std::sync::Mutex::new(Vec::new()),
        });
        let context_ptr = Box::into_raw(context);
        let token = unsafe {
            maho_routines_register_status_callback(core, context_ptr.cast(), record_routine_status)
        };
        assert_ne!(token, 0);

        let registry = routine_run_registry(core);
        let first = registry.begin("r", maho_core::routines::RoutineRunSource::Manual);
        registry.mark_running(&first.run_id).expect("running");
        assert_eq!(
            unsafe {
                (*context_ptr)
                    .events
                    .lock()
                    .unwrap_or_else(std::sync::PoisonError::into_inner)
                    .len()
            },
            2
        );

        // A token cannot be removed through a different core.
        unsafe { maho_routines_unregister_status_callback(other_core, token) };
        registry
            .mark_succeeded(&first.run_id, "done".to_string())
            .expect("succeeded");
        assert_eq!(
            unsafe {
                (*context_ptr)
                    .events
                    .lock()
                    .unwrap_or_else(std::sync::PoisonError::into_inner)
                    .len()
            },
            3
        );

        unsafe { maho_routines_unregister_status_callback(core, token) };
        let _second = registry.begin("r", maho_core::routines::RoutineRunSource::Event);
        let events = unsafe {
            (*context_ptr)
                .events
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner)
                .clone()
        };
        assert_eq!(events.len(), 3);
        assert_eq!(events[0]["state"], "queued");
        assert_eq!(events[1]["state"], "running");
        assert_eq!(events[2]["state"], "succeeded");

        unsafe {
            drop(Box::from_raw(context_ptr));
            maho_core_free(other_core);
            maho_core_free(core);
        }
    }

    #[test]
    fn routine_approval_ffi_rejects_stale_ids_and_resolves_current_id() {
        let core = maho_core_new();
        assert!(!core.is_null());
        let registry = routine_run_registry(core);
        let queued = registry.begin("r", maho_core::routines::RoutineRunSource::Manual);
        registry.mark_running(&queued.run_id).expect("running");
        let waiter = registry
            .request_approval(
                &queued.run_id,
                "browser_type".to_string(),
                "sensitive".to_string(),
            )
            .expect("awaiting approval");
        let approval_id = registry.snapshot()[0]
            .approval
            .as_ref()
            .expect("approval")
            .approval_id
            .clone();
        let run_id = CString::new(queued.run_id.clone()).expect("run id");
        let wrong_id = CString::new("wrong").expect("wrong id");
        let approval_id_c = CString::new(approval_id.clone()).expect("approval id");

        assert!(!unsafe {
            maho_routines_respond_to_approval(core, run_id.as_ptr(), wrong_id.as_ptr(), true)
        });
        assert_eq!(registry.snapshot()[0].revision, 3);
        assert!(unsafe {
            maho_routines_respond_to_approval(core, run_id.as_ptr(), approval_id_c.as_ptr(), true)
        });
        assert!(maho_core::memory_manager::get_runtime().block_on(waiter));
        assert_eq!(
            registry.snapshot()[0].state,
            maho_core::routine_runs::RoutineRunState::Running
        );
        assert!(!unsafe {
            maho_routines_respond_to_approval(core, run_id.as_ptr(), approval_id_c.as_ptr(), false)
        });
        assert_eq!(registry.snapshot()[0].revision, 4);

        unsafe { maho_core_free(core) };
    }

    #[test]
    fn routine_active_runs_ffi_returns_owned_snapshot_json() {
        let core = maho_core_new();
        assert!(!core.is_null());
        let queued = routine_run_registry(core)
            .begin("routine-id", maho_core::routines::RoutineRunSource::Manual);

        let snapshot = unsafe { maho_routines_active_runs(core) };
        assert!(!snapshot.is_null());
        let json = unsafe { CStr::from_ptr(snapshot) }
            .to_str()
            .expect("snapshot utf8")
            .to_owned();
        unsafe { maho_string_free(snapshot) };
        let runs: Vec<serde_json::Value> = serde_json::from_str(&json).expect("snapshot json");

        assert_eq!(runs.len(), 1);
        assert_eq!(runs[0]["runId"], queued.run_id);
        assert_eq!(runs[0]["routineId"], "routine-id");
        assert_eq!(runs[0]["state"], "queued");
        assert!(unsafe { maho_routines_active_runs(ptr::null_mut()) }.is_null());

        unsafe { maho_core_free(core) };
    }

    struct BlockingRoutineStatusContext {
        entered: std::sync::mpsc::SyncSender<()>,
        release: std::sync::Mutex<std::sync::mpsc::Receiver<()>>,
    }

    unsafe extern "C" fn block_routine_status(user_data: *mut c_void, _json: *const c_char) {
        let context = unsafe { &*(user_data as *const BlockingRoutineStatusContext) };
        context.entered.send(()).expect("entered signal");
        context
            .release
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .recv()
            .expect("release signal");
    }

    #[test]
    fn routine_status_unregister_waits_for_in_flight_callback() {
        let core = maho_core_new();
        assert!(!core.is_null());
        let (entered_tx, entered_rx) = std::sync::mpsc::sync_channel(1);
        let (release_tx, release_rx) = std::sync::mpsc::sync_channel(1);
        let context = Box::new(BlockingRoutineStatusContext {
            entered: entered_tx,
            release: std::sync::Mutex::new(release_rx),
        });
        let context_ptr = Box::into_raw(context);
        let token = unsafe {
            maho_routines_register_status_callback(core, context_ptr.cast(), block_routine_status)
        };
        assert_ne!(token, 0);

        let registry = routine_run_registry(core);
        let publish = std::thread::spawn(move || {
            registry.begin("r", maho_core::routines::RoutineRunSource::Manual);
        });
        entered_rx
            .recv_timeout(std::time::Duration::from_secs(5))
            .expect("callback entered");

        let core_key = core as usize;
        let (unregistered_tx, unregistered_rx) = std::sync::mpsc::sync_channel(1);
        let unregister = std::thread::spawn(move || {
            unsafe { maho_routines_unregister_status_callback(core_key as *mut MahoCore, token) };
            unregistered_tx.send(()).expect("unregistered signal");
        });
        assert!(unregistered_rx
            .recv_timeout(std::time::Duration::from_millis(100))
            .is_err());
        release_tx.send(()).expect("release callback");
        unregistered_rx
            .recv_timeout(std::time::Duration::from_secs(5))
            .expect("unregister completed");
        publish.join().expect("publisher completed");
        unregister.join().expect("unregister completed");

        unsafe {
            drop(Box::from_raw(context_ptr));
            maho_core_free(core);
        }
    }

    struct SelfUnregisterContext {
        core: usize,
        token: std::sync::atomic::AtomicU64,
        calls: std::sync::atomic::AtomicU64,
    }

    unsafe extern "C" fn self_unregister_tool_callback(
        user_data: *mut c_void,
        _json: *const c_char,
    ) {
        let context = unsafe { &*(user_data as *const SelfUnregisterContext) };
        context
            .calls
            .fetch_add(1, std::sync::atomic::Ordering::SeqCst);
        let token = context.token.load(std::sync::atomic::Ordering::SeqCst);
        unsafe {
            maho_core_unregister_tool_availability_callback(context.core as *mut MahoCore, token);
        }
    }

    #[test]
    fn tool_callback_can_unregister_itself_without_deadlock() {
        let core = maho_core_new() as usize;
        assert_ne!(core, 0);
        let context = Box::new(SelfUnregisterContext {
            core,
            token: std::sync::atomic::AtomicU64::new(0),
            calls: std::sync::atomic::AtomicU64::new(0),
        });
        let context_ptr = Box::into_raw(context);
        let token = unsafe {
            maho_core_register_tool_availability_callback(
                core as *mut MahoCore,
                context_ptr.cast(),
                self_unregister_tool_callback,
            )
        };
        assert_ne!(token, 0);
        unsafe {
            (*context_ptr)
                .token
                .store(token, std::sync::atomic::Ordering::SeqCst)
        };

        process_split_callbacks_for_updates(&[
            maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged {
                workspace_id: "workspace".to_string(),
                server_name: "server".to_string(),
                available: true,
            },
        ]);
        process_split_callbacks_for_updates(&[
            maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged {
                workspace_id: "workspace".to_string(),
                server_name: "server".to_string(),
                available: false,
            },
        ]);

        assert_eq!(
            unsafe {
                (*context_ptr)
                    .calls
                    .load(std::sync::atomic::Ordering::SeqCst)
            },
            1
        );
        unsafe {
            drop(Box::from_raw(context_ptr));
            maho_core_free(core as *mut MahoCore);
        }
    }

    struct NestedSelfUnregisterContext {
        token: std::sync::atomic::AtomicU64,
        calls: std::sync::atomic::AtomicU64,
    }

    unsafe extern "C" fn nested_self_unregister_tool_callback(
        user_data: *mut c_void,
        _json: *const c_char,
    ) {
        let context = unsafe { &*(user_data as *const NestedSelfUnregisterContext) };
        let call = context
            .calls
            .fetch_add(1, std::sync::atomic::Ordering::SeqCst);
        if call == 0 {
            process_split_callbacks_for_updates(&[
                maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged {
                    workspace_id: "nested".to_string(),
                    server_name: "server".to_string(),
                    available: false,
                },
            ]);
        } else {
            maho_core_unregister_tool_availability_callback(
                ptr::null_mut(),
                context.token.load(std::sync::atomic::Ordering::SeqCst),
            );
        }
    }

    #[test]
    fn nested_tool_callback_can_unregister_itself_without_deadlock() {
        let core = maho_core_new();
        assert!(!core.is_null());
        let context = Box::into_raw(Box::new(NestedSelfUnregisterContext {
            token: std::sync::atomic::AtomicU64::new(0),
            calls: std::sync::atomic::AtomicU64::new(0),
        }));
        let token = unsafe {
            maho_core_register_tool_availability_callback(
                core,
                context.cast(),
                nested_self_unregister_tool_callback,
            )
        };
        assert_ne!(token, 0);
        unsafe {
            (*context)
                .token
                .store(token, std::sync::atomic::Ordering::SeqCst)
        };
        process_split_callbacks_for_updates(&[
            maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged {
                workspace_id: "outer".to_string(),
                server_name: "server".to_string(),
                available: true,
            },
        ]);
        assert_eq!(
            unsafe { (*context).calls.load(std::sync::atomic::Ordering::SeqCst) },
            2
        );
        unsafe {
            drop(Box::from_raw(context));
            maho_core_free(core);
        }
    }

    #[test]
    fn callback_unregister_succeeds_after_core_shutdown() {
        let core = maho_core_new();
        assert!(!core.is_null());
        let context = Box::into_raw(Box::new(SelfUnregisterContext {
            core: 0,
            token: std::sync::atomic::AtomicU64::new(0),
            calls: std::sync::atomic::AtomicU64::new(0),
        }));
        let token = unsafe {
            maho_core_register_tool_availability_callback(
                core,
                context.cast(),
                self_unregister_tool_callback,
            )
        };
        assert_ne!(token, 0);
        unsafe { maho_core_free(core) };
        unsafe { maho_core_unregister_tool_availability_callback(ptr::null_mut(), token) };
        process_split_callbacks_for_updates(&[
            maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged {
                workspace_id: "after-shutdown".to_string(),
                server_name: "server".to_string(),
                available: true,
            },
        ]);
        assert_eq!(
            unsafe { (*context).calls.load(std::sync::atomic::Ordering::SeqCst) },
            0
        );
        unsafe { drop(Box::from_raw(context)) };
    }

    struct ReentrantCallbackContext {
        core: usize,
        entered: std::sync::Barrier,
        proceed: std::sync::Barrier,
    }

    unsafe extern "C" fn reentrant_tool_callback(user_data: *mut c_void, _json: *const c_char) {
        let context = unsafe { &*(user_data as *const ReentrantCallbackContext) };
        context.entered.wait();
        context.proceed.wait();
        let result = unsafe {
            maho_core_boost_list_for_domain(context.core as *mut MahoCore, c"example.com".as_ptr())
        };
        if !result.is_null() {
            unsafe { maho_string_free(result) };
        }
    }

    #[test]
    fn unregister_waits_without_blocking_callback_ffi_reentry() {
        let core = maho_core_new() as usize;
        assert_ne!(core, 0);
        let context = FfiArc::new(ReentrantCallbackContext {
            core,
            entered: std::sync::Barrier::new(2),
            proceed: std::sync::Barrier::new(2),
        });
        let context_ptr = FfiArc::into_raw(FfiArc::clone(&context)) as *mut c_void;
        let token = unsafe {
            maho_core_register_tool_availability_callback(
                core as *mut MahoCore,
                context_ptr,
                reentrant_tool_callback,
            )
        };
        assert_ne!(token, 0);

        let dispatch = std::thread::spawn(|| {
            process_split_callbacks_for_updates(&[
                maho_types::events::core_update::CoreUpdate::ToolAvailabilityChanged {
                    workspace_id: "workspace".to_string(),
                    server_name: "server".to_string(),
                    available: true,
                },
            ]);
        });
        context.entered.wait();
        let unregister = std::thread::spawn(move || unsafe {
            maho_core_unregister_tool_availability_callback(core as *mut MahoCore, token);
        });
        context.proceed.wait();

        dispatch.join().expect("dispatch");
        unregister.join().expect("unregister");
        unsafe {
            drop(FfiArc::from_raw(
                context_ptr as *const ReentrantCallbackContext,
            ));
            maho_core_free(core as *mut MahoCore);
        }
    }

    #[test]
    fn ffi_gate_serializes_concurrent_core_mutations() {
        // `maho_core_boost_create` returns null while the import gate is raised.
        let _gate_idle = crate::common::import_gate_idle();
        let core = maho_core_new() as usize;
        assert_ne!(core, 0);
        let workers: Vec<_> = (0..8)
            .map(|worker| {
                std::thread::spawn(move || {
                    let core = core as *mut MahoCore;
                    for index in 0..25 {
                        let domain = CString::new(format!("worker-{worker}.example")).unwrap();
                        let name = CString::new(format!("Boost {index}")).unwrap();
                        let result =
                            unsafe { maho_core_boost_create(core, domain.as_ptr(), name.as_ptr()) };
                        assert!(!result.is_null());
                        unsafe { maho_string_free(result) };
                    }
                })
            })
            .collect();
        for worker in workers {
            worker.join().unwrap();
        }
        unsafe { maho_core_free(core as *mut MahoCore) };
    }

    #[test]
    fn import_admission_waits_for_existing_ffi_scope() {
        let _gate_lock = crate::common::import_gate_exclusive();
        import_gate::exit();
        let entered = FfiArc::new(std::sync::Barrier::new(2));
        let release = FfiArc::new(std::sync::Barrier::new(2));
        let holder_entered = FfiArc::clone(&entered);
        let holder_release = FfiArc::clone(&release);
        let holder = std::thread::spawn(move || {
            with_ffi_serialization(|| {
                holder_entered.wait();
                holder_release.wait();
            });
        });
        entered.wait();

        let admitted = FfiArc::new(std::sync::atomic::AtomicBool::new(false));
        let admitted_worker = FfiArc::clone(&admitted);
        let importer = std::thread::spawn(move || {
            let result = with_ffi_serialization(import_gate::enter);
            admitted_worker.store(result, std::sync::atomic::Ordering::SeqCst);
        });

        // Observe the exact event "importer is parked on the FFI gate" rather
        // than reading `admitted` at an arbitrary moment: without this the
        // check below would pass simply because the importer had not started
        // yet, and a gate that never serialized would still look ok.
        //
        // Nothing here may assert: the holder still owns the process-wide FFI
        // serialization mutex and only `release.wait()` frees it, so a panic
        // before that point would strand every later FFI call in this binary.
        // Record the observations, unwind the fixture, then judge.
        let blocked_on_gate = gate_probe::wait_until_blocked(
            importer.thread().id(),
            std::time::Duration::from_secs(30),
        );
        let admitted_while_scope_held = admitted.load(std::sync::atomic::Ordering::SeqCst);

        // `entered.wait()` already paired with the holder, so the holder is
        // guaranteed to be parked on `holder_release` and this cannot hang.
        release.wait();
        let holder_exit = holder.join();
        let importer_exit = importer.join();
        let admitted_after_release = admitted.load(std::sync::atomic::Ordering::SeqCst);
        // The gate is also lowered by `ImportGateExclusive::drop` on any panic.
        import_gate::exit();

        assert!(holder_exit.is_ok(), "holder thread panicked");
        assert!(importer_exit.is_ok(), "importer thread panicked");
        assert!(
            blocked_on_gate,
            "importer must block on the FFI serialization gate while a scope is held"
        );
        assert!(
            !admitted_while_scope_held,
            "importer must not be admitted while an FFI scope is held"
        );
        assert!(
            admitted_after_release,
            "importer must be admitted once the FFI scope is released"
        );
    }

    #[test]
    fn active_boost_clear_reports_import_rejection() {
        let _gate_lock = crate::common::import_gate_exclusive();
        let core = maho_core_new();
        assert!(!core.is_null());
        let domain = CString::new("example.com").unwrap();

        // Record, then tear down, then judge: a panic between `enter()` and
        // `exit()` would leave the process-global gate raised for every later
        // test. `ImportGateExclusive::drop` is the backstop, this is the
        // ordinary path.
        let entered = import_gate::enter();
        let rejected = !unsafe { maho_core_boost_set_active(core, domain.as_ptr(), ptr::null()) };
        import_gate::exit();
        unsafe { maho_core_free(core) };

        assert!(entered, "test must own the import gate");
        assert!(
            rejected,
            "boost_set_active must report rejection while an import is active"
        );
    }
    use std::sync::Mutex;

    #[derive(Debug, PartialEq, Eq)]
    enum CallbackLeaseEvent {
        Complete,
        Error,
        Release,
    }

    unsafe extern "C" fn record_complete_then_release(
        user_data: *mut c_void,
        _full_text: *const c_char,
        _tool_calls_json: *const c_char,
    ) {
        // SAFETY: each test passes a live `Mutex<Vec<CallbackLeaseEvent>>` as `user_data`
        // and drops its lease before the event log leaves scope.
        let events = unsafe { &*(user_data as *const Mutex<Vec<CallbackLeaseEvent>>) };
        events.lock().unwrap().push(CallbackLeaseEvent::Complete);
    }

    unsafe extern "C" fn record_error_then_release(user_data: *mut c_void, _error: *const c_char) {
        // SAFETY: each test passes a live `Mutex<Vec<CallbackLeaseEvent>>` as `user_data`
        // and drops its lease before the event log leaves scope.
        let events = unsafe { &*(user_data as *const Mutex<Vec<CallbackLeaseEvent>>) };
        events.lock().unwrap().push(CallbackLeaseEvent::Error);
    }

    unsafe extern "C" fn record_release(user_data: *mut c_void) {
        // SAFETY: each test passes a live `Mutex<Vec<CallbackLeaseEvent>>` as `user_data`
        // and drops every lease before the event log leaves scope.
        let events = unsafe { &*(user_data as *const Mutex<Vec<CallbackLeaseEvent>>) };
        events.lock().unwrap().push(CallbackLeaseEvent::Release);
    }

    #[test]
    fn turn_callback_lease_releases_once_after_complete_callback() {
        // Given: the FFI caller's callback context and a planned private turn lease.
        let events = Mutex::new(Vec::new());
        let user_data = (&events as *const Mutex<Vec<CallbackLeaseEvent>>)
            .cast_mut()
            .cast();
        let lease = TurnCallbackLease::new(record_release, user_data);
        lease.arm();

        // When: a successful turn invokes the C ABI terminal callback, then ends.
        unsafe { record_complete_then_release(user_data, ptr::null(), ptr::null()) };
        drop(lease);

        // Then: the terminal callback happens before exactly one release.
        assert_eq!(
            *events.lock().unwrap(),
            vec![CallbackLeaseEvent::Complete, CallbackLeaseEvent::Release]
        );
    }

    #[test]
    fn credential_error_callback_envelopes_are_versioned_and_secret_free() {
        // Given: every typed credential error and key-like sentinel input.
        let sentinel = "S3NTINEL-credential-value";
        let cases = [
            (
                maho_agent::CredentialError::ProviderNotConfigured,
                "provider_not_configured",
            ),
            (
                maho_agent::CredentialError::CredentialUnusable,
                "credential_unusable",
            ),
            (
                maho_agent::CredentialError::SecureStoreUnavailable,
                "secure_store_unavailable",
            ),
            (
                maho_agent::CredentialError::CredentialDecryptFailed,
                "credential_decrypt_failed",
            ),
            (
                maho_agent::CredentialError::ManagedAuthUnavailable,
                "managed_auth_unavailable",
            ),
            (
                maho_agent::CredentialError::UnsupportedProvider,
                "unsupported_provider",
            ),
        ];

        for (error, code) in cases {
            // When: the FFI callback representation is selected.
            let display = error.to_string();
            let envelope = agent_error_callback_text(&error.into());
            let envelope_json: serde_json::Value =
                serde_json::from_str(&envelope).expect("credential envelope is valid JSON");

            // Then: only the versioned allowlisted protocol fields are exposed.
            assert_eq!(envelope_json["version"], 1);
            assert_eq!(envelope_json["kind"], "credential_error");
            assert_eq!(envelope_json["code"], code);
            assert!(!display.contains(sentinel));
            assert!(!envelope.contains(sentinel));
        }
    }

    #[test]
    fn unrelated_agent_errors_keep_the_legacy_callback_text() {
        // Given: a failure that is not a typed credential error.
        let error = maho_agent::AgentError::ExecutionError("legacy failure".to_string());

        // When: it crosses the existing callback boundary.
        let callback_text = agent_error_callback_text(&error);

        // Then: the established plain-text behavior remains intact.
        assert_eq!(callback_text, "Execution error: legacy failure");
    }

    #[test]
    fn turn_callback_lease_releases_once_after_error_callback() {
        // Given: the FFI caller's callback context and a planned private turn lease.
        let events = Mutex::new(Vec::new());
        let user_data = (&events as *const Mutex<Vec<CallbackLeaseEvent>>)
            .cast_mut()
            .cast();
        let lease = TurnCallbackLease::new(record_release, user_data);
        lease.arm();

        // When: a failed turn invokes the C ABI terminal callback, then ends.
        unsafe { record_error_then_release(user_data, ptr::null()) };
        drop(lease);

        // Then: the terminal callback happens before exactly one release.
        assert_eq!(
            *events.lock().unwrap(),
            vec![CallbackLeaseEvent::Error, CallbackLeaseEvent::Release]
        );
    }

    #[test]
    fn turn_callback_lease_releases_once_when_accepted_turn_is_dropped() {
        // Given: an accepted turn which owns the planned private callback lease.
        let events = Mutex::new(Vec::new());
        let user_data = (&events as *const Mutex<Vec<CallbackLeaseEvent>>)
            .cast_mut()
            .cast();
        let lease = TurnCallbackLease::new(record_release, user_data);
        lease.arm();

        // When: the task is aborted or dropped before either terminal callback runs.
        drop(lease);

        // Then: Drop releases the caller context exactly once.
        assert_eq!(*events.lock().unwrap(), vec![CallbackLeaseEvent::Release]);
    }

    #[test]
    fn terminal_callback_helper_runs_before_turn_callback_lease_release() {
        let events = Mutex::new(Vec::new());
        let user_data = (&events as *const Mutex<Vec<CallbackLeaseEvent>>)
            .cast_mut()
            .cast();
        let lease = Arc::new(TurnCallbackLease::new(record_release, user_data));
        lease.arm();

        invoke_with_turn_callback_lease(Some(lease), || {
            events.lock().unwrap().push(CallbackLeaseEvent::Complete);
        });

        assert_eq!(
            *events.lock().unwrap(),
            vec![CallbackLeaseEvent::Complete, CallbackLeaseEvent::Release]
        );
    }

    #[test]
    fn session_callback_lease_releases_after_final_runtime_callback_clone_drops() {
        // Given: a session-owned lease plus the clone retained by a runtime callback.
        let events = Mutex::new(Vec::new());
        let user_data = (&events as *const Mutex<Vec<CallbackLeaseEvent>>)
            .cast_mut()
            .cast();
        let session_lease = SessionCallbackLease::new(Some(record_release), user_data);
        let runtime_callback_lease = Arc::clone(&session_lease);
        session_lease.arm();

        // When: session teardown drops its owner before the runtime callback is destroyed.
        drop(session_lease);
        assert!(events.lock().unwrap().is_empty());
        drop(runtime_callback_lease);

        // Then: the release callback runs only after the final runtime-clone drops.
        assert_eq!(*events.lock().unwrap(), vec![CallbackLeaseEvent::Release]);
    }

    #[test]
    fn unarmed_turn_callback_lease_drops_without_release() {
        // Given: a leased turn that has not yet transferred ownership to Rust.
        let events = Mutex::new(Vec::new());
        let user_data = (&events as *const Mutex<Vec<CallbackLeaseEvent>>)
            .cast_mut()
            .cast();
        let lease = TurnCallbackLease::new(record_release, user_data);

        // When: the pending turn is rejected or unwinds before acceptance.
        drop(lease);

        // Then: the caller retains the context because the lease was never armed.
        assert!(events.lock().unwrap().is_empty());
    }

    #[test]
    fn unarmed_session_callback_lease_drops_without_release() {
        // Given: a leased session that has not yet returned a non-null handle.
        let events = Mutex::new(Vec::new());
        let user_data = (&events as *const Mutex<Vec<CallbackLeaseEvent>>)
            .cast_mut()
            .cast();
        let lease = SessionCallbackLease::new(Some(record_release), user_data);

        // When: session setup fails before acceptance.
        drop(lease);

        // Then: no release callback runs for the unaccepted session context.
        assert!(events.lock().unwrap().is_empty());
    }

    #[test]
    fn none_session_callback_lease_drops_safely_without_callback() {
        let lease = SessionCallbackLease::new(None, ptr::null_mut());
        lease.arm();
        drop(lease);
    }

    #[test]
    fn accepted_leased_turn_latch_signals_before_backend_admission() {
        // Given: an accepted leased turn whose backend registration has not run yet.
        let accepted_turns = Arc::new(AcceptedLeasedTurns {
            current: Mutex::new(None),
        });
        let accepted_turn = accepted_turns.accept();
        let latch = accepted_turn.latch();

        // When: cancellation is requested synchronously at the FFI boundary.
        let signaled = accepted_turns.signal_current();

        // Then: cancel reports the accepted turn and its backend latch is already set.
        assert!(signaled);
        assert!(latch.load(AtomicOrdering::Acquire));
    }

    #[test]
    fn prior_leased_turn_drop_does_not_clear_newer_latch() {
        // Given: a session accepts a replacement leased turn before the prior future exits.
        let accepted_turns = Arc::new(AcceptedLeasedTurns {
            current: Mutex::new(None),
        });
        let prior_turn = accepted_turns.accept();
        let current_turn = accepted_turns.accept();
        let current_latch = current_turn.latch();

        // When: the prior future finishes after the current turn was accepted.
        drop(prior_turn);
        let signaled = accepted_turns.signal_current();

        // Then: only the identity-matched current latch remains cancellable.
        assert!(signaled);
        assert!(current_latch.load(AtomicOrdering::Acquire));
        drop(current_turn);
        assert!(!accepted_turns.signal_current());
    }

    #[test]
    fn leased_send_rejection_does_not_transfer_release_ownership() {
        // Given: a caller-owned release context and an invalid session pointer.
        let events = Mutex::new(Vec::new());
        let release_user_data = (&events as *const Mutex<Vec<CallbackLeaseEvent>>)
            .cast_mut()
            .cast();

        // When: the leased call is rejected before turn ownership is accepted.
        let accepted = unsafe {
            maho_agent_send_message_leased(
                ptr::null_mut(),
                ptr::null(),
                None,
                None,
                None,
                None,
                None,
                None,
                ptr::null_mut(),
                record_release,
                release_user_data,
            )
        };

        // Then: the caller keeps its context and the release callback never runs.
        assert!(!accepted);
        assert!(events.lock().unwrap().is_empty());
    }

    #[test]
    fn legacy_agent_exports_have_no_release_parameters() {
        let source = include_str!("lib.rs");

        let create_signature = source
            .split_once("pub unsafe extern \"C\" fn maho_agent_create_session(")
            .unwrap()
            .1
            .split_once(") -> *mut MahoAgentSession")
            .unwrap()
            .0;
        let send_signature = source
            .split_once("pub unsafe extern \"C\" fn maho_agent_send_message(")
            .unwrap()
            .1
            .split_once(") -> bool")
            .unwrap()
            .0;
        let simple_signature = source
            .split_once("pub unsafe extern \"C\" fn maho_agent_send_message_simple(")
            .unwrap()
            .1
            .split_once(") -> bool")
            .unwrap()
            .0;

        assert!(!create_signature.contains("release"));
        assert!(!send_signature.contains("release"));
        assert!(!simple_signature.contains("release"));
        assert!(source.contains("pub type MahoAgentReleaseCallback"));
        assert!(source.contains("on_session_release: MahoAgentReleaseCallback"));
        assert!(source.contains("on_release: MahoAgentReleaseCallback"));
        assert!(!source.contains(concat!("Option<", "MahoAgentReleaseCallback>")));
    }

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

        let _gate_idle = crate::common::import_gate_idle();
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

        let _gate_idle = crate::common::import_gate_idle();
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

        let _gate_idle = crate::common::import_gate_idle();

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

        // The spawned thread below injects the process-wide SQLCipher test
        // key; hold the key lock so parallel key-flipping tests cannot
        // corrupt this test's (or their own) database opens.
        let _key_lock = crate::common::sqlcipher_test_key_lock();

        // Run on a freshly-spawned OS thread that is guaranteed to have NO
        // ambient tokio runtime, mirroring the Chromium browser-thread call site.
        let handle = std::thread::spawn(|| {
            let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
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
        let _gate_idle = crate::common::import_gate_idle();
        let _key_lock = crate::common::sqlcipher_test_key_lock();
        let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
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

    /// `maho_routines_update_custom` must patch an existing custom routine in
    /// place (same id, untouched fields survive), switch trigger kinds by
    /// clearing the other, reject invalid triggers with -1, and return -2 for
    /// unknown or built-in ids.
    #[test]
    fn custom_routine_ffi_update_patches_in_place() {
        let _gate_idle = crate::common::import_gate_idle();
        let _key_lock = crate::common::sqlcipher_test_key_lock();
        let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
        use maho_core::maho_core::MahoCore;
        use std::ffi::{CStr, CString};

        let db = std::env::temp_dir().join(format!(
            "maho_routines_ffi_update_{}.sqlite",
            std::process::id()
        ));
        let db_path = db.to_string_lossy().to_string();
        let mut core = MahoCore::new().with_storage(&db_path);
        let core_ptr: *mut MahoCore = &mut core;

        let list_json = || -> String {
            let ptr = unsafe { maho_routines_list_all(core_ptr) };
            assert!(!ptr.is_null(), "list_all must return JSON");
            let json = unsafe { CStr::from_ptr(ptr).to_str().unwrap().to_string() };
            unsafe { maho_string_free(ptr) };
            json
        };

        // Create a cron-scheduled custom routine.
        let create_json = CString::new(
            r#"{"name":"Patch Me","prompt":"Original prompt","schedule":"0 9 * * *"}"#,
        )
        .unwrap();
        assert_eq!(
            unsafe { maho_routines_create_custom(core_ptr, create_json.as_ptr()) },
            0
        );
        let views: Vec<serde_json::Value> = serde_json::from_str(&list_json()).unwrap();
        let rid = views
            .iter()
            .find(|v| v["source"] == "custom")
            .map(|v| v["id"].as_str().unwrap().to_string())
            .unwrap();
        let id_c = CString::new(rid.clone()).unwrap();

        // Partial update: rename + disable only. Prompt and schedule survive,
        // and the id is unchanged (no duplicate row).
        let patch = CString::new(r#"{"name":"Patched","enabled":false}"#).unwrap();
        assert_eq!(
            unsafe { maho_routines_update_custom(core_ptr, id_c.as_ptr(), patch.as_ptr()) },
            0
        );
        let views: Vec<serde_json::Value> = serde_json::from_str(&list_json()).unwrap();
        assert_eq!(views.len(), 5, "update must not create a new row");
        let custom = views.iter().find(|v| v["id"] == rid).unwrap();
        assert_eq!(custom["name"], "Patched");
        assert_eq!(custom["cron"], "0 9 * * *", "untouched schedule survives");
        assert_eq!(custom["enabled"], false);

        // Switching to an event trigger clears the cron schedule.
        let to_event = CString::new(r#"{"trigger":"on_startup"}"#).unwrap();
        assert_eq!(
            unsafe { maho_routines_update_custom(core_ptr, id_c.as_ptr(), to_event.as_ptr()) },
            0
        );
        let views: Vec<serde_json::Value> = serde_json::from_str(&list_json()).unwrap();
        let custom = views.iter().find(|v| v["id"] == rid).unwrap();
        assert!(custom["cron"].is_null(), "cron must be cleared by event switch");
        assert_eq!(custom["trigger"], "on_startup");

        // Invalid cron must be rejected.
        let bad = CString::new(r#"{"schedule":"not a cron"}"#).unwrap();
        assert_eq!(
            unsafe { maho_routines_update_custom(core_ptr, id_c.as_ptr(), bad.as_ptr()) },
            -1
        );

        // Unknown ids and built-in ids are not updatable (-2).
        let missing = CString::new("nonexistent-id").unwrap();
        let any = CString::new(r#"{"name":"X"}"#).unwrap();
        assert_eq!(
            unsafe { maho_routines_update_custom(core_ptr, missing.as_ptr(), any.as_ptr()) },
            -2
        );
        let builtin = CString::new("morning_briefing").unwrap();
        assert_eq!(
            unsafe { maho_routines_update_custom(core_ptr, builtin.as_ptr(), any.as_ptr()) },
            -2
        );

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

    /// Routine inbox query FFI: `maho_routines_history` and
    /// `maho_routines_latest` must return recorded runs (persisted via the
    /// durable ledger) as JSON, including a failed run whose error text is
    /// preserved. Seeds the same sqlite the core is backed by, then queries
    /// through the FFI.
    #[test]
    fn routines_history_and_latest_query_recorded_runs() {
        let _gate_idle = crate::common::import_gate_idle();
        let _key_lock = crate::common::sqlcipher_test_key_lock();
        let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
        use maho_core::maho_core::MahoCore;
        use std::ffi::{CStr, CString};

        let db = std::env::temp_dir().join(format!(
            "maho_routines_inbox_query_{}.sqlite",
            std::process::id()
        ));
        let db_path = db.to_string_lossy().to_string();
        let mut core = MahoCore::new().with_storage(&db_path);
        let core_ptr: *mut MahoCore = &mut core;

        // Seed two runs (one success, one failure) on the same db the FFI reads.
        {
            let storage = maho_storage::sqlite::SqliteStorage::open(&db_path).unwrap();
            storage
                .record_routine_result("weekly_tab_tidy", 100, true, "tidied 12 tabs", "scheduled")
                .unwrap();
            storage
                .record_routine_result("weekly_tab_tidy", 200, false, "agent error", "manual")
                .unwrap();
        }

        // history for the routine: newest-first, both rows present.
        let rid = CString::new("weekly_tab_tidy").unwrap();
        let hist_ptr = unsafe { maho_routines_history(core_ptr, rid.as_ptr(), 10) };
        assert!(!hist_ptr.is_null(), "history must return JSON");
        let hist_json = unsafe { CStr::from_ptr(hist_ptr).to_str().unwrap().to_string() };
        unsafe { maho_string_free(hist_ptr) };
        let records: Vec<serde_json::Value> = serde_json::from_str(&hist_json).unwrap();
        assert_eq!(records.len(), 2);
        assert_eq!(records[0]["content"], "agent error", "newest first");
        assert_eq!(records[0]["success"], false);
        assert_eq!(records[0]["source"], "manual");

        // history for all routines (null id) also returns rows.
        let all_ptr = unsafe { maho_routines_history(core_ptr, std::ptr::null(), 0) };
        let all_json = unsafe { CStr::from_ptr(all_ptr).to_str().unwrap().to_string() };
        unsafe { maho_string_free(all_ptr) };
        let all: Vec<serde_json::Value> = serde_json::from_str(&all_json).unwrap();
        assert_eq!(all.len(), 2);

        // latest returns the single most recent run.
        let latest_ptr = unsafe { maho_routines_latest(core_ptr, rid.as_ptr()) };
        assert!(!latest_ptr.is_null(), "latest must return JSON");
        let latest_json = unsafe { CStr::from_ptr(latest_ptr).to_str().unwrap().to_string() };
        unsafe { maho_string_free(latest_ptr) };
        let latest: serde_json::Value = serde_json::from_str(&latest_json).unwrap();
        assert_eq!(latest["content"], "agent error");
        assert_eq!(latest["ranAt"], 200);

        // A routine that never ran returns null from latest.
        let ghost = CString::new("never_ran").unwrap();
        let none_ptr = unsafe { maho_routines_latest(core_ptr, ghost.as_ptr()) };
        assert!(none_ptr.is_null(), "latest of a never-run routine is null");

        let _ = std::fs::remove_file(&db);
    }

    /// C2: registering a settings-change callback and then calling `maho_core_set_density`
    /// must invoke the callback with the current settings JSON. Proves the emit
    /// (maho-core global bus) + dispatch (FFI SettingsChanged arm) wiring end to end.
    #[test]
    fn set_density_fires_settings_change_callback() {
        use maho_core::maho_core::MahoCore;
        use std::ffi::CString;
        use std::sync::atomic::{AtomicU32, Ordering as AtomicOrdering};

        let _gate_idle = crate::common::import_gate_idle();

        unsafe extern "C" fn on_settings_change(user_data: *mut c_void, json: *const c_char) {
            if user_data.is_null() {
                return;
            }
            // SAFETY: `user_data` is the AtomicU32 pointer supplied at registration and is
            // kept alive by the test until after unregistration.
            let counter = unsafe { &*(user_data as *const AtomicU32) };
            counter.fetch_add(1, AtomicOrdering::SeqCst);
            if !json.is_null() {
                // SAFETY: `json` is a valid NUL-terminated C string owned by the dispatcher
                // for the duration of this call.
                let parsed = unsafe { CStr::from_ptr(json) }.to_str();
                assert!(parsed.is_ok(), "settings JSON must be valid UTF-8");
            }
        }

        let counter = Box::into_raw(Box::new(AtomicU32::new(0)));
        let mut core = MahoCore::new();
        let core_ptr: *mut MahoCore = &mut core;

        // SAFETY: `core_ptr` is valid; `counter` outlives the registration below.
        let token = unsafe {
            maho_core_register_settings_change_callback(
                core_ptr,
                counter as *mut c_void,
                on_settings_change,
            )
        };

        let density = CString::new("compact").unwrap();
        // SAFETY: `core_ptr` and `density` are valid for the duration of this call.
        unsafe { maho_core_set_density(core_ptr, density.as_ptr()) };

        // SAFETY: `counter` is still alive (not yet reclaimed).
        let fired = unsafe { &*counter }.load(AtomicOrdering::SeqCst);
        assert!(
            fired >= 1,
            "settings-change callback must fire on set_density"
        );

        // SAFETY: unregister before reclaiming `counter` so no dispatch can dereference it.
        unsafe { maho_core_unregister_settings_change_callback(core_ptr, token) };
        // SAFETY: no registered callback references `counter` after unregistration.
        unsafe { drop(Box::from_raw(counter)) };
    }
}

// === Vault FFI C ABI ===

use maho_core::vault_manager::{
    OriginMatchPolicy, VaultCrudError, VaultLoginInput, VaultLoginUpdate, VaultManagerError,
};
use maho_types::vault::{
    VaultAuditPageRequest, VaultErrorCode, VaultItemSearchRequest, VaultPolicy, VaultRevision,
};

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct VaultFfiError {
    code: VaultErrorCode,
    message: String,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct VaultFfiResponse<T> {
    ok: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    data: Option<T>,
    #[serde(skip_serializing_if = "Option::is_none")]
    error: Option<VaultFfiError>,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct VaultMutationResult {
    id: VaultItemId,
    #[serde(serialize_with = "serialize_vault_revision_as_decimal_string")]
    revision: VaultRevision,
}

impl From<VaultItemPublicDto> for VaultMutationResult {
    fn from(item: VaultItemPublicDto) -> Self {
        Self {
            id: item.id,
            revision: item.revision,
        }
    }
}

fn serialize_vault_revision_as_decimal_string<S>(
    revision: &VaultRevision,
    serializer: S,
) -> Result<S::Ok, S::Error>
where
    S: serde::Serializer,
{
    serializer.serialize_str(&revision.value().to_string())
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultInitializeRequest {
    master_passphrase: String,
    recovery_secret: String,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultUnlockRequest {
    master_passphrase: String,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultRecoveryUnlockRequest {
    recovery_secret: String,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultLoginRequest {
    #[serde(default)]
    notes: Option<String>,
    metadata: VaultItemPublicMetadata,
    username: String,
    password: String,
    /// Task 4: optional browser-form detail persisted inside the encrypted
    /// record so a Chromium `PasswordForm` round-trips. Absent for callers that
    /// have no form context (settings, importers).
    #[serde(default)]
    form_details: Option<maho_core::vault_manager::VaultCredentialFormDetails>,
}

fn deserialize_vault_revision<'de, D>(deserializer: D) -> Result<VaultRevision, D::Error>
where
    D: serde::Deserializer<'de>,
{
    struct VaultRevisionVisitor;

    impl serde::de::Visitor<'_> for VaultRevisionVisitor {
        type Value = VaultRevision;

        fn expecting(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            formatter.write_str("a non-negative integer or canonical unsigned decimal string")
        }

        fn visit_u64<E>(self, value: u64) -> Result<Self::Value, E> {
            Ok(VaultRevision::new(value))
        }

        fn visit_i64<E>(self, value: i64) -> Result<Self::Value, E>
        where
            E: serde::de::Error,
        {
            u64::try_from(value)
                .map(VaultRevision::new)
                .map_err(|_| E::custom("Vault revision must not be negative"))
        }

        fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
        where
            E: serde::de::Error,
        {
            let canonical = value == "0"
                || (!value.starts_with('0')
                    && !value.is_empty()
                    && value.bytes().all(|byte| byte.is_ascii_digit()));
            if !canonical {
                return Err(E::custom(
                    "Vault revision must be a canonical unsigned decimal string",
                ));
            }
            value
                .parse::<u64>()
                .map(VaultRevision::new)
                .map_err(|_| E::custom("Vault revision is out of range"))
        }
    }

    deserializer.deserialize_any(VaultRevisionVisitor)
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct VaultLoginUpdateRequest {
    #[serde(default)]
    notes: Option<String>,
    id: VaultItemId,
    #[serde(
        default,
        alias = "expected_revision",
        deserialize_with = "deserialize_vault_revision"
    )]
    expected_revision: VaultRevision,
    metadata: VaultItemPublicMetadata,
    username: String,
    #[serde(default)]
    password: Option<String>,
    /// Absent leaves the stored form detail untouched; present replaces it.
    #[serde(default)]
    form_details: Option<maho_core::vault_manager::VaultCredentialFormDetails>,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct VaultDeleteRequest {
    id: VaultItemId,
    #[serde(
        default,
        alias = "expected_revision",
        deserialize_with = "deserialize_vault_revision"
    )]
    expected_revision: VaultRevision,
}

#[derive(Clone, Copy, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
enum VaultSecretActionRequest {
    Copy,
    Fill,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultUseSecretRequest {
    item_id: VaultItemId,
    /// Accepts both wire encodings the browser process emits: a JSON number
    /// (`maho_settings_password_helpers.cc`) and a canonical decimal string
    /// (`maho_password_store_backend.cc` and every `VaultMutationResult`
    /// echoed straight back from an add/update response).
    #[serde(
        alias = "expected_revision",
        deserialize_with = "deserialize_vault_revision"
    )]
    expected_revision: VaultRevision,
    action: VaultSecretActionRequest,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct VaultUseSecretResponse {}

#[derive(serde::Deserialize)]
#[serde(rename_all = "snake_case")]
enum VaultMatchPolicyRequest {
    Exact,
    Subdomain,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultMatchItemsRequest {
    request: VaultItemSearchRequest,
    policy: VaultMatchPolicyRequest,
}


#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultParityMutationRequest {
    id: VaultItemId,
    #[serde(alias = "expected_revision", deserialize_with = "deserialize_vault_revision")]
    expected_revision: VaultRevision,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultFavoriteRequest {
    id: VaultItemId,
    #[serde(deserialize_with = "deserialize_vault_revision")]
    expected_revision: VaultRevision,
    favorite: bool,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultNoteRequest {
    title: String,
    notes: String,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultNoteUpdateRequest {
    id: VaultItemId,
    #[serde(deserialize_with = "deserialize_vault_revision")]
    expected_revision: VaultRevision,
    title: String,
    notes: String,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultTotpRequest {
    id: VaultItemId,
    #[serde(deserialize_with = "deserialize_vault_revision")]
    expected_revision: VaultRevision,
    secret: String,
}

#[derive(serde::Deserialize)]
#[serde(deny_unknown_fields)]
struct VaultItemRequest { id: VaultItemId }

#[derive(serde::Deserialize)]
#[serde(deny_unknown_fields)]
struct VaultStrengthRequest { password: String }

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_trash_item_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let request = match unsafe { vault_parse_json::<VaultParityMutationRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match core.vault_trash_item(request.id, request.expected_revision) {
            Ok(result) => vault_ok(VaultMutationResult::from(result)),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_restore_item_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let request = match unsafe { vault_parse_json::<VaultParityMutationRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match core.vault_restore_item(request.id, request.expected_revision) {
            Ok(result) => vault_ok(VaultMutationResult::from(result)),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_set_favorite_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let request = match unsafe { vault_parse_json::<VaultFavoriteRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match core.vault_set_favorite(request.id, request.expected_revision, request.favorite) {
            Ok(result) => vault_ok(VaultMutationResult::from(result)),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_empty_trash_json(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        match core.vault_empty_trash() {
            Ok(result) => vault_ok(result),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_get_notes_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let request = match unsafe { vault_parse_json::<VaultItemRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match core.vault_get_notes(request.id) {
            Ok(result) => match core.vault_get_username(request.id) {
                Ok(username) => vault_ok(serde_json::json!({
                    "notes": result.as_str(),
                    "username": username.as_str(),
                })),
                Err(error) => vault_crud_error(error),
            },
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_add_secure_note_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let request = match unsafe { vault_parse_json::<VaultNoteRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match core.vault_add_secure_note(request.title, Zeroizing::new(request.notes)) {
            Ok(result) => vault_ok(VaultMutationResult::from(result)),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_update_secure_note_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let request = match unsafe { vault_parse_json::<VaultNoteUpdateRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match core.vault_update_secure_note(request.id, request.expected_revision, request.title, Zeroizing::new(request.notes)) {
            Ok(result) => vault_ok(VaultMutationResult::from(result)),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_set_login_totp_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let request = match unsafe { vault_parse_json::<VaultTotpRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match core.vault_set_login_totp(request.id, request.expected_revision, &Zeroizing::new(request.secret)) {
            Ok(result) => vault_ok(VaultMutationResult::from(result)),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_totp_code_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let request = match unsafe { vault_parse_json::<VaultItemRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match core.vault_totp_code(request.id, u64::try_from(chrono::Utc::now().timestamp()).unwrap_or(0)) {
            Ok(result) => vault_ok(result),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_health_report_json(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        match core.vault_health_report() {
            Ok(result) => vault_ok(result),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and readable NUL-terminated request.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_generate_password_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller guarantees the core and request remain valid for this call.
        if unsafe { vault_core(core) }.is_none() { return ptr::null_mut(); }
        // SAFETY: The request is caller-owned readable NUL-terminated UTF-8.
        let request = match unsafe { vault_parse_json::<maho_types::vault_generator::PasswordGeneratorOptions>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match maho_core::vault_generator::generate_password(&request) {
            Ok(result) => vault_ok(result),
            Err(error) => vault_error(VaultErrorCode::StorageFailure, error.to_string()),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and readable NUL-terminated request.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_password_strength_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller guarantees the core and request remain valid for this call.
        if unsafe { vault_core(core) }.is_none() { return ptr::null_mut(); }
        // SAFETY: The request is caller-owned readable NUL-terminated UTF-8.
        let request = match unsafe { vault_parse_json::<VaultStrengthRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        vault_ok(maho_core::vault_generator::estimate_strength(&Zeroizing::new(request.password)))
    }, ptr::null_mut())
}

fn vault_ok<T: serde::Serialize>(data: T) -> *mut c_char {
    to_json_cstring(&VaultFfiResponse {
        ok: true,
        data: Some(data),
        error: None,
    })
}

fn vault_error(code: VaultErrorCode, message: impl Into<String>) -> *mut c_char {
    to_json_cstring(&VaultFfiResponse::<()> {
        ok: false,
        data: None,
        error: Some(VaultFfiError {
            code,
            message: message.into(),
        }),
    })
}

fn vault_lifecycle_error(error: VaultManagerError) -> *mut c_char {
    let code = match error {
        VaultManagerError::VaultLocked => VaultErrorCode::Locked,
        VaultManagerError::Uninitialized => VaultErrorCode::Uninitialized,
        VaultManagerError::AlreadyInitialized => VaultErrorCode::InvalidCredentials,
        VaultManagerError::RateLimited => VaultErrorCode::RateLimited,
        VaultManagerError::InvalidCredentials => VaultErrorCode::InvalidCredentials,
        VaultManagerError::CorruptedVaultData => VaultErrorCode::UnsupportedSchemaVersion,
        // The vault file has no wrapped key slot for the requested unlock kind,
        // so this file cannot be opened that way: same class as CorruptedVaultData.
        VaultManagerError::NoAccountWrap => VaultErrorCode::UnsupportedSchemaVersion,
        VaultManagerError::Storage(_) => VaultErrorCode::StorageFailure,
        VaultManagerError::DeviceProtector(_) => VaultErrorCode::ProviderUnavailable,
        VaultManagerError::DeviceWrapper(_) => VaultErrorCode::StorageFailure,
        VaultManagerError::DeviceBindingMismatch => VaultErrorCode::InvalidCredentials,
        VaultManagerError::Crypto(_) => VaultErrorCode::InvalidCredentials,
    };
    vault_error(code, error.to_string())
}

fn vault_crud_error(error: VaultCrudError) -> *mut c_char {
    let code = match error {
        VaultCrudError::Locked => VaultErrorCode::Locked,
        VaultCrudError::ItemNotFound => VaultErrorCode::NotFound,
        VaultCrudError::RevisionConflict { .. } => VaultErrorCode::RevisionConflict,
        VaultCrudError::Duplicate { .. } => VaultErrorCode::RevisionConflict,
        VaultCrudError::InvalidOrigin { .. } => VaultErrorCode::InvalidOrigin,
        VaultCrudError::ItemKindMismatch | VaultCrudError::FieldMismatch => {
            VaultErrorCode::SecretFieldForbidden
        }
        VaultCrudError::MalformedPayload | VaultCrudError::Crypto => {
            VaultErrorCode::UnsupportedSchemaVersion
        }
        VaultCrudError::Storage(_) => VaultErrorCode::StorageFailure,
        _ => VaultErrorCode::StorageFailure,
    };
    vault_error(code, error.to_string())
}

fn vault_secret_buffer(secret: &Zeroizing<String>) -> *mut c_char {
    if secret.as_bytes().contains(&0) {
        return ptr::null_mut();
    }
    CString::new(secret.as_str())
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `core` must be null or a valid, uniquely borrowed `MahoCore` pointer and
/// `request_json` must be a valid NUL-terminated UTF-8 JSON string.
unsafe fn vault_use_login_secret(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> Result<Zeroizing<String>, *mut c_char> {
    // SAFETY: Category 8 (FFI boundary): the caller contract for this helper
    // requires a null or live uniquely borrowed `MahoCore` pointer.
    let Some(core) = (unsafe { vault_core(core) }) else {
        return Err(ptr::null_mut());
    };
    // SAFETY: Category 8 (FFI boundary): the caller contract requires a valid
    // NUL-terminated UTF-8 JSON request pointer for the duration of this call.
    let request = match unsafe { vault_parse_json::<VaultUseSecretRequest>(request_json) } {
        Ok(request) => request,
        Err(response) => return Err(response),
    };
    match request.action {
        VaultSecretActionRequest::Copy | VaultSecretActionRequest::Fill => core
            .vault_use_login_password_at_revision(request.item_id, request.expected_revision)
            .map_err(vault_crud_error),
    }
}

/// # Safety
/// `response` must be null or a string returned by `to_json_cstring`.
unsafe fn free_vault_error_response(response: *mut c_char) {
    if response.is_null() {
        return;
    }
    // SAFETY: Category 12 (invalid free): `response` comes from this crate's
    // JSON error allocation path and is consumed exactly once here.
    unsafe { maho_string_free(response) };
}

/// # Safety
/// `request_json` must be null or a valid NUL-terminated C string readable for its full length.
unsafe fn vault_parse_json<T: serde::de::DeserializeOwned>(
    request_json: *const c_char,
) -> Result<T, *mut c_char> {
    if request_json.is_null() {
        return Err(vault_error(
            VaultErrorCode::StorageFailure,
            "request JSON is required",
        ));
    }
    // SAFETY: Category 8 (FFI boundary): the public caller contract guarantees a live,
    // NUL-terminated pointer; this helper rejects null and validates UTF-8 before parsing.
    let request = match unsafe { CStr::from_ptr(request_json) }.to_str() {
        Ok(value) => value,
        Err(_) => return Err(vault_error(VaultErrorCode::StorageFailure, "invalid_utf8")),
    };
    serde_json::from_str(request).map_err(|error| {
        vault_error(
            VaultErrorCode::StorageFailure,
            format!("invalid JSON: {error}"),
        )
    })
}

/// # Safety
/// `core` must be null or a valid, uniquely borrowed `MahoCore` pointer owned by the caller.
unsafe fn vault_core(core: *mut MahoCore) -> Option<&'static mut MahoCore> {
    // SAFETY: Category 8 (FFI boundary): the public caller contract guarantees that a non-null
    // `core` points to a live, uniquely borrowed MahoCore for the duration of the call.
    unsafe { core.as_mut() }
}

/// Returns the secret-free Vault status owned by `core`.
///
/// # Safety
/// `core` must be a valid MahoCore pointer. The returned string is freed with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_status_json(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            vault_ok(core.vault_status())
        },
        ptr::null_mut()
    )
}

/// Returns the effective default agent Vault policy from durable storage.
///
/// # Safety
/// `core` must be a valid MahoCore pointer. The returned string is freed with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_get_policy_json(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            match core.vault_agent_policy_status() {
                Ok(policy) => vault_ok(policy),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Persists an agent Vault policy override and records a policy-change audit row.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_set_policy_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultPolicy>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.vault_set_agent_policy(request) {
                Ok(policy) => vault_ok(policy),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Returns a paged, public, secret-free Vault audit view.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_audit_page_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultAuditPageRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.vault_audit_page(&request) {
                Ok(page) => vault_ok(page),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Initializes the profile-owned Vault with independent user and recovery secrets.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_initialize_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultInitializeRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.initialize_vault(
                request.master_passphrase.as_bytes(),
                request.recovery_secret.as_bytes(),
            ) {
                Ok(()) => vault_ok(core.vault_status()),
                Err(error) => vault_lifecycle_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Unlocks the profile-owned Vault.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_unlock_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultUnlockRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.unlock_vault(request.master_passphrase.as_bytes()) {
                Ok(()) => vault_ok(core.vault_status()),
                Err(error) => vault_lifecycle_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Unlocks the profile-owned Vault with its independent recovery secret.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_unlock_with_recovery_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultRecoveryUnlockRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.unlock_vault_with_recovery(request.recovery_secret.as_bytes()) {
                Ok(()) => vault_ok(core.vault_status()),
                Err(error) => vault_lifecycle_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Locks the profile-owned Vault and revokes active grants.
///
/// # Safety
/// `core` must be a valid MahoCore pointer.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_lock_json(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            match core.lock_vault() {
                Ok(()) => vault_ok(core.vault_status()),
                Err(error) => vault_lifecycle_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Adds a durable login record and returns only its public metadata.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_add_login_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultLoginRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
             let mut form_details = request.form_details;
            if let Some(notes) = request.notes {
                let details = form_details.get_or_insert_with(maho_core::vault_manager::VaultCredentialFormDetails::default);
                details.notes.clear();
                if !notes.is_empty() {
                    let mut note = maho_core::vault_manager::VaultCredentialNote::default();
                    note.value = notes;
                    details.notes.push(note);
                }
            }
            let input = VaultLoginInput {
                metadata: request.metadata,
                username: request.username,
                password: zeroize::Zeroizing::new(request.password),
                form_details,
            };
            match core.vault_add_login(input) {
                Ok(item) => vault_ok(VaultMutationResult::from(item)),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Lists public metadata for items in the profile-owned Vault.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_list_items_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultItemListRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.vault_list_items(&request) {
                Ok(items) => vault_ok(items),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Searches exact-origin public item metadata.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_search_items_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultItemSearchRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.vault_search_items(&request) {
                Ok(items) => vault_ok(items),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Matches public item metadata using an explicit origin policy.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_match_items_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultMatchItemsRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            let policy = match request.policy {
                VaultMatchPolicyRequest::Exact => OriginMatchPolicy::Exact,
                VaultMatchPolicyRequest::Subdomain => OriginMatchPolicy::Subdomain,
            };
            match core.vault_match_items(&request.request, policy) {
                Ok(items) => vault_ok(items),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Updates a durable login record and returns its public metadata.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_update_login_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultLoginUpdateRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            let update = VaultLoginUpdate {
                notes: request.notes.map(Zeroizing::new),
                metadata: request.metadata,
                username: request.username,
                password: request.password.map(zeroize::Zeroizing::new),
                form_details: request.form_details,
            };
            match core.vault_update_login(request.id, request.expected_revision, update) {
                Ok(item) => vault_ok(VaultMutationResult::from(item)),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Tombstones a durable Vault item and returns its public metadata.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_delete_item_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultDeleteRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.vault_delete_item(request.id, request.expected_revision) {
                Ok(item) => vault_ok(item),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Consumes a login secret for a browser-process action and returns no plaintext.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_use_secret_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            // SAFETY: Category 8 (FFI boundary): this public ABI forwards its
            // documented pointer contracts to the checked helper.
            match unsafe { vault_use_login_secret(core, request_json) } {
                Ok(_secret) => vault_ok(VaultUseSecretResponse {}),
                Err(response) => response,
            }
        },
        ptr::null_mut()
    )
}

/// Returns a short-lived login secret buffer for browser-process copy/fill use.
///
/// The returned buffer is never suitable for WebUI/Mojo responses. The browser
/// process must consume it immediately and release it with
/// [`maho_vault_free_secret_buffer`], which zeroizes before deallocation.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_use_secret_buffer_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            // SAFETY: Category 8 (FFI boundary): this public ABI forwards its
            // documented pointer contracts to the checked helper.
            match unsafe { vault_use_login_secret(core, request_json) } {
                Ok(secret) => vault_secret_buffer(&secret),
                Err(response) => {
                    // SAFETY: Category 12 (invalid free): this path discards a
                    // structured error string allocated by `vault_use_login_secret`
                    // exactly once because this raw-buffer ABI reports errors as null.
                    unsafe { free_vault_error_response(response) };
                    ptr::null_mut()
                }
            }
        },
        ptr::null_mut()
    )
}

/// Frees a secret buffer created by FFI functions, ensuring memory is zeroized before deallocation.
///
/// # Safety
/// `buffer` must be a valid pointer created by Vault secret FFI functions or NUL.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_free_secret_buffer(buffer: *mut c_char) {
    if buffer.is_null() {
        return;
    }
    // SAFETY: Category 12 (invalid free): callers may only pass a pointer
    // returned by Vault secret-buffer FFI functions, transferring ownership
    // back exactly once for zeroization and deallocation.
    unsafe {
        let mut vec = CString::from_raw(buffer).into_bytes();
        vec.zeroize();
    }
}

#[cfg(test)]
mod vault_ffi_tests {
    use super::*;
    use std::fs;

    const MASTER: &str = "correct horse battery staple";
    const RECOVERY: &str = "test-only independent recovery material";
    const SECRET: &str = "S3NTINEL-vault-ffi-secret";

    fn request(value: serde_json::Value) -> CString {
        CString::new(value.to_string()).expect("test JSON contains no NUL")
    }

    fn update_request_with_revision(revision: serde_json::Value) -> serde_json::Value {
        serde_json::json!({
            "id": VaultItemId::new(),
            "expectedRevision": revision,
            "metadata": {
                "title": "Revision Test",
                "origins": ["https://revision.example"],
                "usernameHint": "revision@example.test",
                "itemKind": "login",
                "totp": null,
                "passkey": null
            },
            "username": "revision@example.test"
        })
    }

    fn delete_request_with_revision(revision: serde_json::Value) -> serde_json::Value {
        serde_json::json!({
            "id": VaultItemId::new(),
            "expectedRevision": revision
        })
    }

    fn use_secret_request_with_revision(revision: serde_json::Value) -> serde_json::Value {
        serde_json::json!({
            "itemId": VaultItemId::new(),
            "expectedRevision": revision,
            "action": "fill"
        })
    }

    #[test]
    fn vault_mutation_requests_accept_string_and_legacy_numeric_revisions() {
        for value in [
            serde_json::json!(u64::MAX.to_string()),
            serde_json::json!(42),
        ] {
            let update: VaultLoginUpdateRequest =
                serde_json::from_value(update_request_with_revision(value.clone()))
                    .expect("update request accepts supported revision encoding");
            let delete: VaultDeleteRequest =
                serde_json::from_value(delete_request_with_revision(value.clone()))
                    .expect("delete request accepts supported revision encoding");
            // The secret-use request carries the same revision field and must
            // accept the same two wire encodings: browser code posts a JSON
            // number, while `VaultMutationResult` echoes a decimal string.
            let use_secret: VaultUseSecretRequest =
                serde_json::from_value(use_secret_request_with_revision(value))
                    .expect("use-secret request accepts supported revision encoding");
            let expected = if update.expected_revision.value() == u64::MAX {
                u64::MAX
            } else {
                42
            };
            assert_eq!(update.expected_revision.value(), expected);
            assert_eq!(delete.expected_revision.value(), expected);
            assert_eq!(use_secret.expected_revision.value(), expected);
        }
    }

    #[test]
    fn vault_mutation_results_expose_lossless_identity() {
        let (_key_lock, core, path) = core_with_storage();
        let init = initialize_request();
        assert_eq!(
            read_owned_json(unsafe { maho_vault_initialize_json(core, init.as_ptr()) })["ok"],
            true
        );

        let add = add_login_request();
        let added = read_owned_json(unsafe { maho_vault_add_login_json(core, add.as_ptr()) });
        assert_eq!(added["ok"], true);
        let id = added["data"]["id"]
            .as_str()
            .expect("add mutation result carries UUID")
            .to_string();
        assert_eq!(added["data"]["revision"], "1");

        let update = request(serde_json::json!({
            "id": id,
            "expectedRevision": "1",
            "metadata": {
                "title": "Updated Example",
                "origins": ["https://example.com"],
                "usernameHint": "ignored-by-core",
                "itemKind": "login",
                "totp": null,
                "passkey": null
            },
            "username": "person@example.com",
            "password": null
        }));
        let updated =
            read_owned_json(unsafe { maho_vault_update_login_json(core, update.as_ptr()) });
        assert_eq!(updated["ok"], true);
        assert_eq!(updated["data"]["id"], id);
        assert_eq!(updated["data"]["revision"], "2");

        free_core(core, &path);
    }

    #[test]
    fn vault_mutation_requests_reject_invalid_revision_encodings() {
        let invalid = [
            serde_json::json!(-1),
            serde_json::json!(1.5),
            serde_json::json!("not-a-number"),
            serde_json::json!("-1"),
            serde_json::json!("1.5"),
            serde_json::json!("01"),
            serde_json::json!("18446744073709551616"),
        ];

        for value in invalid {
            assert!(serde_json::from_value::<VaultLoginUpdateRequest>(
                update_request_with_revision(value.clone())
            )
            .is_err());
            assert!(
                serde_json::from_value::<VaultDeleteRequest>(delete_request_with_revision(
                    value.clone()
                ))
                .is_err()
            );
            assert!(serde_json::from_value::<VaultUseSecretRequest>(
                use_secret_request_with_revision(value)
            )
            .is_err());
        }
    }

    fn read_owned_json(pointer: *mut c_char) -> serde_json::Value {
        assert!(!pointer.is_null(), "Vault JSON ABI returned null");
        // SAFETY: the Vault FFI returns a CString allocated by Rust and owned by this test.
        let value = unsafe { CStr::from_ptr(pointer) }
            .to_str()
            .expect("Vault JSON ABI returns UTF-8")
            .to_owned();
        // SAFETY: this test consumes the returned allocation exactly once.
        unsafe { maho_string_free(pointer) };
        serde_json::from_str(&value).expect("Vault JSON ABI returns JSON")
    }

    fn core_with_storage() -> (
        std::sync::MutexGuard<'static, ()>,
        *mut MahoCore,
        std::path::PathBuf,
    ) {
        // Hold the process-wide test-key lock for the caller's whole test:
        // the key is injected here and re-read on every vault FFI storage
        // open below, and parallel key-flipping tests corrupt that.
        let key_lock = crate::common::sqlcipher_test_key_lock();
        maho_storage::sqlite::set_sqlcipher_key("vault-ffi-test-key")
            .expect("test SQLCipher key configures");
        let path = std::env::temp_dir().join(format!("maho-vault-ffi-{}", uuid::Uuid::new_v4()));
        fs::create_dir_all(&path).expect("test profile directory creates");
        let db_path = path.join("vault.sqlite");
        let db = db_path.to_str().expect("temporary path is UTF-8");
        (
            key_lock,
            Box::into_raw(Box::new(MahoCore::new().with_storage(db))),
            path,
        )
    }

    fn free_core(core: *mut MahoCore, path: &std::path::Path) {
        // SAFETY: `core` is uniquely owned by this test and has not been freed.
        unsafe { maho_core_free(core) };
        fs::remove_dir_all(path).expect("test profile directory removes");
    }

    fn initialize_request() -> CString {
        request(serde_json::json!({
            "masterPassphrase": MASTER,
            "recoverySecret": RECOVERY,
        }))
    }

    fn add_login_request() -> CString {
        request(serde_json::json!({
            "metadata": {
                "title": "Example",
                "origins": ["https://example.com"],
                "usernameHint": "ignored-by-core",
                "itemKind": "login",
                "totp": null,
                "passkey": null,
            },
            "username": "person@example.com",
            "password": SECRET,
        }))
    }

    // Task 4: the FFI JSON contract must carry the browser-form detail the C++
    // adapter sends, and hand it back on the backend credential read.
    //
    // Failing-first rationale: `VaultLoginRequest` uses `deny_unknown_fields`, so
    // before Task 4 an add request containing `formDetails` was REJECTED
    // outright; the request below returned ok=false and no detail could ever
    // round-trip.
    #[test]
    fn vault_ffi_add_login_round_trips_form_details_through_the_json_contract() {
        let (_key_lock, core, path) = core_with_storage();
        let init = initialize_request();
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_initialize_json(core, init.as_ptr()) })["ok"],
            true
        );

        let add = request(serde_json::json!({
            "metadata": {
                "title": "Detail",
                "origins": ["https://detail.example"],
                "usernameHint": "ignored-by-core",
                "itemKind": "login",
                "totp": null,
                "passkey": null,
            },
            "username": "person@detail.example",
            "password": SECRET,
            "formDetails": {
                "scheme": 1,
                "signonRealm": "https://detail.example",
                "url": "https://detail.example/login",
                "action": "https://detail.example/submit",
                "usernameElement": "user",
                "passwordElement": "pass",
                "blockedByUser": true,
                "matchType": 4,
                "inStore": 1,
                "formData": "cGlja2xl",
            },
        }));
        // SAFETY: the core and request are valid for the duration of this call.
        let added = read_owned_json(unsafe { maho_vault_add_login_json(core, add.as_ptr()) });
        assert_eq!(added["ok"], true);

        // SAFETY: the core is uniquely owned by this test.
        let core_ref = unsafe { &*core };
        let session = core_ref
            .create_vault_backend_session()
            .expect("backend session");
        let credentials = session
            .login_credentials(Some("https://detail.example"), None)
            .expect("backend credential read");
        assert_eq!(1, credentials.len());
        let details = credentials[0]
            .form_details
            .as_ref()
            .expect("form details survive the FFI contract");
        assert_eq!(1, details.scheme);
        assert_eq!("https://detail.example/login", details.url);
        assert_eq!("https://detail.example/submit", details.action);
        assert_eq!("user", details.username_element);
        assert_eq!("pass", details.password_element);
        assert!(details.blocked_by_user);
        assert_eq!(Some(4), details.match_type);
        assert_eq!(1, details.in_store);
        assert_eq!("cGlja2xl", details.form_data);
        // Public list metadata must NOT gain the form detail: it stays inside the
        // encrypted record.
        let list = request(
            serde_json::json!({"schemaVersion": 1, "provider": null, "kinds": [], "cursor": null, "limit": 50}),
        );
        // SAFETY: the core and request are valid for the duration of this call.
        let listed = read_owned_json(unsafe { maho_vault_list_items_json(core, list.as_ptr()) });
        assert!(!listed.to_string().contains("formDetails"));
        assert!(!listed.to_string().contains("passwordElement"));

        drop(session);
        free_core(core, &path);
    }

    // Failing-first rationale: the backend guessed the lookup kind from the
    // STORED records, so a `Basic realm=""` item (whose signon realm equals the
    // plain origin) hijacked HTML lookups for the same site and hid the stored
    // HTML credential. The request now carries the scheme, and the scheme
    // decides HTML-origin versus HTTP-auth-realm resolution.
    #[test]
    fn vault_ffi_login_query_honors_requested_scheme_when_realms_collide() {
        let (_key_lock, core, path) = core_with_storage();
        let init = initialize_request();
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_initialize_json(core, init.as_ptr()) })["ok"],
            true
        );

        let collide_realm = "https://collide.example";
        let add = |scheme: u8, username: &str| {
            request(serde_json::json!({
                "metadata": {
                    "title": "Collision",
                    "origins": [collide_realm],
                    "usernameHint": "ignored-by-core",
                    "itemKind": "login",
                    "totp": null,
                    "passkey": null,
                },
                "username": username,
                "password": SECRET,
                "formDetails": {
                    "scheme": scheme,
                    "signonRealm": collide_realm,
                    "url": collide_realm,
                    "action": collide_realm,
                    "usernameElement": "user",
                    "passwordElement": "pass",
                    "blockedByUser": false,
                },
            }))
        };
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe {
                maho_vault_add_login_json(core, add(0, "html-user").as_ptr())
            })["ok"],
            true
        );
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe {
                maho_vault_add_login_json(core, add(1, "basic-user").as_ptr())
            })["ok"],
            true
        );

        // SAFETY: the core is uniquely owned by this test.
        let core_ref = unsafe { &*core };
        let session = core_ref
            .create_vault_backend_session()
            .expect("backend session");

        // The HTML query must answer with the HTML credential even though a
        // Basic item shares the identical signon realm.
        let html_credentials = session
            .login_credentials(Some(collide_realm), Some(0))
            .expect("html credential read");
        assert_eq!(1, html_credentials.len());
        assert_eq!(
            0,
            html_credentials[0]
                .form_details
                .as_ref()
                .expect("html form details")
                .scheme
        );

        // The Basic query must resolve the HTTP-auth credential, not the HTML
        // one sharing the realm.
        let basic_credentials = session
            .login_credentials(Some(collide_realm), Some(1))
            .expect("basic credential read");
        assert_eq!(1, basic_credentials.len());
        assert_eq!(
            1,
            basic_credentials[0]
                .form_details
                .as_ref()
                .expect("basic form details")
                .scheme
        );

        // Legacy callers without a scheme keep the stored-realm inference.
        let inferred = session
            .login_credentials(Some(collide_realm), None)
            .expect("inferred credential read");
        assert_eq!(1, inferred.len());

        drop(session);
        free_core(core, &path);
    }

    #[test]
    fn vault_secret_buffer_free_accepts_owned_secret_buffer() {
        let secret = CString::new("miri-secret-buffer").expect("test secret has no NUL");
        let raw = secret.into_raw();

        // SAFETY: this test transfers one CString allocation to the Vault secret
        // buffer free function and never uses the pointer again.
        unsafe { maho_vault_free_secret_buffer(raw) };
    }

    #[test]
    fn vault_ffi_contract_removes_process_global_five_function_abi() {
        let source = include_str!("lib.rs");

        assert!(!source.contains(concat!("GLOBAL_", "VAULT_MANAGER")));
        assert!(!source.contains(concat!("fn with_", "vault")));
        assert!(!source.contains(concat!("fn maho_vault_", "initialize(")));
        assert!(!source.contains(concat!("fn maho_vault_", "unlock(")));
        assert!(!source.contains(concat!("fn maho_vault_", "lock()")));
        assert!(!source.contains(concat!("fn maho_vault_", "encrypt_item(")));
    }

    #[test]
    fn vault_ffi_returns_explicit_boundary_failures_for_null_and_invalid_utf8() {
        let init = initialize_request();
        let invalid_utf8 = [0xff_u8 as c_char, 0];

        // SAFETY: null core is an explicitly supported boundary input.
        assert!(unsafe { maho_vault_initialize_json(ptr::null_mut(), init.as_ptr()) }.is_null());

        let (_key_lock, core, path) = core_with_storage();
        // SAFETY: `core` is live and the invalid bytes are NUL-terminated.
        let response =
            read_owned_json(unsafe { maho_vault_initialize_json(core, invalid_utf8.as_ptr()) });
        assert_eq!(response["ok"], false);
        assert_eq!(response["error"]["code"], "storage_failure");
        assert_eq!(response["error"]["message"], "invalid_utf8");
        free_core(core, &path);
    }

    #[test]
    fn vault_ffi_initialization_fails_closed_without_profile_storage() {
        let core = maho_core_new();
        let init = initialize_request();

        // SAFETY: `core` is a live FFI-created core and `init` is NUL-terminated JSON.
        let response = read_owned_json(unsafe { maho_vault_initialize_json(core, init.as_ptr()) });
        assert_eq!(response["ok"], false);
        assert_eq!(response["error"]["code"], "storage_failure");
        // SAFETY: `core` is uniquely owned by this test.
        unsafe { maho_core_free(core) };
    }

    #[test]
    fn core_tick_at_returns_null_for_null_core() {
        // SAFETY: the function tolerates a null core and must return null.
        assert!(unsafe { maho_core_tick_at(std::ptr::null_mut(), 0) }.is_null());
    }

    #[test]
    fn core_tick_at_drives_vault_auto_lock_deterministically() {
        let _gate_idle = crate::common::import_gate_idle();
        let (_key_lock, core, path) = core_with_storage();
        let init = initialize_request();
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_initialize_json(core, init.as_ptr()) })["ok"],
            true
        );
        let unlock = request(serde_json::json!({"masterPassphrase": MASTER}));
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_unlock_json(core, unlock.as_ptr()) })["data"]
                ["lockState"],
            "unlocked"
        );

        let future = chrono::Utc::now().timestamp() + 172_800; // two days out
                                                               // SAFETY: the core is a valid profile-owned core; the returned string is
                                                               // freed by `read_owned_json`.
        let updates = read_owned_json(unsafe { maho_core_tick_at(core, future) });
        assert!(updates.is_array());

        // SAFETY: the core is a valid profile-owned core.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_status_json(core) })["data"]["lockState"],
            "auto_locked"
        );
        free_core(core, &path);
    }

    #[test]
    fn vault_ffi_uses_profile_owned_durable_lifecycle() {
        let (_key_lock, core, path) = core_with_storage();
        let init = initialize_request();
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_initialize_json(core, init.as_ptr()) })["ok"],
            true
        );
        // SAFETY: the core is a valid profile-owned core.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_lock_json(core) })["data"]["lockState"],
            "locked"
        );
        let unlock = request(serde_json::json!({"masterPassphrase": MASTER}));
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_unlock_json(core, unlock.as_ptr()) })["data"]
                ["lockState"],
            "unlocked"
        );
        // SAFETY: the core is a valid profile-owned core.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_status_json(core) })["data"]["lockState"],
            "unlocked"
        );
        // SAFETY: the core is a valid profile-owned core.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_lock_json(core) })["ok"],
            true
        );
        let recovery = request(serde_json::json!({"recoverySecret": RECOVERY}));
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe {
                maho_vault_unlock_with_recovery_json(core, recovery.as_ptr())
            })["data"]["lockState"],
            "unlocked"
        );
        free_core(core, &path);
    }

    #[test]
    fn vault_ffi_crud_returns_metadata_and_keeps_secret_out_of_queries() {
        let (_key_lock, core, path) = core_with_storage();
        let init = initialize_request();
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_initialize_json(core, init.as_ptr()) })["ok"],
            true
        );
        let add = add_login_request();
        // SAFETY: the core and request are valid for the duration of this call.
        let added = read_owned_json(unsafe { maho_vault_add_login_json(core, add.as_ptr()) });
        let id = added["data"]["id"]
            .as_str()
            .expect("add returns ID")
            .to_string();
        let list = request(
            serde_json::json!({"schemaVersion": 1, "provider": null, "kinds": [], "cursor": null, "limit": 50}),
        );
        // SAFETY: the core and request are valid for the duration of this call.
        let listed = read_owned_json(unsafe { maho_vault_list_items_json(core, list.as_ptr()) });
        let search = request(
            serde_json::json!({"schemaVersion": 1, "origin": "https://example.com", "provider": null, "kinds": []}),
        );
        // SAFETY: the core and request are valid for the duration of this call.
        let searched =
            read_owned_json(unsafe { maho_vault_search_items_json(core, search.as_ptr()) });
        let matched = request(
            serde_json::json!({"request": {"schemaVersion": 1, "origin": "https://login.example.com", "provider": null, "kinds": []}, "policy": "subdomain"}),
        );
        // SAFETY: the core and request are valid for the duration of this call.
        let matched =
            read_owned_json(unsafe { maho_vault_match_items_json(core, matched.as_ptr()) });
        assert_eq!(
            listed["data"].as_array().expect("list data is array").len(),
            1
        );
        assert_eq!(
            searched["data"]
                .as_array()
                .expect("search data is array")
                .len(),
            1
        );
        assert_eq!(
            matched["data"]
                .as_array()
                .expect("match data is array")
                .len(),
            1
        );
        assert!(!listed.to_string().contains(SECRET));
        assert!(!searched.to_string().contains(SECRET));

        // SAFETY: the core is valid and the explicit lock transitions it to a denied state.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_lock_json(core) })["ok"],
            true
        );
        // SAFETY: the core and request are valid for the duration of this call.
        let locked = read_owned_json(unsafe { maho_vault_list_items_json(core, list.as_ptr()) });
        assert_eq!(locked["ok"], false);
        assert_eq!(locked["error"]["code"], "locked");
        let unlock = request(serde_json::json!({"masterPassphrase": MASTER}));
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_unlock_json(core, unlock.as_ptr()) })["ok"],
            true
        );

        assert!(!include_str!("lib.rs").contains(concat!("maho_vault_", "agent_reveal")));
        assert!(!include_str!("lib.rs").contains(concat!("maho_vault_", "use_login_password_json")));

        let used_revision =
            read_owned_json(unsafe { maho_vault_list_items_json(core, list.as_ptr()) })["data"][0]
                ["revision"]
                .as_u64()
                .expect("use records a revision");
        let update = request(serde_json::json!({
            "id": id,
            "expectedRevision": used_revision,
            "metadata": {
                "title": "Renamed Example",
                "origins": ["https://example.com"],
                "usernameHint": "ignored-by-core",
                "itemKind": "login",
                "totp": null,
                "passkey": null,
            },
            "username": "renamed@example.com",
            "password": null,
        }));
        // SAFETY: the core and request are valid for the duration of this call.
        let updated =
            read_owned_json(unsafe { maho_vault_update_login_json(core, update.as_ptr()) });
        assert_eq!(updated["data"]["id"], id);
        assert!(updated["data"]["revision"].is_string());
        let delete = request(serde_json::json!({
            "id": updated["data"]["id"],
            "expectedRevision": updated["data"]["revision"],
        }));
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_delete_item_json(core, delete.as_ptr()) })["ok"],
            true
        );

        free_core(core, &path);
    }
}
