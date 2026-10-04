// Copyright 2026 Maho Browser. All rights reserved.

#![allow(clippy::disallowed_methods)]

use std::cell::Cell;
use std::time::Instant;

use rusqlite::Connection;

use crate::account;
use crate::state::{self, PkceEntry};

use super::transport::{OAuthTransport, TokenExchange, TokenExchangeRequest, UserInfo};
use super::{
    build_authorization_url, build_authorization_url_with_options, generate_pkce,
    is_allowed_redirect_uri, pkce_challenge, resolve_client_id, resolve_client_secret, start_oauth,
    start_oauth_with_options, CompleteOAuthOptions, OAuthCompletion, StartOAuthOptions,
};

pub(super) fn migrated_conn() -> Connection {
    let conn = Connection::open_in_memory().unwrap();
    maho_core::db::migrations::run_migrations(&conn).unwrap();
    conn
}

pub(super) fn insert_pending(state_token: &str, provider: &str) {
    state::pending_pkce().insert(
        state_token.to_string(),
        PkceEntry {
            code_verifier: "verifier".to_string(),
            provider: provider.to_string(),
            client_id: "cid".to_string(),
            client_secret: String::new(),
            redirect_uri: "http://localhost".to_string(),
            expected_google_sub: None,
            reauthorize_account_id: None,
            created_at: Instant::now(),
        },
    );
}

fn insert_pending_reauthorization(state_token: &str, account_id: &str) {
    state::pending_pkce().insert(
        state_token.to_string(),
        PkceEntry {
            code_verifier: "verifier".to_string(),
            provider: "gmail".to_string(),
            client_id: "cid".to_string(),
            client_secret: String::new(),
            redirect_uri: "http://localhost".to_string(),
            expected_google_sub: None,
            reauthorize_account_id: Some(account_id.to_string()),
            created_at: Instant::now(),
        },
    );
}

pub(super) struct FakeTransport {
    subject: Option<String>,
    email: String,
    refresh_token: Option<String>,
    expires_at: Option<String>,
    pub(super) exchange_calls: Cell<u32>,
    userinfo_calls: Cell<u32>,
}

