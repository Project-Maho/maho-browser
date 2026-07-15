// Copyright 2026 Maho Browser. All rights reserved.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use futures::FutureExt as _;
use tokio_util::sync::CancellationToken;

use crate::account::{connect_imap, resolve_account_auth, safe_logout};
use crate::error::{MailFfiError, Result};
use crate::state::{AppCtx, WorkerHandle};

const BACKFILL_BATCH_SIZE: i64 = 10;
const BACKFILL_DELAY: Duration = Duration::from_millis(100);
const SUPERVISOR_BACKOFF_MAX: Duration = Duration::from_secs(60);

pub fn start_backfill_worker(ctx: Arc<AppCtx>, account_id: String) -> WorkerHandle {
    let stop = Arc::new(AtomicBool::new(false));
    let token = CancellationToken::new();
    let task = tokio::spawn(supervise_backfill(
        ctx,
        account_id,
        Arc::clone(&stop),
        token,
    ));
    WorkerHandle {
        stop,
        tasks: vec![task],
    }
}

async fn supervise_backfill(
    ctx: Arc<AppCtx>,
    account_id: String,
    stop: Arc<AtomicBool>,
    token: CancellationToken,
) {
    let mut backoff = Duration::from_secs(1);
    while !stop.load(Ordering::SeqCst) {
        let attempt = std::panic::AssertUnwindSafe(backfill_loop(
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
) {
    let mut consecutive_errors = 0u32;
    while !stop.load(Ordering::SeqCst) && consecutive_errors < 5 {
        match backfill_batch(Arc::clone(&ctx), &account_id).await {
            Ok(0) => return,
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

pub async fn backfill_batch(ctx: Arc<AppCtx>, account_id: &str) -> Result<usize> {
    let account_id = account_id.to_string();
    tokio::task::spawn_blocking(move || backfill_batch_blocking(&ctx, &account_id))
        .await
        .map_err(|e| MailFfiError::Internal(format!("backfill join error: {e}")))?
}

fn backfill_batch_blocking(ctx: &AppCtx, account_id: &str) -> Result<usize> {
    let conn = ctx
        .pool
        .get()
        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
    let emails = maho_core::services::email::get_emails_needing_body_fetch(
        &conn,
        account_id,
        BACKFILL_BATCH_SIZE,
    )?;
    if emails.is_empty() {
        return Ok(0);
    }
    let resolved = resolve_account_auth(&conn, &ctx.credential_key, account_id)?;
    let mut client = connect_imap(&resolved)?;
    let mut fetched = 0usize;
    for (email_id, message_id, folder_path, _uid) in emails {
        match client.fetch_body_by_message_id(&folder_path, &message_id)? {
            Some((body_text, body_html)) => {
                maho_core::services::email::update_email_body(
                    &conn, &email_id, body_text, body_html,
                )?;
                fetched += 1;
            }
            None => {
                let now = chrono::Utc::now().format("%Y-%m-%d %H:%M:%S").to_string();
                conn.execute(
                    "UPDATE emails SET body_fetched_at = ?1 WHERE id = ?2",
                    rusqlite::params![now, email_id],
                )?;
                fetched += 1;
            }
        }
    }
    safe_logout(client);
    Ok(fetched)
}
