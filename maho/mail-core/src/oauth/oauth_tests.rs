// Copyright 2026 Maho Browser. All rights reserved.

#![allow(clippy::disallowed_methods)]

use std::cell::Cell;
use std::time::Instant;

use rusqlite::Connection;

use crate::account;
use crate::state::{self, PkceEntry};

use super::transport::{OAuthTransport, TokenExchange, TokenExchangeRequest, UserInfo};
use super::{
    build_authorization_url, generate_pkce, is_allowed_redirect_uri, pkce_challenge,
    resolve_client_id,
    resolve_client_secret, start_oauth, OAuthCompletion,
};

fn migrated_conn() -> Connection {
    let conn = Connection::open_in_memory().unwrap();
    maho_core::db::migrations::run_migrations(&conn).unwrap();
    conn
}

fn insert_pending(state_token: &str, provider: &str) {
    state::pending_pkce().insert(
        state_token.to_string(),
        PkceEntry {
            code_verifier: "verifier".to_string(),
            provider: provider.to_string(),
            client_id: "cid".to_string(),
            client_secret: String::new(),
            redirect_uri: "http://localhost".to_string(),
            created_at: Instant::now(),
        },
    );
}

struct FakeTransport {
    email: String,
    refresh_token: Option<String>,
    exchange_calls: Cell<u32>,
    userinfo_calls: Cell<u32>,
}

impl FakeTransport {
    fn new(email: &str, refresh_token: Option<&str>) -> Self {
        Self {
            email: email.to_string(),
            refresh_token: refresh_token.map(str::to_string),
            exchange_calls: Cell::new(0),
            userinfo_calls: Cell::new(0),
        }
    }
}

impl OAuthTransport for FakeTransport {
    fn exchange_code(&self, _request: &TokenExchangeRequest<'_>) -> super::Result<TokenExchange> {
        self.exchange_calls.set(self.exchange_calls.get() + 1);
        Ok(TokenExchange {
            access_token: "access-tok".to_string(),
            refresh_token: self.refresh_token.clone(),
        })
    }

    fn fetch_userinfo(&self, _userinfo_url: &str, _access_token: &str) -> super::Result<UserInfo> {
        self.userinfo_calls.set(self.userinfo_calls.get() + 1);
        Ok(UserInfo {
            email: self.email.clone(),
            name: "OAuth User".to_string(),
        })
    }
}

#[test]
fn pkce_challenge_matches_rfc7636_vector() {
    assert_eq!(
        pkce_challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
        "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"
    );
}

#[test]
fn generate_pkce_verifier_is_43_chars_and_challenge_derives() {
    let (verifier, challenge) = generate_pkce();
    assert_eq!(verifier.len(), 43);
    assert!(!verifier.contains('='));
    assert_eq!(challenge, pkce_challenge(&verifier));
}

#[test]
fn build_authorization_url_gmail_contains_expected_params() {
    let url = build_authorization_url(
        "gmail",
        "cid-1",
        "http://localhost",
        "chal-abc",
        "state-xyz",
        "https://mail.google.com/ openid email profile",
    )
    .unwrap();
    assert!(url.starts_with("https://accounts.google.com/o/oauth2/v2/auth?"));
    assert!(url.contains("client_id=cid-1"));
    assert!(url.contains("code_challenge=chal-abc"));
    assert!(url.contains("code_challenge_method=S256"));
    assert!(url.contains("state=state-xyz"));
    assert!(url.contains("mail.google.com"));
}

#[test]
fn build_authorization_url_outlook_uses_microsoft_endpoint() {
    let url = build_authorization_url(
        "outlook",
        "cid-2",
        "http://localhost",
        "chal",
        "st",
        "https://outlook.office365.com/IMAP.AccessAsUser.All offline_access",
    )
    .unwrap();
    assert!(url.starts_with("https://login.microsoftonline.com/common/oauth2/v2.0/authorize?"));
    assert!(url.contains("outlook.office365.com"));
    assert!(url.contains("code_challenge_method=S256"));
}

#[test]
fn build_authorization_url_unknown_provider_errors() {
    assert!(build_authorization_url("yahoo", "c", "r", "ch", "s", "sc").is_err());
}

#[test]
fn start_oauth_registers_pending_entry_roundtrip() {
    let response = start_oauth("gmail", "client-123", "http://localhost:1234").unwrap();
    assert!(response.auth_url.contains("client-123"));

    let entry = state::pending_pkce()
        .take(&response.state)
        .expect("pending entry registered");
    assert_eq!(entry.provider, "gmail");
    assert_eq!(entry.client_id, "client-123");
    assert_eq!(entry.redirect_uri, "http://localhost:1234");
    assert!(response.auth_url.contains(&pkce_challenge(&entry.code_verifier)));
}

