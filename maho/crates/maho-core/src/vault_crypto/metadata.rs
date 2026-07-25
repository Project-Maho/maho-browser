use std::fmt;

use serde::{Deserialize, Serialize};

use super::{
    VaultCryptoError, ARGON2_ITERATIONS, ARGON2_MEMORY_KIB, ARGON2_PARALLELISM, VAULT_SALT_LEN,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum VaultKdfVersion {
    V1,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct VaultKdfParameters {
    pub memory_kib: u32,
    pub iterations: u32,
    pub parallelism: u32,
}

#[derive(Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(transparent)]
pub struct VaultSalt(pub(super) [u8; VAULT_SALT_LEN]);

impl fmt::Debug for VaultSalt {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("VaultSalt([REDACTED])")
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(
    rename_all = "camelCase",
    deny_unknown_fields,
    try_from = "VaultKdfMetadataWire",
    into = "VaultKdfMetadataWire"
)]
pub struct VaultKdfMetadata {
    version: VaultKdfVersion,
    memory_kib: u32,
    iterations: u32,
    parallelism: u32,
    pub(super) salt: VaultSalt,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultKdfMetadataWire {
    version: VaultKdfVersion,
    memory_kib: u32,
    iterations: u32,
    parallelism: u32,
    salt: VaultSalt,
}

impl TryFrom<VaultKdfMetadataWire> for VaultKdfMetadata {
    type Error = VaultCryptoError;

    fn try_from(value: VaultKdfMetadataWire) -> Result<Self, Self::Error> {
        let metadata = Self {
            version: value.version,
            memory_kib: value.memory_kib,
            iterations: value.iterations,
            parallelism: value.parallelism,
            salt: value.salt,
        };
        metadata.validate()?;
        Ok(metadata)
    }
}

impl From<VaultKdfMetadata> for VaultKdfMetadataWire {
    fn from(value: VaultKdfMetadata) -> Self {
        Self {
            version: value.version,
            memory_kib: value.memory_kib,
            iterations: value.iterations,
            parallelism: value.parallelism,
            salt: value.salt,
        }
    }
}

impl VaultKdfMetadata {
    pub fn generate() -> Result<Self, VaultCryptoError> {
        let mut salt = [0; VAULT_SALT_LEN];
        rand::RngCore::try_fill_bytes(&mut rand::rngs::OsRng, &mut salt)
            .map_err(|_| VaultCryptoError::Randomness)?;
        Ok(Self {
            version: VaultKdfVersion::V1,
            memory_kib: ARGON2_MEMORY_KIB,
            iterations: ARGON2_ITERATIONS,
            parallelism: ARGON2_PARALLELISM,
            salt: VaultSalt(salt),
        })
    }

    pub const fn version(&self) -> VaultKdfVersion {
        self.version
    }

    pub const fn parameters(&self) -> VaultKdfParameters {
        VaultKdfParameters {
            memory_kib: self.memory_kib,
            iterations: self.iterations,
            parallelism: self.parallelism,
        }
    }

    pub const fn salt_len(&self) -> usize {
        self.salt.0.len()
    }

    pub(super) fn validate(&self) -> Result<(), VaultCryptoError> {
        match self.version {
            VaultKdfVersion::V1
                if self.memory_kib == ARGON2_MEMORY_KIB
                    && self.iterations == ARGON2_ITERATIONS
                    && self.parallelism == ARGON2_PARALLELISM =>
            {
                Ok(())
            }
            VaultKdfVersion::V1 => Err(VaultCryptoError::InvalidKdfMetadata),
        }
    }

    #[cfg(test)]
    pub(super) const fn salt_for_test(&self) -> &[u8; VAULT_SALT_LEN] {
        &self.salt.0
    }
}
