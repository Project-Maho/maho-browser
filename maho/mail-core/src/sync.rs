// Copyright 2026 Maho Browser. All rights reserved.

use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use futures::FutureExt as _;
use maho_core::folder_normalize::{classify_folder, folder_type_to_string, is_visible_folder};
use maho_core::imap_client::ImapEnvelope;
use rusqlite::params;
use tokio_util::sync::CancellationToken;
use uuid::Uuid;

use crate::account::{connect_imap, resolve_account_auth, safe_logout, AccountAuth};
use crate::error::{MailFfiError, Result};
use crate::state::{AppCtx, WorkerHandle};

const SYNC_INTERVAL: Duration = Duration::from_secs(60);
const IDLE_TIMEOUT: Duration = Duration::from_secs(120);
const SUPERVISOR_BACKOFF_MAX: Duration = Duration::from_secs(60);

#[derive(Clone)]
struct FolderSyncTarget {
    id: String,
    path: String,
    last_synced_uid: u32,
    uid_validity: u32,
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
            }
        }
        tokio::select! {
            _ = token.cancelled() => return,
            () = tokio::time::sleep(SYNC_INTERVAL) => {}
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
            Err(err) => log::warn!("[mail-ffi] idle failed for {account_id}: {err}"),
        }
        tokio::select! {
            _ = token.cancelled() => return,
            () = tokio::time::sleep(Duration::from_secs(5)) => {}
        }
    }
}

pub async fn sync_account_once(ctx: Arc<AppCtx>, account_id: &str) -> Result<usize> {
    let account_id = account_id.to_string();
    tokio::task::spawn_blocking(move || sync_account_blocking(&ctx, &account_id))
        .await
        .map_err(|e| MailFfiError::Internal(format!("sync join error: {e}")))?
}

fn sync_account_blocking(ctx: &AppCtx, account_id: &str) -> Result<usize> {
    let conn = ctx
        .pool
        .get()
        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
    let resolved = resolve_account_auth(&conn, &ctx.credential_key, account_id)?;
    sync_folders(&conn, &resolved)?;
    let folders = sync_targets(&conn, account_id)?;
    let mut inserted = 0usize;
    for folder in folders {
        inserted += sync_folder(&conn, &resolved, &folder)?;
    }
    Ok(inserted)
}

