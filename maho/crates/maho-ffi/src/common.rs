use std::cell::{Cell, RefCell};
use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr;
use std::sync::atomic::AtomicU64;
use std::sync::{Arc as FfiArc, Condvar as FfiCondvar, Mutex as FfiMutex, MutexGuard};

use crate::ffi_safe;

pub const MAHO_ABI_VERSION: u32 = 2;

pub extern "C" fn maho_abi_version() -> u32 {
    MAHO_ABI_VERSION
}

static FFI_SERIALIZATION_GATE: FfiMutex<()> = FfiMutex::new(());

thread_local! {
    pub(crate) static FFI_SERIALIZATION_DEPTH: Cell<usize> = const { Cell::new(0) };
    pub(crate) static DEFERRED_CORE_UPDATES: RefCell<Vec<maho_types::events::core_update::CoreUpdate>> =
        const { RefCell::new(Vec::new()) };
    pub(crate) static ACTIVE_CALLBACK_TOKENS: RefCell<Vec<u64>> = const { RefCell::new(Vec::new()) };
}

pub(crate) static NEXT_CALLBACK_TOKEN: AtomicU64 = AtomicU64::new(1);

/// Serializes tests that flip the process-global SQLCipher test key
/// (`maho_storage::sqlite::set_sqlcipher_key`). The key is process-wide, so
/// parallel tests injecting different values make each other's
/// `SqliteStorage::open` fail with "file is not a database". Every test that
/// (re)configures the key and then opens databases must hold this guard for
/// the whole key-sensitive region — in practice, for the whole test body
/// (see `core_with_storage` helpers, the routines tests, and the
/// runtime-config roundtrip test).
#[cfg(test)]
pub(crate) fn sqlcipher_test_key_lock() -> MutexGuard<'static, ()> {
    use std::sync::{Mutex, OnceLock};
    static LOCK: OnceLock<Mutex<()>> = OnceLock::new();
    LOCK.get_or_init(|| Mutex::new(()))
        .lock()
        .unwrap_or_else(|e| e.into_inner())
}

#[cfg(test)]
fn import_gate_rwlock() -> &'static std::sync::RwLock<()> {
    use std::sync::{OnceLock, RwLock};
    static LOCK: OnceLock<RwLock<()>> = OnceLock::new();
    LOCK.get_or_init(|| RwLock::new(()))
}

/// Shared guard for tests that call gated `maho_core_*` entry points and
/// assert they succeed. Readers run in parallel with each other.
#[cfg(test)]
pub(crate) fn import_gate_idle() -> std::sync::RwLockReadGuard<'static, ()> {
    import_gate_rwlock()
        .read()
        .unwrap_or_else(|e| e.into_inner())
}

/// Exclusive guard for tests that raise or lower `import_gate::IMPORT_ACTIVE`.
/// Excludes every `import_gate_idle` reader, and lowers the gate on drop so a
/// panicking test cannot leave it raised for the rest of the binary.
#[cfg(test)]
pub(crate) struct ImportGateExclusive(#[allow(dead_code)] std::sync::RwLockWriteGuard<'static, ()>);

#[cfg(test)]
impl Drop for ImportGateExclusive {
    fn drop(&mut self) {
        // Runs before the write guard field is released, so the gate is always
        // lowered before any `import_gate_idle` reader can be admitted.
        crate::import_gate::exit();
    }
}

#[cfg(test)]
pub(crate) fn import_gate_exclusive() -> ImportGateExclusive {
    ImportGateExclusive(
        import_gate_rwlock()
            .write()
            .unwrap_or_else(|e| e.into_inner()),
    )
}

#[derive(Default)]
pub(crate) struct CallbackLeaseState {
    pub(crate) removed: bool,
    pub(crate) in_flight: usize,
}

#[derive(Default)]
pub(crate) struct CallbackLease {
    pub(crate) state: FfiMutex<CallbackLeaseState>,
    pub(crate) idle: FfiCondvar,
}

impl CallbackLease {
    pub(crate) fn enter(self: &FfiArc<Self>, token: u64) -> Option<CallbackInvocation> {
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

    pub(crate) fn remove_and_wait(&self, token: u64) {
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

pub(crate) struct CallbackInvocation {
    pub(crate) token: u64,
    pub(crate) lease: FfiArc<CallbackLease>,
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
        let guard = (!nested).then(|| {
            FFI_SERIALIZATION_GATE
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner)
        });
        Self { _guard: guard }
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

pub extern "C" fn maho_ffi_serialization_scope_enter() -> *mut c_void {
    match std::panic::catch_unwind(FfiSerializationScope::enter) {
        Ok(scope) => Box::into_raw(Box::new(scope)).cast(),
        Err(_) => ptr::null_mut(),
    }
}

/// # Safety
/// `scope` must be null or returned by `maho_ffi_serialization_scope_enter`
/// on the same thread.
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

pub(crate) fn to_json_cstring<T: serde::Serialize + ?Sized>(value: &T) -> *mut c_char {
    match serde_json::to_string(value) {
        Ok(json) => CString::new(json)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        Err(_) => ptr::null_mut(),
    }
}

pub(crate) fn cstring_or_fallback(value: &str, fallback: &'static str) -> Option<CString> {
    CString::new(value)
        .ok()
        .or_else(|| CString::new(fallback).ok())
}

pub(crate) fn cstr_to_str<'a>(ptr: *const c_char) -> Option<&'a str> {
    if ptr.is_null() {
        None
    } else {
        unsafe { CStr::from_ptr(ptr) }.to_str().ok()
    }
}

pub(crate) fn to_c_string(value: &str) -> *mut c_char {
    CString::new(value)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

pub(crate) fn base64_encode(data: &[u8]) -> String {
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

pub(crate) unsafe fn maho_core_free_string(s: *mut c_char) {
    if !s.is_null() {
        drop(CString::from_raw(s));
    }
}

pub(crate) fn dispatch_split_callbacks_for_updates(
    _updates: &[maho_types::events::core_update::CoreUpdate],
) {
}