impl FakeTransport {
    pub(super) fn new(email: &str, refresh_token: Option<&str>) -> Self {
        Self {
            subject: Some("google-subject-1".to_string()),
            email: email.to_string(),
            refresh_token: refresh_token.map(str::to_string),
            expires_at: None,
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
            expires_at: self.expires_at.clone(),
        })
    }

    fn fetch_userinfo(&self, _userinfo_url: &str, _access_token: &str) -> super::Result<UserInfo> {
        self.userinfo_calls.set(self.userinfo_calls.get() + 1);
        Ok(UserInfo {
            subject: self.subject.clone(),
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
fn gmail_authorization_url_supports_encoded_login_hint_and_incremental_grants() {
    let url = build_authorization_url_with_options(
        "gmail",
        "cid-1",
        "http://localhost",
        "chal",
        "state",
        "https://mail.google.com/ openid email profile",
        &StartOAuthOptions {
            login_hint: Some("person+mail@example.com".to_string()),
            expected_google_sub: Some("main-google-sub".to_string()),
            reauthorize_account_id: None,
        },
    )
    .unwrap();
    let parsed = reqwest::Url::parse(&url).unwrap();
    let params: std::collections::HashMap<_, _> = parsed.query_pairs().into_owned().collect();
    assert_eq!(
        params.get("login_hint").map(String::as_str),
        Some("person+mail@example.com")
    );
    assert_eq!(
        params.get("include_granted_scopes").map(String::as_str),
        Some("true")
    );
}

#[test]
fn outlook_authorization_url_omits_google_incremental_grants() {
    let url = build_authorization_url_with_options(
        "outlook",
        "cid-2",
        "http://localhost",
        "chal",
        "state",
        "openid email",
        &StartOAuthOptions {
            login_hint: Some("person@example.com".to_string()),
            expected_google_sub: None,
            reauthorize_account_id: None,
        },
    )
    .unwrap();
    let parsed = reqwest::Url::parse(&url).unwrap();
    let params: std::collections::HashMap<_, _> = parsed.query_pairs().into_owned().collect();
    assert_eq!(
        params.get("login_hint").map(String::as_str),
        Some("person@example.com")
    );
    assert!(!params.contains_key("include_granted_scopes"));
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
    let expected_client_id = resolve_client_id("gmail", "client-123").unwrap();
    assert!(response.auth_url.contains(&expected_client_id));

    let entry = state::pending_pkce()
        .take(&response.state)
        .expect("pending entry registered");
    assert_eq!(entry.provider, "gmail");
    assert_eq!(entry.client_id, expected_client_id);
    assert_eq!(
        entry.client_secret,
        option_env!("GMAIL_CLIENT_SECRET").unwrap_or("")
    );
    assert_eq!(entry.redirect_uri, "http://localhost:1234");
    assert!(response
        .auth_url
        .contains(&pkce_challenge(&entry.code_verifier)));
}

#[test]
fn start_rejects_whitespace_only_expected_google_subject() {
    let result = start_oauth_with_options(
        "gmail",
        "client-123",
        "http://localhost:1234",
        StartOAuthOptions {
            login_hint: None,
            expected_google_sub: Some(" \t\n ".to_string()),
            reauthorize_account_id: None,
        },
    );
    assert!(
        matches!(result, Err(super::MailFfiError::InvalidRequest(message)) if message.contains("expected_google_sub"))
    );
}

#[test]
fn start_trims_surrounding_expected_google_subject_before_capture() {
    let started = start_oauth_with_options(
        "gmail",
        "client-123",
        "http://localhost:1234",
        StartOAuthOptions {
            login_hint: None,
            expected_google_sub: Some("  google-subject-1\t".to_string()),
            reauthorize_account_id: None,
        },
    )
    .unwrap();
    let entry = state::pending_pkce().take(&started.state).unwrap();
    assert_eq!(
        entry.expected_google_sub.as_deref(),
        Some("google-subject-1")
    );
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
fn complete_same_google_subject_creates_account_and_stores_only_mail_credentials() {
    let mut conn = migrated_conn();
    let key = [9u8; 32];
    let started = start_oauth_with_options(
        "gmail",
        "client-123",
        "http://localhost:1234",
        StartOAuthOptions {
            login_hint: Some("newuser@gmail.com".to_string()),
            expected_google_sub: Some("google-subject-1".to_string()),
            reauthorize_account_id: None,
        },
    )
    .unwrap();
    let fake = FakeTransport::new("newuser@gmail.com", Some("refresh-xyz"));

    let response = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &fake,
    }
    .complete_with_options(&started.state, "authcode", CompleteOAuthOptions::default())
    .unwrap();

    let credential_types: Vec<String> = conn
        .prepare("SELECT credential_type FROM encrypted_credentials WHERE account_id = ?1 ORDER BY credential_type")
        .unwrap()
        .query_map([&response.id], |row| row.get(0))
        .unwrap()
        .collect::<rusqlite::Result<_>>()
        .unwrap();
    let expected_credential_types = match option_env!("GMAIL_CLIENT_SECRET") {
        Some(secret) if !secret.is_empty() => vec![
            "oauth2_access_token",
            "oauth2_client_secret",
            "oauth2_refresh_token",
        ],
        _ => vec!["oauth2_access_token", "oauth2_refresh_token"],
    };
    assert_eq!(credential_types, expected_credential_types);
}

#[test]
fn complete_reauthorization_replaces_credentials_without_replacing_account() {
    let mut conn = migrated_conn();
    let key = [9u8; 32];
    conn.execute(
        "INSERT INTO accounts (
             id, email, display_name, auth_type, imap_host, imap_port,
             imap_encryption, smtp_host, smtp_port, smtp_encryption, username,
             oauth2_client_id, created_at, updated_at
         ) VALUES (
             'existing-account', 'user@gmail.com', 'Existing User',
             'oauth2_gmail', 'imap.gmail.com', 993, 'tls',
             'smtp.gmail.com', 587, 'starttls', 'user@gmail.com',
             'old-client', datetime('now'), datetime('now')
         )",
        [],
    )
    .unwrap();
    crate::credentials::store_encrypted_credential(
        &conn,
        &key,
        "existing-account",
        "oauth2_access_token",
        "old-access",
    )
    .unwrap();
    crate::credentials::store_encrypted_credential(
        &conn,
        &key,
        "existing-account",
        "oauth2_refresh_token",
        "old-refresh",
    )
    .unwrap();
    insert_pending_reauthorization("reauth-state", "existing-account");
    let fake = FakeTransport::new("user@gmail.com", Some("new-refresh"));

    let response = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &fake,
    }
    .complete("reauth-state", "authcode")
    .unwrap();

    assert_eq!(response.id, "existing-account");
    assert_eq!(
        conn.query_row("SELECT COUNT(*) FROM accounts", [], |row| row
            .get::<_, i64>(0))
            .unwrap(),
        1
    );
    assert_eq!(
        crate::credentials::get_encrypted_credential(
            &conn,
            &key,
            "existing-account",
            "oauth2_access_token",
        )
        .unwrap()
        .as_deref(),
        Some("access-tok")
    );
    assert_eq!(
        crate::credentials::get_encrypted_credential(
            &conn,
            &key,
            "existing-account",
            "oauth2_refresh_token",
        )
        .unwrap()
        .as_deref(),
        Some("new-refresh")
    );
}

#[test]
fn complete_google_subject_mismatch_is_typed_and_persists_nothing() {
    let mut conn = migrated_conn();
    let key = [9u8; 32];
    insert_pending("mismatch-state", "gmail");
    let fake = FakeTransport::new("other@gmail.com", Some("refresh-xyz"));

    let result = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &fake,
    }
    .complete_with_options(
        "mismatch-state",
        "authcode",
        CompleteOAuthOptions {
            expected_google_sub: Some("main-google-sub".to_string()),
        },
    );

    assert!(matches!(
        result,
        Err(super::MailFfiError::GoogleIdentityMismatch { expected, actual })
            if expected == "main-google-sub" && actual == "google-subject-1"
    ));
    let account_count: i64 = conn
        .query_row("SELECT COUNT(*) FROM accounts", [], |row| row.get(0))
        .unwrap();
    let credential_count: i64 = conn
        .query_row("SELECT COUNT(*) FROM encrypted_credentials", [], |row| {
            row.get(0)
        })
        .unwrap();
    assert_eq!((account_count, credential_count), (0, 0));
    assert!(matches!(
        OAuthCompletion {
            conn: &mut conn,
            credential_key: &key,
            transport: &fake
        }
        .complete("mismatch-state", "authcode"),
        Err(super::MailFfiError::InvalidState(_))
    ));
}

