// Copyright 2026 Maho Browser. All rights reserved.

use std::path::Path;

use maho_core::transfer::{decrypt_transfer_artifact, CredentialExportPayload, ExportedCredential};

use crate::error::Result;
use crate::state::AppCtx;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImportedMailTransfer {
    pub sqlcipher_key: String,
    pub credential_key: [u8; 32],
    pub credentials: Vec<ExportedCredential>,
}

pub fn import_transfer_artifact_bytes(
    artifact_bytes: &[u8],
    passphrase: &str,
) -> Result<ImportedMailTransfer> {
    decrypt_transfer_artifact(artifact_bytes, passphrase)
        .map(ImportedMailTransfer::from_payload)
        .map_err(Into::into)
}

pub fn open_imported_database(
    db_path: &Path,
    artifact_bytes: &[u8],
    passphrase: &str,
) -> Result<AppCtx> {
    let imported = import_transfer_artifact_bytes(artifact_bytes, passphrase)?;
    let pool = maho_core::db::init_database_pool(db_path)?;
    Ok(AppCtx {
        pool,
        db_path: db_path.to_path_buf(),
        sqlcipher_key: imported.sqlcipher_key,
        credential_key: imported.credential_key,
    })
}

pub fn import_migration_archive(ctx: &AppCtx, archive_json_str: &str) -> Result<serde_json::Value> {
    #[derive(Debug, serde::Deserialize)]
    struct Request {
        artifact_base64: String,
        passphrase: String,
        db_path: String,
    }

    let req: Request = serde_json::from_str(archive_json_str)
        .map_err(|e| crate::error::MailFfiError::Internal(format!("Failed to parse JSON: {e}")))?;

    use base64::Engine;
    let artifact_bytes = base64::prelude::BASE64_STANDARD
        .decode(&req.artifact_base64)
        .map_err(|e| {
            crate::error::MailFfiError::Internal(format!("Failed to decode base64: {e}"))
        })?;

    let db_path = Path::new(&req.db_path);
    if !db_path.exists() {
        return Err(crate::error::MailFfiError::Internal(format!(
            "Database file not found: {}",
            req.db_path
        )));
    }

    // Open the old database
    let old_ctx = open_imported_database(db_path, &artifact_bytes, &req.passphrase)?;
    let old_conn = old_ctx.pool.get().map_err(|e| {
        crate::error::MailFfiError::Internal(format!("Failed to get connection to old DB: {e}"))
    })?;

    // Open current database
    let mut current_conn = ctx.pool.get().map_err(|e| {
        crate::error::MailFfiError::Internal(format!("Failed to get connection to current DB: {e}"))
    })?;

    // Let's copy accounts and credentials
    let tx = current_conn.transaction().map_err(|e| {
        crate::error::MailFfiError::Internal(format!("Failed to start transaction: {e}"))
    })?;

    // 1. Read accounts from old DB
    let mut stmt = old_conn
        .prepare(
            "SELECT id, email, display_name, auth_type, imap_host, imap_port, imap_encryption,
                smtp_host, smtp_port, smtp_encryption, username, oauth2_client_id,
                oauth2_client_secret, oauth2_refresh_token, oauth2_access_token,
                oauth2_expires_at, password, created_at, updated_at
         FROM accounts",
        )
        .map_err(|e| {
            crate::error::MailFfiError::Internal(format!("Failed to prepare accounts stmt: {e}"))
        })?;

    let account_rows = stmt
        .query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,          // id
                row.get::<_, String>(1)?,          // email
                row.get::<_, Option<String>>(2)?,  // display_name
                row.get::<_, String>(3)?,          // auth_type
                row.get::<_, String>(4)?,          // imap_host
                row.get::<_, i32>(5)?,             // imap_port
                row.get::<_, String>(6)?,          // imap_encryption
                row.get::<_, String>(7)?,          // smtp_host
                row.get::<_, i32>(8)?,             // smtp_port
                row.get::<_, String>(9)?,          // smtp_encryption
                row.get::<_, String>(10)?,         // username
                row.get::<_, Option<String>>(11)?, // oauth2_client_id
                row.get::<_, Option<String>>(12)?, // oauth2_client_secret
                row.get::<_, Option<String>>(13)?, // oauth2_refresh_token
                row.get::<_, Option<String>>(14)?, // oauth2_access_token
                row.get::<_, Option<String>>(15)?, // oauth2_expires_at
                row.get::<_, Option<String>>(16)?, // password
                row.get::<_, String>(17)?,         // created_at
                row.get::<_, String>(18)?,         // updated_at
            ))
        })
        .map_err(|e| {
            crate::error::MailFfiError::Internal(format!("Failed to query accounts: {e}"))
        })?;

    let mut imported_accounts_count = 0;
    for row_res in account_rows {
        let r = row_res.map_err(|e| {
            crate::error::MailFfiError::Internal(format!("Failed to read account row: {e}"))
        })?;
        tx.execute(
            "INSERT OR REPLACE INTO accounts (id, email, display_name, auth_type, imap_host, imap_port, imap_encryption,
                                             smtp_host, smtp_port, smtp_encryption, username, oauth2_client_id,
                                             oauth2_client_secret, oauth2_refresh_token, oauth2_access_token,
                                             oauth2_expires_at, password, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19)",
            rusqlite::params![
                r.0, r.1, r.2, r.3, r.4, r.5, r.6, r.7, r.8, r.9, r.10, r.11,
                None::<String>, // oauth2_client_secret
                None::<String>, // oauth2_refresh_token
                None::<String>, // oauth2_access_token
                r.15,           // oauth2_expires_at
                None::<String>, // password
                r.17,           // created_at
                r.18,           // updated_at
            ]
        ).map_err(|e| crate::error::MailFfiError::Internal(format!("Failed to insert account: {e}")))?;
        imported_accounts_count += 1;
    }

    // 2. Read folders from old DB and insert them
    let mut stmt = old_conn.prepare(
        "SELECT id, account_id, name, path, folder_type, last_synced_at, unread_count, total_count
         FROM folders"
    ).map_err(|e| crate::error::MailFfiError::Internal(format!("Failed to prepare folders stmt: {e}")))?;

    let folder_rows = stmt
        .query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,         // id
                row.get::<_, String>(1)?,         // account_id
                row.get::<_, String>(2)?,         // name
                row.get::<_, String>(3)?,         // path
                row.get::<_, String>(4)?,         // folder_type
                row.get::<_, Option<String>>(5)?, // last_synced_at
                row.get::<_, i32>(6)?,            // unread_count
                row.get::<_, i32>(7)?,            // total_count
            ))
        })
        .map_err(|e| {
            crate::error::MailFfiError::Internal(format!("Failed to query folders: {e}"))
        })?;

    for row_res in folder_rows {
        let r = row_res.map_err(|e| {
            crate::error::MailFfiError::Internal(format!("Failed to read folder row: {e}"))
        })?;
        tx.execute(
            "INSERT OR REPLACE INTO folders (id, account_id, name, path, folder_type, last_synced_at, unread_count, total_count)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)",
            rusqlite::params![
                r.0, r.1, r.2, r.3, r.4, r.5, r.6, r.7
            ]
        ).map_err(|e| crate::error::MailFfiError::Internal(format!("Failed to insert folder: {e}")))?;
    }

    // 3. Re-encrypt credentials using current ctx.credential_key
    let imported = import_transfer_artifact_bytes(&artifact_bytes, &req.passphrase)?;
    for cred in imported.credentials {
        crate::credentials::store_encrypted_credential(
            &tx,
            &ctx.credential_key,
            &cred.account_id,
            &cred.credential_type,
            &cred.value,
        )?;
    }

    tx.commit().map_err(|e| {
        crate::error::MailFfiError::Internal(format!("Failed to commit transaction: {e}"))
    })?;
    crate::ffi::emit_accounts_changed_event("");

    let mut result = serde_json::Map::new();
    result.insert(
        "status".to_string(),
        serde_json::Value::String("success".to_string()),
    );
    result.insert(
        "imported_accounts_count".to_string(),
        serde_json::Value::from(imported_accounts_count),
    );
    Ok(serde_json::Value::Object(result))
}

