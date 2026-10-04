// Copyright 2026 Maho Browser. All rights reserved.

//! Account auth resolution and IMAP connection establishment.
//!
//! Scope for this task: IMAP onboarding via password and OAuth2 (Gmail /
//! Microsoft). Credentials are read from the `accounts` row columns when
//! present, else decrypted from `encrypted_credentials` with the injected
//! credential key. OAuth2 access tokens are proactively refreshed when expired.
//!
//! DEFERRED (not ported in this task): SMTP send/compose, calendar, PGP, S/MIME,
//! rules, templates, signatures, translation. See `lib.rs` for the full list.

use std::sync::Arc;
use std::time::Duration;

use chrono::{DateTime, Utc};
use maho_core::imap_client::ImapClient;
use maho_core::models::account::{Account, AccountResponse, CreateAccountRequest, Encryption};
use rusqlite::Connection;
use serde::Deserialize;
use uuid::Uuid;

use crate::credentials::{get_encrypted_credential, store_encrypted_credential};
use crate::error::{MailFfiError, Result};
use crate::state::AppCtx;

/// Resolved IMAP authentication material for an account.
#[derive(Clone)]
pub enum ImapAuth {
    Password(String),
    OAuth2 { access_token: String },
}

#[derive(Clone)]
pub struct AccountAuth {
    pub account: Account,
    pub auth: ImapAuth,
}

/// Load the account row.
pub fn load_account(conn: &Connection, account_id: &str) -> Result<Account> {
    conn.query_row(
        "SELECT id, email, display_name, auth_type, imap_host, imap_port, imap_encryption,
                smtp_host, smtp_port, smtp_encryption, username, oauth2_client_id,
                oauth2_client_secret, oauth2_refresh_token, oauth2_access_token,
                oauth2_expires_at, password, created_at, updated_at
         FROM accounts WHERE id = ?1",
        [account_id],
        Account::from_row,
    )
    .map_err(|_| MailFfiError::AccountNotFound(account_id.to_string()))
}

/// Resolve usable IMAP auth for an account, refreshing OAuth2 tokens if needed.
///
/// `credential_key` decrypts values from `encrypted_credentials` when the
/// plaintext columns are empty. On successful OAuth2 refresh the new token is
/// persisted back to the `accounts` row.
pub fn resolve_account_auth(
    conn: &Connection,
    credential_key: &[u8; 32],
    account_id: &str,
) -> Result<AccountAuth> {
    let account = load_account(conn, account_id)?;

    if account.auth_type.starts_with("oauth2") {
        let access_token = resolve_oauth2_access_token(conn, credential_key, &account)?;
        Ok(AccountAuth {
            account,
            auth: ImapAuth::OAuth2 { access_token },
        })
    } else {
        let password = column_or_encrypted(
            account.password.clone(),
            conn,
            credential_key,
            account_id,
            "password",
        )?
        .ok_or_else(|| MailFfiError::MissingCredentials(account_id.to_string()))?;
        Ok(AccountAuth {
            account,
            auth: ImapAuth::Password(password),
        })
    }
}

/// Establish an authenticated IMAP connection for the resolved account.
/// Blocking — must be called from a blocking context (e.g. `spawn_blocking`).
pub fn connect_imap(resolved: &AccountAuth) -> Result<ImapClient> {
    let acc = &resolved.account;
    let client = match &resolved.auth {
        ImapAuth::Password(password) => ImapClient::connect(
            &acc.imap_host,
            acc.imap_port,
            &acc.imap_encryption,
            &acc.username,
            password,
        )?,
        ImapAuth::OAuth2 { access_token } => ImapClient::connect_oauth2(
            &acc.imap_host,
            acc.imap_port,
            &acc.imap_encryption,
            &acc.username,
            access_token,
        )?,
    };
    Ok(client)
}

/// Check if an error represents an authentication failure from an IMAP/SMTP server
/// or credential resolution.
pub fn is_auth_error(err: &MailFfiError) -> bool {
    match err {
        MailFfiError::MissingCredentials(_) | MailFfiError::OAuth2Refresh(_) => true,
        MailFfiError::Core(maho_core::error::AppError::Auth(_)) => true,
        MailFfiError::Core(ref app_err) => {
            let msg = app_err.to_string().to_uppercase();
            msg.contains("AUTHENTICATIONFAILED")
                || msg.contains("UNAUTHORIZED")
                || msg.contains("INVALID CREDENTIALS")
                || msg.contains("535")
        }
        _ => false,
    }
}

/// Invalidate cached OAuth2 access token in the database so the next
/// `resolve_account_auth` forces a fresh token exchange via the provider.
pub fn invalidate_oauth2_token(conn: &Connection, account_id: &str) -> Result<()> {
    conn.execute(
        "UPDATE accounts SET oauth2_expires_at = '1970-01-01T00:00:00Z' WHERE id = ?1",
        [account_id],
    )
    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;
    Ok(())
}

/// Establish an authenticated IMAP connection for the resolved account, with
/// one-shot token refresh and retry on OAuth2 authentication failure.
pub fn connect_imap_with_retry(
    conn: &Connection,
    credential_key: &[u8; 32],
    resolved: &mut AccountAuth,
) -> Result<ImapClient> {
    connect_imap_with_retry_fn(conn, credential_key, resolved, connect_imap)
}

