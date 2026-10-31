use std::collections::HashMap;
use std::fmt;

use maho_types::vault::{
    VaultCiphertextEnvelope, VaultCiphertextPurpose, VaultKeyVersion, VaultSchemaVersion,
};
use serde::{Deserialize, Serialize};
use zeroize::{Zeroize, ZeroizeOnDrop, Zeroizing};

use super::{decrypt_record, encrypt_record, VaultCryptoError, KEY_LEN};

#[derive(Zeroize, ZeroizeOnDrop)]
pub struct VaultKey {
    pub(super) bytes: [u8; KEY_LEN],
    #[zeroize(skip)]
    pub(super) version: VaultKeyVersion,
    #[cfg(test)]
    probe: Option<ZeroizeProbe>,
}

impl fmt::Debug for VaultKey {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("VaultKey")
            .field("version", &self.version)
            .field("bytes", &"[REDACTED]")
            .finish()
    }
}

impl VaultKey {
    pub fn generate(version: VaultKeyVersion) -> Result<Self, VaultCryptoError> {
        let mut bytes = Zeroizing::new([0; KEY_LEN]);
        rand::RngCore::try_fill_bytes(&mut rand::rngs::OsRng, bytes.as_mut())
            .map_err(|_| VaultCryptoError::Randomness)?;
        Ok(Self::from_bytes(*bytes, version))
    }

    pub const fn version(&self) -> VaultKeyVersion {
        self.version
    }

    pub(super) fn from_bytes(bytes: [u8; KEY_LEN], version: VaultKeyVersion) -> Self {
        Self {
            bytes,
            version,
            #[cfg(test)]
            probe: None,
        }
    }

    #[cfg(test)]
    pub(super) fn generate_with_probe(
        version: VaultKeyVersion,
        zeroized: std::sync::Arc<std::sync::atomic::AtomicBool>,
    ) -> Result<Self, VaultCryptoError> {
        let mut key = Self::generate(version)?;
        key.probe = Some(ZeroizeProbe(zeroized));
        Ok(key)
    }

    #[cfg(test)]
    pub(super) const fn bytes_for_test(&self) -> &[u8; KEY_LEN] {
        &self.bytes
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize, Zeroize)]
#[serde(rename_all = "snake_case")]
pub enum WrappingDomain {
    User,
    Recovery,
    Device,
    Account,
}

#[derive(Zeroize, ZeroizeOnDrop)]
pub struct WrappingKey {
    pub(super) bytes: [u8; KEY_LEN],
    #[zeroize(skip)]
    pub(super) domain: WrappingDomain,
}

impl fmt::Debug for WrappingKey {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("WrappingKey")
            .field("domain", &self.domain)
            .field("bytes", &"[REDACTED]")
            .finish()
    }
}

impl WrappingKey {
    #[cfg(test)]
    pub(super) const fn bytes_for_test(&self) -> &[u8; KEY_LEN] {
        &self.bytes
    }
}

#[derive(Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct WrappedVaultKeyEnvelope {
    pub schema_version: VaultSchemaVersion,
    pub wrapping_domain: WrappingDomain,
    pub key_version: VaultKeyVersion,
    #[serde(deserialize_with = "super::bounded_bytes::nonce")]
    pub nonce: Vec<u8>,
    #[serde(deserialize_with = "super::bounded_bytes::ciphertext")]
    pub ciphertext: Vec<u8>,
    #[serde(deserialize_with = "super::bounded_bytes::tag")]
    pub tag: Vec<u8>,
}

impl fmt::Debug for WrappedVaultKeyEnvelope {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("WrappedVaultKeyEnvelope")
            .field("schema_version", &self.schema_version)
            .field("wrapping_domain", &self.wrapping_domain)
            .field("key_version", &self.key_version)
            .field("nonce", &"[REDACTED]")
            .field("ciphertext", &"[REDACTED]")
            .field("tag", &"[REDACTED]")
            .finish()
    }
}

pub struct VaultKeyring {
    keys: HashMap<VaultKeyVersion, VaultKey>,
    current_version: VaultKeyVersion,
}

impl fmt::Debug for VaultKeyring {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("VaultKeyring")
            .field("current_version", &self.current_version)
            .field("retained_key_count", &self.keys.len())
            .finish_non_exhaustive()
    }
}

impl VaultKeyring {
    pub fn generate() -> Result<Self, VaultCryptoError> {
        let initial = VaultKey::generate(VaultKeyVersion::new(1))?;
        Ok(Self::new(initial))
    }

    pub fn new(initial: VaultKey) -> Self {
        let current_version = initial.version;
        Self {
            keys: HashMap::from([(current_version, initial)]),
            current_version,
        }
    }

    pub fn rotate(&mut self) -> Result<VaultKeyVersion, VaultCryptoError> {
        let next = self
            .current_version
            .value()
            .checked_add(1)
            .ok_or(VaultCryptoError::KeyVersionExhausted)?;
        let version = VaultKeyVersion::new(next);
        self.keys.insert(version, VaultKey::generate(version)?);
        self.current_version = version;
        Ok(version)
    }

    pub fn encrypt(
        &self,
        plaintext: &[u8],
        purpose: VaultCiphertextPurpose,
        aad: &[u8],
    ) -> Result<VaultCiphertextEnvelope, VaultCryptoError> {
        let key =
            self.keys
                .get(&self.current_version)
                .ok_or(VaultCryptoError::UnknownKeyVersion {
                    version: self.current_version.value(),
                })?;
        encrypt_record(plaintext, key, purpose, aad)
    }

    pub fn decrypt(
        &self,
        envelope: &VaultCiphertextEnvelope,
        aad: &[u8],
    ) -> Result<Zeroizing<Vec<u8>>, VaultCryptoError> {
        let key =
            self.keys
                .get(&envelope.key_version)
                .ok_or(VaultCryptoError::UnknownKeyVersion {
                    version: envelope.key_version.value(),
                })?;
        decrypt_record(envelope, key, aad)
    }
}

#[cfg(test)]
struct ZeroizeProbe(std::sync::Arc<std::sync::atomic::AtomicBool>);

#[cfg(test)]
impl Zeroize for ZeroizeProbe {
    fn zeroize(&mut self) {
        self.0.store(true, std::sync::atomic::Ordering::SeqCst);
    }
}
