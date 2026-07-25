//! Opaque FFI handles for the off-thread Vault backend read path.

use std::ffi::{c_char, CStr};
use std::ptr;
use std::sync::{Arc, Mutex};

use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::VaultCrudError;
use maho_core::vault_runtime::session::{VaultBackendSession, VaultBackendSessionError};
use maho_types::vault::{VaultBatchReadRequest, VaultSchemaVersion};
use zeroize::Zeroize;

mod result;

pub use result::{
    maho_vault_backend_buffer_data, maho_vault_backend_buffer_free, maho_vault_backend_buffer_len,
    maho_vault_backend_result_consume, maho_vault_backend_result_free,
    maho_vault_backend_result_status,
};

/// Opaque backend session created from a one-time MahoCore snapshot.
pub struct MahoVaultBackendSession {
    state: SharedSession,
}

/// Opaque result returned by a backend session operation.
pub struct MahoVaultBackendResult {
    state: SharedSession,
    outcome: Mutex<BackendOutcome>,
}

/// Opaque owned JSON bytes returned from a consumed backend result.
pub struct MahoVaultBackendBuffer {
    bytes: Vec<u8>,
}

type SharedSession = Arc<Mutex<Option<VaultBackendSession>>>;

enum BackendOutcome {
    Success(Vec<u8>),
    InvalidRequest,
    RuntimeUnavailable,
    Locked,
    Failed,
    Consumed,
}

impl Drop for MahoVaultBackendBuffer {
    fn drop(&mut self) {
        self.bytes.zeroize();
    }
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct BackendCredentialRequest {
    #[serde(default)]
    origin: Option<String>,
}

/// Typed status for a backend operation result.
#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MahoVaultBackendResultStatus {
    Success = 0,
    InvalidRequest = 1,
    RuntimeUnavailable = 2,
    Locked = 3,
    Failed = 4,
}

impl BackendOutcome {
    fn status(&self) -> MahoVaultBackendResultStatus {
        match self {
            Self::Success(_) => MahoVaultBackendResultStatus::Success,
            Self::InvalidRequest => MahoVaultBackendResultStatus::InvalidRequest,
            Self::RuntimeUnavailable => MahoVaultBackendResultStatus::RuntimeUnavailable,
            Self::Locked => MahoVaultBackendResultStatus::Locked,
            Self::Failed | Self::Consumed => MahoVaultBackendResultStatus::Failed,
        }
    }
}

fn request_from_json(
    request_json: *const c_char,
) -> Option<Result<VaultBatchReadRequest, BackendOutcome>> {
    if request_json.is_null() {
        return None;
    }
    // SAFETY: the non-null pointer must reference a NUL-terminated request for this call.
    let request = match unsafe { CStr::from_ptr(request_json) }.to_str() {
        Ok(request) => request,
        Err(_) => return Some(Err(BackendOutcome::InvalidRequest)),
    };
    Some(serde_json::from_str(request).map_err(|_| BackendOutcome::InvalidRequest))
}

fn credential_request_from_json(
    request_json: *const c_char,
) -> Option<Result<BackendCredentialRequest, BackendOutcome>> {
    if request_json.is_null() {
        return None;
    }
    // SAFETY: the non-null pointer must reference a NUL-terminated request for this call.
    let request = match unsafe { CStr::from_ptr(request_json) }.to_str() {
        Ok(request) => request,
        Err(_) => return Some(Err(BackendOutcome::InvalidRequest)),
    };
    Some(serde_json::from_str(request).map_err(|_| BackendOutcome::InvalidRequest))
}

fn outcome_from_error(error: VaultBackendSessionError) -> BackendOutcome {
    match error {
        VaultBackendSessionError::RuntimeUnavailable => BackendOutcome::RuntimeUnavailable,
        VaultBackendSessionError::Batch(VaultCrudError::Locked) => BackendOutcome::Locked,
        VaultBackendSessionError::StorageUnavailable
        | VaultBackendSessionError::Storage(_)
        | VaultBackendSessionError::Batch(_) => BackendOutcome::Failed,
        _ => BackendOutcome::Failed,
    }
}

fn revalidate(session: &VaultBackendSession) -> Result<(), BackendOutcome> {
    let request = VaultBatchReadRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        item_ids: Vec::new(),
    };
    session
        .batch_read(&request)
        .map(|_| ())
        .map_err(outcome_from_error)
}

