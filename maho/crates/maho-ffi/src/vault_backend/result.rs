use std::ptr;

use super::{
    revalidate, MahoVaultBackendBuffer, MahoVaultBackendResult, MahoVaultBackendResultStatus,
};

/// Returns a typed status for a backend result without consuming it.
///
/// # Safety
/// `result` must be null or a valid backend result handle.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_backend_result_status(
    result: *const MahoVaultBackendResult,
) -> MahoVaultBackendResultStatus {
    crate::ffi_safe!(
        {
            // SAFETY: the caller guarantees a non-null handle remains live for this call.
            unsafe { result.as_ref() }
                .and_then(|result| result.outcome.lock().ok())
                .map(|outcome| outcome.status())
                .unwrap_or(MahoVaultBackendResultStatus::Failed)
        },
        MahoVaultBackendResultStatus::Failed
    )
}

/// Consumes a successful result into dedicated backend-owned JSON bytes.
///
/// # Safety
/// `result` must be null or a backend result handle returned by `session_execute`.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_backend_result_consume(
    result: *mut MahoVaultBackendResult,
) -> *mut MahoVaultBackendBuffer {
    crate::ffi_safe!(
        {
            if result.is_null() {
                return ptr::null_mut();
            }
            // SAFETY: the caller guarantees a non-null handle remains live for this call.
            let result = unsafe { &*result };
            let Ok(mut outcome) = result.outcome.lock() else {
                return ptr::null_mut();
            };
            let Ok(state) = result.state.lock() else {
                *outcome = super::BackendOutcome::Failed;
                return ptr::null_mut();
            };
            let Some(session) = state.as_ref() else {
                *outcome = super::BackendOutcome::Failed;
                return ptr::null_mut();
            };
            if let Err(error) = revalidate(session) {
                *outcome = error;
                return ptr::null_mut();
            }
            let super::BackendOutcome::Success(payload) =
                std::mem::replace(&mut *outcome, super::BackendOutcome::Consumed)
            else {
                return ptr::null_mut();
            };
            Box::into_raw(Box::new(MahoVaultBackendBuffer { bytes: payload }))
        },
        ptr::null_mut()
    )
}

/// Releases an unconsumed backend result.
///
/// # Safety
/// `result` must be null or a backend result handle returned by `session_execute`.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_backend_result_free(result: *mut MahoVaultBackendResult) {
    crate::ffi_safe!(
        {
            if !result.is_null() {
                // SAFETY: ownership is transferred back exactly once by the caller.
                drop(unsafe { Box::from_raw(result) });
            }
        },
        ()
    )
}

/// Returns the byte pointer for a dedicated backend buffer.
///
/// # Safety
/// `buffer` must be null or a valid backend buffer handle.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_backend_buffer_data(
    buffer: *const MahoVaultBackendBuffer,
) -> *const u8 {
    crate::ffi_safe!(
        {
            // SAFETY: the caller guarantees a non-null handle remains live for this call.
            unsafe { buffer.as_ref() }.map_or(ptr::null(), |buffer| buffer.bytes.as_ptr())
        },
        ptr::null()
    )
}

/// Returns the byte length for a dedicated backend buffer.
///
/// # Safety
/// `buffer` must be null or a valid backend buffer handle.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_backend_buffer_len(
    buffer: *const MahoVaultBackendBuffer,
) -> usize {
    crate::ffi_safe!(
        {
            // SAFETY: the caller guarantees a non-null handle remains live for this call.
            unsafe { buffer.as_ref() }.map_or(0, |buffer| buffer.bytes.len())
        },
        0
    )
}

/// Releases a dedicated backend buffer. Never use `maho_string_free` for this allocation.
///
/// # Safety
/// `buffer` must be null or a backend buffer handle returned by `result_consume`.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_backend_buffer_free(buffer: *mut MahoVaultBackendBuffer) {
    crate::ffi_safe!(
        {
            if !buffer.is_null() {
                // SAFETY: ownership is transferred back exactly once by the caller.
                drop(unsafe { Box::from_raw(buffer) });
            }
        },
        ()
    )
}