impl ImportedMailTransfer {
    fn from_payload(payload: CredentialExportPayload) -> Self {
        Self {
            sqlcipher_key: payload.sqlcipher_key,
            credential_key: payload.credential_key,
            credentials: payload.credentials,
        }
    }
}

#[cfg(test)]
#[allow(clippy::disallowed_methods)]
mod tests {
    use maho_core::transfer::{
        encrypt_transfer_artifact, CredentialExportPayload, ExportedCredential,
    };
    use rusqlite::params;

    use super::{import_transfer_artifact_bytes, open_imported_database};

    fn payload() -> CredentialExportPayload {
        CredentialExportPayload {
            version: 1,
            exported_at: "2026-07-15T12:00:00Z".to_string(),
            sqlcipher_key: "import-sql-key".to_string(),
            credential_key: [4u8; 32],
            credentials: vec![ExportedCredential {
                account_id: "acc1".to_string(),
                credential_type: "password".to_string(),
                value: "secret-password".to_string(),
            }],
        }
    }

    fn db_path() -> std::path::PathBuf {
        std::env::temp_dir().join(format!("maho-mail-import-{}.db", uuid::Uuid::new_v4()))
    }

    #[test]
    fn imported_artifact_exposes_keys_and_account_credentials() {
        let artifact = encrypt_transfer_artifact(&payload(), "passphrase").unwrap();

        let imported = import_transfer_artifact_bytes(&artifact, "passphrase").unwrap();

        assert_eq!(imported.sqlcipher_key, "import-sql-key");
        assert_eq!(imported.credential_key, [4u8; 32]);
        assert_eq!(imported.credentials[0].value, "secret-password");
    }

    #[test]
    fn imported_artifact_opens_copied_sqlcipher_database() {
        let path = db_path();
        let conn = maho_core::db::init_database(&path).unwrap();
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)",
            params![
                "acc1",
                "user@example.com",
                "User",
                "imap.example.com",
                993,
                "Tls",
                "smtp.example.com",
                587,
                "StartTls",
                "user@example.com"
            ],
        )
        .unwrap();
        drop(conn);
        let artifact = encrypt_transfer_artifact(&payload(), "passphrase").unwrap();

        let ctx = open_imported_database(&path, &artifact, "passphrase").unwrap();
        let pooled = ctx.pool.get().unwrap();
        let count: i64 = pooled
            .query_row("SELECT COUNT(*) FROM accounts", [], |row| row.get(0))
            .unwrap();

        assert_eq!(count, 1);
        let _ = std::fs::remove_file(path);
    }
}
