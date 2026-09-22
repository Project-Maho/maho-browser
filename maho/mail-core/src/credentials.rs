// Copyright 2026 Maho Browser. All rights reserved.

//! Injected-key handling and per-account credential decryption.
//!
//! The mail helper injects two keys via `MahoMailInjectKeys`:
//!   * `sqlcipher_key`  — carried for the transfer artifact format only; the
//!     mail database itself is a plain SQLite file and is never keyed.
//!   * `credential_key` — a 32-byte AES-256-GCM key used to decrypt per-account
//!     credentials stored in the `encrypted_credentials` table.
//!
//! This mirrors the AES-256-GCM scheme in `maho_core::credential_store`, except
//! the key is supplied by the browser (HC1) instead of the OS keyring.

use aes_gcm::aead::{Aead, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce};
use base64::Engine as _;
use rand::RngCore;
use rusqlite::Connection;

use crate::error::{MailFfiError, Result};

/// Decode the injected credential key string into a 32-byte AES key.
///
/// Accepts standard base64 that decodes to exactly 32 bytes (maho-core format),
/// or a 64-char hex string. Any other input is rejected with an error: silently
/// downgrading to a low-entropy key (e.g. an all-zero key for empty input) would
/// encrypt every account credential under an attacker-guessable key, which is
/// strictly worse than failing closed and disabling mail.
pub fn decode_credential_key(raw: &str) -> Result<[u8; 32]> {
    let trimmed = raw.trim();

    if let Ok(bytes) = base64::prelude::BASE64_STANDARD.decode(trimmed) {
        if bytes.len() == 32 {
            let mut key = [0u8; 32];
            key.copy_from_slice(&bytes);
            return Ok(key);
        }
    }

    if trimmed.len() == 64 {
        if let Some(bytes) = decode_hex(trimmed) {
            if bytes.len() == 32 {
                let mut key = [0u8; 32];
                key.copy_from_slice(&bytes);
                return Ok(key);
            }
        }
    }

    // Fail closed: an unrecognized key must never be silently folded into a
    // low-entropy AES key.
    Err(MailFfiError::InvalidKey(
        "credential_key must be base64 or hex encoding exactly 32 bytes",
    ))
}

fn decode_hex(s: &str) -> Option<Vec<u8>> {
    if s.len() % 2 != 0 {
        return None;
    }
    let mut out = Vec::with_capacity(s.len() / 2);
    let bytes = s.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        let hi = (bytes[i] as char).to_digit(16)?;
        let lo = (bytes[i + 1] as char).to_digit(16)?;
        out.push(((hi << 4) | lo) as u8);
        i += 2;
    }
    Some(out)
}

fn decrypt(key: &[u8; 32], ciphertext: &[u8], nonce_bytes: &[u8]) -> Result<String> {
    if nonce_bytes.len() != 12 {
        return Err(MailFfiError::CredentialDecrypt(
            "invalid nonce length".into(),
        ));
    }
    let cipher = Aes256Gcm::new_from_slice(key)
        .map_err(|e| MailFfiError::CredentialDecrypt(format!("cipher init: {e}")))?;
    let nonce = Nonce::from_slice(nonce_bytes);
    let plaintext = cipher
        .decrypt(nonce, ciphertext)
        .map_err(|e| MailFfiError::CredentialDecrypt(format!("decrypt: {e}")))?;
    String::from_utf8(plaintext).map_err(|e| MailFfiError::CredentialDecrypt(format!("utf8: {e}")))
}

/// Encrypt `plaintext` under `key` with AES-256-GCM, mirroring the layout read
/// by [`get_encrypted_credential`]: a fresh 12-byte nonce plus the AEAD
/// ciphertext (with the appended GCM tag). Returns `(ciphertext, nonce)`.
fn encrypt(key: &[u8; 32], plaintext: &str) -> Result<(Vec<u8>, Vec<u8>)> {
    let mut nonce_bytes = [0u8; 12];
    rand::thread_rng().fill_bytes(&mut nonce_bytes);
    let cipher = Aes256Gcm::new_from_slice(key)
        .map_err(|e| MailFfiError::CredentialEncrypt(format!("cipher init: {e}")))?;
    let nonce = Nonce::from_slice(&nonce_bytes);
    let ciphertext = cipher
        .encrypt(nonce, plaintext.as_bytes())
        .map_err(|e| MailFfiError::CredentialEncrypt(format!("encrypt: {e}")))?;
    Ok((ciphertext, nonce_bytes.to_vec()))
}