/// Parameterized connection helper accepting a custom connector for testing.
pub fn connect_imap_with_retry_fn<F>(
    conn: &Connection,
    credential_key: &[u8; 32],
    resolved: &mut AccountAuth,
    connect: F,
) -> Result<ImapClient>
where
    F: Fn(&AccountAuth) -> Result<ImapClient>,
{
    match connect(resolved) {
        Ok(client) => Ok(client),
        Err(err) if resolved.account.auth_type.starts_with("oauth2") && is_auth_error(&err) => {
            log::warn!(
                "[mail-ffi] IMAP authentication failed for oauth2 account {}; forcing token refresh and retrying once: {err}",
                resolved.account.id
            );
            invalidate_oauth2_token(conn, &resolved.account.id)?;
            let refreshed = resolve_account_auth(conn, credential_key, &resolved.account.id)?;
            *resolved = refreshed;
            connect(resolved)
        }
        Err(err) => Err(err),
    }
}


fn column_or_encrypted(
    column: Option<String>,
    conn: &Connection,
    credential_key: &[u8; 32],
    account_id: &str,
    credential_type: &str,
) -> Result<Option<String>> {
    if let Some(value) = column {
        if !value.is_empty() {
            return Ok(Some(value));
        }
    }
    get_encrypted_credential(conn, credential_key, account_id, credential_type)
}

fn resolve_oauth2_access_token(
    conn: &Connection,
    credential_key: &[u8; 32],
    account: &Account,
) -> Result<String> {
    let current = column_or_encrypted(
        account.oauth2_access_token.clone(),
        conn,
        credential_key,
        &account.id,
        "oauth2_access_token",
    )?;

    let expired = account
        .oauth2_expires_at
        .as_deref()
        .map(is_expired)
        .unwrap_or(false);

    if let Some(token) = current.as_ref() {
        if !expired {
            return Ok(token.clone());
        }
    }

    match refresh_oauth2_token(conn, credential_key, account) {
        Ok(new_token) => {
            crate::ffi::emit_refresh_succeeded_event(&account.id);
            Ok(new_token)
        }
        Err(refresh_err) => {
            crate::ffi::emit_reauth_event(
                &account.id,
                &account.auth_type,
                classify_reauth_reason(&refresh_err),
            );
            match current {
                Some(token) if !expired => {
                    log::warn!(
                        "[mail-ffi] oauth2 refresh failed for {}, using existing unexpired token: {refresh_err}",
                        account.id
                    );
                    Ok(token)
                }
                _ => Err(refresh_err),
            }
        }
    }
}

fn is_expired(expires_at: &str) -> bool {
    match DateTime::parse_from_rfc3339(expires_at) {
        Ok(dt) => {
            let with_margin = dt.with_timezone(&Utc) - chrono::Duration::minutes(5);
            Utc::now() >= with_margin
        }
        Err(_) => true,
    }
}

fn classify_reauth_reason(err: &MailFfiError) -> &'static str {
    let MailFfiError::OAuth2Refresh(message) = err else {
        return "unknown";
    };
    let lower = message.to_ascii_lowercase();
    if lower.contains("missing refresh_token") {
        return "refresh_token_missing";
    }
    if lower.contains("invalid_grant") {
        return "invalid_grant";
    }
    if lower.contains("invalid_client") {
        return "invalid_client";
    }
    "unknown"
}

#[derive(serde::Deserialize)]
struct TokenResponse {
    access_token: String,
    expires_in: Option<i64>,
    refresh_token: Option<String>,
}

/// Build `refresh_token` grant params, including `client_secret` ONLY when
/// non-empty. Public/desktop PKCE clients have no secret; sending an empty
/// `client_secret=` makes Google reject the refresh with `invalid_request`
/// ("client_secret is missing."). Confidential clients that supply one still
/// forward it.
fn build_refresh_params<'a>(
    client_id: &'a str,
    client_secret: &'a str,
    refresh_token: &'a str,
) -> Vec<(&'static str, &'a str)> {
    let mut params: Vec<(&'static str, &'a str)> = vec![
        ("grant_type", "refresh_token"),
        ("client_id", client_id),
        ("refresh_token", refresh_token),
    ];
    if !client_secret.is_empty() {
        params.push(("client_secret", client_secret));
    }
    params
}

/// Refresh an OAuth2 access token via the provider token endpoint and persist
/// it. Blocking (uses `reqwest::blocking`); call from a blocking context.
fn persist_refreshed_oauth2_credentials(
    conn: &Connection,
    credential_key: &[u8; 32],
    account_id: &str,
    access_token: &str,
    refresh_token: Option<&str>,
    expires_at: Option<&str>,
) -> Result<()> {
    let tx = conn.unchecked_transaction()?;
    store_encrypted_credential(
        &tx,
        credential_key,
        account_id,
        "oauth2_access_token",
        access_token,
    )?;
    if let Some(refresh_token) = refresh_token.filter(|value| !value.is_empty()) {
        store_encrypted_credential(
            &tx,
            credential_key,
            account_id,
            "oauth2_refresh_token",
            refresh_token,
        )?;
    }

    tx.execute(
        "UPDATE accounts SET oauth2_access_token = NULL, oauth2_expires_at = ?1,
             oauth2_refresh_token = NULL,
             updated_at = datetime('now')
         WHERE id = ?2",
        rusqlite::params![expires_at, account_id],
    )
    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;
    tx.commit()?;
    Ok(())
}

