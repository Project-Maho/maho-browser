// Copyright 2026 Maho Browser. All rights reserved.

#![allow(clippy::disallowed_methods)]

use super::*;

fn seeded() -> (Connection, [u8; 32]) {
    let conn = Connection::open_in_memory().unwrap();
    maho_core::db::migrations::run_migrations(&conn).unwrap();
    conn.execute_batch(
        "INSERT INTO accounts (id, email, display_name, auth_type, imap_host,
         imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption,
         username, oauth2_expires_at, created_at, updated_at)
         VALUES ('review-account', 'user@example.invalid', 'User', 'oauth2_outlook',
         'outlook.office365.com', 993, 'tls', 'smtp.office365.com', 587, 'starttls',
         'user@example.invalid', '2020-01-01T00:00:00Z', 'old-created', 'old-updated');",
    )
    .unwrap();
    let key = [71; 32];
    for (kind, value) in [
        ("oauth2_access_token", "old-access"),
        ("oauth2_refresh_token", "old-refresh"),
    ] {
        store_encrypted_credential(&conn, &key, "review-account", kind, value).unwrap();
    }
    (conn, key)
}

#[test]
fn acc01_outlook_reauthorization_resolves_existing_email() {
    let (conn, _) = seeded();
    assert_eq!(
        oauth_reauthorization_email(&conn, "review-account").unwrap(),
        "user@example.invalid"
    );
}

fn assert_refresh_failure_rolls_back(trigger: &str) {
    let (conn, key) = seeded();
    conn.execute_batch(trigger).unwrap();
    let result = persist_refreshed_oauth2_credentials(
        &conn,
        &key,
        "review-account",
        "new-access",
        Some("new-refresh"),
        Some("2099-01-01T00:00:00Z"),
    );
    assert!(result.is_err());
    for (kind, expected) in [
        ("oauth2_access_token", "old-access"),
        ("oauth2_refresh_token", "old-refresh"),
    ] {
        assert_eq!(
            get_encrypted_credential(&conn, &key, "review-account", kind)
                .unwrap()
                .as_deref(),
            Some(expected),
            "{kind} changed after failed persistence"
        );
    }
    let account = load_account(&conn, "review-account").unwrap();
    assert_eq!(
        account.oauth2_expires_at.as_deref(),
        Some("2020-01-01T00:00:00Z")
    );
    assert_eq!(account.updated_at, "old-updated");
}

#[test]
fn acc05_refresh_rotation_failure_rolls_back_access_token() {
    assert_refresh_failure_rolls_back(
        "CREATE TRIGGER fail_rotation BEFORE INSERT ON encrypted_credentials
         WHEN NEW.credential_type = 'oauth2_refresh_token'
         BEGIN SELECT RAISE(ABORT, 'forced rotation failure'); END;",
    );
}

#[test]
fn acc05_refresh_metadata_failure_rolls_back_both_tokens() {
    assert_refresh_failure_rolls_back(
        "CREATE TRIGGER fail_metadata BEFORE UPDATE ON accounts
         BEGIN SELECT RAISE(ABORT, 'forced metadata failure'); END;",
    );
}
