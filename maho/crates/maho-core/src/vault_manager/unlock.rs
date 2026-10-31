//! Unlock paths (user + recovery) with deterministic rate-limit gating and the
//! private `Unlocking` transient. Split from the manager root to keep each file
//! within the responsibility/size budget; as a child module it retains access to
//! `VaultManager`'s private fields.

use chrono::{DateTime, Utc};

use crate::vault_crypto::{
    derive_account_wrapping_key, derive_recovery_wrapping_key, derive_user_wrapping_key,
    unwrap_vault_key, VaultCryptoError,
};

use super::state::RuntimeState;
use super::{VaultManager, VaultManagerError, ACCOUNT_WRAP_AAD, MANAGER_WRAP_AAD};

#[derive(Clone, Copy)]
enum UnlockKind {
    User,
    Recovery,
    Account,
}

impl VaultManager {
    pub fn unlock(
        &mut self,
        master_passphrase: &[u8],
        now: DateTime<Utc>,
    ) -> Result<(), VaultManagerError> {
        self.attempt_unlock(master_passphrase, now, UnlockKind::User)
    }

    pub fn unlock_with_recovery(
        &mut self,
        recovery_secret: &[u8],
        now: DateTime<Utc>,
    ) -> Result<(), VaultManagerError> {
        self.attempt_unlock(recovery_secret, now, UnlockKind::Recovery)
    }

    pub fn unlock_with_account(
        &mut self,
        account_seed: &[u8],
        now: DateTime<Utc>,
    ) -> Result<(), VaultManagerError> {
        self.attempt_unlock(account_seed, now, UnlockKind::Account)
    }

    fn attempt_unlock(
        &mut self,
        secret: &[u8],
        now: DateTime<Utc>,
        kind: UnlockKind,
    ) -> Result<(), VaultManagerError> {
        // Rate-limit gate runs BEFORE any key derivation.
        if self.throttle.check(now).is_err() {
            return Err(VaultManagerError::RateLimited);
        }
        let metadata = self
            .kdf_metadata
            .clone()
            .ok_or(VaultManagerError::Uninitialized)?;
        let envelope = match kind {
            UnlockKind::User => self.wrapped_user_key.clone(),
            UnlockKind::Recovery => self.wrapped_recovery_key.clone(),
            UnlockKind::Account => self.wrapped_account_key.clone(),
        }
        .ok_or(VaultManagerError::NoAccountWrap)?;
        let wrapping_key = match kind {
            UnlockKind::User => derive_user_wrapping_key(secret, &metadata)?,
            UnlockKind::Recovery => derive_recovery_wrapping_key(secret, &metadata)?,
            UnlockKind::Account => derive_account_wrapping_key(secret, &metadata)?,
        };
        let aad: &[u8] = match kind {
            UnlockKind::Account => ACCOUNT_WRAP_AAD,
            UnlockKind::User | UnlockKind::Recovery => MANAGER_WRAP_AAD,
        };
        self.state = RuntimeState::Unlocking;
        let key = match unwrap_vault_key(&envelope, &wrapping_key, aad) {
            Ok(key) => key,
            Err(VaultCryptoError::AuthenticationFailed) => {
                self.state = RuntimeState::Locked;
                self.throttle.record_failure(now);
                return Err(VaultManagerError::InvalidCredentials);
            }
            Err(
                VaultCryptoError::InvalidEnvelope
                | VaultCryptoError::InputTooLarge
                | VaultCryptoError::WrappingDomainMismatch,
            ) => {
                self.state = RuntimeState::Locked;
                return Err(VaultManagerError::CorruptedVaultData);
            }
            Err(error) => {
                self.state = RuntimeState::Locked;
                return Err(VaultManagerError::Crypto(error));
            }
        };
        self.active_key = Some(key);
        self.state = RuntimeState::Unlocked;
        self.throttle.reset();
        self.last_activity = Some(now);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use chrono::TimeZone;

    use super::super::VaultManager;
    use super::*;

    fn now() -> DateTime<Utc> {
        Utc.with_ymd_and_hms(2026, 7, 22, 12, 0, 0).unwrap()
    }

    #[test]
    fn vault_state_unlock_distinguishes_wrong_credentials_from_structural_corruption() {
        let mut wrong_credentials = VaultManager::new();
        wrong_credentials
            .initialize(b"correct horse", b"recovery", now())
            .unwrap();
        wrong_credentials.lock();
        let mut corrupted = VaultManager::new();
        corrupted
            .initialize(b"correct horse", b"recovery", now())
            .unwrap();
        corrupted.lock();
        corrupted
            .wrapped_user_key
            .as_mut()
            .unwrap()
            .ciphertext
            .push(0);

        let wrong_result = wrong_credentials.unlock(b"wrong battery", now());
        let corrupted_result = corrupted.unlock(b"correct horse", now());

        assert!(matches!(
            wrong_result,
            Err(VaultManagerError::InvalidCredentials)
        ));
        assert_eq!(wrong_credentials.status().failed_unlock_count, 1);
        assert!(matches!(
            corrupted_result,
            Err(VaultManagerError::CorruptedVaultData)
        ));
        assert_eq!(corrupted.status().failed_unlock_count, 0);
    }

    #[test]
    fn vault_state_unlock_does_not_turn_authenticated_corruption_into_an_error_oracle() {
        let mut manager = VaultManager::new();
        manager
            .initialize(b"correct horse", b"recovery", now())
            .unwrap();
        manager.lock();
        manager.wrapped_user_key.as_mut().unwrap().tag[0] ^= 1;

        let result = manager.unlock(b"correct horse", now());

        assert!(matches!(result, Err(VaultManagerError::InvalidCredentials)));
    }
}