#[test]
fn complete_rejects_whitespace_only_override_before_network_or_state_consumption() {
    let mut conn = migrated_conn();
    let key = [9u8; 32];
    insert_pending("whitespace-override-state", "gmail");
    let fake = FakeTransport::new("other@gmail.com", Some("refresh-xyz"));

    let result = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &fake,
    }
    .complete_with_options(
        "whitespace-override-state",
        "authcode",
        CompleteOAuthOptions {
            expected_google_sub: Some(" \t\n ".to_string()),
        },
    );

    assert!(
        matches!(result, Err(super::MailFfiError::InvalidRequest(message)) if message.contains("expected_google_sub"))
    );
    assert_eq!(fake.exchange_calls.get(), 0);
    assert!(state::pending_pkce().contains("whitespace-override-state"));
    assert_eq!(
        conn.query_row("SELECT COUNT(*) FROM accounts", [], |row| row
            .get::<_, i64>(0))
            .unwrap(),
        0
    );
    assert_eq!(
        conn.query_row("SELECT COUNT(*) FROM encrypted_credentials", [], |row| row
            .get::<_, i64>(
            0
        ))
        .unwrap(),
        0
    );
    state::pending_pkce().take("whitespace-override-state");
}

#[test]
fn complete_expected_google_subject_rejects_whitespace_userinfo_sub_without_persistence() {
    let mut conn = migrated_conn();
    let key = [9u8; 32];
    insert_pending("whitespace-sub-state", "gmail");
    let mut fake = FakeTransport::new("other@gmail.com", Some("refresh-xyz"));
    fake.subject = Some(" \t\n ".to_string());

    let result = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &fake,
    }
    .complete_with_options(
        "whitespace-sub-state",
        "authcode",
        CompleteOAuthOptions {
            expected_google_sub: Some("main-google-sub".to_string()),
        },
    );

    assert!(matches!(
        result,
        Err(super::MailFfiError::GoogleIdentityMissingSubject)
    ));
    let counts = (
        conn.query_row("SELECT COUNT(*) FROM accounts", [], |row| {
            row.get::<_, i64>(0)
        })
        .unwrap(),
        conn.query_row("SELECT COUNT(*) FROM encrypted_credentials", [], |row| {
            row.get::<_, i64>(0)
        })
        .unwrap(),
    );
    assert_eq!(counts, (0, 0));
}

