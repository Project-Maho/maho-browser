use std::sync::Mutex;

use chrono::{DateTime, Utc};
use maho_types::vault::{TotpAlgorithm, VaultCiphertextEnvelope, VaultItemId, VaultStatus};
use zeroize::Zeroizing;

use crate::oscrypt::{VaultDeviceBinding, VaultDeviceProtector, VaultDeviceWrapBundle};
use crate::vault_crypto::WrappedVaultKeyEnvelope;
use crate::vault_manager::{
    VaultAccountProvisionSlots, VaultHydrationOutcome, VaultHydrationSlots, VaultManager,
    VaultManagerError, VaultPersistedSlots,
};

pub(crate) mod audit;
mod backend_batch;
mod crud_create;
mod crud_mutate_use;
mod crud_query;
mod parity;
pub(crate) mod repository;
pub mod session;

pub struct VaultRuntime {
    manager: Mutex<VaultManager>,
}

pub(crate) enum VaultSecretResult {
    LoginPassword(Zeroizing<String>),
    Totp {
        seed: Zeroizing<Vec<u8>>,
        algorithm: TotpAlgorithm,
        digits: u32,
        period_seconds: u64,
    },
    PasskeyPrivateKey(Zeroizing<Vec<u8>>),
    SecureBytes(Zeroizing<Vec<u8>>),
}

impl Default for VaultRuntime {
    fn default() -> Self {
        Self::new()
    }
}

impl VaultRuntime {
    pub fn new() -> Self {
        Self {
            manager: Mutex::new(VaultManager::new()),
        }
    }

    pub fn status(&self) -> VaultStatus {
        match self.manager.lock() {
            Ok(manager) => manager.status(),
            Err(poisoned) => {
                let mut manager = poisoned.into_inner();
                manager.lock();
                manager.status()
            }
        }
    }

    pub fn hydrate_locked(
        &self,
        slots: VaultHydrationSlots,
    ) -> Result<VaultHydrationOutcome, VaultManagerError> {
        self.with_manager(|manager| manager.hydrate_locked(slots))?
    }

    pub fn initialize(
        &self,
        master_passphrase: &[u8],
        recovery_secret: &[u8],
        now: DateTime<Utc>,
    ) -> Result<VaultPersistedSlots, VaultManagerError> {
        self.with_manager(|manager| manager.initialize(master_passphrase, recovery_secret, now))?
    }

