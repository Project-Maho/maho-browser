// Copyright 2026 Maho Browser. All rights reserved.

use std::ffi::c_void;
use std::os::raw::c_char;

use crate::error::{MailFfiError, Result};
use crate::account::{resolve_account_auth, connect_imap, safe_logout};

use super::read_api::{accept_read, accept_read_call, blocking_json, MahoMailReadCallback};
use super::{c_string, non_empty};

fn get_email_imap_info(conn: &rusqlite::Connection, email_id: &str) -> Result<Option<(String, String, u32)>> {
    let result = conn.query_row(
        "SELECT e.account_id, f.path, e.uid FROM emails e JOIN folders f ON e.folder_id = f.id WHERE e.id = ?1",
        [email_id],
        |row| {
            let account_id: String = row.get(0)?;
            let folder_path: String = row.get(1)?;
            let uid: Option<i64> = row.get(2)?;
            Ok((account_id, folder_path, uid))
        },
    ).map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

    match result.2 {
        Some(uid) if uid > 0 => Ok(Some((result.0, result.1, uid as u32))),
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
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                maho_core::services::email::mark_read(&conn, &email_id)?;
                
                if let Some((account_id, folder_path, uid)) = get_email_imap_info(&conn, &email_id)? {
                    let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                    if let Ok(mut client) = connect_imap(&resolved) {
                        let res = client.set_flags(&folder_path, uid, "\\Seen");
                        safe_logout(client);
                        if res.is_err() {
                            let _ = maho_core::services::offline_queue::queue_mutation(
                                &conn,
                                &account_id,
                                uid as i64,
                                &folder_path,
                                "mark_read",
                                None,
                            );
                        }
                    } else {
                        let _ = maho_core::services::offline_queue::queue_mutation(
                            &conn,
                            &account_id,
                            uid as i64,
                            &folder_path,
                            "mark_read",
                            None,
                        );
                    }
                }
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
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                maho_core::services::email::mark_unread(&conn, &email_id)?;
                
                if let Some((account_id, folder_path, uid)) = get_email_imap_info(&conn, &email_id)? {
                    let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                    if let Ok(mut client) = connect_imap(&resolved) {
                        let res = client.remove_flags(&folder_path, uid, "\\Seen");
                        safe_logout(client);
                        if res.is_err() {
                            let _ = maho_core::services::offline_queue::queue_mutation(
                                &conn,
                                &account_id,
                                uid as i64,
                                &folder_path,
                                "mark_unread",
                                None,
                            );
                        }
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
                Ok("{}".to_string())
            })
        })
    })
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
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                maho_core::services::email::toggle_star(&conn, &email_id)?;
                
                let is_starred: bool = conn.query_row(
                    "SELECT is_starred FROM emails WHERE id = ?1",
                    [&email_id],
                    |row| row.get(0)
                ).map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                if let Some((account_id, folder_path, uid)) = get_email_imap_info(&conn, &email_id)? {
                    let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                    if let Ok(mut client) = connect_imap(&resolved) {
                        let res = if is_starred {
                            client.set_flags(&folder_path, uid, "\\Flagged")
                        } else {
                            client.remove_flags(&folder_path, uid, "\\Flagged")
                        };
                        safe_logout(client);
                        if res.is_err() {
                            let _ = maho_core::services::offline_queue::queue_mutation(
                                &conn,
                                &account_id,
                                uid as i64,
                                &folder_path,
                                if is_starred { "star" } else { "unstar" },
                                None,
                            );
                        }
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
                Ok("{}".to_string())
            })
        })
    })
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
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let imap_info = get_email_imap_info(&conn, &email_id)?;
                maho_core::services::email::delete_email(&conn, &email_id)?;
                
                if let Some((account_id, folder_path, uid)) = imap_info {
                    let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                    if let Ok(mut client) = connect_imap(&resolved) {
                        let _ = client.set_flags(&folder_path, uid, "\\Deleted");
                        let _ = client.expunge();
                        safe_logout(client);
                    } else {
                        let _ = maho_core::services::offline_queue::queue_mutation(
                            &conn,
                            &account_id,
                            uid as i64,
                            &folder_path,
                            "delete",
                            None,
                        );
                    }
                }
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
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let target_folder_path: String = conn.query_row(
                    "SELECT path FROM folders WHERE id = ?1",
                    [&target_folder_id],
                    |row| row.get(0)
                ).map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                let imap_info = get_email_imap_info(&conn, &email_id)?;
                maho_core::services::email::move_email(&conn, &email_id, &target_folder_id)?;
                
                if let Some((account_id, folder_path, uid)) = imap_info {
                    let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                    if let Ok(mut client) = connect_imap(&resolved) {
                        let _ = client.move_email(&folder_path, uid, &target_folder_path);
                        safe_logout(client);
                    } else {
                        let _ = maho_core::services::offline_queue::queue_mutation(
                            &conn,
                            &account_id,
                            uid as i64,
                            &folder_path,
                            "move",
                            Some(&target_folder_path),
                        );
                    }
                }
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailBatchMarkRead(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        #[derive(serde::Deserialize)]
        struct Req { email_ids: Vec<String> }
        let Ok(req) = serde_json::from_str::<Req>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                for id in &req.email_ids {
                    maho_core::services::email::mark_read(&conn, id)?;
                    if let Some((account_id, folder_path, uid)) = get_email_imap_info(&conn, id)? {
                        let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                        if let Ok(mut client) = connect_imap(&resolved) {
                            let _ = client.set_flags(&folder_path, uid, "\\Seen");
                            safe_logout(client);
                        } else {
                            let _ = maho_core::services::offline_queue::queue_mutation(
                                &conn,
                                &account_id,
                                uid as i64,
                                &folder_path,
                                "mark_read",
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
        struct Req { email_ids: Vec<String> }
        let Ok(req) = serde_json::from_str::<Req>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                for id in &req.email_ids {
                    maho_core::services::email::mark_unread(&conn, id)?;
                    if let Some((account_id, folder_path, uid)) = get_email_imap_info(&conn, id)? {
                        let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                        if let Ok(mut client) = connect_imap(&resolved) {
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
        struct Req { email_ids: Vec<String> }
        let Ok(req) = serde_json::from_str::<Req>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                for id in &req.email_ids {
                    let imap_info = get_email_imap_info(&conn, id)?;
                    maho_core::services::email::delete_email(&conn, id)?;
                    if let Some((account_id, folder_path, uid)) = imap_info {
                        let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                        if let Ok(mut client) = connect_imap(&resolved) {
                            let _ = client.set_flags(&folder_path, uid, "\\Deleted");
                            let _ = client.expunge();
                            safe_logout(client);
                        } else {
                            let _ = maho_core::services::offline_queue::queue_mutation(
                                &conn,
                                &account_id,
                                uid as i64,
                                &folder_path,
                                "delete",
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
        struct Req { email_ids: Vec<String>, target_folder_id: String }
        let Ok(req) = serde_json::from_str::<Req>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let target_folder_path: String = conn.query_row(
                    "SELECT path FROM folders WHERE id = ?1",
                    [&req.target_folder_id],
                    |row| row.get(0)
                ).map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                for id in &req.email_ids {
                    let imap_info = get_email_imap_info(&conn, id)?;
                    maho_core::services::email::move_email(&conn, id, &req.target_folder_id)?;
                    if let Some((account_id, folder_path, uid)) = imap_info {
                        let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                        if let Ok(mut client) = connect_imap(&resolved) {
                            let _ = client.move_email(&folder_path, uid, &target_folder_path);
                            safe_logout(client);
                        } else {
                            let _ = maho_core::services::offline_queue::queue_mutation(
                                &conn,
                                &account_id,
                                uid as i64,
                                &folder_path,
                                "move",
                                Some(&target_folder_path),
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
        struct Req { email_ids: Vec<String> }
        let Ok(req) = serde_json::from_str::<Req>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                for id in &req.email_ids {
                    maho_core::services::email::toggle_star(&conn, id)?;
                    let is_starred: bool = conn.query_row(
                        "SELECT is_starred FROM emails WHERE id = ?1",
                        [id],
                        |row| row.get(0)
                    ).map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                    if let Some((account_id, folder_path, uid)) = get_email_imap_info(&conn, id)? {
                        let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                        if let Ok(mut client) = connect_imap(&resolved) {
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
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                let mut client = connect_imap(&resolved)?;
                client.create_folder(&folder_name).map_err(|e| MailFfiError::Core(e))?;
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
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let (old_path, folder_type): (String, String) = conn.query_row(
                    "SELECT path, folder_type FROM folders WHERE id = ?1 AND account_id = ?2",
                    rusqlite::params![folder_id, account_id],
                    |row| Ok((row.get(0)?, row.get(1)?))
                ).map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                if folder_type != "custom" {
                    return Err(MailFfiError::Core(maho_core::error::AppError::Validation(
                        "Cannot rename system folders".to_string()
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
                client.rename_folder(&old_path, &new_path).map_err(|e| MailFfiError::Core(e))?;
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
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let (folder_path, folder_type): (String, String) = conn.query_row(
                    "SELECT path, folder_type FROM folders WHERE id = ?1 AND account_id = ?2",
                    rusqlite::params![folder_id, account_id],
                    |row| Ok((row.get(0)?, row.get(1)?))
                ).map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                if folder_type != "custom" {
                    return Err(MailFfiError::Core(maho_core::error::AppError::Validation(
                        "Cannot delete system folders".to_string()
                    )));
                }

                let resolved = resolve_account_auth(&conn, &ctx.credential_key, &account_id)?;
                let mut client = connect_imap(&resolved)?;
                client.delete_folder(&folder_path).map_err(|e| MailFfiError::Core(e))?;
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
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
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
        let Ok(_account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |_ctx| {
            Box::pin(async move {
                Ok("0".to_string())
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
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
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
                let conn = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let mutations = maho_core::services::offline_queue::list_pending_mutations(&conn, &account_id)?;
                Ok(mutations)
            })
        })
    })
}
