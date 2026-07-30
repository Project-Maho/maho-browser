use std::fs;

use super::{
    copy::copy_db_atomically_with_verifier, decrypt_transfer_artifact, encrypt_transfer_artifact,
    CredentialExportPayload, ExportedCredential,
};

fn payload() -> CredentialExportPayload {
    CredentialExportPayload {
        version: 1,
        exported_at: "2026-07-15T12:00:00Z".to_string(),
        sqlcipher_key: "sqlcipher-secret".to_string(),
        credential_key: [9u8; 32],
        credentials: vec![ExportedCredential {
            account_id: "acc1".to_string(),
            credential_type: "password".to_string(),
            value: "hunter2".to_string(),
        }],
    }
}

fn temp_dir() -> std::path::PathBuf {
    std::env::temp_dir().join(format!("maho-transfer-test-{}", uuid::Uuid::new_v4()))
}

#[test]
fn encrypted_artifact_roundtrips_without_plaintext_leakage() {
    let artifact = encrypt_transfer_artifact(&payload(), "correct horse battery staple").unwrap();
    let text = String::from_utf8(artifact.clone()).unwrap();

    assert!(!text.contains("sqlcipher-secret"));
    assert!(!text.contains("hunter2"));

    let restored = decrypt_transfer_artifact(&artifact, "correct horse battery staple").unwrap();

    assert_eq!(restored.sqlcipher_key, "sqlcipher-secret");
    assert_eq!(restored.credential_key, [9u8; 32]);
    assert_eq!(restored.credentials[0].value, "hunter2");
    assert!(decrypt_transfer_artifact(&artifact, "wrong passphrase").is_err());
}

#[test]
fn db_copy_failure_leaves_source_and_destination_intact() {
    let dir = temp_dir();
    fs::create_dir_all(&dir).unwrap();
    let source = dir.join("source.db");
    let destination = dir.join("destination.db");
    fs::write(&source, b"source-original").unwrap();
    fs::write(&destination, b"destination-original").unwrap();

    let result = copy_db_atomically_with_verifier(&source, &destination, |_candidate| {
        Err(crate::error::AppError::Internal(
            "forced verify failure".to_string(),
        ))
    });

    assert!(result.is_err());
    assert_eq!(fs::read(&source).unwrap(), b"source-original");
    assert_eq!(fs::read(&destination).unwrap(), b"destination-original");
    let _ = fs::remove_dir_all(dir);
}
