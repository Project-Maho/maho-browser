use std::collections::BTreeMap;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

use aes_gcm::aead::{Aead, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce};

use rusqlite::Connection;

use super::{
    copy_sqlcipher_db_atomically, encrypt_transfer_artifact, CredentialExportPayload,
    ExportedCredential,
};
use crate::error::AppError;

pub fn export_runtime_transfer_artifact(
    db_path: &Path,
    artifact_path: &Path,
    passphrase: &str,
    db_copy_path: Option<&Path>,
    sqlcipher_key: &str,
    credential_key: &[u8; 32],
) -> Result<(), AppError> {
    let payload = build_runtime_transfer_payload(db_path, sqlcipher_key, credential_key)?;
    let artifact = encrypt_transfer_artifact(&payload, passphrase)?;
    write_artifact_atomically(artifact_path, &artifact)?;
    if let Some(destination) = db_copy_path {
        copy_sqlcipher_db_atomically(db_path, destination, &payload.sqlcipher_key)?;
    }
    Ok(())
}

pub fn build_runtime_transfer_payload(
    db_path: &Path,
    sqlcipher_key: &str,
    credential_key: &[u8; 32],
) -> Result<CredentialExportPayload, AppError> {
    let conn = open_sqlcipher_db(db_path, sqlcipher_key)?;
    Ok(CredentialExportPayload {
        version: 1,
        exported_at: chrono::Utc::now().to_rfc3339(),
        sqlcipher_key: sqlcipher_key.trim().to_string(),
        credential_key: *credential_key,
        credentials: collect_credentials(&conn, credential_key)?,
    })
}

fn collect_credentials(
    conn: &Connection,
    credential_key: &[u8; 32],
) -> Result<Vec<ExportedCredential>, AppError> {
    let mut credentials = BTreeMap::<(String, String), ExportedCredential>::new();
    collect_account_columns(conn, &mut credentials)?;
    collect_encrypted_credentials(conn, credential_key, &mut credentials)?;
    Ok(credentials.into_values().collect())
}

fn collect_account_columns(
    conn: &Connection,
    credentials: &mut BTreeMap<(String, String), ExportedCredential>,
) -> Result<(), AppError> {
    let mut stmt = conn.prepare(
        "SELECT id, password, oauth2_access_token, oauth2_refresh_token, oauth2_client_secret FROM accounts",
    )?;
    let rows = stmt.query_map([], |row| {
        Ok((
            row.get::<_, String>(0)?,
            row.get::<_, Option<String>>(1)?,
            row.get::<_, Option<String>>(2)?,
            row.get::<_, Option<String>>(3)?,
            row.get::<_, Option<String>>(4)?,
        ))
    })?;
    for row in rows {
        let (account_id, password, access, refresh, secret) = row?;
        insert_nonempty(credentials, &account_id, "password", password.as_deref());
        insert_nonempty(
            credentials,
            &account_id,
            "oauth2_access_token",
            access.as_deref(),
        );
        insert_nonempty(
            credentials,
            &account_id,
            "oauth2_refresh_token",
            refresh.as_deref(),
        );
        insert_nonempty(
            credentials,
            &account_id,
            "oauth2_client_secret",
            secret.as_deref(),
        );
    }
    Ok(())
}

fn collect_encrypted_credentials(
    conn: &Connection,
    credential_key: &[u8; 32],
    credentials: &mut BTreeMap<(String, String), ExportedCredential>,
) -> Result<(), AppError> {
    let mut stmt = conn.prepare(
        "SELECT account_id, credential_type, encrypted_value, nonce FROM encrypted_credentials",
    )?;
    let rows = stmt.query_map([], |row| {
        Ok((
            row.get::<_, String>(0)?,
            row.get::<_, String>(1)?,
            row.get::<_, Vec<u8>>(2)?,
            row.get::<_, Vec<u8>>(3)?,
        ))
    })?;
    for row in rows {
        let (account_id, credential_type, encrypted_value, nonce) = row?;
        let value = decrypt_credential(credential_key, &encrypted_value, &nonce)?;
        insert_credential(credentials, account_id, credential_type, value);
    }
    Ok(())
}

fn insert_nonempty(
    credentials: &mut BTreeMap<(String, String), ExportedCredential>,
    account_id: &str,
    credential_type: &str,
    value: Option<&str>,
) {
    if let Some(value) = value.filter(|value| !value.is_empty()) {
        insert_credential(
            credentials,
            account_id.to_string(),
            credential_type.to_string(),
            value.to_string(),
        );
    }
}

fn insert_credential(
    credentials: &mut BTreeMap<(String, String), ExportedCredential>,
    account_id: String,
    credential_type: String,
    value: String,
) {
    credentials.insert(
        (account_id.clone(), credential_type.clone()),
        ExportedCredential {
            account_id,
            credential_type,
            value,
        },
    );
}

fn decrypt_credential(key: &[u8; 32], ciphertext: &[u8], nonce: &[u8]) -> Result<String, AppError> {
    let cipher = Aes256Gcm::new_from_slice(key)
        .map_err(|e| AppError::Keyring(format!("Failed to create credential cipher: {e}")))?;
    let plaintext = cipher
        .decrypt(Nonce::from_slice(nonce), ciphertext)
        .map_err(|e| AppError::Keyring(format!("Credential decryption failed: {e}")))?;
    String::from_utf8(plaintext)
        .map_err(|e| AppError::Keyring(format!("Credential plaintext is not UTF-8: {e}")))
}

fn open_sqlcipher_db(db_path: &Path, sqlcipher_key: &str) -> Result<Connection, AppError> {
    let conn = Connection::open(db_path)?;
    conn.pragma_update(None, "key", sqlcipher_key.trim())?;
    conn.execute_batch("PRAGMA foreign_keys=ON;")?;
    Ok(conn)
}

fn write_artifact_atomically(destination: &Path, bytes: &[u8]) -> Result<(), AppError> {
    let parent = destination.parent().ok_or_else(|| {
        AppError::Validation("artifact destination must have a parent".to_string())
    })?;
    fs::create_dir_all(parent)?;
    let temp_path = artifact_temp_path(destination);
    let mut file = fs::File::create(&temp_path)?;
    file.write_all(bytes)?;
    file.sync_all()?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(&temp_path, fs::Permissions::from_mode(0o600))?;
    }
    fs::rename(&temp_path, destination)?;
    Ok(())
}

fn artifact_temp_path(destination: &Path) -> PathBuf {
    let extension = destination
        .extension()
        .map(|ext| format!("{}.tmp", ext.to_string_lossy()))
        .unwrap_or_else(|| "tmp".to_string());
    destination.with_extension(format!("{extension}.{}", uuid::Uuid::new_v4()))
}
