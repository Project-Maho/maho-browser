// Copyright 2026 Maho Browser. All rights reserved.

use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;
use std::time::Duration;

use futures::stream::{FuturesUnordered, StreamExt as _};
use futures::FutureExt as _;
use maho_core::folder_normalize::{classify_folder, folder_type_to_string, is_visible_folder};
use maho_core::imap_client::ImapEnvelope;
use rusqlite::{params, OptionalExtension, TransactionBehavior};
use tokio_util::sync::CancellationToken;
use uuid::Uuid;

use crate::account::{
    connect_imap_with_retry, resolve_account_auth, safe_logout, AccountAuth,
};
use crate::error::{MailFfiError, Result};
use crate::state::{AppCtx, WorkerHandle};

const DEFAULT_SYNC_INTERVAL_MINUTES: u64 = 15;
#[path = "sync_reconciliation.rs"]
mod reconciliation;
static SYNC_INTERVAL_MINUTES: AtomicU64 = AtomicU64::new(DEFAULT_SYNC_INTERVAL_MINUTES);
const IDLE_TIMEOUT: Duration = Duration::from_secs(120);
const SUPERVISOR_BACKOFF_MAX: Duration = Duration::from_secs(60);
const MAX_CONCURRENT_ACCOUNT_SYNCS: usize = 2;
const MAX_CONCURRENT_FOLDER_SYNCS: usize = 4;

static ACCOUNT_SYNC_SEMAPHORE: std::sync::LazyLock<Arc<tokio::sync::Semaphore>> =
    std::sync::LazyLock::new(|| {
        Arc::new(tokio::sync::Semaphore::new(MAX_CONCURRENT_ACCOUNT_SYNCS))
    });
static FOLDER_SYNC_SEMAPHORE: std::sync::LazyLock<Arc<tokio::sync::Semaphore>> =
    std::sync::LazyLock::new(|| Arc::new(tokio::sync::Semaphore::new(MAX_CONCURRENT_FOLDER_SYNCS)));

#[derive(Clone)]
struct FolderSyncTarget {
    id: String,
    path: String,
    folder_type: String,
    last_synced_uid: u32,
    uid_validity: u32,
}

struct FolderFetchResult {
    folder: FolderSyncTarget,
    envelopes: Vec<ImapEnvelope>,
    server_uid_validity: u32,
    reconciliation: Vec<ImapEnvelope>,
    flag_snapshot: Option<reconciliation::Snapshot>,
}

pub fn start_sync_worker(ctx: Arc<AppCtx>, account_id: String) -> WorkerHandle {
    let stop = Arc::new(AtomicBool::new(false));
    let token = CancellationToken::new();
    let sync_task = spawn_supervised(
        "sync",
        ctx.clone(),
        account_id.clone(),
        Arc::clone(&stop),
        token.clone(),
        sync_loop,
    );
    let idle_task = spawn_supervised("idle", ctx, account_id, Arc::clone(&stop), token, idle_loop);
    WorkerHandle {
        stop,
        tasks: vec![sync_task, idle_task],
        wake: None,
    }
}

fn spawn_supervised<Fut, F>(
    name: &'static str,
    ctx: Arc<AppCtx>,
    account_id: String,
    stop: Arc<AtomicBool>,
    token: CancellationToken,
    run: F,
) -> tokio::task::JoinHandle<()>
where
    Fut: std::future::Future<Output = ()> + Send + 'static,
    F: Fn(Arc<AppCtx>, String, Arc<AtomicBool>, CancellationToken) -> Fut + Send + Sync + 'static,
{
    tokio::spawn(async move {
        let mut backoff = Duration::from_secs(1);
        while !stop.load(Ordering::SeqCst) {
            let attempt = std::panic::AssertUnwindSafe(run(
                Arc::clone(&ctx),
                account_id.clone(),
                Arc::clone(&stop),
                token.child_token(),
            ))
            .catch_unwind()
            .await;
            if stop.load(Ordering::SeqCst) {
                break;
            }
            match attempt {
                Ok(()) => backoff = Duration::from_secs(1),
                Err(_) => {
                    log::error!("[mail-ffi] {name} worker panicked for {account_id}; restarting");
                }
            }
            tokio::time::sleep(backoff).await;
            backoff = (backoff * 2).min(SUPERVISOR_BACKOFF_MAX);
        }
        token.cancel();
    })
}

fn is_auth_error(err: &MailFfiError) -> bool {
    crate::account::is_auth_error(err)
}

async fn sync_loop(
    ctx: Arc<AppCtx>,
    account_id: String,
    stop: Arc<AtomicBool>,
    token: CancellationToken,
) {
    loop {
        if stop.load(Ordering::SeqCst) {
            return;
        }
        match sync_account_once(Arc::clone(&ctx), &account_id).await {
            Ok(inserted) => {
                log::info!("[mail-ffi] sync completed for {account_id}: {inserted} new messages");
            }
            Err(err) => {
                log::warn!("[mail-ffi] sync failed for {account_id}: {err}");
                if is_auth_error(&err) && !matches!(err, MailFfiError::OAuth2Refresh(_)) {
                    crate::ffi::emit_reauth_event(&account_id, "unknown", &err.to_string());
                }
            }
        }
        tokio::select! {
            _ = token.cancelled() => return,
            () = tokio::time::sleep(Duration::from_secs(
                SYNC_INTERVAL_MINUTES.load(Ordering::Relaxed) * 60,
            )) => {}
        }
    }
}

pub fn set_sync_interval_minutes(minutes: u64) -> bool {
    if !matches!(minutes, 5 | 15 | 30 | 60) {
        return false;
    }
    SYNC_INTERVAL_MINUTES.store(minutes, Ordering::Relaxed);
    true
}

#[cfg(test)]
mod sync_interval_tests {
    use super::*;

    #[test]
    fn accepts_only_canonical_minute_intervals() {
        for minutes in [5, 15, 30, 60] {
            assert!(set_sync_interval_minutes(minutes));
            assert_eq!(SYNC_INTERVAL_MINUTES.load(Ordering::Relaxed), minutes);
        }
        for minutes in [0, 1, 14, 61, 900] {
            assert!(!set_sync_interval_minutes(minutes));
        }
    }
}

