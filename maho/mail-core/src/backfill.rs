// Copyright 2026 Maho Browser. All rights reserved.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use futures::FutureExt as _;
use tokio_util::sync::CancellationToken;

use crate::account::{connect_imap, resolve_account_auth, safe_logout};
use crate::error::{MailFfiError, Result};
use crate::state::{AppCtx, SyncRegistry, WorkerHandle};

const BACKFILL_BATCH_SIZE: i64 = 10;
const BACKFILL_DELAY: Duration = Duration::from_millis(100);
const SUPERVISOR_BACKOFF_MAX: Duration = Duration::from_secs(60);

pub fn start_backfill_worker(ctx: Arc<AppCtx>, account_id: String) -> WorkerHandle {
    let stop = Arc::new(AtomicBool::new(false));
    let wake = Arc::new(tokio::sync::Notify::new());
    let token = CancellationToken::new();
    let task = tokio::spawn(supervise_backfill(
        ctx,
        account_id,
        Arc::clone(&stop),
        token,
        Arc::clone(&wake),
    ));
    WorkerHandle {
        stop,
        tasks: vec![task],
        wake: Some(wake),
    }
}

async fn supervise_backfill(
    ctx: Arc<AppCtx>,
    account_id: String,
    stop: Arc<AtomicBool>,
    token: CancellationToken,
    wake: Arc<tokio::sync::Notify>,
) {
    let mut backoff = Duration::from_secs(1);
    while !stop.load(Ordering::SeqCst) {
        let attempt = std::panic::AssertUnwindSafe(backfill_loop(
            Arc::clone(&ctx),
            account_id.clone(),
            Arc::clone(&stop),
            token.child_token(),
            Arc::clone(&wake),
        ))
        .catch_unwind()
        .await;
        if stop.load(Ordering::SeqCst) {
            break;
        }
        match attempt {
            Ok(()) => break,
            Err(_) => {
                log::error!("[mail-ffi] backfill worker panicked for {account_id}; restarting")
            }
        }
        tokio::time::sleep(backoff).await;
        backoff = (backoff * 2).min(SUPERVISOR_BACKOFF_MAX);
    }
    token.cancel();
}

async fn backfill_loop(
    ctx: Arc<AppCtx>,
    account_id: String,
    stop: Arc<AtomicBool>,
    token: CancellationToken,
    wake: Arc<tokio::sync::Notify>,
) {
    run_backfill_loop(&account_id, &stop, &token, &wake, || {
        backfill_batch(Arc::clone(&ctx), &account_id, Arc::clone(&stop))
    }).await;
}

async fn run_backfill_loop<F, Fut>(
    account_id: &str,
    stop: &Arc<AtomicBool>,
    token: &CancellationToken,
    wake: &tokio::sync::Notify,
    mut batch: F,
) where
    F: FnMut() -> Fut,
    Fut: std::future::Future<Output = Result<usize>>,
{
    let mut consecutive_errors = 0u32;
    while !stop.load(Ordering::SeqCst) && consecutive_errors < 5 {
        match batch().await {
            Ok(0) => {
                consecutive_errors = 0;
                tokio::select! {
                    _ = token.cancelled() => return,
                    () = wake.notified() => {}
                }
                continue;
            }
            Ok(count) => {
                consecutive_errors = 0;
                log::info!("[mail-ffi] backfilled {count} bodies for {account_id}");
            }
            Err(err) => {
                consecutive_errors += 1;
                log::warn!("[mail-ffi] backfill batch failed for {account_id}: {err}");
            }
        }
        tokio::select! {
            _ = token.cancelled() => return,
            () = tokio::time::sleep(BACKFILL_DELAY) => {}
        }
    }
}

async fn backfill_batch(
    ctx: Arc<AppCtx>,
    account_id: &str,
    stop: Arc<AtomicBool>,
) -> Result<usize> {
    let account_id = account_id.to_string();
    tokio::task::spawn_blocking(move || {
        let worker = BackfillWorker {
            registry: crate::state::registry(),
            account_id: &account_id,
            stop: &stop,
        };
        backfill_batch_with_connector(&ctx, &worker, connect_imap)
    })
    .await
    .map_err(|e| MailFfiError::Internal(format!("backfill join error: {e}")))?
}

struct BackfillWorker<'a> {
    registry: &'a SyncRegistry,
    account_id: &'a str,
    stop: &'a Arc<AtomicBool>,
}

