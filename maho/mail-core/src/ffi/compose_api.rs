// Copyright 2026 Maho Browser. All rights reserved.

use base64::Engine;
use std::ffi::c_void;
use std::os::raw::c_char;

use serde::{Deserialize, Serialize};
use std::path::PathBuf;

use super::read_api::{accept_read, accept_read_call, blocking_json, MahoMailReadCallback};
use super::{c_string, non_empty};
use crate::account::{resolve_account_auth, ImapAuth};
use crate::error::{MailFfiError, Result};

fn attachment_cache_dir(ctx: &crate::state::AppCtx) -> Result<PathBuf> {
    let mail_root = ctx.db_path.parent().ok_or_else(|| {
        MailFfiError::Internal("mail database has no profile-local parent".to_string())
    })?;
    Ok(mail_root.join("AttachmentCache"))
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ComposeEmailRequest {
    pub account_id: String,
    pub to: Vec<String>,
    pub cc: Option<Vec<String>>,
    pub bcc: Option<Vec<String>>,
    pub subject: String,
    pub body_text: Option<String>,
    pub body_html: Option<String>,
    pub read_receipt: Option<bool>,
    pub attachments: Option<Vec<maho_core::smtp_client::ComposeAttachment>>,
    pub in_reply_to: Option<String>,
    pub references: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReplyContext {
    pub original_subject: String,
    pub original_from: String,
    pub original_date: String,
    pub original_body_text: Option<String>,
    pub original_body_html: Option<String>,
    pub original_to_addresses: Option<String>,
    pub original_cc_addresses: Option<String>,
    pub original_message_id: Option<String>,
    pub original_references: Option<String>,
}

#[derive(Clone)]
pub(crate) struct SmtpCredentials {
    pub(crate) email: String,
    pub(crate) smtp_host: String,
    pub(crate) smtp_port: u16,
    pub(crate) smtp_encryption: maho_core::models::account::Encryption,
    pub(crate) username: String,
    pub(crate) auth: ImapAuth,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OutboxItem {
    pub id: String,
    pub account_id: String,
    pub to_addresses: String,
    pub cc_addresses: Option<String>,
    pub bcc_addresses: Option<String>,
    pub subject: String,
    pub body_html: Option<String>,
    pub body_text: Option<String>,
    pub status: String,
    pub retry_count: i64,
    pub max_retries: i64,
    pub last_error: Option<String>,
    pub created_at: String,
    pub updated_at: String,
}

fn map_outbox_item(row: &rusqlite::Row) -> rusqlite::Result<OutboxItem> {
    Ok(OutboxItem {
        id: row.get("id")?,
        account_id: row.get("account_id")?,
        to_addresses: row.get("to_addresses")?,
        cc_addresses: row.get("cc_addresses")?,
        bcc_addresses: row.get("bcc_addresses")?,
        subject: row.get("subject")?,
        body_html: row.get("body_html")?,
        body_text: row.get("body_text")?,
        status: row.get("status")?,
        retry_count: row.get("retry_count")?,
        max_retries: row.get("max_retries")?,
        last_error: row.get("last_error")?,
        created_at: row.get("created_at")?,
        updated_at: row.get("updated_at")?,
    })
}

pub(crate) fn get_smtp_credentials(
    conn: &rusqlite::Connection,
    credential_key: &[u8; 32],
    account_id: &str,
) -> Result<SmtpCredentials> {
    let (email, smtp_host, smtp_port, smtp_enc_str, username): (
        String,
        String,
        u16,
        String,
        String,
    ) = conn
        .query_row(
            "SELECT email, smtp_host, smtp_port, smtp_encryption, username FROM accounts WHERE id = ?1",
            [account_id],
            |row| {
                let port: i64 = row.get("smtp_port")?;
                Ok((
                    row.get::<_, String>("email")?,
                    row.get::<_, String>("smtp_host")?,
                    port as u16,
                    row.get::<_, String>("smtp_encryption")?,
                    row.get::<_, String>("username")?,
                ))
            },
        )
        .map_err(|_| MailFfiError::AccountNotFound(account_id.to_string()))?;

    let auth = resolve_account_auth(conn, credential_key, account_id)?;

    Ok(SmtpCredentials {
        email,
        smtp_host,
        smtp_port,
        smtp_encryption: maho_core::models::account::Encryption::from_str_lossy(&smtp_enc_str),
        username,
        auth: auth.auth,
    })
}

pub(crate) async fn smtp_credentials_async(
    ctx: &crate::state::AppCtx,
    account_id: &str,
) -> Result<SmtpCredentials> {
    let pool = ctx.pool.clone();
    let key = ctx.credential_key;
    let account_id = account_id.to_owned();
    tokio::task::spawn_blocking(move || {
        let conn = pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
        get_smtp_credentials(&conn, &key, &account_id)
    }).await.map_err(|e| MailFfiError::Internal(format!("SMTP credential task: {e}")))?
}

#[allow(clippy::too_many_arguments)]
pub(crate) async fn send_smtp_message(
    creds: SmtpCredentials,
    use_xoauth2: bool,
    from: String,
    to: Vec<String>,
    cc: Vec<String>,
    bcc: Vec<String>,
    subject: String,
    body_text: Option<String>,
    body_html: Option<String>,
    in_reply_to: Option<String>,
    references: Option<String>,
    read_receipt_to: Option<String>,
    attachments: Vec<maho_core::smtp_client::ComposeAttachment>,
    calendar_alternative: Option<maho_core::smtp_client::CalendarAlternativePart>,
) -> Result<()> {
    tokio::task::spawn_blocking(move || {
        let secret = match &creds.auth {
            ImapAuth::Password(password) => password.as_str(),
            ImapAuth::OAuth2 { access_token } if use_xoauth2 => access_token.as_str(),
            ImapAuth::OAuth2 { .. } => "",
        };
        maho_core::smtp_client::send_email(
            &creds.smtp_host,
            creds.smtp_port,
            &creds.smtp_encryption,
            &creds.username,
            secret,
            use_xoauth2,
            &from,
            &to,
            &cc,
            &bcc,
            &subject,
            body_text.as_deref(),
            body_html.as_deref(),
            in_reply_to.as_deref(),
            references.as_deref(),
            read_receipt_to.as_deref(),
            &attachments,
            calendar_alternative.as_ref(),
        )
    })
    .await
    .map_err(|e| MailFfiError::Internal(e.to_string()))?
    .map_err(MailFfiError::Core)
}

#[allow(clippy::too_many_arguments)]
pub(crate) fn insert_outbox_item(
    db: &rusqlite::Connection,
    account_id: &str,
    to_json: &str,
    cc_json: Option<&str>,
    bcc_json: Option<&str>,
    subject: &str,
    body_html: Option<&str>,
    body_text: Option<&str>,
    in_reply_to: Option<&str>,
    references: Option<&str>,
    attachments_json: Option<&str>,
) -> Result<OutboxItem> {
    let id = uuid::Uuid::new_v4().to_string();
    db.execute(
        "INSERT INTO outbox (id, account_id, to_addresses, cc_addresses, bcc_addresses, subject, body_html, body_text, in_reply_to, email_references, attachments_json)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)",
        rusqlite::params![id, account_id, to_json, cc_json, bcc_json, subject, body_html, body_text, in_reply_to, references, attachments_json],
    )?;

    let item = db.query_row(
        "SELECT id, account_id, to_addresses, cc_addresses, bcc_addresses, subject, body_html, body_text, status, retry_count, max_retries, last_error, created_at, updated_at
         FROM outbox WHERE id = ?1",
        rusqlite::params![id],
        map_outbox_item,
    )?;

    Ok(item)
}

fn queue_to_outbox(conn: &rusqlite::Connection, request: &ComposeEmailRequest) -> Result<OutboxItem> {
    let to_json = serde_json::to_string(&request.to)?;
    let cc_json = request
        .cc
        .as_ref()
        .map(|v| serde_json::to_string(v).unwrap_or_default());
    let bcc_json = request
        .bcc
        .as_ref()
        .map(|v| serde_json::to_string(v).unwrap_or_default());
    let attachments_json = request
        .attachments
        .as_ref()
        .map(|a| serde_json::to_string(a).unwrap_or_default());

    insert_outbox_item(
        conn,
        &request.account_id,
        &to_json,
        cc_json.as_deref(),
        bcc_json.as_deref(),
        &request.subject,
        request.body_html.as_deref(),
        request.body_text.as_deref(),
        request.in_reply_to.as_deref(),
        request.references.as_deref(),
        attachments_json.as_deref(),
    )
}

fn queue_uncertain_delivery(
    conn: &rusqlite::Connection,
    request: &ComposeEmailRequest,
    error: &str,
) -> Result<String> {
    let transaction = conn.unchecked_transaction()?;
    let item = queue_to_outbox(&transaction, request)?;
    transaction.execute(
        "UPDATE outbox SET status='uncertain', last_error=?1 WHERE id=?2",
        rusqlite::params![error, item.id],
    )?;
    transaction.commit()?;
    Ok(serde_json::json!({"status": "uncertain", "id": item.id}).to_string())
}

fn is_network_error(err: &MailFfiError) -> bool {
    match err {
        MailFfiError::Core(maho_core::error::AppError::Network(_)) => true,
        _ => false,
    }
}

fn is_oauth_auth_error(err: &MailFfiError) -> bool {
    let message = match err {
        MailFfiError::Core(maho_core::error::AppError::Network(msg)) => msg.to_lowercase(),
        MailFfiError::Core(maho_core::error::AppError::Auth(msg)) => msg.to_lowercase(),
        _ => return false,
    };

    message.contains("535")
        || message.contains("authentication")
        || message.contains("auth")
        || message.contains("invalid credentials")
        || message.contains("login")
}

fn is_auth_error(err: &MailFfiError) -> bool {
    let message = match err {
        MailFfiError::Core(maho_core::error::AppError::Network(msg)) => msg.to_lowercase(),
        MailFfiError::Core(maho_core::error::AppError::Auth(msg)) => msg.to_lowercase(),
        _ => return false,
    };
    message.contains("535")
        || message.contains("authentication")
        || message.contains("auth")
        || message.contains("invalid credentials")
        || message.contains("login")
}

pub(crate) fn build_references_chain(
    existing_refs: Option<&str>,
    parent_msg_id: Option<&str>,
) -> Option<String> {
    let mut ids: Vec<&str> = Vec::new();

    if let Some(refs) = existing_refs {
        for id in refs.split_whitespace() {
            if !id.is_empty() && !ids.contains(&id) {
                ids.push(id);
            }
        }
    }

    if let Some(msg_id) = parent_msg_id {
        if !msg_id.is_empty() && !ids.contains(&msg_id) {
            ids.push(msg_id);
        }
    }

    if ids.is_empty() {
        return None;
    }

    let start = if ids.len() > 20 { ids.len() - 20 } else { 0 };
    Some(ids[start..].join(" "))
}

static FLUSHING_ACCOUNTS: std::sync::Mutex<Option<std::collections::HashSet<String>>> =
    std::sync::Mutex::new(None);

fn try_start_flush(account_id: &str) -> bool {
    let mut guard = match FLUSHING_ACCOUNTS.lock() {
        Ok(g) => g,
        Err(p) => p.into_inner(),
    };
    let set = guard.get_or_insert_with(std::collections::HashSet::new);
    if set.contains(account_id) {
        false
    } else {
        set.insert(account_id.to_string());
        true
    }
}

fn stop_flush(account_id: &str) {
    let mut guard = match FLUSHING_ACCOUNTS.lock() {
        Ok(g) => g,
        Err(p) => p.into_inner(),
    };
    if let Some(set) = guard.as_mut() {
        set.remove(account_id);
    }
}

struct AccountFlushGuard(String);

/// Recover only work for which this process has no live sender. Never replay an
/// interrupted SMTP transaction: the server may already have accepted DATA.
pub(crate) fn recover_interrupted_outbox(conn: &rusqlite::Connection) -> Result<()> {
    let active = FLUSHING_ACCOUNTS.lock().unwrap_or_else(std::sync::PoisonError::into_inner);
    let mut stmt = conn.prepare("SELECT DISTINCT account_id FROM outbox WHERE status = 'sending'")?;
    let accounts = stmt.query_map([], |row| row.get::<_, String>(0))?
        .collect::<rusqlite::Result<Vec<_>>>()?;
    for account in accounts {
        if active.as_ref().is_some_and(|set| set.contains(&account)) {
            continue;
        }
        conn.execute(
            "UPDATE outbox SET status = 'uncertain', last_error = 'Delivery interrupted; check Sent before resending', updated_at = datetime('now') WHERE account_id = ?1 AND status = 'sending'",
            [&account],
        )?;
    }
    Ok(())
}

impl Drop for AccountFlushGuard {
    fn drop(&mut self) {
        stop_flush(&self.0);
    }
}

const DEFAULT_CACHE_LIMIT_BYTES: u64 = 100 * 1024 * 1024;

fn enforce_cache_limit(cache_dir: &std::path::Path, max_bytes: u64) {
    let entries = match std::fs::read_dir(cache_dir) {
        Ok(entries) => entries,
        Err(_) => return,
    };

    let mut files: Vec<(std::path::PathBuf, u64, std::time::SystemTime)> = Vec::new();
    let mut total_size: u64 = 0;

    for entry in entries {
        let entry = match entry {
            Ok(e) => e,
            Err(_) => continue,
        };

        let metadata = match entry.metadata() {
            Ok(m) => m,
            Err(_) => continue,
        };

        if !metadata.is_file() {
            continue;
        }

        let size = metadata.len();
        let modified = metadata.modified().unwrap_or(std::time::UNIX_EPOCH);

        files.push((entry.path(), size, modified));
        total_size += size;
    }

    if total_size <= max_bytes {
        return;
    }

    files.sort_by(|a, b| a.2.cmp(&b.2));

    for (path, size, _) in files {
        if total_size <= max_bytes {
            break;
        }

        if std::fs::remove_file(&path).is_ok() {
            total_size = total_size.saturating_sub(size);
        }
    }
}

#[no_mangle]
pub extern "C" fn MahoMailSendEmail(
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
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                // The connection guard is `!Send`, so every database step below
                // takes it for the duration of that step only and never holds
                // it across the SMTP round trips.
                let creds = smtp_credentials_async(&ctx, &request.account_id).await?;

                let to = request.to.clone();
                let cc = request.cc.clone().unwrap_or_default();
                let bcc = request.bcc.clone().unwrap_or_default();
                let subject = request.subject.clone();
                let body_text = request.body_text.clone();
                let body_html = request.body_html.clone();
                let from = creds.email.clone();
                let use_xoauth2 = match &creds.auth {
                    ImapAuth::OAuth2 { .. } => true,
                    _ => false,
                };
                let attachments = request.attachments.clone().unwrap_or_default();
                let read_receipt_to = if request.read_receipt.unwrap_or(false) {
                    Some(creds.email.clone())
                } else {
                    None
                };

                match send_smtp_message(
                    creds.clone(),
                    use_xoauth2,
                    from.clone(),
                    to.clone(),
                    cc.clone(),
                    bcc.clone(),
                    subject.clone(),
                    body_text.clone(),
                    body_html.clone(),
                    request.in_reply_to.clone(),
                    request.references.clone(),
                    read_receipt_to.clone(),
                    attachments.clone(),
                    None,
                )
                .await
                {
                    Ok(()) => Ok(serde_json::json!({"status": "sent"}).to_string()),
                    Err(err) if use_xoauth2 && is_oauth_auth_error(&err) => {
                        let _ = ctx.with_db(|conn| {
                            Ok(conn.execute(
                                "UPDATE accounts SET oauth2_expires_at = '1970-01-01T00:00:00Z' WHERE id = ?1",
                                [&request.account_id],
                            ))
                        });
                        if let Ok(refreshed_creds) =
                            smtp_credentials_async(&ctx, &request.account_id).await
                        {
                            match send_smtp_message(
                                refreshed_creds,
                                true,
                                from,
                                to,
                                cc,
                                bcc,
                                subject,
                                body_text,
                                body_html,
                                request.in_reply_to.clone(),
                                request.references.clone(),
                                read_receipt_to,
                                attachments,
                                None,
                            )
                            .await
                            {
                                Ok(()) => Ok(serde_json::json!({"status": "sent"}).to_string()),
                                Err(MailFfiError::Core(maho_core::error::AppError::DeliveryUncertain(error))) => {
                                    ctx.with_db(|conn| queue_uncertain_delivery(conn, &request, &error))
                                }
                                Err(retry_err) if is_network_error(&retry_err) => {
                                    ctx.with_db(|conn| queue_to_outbox(conn, &request))?;
                                    Ok(serde_json::json!({"status": "queued"}).to_string())
                                }
                                Err(retry_err) => Err(retry_err),
                            }
                        } else {
                            Err(err)
                        }
                    }
                    Err(MailFfiError::Core(maho_core::error::AppError::DeliveryUncertain(error))) => {
                        ctx.with_db(|conn| queue_uncertain_delivery(conn, &request, &error))
                    }
                    Err(err) if is_network_error(&err) => {
                        ctx.with_db(|conn| queue_to_outbox(conn, &request))?;
                        Ok(serde_json::json!({"status": "queued"}).to_string())
                    }
                    Err(err) => Err(err),
                }
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailSaveDraft(
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
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let drafts_folder_id: String = conn
                    .query_row(
                        "SELECT id FROM folders WHERE account_id = ?1 AND folder_type = 'drafts' LIMIT 1",
                        [&request.account_id],
                        |row| row.get(0),
                    )
                    .map_err(|_| {
                        MailFfiError::Core(maho_core::error::AppError::NotFound("Drafts folder not found. Please sync folders first.".to_string()))
                    })?;

                let id = uuid::Uuid::new_v4().to_string();
                let now = chrono::Utc::now().format("%Y-%m-%d %H:%M:%S").to_string();
                let to_json =
                    serde_json::to_string(&request.to).unwrap_or_else(|_| "[]".to_string());
                let cc_json = request
                    .cc
                    .as_ref()
                    .map(|v| serde_json::to_string(v).unwrap_or_else(|_| "[]".to_string()));

                let has_attachments: i32 =
                    if request.attachments.as_ref().is_some_and(|a| !a.is_empty()) {
                        1
                    } else {
                        0
                    };
                let draft_attachments_json: Option<String> = request
                    .attachments
                    .as_ref()
                    .filter(|a| !a.is_empty())
                    .and_then(|a| serde_json::to_string(a).ok());

                conn.execute(
                    "INSERT INTO emails
                        (id, account_id, folder_id, message_id, subject, from_address,
                         to_addresses, cc_addresses, date, snippet, is_read, is_starred,
                         is_draft, has_attachments, draft_attachments_json, body_text, body_html, raw_size, created_at,
                         bcc_addresses, in_reply_to, email_references, read_receipt, body_fetched_at)
                     VALUES (?1, ?2, ?3, '', ?4, '', ?5, ?6, ?7, ?8, 1, 0, 1, ?9, ?10, ?11, ?12, 0, ?13, ?14, ?15, ?16, ?17, ?13)",
                    rusqlite::params![
                        id,
                        request.account_id,
                        drafts_folder_id,
                        request.subject,
                        to_json,
                        cc_json,
                        now,
                        request.subject.chars().take(100).collect::<String>(),
                        has_attachments,
                        draft_attachments_json,
                        request.body_text,
                        request.body_html,
                        now,
                        request.bcc.as_ref().map(serde_json::to_string).transpose()?,
                        request.in_reply_to,
                        request.references,
                        request.read_receipt,
                    ],
                )?;
                Ok(id)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailUpdateDraft(
    draft_id: *const c_char,
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(draft_id) = non_empty(draft_id, "draft_id") else {
            return false;
        };
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(request) = serde_json::from_str::<ComposeEmailRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let to_json =
                    serde_json::to_string(&request.to).unwrap_or_else(|_| "[]".to_string());
                let cc_json = request
                    .cc
                    .as_ref()
                    .map(|v| serde_json::to_string(v).unwrap_or_else(|_| "[]".to_string()));
                let now = chrono::Utc::now().format("%Y-%m-%d %H:%M:%S").to_string();
                let has_attachments: i32 =
                    if request.attachments.as_ref().is_some_and(|a| !a.is_empty()) {
                        1
                    } else {
                        0
                    };
                let draft_attachments_json: Option<String> = request
                    .attachments
                    .as_ref()
                    .filter(|a| !a.is_empty())
                    .and_then(|a| serde_json::to_string(a).ok());

                conn.execute(
                    "UPDATE emails SET subject = ?1, to_addresses = ?2, cc_addresses = ?3,
                     body_text = ?4, body_html = ?5, snippet = ?6, date = ?7,
                     has_attachments = ?8, draft_attachments_json = ?9,
                     bcc_addresses = ?12, in_reply_to = ?13, email_references = ?14,
                     read_receipt = ?15, body_fetched_at = ?7
                     WHERE id = ?10 AND is_draft = 1 AND account_id = ?11",
                    rusqlite::params![
                        request.subject,
                        to_json,
                        cc_json,
                        request.body_text,
                        request.body_html,
                        request.subject.chars().take(100).collect::<String>(),
                        now,
                        has_attachments,
                        draft_attachments_json,
                        draft_id,
                        request.account_id,
                        request.bcc.as_ref().map(serde_json::to_string).transpose()?,
                        request.in_reply_to,
                        request.references,
                        request.read_receipt,
                    ],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailGetReplyContext(
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
                conn.query_row(
                    "SELECT subject, from_address, date, body_text, body_html, to_addresses, cc_addresses, message_id, in_reply_to FROM emails WHERE id = ?1",
                    [&email_id],
                    |row| {
                        let message_id_opt: Option<String> = row.get("message_id")?;
                        let in_reply_to_opt: Option<String> = row.get("in_reply_to")?;
                        let original_references = build_references_chain(in_reply_to_opt.as_deref(), message_id_opt.as_deref());
                        Ok(ReplyContext {
                            original_subject: row.get("subject")?,
                            original_from: row.get("from_address")?,
                            original_date: row.get("date")?,
                            original_body_text: row.get("body_text")?,
                            original_body_html: row.get("body_html")?,
                            original_to_addresses: row.get("to_addresses")?,
                            original_cc_addresses: row.get("cc_addresses")?,
                            original_message_id: message_id_opt,
                            original_references,
                        })
                    },
                )
                .map_err(|_| MailFfiError::Core(maho_core::error::AppError::NotFound(format!("Email {} not found", email_id))))
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailQueueEmail(
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
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let to_json = serde_json::to_string(&request.to)?;
                let cc_json = request
                    .cc
                    .as_ref()
                    .map(|v| serde_json::to_string(v).unwrap_or_default());
                let bcc_json = request
                    .bcc
                    .as_ref()
                    .map(|v| serde_json::to_string(v).unwrap_or_default());
                let attachments_json = request
                    .attachments
                    .as_ref()
                    .map(|a| serde_json::to_string(a).unwrap_or_default());

                let item = insert_outbox_item(
                    &conn,
                    &request.account_id,
                    &to_json,
                    cc_json.as_deref(),
                    bcc_json.as_deref(),
                    &request.subject,
                    request.body_html.as_deref(),
                    request.body_text.as_deref(),
                    request.in_reply_to.as_deref(),
                    request.references.as_deref(),
                    attachments_json.as_deref(),
                )?;
                Ok(item)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListOutbox(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let account_id_opt = if account_id.is_null() {
            None
        } else {
            match c_string(account_id, "account_id") {
                Ok(s) => {
                    if s.trim().is_empty() {
                        None
                    } else {
                        Some(s)
                    }
                }
                Err(_) => return false,
            }
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                if let Some(ref aid) = account_id_opt {
                    let mut s = conn.prepare(
                        "SELECT id, account_id, to_addresses, cc_addresses, bcc_addresses, subject, body_html, body_text, status, retry_count, max_retries, last_error, created_at, updated_at
                         FROM outbox WHERE account_id = ?1 AND status IN ('queued', 'failed', 'sending', 'uncertain') ORDER BY created_at ASC",
                    )?;
                    let rows = s
                        .query_map(rusqlite::params![aid], map_outbox_item)?
                        .collect::<std::result::Result<Vec<_>, _>>()?;
                    Ok(rows)
                } else {
                    let mut s = conn.prepare(
                        "SELECT id, account_id, to_addresses, cc_addresses, bcc_addresses, subject, body_html, body_text, status, retry_count, max_retries, last_error, created_at, updated_at
                         FROM outbox WHERE status IN ('queued', 'failed', 'sending', 'uncertain') ORDER BY created_at ASC",
                    )?;
                    let rows = s
                        .query_map([], map_outbox_item)?
                        .collect::<std::result::Result<Vec<_>, _>>()?;
                    Ok(rows)
                }
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailRetryOutboxItem(
    item_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(item_id) = non_empty(item_id, "item_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "UPDATE outbox SET status = 'queued', updated_at = datetime('now') WHERE id = ?1 AND status = 'failed'",
                    rusqlite::params![item_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDeleteOutboxItem(
    item_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(item_id) = non_empty(item_id, "item_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "DELETE FROM outbox WHERE id = ?1",
                    rusqlite::params![item_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailFlushOutbox(
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                // The connection guard is `!Send`: each database step takes it
                // for that step only, so it is never held across an SMTP send.
                ctx.with_db(recover_interrupted_outbox)?;
                let pending = ctx.with_db(|conn| {
                    let mut stmt = conn.prepare(
                        "SELECT id, account_id, to_addresses, cc_addresses, bcc_addresses, subject, body_html, body_text, in_reply_to, email_references, attachments_json
                         FROM outbox
                         WHERE status = 'queued' OR (status = 'failed' AND retry_count < max_retries)",
                    )?;
                    #[allow(clippy::type_complexity)]
                    let rows: Vec<(
                        String,
                        String,
                        String,
                        Option<String>,
                        Option<String>,
                        String,
                        Option<String>,
                        Option<String>,
                        Option<String>,
                        Option<String>,
                        Option<String>,
                    )> = stmt
                        .query_map([], |row| {
                            Ok((
                                row.get(0)?,
                                row.get(1)?,
                                row.get(2)?,
                                row.get(3)?,
                                row.get(4)?,
                                row.get(5)?,
                                row.get(6)?,
                                row.get(7)?,
                                row.get(8)?,
                                row.get(9)?,
                                row.get(10)?,
                            ))
                        })?
                        .filter_map(|r| r.ok())
                        .collect();
                    Ok(rows)
                })?;

                if pending.is_empty() {
                    return serde_json::to_string(&0u32).map_err(MailFfiError::from);
                }

                let mut by_account: std::collections::HashMap<String, Vec<_>> =
                    std::collections::HashMap::new();
                for item in pending {
                    by_account.entry(item.1.clone()).or_default().push(item);
                }

                let mut total_sent_count = 0;

                for (account_id, items) in by_account {
                    if !try_start_flush(&account_id) {
                        continue;
                    }

                    let _guard = AccountFlushGuard(account_id.clone());

                    for (
                        id,
                        _,
                        to_json,
                        cc_json,
                        bcc_json,
                        subject,
                        body_html,
                        body_text,
                        in_reply_to,
                        email_references,
                        attachments_json,
                    ) in items
                    {
                        let to: Vec<String> = serde_json::from_str(&to_json).unwrap_or_default();
                        let cc: Vec<String> = cc_json
                            .as_ref()
                            .and_then(|s| serde_json::from_str(s).ok())
                            .unwrap_or_default();
                        let bcc: Vec<String> = bcc_json
                            .as_ref()
                            .and_then(|s| serde_json::from_str(s).ok())
                            .unwrap_or_default();
                        let attachments: Vec<maho_core::smtp_client::ComposeAttachment> =
                            attachments_json
                                .as_ref()
                                .and_then(|s| serde_json::from_str(s).ok())
                                .unwrap_or_default();

                        let (creds, use_xoauth2, from) = {
                            let claimed = ctx.with_db(|conn| {
                                conn.execute(
                                    "UPDATE outbox SET status = 'sending', updated_at = datetime('now') WHERE id = ?1 AND (status = 'queued' OR (status = 'failed' AND retry_count < max_retries))",
                                    rusqlite::params![id],
                                )
                                .map_err(MailFfiError::from)
                            })?;
                            if claimed == 0 {
                                continue;
                            }

                            match smtp_credentials_async(&ctx, &account_id).await {
                                Ok(c) => {
                                    let xoauth2 = match &c.auth {
                                        ImapAuth::OAuth2 { .. } => true,
                                        _ => false,
                                    };
                                    let from_addr = c.email.clone();
                                    (c, xoauth2, from_addr)
                                }
                                Err(e) => {
                                    let err_msg = e.to_string();
                                    let _ = ctx.with_db(|conn| {
                                        Ok(conn.execute(
                                            "UPDATE outbox SET status = 'failed', last_error = ?1, retry_count = retry_count + 1, updated_at = datetime('now') WHERE id = ?2",
                                            rusqlite::params![err_msg, id],
                                        ))
                                    });
                                    continue;
                                }
                            }
                        };

                        match send_smtp_message(
                            creds,
                            use_xoauth2,
                            from,
                            to,
                            cc,
                            bcc,
                            subject.clone(),
                            body_text.clone(),
                            body_html.clone(),
                            in_reply_to.clone(),
                            email_references.clone(),
                            None,
                            attachments,
                            None,
                        )
                        .await
                        {
                            Ok(()) => {
                                let _ = ctx.with_db(|conn| {
                                    Ok(conn.execute(
                                        "UPDATE outbox SET status = 'sent', updated_at = datetime('now') WHERE id = ?1",
                                        rusqlite::params![id],
                                    ))
                                });
                                total_sent_count += 1;
                                log::info!("Outbox: sent email {}", id);
                            }
                            Err(MailFfiError::Core(maho_core::error::AppError::DeliveryUncertain(error))) => {
                                ctx.with_db(|conn| {
                                    conn.execute(
                                        "UPDATE outbox SET status='uncertain', last_error=?1, updated_at=datetime('now') WHERE id=?2",
                                        rusqlite::params![error, id],
                                    )
                                    .map_err(MailFfiError::from)
                                })?;
                            }
                            Err(e) => {
                                if is_auth_error(&e) {
                                    let _ = ctx.with_db(|conn| {
                                        Ok(conn.execute(
                                            "UPDATE outbox SET status = 'failed', last_error = ?1, retry_count = max_retries, updated_at = datetime('now') WHERE id = ?2",
                                            rusqlite::params![e.to_string(), id],
                                        ))
                                    });
                                    log::warn!(
                                        "Outbox: auth error for email {}, exhausting retries: {}",
                                        id,
                                        e
                                    );
                                } else {
                                    let _ = ctx.with_db(|conn| {
                                        Ok(conn.execute(
                                            "UPDATE outbox SET status = 'failed', last_error = ?1, retry_count = retry_count + 1, updated_at = datetime('now') WHERE id = ?2",
                                            rusqlite::params![e.to_string(), id],
                                        ))
                                    });
                                    log::error!("Outbox: failed to send email {}: {}", id, e);
                                }
                            }
                        }
                    }
                }

                let _ = ctx.with_db(|conn| {
                    Ok(conn.execute(
                        "DELETE FROM outbox WHERE status = 'sent' AND updated_at < datetime('now', '-7 days')",
                        [],
                    ))
                });

                serde_json::to_string(&total_sent_count).map_err(MailFfiError::from)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDownloadAttachment(
    account_id: *const c_char,
    email_uid: i64,
    folder_id: *const c_char,
    part_id: *const c_char,
    filename: *const c_char,
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
        let Ok(part_id) = non_empty(part_id, "part_id") else {
            return false;
        };
        let Ok(filename) = non_empty(filename, "filename") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                // The connection guard is `!Send`: taken per database step so it
                // is never held across the IMAP fetch below.
                let temp_dir = attachment_cache_dir(&ctx)?;
                std::fs::create_dir_all(&temp_dir)
                    .map_err(|e| MailFfiError::Internal(e.to_string()))?;

                let (folder_path, epoch): (String, i64) = ctx.with_db(|conn| {
                    conn.query_row(
                        "SELECT path, uid_validity FROM folders WHERE id = ?1 AND account_id = ?2",
                        rusqlite::params![folder_id, account_id],
                        |row| Ok((row.get(0)?, row.get(1)?)),
                    )
                    .map_err(MailFfiError::from)
                })?;
                let uid = u32::try_from(email_uid).ok().filter(|uid| *uid > 0)
                    .ok_or(MailFfiError::InvalidArg("email_uid"))?;
                let safe_filename =
                    filename.replace(['/', '\\', ':', '*', '?', '"', '<', '>', '|'], "_");
                use sha2::{Digest, Sha256};
                let identity = serde_json::to_vec(&(&account_id, &folder_id, &folder_path, epoch, uid, &part_id))?;
                let cache_key = format!("{:x}_{}", Sha256::digest(identity), safe_filename);
                let file_path = temp_dir.join(&cache_key);

                if file_path.exists() {
                    return serde_json::to_string(&file_path.to_string_lossy().to_string())
                        .map_err(MailFfiError::from);
                }

                let resolved = ctx.with_db(|conn| {
                    resolve_account_auth(conn, &ctx.credential_key, &account_id)
                })?;

                let part_id_clone = part_id.clone();
                let folder_path_clone = folder_path.clone();

                let raw_data = match tokio::task::spawn_blocking({
                    let resolved = resolved.clone();
                    let folder_path = folder_path_clone.clone();
                    let part_id = part_id_clone.clone();
                    move || {
                        let mut client = crate::account::connect_imap(&resolved)?;
                        client
                            .fetch_attachment(&folder_path, uid, &part_id)
                            .map_err(MailFfiError::from)
                    }
                })
                .await
                .map_err(|e| MailFfiError::Internal(e.to_string()))?
                {
                    Ok(data) => data,
                    Err(err)
                        if resolved.account.auth_type.starts_with("oauth2")
                            && is_oauth_auth_error(&err) =>
                    {
                        let _ = ctx.with_db(|conn| {
                            Ok(conn.execute(
                                "UPDATE accounts SET oauth2_expires_at = '1970-01-01T00:00:00Z' WHERE id = ?1",
                                [&account_id],
                            ))
                        });
                        let refreshed = ctx.with_db(|conn| {
                            resolve_account_auth(conn, &ctx.credential_key, &account_id)
                        })?;
                        tokio::task::spawn_blocking(move || {
                            let mut client = crate::account::connect_imap(&refreshed)?;
                            client
                                .fetch_attachment(&folder_path_clone, uid, &part_id_clone)
                                .map_err(MailFfiError::from)
                        })
                        .await
                        .map_err(|e| MailFfiError::Internal(e.to_string()))??
                    }
                    Err(err) => return Err(err),
                };

                std::fs::write(&file_path, &raw_data)
                    .map_err(|e| MailFfiError::Internal(e.to_string()))?;

                let cache_dir = temp_dir.clone();
                tokio::spawn(async move {
                    enforce_cache_limit(&cache_dir, DEFAULT_CACHE_LIMIT_BYTES);
                });

                serde_json::to_string(&file_path.to_string_lossy().to_string())
                    .map_err(MailFfiError::from)
            })
        })
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::{CStr, CString};
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::{Condvar, Mutex};
    use std::time::{Duration, Instant};

    struct Capture {
        slot: Mutex<Option<(bool, String)>>,
        count: AtomicUsize,
        cv: Condvar,
    }

    impl Capture {
        fn new() -> Self {
            Self {
                slot: Mutex::new(None),
                count: AtomicUsize::new(0),
                cv: Condvar::new(),
            }
        }

        fn wait(&self, ms: u64) -> Option<(bool, String)> {
            let deadline = Instant::now() + Duration::from_millis(ms);
            let mut guard = self.slot.lock().unwrap();
            while guard.is_none() {
                let now = Instant::now();
                if now >= deadline {
                    break;
                }
                let (next, _) = self.cv.wait_timeout(guard, deadline - now).unwrap();
                guard = next;
            }
            guard.clone()
        }

        fn peek(&self) -> Option<(bool, String)> {
            self.slot.lock().unwrap().clone()
        }

        fn invocations(&self) -> usize {
            self.count.load(Ordering::SeqCst)
        }

        fn as_user_data(&self) -> *mut c_void {
            std::ptr::from_ref(self).cast::<c_void>().cast_mut()
        }
    }

    unsafe extern "C" fn capture_cb(ok: bool, json: *const c_char, user_data: *mut c_void) {
        let capture = unsafe { &*user_data.cast::<Capture>() };
        let payload = if json.is_null() {
            String::new()
        } else {
            unsafe { CStr::from_ptr(json) }
                .to_string_lossy()
                .into_owned()
        };
        capture.count.fetch_add(1, Ordering::SeqCst);
        *capture.slot.lock().unwrap() = Some((ok, payload));
        capture.cv.notify_all();
    }

    #[test]
    fn test_null_callback_rejected() {
        let capture = Capture::new();
        assert!(!MahoMailGetReplyContext(
            std::ptr::null(),
            None,
            capture.as_user_data()
        ));
    }

    #[test]
    fn test_account_flush_guard_drop_recovers_from_poison() {
        let _ = std::panic::catch_unwind(|| {
            let _lock = FLUSHING_ACCOUNTS.lock().unwrap();
            panic!("poisoning mutex for test");
        });
        assert!(FLUSHING_ACCOUNTS.is_poisoned());

        let res = std::panic::catch_unwind(|| {
            let guard = AccountFlushGuard("acc_poison_test".to_string());
            drop(guard);
        });
        assert!(res.is_ok(), "AccountFlushGuard::drop must not panic on poisoned mutex");
    }

    #[test]
    fn test_save_and_update_draft_and_reply_context() {
        let _ctx_guard = crate::test_support::global_ctx_guard();
        let _rt = crate::runtime::runtime().expect("runtime");
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx_arc(pool);
        let _ = crate::state::set_ctx(ctx.clone());

        // seed drafts folder
        {
            let conn = ctx.pool.get().unwrap();
            conn.execute(
                "INSERT INTO folders (id, account_id, name, path, folder_type) VALUES ('fold_drafts', 'acc1', 'Drafts', 'Drafts', 'drafts')",
                [],
            ).unwrap();
        }

        // 1. Save draft
        let req = ComposeEmailRequest {
            account_id: "acc1".to_string(),
            to: vec!["recipient@example.com".to_string()],
            cc: None,
            bcc: None,
            subject: "Draft Subject".to_string(),
            body_text: Some("Draft body text".to_string()),
            body_html: Some("<p>Draft body html</p>".to_string()),
            read_receipt: None,
            attachments: None,
            in_reply_to: Some("<msg1@example.com>".to_string()),
            references: None,
        };
        let req_json = CString::new(serde_json::to_string(&req).unwrap()).unwrap();
        let capture = Capture::new();

        assert!(MahoMailSaveDraft(
            req_json.as_ptr(),
            Some(capture_cb),
            capture.as_user_data()
        ));

        let (ok, draft_id_json) = capture.wait(10_000).expect("callback fired");
        assert!(ok);
        let draft_id: String = serde_json::from_str(&draft_id_json).unwrap();
        assert!(!draft_id.is_empty());

        // 2. Update draft
        let mut update_req = req.clone();
        update_req.subject = "Updated Draft Subject".to_string();
        let update_req_json = CString::new(serde_json::to_string(&update_req).unwrap()).unwrap();
        let draft_id_c = CString::new(draft_id.clone()).unwrap();

        let capture2 = Capture::new();
        assert!(MahoMailUpdateDraft(
            draft_id_c.as_ptr(),
            update_req_json.as_ptr(),
            Some(capture_cb),
            capture2.as_user_data()
        ));

        let (ok2, _) = capture2.wait(10_000).expect("callback fired");
        assert!(ok2);

        // 3. Get Reply Context for seeded email 'em2'
        let em2_id_c = CString::new("em2").unwrap();
        let capture3 = Capture::new();
        assert!(MahoMailGetReplyContext(
            em2_id_c.as_ptr(),
            Some(capture_cb),
            capture3.as_user_data()
        ));

        let (ok3, context_json) = capture3.wait(10_000).expect("callback fired");
        assert!(ok3);
        let context: ReplyContext = serde_json::from_str(&context_json).unwrap();
        assert_eq!(context.original_subject, "Re: Hello");
        assert_eq!(context.original_from, "user@example.com");
    }

    #[test]
    fn test_outbox_operations() {
        let _ctx_guard = crate::test_support::global_ctx_guard();
        let _rt = crate::runtime::runtime().expect("runtime");
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx_arc(pool);
        let _ = crate::state::set_ctx(ctx.clone());

        // 1. Queue email
        let req = ComposeEmailRequest {
            account_id: "acc1".to_string(),
            to: vec!["to@example.com".to_string()],
            cc: None,
            bcc: None,
            subject: "Outbox Test".to_string(),
            body_text: Some("Body".to_string()),
            body_html: None,
            read_receipt: None,
            attachments: None,
            in_reply_to: None,
            references: None,
        };
        let req_json = CString::new(serde_json::to_string(&req).unwrap()).unwrap();
        let capture = Capture::new();

        assert!(MahoMailQueueEmail(
            req_json.as_ptr(),
            Some(capture_cb),
            capture.as_user_data()
        ));

        let (ok, item_json) = capture.wait(10_000).expect("callback fired");
        assert!(ok);
        let item: OutboxItem = serde_json::from_str(&item_json).unwrap();
        assert_eq!(item.subject, "Outbox Test");
        assert_eq!(item.status, "queued");

        // 2. List outbox
        let acc1_c = CString::new("acc1").unwrap();
        let capture2 = Capture::new();
        assert!(MahoMailListOutbox(
            acc1_c.as_ptr(),
            Some(capture_cb),
            capture2.as_user_data()
        ));
        let (ok2, list_json) = capture2.wait(10_000).expect("callback fired");
        assert!(ok2);
        let list: Vec<OutboxItem> = serde_json::from_str(&list_json).unwrap();
        assert_eq!(list.len(), 1);
        assert_eq!(list[0].id, item.id);

        // 3. Delete outbox item
        let item_id_c = CString::new(item.id.clone()).unwrap();
        let capture3 = Capture::new();
        assert!(MahoMailDeleteOutboxItem(
            item_id_c.as_ptr(),
            Some(capture_cb),
            capture3.as_user_data()
        ));
        let (ok3, _) = capture3.wait(10_000).expect("callback fired");
        assert!(ok3);

        // 4. Verify outbox is empty
        let capture4 = Capture::new();
        assert!(MahoMailListOutbox(
            acc1_c.as_ptr(),
            Some(capture_cb),
            capture4.as_user_data()
        ));
        let (ok4, list_json2) = capture4.wait(10_000).expect("callback fired");
        assert!(ok4);
        let list2: Vec<OutboxItem> = serde_json::from_str(&list_json2).unwrap();
        assert_eq!(list2.len(), 0);
    }
}

// === [W-D] ===

#[derive(Deserialize)]
pub struct DelegateEmailRequest {
    pub email_id: String,
    pub account_id: String,
    pub delegate_to: String,
    pub note: String,
}

#[no_mangle]
pub extern "C" fn MahoMailDelegateEmail(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(request) = serde_json::from_str::<DelegateEmailRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                // The connection guard is `!Send`: taken per database step so it
                // is never held across the SMTP send below.
                let (subject, from_address, body_text, body_html) = ctx.with_db(|conn| {
                    conn.query_row(
                        "SELECT subject, from_address, COALESCE(body_text, snippet), body_html FROM emails WHERE id = ?1",
                        [&request.email_id],
                        |row| Ok((
                            row.get::<_, String>(0)?,
                            row.get::<_, String>(1)?,
                            row.get::<_, Option<String>>(2)?,
                            row.get::<_, Option<String>>(3)?,
                        )),
                    )
                    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))
                })?;

                let fwd_subject = if subject.to_lowercase().starts_with("fwd:") {
                    subject
                } else {
                    format!("Fwd: {}", subject)
                };

                let note_block = if request.note.is_empty() {
                    String::new()
                } else {
                    format!("{}\n\n---\n\n", request.note)
                };

                let fwd_text = format!(
                    "{}---------- Forwarded message ----------\nFrom: {}\nSubject: {}\n\n{}",
                    note_block,
                    from_address,
                    fwd_subject,
                    body_text.as_deref().unwrap_or("")
                );

                let fwd_html = body_html.map(|html| {
                    let note_html = if request.note.is_empty() {
                        String::new()
                    } else {
                        format!("<p>{}</p><hr/>", request.note.replace('\n', "<br/>"))
                    };
                    format!(
                        "{}<div style=\"padding:10px 0;color:#666\"><b>---------- Forwarded message ----------</b><br/>From: {}<br/>Subject: {}</div>{}",
                        note_html,
                        from_address,
                        fwd_subject,
                        html
                    )
                });

                let creds = smtp_credentials_async(&ctx, &request.account_id).await?;
                let use_xoauth2 = match &creds.auth {
                    ImapAuth::OAuth2 { .. } => true,
                    _ => false,
                };
                let from = creds.email.clone();

                send_smtp_message(
                    creds,
                    use_xoauth2,
                    from,
                    vec![request.delegate_to.clone()],
                    vec![],
                    vec![],
                    fwd_subject.clone(),
                    Some(fwd_text),
                    fwd_html,
                    None,
                    None,
                    None,
                    vec![],
                    None,
                )
                .await?;

                let info: Option<(String, String)> = ctx.with_db(|conn| {
                    Ok(conn
                        .query_row("SELECT provider, model FROM ai_configs LIMIT 1", [], |row| {
                            Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?))
                        })
                        .ok())
                })?;
                let (prov, mdl) = info
                    .as_ref()
                    .map(|(p, m)| (p.as_str(), m.as_str()))
                    .unwrap_or(("unknown", ""));
                let id = uuid::Uuid::new_v4().to_string();
                let input_summary = if fwd_subject.len() > 100 {
                    &fwd_subject[..100]
                } else {
                    &fwd_subject
                };
                let output_summary = format!("Delegated to {}", request.delegate_to);
                let _ = ctx.with_db(|conn| {
                    Ok(conn.execute(
                        "INSERT INTO ai_action_log (id, account_id, email_id, action_type, provider, model, input_summary, output_summary, status) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
                        rusqlite::params![id, request.account_id, request.email_id, "delegation", prov, mdl, input_summary, output_summary, "success"],
                    ))
                });

                Ok("{}".to_string())
            })
        })
    })
}

#[derive(Deserialize)]
pub struct SendMdnReceiptRequest {
    pub email_id: String,
    pub account_id: String,
}

#[no_mangle]
pub extern "C" fn MahoMailSendMdnReceipt(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(request) = serde_json::from_str::<SendMdnReceiptRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                // The connection guard is `!Send`: taken for this read only, so
                // it is never held across the SMTP send below.
                let (original_message_id, mdn_requested, from_address) = ctx.with_db(|conn| {
                    conn.query_row(
                        "SELECT message_id, mdn_requested, from_address FROM emails WHERE id = ?1",
                        [&request.email_id],
                        |row| {
                            Ok((
                                row.get::<_, String>(0)?,
                                row.get::<_, Option<String>>(1)?,
                                row.get::<_, String>(2)?,
                            ))
                        },
                    )
                    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))
                })?;

                let report_to = match mdn_requested {
                    Some(addr) if !addr.is_empty() => addr,
                    _ => from_address,
                };

                let creds = smtp_credentials_async(&ctx, &request.account_id).await?;
                let recipient_email = creds.email.clone();
                let use_xoauth2 = match &creds.auth {
                    ImapAuth::OAuth2 { .. } => true,
                    _ => false,
                };
                let from = creds.email.clone();

                let mdn_text = format!(
                    "This is a Return Receipt for the mail that you sent to {}.\n\n                     Note: This Return Receipt only acknowledges that the message was displayed on the recipient's computer.                      There is no guarantee that the recipient has read or understood the message contents.\n",
                    recipient_email
                );

                let mdn_html = format!(
                    "<html><body><p>This is a Return Receipt for the mail that you sent to {}.</p>\n                     <p>Note: This Return Receipt only acknowledges that the message was displayed on the recipient's computer.                      There is no guarantee that the recipient has read or understood the message contents.</p></body></html>",
                    recipient_email
                );

                send_smtp_message(
                    creds,
                    use_xoauth2,
                    from,
                    vec![report_to],
                    vec![],
                    vec![],
                    format!("Return Receipt (displayed) - {}", original_message_id),
                    Some(mdn_text),
                    Some(mdn_html),
                    Some(original_message_id),
                    None,
                    None,
                    vec![],
                    None,
                )
                .await?;

                Ok("{}".to_string())
            })
        })
    })
}