#[test]
fn resolve_client_id_ignores_caller_value_for_baked_providers() {
    // Security contract: a caller-supplied client_id must never override the
    // baked identity for a known provider, otherwise the consent screen can be
    // pointed at a third-party OAuth app. Where no identity is baked, the caller
    // value is the only source and is still honored.
    for provider in ["gmail", "outlook"] {
        let baked = match provider {
            "gmail" => option_env!("GMAIL_CLIENT_ID"),
            "outlook" => option_env!("OUTLOOK_CLIENT_ID"),
            _ => None,
        }
        .filter(|value| !value.is_empty());

        let resolved = resolve_client_id(provider, "attacker-cid").unwrap();
        match baked {
            Some(id) => assert_eq!(
                resolved, id,
                "{provider}: baked client_id must win over the caller value"
            ),
            None => assert_eq!(
                resolved, "attacker-cid",
                "{provider}: with no baked identity the caller value is used"
            ),
        }
    }
}

#[test]
fn start_oauth_rejects_non_loopback_redirect_uri() {
    // The redirect target is embedded in the authorization URL, so a remote
    // redirect would have the provider deliver the code to an endpoint the
    // caller controls.
    for bad in [
        "https://evil.example/callback",
        "http://evil.example/callback",
        "not-a-url",
    ] {
        assert!(
            matches!(
                start_oauth("gmail", "", bad),
                Err(super::MailFfiError::InvalidRequest(_))
            ),
            "redirect_uri {bad} must be rejected"
        );
    }
    assert!(is_allowed_redirect_uri("http://127.0.0.1:1234"));
    assert!(is_allowed_redirect_uri("http://localhost:1234"));
    assert!(!is_allowed_redirect_uri("https://localhost:1234"));
}

#[test]
fn resolve_client_id_empty_without_baked_default_errors() {
    // outlook has no OUTLOOK_CLIENT_ID baked in this workspace's mail/.env, so
    // an empty client_id must surface a typed InvalidRequest (never a panic).
    match option_env!("OUTLOOK_CLIENT_ID") {
        Some(id) if !id.is_empty() => {
            assert_eq!(resolve_client_id("outlook", "").unwrap(), id);
        }
        _ => {
            assert!(matches!(
                resolve_client_id("outlook", ""),
                Err(super::MailFfiError::InvalidRequest(_))
            ));
        }
    }
}

#[test]
fn resolve_client_id_empty_gmail_uses_baked_default_when_present() {
    match option_env!("GMAIL_CLIENT_ID") {
        Some(id) if !id.is_empty() => {
            assert_eq!(resolve_client_id("gmail", "").unwrap(), id);
            let response = start_oauth("gmail", "", "http://localhost:1234").unwrap();
            assert!(response.auth_url.contains(id));
            let _ = state::pending_pkce().take(&response.state);
        }
        _ => {
            assert!(matches!(
                resolve_client_id("gmail", ""),
                Err(super::MailFfiError::InvalidRequest(_))
            ));
        }
    }
}

#[test]
fn complete_unknown_state_errors_before_network() {
    let mut conn = migrated_conn();
    let key = [9u8; 32];
    let fake = FakeTransport::new("u@gmail.com", Some("rt"));
    let result = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &fake,
    }
    .complete("no-such-state", "code");
    assert!(matches!(result, Err(super::MailFfiError::InvalidState(_))));
    assert_eq!(fake.exchange_calls.get(), 0);
}

#[test]
fn complete_empty_code_errors_before_network() {
    let mut conn = migrated_conn();
    let key = [9u8; 32];
    insert_pending("empty-code-state", "gmail");
    let fake = FakeTransport::new("u@gmail.com", Some("rt"));
    let result = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &fake,
    }
    .complete("empty-code-state", "   ");
    assert!(result.is_err());
    assert_eq!(fake.exchange_calls.get(), 0);
}

#[test]
fn complete_success_creates_account_and_stores_refresh_token() {
    let mut conn = migrated_conn();
    let key = [9u8; 32];
    insert_pending("success-state", "gmail");
    let fake = FakeTransport::new("newuser@gmail.com", Some("refresh-xyz"));

    let response = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &fake,
    }
    .complete("success-state", "authcode")
    .unwrap();

    assert_eq!(response.email, "newuser@gmail.com");
    assert_eq!(response.auth_type, "oauth2_gmail");
    assert_eq!(response.imap_host, "imap.gmail.com");

    let loaded = account::load_account(&conn, &response.id).unwrap();
    assert_eq!(loaded.email, "newuser@gmail.com");
    assert!(
        loaded.oauth2_refresh_token.is_none(),
        "plaintext refresh column must be nulled"
    );

    let stored = crate::credentials::get_encrypted_credential(
        &conn,
        &key,
        &response.id,
        "oauth2_refresh_token",
    )
    .unwrap();
    assert_eq!(stored.as_deref(), Some("refresh-xyz"));
    assert_eq!(fake.exchange_calls.get(), 1);
    assert_eq!(fake.userinfo_calls.get(), 1);
}

#[test]
fn baked_client_secret_is_never_used() {
    // Security (T2): a client secret must NEVER be compiled into the binary.
    // Even if GMAIL_CLIENT_SECRET/OUTLOOK_CLIENT_SECRET are present in the build
    // environment, an empty caller secret must resolve to "" (public/PKCE client)
    // rather than a baked secret that `strings` could extract.
    assert_eq!(resolve_client_secret("gmail", ""), "");
    assert_eq!(resolve_client_secret("outlook", ""), "");
    // An explicit per-account secret still passes through unchanged.
    assert_eq!(resolve_client_secret("gmail", "explicit"), "explicit");
}
