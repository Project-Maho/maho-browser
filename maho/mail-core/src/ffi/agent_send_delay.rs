// Copyright 2026 Maho Browser. All rights reserved.

//! Opt-in undo window for agent-sent mail (`agent.mail_send_delay_seconds`).
//!
//! Default 0 sends immediately (the 2026-10-21 no-wait directive). With 10, 20
//! or 30 seconds the agent send is persisted to `agent_send_queue`, the call
//! returns `{"status":"queued"}` at once, and delivery happens at the deadline
//! unless the user cancels it (toast or Agent activity journal) first.
//!
//! Durability: the queue is a SQLite table, so a crash or quit before the
//! deadline does not drop the message. On the next start
//! ([`resume_pending`]) every still-`pending` row is re-armed and sent at its
//! deadline, or immediately if the deadline already passed while the browser
//! was down. The scheduler tick also sweeps due rows as a backstop.
//!
//! Exactly-once: the only transition into delivery is one conditional UPDATE
//! (`pending` -> `sending`, gated on the deadline), and cancel is the same
//! conditional UPDATE to `cancelled`; whichever commits first wins. A row left
//! in `sending` by a crash is never replayed (the SMTP server may already have
//! accepted it) and is marked `uncertain` on restart, mirroring the outbox.
//!
//! Only mail send is covered. Purchases, form submits and other browser
//! actions are not delayed and are not claimed to be undoable.

use std::ffi::c_void;
use std::future::Future;
use std::os::raw::c_char;
use std::sync::Arc;

use rusqlite::{params, OptionalExtension};
use serde::Serialize;

use super::compose_api::{send_now, ComposeEmailRequest};
use super::read_api::{accept_read, accept_read_call, blocking_json, MahoMailReadCallback};
use super::{c_string, non_empty};
use crate::error::{MailFfiError, Result};
use crate::state::AppCtx;

pub(crate) const DELAY_SETTING_KEY: &str = "agent.mail_send_delay_seconds";
pub(crate) const ALLOWED_DELAYS: [u64; 4] = [0, 10, 20, 30];

fn ensure_table(conn: &rusqlite::Connection) -> Result<()> {
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS agent_send_queue (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            request_json TEXT NOT NULL,
            send_at_ms INTEGER NOT NULL,
            status TEXT NOT NULL DEFAULT 'pending',
            result_json TEXT,
            last_error TEXT,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at TEXT NOT NULL DEFAULT (datetime('now'))
        );
        CREATE INDEX IF NOT EXISTS idx_agent_send_queue_due
            ON agent_send_queue(status, send_at_ms);",
    )?;
    Ok(())
}

/// Reads the opt-in delay. Missing, malformed or non-option values mean 0, so
/// a bad setting can only ever fall back to the immediate-send default.
pub(crate) fn configured_delay_seconds(conn: &rusqlite::Connection) -> u64 {
    let raw: Option<String> = conn
        .query_row(
            "SELECT value FROM app_settings WHERE key = ?1",
            [DELAY_SETTING_KEY],
            |row| row.get(0),
        )
        .optional()
        .ok()
        .flatten();
    raw.and_then(|v| v.trim().trim_matches('"').parse::<u64>().ok())
        .filter(|v| ALLOWED_DELAYS.contains(v))
        .unwrap_or(0)
}