#[test]
fn complete_compares_trimmed_non_empty_google_subjects() {
    let mut conn = migrated_conn();
    let key = [9u8; 32];
    insert_pending("trimmed-sub-state", "gmail");
    let mut fake = FakeTransport::new("same@gmail.com", Some("refresh-xyz"));
    fake.subject = Some("\tgoogle-subject-1  ".to_string());

    let response = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &fake,
    }
    .complete_with_options(
        "trimmed-sub-state",
        "authcode",
        CompleteOAuthOptions {
            expected_google_sub: Some("  google-subject-1\n".to_string()),
        },
    )
    .unwrap();

    assert_eq!(response.email, "same@gmail.com");
}

#[test]
fn complete_expected_google_subject_rejects_missing_userinfo_sub_without_persistence() {
    let mut conn = migrated_conn();
    let key = [9u8; 32];
    insert_pending("missing-sub-state", "gmail");
    let mut fake = FakeTransport::new("other@gmail.com", Some("refresh-xyz"));
    fake.subject = None;

    let result = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &fake,
    }
    .complete_with_options(
        "missing-sub-state",
        "authcode",
        CompleteOAuthOptions {
            expected_google_sub: Some("main-google-sub".to_string()),
        },
    );

    assert!(matches!(
        result,
        Err(super::MailFfiError::GoogleIdentityMissingSubject)
    ));
    let counts: (i64, i64) = (
        conn.query_row("SELECT COUNT(*) FROM accounts", [], |row| row.get(0))
            .unwrap(),
        conn.query_row("SELECT COUNT(*) FROM encrypted_credentials", [], |row| {
            row.get(0)
        })
        .unwrap(),
    );
    assert_eq!(counts, (0, 0));
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
        loaded.oauth2_access_token.is_none(),
        "plaintext access column must be nulled"
    );
    assert!(
        loaded.oauth2_refresh_token.is_none(),
        "plaintext refresh column must be nulled"
    );

    let stored_access = crate::credentials::get_encrypted_credential(
        &conn,
        &key,
        &response.id,
        "oauth2_access_token",
    )
    .unwrap();
    assert_eq!(stored_access.as_deref(), Some("access-tok"));
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
fn complete_success_persists_initial_expiry_metadata() {
    let mut conn = migrated_conn();
    let key = [10u8; 32];
    insert_pending("success-expiry-state", "gmail");
    let mut fake = FakeTransport::new("expiry@gmail.com", Some("refresh-xyz"));
    fake.expires_at = Some("2026-09-01T12:34:56Z".to_string());

    let response = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &fake,
    }
    .complete("success-expiry-state", "authcode")
    .unwrap();

    let loaded = account::load_account(&conn, &response.id).unwrap();
    assert_eq!(
        loaded.oauth2_expires_at.as_deref(),
        Some("2026-09-01T12:34:56Z")
    );
}

