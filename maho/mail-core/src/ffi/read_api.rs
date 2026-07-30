// Copyright 2026 Maho Browser. All rights reserved.

//! Asynchronous, callback-based read-query FFI for the mail helper.
//!
//! Each export validates its inputs, then hands the read + serialization to the
//! process tokio runtime; the caller's [`MahoMailReadCallback`] is invoked
//! exactly once per ACCEPTED call. Rejected calls (null/invalid argument, null
//! callback, unavailable state/runtime, or a panic raised before dispatch)
//! return `false` and never invoke the callback.

use std::ffi::{c_void, CString};
use std::future::Future;
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::pin::Pin;
use std::sync::atomic::AtomicPtr;
use std::sync::Arc;

use futures::future::FutureExt;
use tokio::runtime::Runtime;

use crate::error::{MailFfiError, Result};
use crate::read;
use crate::state::{self, AppCtx};

use super::{c_string, non_empty};

/// FFI-safe, nullable read-result callback. Invoked exactly once per accepted
/// read call: `ok=true` with a NUL-terminated JSON C string, or `ok=false`
/// with a NUL-terminated error C string. The `*const c_char` is owned by Rust
/// and valid ONLY for the callback's duration — the callee must copy before
/// returning. `user_data` is the caller's opaque pointer, forwarded unchanged.
pub type MahoMailReadCallback =
    Option<unsafe extern "C" fn(ok: bool, json: *const c_char, user_data: *mut c_void)>;

type ReadCallback = unsafe extern "C" fn(bool, *const c_char, *mut c_void);
pub(crate) type ReadFuture = Pin<Box<dyn Future<Output = Result<String>> + Send + 'static>>;

#[no_mangle]
pub extern "C" fn MahoMailListAccounts(
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        accept_read(callback, user_data, |ctx| {
            blocking_json(move || read::list_accounts(&ctx))
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListFolders(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || read::list_folders(&ctx, &account_id))
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListEmails(
    account_id: *const c_char,
    folder_id: *const c_char,
    limit: i64,
    offset: i64,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(folder_id) = non_empty(folder_id, "folder_id") else {
            return false;
        };
        if limit < 0 || offset < 0 {
            return false;
        }
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                read::list_local_emails(&ctx, &account_id, &folder_id, limit, offset)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailGetEmail(
    email_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let response = read::get_email(ctx, &email_id).await?;
                serde_json::to_string(&response).map_err(MailFfiError::from)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailSearchEmails(
    query_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(query_json, "query_json") else {
            return false;
        };
        let Ok(params) = serde_json::from_str::<read::SearchParams>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || read::search_emails(&ctx, &params))
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListThread(
    account_id: *const c_char,
    message_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(message_id) = non_empty(message_id, "message_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || read::list_thread(&ctx, &account_id, &message_id))
        })
    })
}

/// Shared pre-dispatch acceptance boundary. Runs the ENTIRE synchronous body of
/// a read entrypoint (arg validation, `make_future`, allocation, `rt.spawn`)
/// under `catch_unwind`, so a panic BEFORE the worker begins returns false and
/// invokes no callback. [Category 14 — unwinding]
pub(crate) fn accept_read_call(body: impl FnOnce() -> bool) -> bool {
    match catch_unwind(AssertUnwindSafe(body)) {
        Ok(accepted) => accepted,
        Err(_) => {
            log::error!("[mail-ffi] read acceptance panicked before dispatch");
            false
        }
    }
}

/// Acceptance tail: requires a live callback, initialized ctx, and a runtime,
/// then schedules `make_future(ctx)` and returns true. A failed precondition
/// returns false without a callback.
pub(crate) fn accept_read<F>(
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
    make_future: F,
) -> bool
where
    F: FnOnce(Arc<AppCtx>) -> ReadFuture,
{
    let Some(callback) = callback else {
        return false;
    };
    let Ok(ctx) = state::ctx() else {
        return false;
    };
    let Some(rt) = crate::runtime::runtime() else {
        return false;
    };
    let fut = make_future(ctx);
    // [Category 11 — provenance] Carry the opaque caller pointer to the worker
    // thread inside an `AtomicPtr`, which is `Send` via std (no manual impl) and
    // preserves provenance WITHOUT an integer round-trip, so it stays sound
    // under `-Zmiri-strict-provenance`.
    spawn_read_task(rt, callback, AtomicPtr::new(user_data), fut);
    true
}

pub(crate) fn blocking_json<T, F>(run: F) -> ReadFuture
where
    T: serde::Serialize + Send + 'static,
    F: FnOnce() -> Result<T> + Send + 'static,
{
    Box::pin(async move {
        tokio::task::spawn_blocking(move || {
            let value = run()?;
            serde_json::to_string(&value).map_err(MailFfiError::from)
        })
        .await
        .map_err(|e| MailFfiError::Internal(format!("read join error: {e}")))?
    })
}

pub(crate) fn spawn_read_task(
    rt: &'static Runtime,
    callback: ReadCallback,
    user_data: AtomicPtr<c_void>,
    fut: ReadFuture,
) {
    rt.spawn(async move {
        // [Category 14 — unwinding] Convert any panic from the read/serialize
        // future into an error outcome so it never unwinds out of the task and
        // toward the FFI boundary.
        let outcome = match AssertUnwindSafe(fut).catch_unwind().await {
            Ok(result) => result,
            Err(_) => Err(MailFfiError::Internal("read worker panicked".to_string())),
        };
        invoke_read_callback(callback, user_data, outcome);
    });
}

fn invoke_read_callback(
    callback: ReadCallback,
    user_data: AtomicPtr<c_void>,
    outcome: Result<String>,
) {
    let (ok, message) = match outcome {
        Ok(json) => (true, json),
        Err(err) => (false, err.to_string()),
    };
    // A `CString` rejects interior NUL bytes; valid JSON never has one, but a
    // rogue payload is DOWNGRADED to an error callback so the caller still gets
    // exactly one invocation.
    let (ok, payload) = match CString::new(message) {
        Ok(payload) => (ok, payload),
        Err(_) => (
            false,
            CString::new("read result contained an interior NUL byte").unwrap_or_default(),
        ),
    };
    // [Category 11 — provenance] One-time recovery: `into_inner` consumes the
    // `AtomicPtr` and returns the caller pointer with its original provenance
    // (no atomic load, no shared-mutation implication).
    let user_data = user_data.into_inner();
    // SAFETY: [Cat 8 — FFI boundary] `callback` is the caller's live extern "C"
    // fn (non-None checked at accept) called with its exact ABI; `payload` is
    // Rust-owned (valid only for this call) and `user_data` keeps provenance.
    // [Cat 14 — unwinding] Rust-origin panics convert to errors before this
    // crossing; the foreign callback itself must not unwind into Rust.
    unsafe {
        callback(ok, payload.as_ptr(), user_data);
    }
    drop(payload);
}

#[cfg(test)]
#[path = "read_api_tests.rs"]
mod read_api_tests;