fn refresh_oauth2_token(
    conn: &Connection,
    credential_key: &[u8; 32],
    account: &Account,
) -> Result<String> {
    let token_url = token_endpoint(&account.auth_type).ok_or_else(|| {
        MailFfiError::OAuth2Refresh(format!("unknown provider {}", account.auth_type))
    })?;

    #[cfg(test)]
    let token_url = if account.oauth2_client_id.as_deref() == Some("review-loopback") {
        account.smtp_host.as_str()
    } else {
        token_url
    };

    let client_id = account
        .oauth2_client_id
        .clone()
        .filter(|s| !s.is_empty())
        .ok_or_else(|| MailFfiError::OAuth2Refresh("missing client_id".into()))?;

    let client_secret = column_or_encrypted(
        account.oauth2_client_secret.clone(),
        conn,
        credential_key,
        &account.id,
        "oauth2_client_secret",
    )?
    .unwrap_or_default();

    let refresh_token = column_or_encrypted(
        account.oauth2_refresh_token.clone(),
        conn,
        credential_key,
        &account.id,
        "oauth2_refresh_token",
    )?
    .ok_or_else(|| MailFfiError::OAuth2Refresh("missing refresh_token".into()))?;

    let params = build_refresh_params(&client_id, &client_secret, &refresh_token);

    let http = reqwest::blocking::Client::builder()
        .timeout(Duration::from_secs(30))
        .build()
        .map_err(|e| MailFfiError::OAuth2Refresh(format!("http client: {e}")))?;

    let resp = http
        .post(token_url)
        .form(&params)
        .send()
        .map_err(|e| MailFfiError::OAuth2Refresh(format!("request: {e}")))?;

    if !resp.status().is_success() {
        let status = resp.status();
        let body = resp.text().unwrap_or_default();
        let sanitized = sanitize_oauth_error_body(&body);
        let error_msg = if sanitized.is_empty() {
            format!("token endpoint returned {status}")
        } else {
            format!("token endpoint returned {status}: {sanitized}")
        };
        return Err(MailFfiError::OAuth2Refresh(error_msg));
    }

    let token: TokenResponse = resp
        .json()
        .map_err(|e| MailFfiError::OAuth2Refresh(format!("parse response: {e}")))?;

    let expires_at = token
        .expires_in
        .map(|secs| (Utc::now() + chrono::Duration::seconds(secs)).to_rfc3339());

    let new_access_token = token.access_token;
    let rotated_refresh_token = token.refresh_token;
    persist_refreshed_oauth2_credentials(
        conn,
        credential_key,
        &account.id,
        &new_access_token,
        rotated_refresh_token.as_deref(),
        expires_at.as_deref(),
    )?;

    Ok(new_access_token)
}

#[derive(serde::Deserialize)]
struct OAuthErrorPayload {
    error: Option<String>,
    error_description: Option<String>,
}

pub(crate) fn sanitize_oauth_error_body(body: &str) -> String {
    let mut msg = String::new();
    if let Ok(payload) = serde_json::from_str::<OAuthErrorPayload>(body) {
        if let Some(err) = payload.error {
            let trimmed = err.trim();
            if !trimmed.is_empty() {
                msg.push_str(trimmed);
            }
        }
        if let Some(desc) = payload.error_description {
            let trimmed = desc.trim();
            if !trimmed.is_empty() {
                if !msg.is_empty() {
                    msg.push_str(": ");
                }
                msg.push_str(trimmed);
            }
        }
    }
    if msg.chars().count() > 200 {
        msg.chars().take(200).collect()
    } else {
        msg
    }
}

pub(crate) fn token_endpoint(auth_type: &str) -> Option<&'static str> {
    let lower = auth_type.to_ascii_lowercase();
    if lower.contains("google") || lower.contains("gmail") {
        Some("https://oauth2.googleapis.com/token")
    } else if lower.contains("microsoft") || lower.contains("outlook") || lower.contains("office") {
        Some("https://login.microsoftonline.com/common/oauth2/v2.0/token")
    } else {
        None
    }
}

/// Best-effort logout that never surfaces an error (used on teardown paths).
pub fn safe_logout(mut client: ImapClient) {
    let _ = client.logout();
}