    pub fn initialize_and_persist(
        &self,
        master_passphrase: &[u8],
        recovery_secret: &[u8],
        now: DateTime<Utc>,
        persist: impl FnOnce(&VaultPersistedSlots) -> Result<(), VaultManagerError>,
    ) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| {
            let slots = manager.initialize(master_passphrase, recovery_secret, now)?;
            if let Err(error) = persist(&slots) {
                manager.revert_uninitialized();
                return Err(error);
            }
            Ok(())
        })?
    }

    pub fn initialize_account_and_persist(
        &self,
        account_seed: &[u8],
        now: DateTime<Utc>,
        persist: impl FnOnce(&VaultAccountProvisionSlots) -> Result<(), VaultManagerError>,
    ) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| {
            let slots = manager.initialize_account(account_seed, now)?;
            if let Err(error) = persist(&slots) {
                manager.revert_uninitialized();
                return Err(error);
            }
            Ok(())
        })?
    }

    pub fn wrap_account_key_and_persist(
        &self,
        account_seed: &[u8],
        persist: impl FnOnce(&WrappedVaultKeyEnvelope) -> Result<(), VaultManagerError>,
    ) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| {
            if manager.has_account_wrap() {
                return Err(VaultManagerError::AlreadyInitialized);
            }
            let envelope = manager.wrap_active_key_for_account(account_seed)?;
            if let Err(error) = persist(&envelope) {
                manager.lock();
                return Err(error);
            }
            manager.store_account_wrap(envelope);
            Ok(())
        })?
    }

    pub fn unlock_with_account(
        &self,
        account_seed: &[u8],
        now: DateTime<Utc>,
    ) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| manager.unlock_with_account(account_seed, now))?
    }

    pub fn wrap_active_key_for_account(
        &self,
        account_seed: &[u8],
    ) -> Result<WrappedVaultKeyEnvelope, VaultManagerError> {
        self.with_manager(|manager| manager.wrap_active_key_for_account(account_seed))?
    }

    pub fn has_account_wrap(&self) -> bool {
        self.with_manager(|manager| manager.has_account_wrap()).unwrap_or(false)
    }

    pub fn initialize_with_device_and_persist(
        &self,
        master_passphrase: &[u8],
        recovery_secret: &[u8],
        binding: &VaultDeviceBinding,
        protector: &dyn VaultDeviceProtector,
        now: DateTime<Utc>,
        persist: impl FnOnce(
            &VaultPersistedSlots,
            &VaultDeviceWrapBundle,
        ) -> Result<(), VaultManagerError>,
    ) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| {
            let slots = manager.initialize(master_passphrase, recovery_secret, now)?;
            let bundle = match manager.wrap_active_key_for_device(binding, protector) {
                Ok(bundle) => bundle,
                Err(error) => {
                    manager.revert_uninitialized();
                    return Err(error);
                }
            };
            if let Err(error) = persist(&slots, &bundle) {
                manager.revert_uninitialized();
                return Err(error);
            }
            Ok(())
        })?
    }

    pub fn revert_uninitialized(&self) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| manager.revert_uninitialized())
    }

    pub fn unlock(
        &self,
        master_passphrase: &[u8],
        now: DateTime<Utc>,
    ) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| manager.unlock(master_passphrase, now))?
    }

    pub fn unlock_with_recovery(
        &self,
        recovery_secret: &[u8],
        now: DateTime<Utc>,
    ) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| manager.unlock_with_recovery(recovery_secret, now))?
    }

    pub fn unlock_with_device(
        &self,
        bundle: &VaultDeviceWrapBundle,
        binding: &VaultDeviceBinding,
        protector: &dyn VaultDeviceProtector,
        now: DateTime<Utc>,
    ) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| manager.unlock_with_device(bundle, binding, protector, now))?
    }

    pub fn wrap_active_key_for_device(
        &self,
        binding: &VaultDeviceBinding,
        protector: &dyn VaultDeviceProtector,
    ) -> Result<VaultDeviceWrapBundle, VaultManagerError> {
        self.with_manager(|manager| manager.wrap_active_key_for_device(binding, protector))?
    }

    pub fn rewrap_device_with_recovery_and_persist(
        &self,
        recovery_secret: &[u8],
        binding: &VaultDeviceBinding,
        protector: &dyn VaultDeviceProtector,
        now: DateTime<Utc>,
        persist: impl FnOnce(&VaultDeviceWrapBundle) -> Result<(), VaultManagerError>,
    ) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| {
            manager.unlock_with_recovery(recovery_secret, now)?;
            let bundle = match manager.wrap_active_key_for_device(binding, protector) {
                Ok(bundle) => bundle,
                Err(error) => {
                    manager.lock();
                    return Err(error);
                }
            };
            if let Err(error) = persist(&bundle) {
                manager.lock();
                return Err(error);
            }
            Ok(())
        })?
    }

    pub fn lock(&self) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| manager.lock())
    }

    pub fn evaluate_auto_lock(&self, now: DateTime<Utc>) -> Result<bool, VaultManagerError> {
        self.with_manager(|manager| manager.evaluate_auto_lock(now))
    }

    pub fn record_activity(&self, now: DateTime<Utc>) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| manager.record_activity(now))
    }

    pub fn set_auto_lock_minutes(&self, minutes: u32) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| manager.set_auto_lock_minutes(minutes))
    }

    pub fn set_item_count(&self, count: u64) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| manager.set_item_count(count))
    }

    pub fn ensure_unlocked(&self) -> Result<(), VaultManagerError> {
        self.with_manager(|manager| manager.active_key().map(|_| ()))?
    }

    pub fn encrypt_and_wrap(
        &self,
        plaintext_secret: &[u8],
        item_id: &VaultItemId,
    ) -> Result<VaultCiphertextEnvelope, VaultManagerError> {
        self.with_manager(|manager| manager.encrypt_and_wrap(plaintext_secret, item_id))?
    }

    pub fn decrypt_record(
        &self,
        envelope: &VaultCiphertextEnvelope,
        item_id: &VaultItemId,
    ) -> Result<Zeroizing<Vec<u8>>, VaultManagerError> {
        self.with_manager(|manager| manager.decrypt_record(envelope, item_id))?
    }

    fn with_manager<T>(
        &self,
        operation: impl FnOnce(&mut VaultManager) -> T,
    ) -> Result<T, VaultManagerError> {
        match self.manager.lock() {
            Ok(mut manager) => Ok(operation(&mut manager)),
            Err(poisoned) => {
                let mut manager = poisoned.into_inner();
                manager.lock();
                Err(VaultManagerError::Storage(
                    "vault runtime mutex poisoned".to_string(),
                ))
            }
        }
    }
}

#[cfg(test)]
mod tests;
