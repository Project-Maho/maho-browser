// Copyright 2026 Maho Browser. All rights reserved.

use std::collections::BTreeMap;
use std::ffi::c_void;
use std::os::raw::c_char;

use serde_json::json;

use crate::account::{connect_imap, resolve_account_auth, safe_logout};
use crate::error::{MailFfiError, Result};

use super::read_api::{accept_read, accept_read_call, blocking_json, MahoMailReadCallback};
use super::{c_string, non_empty};

#[path = "mail_replay.rs"]
mod mail_replay;

#[cfg(test)]
#[path = "mutation_review_tests.rs"]
mod mutation_review_tests;

#[cfg(test)]
#[path = "memory_thread_batch_tests.rs"]
mod memory_thread_batch_tests;

fn connect_email_imap(
    conn: &rusqlite::Connection,
    resolved: &crate::account::AccountAuth,
    folder_path: &str,
) -> Result<maho_core::imap_client::ImapClient> {
    let epoch: u32 = conn.query_row(
        "SELECT uid_validity FROM folders WHERE account_id=?1 AND path=?2",
        rusqlite::params![resolved.account.id, folder_path], |row| row.get(0),
    )?;
    let mut client = connect_imap(resolved)?;
    if let Err(error) = client.validate_uid_validity(folder_path, epoch) {
        safe_logout(client);
        return Err(error.into());
    }
    Ok(client)
}

fn get_email_imap_info(
    conn: &rusqlite::Connection,
    email_id: &str,
) -> Result<Option<(String, String, u32)>> {
    let result = conn.query_row(
        "SELECT e.account_id, f.path, e.uid FROM emails e JOIN folders f ON e.folder_id = f.id AND e.account_id = f.account_id WHERE e.id = ?1",
        [email_id],
        |row| {
            let account_id: String = row.get(0)?;
            let folder_path: String = row.get(1)?;
            let uid: Option<i64> = row.get(2)?;
            Ok((account_id, folder_path, uid))
        },
    ).map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

    match result.2 {
        Some(uid) if uid > 0 => {
            let uid = u32::try_from(uid)
                .map_err(|_| MailFfiError::InvalidArg("email UID exceeds IMAP range"))?;
            Ok(Some((result.0, result.1, uid)))
        }
        _ => Ok(None),
    }
}

#[no_mangle]
pub extern "C" fn MahoMailMarkRead(
    email_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let remote = get_email_imap_info(&conn, &email_id)?.map(|identity| {
                    resolve_account_auth(&conn, &ctx.credential_key, &identity.0)
                        .map(|auth| (identity, auth))
                }).transpose()?;
                let transaction = conn.unchecked_transaction()?;
                let conn = &transaction;
                maho_core::services::email::mark_read(&conn, &email_id)?;

                if let Some(((account_id, folder_path, uid), resolved)) = remote {
                    if let Ok(mut client) = connect_email_imap(&conn, &resolved, &folder_path) {
                        let res = client.set_flags(&folder_path, uid, "\\Seen");
                        safe_logout(client);
                        if res.is_err() {
                            maho_core::services::offline_queue::queue_mutation(
                                &conn,
                                &account_id,
                                uid as i64,
                                &folder_path,
                                "mark_read",
                                None,
                            )?;
                        }
                    } else {
                        maho_core::services::offline_queue::queue_mutation(
                            &conn,
                            &account_id,
                            uid as i64,
                            &folder_path,
                            "mark_read",
                            None,
                        )?;
                    }
                }
                transaction.commit()?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailMarkUnread(
    email_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let remote = get_email_imap_info(&conn, &email_id)?.map(|identity| {
                    resolve_account_auth(&conn, &ctx.credential_key, &identity.0)
                        .map(|auth| (identity, auth))
                }).transpose()?;
                let transaction = conn.unchecked_transaction()?;
                let conn = &transaction;
                maho_core::services::email::mark_unread(&conn, &email_id)?;

                if let Some(((account_id, folder_path, uid), resolved)) = remote {
                    if let Ok(mut client) = connect_email_imap(&conn, &resolved, &folder_path) {
                        let res = client.remove_flags(&folder_path, uid, "\\Seen");
                        safe_logout(client);
                        if res.is_err() {
                            maho_core::services::offline_queue::queue_mutation(
                                &conn,
                                &account_id,
                                uid as i64,
                                &folder_path,
                                "mark_unread",
                                None,
                            )?;
                        }
                    } else {
                        maho_core::services::offline_queue::queue_mutation(
                            &conn,
                            &account_id,
                            uid as i64,
                            &folder_path,
                            "mark_unread",
                            None,
                        )?;
                    }
                }
                transaction.commit()?;
                Ok("{}".to_string())
            })
        })
    })
}

