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
use std::time::{Duration, Instant};

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

#[cfg(test)]
pub fn clear_ctx_for_test() {
    if let Ok(mut guard) = CTX.write() {
        *guard = None;
    }
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

/// Guarded auto-start of an account's sync + backfill workers on `rt`, reusing
/// the existing worker constructors. Returns `(sync_spawned, backfill_spawned)`;
/// a `false` means a live worker already held that slot (idempotent no-op).
pub fn start_account_workers(
    rt: &tokio::runtime::Runtime,
    ctx: &Arc<AppCtx>,
    account_id: &str,
) -> (bool, bool) {
    let _guard = rt.enter();
    let sync = registry().ensure_sync(account_id, {
        let ctx = Arc::clone(ctx);
        let id = account_id.to_string();
        move || crate::sync::start_sync_worker(ctx, id)
    });
    let backfill = registry().ensure_backfill(account_id, {
        let ctx = Arc::clone(ctx);
        let id = account_id.to_string();
        move || crate::backfill::start_backfill_worker(ctx, id)
    });
    (sync, backfill)
}

/// Best-effort auto-start invoked from onboarding paths. Uses the process
/// [`ctx`] + [`crate::runtime::runtime`] globals; silently no-ops when either is
/// unavailable (e.g. before keys are injected) so onboarding never fails on it.
pub fn spawn_account_workers_if_ready(account_id: &str) {
    let Ok(ctx) = ctx() else {
        log::debug!("[mail-ffi] auto-start skipped for {account_id}: ctx not initialized");
        return;
    };
    let Some(rt) = crate::runtime::runtime() else {
        log::warn!("[mail-ffi] auto-start skipped for {account_id}: runtime unavailable");
        return;
    };
    let _ = start_account_workers(rt, &ctx, account_id);
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

    /// A worker is live only while it has not been stopped AND at least one of
    /// its supervising tasks is still running. A finished or panicked worker
    /// (all tasks `is_finished`) is NOT live, so the guarded start helpers may
    /// restart it.
    pub fn is_live(&self) -> bool {
        if self.stop.load(std::sync::atomic::Ordering::SeqCst) {
            return false;
        }
        self.tasks.iter().any(|task| !task.is_finished())
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
            Ok(m) => m.get(account_id).is_some_and(WorkerHandle::is_live),
            Err(p) => p
                .into_inner()
                .get(account_id)
                .is_some_and(WorkerHandle::is_live),
        }
    }

    /// Guarded, idempotent start for an account's sync worker. Under a single
    /// lock, spawns and registers a worker ONLY if no live worker is already
    /// present; a finished/panicked worker is replaced. Returns `true` when a
    /// new worker was spawned, `false` when a live worker already held the slot.
    pub fn ensure_sync<F>(&self, account_id: &str, spawn: F) -> bool
    where
        F: FnOnce() -> WorkerHandle,
    {
        let mut map = match self.sync.lock() {
            Ok(m) => m,
            Err(p) => p.into_inner(),
        };
        if map.get(account_id).is_some_and(WorkerHandle::is_live) {
            return false;
        }
        if let Some(prev) = map.insert(account_id.to_string(), spawn()) {
            prev.stop();
        }
        true
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
            Ok(m) => m.get(account_id).is_some_and(WorkerHandle::is_live),
            Err(p) => p
                .into_inner()
                .get(account_id)
                .is_some_and(WorkerHandle::is_live),
        }
    }

    /// Guarded, idempotent counterpart to [`Self::ensure_sync`] for the backfill
    /// worker. Same single-lock claim-before-spawn semantics.
    pub fn ensure_backfill<F>(&self, account_id: &str, spawn: F) -> bool
    where
        F: FnOnce() -> WorkerHandle,
    {
        let mut map = match self.backfill.lock() {
            Ok(m) => m,
            Err(p) => p.into_inner(),
        };
        if map.get(account_id).is_some_and(WorkerHandle::is_live) {
            return false;
        }
        if let Some(prev) = map.insert(account_id.to_string(), spawn()) {
            prev.stop();
        }
        true
    }

    /// Stop and drop the sync + backfill workers for a single account (used by
    /// account deletion). No-op when the account has no running workers.
    pub fn stop_account(&self, account_id: &str) {
        let sync = match self.sync.lock() {
            Ok(mut m) => m.remove(account_id),
            Err(p) => p.into_inner().remove(account_id),
        };
        if let Some(handle) = sync {
            handle.stop();
        }
        let backfill = match self.backfill.lock() {
            Ok(mut m) => m.remove(account_id),
            Err(p) => p.into_inner().remove(account_id),
        };
        if let Some(handle) = backfill {
            handle.stop();
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

/// Pending PKCE entries older than this are evicted on the next store access.
const PKCE_TTL: Duration = Duration::from_secs(600);

/// One in-flight OAuth2 PKCE authorization, keyed by the opaque `state` token
/// returned to the browser. Holds the verifier and the parameters needed to
/// finish the exchange once the browser delivers the authorization code.
#[derive(Clone)]
pub struct PkceEntry {
    pub code_verifier: String,
    pub provider: String,
    pub client_id: String,
    pub client_secret: String,
    pub redirect_uri: String,
    pub created_at: Instant,
}

/// Process-wide store of in-flight PKCE authorizations. Entries expire after
/// [`PKCE_TTL`]; expired entries are purged lazily on every access.
#[derive(Default)]
pub struct PkceStore {
    entries: Mutex<HashMap<String, PkceEntry>>,
}

impl PkceStore {
    pub fn insert(&self, state: String, entry: PkceEntry) {
        let mut map = match self.entries.lock() {
            Ok(m) => m,
            Err(p) => p.into_inner(),
        };
        map.retain(|_, e| e.created_at.elapsed() < PKCE_TTL);
        map.insert(state, entry);
    }

    pub fn take(&self, state: &str) -> Option<PkceEntry> {
        let mut map = match self.entries.lock() {
            Ok(m) => m,
            Err(p) => p.into_inner(),
        };
        map.retain(|_, e| e.created_at.elapsed() < PKCE_TTL);
        map.remove(state)
    }

    pub fn purge_expired(&self) {
        let mut map = match self.entries.lock() {
            Ok(m) => m,
            Err(p) => p.into_inner(),
        };
        map.retain(|_, e| e.created_at.elapsed() < PKCE_TTL);
    }

    pub fn contains(&self, state: &str) -> bool {
        let mut map = match self.entries.lock() {
            Ok(m) => m,
            Err(p) => p.into_inner(),
        };
        map.retain(|_, e| e.created_at.elapsed() < PKCE_TTL);
        map.contains_key(state)
    }
}

use std::collections::HashSet;

/// Store of cancelled OAuth2 state tokens. Checked in the loopback accept loop.
#[derive(Default)]
pub struct CancellationStore {
    cancelled_states: Mutex<HashSet<String>>,
}

impl CancellationStore {
    pub fn cancel(&self, state: String) -> bool {
        let mut set = match self.cancelled_states.lock() {
            Ok(s) => s,
            Err(p) => p.into_inner(),
        };
        set.insert(state)
    }

    pub fn is_cancelled(&self, state: &str) -> bool {
        let set = match self.cancelled_states.lock() {
            Ok(s) => s,
            Err(p) => p.into_inner(),
        };
        set.contains(state)
    }

    pub fn remove(&self, state: &str) -> bool {
        let mut set = match self.cancelled_states.lock() {
            Ok(s) => s,
            Err(p) => p.into_inner(),
        };
        set.remove(state)
    }
}

static PENDING_PKCE: OnceLock<PkceStore> = OnceLock::new();
static CANCELLATION_STORE: OnceLock<CancellationStore> = OnceLock::new();

pub fn pending_pkce() -> &'static PkceStore {
    PENDING_PKCE.get_or_init(PkceStore::default)
}

pub fn cancellation_store() -> &'static CancellationStore {
    CANCELLATION_STORE.get_or_init(CancellationStore::default)
}

