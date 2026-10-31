//! Global gate to prevent concurrent FFI access during import.
//!
//! The import orchestrator holds a `*mut MahoCore` across threads via
//! `unsafe impl Sync for CorePtr`. While an import is active, any other
//! FFI call from C++ that mutates `MahoCore` is a data race (`MahoCore`
//! is `!Send + !Sync`). This gate is the runtime enforcement of the
//! "no concurrent FFI during import" invariant.
//!
//! ## Protocol
//!
//! 1. `maho_import_orchestrator_start` calls `enter()` before allocating any
//!    session. If `enter()` returns `false`, the import is rejected (another
//!    import is already running).
//! 2. On every early-exit failure path in `start` that did NOT successfully
//!    launch the background thread, `exit()` is called to release the gate.
//! 3. The import worker thread holds a `GateGuard` for the duration of the
//!    background work; `GateGuard::drop` calls `exit()` unconditionally so
//!    the gate is released on natural completion AND on panic/error.
//! 4. `maho_import_orchestrator_cancel` and `maho_import_orchestrator_free`
//!    call `exit()` after the session is torn down so callers that cancel
//!    before the thread exits also release the gate.
//! 5. Every top-level `maho_core_*` FFI entry point (except `maho_core_free`
//!    and `maho_core_new*`) checks `is_active()` and short-circuits with the
//!    appropriate null/false/empty fallback while an import is running.

use std::sync::atomic::{AtomicBool, Ordering};

pub(crate) static IMPORT_ACTIVE: AtomicBool = AtomicBool::new(false);

/// Attempt to enter the import gate.
///
/// Returns `true` and atomically sets the gate if no import is currently
/// active. Returns `false` (without modifying the gate) if an import is
/// already in progress.
pub(crate) fn enter() -> bool {
    IMPORT_ACTIVE
        .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
        .is_ok()
}

/// Release the import gate. Must be called exactly once for each successful
/// `enter()` call, after the import has fully completed or been cancelled.
pub(crate) fn exit() {
    IMPORT_ACTIVE.store(false, Ordering::Release);
}

/// Returns `true` while an import is active.
///
/// FFI entry points use this to short-circuit mutating operations that would
/// race with the import worker thread.
pub(crate) fn is_active() -> bool {
    IMPORT_ACTIVE.load(Ordering::Acquire)
}

/// RAII guard that releases the import gate when dropped.
///
/// Hold one of these in the import worker thread to ensure `exit()` is called
/// on natural completion, early return, AND panic.
pub(crate) struct GateGuard;

impl Drop for GateGuard {
    fn drop(&mut self) {
        exit();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn import_gate_excludes_concurrent_ops() {
        // `IMPORT_ACTIVE` is process-global; exclude gated-call tests.
        let _gate_lock = crate::common::import_gate_exclusive();
        // Reset to known state first (tests may run in any order)
        exit();

        assert!(enter());
        assert!(is_active());
        assert!(!enter()); // second enter fails (already active)
        exit();
        assert!(!is_active());
        assert!(enter()); // can re-enter after exit
        exit();
    }
}