/// UID STORE chunk size for batch flag mutations (design U10 bound).
const MAIL_BATCH_STORE_CHUNK: usize = 500;

/// Grouped/batched mark-read (design U10): one database lease applies local
/// flags, recounts each touched folder once, and persists the outbox before
/// any network attempt; the pool connection is released before IMAP waits;
/// one authenticated session per account selects each folder once and issues
/// UID STORE chunks of at most 500 UIDs; a short second lease acknowledges
/// only this batch's outbox entries for groups whose remote STORE succeeded,
/// leaving failed groups' retry records (and any newer opposite operations)
/// untouched.
#[no_mangle]
pub extern "C" fn MahoMailBatchMarkRead(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        // Synchronous argument validation: reject malformed requests without
        // ever invoking the callback.
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(parsed) = serde_json::from_str::<serde_json::Value>(&raw) else {
            return false;
        };
        let Some(id_values) = parsed.get("email_ids").and_then(|v| v.as_array()) else {
            return false;
        };
        let mut email_ids: Vec<String> = id_values
            .iter()
            .filter_map(|value| value.as_str().map(str::to_string))
            .collect();
        if email_ids.is_empty() {
            return false;
        }
        // Deduplicate requested ids by identity before any work.
        email_ids.sort();
        email_ids.dedup();
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || batch_mark_read_impl(&ctx, email_ids))
        })
    })
}

struct BatchFolderGroup {
    uids: Vec<u32>,
    epoch: u32,
}