/// Create an account row from `request`, persist its secrets encrypted in
/// `encrypted_credentials` (keyed by the injected `credential_key`), and null
/// the plaintext secret columns on the row so secrets live only in the
/// encrypted table. Returns a secret-free [`AccountResponse`].
pub fn create_account(
    conn: &mut Connection,
    credential_key: &[u8; 32],
    request: CreateAccountRequest,
) -> Result<AccountResponse> {
    if request.email.trim().is_empty() {
        return Err(MailFfiError::InvalidRequest("email is required".into()));
    }
    if request.imap_host.trim().is_empty() {
        return Err(MailFfiError::InvalidRequest("imap_host is required".into()));
    }
    if request.smtp_host.trim().is_empty() {
        return Err(MailFfiError::InvalidRequest("smtp_host is required".into()));
    }

    let id = Uuid::new_v4().to_string();
    let auth_type = request
        .auth_type
        .clone()
        .filter(|value| !value.is_empty())
        .unwrap_or_else(|| "password".to_string());
    let now = chrono::Utc::now().format("%Y-%m-%d %H:%M:%S").to_string();

    let tx = conn.transaction()?;

    tx.execute(
        "INSERT INTO accounts (id, email, display_name, auth_type, imap_host, imap_port,
             imap_encryption, smtp_host, smtp_port, smtp_encryption, username, password,
             oauth2_client_id, oauth2_client_secret, oauth2_refresh_token, oauth2_access_token,
             oauth2_expires_at, created_at, updated_at)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, NULL, ?12, NULL, NULL, NULL, ?13, ?14, ?15)",
        rusqlite::params![
            id,
            request.email,
            request.display_name,
            auth_type,
            request.imap_host,
            request.imap_port,
            request.imap_encryption.as_str(),
            request.smtp_host,
            request.smtp_port,
            request.smtp_encryption.as_str(),
            request.username,
            request.oauth2_client_id,
            request.oauth2_expires_at,
            now,
            now,
        ],
    )?;

    for (credential_type, value) in [
        ("password", request.password.as_deref()),
        (
            "oauth2_access_token",
            request.oauth2_access_token.as_deref(),
        ),
        (
            "oauth2_refresh_token",
            request.oauth2_refresh_token.as_deref(),
        ),
        (
            "oauth2_client_secret",
            request.oauth2_client_secret.as_deref(),
        ),
    ] {
        if let Some(secret) = value.filter(|value| !value.is_empty()) {
            store_encrypted_credential(&tx, credential_key, &id, credential_type, secret)?;
        }
    }

    tx.commit()?;

    let account = load_account(conn, &id)?;
    crate::state::spawn_account_workers_if_ready(&id);
    crate::ffi::emit_accounts_changed_event(&id);
    Ok(AccountResponse::from_account(&account))
}

pub fn oauth_reauthorization_email(conn: &Connection, account_id: &str) -> Result<String> {
    let account = load_account(conn, account_id)?;
    if !matches!(
        account.auth_type.as_str(),
        "oauth2_gmail" | "oauth2_outlook"
    ) {
        return Err(MailFfiError::InvalidRequest(format!(
            "account {account_id} is not a supported OAuth account"
        )));
    }
    Ok(account.email)
}

pub fn replace_oauth_credentials(
    conn: &mut Connection,
    credential_key: &[u8; 32],
    account_id: &str,
    request: CreateAccountRequest,
) -> Result<AccountResponse> {
    let account = load_account(conn, account_id)?;
    if !matches!(
        account.auth_type.as_str(),
        "oauth2_gmail" | "oauth2_outlook"
    ) || request.auth_type.as_deref() != Some(account.auth_type.as_str())
    {
        return Err(MailFfiError::InvalidRequest(format!(
            "account {account_id} OAuth provider does not match"
        )));
    }
    if !account
        .email
        .trim()
        .eq_ignore_ascii_case(request.email.trim())
    {
        return Err(MailFfiError::InvalidRequest(format!(
            "OAuth account mismatch: expected {}, got {}",
            account.email, request.email
        )));
    }
    let access_token = request
        .oauth2_access_token
        .as_deref()
        .filter(|value| !value.is_empty())
        .ok_or_else(|| MailFfiError::OAuth("missing access_token".to_string()))?;
    let refresh_token = request
        .oauth2_refresh_token
        .as_deref()
        .filter(|value| !value.is_empty())
        .ok_or_else(|| MailFfiError::OAuth("missing refresh_token".to_string()))?;

    let tx = conn.transaction()?;
    tx.execute(
        "UPDATE accounts
         SET oauth2_client_id = ?1,
             oauth2_client_secret = NULL,
             oauth2_refresh_token = NULL,
             oauth2_access_token = NULL,
             oauth2_expires_at = ?2,
             updated_at = datetime('now')
         WHERE id = ?3",
        rusqlite::params![
            request.oauth2_client_id,
            request.oauth2_expires_at,
            account_id,
        ],
    )?;
    store_encrypted_credential(
        &tx,
        credential_key,
        account_id,
        "oauth2_access_token",
        access_token,
    )?;
    store_encrypted_credential(
        &tx,
        credential_key,
        account_id,
        "oauth2_refresh_token",
        refresh_token,
    )?;
    tx.execute(
        "DELETE FROM encrypted_credentials
         WHERE account_id = ?1 AND credential_type = 'oauth2_client_secret'",
        [account_id],
    )?;
    if let Some(client_secret) = request
        .oauth2_client_secret
        .as_deref()
        .filter(|value| !value.is_empty())
    {
        store_encrypted_credential(
            &tx,
            credential_key,
            account_id,
            "oauth2_client_secret",
            client_secret,
        )?;
    }
    tx.commit()?;

    crate::state::registry().stop_account(account_id);
    crate::state::spawn_account_workers_if_ready(account_id);
    crate::ffi::emit_refresh_succeeded_event(account_id);
    crate::ffi::emit_accounts_changed_event(account_id);
    Ok(AccountResponse::from_account(&load_account(
        conn, account_id,
    )?))
}

