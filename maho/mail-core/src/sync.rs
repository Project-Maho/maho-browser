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

fn is_auth_error(err: &MailFfiError) -> bool {
    match err {
        MailFfiError::MissingCredentials(_) | MailFfiError::OAuth2Refresh(_) => true,
        MailFfiError::Core(ref app_err) => {
            let msg = app_err.to_string().to_uppercase();
            msg.contains("AUTHENTICATIONFAILED") || msg.contains("UNAUTHORIZED")
        }
        _ => false,
    }
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
                if is_auth_error(&err) {
                    crate::ffi::emit_reauth_event(&account_id, "unknown", &err.to_string());
                }
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
            Err(err) => {
                log::warn!("[mail-ffi] idle failed for {account_id}: {err}");
                if is_auth_error(&err) {
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
    let mut new_inbox_mail = false;
    for folder in folders {
        let folder_type = folder_type(&conn, &folder.id)?;
        let folder_inserted = sync_folder(&conn, &resolved, &folder)?;
        inserted += folder_inserted;
        new_inbox_mail |= should_emit_new_mail(&folder_type, folder_inserted);
    }
    if new_inbox_mail {
        crate::ffi::emit_new_mail_event(account_id);
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

fn folder_type(conn: &rusqlite::Connection, folder_id: &str) -> Result<String> {
    conn.query_row(
        "SELECT folder_type FROM folders WHERE id = ?1",
        [folder_id],
        |row| row.get(0),
    )
    .map_err(Into::into)
}

fn should_emit_new_mail(folder_type: &str, inserted: usize) -> bool {
    folder_type == "inbox" && inserted > 0
}

fn sync_folder(
    conn: &rusqlite::Connection,
    resolved: &AccountAuth,
    folder: &FolderSyncTarget,
) -> Result<usize> {
    let mut client = connect_imap(resolved)?;
    let initial_since_uid = if folder.uid_validity == 0 {
        0
    } else {
        folder.last_synced_uid
    };
    let (mut envelopes, server_uid_validity) =
        client.fetch_envelopes_since_uid(&folder.path, initial_since_uid)?;

    let reset_required = folder.uid_validity != 0
        && server_uid_validity != 0
        && server_uid_validity != folder.uid_validity;
    if reset_required {
        let (reset_envelopes, _) = client.fetch_envelopes_since_uid(&folder.path, 0)?;
        envelopes = reset_envelopes;
    }

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
    let reset_required = folder.uid_validity != 0
        && server_uid_validity != 0
        && server_uid_validity != folder.uid_validity;

    if reset_required {
        conn.execute("DELETE FROM emails WHERE folder_id = ?1", [&folder.id])?;
    }

    let existing_uids = existing_uids(conn, &folder.id, envelopes)?;
    let now = chrono::Utc::now().format("%Y-%m-%d %H:%M:%S").to_string();
    let mut inserted = 0usize;
    let mut max_uid = if reset_required { 0 } else { folder.last_synced_uid };
    for env in envelopes {
        if env.uid > max_uid {
            max_uid = env.uid;
        }
        if existing_uids.contains(&env.uid) {
            continue;
        }
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
                env.date,
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
    }
    let effective_uid_validity = if server_uid_validity == 0 {
        folder.uid_validity
    } else {
        server_uid_validity
    };
    conn.execute(
        "UPDATE folders SET total_count = (SELECT COUNT(*) FROM emails WHERE folder_id = ?1),
             unread_count = (SELECT COUNT(*) FROM emails WHERE folder_id = ?1 AND is_read = 0),
             last_synced_uid = ?2, uid_validity = ?3 WHERE id = ?1",
        params![folder.id, max_uid as i64, effective_uid_validity as i64],
    )?;
    Ok(inserted)
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
    let placeholders = std::iter::repeat_n("?", uids.len())
        .collect::<Vec<_>>()
        .join(",");
    let query = format!(
        "SELECT uid FROM emails
         WHERE folder_id = ? AND uid IN ({placeholders})"
    );
    let mut params_vec: Vec<Box<dyn rusqlite::types::ToSql>> =
        vec![Box::new(folder_id.to_string())];
    for uid in uids {
        params_vec.push(Box::new(uid as i64));
    }
    let refs: Vec<&dyn rusqlite::types::ToSql> = params_vec.iter().map(|p| p.as_ref()).collect();
    let mut stmt = conn.prepare(&query)?;
    let rows = stmt.query_map(refs.as_slice(), |row| row.get::<_, i64>(0))?;
    Ok(rows
        .filter_map(std::result::Result::ok)
        .filter_map(|uid| u32::try_from(uid).ok())
        .collect())
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
    fn existing_uids_empty_input() {
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        let ids = existing_uids(&conn, "inbox", &[]).unwrap();
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