#[cfg(test)]
#[allow(clippy::disallowed_methods)]
mod sync_registry_tests {
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::time::Duration;

    use super::*;

    fn live_handle() -> WorkerHandle {
        WorkerHandle {
            stop: Arc::new(AtomicBool::new(false)),
            tasks: vec![tokio::spawn(std::future::pending::<()>())],
        }
    }

    fn finished_handle() -> WorkerHandle {
        WorkerHandle {
            stop: Arc::new(AtomicBool::new(false)),
            tasks: vec![tokio::spawn(async {})],
        }
    }

    #[tokio::test]
    async fn ensure_sync_starts_once_and_is_idempotent_while_live() {
        let reg = SyncRegistry::default();
        let spawned = Arc::new(AtomicUsize::new(0));

        let c = Arc::clone(&spawned);
        assert!(
            reg.ensure_sync("acc", move || {
                c.fetch_add(1, Ordering::SeqCst);
                live_handle()
            }),
            "first ensure spawns the worker"
        );
        assert!(reg.is_sync_running("acc"), "live worker reports running");

        let c = Arc::clone(&spawned);
        assert!(
            !reg.ensure_sync("acc", move || {
                c.fetch_add(1, Ordering::SeqCst);
                live_handle()
            }),
            "second ensure while live does NOT spawn"
        );
        assert_eq!(spawned.load(Ordering::SeqCst), 1, "exactly one spawn");
        reg.stop_all_sync();
    }