/// Creates an opaque backend session by borrowing `MahoCore` exactly once.
///
/// # Safety
/// `core` must be null or a valid MahoCore pointer for this call.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_backend_session_new(
    core: *mut MahoCore,
) -> *mut MahoVaultBackendSession {
    crate::ffi_safe!(
        {
            // SAFETY: the caller guarantees a non-null `core` is live for this call only.
            let Some(core) = (unsafe { core.as_ref() }) else {
                return ptr::null_mut();
            };
            let Ok(session) = core.create_vault_backend_session() else {
                return ptr::null_mut();
            };
            Box::into_raw(Box::new(MahoVaultBackendSession {
                state: Arc::new(Mutex::new(Some(session))),
            }))
        },
        ptr::null_mut()
    )
}

/// Closes a backend session and invalidates every unconsumed result from it.
///
/// # Safety
/// `session` must be null or a valid backend session handle.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_backend_session_close(session: *mut MahoVaultBackendSession) {
    crate::ffi_safe!(
        {
            // SAFETY: the caller guarantees a non-null handle remains live for this call.
            let Some(session) = (unsafe { session.as_ref() }) else {
                return;
            };
            let Ok(mut state) = session.state.lock() else {
                return;
            };
            *state = None;
        },
        ()
    )
}

/// Releases a backend session handle. This also closes the session.
///
/// # Safety
/// `session` must be null or a backend session handle returned by `session_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_backend_session_free(session: *mut MahoVaultBackendSession) {
    crate::ffi_safe!(
        {
            if session.is_null() {
                return;
            }
            // SAFETY: ownership is transferred back exactly once by the caller.
            let session = unsafe { Box::from_raw(session) };
            let Ok(mut state) = session.state.lock() else {
                return;
            };
            *state = None;
        },
        ()
    )
}

/// Executes a typed Vault batch-read request without accessing `MahoCore`.
///
/// # Safety
/// `session` must be valid and `request_json` must be a valid NUL-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_backend_session_execute(
    session: *mut MahoVaultBackendSession,
    request_json: *const c_char,
) -> *mut MahoVaultBackendResult {
    crate::ffi_safe!(
        {
            // SAFETY: the caller guarantees a non-null handle remains live for this call.
            let Some(session) = (unsafe { session.as_ref() }) else {
                return ptr::null_mut();
            };
            let Some(request) = request_from_json(request_json) else {
                return ptr::null_mut();
            };
            let outcome = match request {
                Ok(request) => {
                    let Ok(state) = session.state.lock() else {
                        return ptr::null_mut();
                    };
                    let Some(backend_session) = state.as_ref() else {
                        return ptr::null_mut();
                    };
                    match backend_session.batch_read(&request) {
                        Ok(result) => match serde_json::to_vec(&result) {
                            Ok(payload) => BackendOutcome::Success(payload),
                            Err(_) => BackendOutcome::Failed,
                        },
                        Err(error) => outcome_from_error(error),
                    }
                }
                Err(error) => error,
            };
            Box::into_raw(Box::new(MahoVaultBackendResult {
                state: Arc::clone(&session.state),
                outcome: Mutex::new(outcome),
            }))
        },
        ptr::null_mut()
    )
}

/// Executes a typed backend-only credential request that may return short-lived login secrets.
///
/// # Safety
/// `session` must be valid and `request_json` must be a valid NUL-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_backend_session_execute_credentials(
    session: *mut MahoVaultBackendSession,
    request_json: *const c_char,
) -> *mut MahoVaultBackendResult {
    crate::ffi_safe!(
        {
            // SAFETY: the caller guarantees a non-null handle remains live for this call.
            let Some(session) = (unsafe { session.as_ref() }) else {
                return ptr::null_mut();
            };
            let Some(request) = credential_request_from_json(request_json) else {
                return ptr::null_mut();
            };
            let outcome = match request {
                Ok(request) => {
                    let Ok(state) = session.state.lock() else {
                        return ptr::null_mut();
                    };
                    let Some(backend_session) = state.as_ref() else {
                        return ptr::null_mut();
                    };
                    match backend_session.login_credentials(request.origin.as_deref()) {
                        Ok(result) => match serde_json::to_vec(&result) {
                            Ok(payload) => BackendOutcome::Success(payload),
                            Err(_) => BackendOutcome::Failed,
                        },
                        Err(error) => outcome_from_error(error),
                    }
                }
                Err(error) => error,
            };
            Box::into_raw(Box::new(MahoVaultBackendResult {
                state: Arc::clone(&session.state),
                outcome: Mutex::new(outcome),
            }))
        },
        ptr::null_mut()
    )
}