/// Stop any running sync/backfill workers for `account_id`, delete every
/// persisted S/MIME key while its database references are still available,
/// then remove all database state in a single transaction.
pub fn delete_account(ctx: &AppCtx, account_id: &str) -> Result<()> {
    crate::state::registry().stop_account(account_id);

    let mut conn = ctx
        .pool
        .get()
        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
    let tx = conn.transaction()?;
    let smime_identity_ids =
        crate::ffi::crypto_api::cleanup_smime_keys_for_account(ctx, &tx, account_id)?;
    tx.execute(
        "DELETE FROM attachments WHERE email_id IN (SELECT id FROM emails WHERE account_id = ?1)",
        [account_id],
    )?;
    tx.execute("DELETE FROM emails WHERE account_id = ?1", [account_id])?;
    tx.execute("DELETE FROM folders WHERE account_id = ?1", [account_id])?;
    for identity_id in &smime_identity_ids {
        tx.execute(
            "DELETE FROM encrypted_credentials WHERE account_id = ?1 AND credential_type = 'smime_private_key'",
            [identity_id],
        )?;
    }
    tx.execute(
        "DELETE FROM encrypted_credentials WHERE account_id = ?1",
        [account_id],
    )?;
    tx.execute("DELETE FROM accounts WHERE id = ?1", [account_id])?;
    tx.commit()?;
    crate::ffi::emit_accounts_changed_event(account_id);
    Ok(())
}

pub fn reconnect_account_with_runtime(
    ctx: &Arc<AppCtx>,
    account_id: &str,
    rt: Option<&tokio::runtime::Runtime>,
) -> Result<()> {
    crate::state::registry().stop_account(account_id);
    let conn = ctx
        .pool
        .get()
        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
    resolve_account_auth(&conn, &ctx.credential_key, account_id)?;
    let rt = rt.ok_or(MailFfiError::NotInitialized)?;
    crate::state::start_account_workers(rt, ctx, account_id);
    crate::ffi::emit_accounts_changed_event(account_id);
    Ok(())
}

/// Stop any running sync/backfill workers for `account_id`, then re-resolve the
/// account's IMAP auth (refreshing OAuth2 tokens if expired) so the next sync
/// starts from a healthy session. Recovers a stale account without deleting
/// local data.
pub fn reconnect_account(ctx: &Arc<AppCtx>, account_id: &str) -> Result<()> {
    reconnect_account_with_runtime(ctx, account_id, crate::runtime::runtime())
}

/// Transient IMAP probe parameters (no persisted account row). Parsed at the
/// FFI boundary from JSON.
#[derive(Debug, Clone, Deserialize)]
pub struct ProbeParams {
    pub imap_host: String,
    pub imap_port: u16,
    pub imap_encryption: Encryption,
    pub username: String,
    #[serde(default)]
    pub auth_type: Option<String>,
    #[serde(default)]
    pub password: Option<String>,
    #[serde(default)]
    pub oauth2_access_token: Option<String>,
}

/// Test an IMAP connection for onboarding using transient credentials, without
/// touching the database. Malformed parameters are rejected before any network
/// call (the unit-testable seam); a real connect failure is surfaced as a typed
/// error rather than a panic. Blocking — call from a blocking context.
pub fn probe_imap_connection(params: &ProbeParams) -> Result<()> {
    let auth = resolve_probe_auth(params)?;
    let client = connect_probe(params, &auth)?;
    safe_logout(client);
    Ok(())
}

fn resolve_probe_auth(params: &ProbeParams) -> Result<ImapAuth> {
    if params.imap_host.trim().is_empty() {
        return Err(MailFfiError::InvalidRequest("imap_host is required".into()));
    }
    if params.username.trim().is_empty() {
        return Err(MailFfiError::InvalidRequest("username is required".into()));
    }

    let is_oauth = params
        .auth_type
        .as_deref()
        .is_some_and(|auth_type| auth_type.starts_with("oauth2"));
    if is_oauth {
        let token = params.oauth2_access_token.as_deref().unwrap_or_default();
        if token.is_empty() {
            return Err(MailFfiError::InvalidRequest(
                "oauth2_access_token is required".into(),
            ));
        }
        Ok(ImapAuth::OAuth2 {
            access_token: token.to_string(),
        })
    } else {
        let password = params.password.as_deref().unwrap_or_default();
        if password.is_empty() {
            return Err(MailFfiError::InvalidRequest("password is required".into()));
        }
        Ok(ImapAuth::Password(password.to_string()))
    }
}

fn connect_probe(params: &ProbeParams, auth: &ImapAuth) -> Result<ImapClient> {
    let client = match auth {
        ImapAuth::Password(password) => ImapClient::connect(
            &params.imap_host,
            params.imap_port,
            &params.imap_encryption,
            &params.username,
            password,
        )?,
        ImapAuth::OAuth2 { access_token } => ImapClient::connect_oauth2(
            &params.imap_host,
            params.imap_port,
            &params.imap_encryption,
            &params.username,
            access_token,
        )?,
    };
    Ok(client)
}

#[cfg(test)]
#[path = "account_review_tests.rs"]
mod account_review_tests;

#[cfg(test)]
#[allow(clippy::disallowed_methods)]
mod tests {
    use super::*;

    #[test]
    fn sanitize_oauth_error_body_does_not_leak_secrets_in_raw_body() {
        let raw_body =
            "refresh_token=secret_refresh_token_12345&client_secret=secret_xyz".repeat(50);
        assert!(raw_body.len() > 2000);
        let sanitized = sanitize_oauth_error_body(&raw_body);
        assert!(
            !sanitized.contains("refresh_token=secret_refresh_token"),
            "sanitized error must not contain secret tokens from raw response body"
        );
        assert!(
            !sanitized.contains("client_secret=secret_xyz"),
            "sanitized error must not contain client secrets"
        );
        assert!(
            sanitized.len() <= 200,
            "sanitized error must be <= 200 chars"
        );
    }

