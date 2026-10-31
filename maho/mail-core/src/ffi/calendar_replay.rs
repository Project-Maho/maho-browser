//! Serialized, bounded delivery of durable calendar mutation intents.
use crate::error::{MailFfiError, Result};
use crate::state::AppCtx;
use maho_core::error::AppError;
use maho_core::services::offline_queue::PendingMutation;
use rusqlite::{params, OptionalExtension};
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::{Arc, Mutex, OnceLock, Weak};

type AccountLocks = HashMap<(PathBuf, String), Weak<tokio::sync::Mutex<()>>>;
static LOCKS: OnceLock<Mutex<AccountLocks>> = OnceLock::new();

pub(crate) async fn lock(ctx: &AppCtx, account: &str) -> Result<tokio::sync::OwnedMutexGuard<()>> {
    let lock = {
        let mut locks = LOCKS.get_or_init(Mutex::default).lock()
            .map_err(|_| MailFfiError::Internal("calendar replay lock poisoned".into()))?;
        locks.retain(|_, lock| lock.strong_count() > 0);
        let key = (ctx.db_path.clone(), account.to_owned());
        match locks.get(&key).and_then(Weak::upgrade) {
            Some(lock) => lock,
            None => {
                let lock = Arc::new(tokio::sync::Mutex::new(()));
                locks.insert(key, Arc::downgrade(&lock));
                lock
            }
        }
    };
    Ok(lock.lock_owned().await)
}

fn map_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<PendingMutation> {
    Ok(PendingMutation {
        id: row.get(0)?, account_id: row.get(1)?, email_uid: row.get(2)?,
        folder_path: row.get(3)?, mutation_type: row.get(4)?, target_folder: row.get(5)?,
        created_at: row.get(6)?, calendar_event_id: row.get(7)?, payload_json: row.get(8)?,
    })
}
const COLUMNS: &str = "id, account_id, email_uid, folder_path, mutation_type, target_folder, created_at, calendar_event_id, payload_json";

fn check_state(payload: &serde_json::Value) -> Result<()> {
    match payload.get("delivery_state").and_then(serde_json::Value::as_str) {
        None | Some("pending") => Ok(()),
        _ => Err(AppError::DeliveryUncertain(
            "RSVP delivery requires reconciliation; automatic replay is disabled".into(),
        ).into()),
    }
}

/// Called under the account lock, before queue_calendar_mutation can replace an
/// existing event intent. An uncertain delivery cannot be made retryable by RSVP.
pub(super) fn check_existing(
    db: &rusqlite::Connection, account: &str, event: &str,
) -> Result<()> {
    let mut stmt = db.prepare("SELECT payload_json FROM pending_mutations WHERE account_id=?1 AND calendar_event_id=?2 AND mutation_type='calendar_rsvp_reply'")?;
    for payload in stmt.query_map(params![account, event], |r| r.get::<_, Option<String>>(0))? {
        let payload: serde_json::Value = serde_json::from_str(payload?.as_deref().unwrap_or(""))?;
        check_state(&payload)?;
    }
    Ok(())
}

pub(crate) async fn flush(ctx: &AppCtx, account: &str) -> Result<usize> {
    let _guard = lock(ctx, account).await?;
    let rows = {
        let db = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
        let mut stmt = db.prepare(&format!("SELECT {COLUMNS} FROM pending_mutations WHERE account_id=?1 AND mutation_type LIKE 'calendar_%' ORDER BY created_at, id LIMIT 50"))?;
        let rows = stmt.query_map([account], map_row)?.collect::<rusqlite::Result<Vec<_>>>()?;
        rows
    };
    let mut delivered = 0;
    let mut errors = Vec::new();
    for row in rows {
        match replay_locked(ctx, &row).await {
            Ok(true) => delivered += 1,
            Ok(false) => {},
            Err(error) => errors.push(format!("{} ({}): {}", row.id, row.mutation_type, error)),
        }
    }
    if !errors.is_empty() {
        return Err(MailFfiError::Internal(format!(
            "Pending mutation delivery failed: {}", errors.join("; ")
        )));
    }
    Ok(delivered)
}

/// Persist immediately before transport, after payload/credential validation.
/// Cancellation or process exit after this point requires reconciliation.
pub(super) fn mark_sending(ctx: &AppCtx, mutation: &PendingMutation) -> Result<()> {
    let mut payload: serde_json::Value = serde_json::from_str(mutation.payload_json.as_deref().unwrap_or(""))?;
    let object = payload.as_object_mut().ok_or_else(|| AppError::Validation("RSVP payload must be an object".into()))?;
    object.insert("delivery_state".into(), "uncertain".into());
    let db = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
    db.execute("UPDATE pending_mutations SET payload_json=?1 WHERE id=?2 AND account_id=?3",
        params![payload.to_string(), mutation.id, mutation.account_id])?;
    Ok(())
}

/// The account lock must cover this entire operation, including SMTP and ack.
pub(super) async fn replay_locked(ctx: &AppCtx, snapshot: &PendingMutation) -> Result<bool> {
    // Never deliver from a stale caller snapshot: another sender may have acked
    // or marked uncertainty while this caller was waiting for the account lock.
    let mutation = {
        let db = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
        db.query_row(&format!("SELECT {COLUMNS} FROM pending_mutations WHERE id=?1 AND account_id=?2"),
            params![snapshot.id, snapshot.account_id], map_row).optional()?
    };
    let Some(mutation) = mutation else { return Ok(false); };
    if mutation.mutation_type == "calendar_rsvp_reply" {
        let payload: serde_json::Value = serde_json::from_str(mutation.payload_json.as_deref().unwrap_or(""))?;
        check_state(&payload)?;
    }
    let result = super::deliver_calendar_mutation(ctx, &mutation).await;
    match result {
        Ok(()) => {
            let db = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
            db.execute("DELETE FROM pending_mutations WHERE id=?1 AND account_id=?2", params![mutation.id, mutation.account_id])?;
            Ok(true)
        }
        Err(error) => {
            // Only definite pre-send failure or SMTP rejection is retryable.
            // In particular a spawn_blocking join failure is not proof of no send.
            if matches!(&error, MailFfiError::Core(AppError::Validation(_) | AppError::Network(_) | AppError::Auth(_))) {
                let db = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                db.execute("UPDATE pending_mutations SET payload_json=?1 WHERE id=?2 AND account_id=?3",
                    params![mutation.payload_json, mutation.id, mutation.account_id])?;
            }
            Err(error)
        }
    }
}
