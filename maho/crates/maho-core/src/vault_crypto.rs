//! Versioned cryptographic envelopes and zeroizing key material for Maho Vault.

mod bounded_bytes;
mod envelope;
mod kdf;
mod keys;
mod metadata;
mod validation;

pub use envelope::{decrypt_record, encrypt_record, unwrap_vault_key, wrap_vault_key};
pub use kdf::{
    derive_account_wrapping_key, derive_device_wrapping_key, derive_recovery_wrapping_key,
    derive_user_wrapping_key,
};
pub use keys::{VaultKey, VaultKeyring, WrappedVaultKeyEnvelope, WrappingDomain, WrappingKey};
pub use metadata::{VaultKdfMetadata, VaultKdfParameters, VaultKdfVersion, VaultSalt};

pub const VAULT_SALT_LEN: usize = 16;
pub const NONCE_LEN: usize = 12;
pub const TAG_LEN: usize = 16;
pub const KEY_LEN: usize = 32;
pub const MAX_AAD_LEN: usize = 4096;
pub const MAX_PLAINTEXT_LEN: usize = maho_types::vault::MAX_VAULT_ENVELOPE_CIPHERTEXT_LEN;
pub(crate) const MAX_WRAPPED_KEY_CIPHERTEXT_LEN: usize = KEY_LEN * 2;

pub(crate) const ARGON2_MEMORY_KIB: u32 = 19_456;
pub(crate) const ARGON2_ITERATIONS: u32 = 2;
pub(crate) const ARGON2_PARALLELISM: u32 = 1;

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum VaultCryptoError {
    #[error("secure random generation failed")]
    Randomness,
    #[error("unsupported or malformed Vault KDF metadata")]
    InvalidKdfMetadata,
    #[error("Vault key derivation failed")]
    Derivation,
    #[error("Vault encryption failed")]
    Encryption,
    #[error("Vault authentication failed")]
    AuthenticationFailed,
    #[error("invalid Vault ciphertext envelope")]
    InvalidEnvelope,
    #[error("Vault cryptographic input exceeds its supported size")]
    InputTooLarge,
    #[error("ciphertext key version {envelope} does not match key version {key}")]
    KeyVersionMismatch { envelope: u32, key: u32 },
    #[error("wrapping domain does not match the wrapped key envelope")]
    WrappingDomainMismatch,
    #[error("Vault key version {version} is unavailable")]
    UnknownKeyVersion { version: u32 },
    #[error("Vault key version space is exhausted")]
    KeyVersionExhausted,
}

#[cfg(test)]
mod tests;