#[derive(Clone)]
struct AttachmentMeta {
    part_id: String,
    filename: String,
    mime_type: String,
}

#[derive(Deserialize)]
pub struct GetForwardedAttachmentsRequest {
    pub account_id: String,
    pub email_uid: i64,
    pub folder_id: String,
}

#[no_mangle]
pub extern "C" fn MahoMailGetForwardedAttachments(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(request) = serde_json::from_str::<GetForwardedAttachmentsRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                // The connection guard is `!Send`: taken per database step so it
                // is never held across the IMAP fetches below.
                let (folder_path, attachment_metas) = ctx.with_db(|conn| {
                    let folder_path: String = conn
                        .query_row(
                            "SELECT path FROM folders WHERE id = ?1",
                            [&request.folder_id],
                            |row| row.get(0),
                        )
                        .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                    let email_id: String = conn.query_row(
                        "SELECT id FROM emails WHERE account_id = ?1 AND uid = ?2 AND folder_id = ?3",
                        rusqlite::params![request.account_id, request.email_uid, request.folder_id],
                        |row| row.get(0),
                    )
                    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                    let mut stmt = conn
                        .prepare(
                            "SELECT id, filename, mime_type FROM attachments WHERE email_id = ?1",
                        )
                        .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;
                    let metas: Vec<AttachmentMeta> = stmt
                        .query_map([&email_id], |row| {
                            Ok(AttachmentMeta {
                                part_id: row.get("id")?,
                                filename: row
                                    .get::<_, Option<String>>("filename")?
                                    .unwrap_or_else(|| "attachment".to_string()),
                                mime_type: row.get("mime_type")?,
                            })
                        })
                        .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?
                        .filter_map(|r| r.ok())
                        .collect();

                    Ok((folder_path, metas))
                })?;

                if attachment_metas.is_empty() {
                    return Ok("[]".to_string());
                }

                let resolved = ctx.with_db(|conn| {
                    resolve_account_auth(conn, &ctx.credential_key, &request.account_id)
                })?;
                let uid = request.email_uid as u32;
                let folder_path_clone = folder_path.clone();
                let metas_clone = attachment_metas.clone();

                let results = match tokio::task::spawn_blocking({
                    let resolved = resolved.clone();
                    let folder_path = folder_path_clone.clone();
                    let metas = metas_clone.clone();
                    move || {
                        let mut client = crate::account::connect_imap(&resolved)?;
                        let engine = base64::engine::general_purpose::STANDARD;
                        let mut compose_attachments = Vec::with_capacity(metas.len());
                        for meta in &metas {
                            let data = client.fetch_attachment(&folder_path, uid, &meta.part_id)?;
                            compose_attachments.push(maho_core::smtp_client::ComposeAttachment {
                                filename: meta.filename.clone(),
                                mime_type: meta.mime_type.clone(),
                                data: engine.encode(&data),
                            });
                        }
                        Ok(compose_attachments)
                    }
                })
                .await
                .map_err(|e| MailFfiError::Internal(e.to_string()))?
                {
                    Ok(data) => data,
                    Err(err)
                        if resolved.account.auth_type.starts_with("oauth2")
                            && is_oauth_auth_error(&err) =>
                    {
                        let _ = ctx.with_db(|conn| {
                            Ok(conn.execute(
                                "UPDATE accounts SET oauth2_expires_at = '1970-01-01T00:00:00Z' WHERE id = ?1",
                                [&request.account_id],
                            ))
                        });
                        let refreshed = ctx.with_db(|conn| {
                            resolve_account_auth(conn, &ctx.credential_key, &request.account_id)
                        })?;
                        tokio::task::spawn_blocking(move || {
                            let mut client = crate::account::connect_imap(&refreshed)?;
                            let engine = base64::engine::general_purpose::STANDARD;
                            let mut compose_attachments = Vec::with_capacity(metas_clone.len());
                            for meta in &metas_clone {
                                let data = client.fetch_attachment(
                                    &folder_path_clone,
                                    uid,
                                    &meta.part_id,
                                )?;
                                compose_attachments.push(
                                    maho_core::smtp_client::ComposeAttachment {
                                        filename: meta.filename.clone(),
                                        mime_type: meta.mime_type.clone(),
                                        data: engine.encode(&data),
                                    },
                                );
                            }
                            Ok::<_, MailFfiError>(compose_attachments)
                        })
                        .await
                        .map_err(|e| MailFfiError::Internal(e.to_string()))??
                    }
                    Err(err) => return Err(err),
                };

                serde_json::to_string(&results).map_err(MailFfiError::from)
            })
        })
    })
}

/// Sanitize a value that will become part of an on-disk attachment cache key.
/// Strips path separators, drive/scheme colons, wildcards and dots so a
/// renderer-controlled component (e.g. account_id) cannot escape the cache dir
/// via `../` traversal.
#[cfg(test)]
fn sanitize_cache_component(s: &str) -> String {
    s.replace(['/', '\\', ':', '*', '?', '"', '<', '>', '|', '.'], "_")
}

#[cfg(test)]
#[path = "compose_review_tests.rs"]
mod compose_review_tests;

#[cfg(test)]
#[allow(clippy::disallowed_methods)]
mod ulw_t6_tests {
    use super::sanitize_cache_component;

    #[test]
    fn blocks_path_traversal_components() {
        let s = sanitize_cache_component("../../etc/passwd");
        assert!(!s.contains('/'), "must strip '/': {s}");
        assert!(!s.contains('\\'), "must strip '\\\\': {s}");
        assert!(!s.contains(".."), "must strip '..': {s}");
    }

    #[test]
    fn preserves_plain_uuid() {
        let id = "a54a1eb5-d624-4437-9ea7-914eb56a6f01";
        assert_eq!(sanitize_cache_component(id), id);
    }
}
