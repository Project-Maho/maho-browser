//! Client-held key mode: rotate the Vault DEK, re-encrypt every stored item
//! under the new key, and rewrap it with recovery-phrase-derived user and
//! recovery slots instead of the account (escrow) wrap.
//!
//! Rotation is two-phase so `MahoCore` can persist atomically: `prepare`
//! computes the new key, slots and re-encrypted rows without touching the
//! runtime; `commit` installs them only after the caller durably persisted
//! everything. A failed persist simply drops the prepared state, leaving the
//! current (escrow) vault untouched and unlocked.

use std::str::FromStr;

use chrono::{DateTime, Utc};
use maho_storage::sqlite::EncryptedVaultItemRow;
use maho_types::vault::{VaultItemId, VaultKeyVersion};

use crate::vault_crypto::{
    decrypt_record, derive_recovery_wrapping_key, derive_user_wrapping_key, encrypt_record,
    wrap_vault_key, VaultCryptoError, VaultKdfMetadata, VaultKey, WrappedVaultKeyEnvelope,
};

use super::persistence::{self, VaultPersistedSlots};
use super::record::{decode_envelope, encode_envelope};
use super::state::RuntimeState;
use super::{VaultManager, VaultManagerError, MANAGER_WRAP_AAD};

/// New key material for a client-held vault, held until the caller commits.
pub struct PreparedClientHeldVault {
    key: VaultKey,
    metadata: VaultKdfMetadata,
    wrapped_user_key: WrappedVaultKeyEnvelope,
    wrapped_recovery_key: WrappedVaultKeyEnvelope,
    slots: VaultPersistedSlots,
    rows: Vec<EncryptedVaultItemRow>,
}

impl PreparedClientHeldVault {
    /// Opaque kdf / user / recovery slot payloads to persist.
    pub fn slots(&self) -> &VaultPersistedSlots {
        &self.slots
    }

    /// Item rows re-encrypted under the rotated key (same ids, revision + 1).
    pub fn rows(&self) -> &[EncryptedVaultItemRow] {
        &self.rows
    }
}

impl VaultManager {
    /// Prepares a DEK rotation into client-held mode. An unlocked vault rotates
    /// to `version + 1` and re-encrypts every row with it; an uninitialized
    /// vault gets a fresh version-1 key and no rows. Both wrap slots derive from
    /// `recovery_secret` (the canonical recovery phrase). Fail-closed: any row
    /// that cannot be decrypted with the current key aborts the rotation, since
    /// it would otherwise become unreadable.
    pub fn prepare_client_held_rotation(
        &self,
        recovery_secret: &[u8],
        rows: &[EncryptedVaultItemRow],
    ) -> Result<PreparedClientHeldVault, VaultManagerError> {
        if recovery_secret.is_empty() {
            return Err(VaultManagerError::InvalidCredentials);
        }
        let (key, rows) = match self.state {
            RuntimeState::Uninitialized => (VaultKey::generate(VaultKeyVersion::new(1))?, Vec::new()),
            _ => {
                let current = self.active_key()?;
                let next = current
                    .version()
                    .value()
                    .checked_add(1)
                    .ok_or(VaultCryptoError::KeyVersionExhausted)?;
                let key = VaultKey::generate(VaultKeyVersion::new(next))?;
                let rows = rows
                    .iter()
                    .map(|row| reencrypt_row(row, current, &key))
                    .collect::<Result<Vec<_>, _>>()?;
                (key, rows)
            }
        };
        let metadata = VaultKdfMetadata::generate()?;
        let user_wrapping = derive_user_wrapping_key(recovery_secret, &metadata)?;
        let recovery_wrapping = derive_recovery_wrapping_key(recovery_secret, &metadata)?;
        let wrapped_user_key = wrap_vault_key(&key, &user_wrapping, MANAGER_WRAP_AAD)?;
        let wrapped_recovery_key = wrap_vault_key(&key, &recovery_wrapping, MANAGER_WRAP_AAD)?;
        let slots = persistence::encode_slots(&metadata, &wrapped_user_key, &wrapped_recovery_key)?;
        Ok(PreparedClientHeldVault {
            key,
            metadata,
            wrapped_user_key,
            wrapped_recovery_key,
            slots,
            rows,
        })
    }

    /// Installs a prepared rotation after it was persisted: the new key is
    /// active, the passphrase/recovery slots replace the old ones, and the
    /// account wrap is removed. The previous key is dropped (zeroized).
    pub fn commit_client_held_rotation(
        &mut self,
        prepared: PreparedClientHeldVault,
        now: DateTime<Utc>,
    ) {
        let PreparedClientHeldVault {
            key,
            metadata,
            wrapped_user_key,
            wrapped_recovery_key,
            ..
        } = prepared;
        self.kdf_metadata = Some(metadata);
        self.wrapped_user_key = Some(wrapped_user_key);
        self.wrapped_recovery_key = Some(wrapped_recovery_key);
        self.clear_account_wrap();
        self.active_key = Some(key);
        self.state = RuntimeState::Unlocked;
        self.throttle.reset();
        self.last_activity = Some(now);
    }
}

fn reencrypt_row(
    row: &EncryptedVaultItemRow,
    current: &VaultKey,
    next: &VaultKey,
) -> Result<EncryptedVaultItemRow, VaultManagerError> {
    let id = VaultItemId::from_str(&row.id).map_err(|_| VaultManagerError::CorruptedVaultData)?;
    let aad = id.as_uuid().as_bytes();
    let envelope =
        decode_envelope(&row.envelope).map_err(|_| VaultManagerError::CorruptedVaultData)?;
    let plaintext = decrypt_record(&envelope, current, aad)?;
    let rotated = encrypt_record(&plaintext, next, envelope.purpose, aad)?;
    let mut updated = row.clone();
    // A new revision makes any concurrent CAS write against the pre-rotation
    // revision (carrying an old-key envelope) fail instead of landing.
    updated.revision = row
        .revision
        .checked_add(1)
        .ok_or(VaultManagerError::CorruptedVaultData)?;
    updated.envelope =
        encode_envelope(&rotated).map_err(|_| VaultManagerError::CorruptedVaultData)?;
    Ok(updated)
}
