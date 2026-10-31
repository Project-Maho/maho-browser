use argon2::{Algorithm, Argon2, AssociatedData, ParamsBuilder, Version};
use zeroize::Zeroizing;

use super::{
    VaultCryptoError, VaultKdfMetadata, VaultKdfVersion, WrappingDomain, WrappingKey, KEY_LEN,
};

const USER_DOMAIN: &[u8] = b"maho-vault-v1/user-wrapping";
const RECOVERY_DOMAIN: &[u8] = b"maho-vault-v1/recovery-wrapping";
const DEVICE_DOMAIN: &[u8] = b"maho-vault-v1/device-wrapping";
const ACCOUNT_DOMAIN: &[u8] = b"maho-vault-v1/account-wrapping";

pub fn derive_user_wrapping_key(
    secret: &[u8],
    metadata: &VaultKdfMetadata,
) -> Result<WrappingKey, VaultCryptoError> {
    derive_wrapping_key(secret, metadata, WrappingDomain::User)
}

pub fn derive_recovery_wrapping_key(
    secret: &[u8],
    metadata: &VaultKdfMetadata,
) -> Result<WrappingKey, VaultCryptoError> {
    derive_wrapping_key(secret, metadata, WrappingDomain::Recovery)
}

pub fn derive_device_wrapping_key(
    secret: &[u8],
    metadata: &VaultKdfMetadata,
) -> Result<WrappingKey, VaultCryptoError> {
    derive_wrapping_key(secret, metadata, WrappingDomain::Device)
}

/// Derives the account-escrow wrapping key from the sync bootstrap seed. The
/// account seed is shared with the Sync E2EE derivation, so the vault domain is
/// separated at the Argon2 associated-data layer: the same seed must never
/// yield the sync key and the vault wrapping key.
pub fn derive_account_wrapping_key(
    secret: &[u8],
    metadata: &VaultKdfMetadata,
) -> Result<WrappingKey, VaultCryptoError> {
    derive_wrapping_key(secret, metadata, WrappingDomain::Account)
}

fn derive_wrapping_key(
    secret: &[u8],
    metadata: &VaultKdfMetadata,
    domain: WrappingDomain,
) -> Result<WrappingKey, VaultCryptoError> {
    metadata.validate()?;
    let associated_data = match domain {
        WrappingDomain::User => USER_DOMAIN,
        WrappingDomain::Recovery => RECOVERY_DOMAIN,
        WrappingDomain::Device => DEVICE_DOMAIN,
        WrappingDomain::Account => ACCOUNT_DOMAIN,
    };
    let parameters = metadata.parameters();
    let mut builder = ParamsBuilder::new();
    builder
        .m_cost(parameters.memory_kib)
        .t_cost(parameters.iterations)
        .p_cost(parameters.parallelism)
        .output_len(KEY_LEN)
        .data(AssociatedData::new(associated_data).map_err(|_| VaultCryptoError::Derivation)?);
    let params = builder.build().map_err(|_| VaultCryptoError::Derivation)?;
    let argon2 = match metadata.version() {
        VaultKdfVersion::V1 => Argon2::new(Algorithm::Argon2id, Version::V0x13, params),
    };
    let mut bytes = Zeroizing::new([0; KEY_LEN]);
    argon2
        .hash_password_into(secret, &metadata.salt.0, bytes.as_mut())
        .map_err(|_| VaultCryptoError::Derivation)?;
    Ok(WrappingKey {
        bytes: *bytes,
        domain,
    })
}
