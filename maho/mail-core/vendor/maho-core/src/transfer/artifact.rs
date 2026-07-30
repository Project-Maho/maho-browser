use aes_gcm::aead::{Aead, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce};
use argon2::Argon2;
use base64::Engine as _;
use rand::RngCore;
use serde::{Deserialize, Serialize};

use crate::error::AppError;

const ARTIFACT_VERSION: u8 = 1;
const SALT_LEN: usize = 16;
const NONCE_LEN: usize = 12;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ExportedCredential {
    pub account_id: String,
    pub credential_type: String,
    pub value: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct CredentialExportPayload {
    pub version: u8,
    pub exported_at: String,
    pub sqlcipher_key: String,
    pub credential_key: [u8; 32],
    pub credentials: Vec<ExportedCredential>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct TransferArtifact {
    version: u8,
    kdf: String,
    cipher: String,
    salt: String,
    nonce: String,
    ciphertext: String,
}

pub fn encrypt_transfer_artifact(
    payload: &CredentialExportPayload,
    passphrase: &str,
) -> Result<Vec<u8>, AppError> {
    if passphrase.is_empty() {
        return Err(AppError::Validation(
            "transfer passphrase is required".to_string(),
        ));
    }
    let mut salt = [0u8; SALT_LEN];
    let mut nonce_bytes = [0u8; NONCE_LEN];
    rand::thread_rng().fill_bytes(&mut salt);
    rand::thread_rng().fill_bytes(&mut nonce_bytes);
    let key = derive_transfer_key(passphrase, &salt)?;
    let cipher = Aes256Gcm::new_from_slice(&key)
        .map_err(|e| AppError::Keyring(format!("Failed to create transfer cipher: {e}")))?;
    let plaintext = serde_json::to_vec(payload)?;
    let ciphertext = cipher
        .encrypt(Nonce::from_slice(&nonce_bytes), plaintext.as_slice())
        .map_err(|e| AppError::Keyring(format!("Transfer artifact encryption failed: {e}")))?;
    let artifact = TransferArtifact {
        version: ARTIFACT_VERSION,
        kdf: "argon2id".to_string(),
        cipher: "aes-256-gcm".to_string(),
        salt: base64::prelude::BASE64_STANDARD.encode(salt),
        nonce: base64::prelude::BASE64_STANDARD.encode(nonce_bytes),
        ciphertext: base64::prelude::BASE64_STANDARD.encode(ciphertext),
    };
    serde_json::to_vec_pretty(&artifact).map_err(Into::into)
}

pub fn decrypt_transfer_artifact(
    artifact_bytes: &[u8],
    passphrase: &str,
) -> Result<CredentialExportPayload, AppError> {
    if passphrase.is_empty() {
        return Err(AppError::Validation(
            "transfer passphrase is required".to_string(),
        ));
    }
    let artifact: TransferArtifact = serde_json::from_slice(artifact_bytes)?;
    if artifact.version != ARTIFACT_VERSION
        || artifact.kdf != "argon2id"
        || artifact.cipher != "aes-256-gcm"
    {
        return Err(AppError::Validation(
            "unsupported transfer artifact".to_string(),
        ));
    }
    let salt = base64::prelude::BASE64_STANDARD
        .decode(artifact.salt)
        .map_err(|e| AppError::Validation(format!("invalid artifact salt: {e}")))?;
    let nonce = base64::prelude::BASE64_STANDARD
        .decode(artifact.nonce)
        .map_err(|e| AppError::Validation(format!("invalid artifact nonce: {e}")))?;
    let ciphertext = base64::prelude::BASE64_STANDARD
        .decode(artifact.ciphertext)
        .map_err(|e| AppError::Validation(format!("invalid artifact ciphertext: {e}")))?;
    if nonce.len() != NONCE_LEN {
        return Err(AppError::Validation(
            "invalid transfer nonce length".to_string(),
        ));
    }
    let key = derive_transfer_key(passphrase, &salt)?;
    let cipher = Aes256Gcm::new_from_slice(&key)
        .map_err(|e| AppError::Keyring(format!("Failed to create transfer cipher: {e}")))?;
    let plaintext = cipher
        .decrypt(Nonce::from_slice(&nonce), ciphertext.as_slice())
        .map_err(|e| AppError::Keyring(format!("Transfer artifact decryption failed: {e}")))?;
    serde_json::from_slice(&plaintext).map_err(Into::into)
}

fn derive_transfer_key(passphrase: &str, salt: &[u8]) -> Result<[u8; 32], AppError> {
    let mut key = [0u8; 32];
    Argon2::default()
        .hash_password_into(passphrase.as_bytes(), salt, &mut key)
        .map_err(|e| AppError::Keyring(format!("Transfer key derivation failed: {e}")))?;
    Ok(key)
}