/// Encrypt and persist a per-account credential into `encrypted_credentials`,
/// matching the AES-256-GCM nonce/ciphertext layout consumed by
/// [`get_encrypted_credential`]. Re-storing the same
/// `(account_id, credential_type)` overwrites the prior value with a fresh
/// nonce.
///
/// `credential_type` is one of `"password"`, `"oauth2_access_token"`,
/// `"oauth2_refresh_token"`, `"oauth2_client_secret"`.
pub fn store_encrypted_credential(
    conn: &Connection,
    credential_key: &[u8; 32],
    account_id: &str,
    credential_type: &str,
    plaintext: &str,
) -> Result<()> {
    let (ciphertext, nonce) = encrypt(credential_key, plaintext)?;
    conn.execute(
        "INSERT INTO encrypted_credentials
             (account_id, credential_type, encrypted_value, nonce, updated_at)
         VALUES (?1, ?2, ?3, ?4, datetime('now'))
         ON CONFLICT(account_id, credential_type) DO UPDATE SET
             encrypted_value = excluded.encrypted_value,
             nonce = excluded.nonce,
             updated_at = excluded.updated_at",
        rusqlite::params![account_id, credential_type, ciphertext, nonce],
    )?;
    Ok(())
}

/// Look up and decrypt a stored credential for an account, if present in the
/// `encrypted_credentials` table.
///
/// `credential_type` is one of `"password"`, `"oauth2_access_token"`,
/// `"oauth2_refresh_token"`, `"oauth2_client_secret"`.
pub fn get_encrypted_credential(
    conn: &Connection,
    credential_key: &[u8; 32],
    account_id: &str,
    credential_type: &str,
) -> Result<Option<String>> {
    let row: std::result::Result<(Vec<u8>, Vec<u8>), rusqlite::Error> = conn.query_row(
        "SELECT encrypted_value, nonce FROM encrypted_credentials
         WHERE account_id = ?1 AND credential_type = ?2",
        rusqlite::params![account_id, credential_type],
        |row| Ok((row.get(0)?, row.get(1)?)),
    );

    match row {
        Ok((ciphertext, nonce)) => Ok(Some(decrypt(credential_key, &ciphertext, &nonce)?)),
        Err(rusqlite::Error::QueryReturnedNoRows) => Ok(None),
        Err(e) => Err(MailFfiError::Core(maho_core::error::AppError::Database(e))),
    }
}

#[cfg(test)]
#[allow(clippy::disallowed_methods)]
mod tests {
    use super::*;

    fn encrypt(key: &[u8; 32], plaintext: &str) -> (Vec<u8>, Vec<u8>) {
        // Deterministic nonce is fine for a unit test (never reused in prod).
        let nonce_bytes = [9u8; 12];
        let cipher = Aes256Gcm::new_from_slice(key).unwrap();
        let nonce = Nonce::from_slice(&nonce_bytes);
        let ct = cipher.encrypt(nonce, plaintext.as_bytes()).unwrap();
        (ct, nonce_bytes.to_vec())
    }

    #[test]
    fn base64_key_roundtrips() {
        let raw_bytes = [7u8; 32];
        let b64 = base64::prelude::BASE64_STANDARD.encode(raw_bytes);
        assert_eq!(decode_credential_key(&b64).unwrap(), raw_bytes);
    }

    #[test]
    fn hex_key_roundtrips() {
        let raw_bytes = [0xABu8; 32];
        let hex: String = raw_bytes.iter().map(|b| format!("{b:02x}")).collect();
        assert_eq!(decode_credential_key(&hex).unwrap(), raw_bytes);
    }

    #[test]
    fn rejects_invalid_key_instead_of_downgrading() {
        // Empty / short / non-encoded keys MUST be rejected, never silently
        // folded into a low-entropy AES key (crypto-downgrade).
        assert!(decode_credential_key("").is_err());
        assert!(decode_credential_key("short").is_err());
        assert!(decode_credential_key("not-a-valid-key").is_err());
    }

    #[test]
    fn encrypt_then_get_credential() {
        let conn = Connection::open_in_memory().unwrap();
        conn.execute_batch(
            "CREATE TABLE encrypted_credentials (
                account_id TEXT NOT NULL,
                credential_type TEXT NOT NULL,
                encrypted_value BLOB NOT NULL,
                nonce BLOB NOT NULL,
                PRIMARY KEY (account_id, credential_type)
            );",
        )
        .unwrap();