fn now_ms() -> i64 {
    chrono::Utc::now().timestamp_millis()
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct QueuedAgentSend {
    pub status: String,
    pub id: String,
    pub send_at_ms: i64,
    pub delay_seconds: u64,
}

pub(crate) fn enqueue(
    conn: &rusqlite::Connection,
    request: &ComposeEmailRequest,
    delay_seconds: u64,
    now_ms: i64,
) -> Result<QueuedAgentSend> {
    ensure_table(conn)?;
    let id = uuid::Uuid::new_v4().to_string();
    let send_at_ms = now_ms + (delay_seconds as i64) * 1000;
    conn.execute(
        "INSERT INTO agent_send_queue (id, account_id, request_json, send_at_ms) VALUES (?1, ?2, ?3, ?4)",
        params![id, request.account_id, serde_json::to_string(request)?, send_at_ms],
    )?;
    Ok(QueuedAgentSend {
        status: "queued".to_string(),
        id,
        send_at_ms,
        delay_seconds,
    })
}

/// Cancels a pending send. Returns false when the row already left `pending`
/// (sent, sending, or cancelled) - the window is over and nothing changed.
pub(crate) fn cancel(conn: &rusqlite::Connection, id: &str) -> Result<bool> {
    ensure_table(conn)?;
    let changed = conn.execute(
        "UPDATE agent_send_queue SET status = 'cancelled', updated_at = datetime('now')
         WHERE id = ?1 AND status = 'pending'",
        [id],
    )?;
    Ok(changed == 1)
}

/// Atomically takes ownership of a due pending row. At most one caller (timer,
/// scheduler sweep, restart recovery, another process) can ever win.
fn claim(conn: &rusqlite::Connection, id: &str, now_ms: i64) -> Result<Option<ComposeEmailRequest>> {
    let json: Option<String> = conn
        .query_row(
            "UPDATE agent_send_queue SET status = 'sending', updated_at = datetime('now')
             WHERE id = ?1 AND status = 'pending' AND send_at_ms <= ?2
             RETURNING request_json",
            params![id, now_ms],
            |row| row.get(0),
        )
        .optional()?;
    json.map(|j| serde_json::from_str(&j).map_err(MailFfiError::from))
        .transpose()
}

fn due_ids(conn: &rusqlite::Connection, now_ms: i64) -> Result<Vec<String>> {
    ensure_table(conn)?;
    let mut stmt = conn.prepare(
        "SELECT id FROM agent_send_queue WHERE status = 'pending' AND send_at_ms <= ?1
         ORDER BY send_at_ms, id",
    )?;
    let ids = stmt
        .query_map([now_ms], |row| row.get(0))?
        .collect::<rusqlite::Result<Vec<String>>>()?;
    Ok(ids)
}

fn record_outcome(conn: &rusqlite::Connection, id: &str, outcome: &Result<String>) -> Result<()> {
    let (status, result, error) = match outcome {
        // send_now returns Ok for an outbox fallback or uncertain delivery too;
        // mirror its status so the journal never claims "sent" falsely.
        Ok(json) => {
            let reported = serde_json::from_str::<serde_json::Value>(json)
                .ok()
                .and_then(|v| v.get("status").and_then(|s| s.as_str()).map(str::to_owned));
            let status = match reported.as_deref() {
                Some("queued") => "outboxed",
                Some("uncertain") => "uncertain",
                _ => "sent",
            };
            (status, Some(json.as_str()), None)
        }
        Err(e) => ("failed", None, Some(e.to_string())),
    };
    conn.execute(
        "UPDATE agent_send_queue SET status = ?2, result_json = ?3, last_error = ?4,
         updated_at = datetime('now') WHERE id = ?1 AND status = 'sending'",
        params![id, status, result, error],
    )?;
    Ok(())
}

/// Delivers one row if it is due and still pending. Returns whether this call
/// performed the delivery. `send` is the transport (production: [`send_now`]).
pub(crate) async fn deliver_if_due<F, Fut>(
    ctx: &Arc<AppCtx>,
    id: &str,
    now_ms: i64,
    send: F,
) -> Result<bool>
where
    F: FnOnce(Arc<AppCtx>, ComposeEmailRequest) -> Fut,
    Fut: Future<Output = Result<String>>,
{
    let Some(request) = ctx.with_db(|conn| {
        ensure_table(conn)?;
        claim(conn, id, now_ms)
    })?
    else {
        return Ok(false);
    };
    let outcome = send(ctx.clone(), request).await;
    if let Err(e) = &outcome {
        log::warn!("agent delayed send {id} failed: {e}");
    }
    ctx.with_db(|conn| record_outcome(conn, id, &outcome))?;
    Ok(true)
}

/// Sweeps every due pending row. Returns how many this call delivered.
pub(crate) async fn run_due_with<F, Fut>(ctx: &Arc<AppCtx>, now_ms: i64, send: F) -> Result<usize>
where
    F: Fn(Arc<AppCtx>, ComposeEmailRequest) -> Fut,
    Fut: Future<Output = Result<String>>,
{
    let ids = ctx.with_db(|conn| due_ids(conn, now_ms))?;
    let mut delivered = 0;
    for id in ids {
        if deliver_if_due(ctx, &id, now_ms, &send).await? {
            delivered += 1;
        }
    }
    Ok(delivered)
}

/// Production sweep used by the scheduler tick.
pub(crate) async fn run_due(ctx: &Arc<AppCtx>, now_ms: i64) -> Result<usize> {
    run_due_with(ctx, now_ms, send_now).await
}

fn arm_timer(ctx: Arc<AppCtx>, id: String, send_at_ms: i64) {
    let Some(rt) = crate::runtime::runtime() else {
        log::error!("agent delayed send {id}: tokio runtime unavailable; scheduler sweep will deliver");
        return;
    };
    rt.spawn(async move {
        let wait = (send_at_ms - now_ms()).max(0) as u64;
        tokio::time::sleep(std::time::Duration::from_millis(wait)).await;
        if let Err(e) = deliver_if_due(&ctx, &id, now_ms().max(send_at_ms), send_now).await {
            log::error!("agent delayed send {id}: {e}");
        }
    });
}

/// Startup recovery: a row left `sending` by a crash is marked `uncertain`
/// (never replayed), and every `pending` row is re-armed so it is sent at its
/// deadline, or immediately when the deadline passed while the app was down.
pub(crate) fn resume_pending(ctx: &Arc<AppCtx>) -> Result<usize> {
    let pending = ctx.with_db(recover_rows)?;
    let count = pending.len();
    for (id, send_at_ms) in pending {
        arm_timer(ctx.clone(), id, send_at_ms);
    }
    Ok(count)
}

/// Database half of [`resume_pending`]: interrupted `sending` rows become
/// `uncertain`; returns the `pending` rows that must be re-armed.
fn recover_rows(conn: &rusqlite::Connection) -> Result<Vec<(String, i64)>> {
    ensure_table(conn)?;
    conn.execute(
        "UPDATE agent_send_queue SET status = 'uncertain',
         last_error = 'Delivery interrupted; check Sent before resending',
         updated_at = datetime('now') WHERE status = 'sending'",
        [],
    )?;
    let mut stmt = conn.prepare(
        "SELECT id, send_at_ms FROM agent_send_queue WHERE status = 'pending' ORDER BY send_at_ms, id",
    )?;
    let rows = stmt
        .query_map([], |row| Ok((row.get::<_, String>(0)?, row.get::<_, i64>(1)?)))?
        .collect::<rusqlite::Result<Vec<_>>>()?;
    Ok(rows)
}

/// Entry for agent-originated sends: immediate when the delay is 0, otherwise
/// durable enqueue + armed timer, returning `{"status":"queued",...}`.
/// `delay_override` is the browser profile pref (Settings > AI) forwarded by
/// the helper; when absent the mail database setting is used. Either source
/// only ever yields one of [`ALLOWED_DELAYS`], else 0.
pub(crate) async fn agent_send(
    ctx: Arc<AppCtx>,
    request: ComposeEmailRequest,
    delay_override: Option<u64>,
) -> Result<String> {
    let queued = ctx.with_db(|conn| {
        let delay = match delay_override {
            Some(v) if ALLOWED_DELAYS.contains(&v) => v,
            Some(_) => 0,
            None => configured_delay_seconds(conn),
        };
        if delay == 0 {
            return Ok(None);
        }
        enqueue(conn, &request, delay, now_ms()).map(Some)
    })?;
    match queued {
        None => send_now(ctx, request).await,
        Some(queued) => {
            arm_timer(ctx, queued.id.clone(), queued.send_at_ms);
            Ok(serde_json::to_string(&queued)?)
        }
    }
}

#[derive(Debug, Serialize)]
pub struct AgentSendQueueItem {
    pub id: String,
    pub account_id: String,
    pub subject: String,
    pub to: Vec<String>,
    pub send_at_ms: i64,
    pub status: String,
}

pub(crate) fn list_pending(conn: &rusqlite::Connection) -> Result<Vec<AgentSendQueueItem>> {
    ensure_table(conn)?;
    let mut stmt = conn.prepare(
        "SELECT id, account_id, request_json, send_at_ms, status FROM agent_send_queue
         WHERE status = 'pending' ORDER BY send_at_ms, id",
    )?;
    let rows = stmt
        .query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, i64>(3)?,
                row.get::<_, String>(4)?,
            ))
        })?
        .collect::<rusqlite::Result<Vec<_>>>()?;
    rows.into_iter()
        .map(|(id, account_id, json, send_at_ms, status)| {
            let req: ComposeEmailRequest = serde_json::from_str(&json)?;
            Ok(AgentSendQueueItem {
                id,
                account_id,
                subject: req.subject,
                to: req.to,
                send_at_ms,
                status,
            })
        })
        .collect()
}

