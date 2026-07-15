// Copyright 2026 Maho Browser. All rights reserved.

//! Process-wide state for the mail FFI backend.
//!
//! Two phases, mirroring the helper's lifecycle:
//!   1. `MahoMailInitialize` records the profile path + version token and sets
//!      up logging. No DB is opened yet (keys not available).
//!   2. `MahoMailInjectKeys` decodes the injected credential key and opens the
//!      real SQLCipher database with the injected sqlcipher key, producing an
//!      [`AppCtx`] that all sync/backfill work reads from.
//!
//! A single [`AppCtx`] is stored behind an `RwLock<Option<Arc<AppCtx>>>`; the
//! per-account background workers are tracked in [`SyncRegistry`].

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex, OnceLock, RwLock};

use maho_core::db::SqlitePool;

use crate::error::{MailFfiError, Result};

/// Recorded during `MahoMailInitialize`, before keys are available.
#[derive(Debug, Clone)]
pub struct InitInfo {
    pub version_token: String,
    pub profile_path: PathBuf,
    /// `<profile>/MahoMail`
    pub app_data_dir: PathBuf,
    /// `<profile>/MahoMail/maho_mail.db`
    pub db_path: PathBuf,
}

/// Fully-initialized context, available only after keys are injected and the
/// SQLCipher DB is open.
pub struct AppCtx {
    pub pool: SqlitePool,
    pub db_path: PathBuf,
    /// Raw sqlcipher key string, re-applied to any freshly opened connection
    /// (`PRAGMA key`). Held to support ad-hoc connections outside the pool.
    pub sqlcipher_key: String,
    /// 32-byte AES-256-GCM key used to decrypt per-account credentials stored
    /// in the `encrypted_credentials` table (mirrors maho-core scheme, but keyed
    /// by the INJECTED credential key rather than the OS keyring).
    pub credential_key: [u8; 32],
}

static INIT_INFO: OnceLock<InitInfo> = OnceLock::new();
static CTX: RwLock<Option<Arc<AppCtx>>> = RwLock::new(None);
static REGISTRY: OnceLock<SyncRegistry> = OnceLock::new();

/// Record init info. Idempotent: first call wins.
pub fn set_init_info(info: InitInfo) {
    let _ = INIT_INFO.set(info);
}

pub fn init_info() -> Option<&'static InitInfo> {
    INIT_INFO.get()
}

/// Install the fully-initialized context after keys are injected.
pub fn set_ctx(ctx: Arc<AppCtx>) -> Result<()> {
    let mut guard = CTX
        .write()
        .map_err(|_| MailFfiError::Internal("ctx lock poisoned".into()))?;
    *guard = Some(ctx);
    Ok(())
}

/// Returns a clone of the current context handle, or `NotInitialized`.
pub fn ctx() -> Result<Arc<AppCtx>> {
    let guard = CTX
        .read()
        .map_err(|_| MailFfiError::Internal("ctx lock poisoned".into()))?;
    guard.clone().ok_or(MailFfiError::NotInitialized)
}

pub fn registry() -> &'static SyncRegistry {
    REGISTRY.get_or_init(SyncRegistry::default)
}

/// Handle to a single account's background workers (sync loop + backfill loop).
/// Dropping/stopping flips the stop flag and aborts the supervising tasks.
pub struct WorkerHandle {
    pub stop: Arc<AtomicBool>,
    pub tasks: Vec<tokio::task::JoinHandle<()>>,
}

impl WorkerHandle {
    pub fn stop(&self) {
        self.stop.store(true, std::sync::atomic::Ordering::SeqCst);
        for task in &self.tasks {
            task.abort();
        }
    }
}

/// Tracks per-account sync workers and per-account backfill workers.
#[derive(Default)]
pub struct SyncRegistry {
    sync: Mutex<HashMap<String, WorkerHandle>>,
    backfill: Mutex<HashMap<String, WorkerHandle>>,
}

impl SyncRegistry {
    /// Register (replacing any prior) the sync worker for an account.
    /// Returns `false` if a worker was already running (already replaced).
    pub fn insert_sync(&self, account_id: String, handle: WorkerHandle) -> bool {
        let mut map = match self.sync.lock() {
            Ok(m) => m,
            Err(p) => p.into_inner(),
        };
        let replaced = map.insert(account_id, handle);
        if let Some(prev) = replaced {
            prev.stop();
            true
        } else {
            false
        }
    }

    pub fn is_sync_running(&self, account_id: &str) -> bool {
        match self.sync.lock() {
            Ok(m) => m.contains_key(account_id),
            Err(p) => p.into_inner().contains_key(account_id),
        }
    }

    pub fn insert_backfill(&self, account_id: String, handle: WorkerHandle) -> bool {
        let mut map = match self.backfill.lock() {
            Ok(m) => m,
            Err(p) => p.into_inner(),
        };
        let replaced = map.insert(account_id, handle);
        if let Some(prev) = replaced {
            prev.stop();
            true
        } else {
            false
        }
    }

    pub fn is_backfill_running(&self, account_id: &str) -> bool {
        match self.backfill.lock() {
            Ok(m) => m.contains_key(account_id),
            Err(p) => p.into_inner().contains_key(account_id),
        }
    }

    /// Stop and drop all sync workers (used by `MahoMailStopSync`).
    pub fn stop_all_sync(&self) {
        let mut map = match self.sync.lock() {
            Ok(m) => m,
            Err(p) => p.into_inner(),
        };
        for (_, handle) in map.drain() {
            handle.stop();
        }
    }

    /// Stop and drop all backfill workers (used by `MahoMailStopSync`).
    pub fn stop_all_backfill(&self) {
        let mut map = match self.backfill.lock() {
            Ok(m) => m,
            Err(p) => p.into_inner(),
        };
        for (_, handle) in map.drain() {
            handle.stop();
        }
    }
}