fn batch_mark_read_impl(
    ctx: &crate::state::AppCtx,
    email_ids: Vec<String>,
) -> Result<serde_json::Value> {
    // ---------- Phase A: one short database lease ----------
    struct PlannedEmail {
        account_id: String,
        folder_path: String,
        uid: u32,
        epoch: u32,
        folder_id: String,
    }

    // account_id -> folder_path -> grouped uids; plus this batch's outbox ids.
    let (groups, resolved_by_account, outbox_ids_by_account) = {
        let conn = ctx
            .pool
            .get()
            .map_err(|e| MailFfiError::Pool(e.to_string()))?;

        // Resolve every requested id before any mutation; missing ids fail the
        // whole batch with the established not-found error.
        let mut planned: Vec<PlannedEmail> = Vec::with_capacity(email_ids.len());
        for email_id in &email_ids {
            let Some((account_id, folder_path, uid)) = get_email_imap_info(&conn, email_id)? else {
                return Err(MailFfiError::Core(maho_core::error::AppError::NotFound(
                    format!("Email {email_id} not found"),
                )));
            };
            let (epoch, folder_id) = conn.query_row("SELECT uid_validity, id FROM folders WHERE account_id=?1 AND path=?2",
                rusqlite::params![account_id, folder_path], |row| Ok((row.get(0)?, row.get(1)?)))?;
            planned.push(PlannedEmail { account_id, folder_path, uid, epoch, folder_id });
        }

        // Token rotation must commit independently of the local mutation.
        let mut resolved_by_account = BTreeMap::new();
        for email in &planned {
            if !resolved_by_account.contains_key(&email.account_id) {
                let resolved = resolve_account_auth(&conn, &ctx.credential_key, &email.account_id)?;
                resolved_by_account.insert(email.account_id.clone(), resolved);
            }
        }
        let transaction = conn.unchecked_transaction()?;
        let conn = &transaction;

        // Bound SQLite parameters as well as the later UID STORE command.
        for chunk in email_ids.chunks(MAIL_BATCH_STORE_CHUNK) {
            let placeholders = vec!["?"; chunk.len()].join(", ");
            conn.execute(&format!("UPDATE emails SET is_read=1 WHERE id IN ({placeholders})"),
                rusqlite::params_from_iter(chunk.iter()))?;
        }
        let folders: std::collections::BTreeSet<_> = planned.iter().map(|email| &email.folder_id).collect();
        for folder in folders {
            conn.execute("UPDATE folders SET unread_count=(SELECT COUNT(*) FROM emails WHERE folder_id=?1 AND is_read=0),
                reconciliation_version=reconciliation_version+1 WHERE id=?1", [folder])?;
        }

        // Persist outbox entries BEFORE network attempts; deduplicated by
        // existing mutation identity (INSERT OR REPLACE in the queue).
        for email in &planned {
            maho_core::services::offline_queue::queue_mutation(
                conn,
                &email.account_id,
                i64::from(email.uid),
                &email.folder_path,
                "mark_read",
                None,
            )?;
        }
        // Capture the ids this batch owns so a successful group acknowledges
        // exactly its own generation and never a newer opposite operation.
        let mut outbox_ids_by_account: BTreeMap<String, Vec<String>> = BTreeMap::new();
        for email in &planned {
            let id: String = conn.query_row(
                "SELECT id FROM pending_mutations \
                 WHERE account_id = ?1 AND email_uid = ?2 AND folder_path = ?3 AND mutation_type = 'mark_read' \
                 LIMIT 1",
                rusqlite::params![email.account_id, i64::from(email.uid), email.folder_path],
                |row| row.get(0),
            )?;
            outbox_ids_by_account
                .entry(email.account_id.clone())
                .or_default()
                .push(id);
        }

        // Group uids by account then folder path.
        let mut groups: BTreeMap<String, BTreeMap<String, BatchFolderGroup>> = BTreeMap::new();
        for email in &planned {
            groups
                .entry(email.account_id.clone())
                .or_default()
                .entry(email.folder_path.clone())
                .or_insert_with(|| BatchFolderGroup { uids: Vec::new(), epoch: email.epoch })
                .uids
                .push(email.uid);
        }

        transaction.commit()?;
        (groups, resolved_by_account, outbox_ids_by_account)
    };
    // The pool connection is released here — before any credential/network wait.

    // ---------- Phase B: network per account ----------
    let mut succeeded_accounts: Vec<String> = Vec::new();
    for (account_id, folders) in &groups {
        let Some(resolved) = resolved_by_account.get(account_id) else {
            continue;
        };
        let mut group_ok = true;
        match connect_imap(resolved) {
            Ok(mut client) => {
                'folders: for (folder_path, group) in folders {
                    if client.validate_uid_validity(folder_path, group.epoch).is_err() {
                        group_ok = false;
                        break;
                    }
                    for chunk in group.uids.chunks(MAIL_BATCH_STORE_CHUNK) {
                        let uid_set = chunk
                            .iter()
                            .map(u32::to_string)
                            .collect::<Vec<_>>()
                            .join(",");
                        if client.set_flags_bulk(folder_path, &uid_set, "\\Seen").is_err() {
                            group_ok = false;
                            break 'folders;
                        }
                    }
                }
                safe_logout(client);
            }
            Err(_) => {
                group_ok = false;
            }
        }
        if group_ok {
            succeeded_accounts.push(account_id.clone());
        }
    }

    // ---------- Phase C: short lease acknowledging only succeeded groups ----------
    if !succeeded_accounts.is_empty() {
        let conn = ctx
            .pool
            .get()
            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
        for account_id in &succeeded_accounts {
            if let Some(ids) = outbox_ids_by_account.get(account_id) {
                for id in ids {
                    maho_core::services::offline_queue::remove_mutation(&conn, id)?;
                }
            }
        }
    }

    Ok(json!({ "batched": true }))
}