    #[test]
    fn sanitize_oauth_error_body_parses_standard_oauth_fields() {
        let json_body = r#"{"error":"invalid_grant","error_description":"Token has been expired or revoked.","sensitive_data":"super_secret_leak"}"#;
        let sanitized = sanitize_oauth_error_body(json_body);
        assert!(sanitized.contains("invalid_grant"));
        assert!(sanitized.contains("Token has been expired or revoked."));
        assert!(
            !sanitized.contains("super_secret_leak"),
            "non-standard fields must not be leaked"
        );
        assert!(sanitized.len() <= 200);
    }

    #[test]
    fn sanitize_oauth_error_body_truncates_long_description() {
        let long_desc = "A".repeat(500);
        let json_body =
            format!(r#"{{"error":"invalid_request","error_description":"{long_desc}"}}"#);
        let sanitized = sanitize_oauth_error_body(&json_body);
        assert!(sanitized.starts_with("invalid_request"));
        assert!(sanitized.len() <= 200);
    }

    #[test]
    fn token_endpoint_detection() {
        assert!(token_endpoint("oauth2_gmail")
            .unwrap()
            .contains("googleapis"));
        assert!(token_endpoint("oauth2_google")
            .unwrap()
            .contains("googleapis"));
        assert!(token_endpoint("oauth2_outlook")
            .unwrap()
            .contains("microsoftonline"));
        assert!(token_endpoint("oauth2_microsoft")
            .unwrap()
            .contains("microsoftonline"));
        assert!(token_endpoint("oauth2_unknown").is_none());
    }

    #[test]
    fn expiry_detection() {
        let past = (Utc::now() - chrono::Duration::hours(1)).to_rfc3339();
        let future = (Utc::now() + chrono::Duration::hours(1)).to_rfc3339();
        assert!(is_expired(&past));
        assert!(!is_expired(&future));
        assert!(is_expired("not-a-date"));
    }

    #[test]
    fn classify_reauth_reasons_is_stable() {
        assert_eq!(
            classify_reauth_reason(&MailFfiError::OAuth2Refresh("missing refresh_token".into())),
            "refresh_token_missing"
        );
        assert_eq!(
            classify_reauth_reason(&MailFfiError::OAuth2Refresh(
                "token endpoint returned 400 Bad Request: {\"error\":\"invalid_grant\"}".into()
            )),
            "invalid_grant"
        );
        assert_eq!(
            classify_reauth_reason(&MailFfiError::OAuth2Refresh(
                "token endpoint returned 401 Unauthorized: {\"error\":\"invalid_client\"}".into()
            )),
            "invalid_client"
        );
        assert_eq!(
            classify_reauth_reason(&MailFfiError::OAuth2Refresh("something else".into())),
            "unknown"
        );
    }

    #[test]
    fn refresh_params_omit_empty_client_secret() {
        let params = build_refresh_params("client-id", "", "refresh-tok");
        assert!(
            !params.iter().any(|(k, _)| *k == "client_secret"),
            "empty client_secret must be omitted for public/desktop PKCE clients"
        );
        assert!(params.contains(&("grant_type", "refresh_token")));
        assert!(params.contains(&("client_id", "client-id")));
        assert!(params.contains(&("refresh_token", "refresh-tok")));
    }

    #[test]
    fn refresh_params_include_present_client_secret() {
        let params = build_refresh_params("client-id", "shhh", "refresh-tok");
        assert!(
            params.contains(&("client_secret", "shhh")),
            "non-empty client_secret must be forwarded for confidential clients"
        );
    }

    fn migrated_conn() -> Connection {
        let conn = Connection::open_in_memory().unwrap();
        maho_core::db::migrations::run_migrations(&conn).unwrap();
        conn
    }

    fn password_request(email: &str) -> CreateAccountRequest {
        CreateAccountRequest {
            email: email.to_string(),
            display_name: "New User".to_string(),
            auth_type: Some("password".to_string()),
            imap_host: "imap.example.com".to_string(),
            imap_port: 993,
            imap_encryption: Encryption::Tls,
            smtp_host: "smtp.example.com".to_string(),
            smtp_port: 587,
            smtp_encryption: Encryption::StartTls,
            username: email.to_string(),
            password: Some("hunter2".to_string()),
            oauth2_client_id: None,
            oauth2_client_secret: None,
            oauth2_access_token: None,
            oauth2_refresh_token: None,
            oauth2_expires_at: None,
        }
    }

    #[test]
    fn create_account_persists_row_and_encrypts_password() {
        let mut conn = migrated_conn();
        let key = [21u8; 32];

        let response =
            create_account(&mut conn, &key, password_request("new@example.com")).unwrap();

        let loaded = load_account(&conn, &response.id).unwrap();
        assert_eq!(loaded.email, "new@example.com");
        assert_eq!(loaded.auth_type, "password");
        assert!(
            loaded.password.is_none(),
            "plaintext password column must be nulled"
        );

        let recovered = get_encrypted_credential(&conn, &key, &response.id, "password").unwrap();
        assert_eq!(recovered.as_deref(), Some("hunter2"));
    }

    #[test]
    fn create_account_rejects_empty_email() {
        let mut conn = migrated_conn();
        let mut request = password_request("new@example.com");
        request.email = String::new();
        assert!(matches!(
            create_account(&mut conn, &[0u8; 32], request),
            Err(MailFfiError::InvalidRequest(_))
        ));
    }

    #[test]
    fn create_account_rollback_on_failure() {
        let mut conn = migrated_conn();
        let key = [21u8; 32];

        let count_accounts = |c: &Connection| -> i64 {
            c.query_row("SELECT COUNT(*) FROM accounts", [], |r| r.get(0))
                .unwrap()
        };
        let count_creds = |c: &Connection| -> i64 {
            c.query_row("SELECT COUNT(*) FROM encrypted_credentials", [], |r| {
                r.get(0)
            })
            .unwrap()
        };

        let initial_accounts = count_accounts(&conn);
        let initial_creds = count_creds(&conn);

        // Install trigger that fails insertion on encrypted_credentials
        conn.execute(
            "CREATE TRIGGER fail_cred BEFORE INSERT ON encrypted_credentials
             BEGIN
                 SELECT RAISE(ABORT, 'forced credential failure');
             END;",
            [],
        )
        .unwrap();

        let result = create_account(&mut conn, &key, password_request("fail@example.com"));
        assert!(result.is_err());

        assert_eq!(
            count_accounts(&conn),
            initial_accounts,
            "accounts table must be rolled back"
        );
        assert_eq!(
            count_creds(&conn),
            initial_creds,
            "creds table must be rolled back"
        );
    }

    #[test]
    fn create_account_secrets_are_null_from_start() {
        let mut conn = migrated_conn();
        let key = [21u8; 32];
        let response = create_account(
            &mut conn,
            &key,
            password_request("null_from_start@example.com"),
        )
        .unwrap();

        // Directly query DB to make sure secrets are NULL
        let (pw, client_sec, ref_tok, acc_tok): (Option<String>, Option<String>, Option<String>, Option<String>) = conn.query_row(
            "SELECT password, oauth2_client_secret, oauth2_refresh_token, oauth2_access_token FROM accounts WHERE id = ?1",
            [&response.id],
            |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?, r.get(3)?))
        ).unwrap();

