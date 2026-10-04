//! Account-escrowed root-key operations: zero-input provisioning, wrapping, and
//! unlock for the "account is the vault root" model.
//!
//! The vault DEK stays random; the account wrap is an additional wrap slot
//! derived from the sync bootstrap seed with Argon2 domain separation
//! (`derive_account_wrapping_key`). A provisioned vault has ONLY the account
//! wrap; passphrase kit vaults gain the account wrap through the migration
//! path (`wrap_active_key_for_account`).

use chrono::{DateTime, Utc};
use maho_types::vault::VaultKeyVersion;

use crate::vault_crypto::{
    derive_account_wrapping_key, wrap_vault_key, VaultKey, VaultKdfMetadata, WrappedVaultKeyEnvelope,
};

use super::persistence;
use super::state::RuntimeState;
use super::{VaultAccountProvisionSlots, VaultManager, VaultManagerError, ACCOUNT_WRAP_AAD};

impl VaultManager {
    /// Provision a fresh account-escrowed Vault: random DEK wrapped ONLY by the
    /// account-derived key. Returns the opaque slots for `MahoCore` to persist
    /// transactionally; the caller must call [`Self::revert_uninitialized`] on
    /// persistence failure.
    pub fn initialize_account(
        &mut self,
        account_seed: &[u8],
        now: DateTime<Utc>,
    ) -> Result<VaultAccountProvisionSlots, VaultManagerError> {
        if self.state != RuntimeState::Uninitialized {
            return Err(VaultManagerError::AlreadyInitialized);
        }
        if account_seed.is_empty() {
            return Err(VaultManagerError::InvalidCredentials);
        }
        let metadata = VaultKdfMetadata::generate()?;
        let account_wrapping = derive_account_wrapping_key(account_seed, &metadata)?;
        let key = VaultKey::generate(VaultKeyVersion::new(1))?;
        let wrapped_account = wrap_vault_key(&key, &account_wrapping, ACCOUNT_WRAP_AAD)?;
        let slots =
            persistence::encode_account_provision_slots(&metadata, &wrapped_account)?;
        self.kdf_metadata = Some(metadata);
        self.wrapped_account_key = Some(wrapped_account);
        self.active_key = Some(key);
        self.state = RuntimeState::Unlocked;
        self.throttle.reset();
        self.last_activity = Some(now);
        Ok(slots)
    }

    /// Wrap the active DEK with the account-derived key, producing the slot
    /// payload for the account-escrow migration. Requires an unlocked Vault.
    /// The runtime is only updated via [`Self::store_account_wrap`] after the
    /// caller persists the slot, so status never claims escrow durably missing.
    pub fn wrap_active_key_for_account(
        &self,
        account_seed: &[u8],
    ) -> Result<WrappedVaultKeyEnvelope, VaultManagerError> {
        let key = self.active_key()?;
        let metadata = self
            .kdf_metadata
            .as_ref()
            .ok_or(VaultManagerError::Uninitialized)?;
        if account_seed.is_empty() {
            return Err(VaultManagerError::InvalidCredentials);
        }
        let wrapping_key = derive_account_wrapping_key(account_seed, metadata)?;
        Ok(wrap_vault_key(key, &wrapping_key, ACCOUNT_WRAP_AAD)?)
    }

    pub fn store_account_wrap(&mut self, envelope: WrappedVaultKeyEnvelope) {
        self.wrapped_account_key = Some(envelope);
    }

    pub fn clear_account_wrap(&mut self) {
        self.wrapped_account_key = None;
    }

    pub fn has_account_wrap(&self) -> bool {
        self.wrapped_account_key.is_some()
    }
}