        let key = [42u8; 32];
        let (ct, nonce) = encrypt(&key, "hunter2");
        conn.execute(
            "INSERT INTO encrypted_credentials (account_id, credential_type, encrypted_value, nonce)
             VALUES (?1, ?2, ?3, ?4)",
            rusqlite::params!["acc1", "password", ct, nonce],
        )
        .unwrap();

        let got = get_encrypted_credential(&conn, &key, "acc1", "password").unwrap();
        assert_eq!(got.as_deref(), Some("hunter2"));

        let missing = get_encrypted_credential(&conn, &key, "acc1", "oauth2_access_token").unwrap();
        assert!(missing.is_none());
    }

    #[test]
    fn wrong_key_fails_decrypt() {
        let conn = Connection::open_in_memory().unwrap();
        conn.execute_batch(
            "CREATE TABLE encrypted_credentials (
                account_id TEXT NOT NULL,
                credential_type TEXT NOT NULL,
                encrypted_value BLOB NOT NULL,
                nonce BLOB NOT NULL,
                PRIMARY KEY (account_id, credential_type)
            );",
        )
        .unwrap();
        let key = [1u8; 32];
        let (ct, nonce) = encrypt(&key, "secret");
        conn.execute(
            "INSERT INTO encrypted_credentials (account_id, credential_type, encrypted_value, nonce)
             VALUES (?1, ?2, ?3, ?4)",
            rusqlite::params!["acc1", "password", ct, nonce],
        )
        .unwrap();

        let wrong = [2u8; 32];
        assert!(get_encrypted_credential(&conn, &wrong, "acc1", "password").is_err());
    }

    #[test]
    fn test_nonce_entropy_not_fixed_uuid_version() {
        let key = [0u8; 32];
        let mut all_have_uuid_v4_nibble = true;
        for _ in 0..50 {
            let (_, nonce) = super::encrypt(&key, "test").unwrap();
            // UUIDv4 has version 4 in byte 6: high nibble == 4
            if (nonce[6] >> 4) != 0x4 {
                all_have_uuid_v4_nibble = false;
                break;
            }
        }
        assert!(
            !all_have_uuid_v4_nibble,
            "nonce should not be derived from UUIDv4 (fixed version bit 0x4 in byte 6)"
        );
    }

    fn creds_table(conn: &Connection) {
        conn.execute_batch(
            "CREATE TABLE encrypted_credentials (
                account_id TEXT NOT NULL,
                credential_type TEXT NOT NULL,
                encrypted_value BLOB NOT NULL,
                nonce BLOB NOT NULL,
                created_at TEXT NOT NULL DEFAULT (datetime('now')),
                updated_at TEXT NOT NULL DEFAULT (datetime('now')),
                PRIMARY KEY (account_id, credential_type)
            );",
        )
        .unwrap();
    }

    #[test]
    fn store_encrypted_credential_roundtrips_through_get() {
        let conn = Connection::open_in_memory().unwrap();
        creds_table(&conn);
        let key = [55u8; 32];

        store_encrypted_credential(&conn, &key, "acc1", "password", "hunter2").unwrap();

        let got = get_encrypted_credential(&conn, &key, "acc1", "password").unwrap();
        assert_eq!(got.as_deref(), Some("hunter2"));
    }

    #[test]
    fn store_encrypted_credential_reencrypts_on_overwrite() {
        let conn = Connection::open_in_memory().unwrap();
        creds_table(&conn);
        let key = [77u8; 32];

        store_encrypted_credential(&conn, &key, "acc1", "password", "old").unwrap();
        store_encrypted_credential(&conn, &key, "acc1", "password", "new").unwrap();

        let got = get_encrypted_credential(&conn, &key, "acc1", "password").unwrap();
        assert_eq!(got.as_deref(), Some("new"));

        // Exactly one row survives the overwrite (upsert, not duplicate insert).
        let count: i64 = conn
            .query_row(
                "SELECT COUNT(*) FROM encrypted_credentials WHERE account_id = 'acc1'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(count, 1);
    }

    #[test]
    fn store_encrypted_credential_wrong_key_fails_decrypt() {
        let conn = Connection::open_in_memory().unwrap();
        creds_table(&conn);
        let key = [3u8; 32];

        store_encrypted_credential(&conn, &key, "acc1", "password", "secret").unwrap();

        let wrong = [4u8; 32];
        assert!(get_encrypted_credential(&conn, &wrong, "acc1", "password").is_err());
    }
}
