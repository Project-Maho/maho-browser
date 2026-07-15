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
    let pool = maho_core::db::init_database_pool(db_path, &imported.sqlcipher_key)?;
    Ok(AppCtx {
        pool,
        db_path: db_path.to_path_buf(),
        sqlcipher_key: imported.sqlcipher_key,
        credential_key: imported.credential_key,
    })
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
        let conn = maho_core::db::init_database_with_key(&path, "import-sql-key").unwrap();
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
