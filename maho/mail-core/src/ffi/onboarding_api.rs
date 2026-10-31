// Copyright 2026 Maho Browser. All rights reserved.

//! Asynchronous, callback-based account-onboarding FFI for the mail helper.
//!
//! Mirrors the acceptance + provenance + exactly-once + panic-isolation
//! contract of [`super::read_api`]: each export validates its inputs, hands the
//! blocking work to the process tokio runtime, and invokes the caller's
//! [`MahoMailReadCallback`] exactly once per ACCEPTED call. Rejected calls
//! (null/invalid argument, null callback, malformed JSON, unavailable
//! state/runtime, or a pre-dispatch panic) return `false` and never invoke the
//! callback.
//!
//! Covers password/IMAP onboarding and OAuth2 PKCE onboarding (URL mint +
//! code exchange). Opening a browser / binding a listener is a C++-layer
//! concern and is not done here.

use std::os::raw::c_char;

use serde::Serialize;

use maho_core::models::account::CreateAccountRequest;

use crate::account;
use crate::error::MailFfiError;
use crate::oauth;

use super::read_api::{accept_read, accept_read_call, blocking_json, MahoMailReadCallback};
use super::{c_string, non_empty};

#[derive(Serialize)]
struct ProbeResponse {
    ok: bool,
}

#[derive(Serialize)]
struct DeletedResponse {
    deleted: String,
}

#[derive(Serialize)]
struct ReconnectedResponse {
    reconnected: String,
}