fn sync_folders(conn: &rusqlite::Connection, resolved: &AccountAuth) -> Result<()> {
    let mut client = connect_imap(resolved)?;
    let folders = client.list_folders()?;
    safe_logout(client);

    let existing = existing_folders(conn, &resolved.account.id)?;
    let mut visible_paths = std::collections::HashSet::new();
    for metadata in folders
        .iter()
        .filter(|metadata| is_visible_folder(metadata))
    {
        visible_paths.insert(metadata.raw_path.clone());
        let folder_type = folder_type_to_string(&classify_folder(metadata));
        if let Some(existing_id) = existing.get(&metadata.raw_path) {
            conn.execute(
                "UPDATE folders SET name = ?1, folder_type = ?2 WHERE id = ?3 AND account_id = ?4",
                params![
                    metadata.display_name,
                    folder_type,
                    existing_id,
                    resolved.account.id
                ],
            )?;
        } else {
            conn.execute(
                "INSERT INTO folders (id, account_id, name, path, folder_type, unread_count, total_count)
                 VALUES (?1, ?2, ?3, ?4, ?5, 0, 0)",
                params![
                    Uuid::new_v4().to_string(),
                    resolved.account.id,
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
        conn.execute(
            "DELETE FROM folders WHERE account_id = ?1 AND path = ?2",
            params![resolved.account.id, stale_path],
        )?;
    }
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
        "SELECT id, path, last_synced_uid, uid_validity FROM folders
         WHERE account_id = ?1 AND folder_type IN ('inbox', 'sent', 'drafts', 'archive', 'custom')",
    )?;
    let rows = stmt.query_map([account_id], |row| {
        let last_uid: i64 = row.get(2)?;
        let uid_validity: i64 = row.get(3)?;
        Ok(FolderSyncTarget {
            id: row.get(0)?,
            path: row.get(1)?,
            last_synced_uid: u32::try_from(last_uid).unwrap_or(0),
            uid_validity: u32::try_from(uid_validity).unwrap_or(0),
        })
    })?;
    Ok(rows.filter_map(std::result::Result::ok).collect())
}

fn sync_folder(
    conn: &rusqlite::Connection,
    resolved: &AccountAuth,
    folder: &FolderSyncTarget,
) -> Result<usize> {
    let mut client = connect_imap(resolved)?;
    let since_uid = if folder.uid_validity == 0 {
        0
    } else {
        folder.last_synced_uid
    };
    let (envelopes, server_uid_validity) =
        client.fetch_envelopes_since_uid(&folder.path, since_uid)?;
    safe_logout(client);
    insert_envelopes(
        conn,
        &resolved.account.id,
        folder,
        &envelopes,
        server_uid_validity,
    )
}

fn insert_envelopes(
    conn: &rusqlite::Connection,
    account_id: &str,
    folder: &FolderSyncTarget,
    envelopes: &[ImapEnvelope],
    server_uid_validity: u32,
) -> Result<usize> {
    if folder.uid_validity != 0 && server_uid_validity != folder.uid_validity {
        conn.execute("DELETE FROM emails WHERE folder_id = ?1", [&folder.id])?;
        conn.execute(
            "UPDATE folders SET last_synced_uid = 0, uid_validity = ?1 WHERE id = ?2",
            params![server_uid_validity as i64, folder.id],
        )?;
    }

    let existing = existing_message_ids(conn, account_id, envelopes)?;
    let now = chrono::Utc::now().format("%Y-%m-%d %H:%M:%S").to_string();
    let mut inserted = 0usize;
    let mut max_uid = folder.last_synced_uid;
    for env in envelopes {
        if env.uid > max_uid {
            max_uid = env.uid;
        }
        if env.message_id.is_empty() || existing.contains(&env.message_id) {
            continue;
        }
        let email_id = Uuid::new_v4().to_string();
        conn.execute(
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
                env.message_id,
                env.in_reply_to,
                env.subject,
                env.from_address,
                env.from_name,
                serde_json::to_string(&env.to_addresses).unwrap_or_else(|_| "[]".to_string()),
                serde_json::to_string(&env.cc_addresses).unwrap_or_else(|_| "[]".to_string()),
                env.date,
                env.subject.chars().take(100).collect::<String>(),
                env.flags.iter().any(|f| f.contains("Seen")),
                env.flags.iter().any(|f| f.contains("Flagged")),
                env.has_attachments,
                env.size as i64,
                now,
            ],
        )?;
        inserted += 1;
        for att in &env.attachments {
            conn.execute(
                "INSERT OR IGNORE INTO attachments (id, email_id, filename, mime_type, size, content_id)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
                params![
                    att.part_id,
                    email_id,
                    att.filename,
                    att.mime_type,
                    att.size,
                    att.content_id.clone().or_else(|| Some(att.part_id.clone()))
                ],
            )?;
        }
    }
    conn.execute(
        "UPDATE folders SET total_count = (SELECT COUNT(*) FROM emails WHERE folder_id = ?1),
             unread_count = (SELECT COUNT(*) FROM emails WHERE folder_id = ?1 AND is_read = 0),
             last_synced_uid = ?2, uid_validity = ?3 WHERE id = ?1",
        params![folder.id, max_uid as i64, server_uid_validity as i64],
    )?;
    Ok(inserted)
}

fn existing_message_ids(
    conn: &rusqlite::Connection,
    account_id: &str,
    envelopes: &[ImapEnvelope],
) -> Result<std::collections::HashSet<String>> {
    let ids: Vec<&str> = envelopes
        .iter()
        .filter(|env| !env.message_id.is_empty())
        .map(|env| env.message_id.as_str())
        .collect();
    if ids.is_empty() {
        return Ok(std::collections::HashSet::new());
    }
    let placeholders = std::iter::repeat_n("?", ids.len())
        .collect::<Vec<_>>()
        .join(",");
    let query = format!(
        "SELECT message_id FROM emails WHERE account_id = ? AND message_id IN ({placeholders})"
    );
    let mut params_vec: Vec<Box<dyn rusqlite::types::ToSql>> =
        vec![Box::new(account_id.to_string())];
    for id in ids {
        params_vec.push(Box::new(id.to_string()));
    }
    let refs: Vec<&dyn rusqlite::types::ToSql> = params_vec.iter().map(|p| p.as_ref()).collect();
    let mut stmt = conn.prepare(&query)?;
    let rows = stmt.query_map(refs.as_slice(), |row| row.get::<_, String>(0))?;
    Ok(rows.filter_map(std::result::Result::ok).collect())
}

async fn idle_wait_once(ctx: Arc<AppCtx>, account_id: &str) -> Result<bool> {
    let account_id = account_id.to_string();
    tokio::task::spawn_blocking(move || {
        let conn = ctx
            .pool
            .get()
            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
        let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
        let folders = sync_targets(&conn, &account_id)?;
        for folder in folders {
            let mut client = connect_imap(&resolved)?;
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
#[allow(clippy::disallowed_methods)]
mod tests {
    use super::*;

    #[test]
    fn existing_message_ids_empty_input() {
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        let ids = existing_message_ids(&conn, "acc1", &[]).unwrap();
        assert!(ids.is_empty());
    }
}
