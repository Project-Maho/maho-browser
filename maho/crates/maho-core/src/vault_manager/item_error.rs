//! Typed, secret-free errors for the Todo 11 Vault CRUD surface.
//!
//! Distinct from lifecycle [`VaultManagerError`]: these describe item-level
//! outcomes (not-found, revision conflict, duplicate, invalid origin/kind/field,
//! malformed payload) that privileged callers must handle. Storage failures are
//! carried opaquely (never string-parsed for control flow) and no variant ever
//! embeds a decrypted secret.

use maho_types::vault::VaultItemId;

use crate::vault_crypto::VaultCryptoError;

use super::VaultManagerError;

#[derive(Debug, thiserror::Error)]
#[non_exhaustive]
pub enum VaultCrudError {
    #[error("Vault is locked")]
    Locked,
    #[error("Vault item not found")]
    ItemNotFound,
    #[error("revision conflict: expected {expected}, actual {actual}")]
    RevisionConflict { expected: u64, actual: u64 },
    #[error("duplicate Vault item {existing} already exists")]
    Duplicate { existing: VaultItemId },
    #[error("invalid credential origin: {value}")]
    InvalidOrigin { value: String },
    #[error("item kind does not match the requested operation")]
    ItemKindMismatch,
    #[error("requested field is unavailable on this item kind")]
    FieldMismatch,
    #[error("Vault item payload is malformed or tampered")]
    MalformedPayload,
    #[error("Vault storage failure: {0}")]
    Storage(String),
    #[error("Vault cryptographic failure")]
    Crypto,
}

impl From<VaultManagerError> for VaultCrudError {
    fn from(error: VaultManagerError) -> Self {
        match error {
            VaultManagerError::VaultLocked | VaultManagerError::Uninitialized => Self::Locked,
            VaultManagerError::Storage(message) => Self::Storage(message),
            VaultManagerError::CorruptedVaultData => Self::MalformedPayload,
            VaultManagerError::Crypto(
                VaultCryptoError::AuthenticationFailed | VaultCryptoError::InvalidEnvelope,
            ) => Self::MalformedPayload,
            VaultManagerError::Crypto(_)
            | VaultManagerError::DeviceProtector(_)
            | VaultManagerError::DeviceWrapper(_)
            | VaultManagerError::DeviceBindingMismatch => Self::Crypto,
            VaultManagerError::NoAccountWrap => {
                Self::Storage("unexpected vault lifecycle state during item operation".to_string())
            }
            VaultManagerError::AlreadyInitialized
            | VaultManagerError::RateLimited
            | VaultManagerError::InvalidCredentials => {
                Self::Storage("unexpected vault lifecycle state during item operation".to_string())
            }
        }
    }
}