#[test]
fn manual_db_state_qa_artifact_same_sub_and_mismatch() {
    let db_path = std::env::temp_dir().join(format!(
        "maho-mail-oauth-task2-{}.sqlite",
        uuid::Uuid::new_v4()
    ));
    let mut conn = Connection::open(&db_path).unwrap();
    maho_core::db::migrations::run_migrations(&conn).unwrap();
    let key = [42u8; 32];

    insert_pending("qa-same", "gmail");
    let same_fake = FakeTransport::new("same@gmail.com", Some("qa-refresh"));
    let same = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &same_fake,
    }
    .complete_with_options(
        "qa-same",
        "qa-code",
        CompleteOAuthOptions {
            expected_google_sub: Some("google-subject-1".to_string()),
        },
    )
    .unwrap();
    let same_accounts: i64 = conn
        .query_row("SELECT COUNT(*) FROM accounts", [], |row| row.get(0))
        .unwrap();
    let same_credentials: i64 = conn
        .query_row("SELECT COUNT(*) FROM encrypted_credentials", [], |row| {
            row.get(0)
        })
        .unwrap();
    println!(
        "MANUAL_DB_QA same_sub account_id={} accounts={} credentials={}",
        same.id, same_accounts, same_credentials
    );
    assert_eq!((same_accounts, same_credentials), (1, 2));

    conn.execute("DELETE FROM encrypted_credentials", [])
        .unwrap();
    conn.execute("DELETE FROM accounts", []).unwrap();
    insert_pending("qa-both-whitespace", "gmail");
    let mut both_whitespace_fake = FakeTransport::new("other@gmail.com", Some("must-not-persist"));
    both_whitespace_fake.subject = Some("  \t ".to_string());
    let both_whitespace = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &both_whitespace_fake,
    }
    .complete_with_options(
        "qa-both-whitespace",
        "qa-code",
        CompleteOAuthOptions {
            expected_google_sub: Some(" \n ".to_string()),
        },
    );
    let both_whitespace_accounts: i64 = conn
        .query_row("SELECT COUNT(*) FROM accounts", [], |row| row.get(0))
        .unwrap();
    let both_whitespace_credentials: i64 = conn
        .query_row("SELECT COUNT(*) FROM encrypted_credentials", [], |row| {
            row.get(0)
        })
        .unwrap();
    println!(
        "MANUAL_DB_QA both_whitespace error={} accounts={} credentials={} state_pending={}",
        both_whitespace.unwrap_err(),
        both_whitespace_accounts,
        both_whitespace_credentials,
        state::pending_pkce().contains("qa-both-whitespace")
    );
    assert_eq!(
        (both_whitespace_accounts, both_whitespace_credentials),
        (0, 0)
    );
    assert!(state::pending_pkce().contains("qa-both-whitespace"));
    state::pending_pkce().take("qa-both-whitespace");

    insert_pending("qa-whitespace", "gmail");
    let mut whitespace_fake = FakeTransport::new("other@gmail.com", Some("must-not-persist"));
    whitespace_fake.subject = Some("  \t ".to_string());
    let whitespace = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &whitespace_fake,
    }
    .complete_with_options(
        "qa-whitespace",
        "qa-code",
        CompleteOAuthOptions {
            expected_google_sub: Some("google-subject-1".to_string()),
        },
    );
    let whitespace_accounts: i64 = conn
        .query_row("SELECT COUNT(*) FROM accounts", [], |row| row.get(0))
        .unwrap();
    let whitespace_credentials: i64 = conn
        .query_row("SELECT COUNT(*) FROM encrypted_credentials", [], |row| {
            row.get(0)
        })
        .unwrap();
    println!(
        "MANUAL_DB_QA whitespace_sub error={} accounts={} credentials={}",
        whitespace.unwrap_err(),
        whitespace_accounts,
        whitespace_credentials
    );
    assert_eq!((whitespace_accounts, whitespace_credentials), (0, 0));

    insert_pending("qa-mismatch", "gmail");
    let mismatch_fake = FakeTransport::new("other@gmail.com", Some("must-not-persist"));
    let mismatch = OAuthCompletion {
        conn: &mut conn,
        credential_key: &key,
        transport: &mismatch_fake,
    }
    .complete_with_options(
        "qa-mismatch",
        "qa-code",
        CompleteOAuthOptions {
            expected_google_sub: Some("different-main-sub".to_string()),
        },
    );
    let mismatch_accounts: i64 = conn
        .query_row("SELECT COUNT(*) FROM accounts", [], |row| row.get(0))
        .unwrap();
    let mismatch_credentials: i64 = conn
        .query_row("SELECT COUNT(*) FROM encrypted_credentials", [], |row| {
            row.get(0)
        })
        .unwrap();
    println!(
        "MANUAL_DB_QA mismatch error={} accounts={} credentials={}",
        mismatch.unwrap_err(),
        mismatch_accounts,
        mismatch_credentials
    );
    assert_eq!((mismatch_accounts, mismatch_credentials), (0, 0));

    drop(conn);
    std::fs::remove_file(&db_path).unwrap();
    println!("MANUAL_DB_QA cleanup removed={}", db_path.display());
    assert!(!db_path.exists());
}

#[test]
fn provider_oauth_uses_configured_secret_before_explicit_fallback() {
    let gmail_secret = option_env!("GMAIL_CLIENT_SECRET").unwrap_or("");
    let outlook_secret = option_env!("OUTLOOK_CLIENT_SECRET").unwrap_or("");
    assert_eq!(resolve_client_secret("gmail", ""), gmail_secret);
    assert_eq!(resolve_client_secret("outlook", ""), outlook_secret);
    assert_eq!(
        resolve_client_secret("gmail", "explicit"),
        if gmail_secret.is_empty() {
            "explicit"
        } else {
            gmail_secret
        }
    );
}
