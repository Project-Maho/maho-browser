use std::ffi::{c_char, c_void};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

use crate::agent::browser_bridge::MahoAgentToolResult;
use crate::agent::events::{
    MahoAgentEventKindV2, MahoUnifiedAgentEventCallback, MahoUnifiedAgentEventEnvelope,
};
use crate::agent::secure_storage::{MahoAgentPermissionCallback, MahoAgentSecureStorageCallback};

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

pub(crate) struct SendableCallback<T>(pub(crate) T);

unsafe impl Send for SendableCallback<MahoAgentReleaseCallback> {}
unsafe impl Sync for SendableCallback<MahoAgentReleaseCallback> {}

unsafe impl Send for SendableCallback<Option<MahoAgentReleaseCallback>> {}
unsafe impl Sync for SendableCallback<Option<MahoAgentReleaseCallback>> {}

unsafe impl Send for SendableCallback<MahoUnifiedAgentEventCallback> {}
unsafe impl Sync for SendableCallback<MahoUnifiedAgentEventCallback> {}

unsafe impl Send
    for SendableCallback<
        unsafe extern "C" fn(
            user_data: *mut c_void,
            envelope: *const MahoUnifiedAgentEventEnvelope,
        ),
    >
{
}
unsafe impl Sync
    for SendableCallback<
        unsafe extern "C" fn(
            user_data: *mut c_void,
            envelope: *const MahoUnifiedAgentEventEnvelope,
        ),
    >
{
}

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

pub(crate) struct SendableUserData(pub(crate) usize);
#[allow(dead_code)]
unsafe impl Send for SendableUserData {}
#[allow(dead_code)]
unsafe impl Sync for SendableUserData {}

#[derive(Default)]
pub(crate) struct AcceptedLeasedTurns {
    pub(crate) current: Mutex<Option<Arc<AtomicBool>>>,
}

impl AcceptedLeasedTurns {
    pub(crate) fn accept(self: &Arc<Self>) -> AcceptedLeasedTurn {
        let latch = Arc::new(AtomicBool::new(false));
        let mut current = self
            .current
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        *current = Some(Arc::clone(&latch));
        AcceptedLeasedTurn {
            owner: Arc::clone(self),
            latch,
        }
    }

    pub(crate) fn signal_current(&self) -> bool {
        let current = self
            .current
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let Some(latch) = current.as_ref() else {
            return false;
        };
        latch.store(true, Ordering::Release);
        true
    }

    pub(crate) fn clear_if_current(&self, latch: &Arc<AtomicBool>) {
        let mut current = self
            .current
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if current
            .as_ref()
            .is_some_and(|active_latch| Arc::ptr_eq(active_latch, latch))
        {
            *current = None;
        }
    }
}

pub(crate) struct AcceptedLeasedTurn {
    pub(crate) owner: Arc<AcceptedLeasedTurns>,
    pub(crate) latch: Arc<AtomicBool>,
}

impl AcceptedLeasedTurn {
    pub(crate) fn latch(&self) -> Arc<AtomicBool> {
        Arc::clone(&self.latch)
    }
}

impl Drop for AcceptedLeasedTurn {
    fn drop(&mut self) {
        self.owner.clear_if_current(&self.latch);
    }
}

pub type MahoAgentReleaseCallback = unsafe extern "C" fn(user_data: *mut c_void);

pub struct CallbackReleasePolicy {
    pub callback: MahoAgentReleaseCallback,
    pub user_data: *mut c_void,
}

pub(crate) struct CallbackRelease {
    pub(crate) armed: AtomicBool,
    pub(crate) callback: Option<SendableCallback<MahoAgentReleaseCallback>>,
    pub(crate) user_data: SendableUserData,
}

impl CallbackRelease {
    pub(crate) fn new(callback: Option<MahoAgentReleaseCallback>, user_data: *mut c_void) -> Self {
        Self {
            armed: AtomicBool::new(false),
            callback: callback.map(SendableCallback),
            user_data: SendableUserData(user_data as usize),
        }
    }

    pub(crate) fn arm(&self) {
        self.armed.store(true, Ordering::Release);
    }

    pub(crate) fn release(&self) {
        if !self.armed.load(Ordering::Acquire) {
            return;
        }
        if let Some(ref callback) = self.callback {
            // SAFETY: the FFI ownership contract transfers `user_data` to this lease exactly once.
            // The caller's release callback must not unwind or retain the pointer after this call.
            unsafe { (callback.0)(self.user_data.0 as *mut c_void) };
        }
    }
}

/// Owns one accepted turn's callback context until every callback closure is destroyed.
pub(crate) struct TurnCallbackLease(pub(crate) CallbackRelease);

impl TurnCallbackLease {
    pub(crate) fn new(callback: MahoAgentReleaseCallback, user_data: *mut c_void) -> Self {
        Self(CallbackRelease::new(Some(callback), user_data))
    }

    pub(crate) fn arm(&self) {
        self.0.arm();
    }
}

impl Drop for TurnCallbackLease {
    fn drop(&mut self) {
        self.0.release();
    }
}

pub(crate) fn invoke_with_turn_callback_lease(
    lease: Option<Arc<TurnCallbackLease>>,
    callback: impl FnOnce(),
) {
    callback();
    drop(lease);
}

/// Owns a session callback context until the session and every runtime callback release it.
pub(crate) struct SessionCallbackLease(pub(crate) CallbackRelease);

impl SessionCallbackLease {
    pub(crate) fn new(
        callback: Option<MahoAgentReleaseCallback>,
        user_data: *mut c_void,
    ) -> Arc<Self> {
        Arc::new(Self(CallbackRelease::new(callback, user_data)))
    }

    pub(crate) fn arm(&self) {
        self.0.arm();
    }
}

impl Drop for SessionCallbackLease {
    fn drop(&mut self) {
        self.0.release();
    }
}

#[derive(Clone)]
pub(crate) struct UnifiedEventCallbackRegistration {
    pub(crate) callback: MahoUnifiedAgentEventCallback,
    pub(crate) user_data: usize,
    pub(crate) lease: Option<Arc<SessionCallbackLease>>,
}

pub(crate) fn dispatch_unified_event(
    registration: &Option<UnifiedEventCallbackRegistration>,
    turn_lease: &Option<Arc<TurnCallbackLease>>,
    session_lease: &Option<Arc<SessionCallbackLease>>,
    run_id: &str,
    event_seq: u64,
    kind: MahoAgentEventKindV2,
    payload_json: &str,
) {
    let Some(reg) = registration.as_ref() else {
        return;
    };
    let Some(cb) = reg.callback else {
        return;
    };
    let _ = (turn_lease, session_lease, &reg.lease);

    let Ok(run_id_c) = std::ffi::CString::new(run_id) else {
        return;
    };
    let Ok(payload_c) = std::ffi::CString::new(payload_json) else {
        return;
    };

    let envelope = MahoUnifiedAgentEventEnvelope {
        abi_version: 2,
        run_id: run_id_c.as_ptr(),
        event_seq,
        kind,
        payload_json: payload_c.as_ptr(),
    };

    let user_data = reg.user_data as *mut c_void;
    // SAFETY: caller provided callback and user_data within the lease period.
    unsafe {
        cb(user_data, &envelope);
    }
}

pub(crate) fn clone_callback_lease<T>(lease: &Option<Arc<T>>) -> Option<Arc<T>> {
    lease.as_ref().map(Arc::clone)
}
