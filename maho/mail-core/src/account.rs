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

use std::time::Duration;

use chrono::{DateTime, Utc};
use maho_core::imap_client::ImapClient;
use maho_core::models::account::Account;
use rusqlite::Connection;

use crate::credentials::get_encrypted_credential;
use crate::error::{MailFfiError, Result};

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
        Ok(new_token) => Ok(new_token),
        Err(refresh_err) => match current {
            Some(token) => {
                log::warn!(
                    "[mail-ffi] oauth2 refresh failed for {}, using existing token: {refresh_err}",
                    account.id
                );
                Ok(token)
            }
            None => Err(refresh_err),
        },
    }
}

fn is_expired(expires_at: &str) -> bool {
    match DateTime::parse_from_rfc3339(expires_at) {
        Ok(dt) => {
            let with_margin = dt.with_timezone(&Utc) - chrono::Duration::minutes(5);
            Utc::now() >= with_margin
        }
        Err(_) => false,
    }
}

#[derive(serde::Deserialize)]
struct TokenResponse {
    access_token: String,
    expires_in: Option<i64>,
    refresh_token: Option<String>,
}

/// Refresh an OAuth2 access token via the provider token endpoint and persist
/// it. Blocking (uses `reqwest::blocking`); call from a blocking context.
fn refresh_oauth2_token(
    conn: &Connection,
    credential_key: &[u8; 32],
    account: &Account,
) -> Result<String> {
    let token_url = token_endpoint(&account.auth_type).ok_or_else(|| {
        MailFfiError::OAuth2Refresh(format!("unknown provider {}", account.auth_type))
    })?;

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

    let params = [
        ("grant_type", "refresh_token"),
        ("client_id", client_id.as_str()),
        ("client_secret", client_secret.as_str()),
        ("refresh_token", refresh_token.as_str()),
    ];

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
        let body = match resp.text() {
            Ok(text) => text,
            Err(err) => format!("<failed to read error body: {err}>"),
        };
        return Err(MailFfiError::OAuth2Refresh(format!(
            "token endpoint returned {status}: {body}"
        )));
    }

    let token: TokenResponse = resp
        .json()
        .map_err(|e| MailFfiError::OAuth2Refresh(format!("parse response: {e}")))?;

    let expires_at = token
        .expires_in
        .map(|secs| (Utc::now() + chrono::Duration::seconds(secs)).to_rfc3339());

    conn.execute(
        "UPDATE accounts SET oauth2_access_token = ?1, oauth2_expires_at = ?2,
             oauth2_refresh_token = COALESCE(?3, oauth2_refresh_token),
             updated_at = datetime('now')
         WHERE id = ?4",
        rusqlite::params![
            token.access_token,
            expires_at,
            token.refresh_token,
            account.id
        ],
    )
    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

    Ok(token.access_token)
}

fn token_endpoint(auth_type: &str) -> Option<&'static str> {
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

#[cfg(test)]
#[allow(clippy::disallowed_methods)]
mod tests {
    use super::*;

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
        assert!(!is_expired("not-a-date"));
    }
}