async fn idle_loop(
    ctx: Arc<AppCtx>,
    account_id: String,
    stop: Arc<AtomicBool>,
    token: CancellationToken,
) {
    loop {
        if stop.load(Ordering::SeqCst) {
            return;
        }
        let idle = idle_wait_once(Arc::clone(&ctx), &account_id).await;
        match idle {
            Ok(true) => {
                let _ = sync_account_once(Arc::clone(&ctx), &account_id).await;
            }
            Ok(false) => {}
            Err(err) => {
                log::warn!("[mail-ffi] idle failed for {account_id}: {err}");
                if is_auth_error(&err) && !matches!(err, MailFfiError::OAuth2Refresh(_)) {
                    crate::ffi::emit_reauth_event(&account_id, "unknown", &err.to_string());
                }
            }
        }
        tokio::select! {
            _ = token.cancelled() => return,
            () = tokio::time::sleep(Duration::from_secs(5)) => {}
        }
    }
}

pub async fn sync_account_once(ctx: Arc<AppCtx>, account_id: &str) -> Result<usize> {
    let _account_permit = Arc::clone(&ACCOUNT_SYNC_SEMAPHORE)
        .acquire_owned()
        .await
        .map_err(|_| MailFfiError::Internal("account sync semaphore closed".into()))?;
    let account_id = account_id.to_string();
    let auth_ctx = Arc::clone(&ctx);
    let auth_account_id = account_id.clone();
    let resolved = spawn_blocking_result("auth", move || {
        let conn = auth_ctx
            .pool
            .get()
            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
        resolve_account_auth(&conn, &auth_ctx.credential_key, &auth_account_id)
    })
    .await?;

    let folder_ctx = Arc::clone(&ctx);
    let (folder_metadata, resolved) = {
        let mut resolved = resolved.clone();
        spawn_blocking_result("folder fetch", move || {
            let conn = folder_ctx
                .pool
                .get()
                .map_err(|e| MailFfiError::Pool(e.to_string()))?;
            let mut client =
                connect_imap_with_retry(&conn, &folder_ctx.credential_key, &mut resolved)?;
            let folders = client.list_folders()?;
            safe_logout(client);
            Ok((folders, resolved))
        })
        .await?
    };

    let folder_ctx = Arc::clone(&ctx);
    let folder_account_id = account_id.clone();
    spawn_blocking_result("folder write", move || {
        let mut conn = folder_ctx
            .pool
            .get()
            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
        sync_folders(&mut conn, &folder_account_id, &folder_metadata)
    })
    .await?;

    let targets_ctx = Arc::clone(&ctx);
    let targets_account_id = account_id.clone();
    let folders = spawn_blocking_result("sync target read", move || {
        let conn = targets_ctx
            .pool
            .get()
            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
        sync_targets(&conn, &targets_account_id)
    })
    .await?;

    let mut pending = FuturesUnordered::new();
    for folder in folders {
        let ctx = Arc::clone(&ctx);
        let resolved = resolved.clone();
        pending.push(sync_folder(ctx, resolved, folder));
    }

    let mut inserted = 0usize;
    while let Some(result) = pending.next().await {
        let (folder, folder_inserted, arrivals) = result?;
        inserted += folder_inserted;
        if should_emit_new_mail(&folder.folder_type, folder_inserted) {
            for arrival in arrivals {
                crate::ffi::emit_new_mail_event(
                    &account_id,
                    &arrival.email_id,
                    &arrival.message_id,
                    &arrival.sender,
                    &arrival.subject,
                    arrival.cursor,
                    arrival.epoch,
                );
            }
        }
    }
    Ok(inserted)
}