/// Cancels a queued agent send from the toast or the Agent activity journal.
/// Result: `{"cancelled":true}` when stopped in time, `false` when the window
/// already closed (sent/sending) or the id is unknown.
#[no_mangle]
pub extern "C" fn MahoMailCancelAgentSend(
    id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(id) = non_empty(id, "id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let cancelled = ctx.with_db(|conn| cancel(conn, &id))?;
                Ok(serde_json::json!({ "cancelled": cancelled }))
            })
        })
    })
}

/// Lists agent sends still inside their undo window.
#[no_mangle]
pub extern "C" fn MahoMailListAgentSendQueue(
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || ctx.with_db(list_pending))
        })
    })
}

/// Optional `agent_send_delay_seconds` field the browser adds to the request.
fn delay_override_from_json(raw: &str) -> Option<u64> {
    serde_json::from_str::<serde_json::Value>(raw)
        .ok()?
        .get("agent_send_delay_seconds")?
        .as_u64()
}

/// Agent-originated send honoring `agent.mail_send_delay_seconds` (request
/// field `agent_send_delay_seconds` from the profile pref wins when present).
#[no_mangle]
pub extern "C" fn MahoMailAgentSendEmail(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(request) = serde_json::from_str::<ComposeEmailRequest>(&raw) else {
            return false;
        };
        let delay_override = delay_override_from_json(&raw);
        accept_read(callback, user_data, move |ctx| {
            Box::pin(agent_send(ctx, request, delay_override))
        })
    })
}

#[cfg(test)]
#[path = "agent_send_delay_tests.rs"]
mod tests;