        assert!(pw.is_none());
        assert!(client_sec.is_none());
        assert!(ref_tok.is_none());
        assert!(acc_tok.is_none());
    }

    #[test]
    fn persist_refreshed_oauth2_credentials_encrypts_tokens_and_nulls_plaintext_columns() {
        let mut conn = migrated_conn();
        let key = [17u8; 32];
        let request = CreateAccountRequest {
            email: "oauth@example.com".to_string(),
            display_name: "OAuth User".to_string(),
            auth_type: Some("oauth2_gmail".to_string()),
            imap_host: "imap.gmail.com".to_string(),
            imap_port: 993,
            imap_encryption: Encryption::Tls,
            smtp_host: "smtp.gmail.com".to_string(),
            smtp_port: 587,
            smtp_encryption: Encryption::StartTls,
            username: "oauth@example.com".to_string(),
            password: None,
            oauth2_client_id: Some("cid".to_string()),
            oauth2_client_secret: Some("secret".to_string()),
            oauth2_access_token: Some("initial-access".to_string()),
            oauth2_refresh_token: Some("initial-refresh".to_string()),
            oauth2_expires_at: Some("2026-01-01T00:00:00Z".to_string()),
        };
        let response = create_account(&mut conn, &key, request).unwrap();

        persist_refreshed_oauth2_credentials(
            &conn,
            &key,
            &response.id,
            "refreshed-access",
            Some("rotated-refresh"),
            Some("2026-02-01T00:00:00Z"),
        )
        .unwrap();

        let loaded = load_account(&conn, &response.id).unwrap();
        assert!(loaded.oauth2_access_token.is_none());
        assert!(loaded.oauth2_refresh_token.is_none());
        assert_eq!(
            loaded.oauth2_expires_at.as_deref(),
            Some("2026-02-01T00:00:00Z")
        );
        assert_eq!(
            get_encrypted_credential(&conn, &key, &response.id, "oauth2_access_token")
                .unwrap()
                .as_deref(),
            Some("refreshed-access")
        );
        assert_eq!(
            get_encrypted_credential(&conn, &key, &response.id, "oauth2_refresh_token")
                .unwrap()
                .as_deref(),
            Some("rotated-refresh")
        );
    }

    #[test]
    fn resolve_account_auth_does_not_return_known_expired_token_after_refresh_failure() {
        let mut conn = migrated_conn();
        let key = [18u8; 32];
        let request = CreateAccountRequest {
            email: "expired@example.com".to_string(),
            display_name: "Expired OAuth".to_string(),
            auth_type: Some("oauth2_gmail".to_string()),
            imap_host: "imap.gmail.com".to_string(),
            imap_port: 993,
            imap_encryption: Encryption::Tls,
            smtp_host: "smtp.gmail.com".to_string(),
            smtp_port: 587,
            smtp_encryption: Encryption::StartTls,
            username: "expired@example.com".to_string(),
            password: None,
            oauth2_client_id: Some("cid".to_string()),
            oauth2_client_secret: None,
            oauth2_access_token: Some("known-expired-token".to_string()),
            oauth2_refresh_token: None,
            oauth2_expires_at: Some((Utc::now() - chrono::Duration::hours(1)).to_rfc3339()),
        };
        let response = create_account(&mut conn, &key, request).unwrap();

        let result = resolve_account_auth(&conn, &key, &response.id);
        assert!(matches!(
            result,
            Err(MailFfiError::OAuth2Refresh(message)) if message.contains("missing refresh_token")
        ));
    }

    #[test]
    fn delete_account_removes_row_and_all_children() {
        let pool = crate::test_support::pool_with_seeded_data();
        let mut ctx = crate::test_support::ctx(pool);
        ctx.db_path = std::env::temp_dir()
            .join("profile")
            .join("MahoMail")
            .join("maho_mail.db");
        {
            let conn = ctx.pool.get().unwrap();
            store_encrypted_credential(&conn, &ctx.credential_key, "acc1", "password", "pw")
                .unwrap();
            conn.execute(
                "INSERT INTO smime_identities
                 (id, account_id, email, subject, issuer, serial_number, fingerprint, not_before, not_after, cert_pem, is_default)
                 VALUES ('smime-1', 'acc1', 'a@example.com', 'subject', 'issuer', '01', 'fingerprint', 'before', 'after', 'certificate', 0)",
                [],
            )
            .unwrap();
            store_encrypted_credential(
                &conn,
                &ctx.credential_key,
                "smime-1",
                "smime_private_key",
                "private-key",
            )
            .unwrap();
        }

        delete_account(&ctx, "acc1").unwrap();

        let conn = ctx.pool.get().unwrap();
        assert!(matches!(
            load_account(&conn, "acc1"),
            Err(MailFfiError::AccountNotFound(_))
        ));
        let counts = |table: &str| -> i64 {
            conn.query_row(
                &format!("SELECT COUNT(*) FROM {table} WHERE account_id = 'acc1'"),
                [],
                |row| row.get(0),
            )
            .unwrap()
        };
        assert_eq!(counts("encrypted_credentials"), 0);
        assert_eq!(counts("emails"), 0);
        assert_eq!(counts("folders"), 0);
        assert_eq!(counts("smime_identities"), 0);
        assert_eq!(
            conn.query_row(
                "SELECT COUNT(*) FROM encrypted_credentials WHERE account_id = 'smime-1'",
                [],
                |row| row.get::<_, i64>(0),
            )
            .unwrap(),
            0,
            "identity-scoped S/MIME credential must be removed before its identity reference"
        );
    }

    fn probe_params(encryption: Encryption, host: &str, password: Option<&str>) -> ProbeParams {
        ProbeParams {
            imap_host: host.to_string(),
            imap_port: 993,
            imap_encryption: encryption,
            username: "u@example.com".to_string(),
            auth_type: None,
            password: password.map(str::to_string),
            oauth2_access_token: None,
        }
    }

    #[test]
    fn probe_rejects_empty_host_before_connect() {
        let params = probe_params(Encryption::Tls, "", Some("pw"));
        assert!(matches!(
            probe_imap_connection(&params),
            Err(MailFfiError::InvalidRequest(_))
        ));
    }

    #[test]
    fn probe_rejects_missing_password() {
        let params = probe_params(Encryption::Tls, "imap.example.com", None);
        assert!(matches!(
            probe_imap_connection(&params),
            Err(MailFfiError::InvalidRequest(_))
        ));
    }

    #[test]
    fn probe_rejects_missing_oauth_token() {
        let mut params = probe_params(Encryption::Tls, "imap.example.com", None);
        params.auth_type = Some("oauth2_gmail".to_string());
        assert!(matches!(
            probe_imap_connection(&params),
            Err(MailFfiError::InvalidRequest(_))
        ));
    }

    #[test]
    fn probe_plaintext_encryption_is_rejected_deterministically() {
        let params = probe_params(Encryption::None, "imap.example.com", Some("pw"));
        assert!(probe_imap_connection(&params).is_err());
    }

    #[test]
    fn reconnect_resolves_existing_password_account() {
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx_arc(pool);
        assert!(reconnect_account(&ctx, "acc1").is_ok());
    }

    #[test]
    fn reconnect_unknown_account_errors() {
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx_arc(pool);
        assert!(matches!(
            reconnect_account(&ctx, "ghost"),
            Err(MailFfiError::AccountNotFound(_))
        ));
    }

    #[test]
    fn reconnect_with_no_runtime_returns_not_initialized() {
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx_arc(pool);
        assert!(matches!(
            reconnect_account_with_runtime(&ctx, "acc1", None),
            Err(MailFfiError::NotInitialized)
        ));
    }

    #[test]
    fn create_account_auto_start_seam_registers_workers_once() {
        let rt = crate::runtime::runtime().expect("runtime");
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx_arc(pool);

        let mut conn = ctx.pool.get().unwrap();
        let response = create_account(
            &mut conn,
            &ctx.credential_key,
            password_request("auto@example.com"),
        )
        .unwrap();
        drop(conn);

        // `create_account` opportunistically auto-starts through the
        // process-global context. Other tests may have initialized that
        // context, so remove that optional worker before exercising this
        // test's explicit local-worker seam.
        crate::state::registry().stop_account(&response.id);

        let (sync, backfill) = crate::state::start_account_workers(rt, &ctx, &response.id);
        assert!(sync && backfill, "first auto-start spawns both workers");
        assert!(crate::state::registry().is_sync_running(&response.id));

        let (sync_again, _) = crate::state::start_account_workers(rt, &ctx, &response.id);
        assert!(
            !sync_again,
            "second auto-start does not respawn the live sync worker"
        );

        crate::state::registry().stop_account(&response.id);
    }
}

#[cfg(test)]
#[path = "account_auth_retry_review_tests.rs"]
mod auth_retry_review_tests;