#[no_mangle]
pub extern "C" fn MahoMailToggleStar(
    email_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let remote = get_email_imap_info(&conn, &email_id)?.map(|identity| {
                    resolve_account_auth(&conn, &ctx.credential_key, &identity.0)
                        .map(|auth| (identity, auth))
                }).transpose()?;
                let transaction = conn.unchecked_transaction()?;
                let conn = &transaction;
                maho_core::services::email::toggle_star(&conn, &email_id)?;

                let is_starred: bool = conn
                    .query_row(
                        "SELECT is_starred FROM emails WHERE id = ?1",
                        [&email_id],
                        |row| row.get(0),
                    )
                    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                if let Some(((account_id, folder_path, uid), resolved)) = remote {
                    if let Ok(mut client) = connect_email_imap(&conn, &resolved, &folder_path) {
                        let res = if is_starred {
                            client.set_flags(&folder_path, uid, "\\Flagged")
                        } else {
                            client.remove_flags(&folder_path, uid, "\\Flagged")
                        };
                        safe_logout(client);
                        if res.is_err() {
                            maho_core::services::offline_queue::queue_mutation(
                                &conn,
                                &account_id,
                                uid as i64,
                                &folder_path,
                                if is_starred { "star" } else { "unstar" },
                                None,
                            )?;
                        }
                    } else {
                        maho_core::services::offline_queue::queue_mutation(
                            &conn,
                            &account_id,
                            uid as i64,
                            &folder_path,
                            if is_starred { "star" } else { "unstar" },
                            None,
                        )?;
                    }
                }
                transaction.commit()?;
                Ok("{}".to_string())
            })
        })
    })
}

fn mutate_location(
    ctx: &crate::state::AppCtx,
    conn: &rusqlite::Connection,
    email_id: &str,
    target: Option<&str>,
) -> Result<()> {
    let tx = conn.unchecked_transaction()?;
    let remote = get_email_imap_info(&tx, email_id)?;
    let target_path = target.map(|id| tx.query_row("SELECT path FROM folders WHERE id=?1", [id], |r| r.get::<_, String>(0))).transpose()?;
    let kind = if target.is_some() { "move" } else { "delete" };
    let pending = if let Some((account, folder, uid)) = remote {
        maho_core::services::offline_queue::queue_mutation(&tx, &account, i64::from(uid), &folder, kind, target_path.as_deref())?;
        Some(maho_core::services::offline_queue::list_pending_mutations(&tx, &account)?.into_iter()
            .find(|row| row.folder_path.as_deref() == Some(&folder) && row.email_uid == Some(i64::from(uid)) && row.mutation_type == kind)
            .ok_or_else(|| MailFfiError::Internal("queued mutation missing".into()))?)
    } else { None };
    if let Some(target) = target {
        maho_core::services::email::move_email(&tx, email_id, target)?;
    } else {
        maho_core::services::email::delete_email(&tx, email_id)?;
    }
    tx.commit()?;
    if let Some(pending) = pending {
        if let Err(error) = mail_replay::deliver_and_ack(conn, &ctx.credential_key, &pending) {
            log::warn!("{kind} retained for retry ({}): {error}", pending.id);
        }
    }
    Ok(())
}