fn backfill_batch_with_connector(
    ctx: &AppCtx,
    worker: &BackfillWorker<'_>,
    connect: impl FnOnce(&crate::account::AccountAuth) -> Result<maho_core::imap_client::ImapClient>,
) -> Result<usize> {
    if worker.stop.load(Ordering::SeqCst) {
        return Ok(0);
    }
    // The database is one connection behind a mutex, so the guard is scoped to
    // each database step and released for the length of the IMAP fetches.
    let emails = ctx.with_db(|conn| {
        Ok(maho_core::services::email::get_emails_needing_body_fetch(
            conn,
            worker.account_id,
            BACKFILL_BATCH_SIZE,
        )?)
    })?;
    if emails.is_empty() {
        return Ok(0);
    }
    let (identities, resolved) = ctx.with_db(|conn| {
        let identities = emails.into_iter().map(|(email_id, folder_path, uid)| -> Result<_> {
            let uid = u32::try_from(uid).ok().filter(|uid| *uid > 0)
                .ok_or(MailFfiError::InvalidArg("email has no valid IMAP UID"))?;
            let (folder_id, epoch): (String, Option<u32>) = conn.query_row(
                "SELECT e.folder_id,f.uid_validity FROM emails e JOIN folders f
                 ON e.folder_id=f.id AND e.account_id=f.account_id
                 WHERE e.id=?1 AND e.account_id=?2 AND f.path=?3 AND e.uid=?4",
                rusqlite::params![email_id, worker.account_id, folder_path, uid],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )?;
            let epoch = epoch.filter(|epoch| *epoch > 0)
                .ok_or(MailFfiError::InvalidArg("mailbox epoch is unavailable"))?;
            Ok((email_id, folder_id, folder_path, uid, epoch))
        }).collect::<Result<Vec<_>>>()?;
        let resolved = resolve_account_auth(conn, &ctx.credential_key, worker.account_id)?;
        Ok((identities, resolved))
    })?;
    let mut client = connect(&resolved)?;
    let result = (|| -> Result<usize> {
        let mut fetched = 0usize;
        for (email_id, folder_id, folder_path, uid, epoch) in identities {
            if worker.stop.load(Ordering::SeqCst) {
                break;
            }
            if client.get_uid_validity(&folder_path)? != epoch {
                return Err(MailFfiError::Internal("mailbox epoch changed before body fetch".into()));
            }
            let body = client.fetch_body_by_uid(&folder_path, uid)?;
            let committed = worker.registry.with_backfill_commit(
                worker.account_id, worker.stop, || {
                    let mut conn = ctx
                        .pool
                        .get()
                        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                    let transaction = conn.transaction_with_behavior(rusqlite::TransactionBehavior::Immediate)?;
                    let current: bool = transaction.query_row(
                        "SELECT EXISTS(SELECT 1 FROM emails e JOIN folders f
                         ON e.folder_id=f.id AND e.account_id=f.account_id
                         WHERE e.id=?1 AND e.account_id=?2 AND e.folder_id=?3
                         AND e.uid=?4 AND f.path=?5 AND f.uid_validity=?6)",
                        rusqlite::params![email_id, worker.account_id, folder_id, uid, folder_path, epoch],
                        |row| row.get(0),
                    )?;
                    if !current {
                        return Err(MailFfiError::Internal("mailbox identity changed during body fetch".into()));
                    }
                    match body {
                        Some((body_text, body_html)) => {
                            maho_core::services::email::update_email_body(
                                &transaction, &email_id, body_text, body_html,
                            )?;
                        }
                        None => {
                            let now = chrono::Utc::now().format("%Y-%m-%d %H:%M:%S").to_string();
                            transaction.execute(
                                "UPDATE emails SET body_fetched_at = ?1 WHERE id = ?2",
                                rusqlite::params![now, email_id],
                            )?;
                        }
                    }
                    transaction.commit()?;
                    Ok(())
                },
            )?;
            if committed.is_none() {
                break;
            }
            fetched += 1;
        }
        Ok(fetched)
    })();
    safe_logout(client);
    result
}

#[cfg(test)]
#[path = "backfill_review_tests.rs"]
mod review_tests;

#[cfg(test)]
#[path = "backfill_lifecycle_tests.rs"]
mod lifecycle_tests;
