//! Crypto-facing operations on an unlocked [`VaultManager`]: the protected-read
//! seam plus record encrypt/decrypt and origin matching.

use maho_types::vault::{
    CredentialOrigin, VaultCiphertextEnvelope, VaultCiphertextPurpose, VaultItemId,
    VaultItemPublicDto,
};
use zeroize::Zeroizing;

use crate::vault_crypto::{decrypt_record, encrypt_record};

use super::{VaultManager, VaultManagerError};

/// Narrow reader seam for payload-bearing storage/downstream reads. The Vault
/// core resolves the lock gate *before* invoking this, guaranteeing that a
/// locked Vault denies with [`VaultManagerError::VaultLocked`] before any
/// ciphertext payload is fetched.
pub trait ProtectedEnvelopeReader {
    fn load_envelope(
        &self,
        item_id: &VaultItemId,
    ) -> Result<VaultCiphertextEnvelope, VaultManagerError>;
}

impl VaultManager {
    /// Encrypt an owned secret into a record envelope bound to `item_id`.
    /// Requires an unlocked Vault.
    pub fn encrypt_and_wrap(
        &self,
        plaintext_secret: &[u8],
        item_id: &VaultItemId,
    ) -> Result<VaultCiphertextEnvelope, VaultManagerError> {
        Ok(encrypt_record(
            plaintext_secret,
            self.active_key()?,
            VaultCiphertextPurpose::VaultItem,
            item_id.as_uuid().as_bytes(),
        )?)
    }

    /// Decrypt a record envelope bound to `item_id`. Requires an unlocked Vault.
    pub fn decrypt_record(
        &self,
        envelope: &VaultCiphertextEnvelope,
        item_id: &VaultItemId,
    ) -> Result<Zeroizing<Vec<u8>>, VaultManagerError> {
        Ok(decrypt_record(
            envelope,
            self.active_key()?,
            item_id.as_uuid().as_bytes(),
        )?)
    }

    /// Protected read: deny before any downstream payload access when locked,
    /// otherwise fetch the envelope through `reader` and decrypt it.
    pub fn read_protected<R: ProtectedEnvelopeReader>(
        &self,
        reader: &R,
        item_id: &VaultItemId,
    ) -> Result<Zeroizing<Vec<u8>>, VaultManagerError> {
        // Lock gate first: no payload-bearing reader runs while locked.
        let key = self.active_key()?;
        let envelope = reader.load_envelope(item_id)?;
        Ok(decrypt_record(
            &envelope,
            key,
            item_id.as_uuid().as_bytes(),
        )?)
    }

    /// Metadata-only exact-origin match over public item DTOs.
    pub fn match_origin<'a>(
        &self,
        items: &'a [VaultItemPublicDto],
        target_origin: &CredentialOrigin,
    ) -> Vec<&'a VaultItemPublicDto> {
        items
            .iter()
            .filter(|item| item.origins.iter().any(|origin| origin == target_origin))
            .collect()
    }
}