#[no_mangle]
pub extern "C" fn MahoMailDeleteEmail(
    email_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                mutate_location(&ctx, &conn, &email_id, None)?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailMoveEmail(
    email_id: *const c_char,
    target_folder_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        let Ok(target_folder_id) = non_empty(target_folder_id, "target_folder_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                mutate_location(&ctx, &conn, &email_id, Some(&target_folder_id))?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailBatchMarkUnread(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        #[derive(serde::Deserialize)]
        struct Req {
            email_ids: Vec<String>,
        }
        let Ok(req) = serde_json::from_str::<Req>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                for id in &req.email_ids {
                    maho_core::services::email::mark_unread(&conn, id)?;
                    if let Some((account_id, folder_path, uid)) = get_email_imap_info(&conn, id)? {
                        let resolved =
                            resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                        if let Ok(mut client) = connect_email_imap(&conn, &resolved, &folder_path) {
                            let _ = client.remove_flags(&folder_path, uid, "\\Seen");
                            safe_logout(client);
                        } else {
                            let _ = maho_core::services::offline_queue::queue_mutation(
                                &conn,
                                &account_id,
                                uid as i64,
                                &folder_path,
                                "mark_unread",
                                None,
                            );
                        }
                    }
                }
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailBatchDelete(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        #[derive(serde::Deserialize)]
        struct Req {
            email_ids: Vec<String>,
        }
        let Ok(req) = serde_json::from_str::<Req>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                for id in &req.email_ids { mutate_location(&ctx, &conn, id, None)?; }
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailBatchMove(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        #[derive(serde::Deserialize)]
        struct Req {
            email_ids: Vec<String>,
            target_folder_id: String,
        }
        let Ok(req) = serde_json::from_str::<Req>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                for id in &req.email_ids { mutate_location(&ctx, &conn, id, Some(&req.target_folder_id))?; }
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailBatchToggleStar(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        #[derive(serde::Deserialize)]
        struct Req {
            email_ids: Vec<String>,
        }
        let Ok(req) = serde_json::from_str::<Req>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                for id in &req.email_ids {
                    maho_core::services::email::toggle_star(&conn, id)?;
                    let is_starred: bool = conn
                        .query_row("SELECT is_starred FROM emails WHERE id = ?1", [id], |row| {
                            row.get(0)
                        })
                        .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                    if let Some((account_id, folder_path, uid)) = get_email_imap_info(&conn, id)? {
                        let resolved =
                            resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                        if let Ok(mut client) = connect_email_imap(&conn, &resolved, &folder_path) {
                            let _ = if is_starred {
                                client.set_flags(&folder_path, uid, "\\Flagged")
                            } else {
                                client.remove_flags(&folder_path, uid, "\\Flagged")
                            };
                            safe_logout(client);
                        } else {
                            let _ = maho_core::services::offline_queue::queue_mutation(
                                &conn,
                                &account_id,
                                uid as i64,
                                &folder_path,
                                if is_starred { "star" } else { "unstar" },
                                None,
                            );
                        }
                    }
                }
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailSyncFolders(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let count = crate::sync::sync_account_once(ctx, &account_id).await?;
                Ok(format!("{{\"new_messages\":{}}}", count))
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailSyncFolder(
    account_id: *const c_char,
    _folder_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    MahoMailSyncFolders(account_id, callback, user_data)
}

#[no_mangle]
pub extern "C" fn MahoMailCreateFolder(
    account_id: *const c_char,
    folder_name: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(folder_name) = non_empty(folder_name, "folder_name") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                let mut client = connect_imap(&resolved)?;
                client
                    .create_folder(&folder_name)
                    .map_err(|e| MailFfiError::Core(e))?;
                safe_logout(client);

                let id = uuid::Uuid::new_v4().to_string();
                let display_name = folder_name
                    .rsplit_once('/')
                    .or_else(|| folder_name.rsplit_once('.'))
                    .map(|(_, name)| name)
                    .unwrap_or(&folder_name);
                conn.execute(
                    "INSERT OR IGNORE INTO folders (id, account_id, name, path, folder_type, unread_count, total_count)
                     VALUES (?1, ?2, ?3, ?4, 'custom', 0, 0)",
                    rusqlite::params![id, account_id, display_name, folder_name],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailRenameFolder(
    account_id: *const c_char,
    folder_id: *const c_char,
    new_name: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(folder_id) = non_empty(folder_id, "folder_id") else {
            return false;
        };
        let Ok(new_name) = non_empty(new_name, "new_name") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let (old_path, folder_type): (String, String) = conn
                    .query_row(
                        "SELECT path, folder_type FROM folders WHERE id = ?1 AND account_id = ?2",
                        rusqlite::params![folder_id, account_id],
                        |row| Ok((row.get(0)?, row.get(1)?)),
                    )
                    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                if folder_type != "custom" {
                    return Err(MailFfiError::Core(maho_core::error::AppError::Validation(
                        "Cannot rename system folders".to_string(),
                    )));
                }

                let new_path = if let Some((parent, _)) = old_path.rsplit_once('/') {
                    format!("{}/{}", parent, new_name)
                } else if let Some((parent, _)) = old_path.rsplit_once('.') {
                    format!("{}.{}", parent, new_name)
                } else {
                    new_name.clone()
                };

                let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                let mut client = connect_imap(&resolved)?;
                client
                    .rename_folder(&old_path, &new_path)
                    .map_err(|e| MailFfiError::Core(e))?;
                safe_logout(client);

                conn.execute(
                    "UPDATE folders SET name = ?1, path = ?2 WHERE id = ?3 AND account_id = ?4",
                    rusqlite::params![new_name, new_path, folder_id, account_id],
                )?;
                conn.execute(
                    "UPDATE folders SET path = REPLACE(path, ?1, ?2) WHERE (path LIKE ?3 OR path LIKE ?4) AND account_id = ?5",
                    rusqlite::params![old_path, new_path, format!("{}/%", old_path), format!("{}.%", old_path), account_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDeleteFolder(
    account_id: *const c_char,
    folder_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(folder_id) = non_empty(folder_id, "folder_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let (folder_path, folder_type): (String, String) = conn
                    .query_row(
                        "SELECT path, folder_type FROM folders WHERE id = ?1 AND account_id = ?2",
                        rusqlite::params![folder_id, account_id],
                        |row| Ok((row.get(0)?, row.get(1)?)),
                    )
                    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                if folder_type != "custom" {
                    return Err(MailFfiError::Core(maho_core::error::AppError::Validation(
                        "Cannot delete system folders".to_string(),
                    )));
                }

                let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                let mut client = connect_imap(&resolved)?;
                client
                    .delete_folder(&folder_path)
                    .map_err(|e| MailFfiError::Core(e))?;
                safe_logout(client);

                conn.execute(
                    "DELETE FROM folders WHERE id = ?1 AND account_id = ?2",
                    rusqlite::params![folder_id, account_id],
                )?;
                conn.execute(
                    "DELETE FROM folders WHERE (path LIKE ?1 OR path LIKE ?2) AND account_id = ?3",
                    rusqlite::params![
                        format!("{}/%", folder_path),
                        format!("{}.%", folder_path),
                        account_id
                    ],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailGetFolderCounts(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let counts = maho_core::services::folder::get_folder_counts(&conn, &account_id)?;
                Ok(counts)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailFlushPendingMutations(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let mail = mail_replay::flush(&ctx, &account_id).await;
                // Attempt both domains even if one is offline; neither can starve the other.
                let calendar = crate::ffi::calendar_api::calendar_replay::flush(&ctx, &account_id).await;
                Ok((mail? + calendar?).to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailGetPendingMutationCount(
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let count = maho_core::services::offline_queue::count_pending_mutations(&conn)?;
                Ok(count)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListPendingMutations(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let mutations =
                    maho_core::services::offline_queue::list_pending_mutations(&conn, &account_id)?;
                Ok(mutations)
            })
        })
    })
}