async fn spawn_blocking_result<T, F>(operation: &'static str, work: F) -> Result<T>
where
    T: Send + 'static,
    F: FnOnce() -> Result<T> + Send + 'static,
{
    tokio::task::spawn_blocking(work)
        .await
        .map_err(|e| MailFfiError::Internal(format!("{operation} join error: {e}")))?
}

fn sync_folders(
    conn: &mut rusqlite::Connection,
    account_id: &str,
    folders: &[maho_core::folder_normalize::FolderMetadata],
) -> Result<()> {
    let transaction = conn.transaction_with_behavior(TransactionBehavior::Immediate)?;
    let existing = existing_folders(&transaction, account_id)?;
    let mut visible_paths = std::collections::HashSet::new();
    for metadata in folders
        .iter()
        .filter(|metadata| is_visible_folder(metadata))
    {
        visible_paths.insert(metadata.raw_path.clone());
        let folder_type = folder_type_to_string(&classify_folder(metadata));
        if let Some(existing_id) = existing.get(&metadata.raw_path) {
            transaction.execute(
                "UPDATE folders SET name = ?1, folder_type = ?2 WHERE id = ?3 AND account_id = ?4",
                params![metadata.display_name, folder_type, existing_id, account_id],
            )?;
        } else {
            transaction.execute(
                "INSERT INTO folders (id, account_id, name, path, folder_type, unread_count, total_count)
                 VALUES (?1, ?2, ?3, ?4, ?5, 0, 0)",
                params![
                    Uuid::new_v4().to_string(),
                    account_id,
                    metadata.display_name,
                    metadata.raw_path,
                    folder_type
                ],
            )?;
        }
    }
    for stale_path in existing
        .keys()
        .filter(|path| !visible_paths.contains(*path))
    {
        transaction.execute(
            "DELETE FROM folders WHERE account_id = ?1 AND path = ?2",
            params![account_id, stale_path],
        )?;
    }
    transaction.commit()?;
    Ok(())
}

fn existing_folders(
    conn: &rusqlite::Connection,
    account_id: &str,
) -> Result<HashMap<String, String>> {
    let mut stmt = conn.prepare("SELECT id, path FROM folders WHERE account_id = ?1")?;
    let rows = stmt.query_map([account_id], |row| {
        Ok((row.get::<_, String>(1)?, row.get::<_, String>(0)?))
    })?;
    Ok(rows.filter_map(std::result::Result::ok).collect())
}

fn sync_targets(conn: &rusqlite::Connection, account_id: &str) -> Result<Vec<FolderSyncTarget>> {
    let mut stmt = conn.prepare(
        "SELECT id, path, folder_type, last_synced_uid, uid_validity FROM folders
         WHERE account_id = ?1 AND folder_type IN ('inbox', 'sent', 'drafts', 'archive', 'custom', 'spam', 'trash')",
    )?;
    let rows = stmt.query_map([account_id], |row| {
        let last_uid: i64 = row.get(3)?;
        let uid_validity: i64 = row.get(4)?;
        Ok(FolderSyncTarget {
            id: row.get(0)?,
            path: row.get(1)?,
            folder_type: row.get(2)?,
            last_synced_uid: u32::try_from(last_uid).unwrap_or(0),
            uid_validity: u32::try_from(uid_validity).unwrap_or(0),
        })
    })?;
    Ok(rows.filter_map(std::result::Result::ok).collect())
}

fn should_emit_new_mail(folder_type: &str, inserted: usize) -> bool {
    folder_type == "inbox" && inserted > 0
}

async fn sync_folder(
    ctx: Arc<AppCtx>,
    resolved: AccountAuth,
    folder: FolderSyncTarget,
) -> Result<(FolderSyncTarget, usize, Vec<NewMailArrival>)> {
    let connect_ctx = Arc::clone(&ctx);
    sync_folder_with_connector(ctx, resolved, folder, move |auth| {
        let conn = connect_ctx
            .pool
            .get()
            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
        let mut auth_copy = auth.clone();
        connect_imap_with_retry(&conn, &connect_ctx.credential_key, &mut auth_copy)
    })
    .await
}

async fn sync_folder_with_connector<F>(
    ctx: Arc<AppCtx>,
    resolved: AccountAuth,
    folder: FolderSyncTarget,
    connect: F,
) -> Result<(FolderSyncTarget, usize, Vec<NewMailArrival>)>
where
    F: FnOnce(&AccountAuth) -> Result<maho_core::imap_client::ImapClient> + Send + 'static,
{
    let _folder_permit = Arc::clone(&FOLDER_SYNC_SEMAPHORE)
        .acquire_owned()
        .await
        .map_err(|_| MailFfiError::Internal("folder sync semaphore closed".into()))?;
    let account_id = resolved.account.id.clone();
    let backfill_wake = crate::state::registry().backfill_wake(&account_id);
    let fetched_folder = folder.clone();
    let history_pool = ctx.pool.clone();
    let fetched = spawn_blocking_result("message fetch", move || {
        let conn = history_pool.get().map_err(|error| MailFfiError::Pool(error.to_string()))?;
        let oldest_uid: Option<u32> = conn.query_row(
                "SELECT MIN(uid) FROM emails WHERE account_id=?1 AND folder_id=?2 AND uid>0",
                params![resolved.account.id, fetched_folder.id], |row| row.get(0),
            )?;
        let flag_selection = reconciliation::select(&conn, &fetched_folder)?;
        drop(conn);
        let mut client = connect(&resolved)?;
        let initial_since_uid = if fetched_folder.uid_validity == 0 {
            0
        } else {
            fetched_folder.last_synced_uid
        };
        let (mut envelopes, server_uid_validity) =
            client.fetch_envelopes_since_uid(&fetched_folder.path, initial_since_uid)?;
        let reset_required = fetched_folder.uid_validity != 0
            && server_uid_validity != 0
            && server_uid_validity != fetched_folder.uid_validity;
        if reset_required {
            let (reset_envelopes, _) = client.fetch_envelopes_since_uid(&fetched_folder.path, 0)?;
            envelopes = reset_envelopes;
        }
        // Revisit a bounded recent window even when there are no new UIDs.
        let (reconciliation, reconciliation_epoch) =
            client.fetch_envelopes_since_uid(&fetched_folder.path, 0)?;
        if reconciliation_epoch != server_uid_validity {
            return Err(MailFfiError::Internal("mailbox epoch changed during reconciliation".into()));
        }
        if !reset_required && fetched_folder.uid_validity != 0 {
            if let Some(oldest_uid) = oldest_uid.filter(|uid| *uid > 1) {
                let (history, history_epoch) = client.fetch_envelopes_before_uid(&fetched_folder.path, oldest_uid)?;
                if history_epoch != server_uid_validity {
                    safe_logout(client);
                    return Err(MailFfiError::Internal("mailbox epoch changed during history fetch".into()));
                }
                envelopes.extend(history);
            }
        }
        let flag_snapshot = if !reset_required && fetched_folder.uid_validity != 0 {
            Some(reconciliation::fetch(&mut client, &fetched_folder, server_uid_validity, flag_selection))
        } else {
            None
        };
        safe_logout(client);
        Ok(FolderFetchResult {
            folder: fetched_folder,
            envelopes,
            server_uid_validity,
            reconciliation,
            flag_snapshot: flag_snapshot.transpose()?,
        })
    })
    .await?;

    let result_folder = fetched.folder.clone();
    let (inserted, arrivals) = spawn_blocking_result("message batch write", move || {
        let mut conn = ctx
            .pool
            .get()
            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
        sync_envelopes_with_snapshots(
            &mut conn,
            &account_id,
            &fetched.folder,
            &fetched.envelopes,
            fetched.server_uid_validity,
            Some(&fetched.reconciliation),
            fetched.flag_snapshot.as_ref(),
        )
    })
    .await?;
    if inserted > 0 {
        if let Some(wake) = backfill_wake {
            wake.notify_one();
        }
    }
    Ok((result_folder, inserted, arrivals))
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct NewMailArrival {
    email_id: String,
    message_id: String,
    sender: String,
    subject: String,
    cursor: u64,
    epoch: u64,
}

#[cfg(test)]
fn sync_envelopes_into_cache(
    conn: &mut rusqlite::Connection,
    account_id: &str,
    folder: &FolderSyncTarget,
    envelopes: &[ImapEnvelope],
    server_uid_validity: u32,
) -> Result<(usize, Vec<NewMailArrival>)> {
    sync_envelopes_with_reconciliation(conn, account_id, folder, envelopes, server_uid_validity, None)
}

#[cfg(test)]
fn sync_envelopes_with_reconciliation(
    conn: &mut rusqlite::Connection,
    account_id: &str,
    folder: &FolderSyncTarget,
    envelopes: &[ImapEnvelope],
    server_uid_validity: u32,
    reconciliation: Option<&[ImapEnvelope]>,
) -> Result<(usize, Vec<NewMailArrival>)> {
    sync_envelopes_with_snapshots(conn, account_id, folder, envelopes, server_uid_validity, reconciliation, None)
}

fn sync_envelopes_with_snapshots(
    conn: &mut rusqlite::Connection,
    account_id: &str,
    folder: &FolderSyncTarget,
    envelopes: &[ImapEnvelope],
    server_uid_validity: u32,
    reconciliation: Option<&[ImapEnvelope]>,
    flag_snapshot: Option<&reconciliation::Snapshot>,
) -> Result<(usize, Vec<NewMailArrival>)> {
    let rebuilding_baseline = folder.uid_validity == 0
        || (server_uid_validity != 0 && server_uid_validity != folder.uid_validity);
    let transaction = conn.transaction_with_behavior(TransactionBehavior::Immediate)?;
    // A fetch owns the epoch it started from, not whichever epoch is current
    // when its blocking network operation eventually finishes.
    let (current_epoch, current_cursor): (u32, u32) = transaction.query_row(
        "SELECT uid_validity, last_synced_uid FROM folders WHERE id = ?1 AND account_id = ?2",
        params![folder.id, account_id],
        |row| Ok((row.get(0)?, row.get(1)?)),
    )?;
    if current_epoch != folder.uid_validity {
        return Err(MailFfiError::Internal("stale mailbox epoch".into()));
    }
    let mut current_folder = folder.clone();
    current_folder.last_synced_uid = current_cursor;
    let flags_current = flag_snapshot.map(|snapshot| reconciliation::is_current(&transaction, folder, snapshot)).transpose()?.unwrap_or(true);
    let reconciliation = reconciliation.filter(|_| current_cursor <= folder.last_synced_uid && flags_current);
    // Both pages describe the same pre-rule server state. Merge their identities
    // before applying rules so no later pass can undo a rule or resurrect a row.
    let mut merged = envelopes.to_vec();
    let mut seen: std::collections::HashSet<_> = envelopes.iter().map(|env| env.uid).collect();
    if let Some(snapshot) = reconciliation {
        merged.extend(snapshot.iter().filter(|env| seen.insert(env.uid)).cloned());
    }
    if !flags_current {
        let existing = existing_uids(&transaction, &folder.id, &merged)?;
        merged.retain(|env| !existing.contains(&env.uid));
    }
    let (inserted, arrivals) = insert_envelopes_with_arrivals(
        &transaction,
        account_id,
        &current_folder,
        &merged,
        server_uid_validity,
    )?;
    if let Some(snapshot) = reconciliation {
        // A short newest-page snapshot covers the entire mailbox. A full
        // page only proves membership at or above its minimum UID.
        let lower = if snapshot.len() < 500 { 1 } else {
            snapshot.iter().map(|env| env.uid).min().unwrap_or(1)
        };
        let present: std::collections::HashSet<_> = snapshot.iter().map(|env| env.uid).collect();
        let candidates = {
            let mut statement = transaction.prepare(
                "SELECT uid FROM emails WHERE account_id=?1 AND folder_id=?2 AND uid>=?3 ORDER BY uid LIMIT 500",
            )?;
            let rows = statement.query_map(params![account_id, folder.id, lower], |row| row.get::<_, u32>(0))?;
            rows.collect::<std::result::Result<Vec<_>, _>>()?
        };
        for uid in candidates {
            if !present.contains(&uid) && !maho_core::services::offline_queue::has_pending_mail_mutation(&transaction, account_id, &folder.path, uid)? {
                transaction.execute(
                    "DELETE FROM emails WHERE account_id=?1 AND folder_id=?2 AND uid=?3",
                    params![account_id, folder.id, uid],
                )?;
            }
        }
        transaction.execute(
            "UPDATE folders SET total_count=(SELECT count(*) FROM emails WHERE folder_id=?1),
             unread_count=(SELECT count(*) FROM emails WHERE folder_id=?1 AND is_read=0) WHERE id=?1",
            [&folder.id],
        )?;
    }
    if let Some(snapshot) = flag_snapshot {
        reconciliation::apply(&transaction, folder, snapshot)?;
    }
    transaction.commit()?;
    Ok((
        inserted,
        if rebuilding_baseline {
            Vec::new()
        } else {
            arrivals.into_iter().filter(|arrival| arrival.cursor > u64::from(current_cursor)).collect()
        },
    ))
}

#[cfg(test)]
fn insert_envelopes(
    conn: &rusqlite::Connection,
    account_id: &str,
    folder: &FolderSyncTarget,
    envelopes: &[ImapEnvelope],
    server_uid_validity: u32,
) -> Result<usize> {
    insert_envelopes_with_arrivals(conn, account_id, folder, envelopes, server_uid_validity)
        .map(|(inserted, _)| inserted)
}

fn insert_envelopes_with_arrivals(
    conn: &rusqlite::Connection,
    account_id: &str,
    folder: &FolderSyncTarget,
    envelopes: &[ImapEnvelope],
    server_uid_validity: u32,
) -> Result<(usize, Vec<NewMailArrival>)> {
    let reset_required = folder.uid_validity != 0
        && server_uid_validity != 0
        && server_uid_validity != folder.uid_validity;

    if reset_required {
        conn.execute("DELETE FROM emails WHERE folder_id = ?1 AND uid > 0", [&folder.id])?;
    }

    let existing_uids = existing_uids(conn, &folder.id, envelopes)?;
    let effective_uid_validity = if server_uid_validity == 0 {
        folder.uid_validity
    } else {
        server_uid_validity
    };
    // Rule-generated outbox rows must capture the epoch of this fetched page.
    conn.execute("UPDATE folders SET uid_validity=?1 WHERE id=?2", params![effective_uid_validity, folder.id])?;
    let now = chrono::Utc::now().format("%Y-%m-%d %H:%M:%S").to_string();
    let mut inserted = 0usize;
    let mut arrivals = Vec::new();
    let mut max_uid = if reset_required {
        0
    } else {
        folder.last_synced_uid
    };
    for env in envelopes {
        if env.uid > max_uid {
            max_uid = env.uid;
        }
        if maho_core::services::offline_queue::has_pending_mail_mutation(conn, account_id, &folder.path, env.uid)? {
            continue;
        }
        if existing_uids.contains(&env.uid) {
            conn.execute(
                "UPDATE emails SET is_read = ?1, is_starred = ?2
                 WHERE folder_id = ?3 AND uid = ?4",
                params![
                    env.flags.iter().any(|flag| flag.contains("Seen")),
                    env.flags.iter().any(|flag| flag.contains("Flagged")),
                    folder.id,
                    env.uid as i64,
                ],
            )?;
            continue;
        }
        let is_new_incoming = env.uid > folder.last_synced_uid;
        let email_id = Uuid::new_v4().to_string();
        let inserted_rows = conn.execute(
            "INSERT OR IGNORE INTO emails
             (id, account_id, folder_id, uid, message_id, in_reply_to, subject,
              from_address, from_name, to_addresses, cc_addresses, date, snippet,
              is_read, is_starred, is_draft, has_attachments, raw_size, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, 0, ?16, ?17, ?18)",
            params![
                email_id,
                account_id,
                folder.id,
                env.uid as i64,
                if env.message_id.is_empty() {
                    Option::<String>::None
                } else {
                    Some(env.message_id.clone())
                },
                env.in_reply_to,
                env.subject,
                env.from_address,
                env.from_name,
                serde_json::to_string(&env.to_addresses).unwrap_or_else(|_| "[]".to_string()),
                serde_json::to_string(&env.cc_addresses).unwrap_or_else(|_| "[]".to_string()),
                normalize_envelope_date(&env.date),
                env.subject.chars().take(100).collect::<String>(),
                env.flags.iter().any(|f| f.contains("Seen")),
                env.flags.iter().any(|f| f.contains("Flagged")),
                env.has_attachments,
                env.size as i64,
                now,
            ],
        )?;
        if inserted_rows == 0 {
            continue;
        }
        inserted += inserted_rows;
        for att in &env.attachments {
            conn.execute(
                "INSERT OR IGNORE INTO attachments (id, email_id, part_id, filename, mime_type, size, content_id)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
                params![
                    Uuid::new_v4().to_string(),
                    email_id,
                    att.part_id,
                    att.filename,
                    att.mime_type,
                    att.size,
                    att.content_id.clone().or_else(|| Some(att.part_id.clone()))
                ],
            )?;
        }

        if is_new_incoming {
            crate::ffi::organize_api::evaluate_rules_for_email_in_tx(conn, account_id, &email_id)?;
            let email_status: Option<(bool, String)> = conn
                .query_row(
                    "SELECT is_read, folder_id FROM emails WHERE id = ?1",
                    params![&email_id],
                    |row| Ok((row.get(0)?, row.get(1)?)),
                )
                .optional()?;

            if let Some((is_read, current_folder_id)) = email_status {
                if current_folder_id == folder.id && !is_read {
                    arrivals.push(NewMailArrival {
                        email_id: email_id.clone(),
                        message_id: if env.message_id.is_empty() {
                            email_id.clone()
                        } else {
                            env.message_id.clone()
                        },
                        sender: env
                            .from_name
                            .clone()
                            .filter(|name| !name.is_empty())
                            .unwrap_or_else(|| env.from_address.clone()),
                        subject: env.subject.clone(),
                        cursor: u64::from(env.uid),
                        epoch: u64::from(effective_uid_validity),
                    });
                }
            }
        }
    }
    conn.execute(
        "UPDATE folders SET total_count = (SELECT COUNT(*) FROM emails WHERE folder_id = ?1),
             unread_count = (SELECT COUNT(*) FROM emails WHERE folder_id = ?1 AND is_read = 0),
             last_synced_uid = ?2, uid_validity = ?3 WHERE id = ?1",
        params![folder.id, max_uid as i64, effective_uid_validity as i64],
    )?;
    Ok((inserted, arrivals))
}

fn normalize_envelope_date(date: &str) -> String {
    chrono::DateTime::parse_from_rfc2822(date)
        .or_else(|_| chrono::DateTime::parse_from_rfc3339(date))
        .map(|date| date.with_timezone(&chrono::Utc).to_rfc3339_opts(chrono::SecondsFormat::Secs, true))
        // Missing or malformed provider dates have no trustworthy instant.
        .unwrap_or_else(|_| date.to_string())
}

fn existing_uids(
    conn: &rusqlite::Connection,
    folder_id: &str,
    envelopes: &[ImapEnvelope],
) -> Result<std::collections::HashSet<u32>> {
    let uids: Vec<u32> = envelopes.iter().map(|env| env.uid).collect();
    if uids.is_empty() {
        return Ok(std::collections::HashSet::new());
    }
    let mut result = std::collections::HashSet::new();
    for chunk in uids.chunks(500) {
        let placeholders = std::iter::repeat_n("?", chunk.len())
            .collect::<Vec<_>>()
            .join(",");
        let query = format!(
            "SELECT uid FROM emails
             WHERE folder_id = ? AND uid IN ({placeholders})"
        );
        let mut params_vec: Vec<Box<dyn rusqlite::types::ToSql>> =
            vec![Box::new(folder_id.to_string())];
        for uid in chunk {
            params_vec.push(Box::new(*uid as i64));
        }
        let refs: Vec<&dyn rusqlite::types::ToSql> = params_vec.iter().map(|p| p.as_ref()).collect();
        let mut stmt = conn.prepare(&query)?;
        let rows = stmt.query_map(refs.as_slice(), |row| row.get::<_, i64>(0))?;
        for row in rows.flatten() {
            if let Ok(uid) = u32::try_from(row) {
                result.insert(uid);
            }
        }
    }
    Ok(result)
}

async fn idle_wait_once(ctx: Arc<AppCtx>, account_id: &str) -> Result<bool> {
    let account_id = account_id.to_string();
    tokio::task::spawn_blocking(move || {
        // The database is one connection behind a mutex, and an IDLE wait blocks
        // for minutes: read what is needed, then release it before waiting.
        let (mut resolved, folders) = ctx.with_db(|conn| {
            let resolved = resolve_account_auth(conn, &ctx.credential_key, &account_id)?;
            let folders = sync_targets(conn, &account_id)?;
            Ok((resolved, folders))
        })?;
        for folder in folders {
            let mut client = ctx.with_db(|conn| {
                connect_imap_with_retry(conn, &ctx.credential_key, &mut resolved)
            })?;
            let changed = client.idle_wait(&folder.path, IDLE_TIMEOUT)?;
            safe_logout(client);
            if changed {
                return Ok(true);
            }
        }
        Ok(false)
    })
    .await
    .map_err(|e| MailFfiError::Internal(format!("idle join error: {e}")))?
}

#[cfg(test)]
#[path = "sync_review_tests.rs"]
mod review_tests;

#[cfg(test)]
#[path = "sync_wire_review_tests.rs"]
mod wire_review_tests;

#[cfg(test)]
#[path = "sync_rule_tests.rs"]
mod rule_tests;

#[cfg(test)]
#[path = "sync_backfill_wake_tests.rs"]
mod backfill_wake_tests;

#[cfg(test)]
#[allow(clippy::disallowed_methods)]
mod tests {
    use super::*;

    #[test]
    fn existing_uids_empty_input() {
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        let ids = existing_uids(&conn, "inbox", &[]).unwrap();
        assert!(ids.is_empty());
    }

    #[test]
    fn existing_uids_handles_large_uid_sets_without_exceeding_variable_limit() {
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        let envelopes: Vec<ImapEnvelope> = (1..=40_000)
            .map(|uid| ImapEnvelope {
                uid,
                message_id: format!("<{uid}@example.com>"),
                in_reply_to: None,
                subject: "test".to_string(),
                from_address: "a@example.com".to_string(),
                from_name: None,
                to_addresses: vec![],
                cc_addresses: vec![],
                date: "2026-08-01T00:00:00Z".to_string(),
                flags: vec![],
                size: 100,
                has_attachments: false,
                attachments: vec![],
            })
            .collect();
        let ids = existing_uids(&conn, "inbox", &envelopes).unwrap();
        assert!(ids.is_empty());
    }

    #[test]
    fn existing_uids_are_scoped_to_folder() {
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, auth_type, imap_host, smtp_host, username)
             VALUES ('acc1', 'a@example.com', 'A', 'password', 'imap.example.com', 'smtp.example.com', 'a@example.com')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type)
             VALUES ('all-mail', 'acc1', 'All Mail', '[Gmail]/All Mail', 'archive'),
                    ('inbox', 'acc1', 'Inbox', 'INBOX', 'inbox')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, date)
             VALUES ('email1', 'acc1', 'all-mail', 10, '<same@example.com>', 'Subject', '2026-07-31T00:00:00Z')",
            [],
        )
        .unwrap();
        let envelopes = vec![ImapEnvelope {
            uid: 10,
            message_id: "<same@example.com>".to_string(),
            in_reply_to: None,
            subject: "Subject".to_string(),
            from_address: "sender@example.com".to_string(),
            from_name: None,
            to_addresses: vec![],
            cc_addresses: vec![],
            date: "2026-07-31T00:00:00Z".to_string(),
            flags: vec![],
            size: 1,
            has_attachments: false,
            attachments: vec![],
        }];

        let ids = existing_uids(&conn, "inbox", &envelopes).unwrap();
        assert!(ids.is_empty());
    }

    #[test]
    fn zero_server_uid_validity_does_not_delete_cached_messages() {
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, auth_type, imap_host, smtp_host, username)
             VALUES ('acc1', 'a@example.com', 'A', 'password', 'imap.example.com', 'smtp.example.com', 'a@example.com')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type, last_synced_uid, uid_validity)
             VALUES ('inbox', 'acc1', 'Inbox', 'INBOX', 'inbox', 10, 1234)",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, date)
             VALUES ('email1', 'acc1', 'inbox', 10, '<cached@example.com>', 'Cached', '2026-07-31T00:00:00Z')",
            [],
        )
        .unwrap();
        let folder = FolderSyncTarget {
            id: "inbox".to_string(),
            path: "INBOX".to_string(),
            folder_type: "inbox".to_string(),
            last_synced_uid: 10,
            uid_validity: 1234,
        };

        insert_envelopes(&conn, "acc1", &folder, &[], 0).unwrap();

        let email_count: i64 = conn
            .query_row(
                "SELECT COUNT(*) FROM emails WHERE folder_id = 'inbox'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        let uid_validity: i64 = conn
            .query_row(
                "SELECT uid_validity FROM folders WHERE id = 'inbox'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(email_count, 1);
        assert_eq!(uid_validity, 1234);
    }

    #[test]
    fn uidvalidity_reset_replaces_messages_and_resets_cursor_atomically() {
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, auth_type, imap_host, smtp_host, username)
             VALUES ('acc1', 'a@example.com', 'A', 'password', 'imap.example.com', 'smtp.example.com', 'a@example.com')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type, last_synced_uid, uid_validity)
             VALUES ('inbox', 'acc1', 'Inbox', 'INBOX', 'inbox', 99, 1234)",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, date)
             VALUES ('stale', 'acc1', 'inbox', 99, '<stale@example.com>', 'Stale', '2026-07-31T00:00:00Z')",
            [],
        )
        .unwrap();
        let folder = FolderSyncTarget {
            id: "inbox".to_string(),
            path: "INBOX".to_string(),
            folder_type: "inbox".to_string(),
            last_synced_uid: 99,
            uid_validity: 1234,
        };
        let envelopes = vec![ImapEnvelope {
            uid: 1,
            message_id: "".to_string(),
            in_reply_to: None,
            subject: "Fresh after reset".to_string(),
            from_address: "sender@example.com".to_string(),
            from_name: None,
            to_addresses: vec![],
            cc_addresses: vec![],
            date: "2026-08-01T00:00:00Z".to_string(),
            flags: vec![],
            size: 1,
            has_attachments: false,
            attachments: vec![],
        }];

        let inserted = insert_envelopes(&conn, "acc1", &folder, &envelopes, 5678).unwrap();
        assert_eq!(inserted, 1);

        let rows: Vec<(i64, Option<String>)> = conn
            .prepare("SELECT uid, message_id FROM emails WHERE folder_id = 'inbox' ORDER BY uid")
            .unwrap()
            .query_map([], |row| Ok((row.get(0)?, row.get(1)?)))
            .unwrap()
            .collect::<std::result::Result<_, _>>()
            .unwrap();
        assert_eq!(rows, vec![(1, None)]);

        let (last_synced_uid, uid_validity): (i64, i64) = conn
            .query_row(
                "SELECT last_synced_uid, uid_validity FROM folders WHERE id = 'inbox'",
                [],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )
            .unwrap();
        assert_eq!(last_synced_uid, 1);
        assert_eq!(uid_validity, 5678);
    }

    #[test]
    fn duplicate_message_ids_and_empty_message_ids_are_preserved_when_uids_differ() {
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, auth_type, imap_host, smtp_host, username)
             VALUES ('acc1', 'a@example.com', 'A', 'password', 'imap.example.com', 'smtp.example.com', 'a@example.com')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type, last_synced_uid, uid_validity)
             VALUES ('inbox', 'acc1', 'Inbox', 'INBOX', 'inbox', 0, 1234)",
            [],
        )
        .unwrap();
        let folder = FolderSyncTarget {
            id: "inbox".to_string(),
            path: "INBOX".to_string(),
            folder_type: "inbox".to_string(),
            last_synced_uid: 0,
            uid_validity: 1234,
        };
        let envelopes = vec![
            ImapEnvelope {
                uid: 10,
                message_id: "<dup@example.com>".to_string(),
                in_reply_to: None,
                subject: "First dup".to_string(),
                from_address: "sender@example.com".to_string(),
                from_name: None,
                to_addresses: vec![],
                cc_addresses: vec![],
                date: "2026-08-01T00:00:00Z".to_string(),
                flags: vec![],
                size: 1,
                has_attachments: false,
                attachments: vec![],
            },
            ImapEnvelope {
                uid: 11,
                message_id: "<dup@example.com>".to_string(),
                in_reply_to: None,
                subject: "Second dup".to_string(),
                from_address: "sender@example.com".to_string(),
                from_name: None,
                to_addresses: vec![],
                cc_addresses: vec![],
                date: "2026-08-01T00:00:01Z".to_string(),
                flags: vec![],
                size: 1,
                has_attachments: false,
                attachments: vec![],
            },
            ImapEnvelope {
                uid: 12,
                message_id: "".to_string(),
                in_reply_to: None,
                subject: "Empty id".to_string(),
                from_address: "sender@example.com".to_string(),
                from_name: None,
                to_addresses: vec![],
                cc_addresses: vec![],
                date: "2026-08-01T00:00:02Z".to_string(),
                flags: vec![],
                size: 1,
                has_attachments: false,
                attachments: vec![],
            },
        ];

        let inserted = insert_envelopes(&conn, "acc1", &folder, &envelopes, 1234).unwrap();
        assert_eq!(inserted, 3);

        let rows: Vec<(i64, Option<String>)> = conn
            .prepare("SELECT uid, message_id FROM emails WHERE folder_id = 'inbox' ORDER BY uid")
            .unwrap()
            .query_map([], |row| Ok((row.get(0)?, row.get(1)?)))
            .unwrap()
            .collect::<std::result::Result<_, _>>()
            .unwrap();
        assert_eq!(
            rows,
            vec![
                (10, Some("<dup@example.com>".to_string())),
                (11, Some("<dup@example.com>".to_string())),
                (12, None),
            ]
        );
    }

    #[test]
    fn attachments_store_global_id_and_part_id() {
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, auth_type, imap_host, smtp_host, username)
             VALUES ('acc1', 'a@example.com', 'A', 'password', 'imap.example.com', 'smtp.example.com', 'a@example.com')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type, last_synced_uid, uid_validity)
             VALUES ('inbox', 'acc1', 'Inbox', 'INBOX', 'inbox', 0, 1234)",
            [],
        )
        .unwrap();
        let folder = FolderSyncTarget {
            id: "inbox".to_string(),
            path: "INBOX".to_string(),
            folder_type: "inbox".to_string(),
            last_synced_uid: 0,
            uid_validity: 1234,
        };
        let envelopes = vec![ImapEnvelope {
            uid: 10,
            message_id: "<with-att@example.com>".to_string(),
            in_reply_to: None,
            subject: "Attachment".to_string(),
            from_address: "sender@example.com".to_string(),
            from_name: None,
            to_addresses: vec![],
            cc_addresses: vec![],
            date: "2026-08-01T00:00:00Z".to_string(),
            flags: vec![],
            size: 1,
            has_attachments: true,
            attachments: vec![maho_core::imap_client::ImapAttachmentMeta {
                part_id: "2.1".to_string(),
                filename: Some("file.pdf".to_string()),
                mime_type: "application/pdf".to_string(),
                size: 123,
                content_id: None,
            }],
        }];

        insert_envelopes(&conn, "acc1", &folder, &envelopes, 1234).unwrap();

        let (id, part_id, content_id): (String, String, Option<String>) = conn
            .query_row(
                "SELECT id, part_id, content_id FROM attachments LIMIT 1",
                [],
                |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
            )
            .unwrap();
        assert_ne!(id, "2.1");
        assert_eq!(part_id, "2.1");
        assert_eq!(content_id.as_deref(), Some("2.1"));
    }

    #[test]
    fn new_inbox_insertions_emit_arrival_events() {
        assert!(should_emit_new_mail("inbox", 1));
        assert!(!should_emit_new_mail("inbox", 0));
        assert!(!should_emit_new_mail("sent", 1));
    }

    #[test]
    fn arrival_ordering_identity_includes_uid_validity_epoch() {
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, auth_type, imap_host, smtp_host, username)
             VALUES ('acc1', 'a@example.com', 'A', 'password', 'imap.example.com', 'smtp.example.com', 'a@example.com')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type, last_synced_uid, uid_validity)
             VALUES ('inbox', 'acc1', 'Inbox', 'INBOX', 'inbox', 0, 1234)",
            [],
        )
        .unwrap();
        let folder = FolderSyncTarget {
            id: "inbox".to_string(),
            path: "INBOX".to_string(),
            folder_type: "inbox".to_string(),
            last_synced_uid: 0,
            uid_validity: 1234,
        };
        let envelope = ImapEnvelope {
            uid: 7,
            message_id: "<epoch@example.com>".to_string(),
            in_reply_to: None,
            subject: "Epoch".to_string(),
            from_address: "sender@example.com".to_string(),
            from_name: None,
            to_addresses: vec![],
            cc_addresses: vec![],
            date: "2026-08-01T00:00:00Z".to_string(),
            flags: vec![],
            size: 1,
            has_attachments: false,
            attachments: vec![],
        };

        let (_, arrivals) =
            insert_envelopes_with_arrivals(&conn, "acc1", &folder, &[envelope], 5678).unwrap();
        assert_eq!(arrivals.len(), 1);
        assert_eq!(arrivals[0].cursor, 7);
        assert_eq!(arrivals[0].epoch, 5678);
    }

    #[test]
    fn uid_validity_rebuild_caches_history_then_publishes_next_arrival_once() {
        // Given a previously synchronized inbox from an older UIDVALIDITY epoch.
        let mut conn = rusqlite::Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, auth_type, imap_host, smtp_host, username)
             VALUES ('acc1', 'a@example.com', 'A', 'password', 'imap.example.com', 'smtp.example.com', 'a@example.com')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type, last_synced_uid, uid_validity)
             VALUES ('inbox', 'acc1', 'Inbox', 'INBOX', 'inbox', 99, 1234)",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, date)
             VALUES ('stale', 'acc1', 'inbox', 99, '<stale@example.com>', 'Stale', '2026-07-31T00:00:00Z')",
            [],
        )
        .unwrap();
        let old_epoch = FolderSyncTarget {
            id: "inbox".to_string(),
            path: "INBOX".to_string(),
            folder_type: "inbox".to_string(),
            last_synced_uid: 99,
            uid_validity: 1234,
        };

        // When historical messages rebuild the cache under a new epoch.
        let historical = [
            test_envelope(1, "Historical one"),
            test_envelope(2, "Historical two"),
        ];
        let (baseline_inserted, baseline_arrivals) =
            sync_envelopes_into_cache(&mut conn, "acc1", &old_epoch, &historical, 5678).unwrap();

        // Then they are cached without producing arrival publications.
        assert_eq!(baseline_inserted, 2);
        assert!(baseline_arrivals.is_empty());
        let cached_uids: Vec<i64> = conn
            .prepare("SELECT uid FROM emails WHERE folder_id = 'inbox' ORDER BY uid")
            .unwrap()
            .query_map([], |row| row.get(0))
            .unwrap()
            .collect::<std::result::Result<_, _>>()
            .unwrap();
        assert_eq!(cached_uids, vec![1, 2]);

        // When the next genuine message arrives in that same new epoch.
        let new_epoch = sync_targets(&conn, "acc1").unwrap().remove(0);
        assert_eq!(new_epoch.last_synced_uid, 2);
        assert_eq!(new_epoch.uid_validity, 5678);
        let (arrival_inserted, arrivals) = sync_envelopes_into_cache(
            &mut conn,
            "acc1",
            &new_epoch,
            &[test_envelope(3, "Genuine arrival")],
            5678,
        )
        .unwrap();

        // Then exactly one publication carries the new epoch and cursor.
        assert_eq!(arrival_inserted, 1);
        assert_eq!(arrivals.len(), 1);
        assert_eq!(arrivals[0].subject, "Genuine arrival");
        assert_eq!(arrivals[0].cursor, 3);
        assert_eq!(arrivals[0].epoch, 5678);
        println!(
            "baseline_inserted={baseline_inserted} baseline_arrivals={} next_inserted={arrival_inserted} next_arrivals={} cursor={} epoch={}",
            baseline_arrivals.len(),
            arrivals.len(),
            arrivals[0].cursor,
            arrivals[0].epoch,
        );
    }

    #[test]
    fn initial_account_bootstrap_establishes_a_quiet_baseline_by_contract() {
        // Given a newly discovered inbox with no persisted UIDVALIDITY epoch.
        let mut conn = rusqlite::Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, auth_type, imap_host, smtp_host, username)
             VALUES ('acc1', 'a@example.com', 'A', 'password', 'imap.example.com', 'smtp.example.com', 'a@example.com')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type, last_synced_uid, uid_validity)
             VALUES ('inbox', 'acc1', 'Inbox', 'INBOX', 'inbox', 0, 0)",
            [],
        )
        .unwrap();
        let bootstrap = FolderSyncTarget {
            id: "inbox".to_string(),
            path: "INBOX".to_string(),
            folder_type: "inbox".to_string(),
            last_synced_uid: 0,
            uid_validity: 0,
        };

        // When existing mailbox history is cached for the first time.
        let (inserted, arrivals) = sync_envelopes_into_cache(
            &mut conn,
            "acc1",
            &bootstrap,
            &[test_envelope(10, "Existing mail")],
            2468,
        )
        .unwrap();

        // Then bootstrap is intentionally quiet while persisting its epoch and cursor.
        assert_eq!(inserted, 1);
        assert!(arrivals.is_empty());
        let state: (i64, i64) = conn
            .query_row(
                "SELECT last_synced_uid, uid_validity FROM folders WHERE id = 'inbox'",
                [],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )
            .unwrap();
        assert_eq!(state, (10, 2468));
    }

    fn test_envelope(uid: u32, subject: &str) -> ImapEnvelope {
        ImapEnvelope {
            uid,
            message_id: format!("<message-{uid}@example.com>"),
            in_reply_to: None,
            subject: subject.to_string(),
            from_address: "sender@example.com".to_string(),
            from_name: Some("Sender".to_string()),
            to_addresses: vec![],
            cc_addresses: vec![],
            date: "2026-08-01T00:00:00Z".to_string(),
            flags: vec![],
            size: 1,
            has_attachments: false,
            attachments: vec![],
        }
    }

    #[test]
    fn message_and_flag_batch_rolls_back_when_folder_cursor_update_fails() {
        let mut conn = rusqlite::Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, auth_type, imap_host, smtp_host, username)
             VALUES ('acc1', 'a@example.com', 'A', 'password', 'imap.example.com', 'smtp.example.com', 'a@example.com')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type, last_synced_uid, uid_validity)
             VALUES ('inbox', 'acc1', 'Inbox', 'INBOX', 'inbox', 10, 1234)",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, subject, date, is_read, is_starred)
             VALUES ('existing', 'acc1', 'inbox', 10, 'Existing', '2026-08-01T00:00:00Z', 0, 0)",
            [],
        )
        .unwrap();
        conn.execute_batch(
            "CREATE TRIGGER reject_folder_cursor_update
             BEFORE UPDATE ON folders
             BEGIN
                 SELECT RAISE(ABORT, 'cursor rejected');
             END;",
        )
        .unwrap();
        let folder = FolderSyncTarget {
            id: "inbox".to_string(),
            path: "INBOX".to_string(),
            folder_type: "inbox".to_string(),
            last_synced_uid: 10,
            uid_validity: 1234,
        };
        let mut existing = test_envelope(10, "Existing");
        existing.flags = vec!["\\Seen".to_string(), "\\Flagged".to_string()];

        assert!(sync_envelopes_into_cache(
            &mut conn,
            "acc1",
            &folder,
            &[existing, test_envelope(11, "New")],
            1234,
        )
        .is_err());

        let rows: Vec<(i64, bool, bool)> = conn
            .prepare(
                "SELECT uid, is_read, is_starred FROM emails WHERE folder_id = 'inbox' ORDER BY uid",
            )
            .unwrap()
            .query_map([], |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)))
            .unwrap()
            .collect::<std::result::Result<_, _>>()
            .unwrap();
        assert_eq!(rows, vec![(10, false, false)]);
        let cursor: i64 = conn
            .query_row(
                "SELECT last_synced_uid FROM folders WHERE id = 'inbox'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(cursor, 10);
    }

    #[test]
    fn duplicate_uids_in_one_sync_count_once() {
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, auth_type, imap_host, smtp_host, username)
             VALUES ('acc1', 'a@example.com', 'A', 'password', 'imap.example.com', 'smtp.example.com', 'a@example.com')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type, last_synced_uid, uid_validity)
             VALUES ('inbox', 'acc1', 'Inbox', 'INBOX', 'inbox', 0, 1234)",
            [],
        )
        .unwrap();
        let folder = FolderSyncTarget {
            id: "inbox".to_string(),
            path: "INBOX".to_string(),
            folder_type: "inbox".to_string(),
            last_synced_uid: 0,
            uid_validity: 1234,
        };
        let envelope = |subject: &str| ImapEnvelope {
            uid: 10,
            message_id: "<same@example.com>".to_string(),
            in_reply_to: None,
            subject: subject.to_string(),
            from_address: "sender@example.com".to_string(),
            from_name: None,
            to_addresses: vec![],
            cc_addresses: vec![],
            date: "2026-08-01T00:00:00Z".to_string(),
            flags: vec![],
            size: 1,
            has_attachments: false,
            attachments: vec![],
        };

        assert_eq!(
            insert_envelopes(
                &conn,
                "acc1",
                &folder,
                &[envelope("first"), envelope("second")],
                1234,
            )
            .unwrap(),
            1
        );
    }
}