#[no_mangle]
pub extern "C" fn MahoMailAddAccount(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut std::ffi::c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(request) = serde_json::from_str::<CreateAccountRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let mut conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                account::create_account(&mut conn, &ctx.credential_key, request)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailTestConnection(
    params_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut std::ffi::c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(params_json, "params_json") else {
            return false;
        };
        let Ok(params) = serde_json::from_str::<account::ProbeParams>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |_ctx| {
            blocking_json(move || {
                account::probe_imap_connection(&params)?;
                Ok(ProbeResponse { ok: true })
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDeleteAccount(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut std::ffi::c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                account::delete_account(&ctx, &account_id)?;
                Ok(DeletedResponse {
                    deleted: account_id,
                })
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailOAuthStartUrl(
    provider: *const c_char,
    client_id: *const c_char,
    redirect_uri: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut std::ffi::c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(provider) = non_empty(provider, "provider") else {
            return false;
        };
        let Ok(client_id) = c_string(client_id, "client_id") else {
            return false;
        };
        let Ok(redirect_uri) = non_empty(redirect_uri, "redirect_uri") else {
            return false;
        };
        accept_read(callback, user_data, move |_ctx| {
            blocking_json(move || oauth::start_oauth(&provider, &client_id, &redirect_uri))
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailOAuthStartUrlWithOptions(
    provider: *const c_char,
    client_id: *const c_char,
    redirect_uri: *const c_char,
    options_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut std::ffi::c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(provider) = non_empty(provider, "provider") else {
            return false;
        };
        let Ok(client_id) = c_string(client_id, "client_id") else {
            return false;
        };
        let Ok(redirect_uri) = non_empty(redirect_uri, "redirect_uri") else {
            return false;
        };
        let Ok(options_json) = non_empty(options_json, "options_json") else {
            return false;
        };
        let Ok(options) = serde_json::from_str::<oauth::StartOAuthOptions>(&options_json) else {
            return false;
        };
        accept_read(callback, user_data, move |_ctx| {
            blocking_json(move || {
                oauth::start_oauth_with_options(&provider, &client_id, &redirect_uri, options)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailOAuthLoopbackSignIn(
    provider: *const c_char,
    options_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut std::ffi::c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(provider) = non_empty(provider, "provider") else {
            return false;
        };
        let Ok(options_json) = non_empty(options_json, "options_json") else {
            return false;
        };
        let Ok(options) = serde_json::from_str::<oauth::StartOAuthOptions>(&options_json) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let mut options = options;
                if let Some(account_id) = options.reauthorize_account_id.as_deref() {
                    let conn = ctx
                        .pool
                        .get()
                        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                    options.login_hint =
                        Some(account::oauth_reauthorization_email(&conn, account_id)?);
                    options.expected_google_sub = None;
                }
                oauth::sign_in_with_oauth_loopback_with_options(&provider, options)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailOAuthComplete(
    state: *const c_char,
    code: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut std::ffi::c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(state) = non_empty(state, "state") else {
            return false;
        };
        let Ok(code) = non_empty(code, "code") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let mut conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                oauth::complete_oauth(&mut conn, &ctx.credential_key, &state, &code)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailOAuthCompleteWithOptions(
    state: *const c_char,
    code: *const c_char,
    options_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut std::ffi::c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(state) = non_empty(state, "state") else {
            return false;
        };
        let Ok(code) = non_empty(code, "code") else {
            return false;
        };
        let Ok(options_json) = non_empty(options_json, "options_json") else {
            return false;
        };
        let Ok(options) = serde_json::from_str::<oauth::CompleteOAuthOptions>(&options_json) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let mut conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                oauth::complete_oauth_with_options(
                    &mut conn,
                    &ctx.credential_key,
                    &state,
                    &code,
                    options,
                )
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailReconnectAccount(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut std::ffi::c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                account::reconnect_account(&ctx, &account_id)?;
                Ok(ReconnectedResponse {
                    reconnected: account_id,
                })
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailImportMigrationArchive(
    archive_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut std::ffi::c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(archive_json) = non_empty(archive_json, "archive_json") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || crate::transfer::import_migration_archive(&ctx, &archive_json))
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailOAuthCancel(state: *const c_char) -> bool {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(move || {
        let Ok(state_str) = non_empty(state, "state") else {
            return false;
        };
        crate::state::pending_pkce().cancel(&state_str)
    }))
    .unwrap_or(false)
}

// === [W-C.Additional] ===

#[no_mangle]
pub extern "C" fn MahoMailGetAccount(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut std::ffi::c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;

                let row = db.query_row(
                    "SELECT id, email, display_name, auth_type, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username, oauth2_client_id, created_at, updated_at FROM accounts WHERE id = ?1",
                    [&account_id],
                    |row| {
                        let imap_port: i64 = row.get("imap_port")?;
                        let smtp_port: i64 = row.get("smtp_port")?;
                        let imap_enc_str: String = row.get("imap_encryption")?;
                        let smtp_enc_str: String = row.get("smtp_encryption")?;
                        Ok(maho_core::models::account::AccountResponse {
                            id: row.get("id")?,
                            email: row.get("email")?,
                            display_name: row.get("display_name")?,
                            auth_type: row.get::<_, Option<String>>("auth_type")?.unwrap_or_else(|| "password".to_string()),
                            imap_host: row.get("imap_host")?,
                            imap_port: imap_port as u16,
                            imap_encryption: maho_core::models::account::Encryption::from_str_lossy(&imap_enc_str),
                            smtp_host: row.get("smtp_host")?,
                            smtp_port: smtp_port as u16,
                            smtp_encryption: maho_core::models::account::Encryption::from_str_lossy(&smtp_enc_str),
                            username: row.get("username")?,
                            oauth2_client_id: row.get("oauth2_client_id")?,
                            created_at: row.get("created_at")?,
                            updated_at: row.get("updated_at")?,
                        })
                    }
                ).map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                Ok(row)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailUpdateAccount(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut std::ffi::c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(request) =
            serde_json::from_str::<maho_core::models::account::UpdateAccountRequest>(&raw)
        else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;

                let exists: bool = db
                    .query_row(
                        "SELECT COUNT(*) > 0 FROM accounts WHERE id = ?1",
                        [&request.id],
                        |row| row.get(0),
                    )
                    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;
                if !exists {
                    return Err(MailFfiError::AccountNotFound(request.id));
                }

                let now = chrono::Utc::now().format("%Y-%m-%d %H:%M:%S").to_string();
                let mut set_clauses = Vec::new();
                let mut params: Vec<Box<dyn rusqlite::types::ToSql>> = Vec::new();

                if let Some(ref email) = request.email {
                    set_clauses.push("email = ?".to_string());
                    params.push(Box::new(email.clone()));
                }
                if let Some(ref display_name) = request.display_name {
                    set_clauses.push("display_name = ?".to_string());
                    params.push(Box::new(display_name.clone()));
                }
                if let Some(ref imap_host) = request.imap_host {
                    set_clauses.push("imap_host = ?".to_string());
                    params.push(Box::new(imap_host.clone()));
                }
                if let Some(imap_port) = request.imap_port {
                    set_clauses.push("imap_port = ?".to_string());
                    params.push(Box::new(imap_port as i64));
                }
                if let Some(ref imap_encryption) = request.imap_encryption {
                    set_clauses.push("imap_encryption = ?".to_string());
                    params.push(Box::new(imap_encryption.as_str().to_string()));
                }
                if let Some(ref smtp_host) = request.smtp_host {
                    set_clauses.push("smtp_host = ?".to_string());
                    params.push(Box::new(smtp_host.clone()));
                }
                if let Some(smtp_port) = request.smtp_port {
                    set_clauses.push("smtp_port = ?".to_string());
                    params.push(Box::new(smtp_port as i64));
                }
                if let Some(ref smtp_encryption) = request.smtp_encryption {
                    set_clauses.push("smtp_encryption = ?".to_string());
                    params.push(Box::new(smtp_encryption.as_str().to_string()));
                }
                if let Some(ref username) = request.username {
                    set_clauses.push("username = ?".to_string());
                    params.push(Box::new(username.clone()));
                }
                if let Some(ref password) = request.password {
                    crate::credentials::store_encrypted_credential(
                        &db,
                        &ctx.credential_key,
                        &request.id,
                        "password",
                        password,
                    )?;
                    set_clauses.push("password = ?".to_string());
                    params.push(Box::new(Option::<String>::None));
                }

                set_clauses.push("updated_at = ?".to_string());
                params.push(Box::new(now));

                let sql = format!(
                    "UPDATE accounts SET {} WHERE id = ?",
                    set_clauses.join(", ")
                );
                params.push(Box::new(request.id.clone()));

                let param_refs: Vec<&dyn rusqlite::types::ToSql> =
                    params.iter().map(|p| p.as_ref()).collect();
                db.execute(&sql, param_refs.as_slice())
                    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                let row = db.query_row(
                    "SELECT id, email, display_name, auth_type, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username, oauth2_client_id, created_at, updated_at FROM accounts WHERE id = ?1",
                    [&request.id],
                    |row| {
                        let imap_port: i64 = row.get("imap_port")?;
                        let smtp_port: i64 = row.get("smtp_port")?;
                        let imap_enc_str: String = row.get("imap_encryption")?;
                        let smtp_enc_str: String = row.get("smtp_encryption")?;
                        Ok(maho_core::models::account::AccountResponse {
                            id: row.get("id")?,
                            email: row.get("email")?,
                            display_name: row.get("display_name")?,
                            auth_type: row.get::<_, Option<String>>("auth_type")?.unwrap_or_else(|| "password".to_string()),
                            imap_host: row.get("imap_host")?,
                            imap_port: imap_port as u16,
                            imap_encryption: maho_core::models::account::Encryption::from_str_lossy(&imap_enc_str),
                            smtp_host: row.get("smtp_host")?,
                            smtp_port: smtp_port as u16,
                            smtp_encryption: maho_core::models::account::Encryption::from_str_lossy(&smtp_enc_str),
                            username: row.get("username")?,
                            oauth2_client_id: row.get("oauth2_client_id")?,
                            created_at: row.get("created_at")?,
                            updated_at: row.get("updated_at")?,
                        })
                    }
                ).map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                serde_json::to_string(&row).map_err(MailFfiError::from)
            })
        })
    })
}

// === [W-C.Additional.Refresh] ===

#[no_mangle]
pub extern "C" fn MahoMailRefreshOAuthToken(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut std::ffi::c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let auth =
                    crate::account::resolve_account_auth(&db, &ctx.credential_key, &account_id)?;
                let token = match auth.auth {
                    crate::account::ImapAuth::OAuth2 { access_token } => access_token,
                    _ => return Err(MailFfiError::OAuth2Refresh("not an oauth2 account".into())),
                };
                // Return the token wrapped in a simple JSON string or object
                serde_json::to_string(&token).map_err(MailFfiError::from)
            })
        })
    })
}

#[cfg(test)]
#[path = "onboarding_api_tests.rs"]
mod onboarding_api_tests;

#[cfg(test)]
#[path = "account_wire_tests.rs"]
mod account_wire_tests;