    #[tokio::test]
    async fn ensure_sync_restarts_after_worker_finished() {
        let reg = SyncRegistry::default();
        let spawned = Arc::new(AtomicUsize::new(0));

        let c = Arc::clone(&spawned);
        assert!(reg.ensure_sync("acc", move || {
            c.fetch_add(1, Ordering::SeqCst);
            finished_handle()
        }));

        // Drive the finished handle's task to completion, then confirm it is no
        // longer considered running.
        let mut settled = false;
        for _ in 0..200 {
            if !reg.is_sync_running("acc") {
                settled = true;
                break;
            }
            tokio::time::sleep(Duration::from_millis(5)).await;
        }
        assert!(settled, "finished worker must be treated as NOT running");

        let c = Arc::clone(&spawned);
        assert!(
            reg.ensure_sync("acc", move || {
                c.fetch_add(1, Ordering::SeqCst);
                live_handle()
            }),
            "ensure restarts a finished worker"
        );
        assert_eq!(spawned.load(Ordering::SeqCst), 2, "restart spawned again");
        reg.stop_all_sync();
    }

    #[tokio::test]
    async fn ensure_backfill_is_idempotent_while_live() {
        let reg = SyncRegistry::default();
        let spawned = Arc::new(AtomicUsize::new(0));

        let c = Arc::clone(&spawned);
        assert!(reg.ensure_backfill("acc", move || {
            c.fetch_add(1, Ordering::SeqCst);
            live_handle()
        }));
        assert!(reg.is_backfill_running("acc"));

        let c = Arc::clone(&spawned);
        assert!(!reg.ensure_backfill("acc", move || {
            c.fetch_add(1, Ordering::SeqCst);
            live_handle()
        }));
        assert_eq!(spawned.load(Ordering::SeqCst), 1);
        reg.stop_all_backfill();
    }
}

#[cfg(test)]
#[allow(clippy::disallowed_methods)]
mod pkce_tests {
    use super::*;

    fn entry(provider: &str) -> PkceEntry {
        PkceEntry {
            code_verifier: "verifier".to_string(),
            provider: provider.to_string(),
            client_id: "client-id".to_string(),
            client_secret: "client-secret".to_string(),
            redirect_uri: "http://localhost".to_string(),
            created_at: Instant::now(),
        }
    }

    #[test]
    fn insert_then_take_returns_entry_once() {
        let store = PkceStore::default();
        store.insert("s1".to_string(), entry("gmail"));

        assert!(store.contains("s1"));
        let got = store.take("s1").expect("entry present");
        assert_eq!(got.provider, "gmail");
        assert_eq!(got.code_verifier, "verifier");
        assert!(store.take("s1").is_none(), "entry consumed on take");
        assert!(!store.contains("s1"));
    }

    #[test]
    fn take_unknown_state_is_none() {
        let store = PkceStore::default();
        assert!(store.take("missing").is_none());
        assert!(!store.contains("missing"));
    }

    #[test]
    fn expired_entry_is_purged_on_take() {
        let store = PkceStore::default();
        let mut stale = entry("gmail");
        stale.created_at = Instant::now()
            .checked_sub(Duration::from_secs(700))
            .expect("past instant");
        store.insert("old".to_string(), stale);

        assert!(!store.contains("old"), "expired entry must be purged");
        assert!(store.take("old").is_none(), "expired entry must be purged");
    }

    #[test]
    fn cancellation_store_roundtrip() {
        let store = CancellationStore::default();
        assert!(!store.is_cancelled("s1"));
        assert!(store.cancel("s1".to_string()));
        assert!(store.is_cancelled("s1"));
        assert!(store.remove("s1"));
        assert!(!store.is_cancelled("s1"));
    }
}
