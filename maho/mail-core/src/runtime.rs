// Copyright 2026 Maho Browser. All rights reserved.

//! Process-wide tokio runtime owned by the mail FFI backend.
//!
//! The mail helper is a plain Chromium child process whose main thread runs a
//! `base::RunLoop` (Mojo). It has no Rust async runtime, so this crate owns its
//! own multi-threaded tokio runtime. All background sync/backfill work is
//! driven from here.

use std::sync::OnceLock;

use tokio::runtime::{Builder, Runtime};

static RUNTIME: OnceLock<Runtime> = OnceLock::new();

/// Returns the shared multi-threaded runtime, creating it on first use.
/// Returns `None` if the runtime could not be constructed (never expected in
/// practice; surfaced as an FFI `false` rather than a panic across the ABI).
pub fn runtime() -> Option<&'static Runtime> {
    if let Some(rt) = RUNTIME.get() {
        return Some(rt);
    }
    let built = Builder::new_multi_thread()
        .worker_threads(2)
        .thread_name("maho-mail")
        .enable_all()
        .build();
    match built {
        Ok(rt) => {
            let _ = RUNTIME.set(rt);
            RUNTIME.get()
        }
        Err(err) => {
            log::error!("[mail-ffi] failed to build tokio runtime: {err}");
            None
        }
    }
}
