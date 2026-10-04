//! Locked/unlocked Vault core state machine (Todo 10).
//!
//! Storage-agnostic: this manager owns runtime lifecycle and bounded zeroizing
//! key material only. `MahoCore` coordinates opaque-slot persistence, hydration,
//! and grant revocation. Time is an explicit timestamp seam (no sleeps).

mod account;
mod credential_form;
mod crypto_ops;
mod device;
mod item_error;
mod item_input;
pub(crate) mod origin;
mod persistence;
pub(crate) mod record;
mod state;
mod throttle;
mod unlock;

pub use credential_form::{
    VaultAlternativeElement, VaultCredentialFormDetails, VaultCredentialNote,
};
pub use crypto_ops::ProtectedEnvelopeReader;
pub use item_error::VaultCrudError;
pub use item_input::{
    VaultLoginInput, VaultLoginUpdate, VaultPasskeyInput, VaultSecureItemInput, VaultTotpInput,
};
pub use origin::OriginMatchPolicy;
pub use persistence::{
    encode_account_wrap_slot, VaultAccountProvisionSlots, VaultAccountWrapSlot,
    VaultHydrationOutcome, VaultHydrationSlots, VaultPersistedSlots, SLOT_KDF_PARAMS,
    SLOT_WRAPPED_ACCOUNT_KEY, SLOT_WRAPPED_RECOVERY_KEY, SLOT_WRAPPED_USER_KEY,
};

use std::path::Path;

use maho_storage::sqlite::{
    inspect_vault_database, VaultDatabaseInspection, VaultDatabaseKey, VaultDatabaseSnapshot,
};

use chrono::{DateTime, Duration, Utc};
use maho_types::passwords::PasswordProviderKind;
use maho_types::vault::{VaultAgentPolicy, VaultKeyVersion, VaultSchemaVersion, VaultStatus};

use crate::oscrypt::{VaultDeviceProtectorError, VaultDeviceWrapBundleError};
use crate::vault_crypto::{
    derive_recovery_wrapping_key, derive_user_wrapping_key, wrap_vault_key, VaultCryptoError,
    VaultKdfMetadata, VaultKey, WrappedVaultKeyEnvelope,
};

use state::RuntimeState;
use throttle::UnlockThrottle;

const MANAGER_WRAP_AAD: &[u8] = b"maho-vault-manager-v1";
const ACCOUNT_WRAP_AAD: &[u8] = b"maho-vault-account-wrap-v1";
const DEFAULT_AUTO_LOCK_MINUTES: u32 = 15;

/// Typed startup classification for the persisted Vault key/database pair.
/// Later FFI/C++ propagation can match this enum directly without parsing an
/// error string or relying on a boolean unlock probe.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum VaultPreflightState {
    Healthy,
    Locked,
    UnrecoverableKey,
    StructuralCorruption,
    PlaintextResidue,
}

/// Inspect the SQLCipher/Vault database without creating, migrating, repairing,
/// or unlocking it. Key-store provenance comes from `key`; opaque Vault slots
/// are structurally decoded through the same hydration seam used at startup.
pub fn preflight_vault_database(path: &Path, key: VaultDatabaseKey<'_>) -> VaultPreflightState {
    match inspect_vault_database(path, key) {
        VaultDatabaseInspection::Absent => VaultPreflightState::Healthy,
        VaultDatabaseInspection::UnrecoverableKey => VaultPreflightState::UnrecoverableKey,
        VaultDatabaseInspection::StructuralCorruption => VaultPreflightState::StructuralCorruption,
        VaultDatabaseInspection::PlaintextResidue => VaultPreflightState::PlaintextResidue,
        VaultDatabaseInspection::Readable(snapshot) => preflight_readable_database(snapshot),
    }
}

fn preflight_readable_database(snapshot: VaultDatabaseSnapshot) -> VaultPreflightState {
    let mut manager = VaultManager::new();
    let slots = VaultHydrationSlots {
        kdf_params: snapshot.kdf_params,
        wrapped_user_key: snapshot.wrapped_user_key,
        wrapped_recovery_key: snapshot.wrapped_recovery_key,
        wrapped_account_key: snapshot.wrapped_account_key,
    };
    match manager.hydrate_locked(slots) {
        Ok(VaultHydrationOutcome::Uninitialized) => VaultPreflightState::Healthy,
        Ok(VaultHydrationOutcome::Locked) => VaultPreflightState::Locked,
        Err(_) => VaultPreflightState::StructuralCorruption,
    }
}

#[derive(Debug, thiserror::Error)]
pub enum VaultManagerError {
    #[error("Vault is locked")]
    VaultLocked,
    #[error("Vault is uninitialized")]
    Uninitialized,
    #[error("Vault is already initialized")]
    AlreadyInitialized,
    #[error("Failed unlock rate limited, please wait")]
    RateLimited,
    #[error("Invalid master password or key")]
    InvalidCredentials,
    #[error("Persisted Vault key data is corrupted or unsupported")]
    CorruptedVaultData,
    #[error("Vault storage failure: {0}")]
    Storage(String),
    #[error("device protector failure: {0}")]
    DeviceProtector(#[from] VaultDeviceProtectorError),
    #[error("device wrapper failure: {0}")]
    DeviceWrapper(#[from] VaultDeviceWrapBundleError),
    #[error("device wrapper does not match this device binding")]
    DeviceBindingMismatch,
    #[error("Vault has no account-wrapped key slot")]
    NoAccountWrap,
    #[error("Crypto error: {0}")]
    Crypto(#[from] VaultCryptoError),
}

pub struct VaultManager {
    state: RuntimeState,
    active_key: Option<VaultKey>,
    kdf_metadata: Option<VaultKdfMetadata>,
    wrapped_user_key: Option<WrappedVaultKeyEnvelope>,
    wrapped_recovery_key: Option<WrappedVaultKeyEnvelope>,
    wrapped_account_key: Option<WrappedVaultKeyEnvelope>,
    throttle: UnlockThrottle,
    auto_lock_minutes: u32,
    last_activity: Option<DateTime<Utc>>,
    agent_policy_default: VaultAgentPolicy,
    item_count: u64,
}

impl Default for VaultManager {
    fn default() -> Self {
        Self::new()
    }
}

impl VaultManager {
    pub fn new() -> Self {
        Self {
            state: RuntimeState::Uninitialized,
            active_key: None,
            kdf_metadata: None,
            wrapped_user_key: None,
            wrapped_recovery_key: None,
            wrapped_account_key: None,
            throttle: UnlockThrottle::default(),
            auto_lock_minutes: DEFAULT_AUTO_LOCK_MINUTES,
            last_activity: None,
            agent_policy_default: VaultAgentPolicy::Deny,
            item_count: 0,
        }
    }

    pub fn status(&self) -> VaultStatus {
        VaultStatus {
            schema_version: VaultSchemaVersion::CURRENT,
            lock_state: self.state.public_lock_state(),
            selected_provider: PasswordProviderKind::MahoNative,
            effective_provider: PasswordProviderKind::MahoNative,
            item_count: self.item_count,
            agent_policy_default: self.agent_policy_default,
            auto_lock_minutes: self.auto_lock_minutes,
            failed_unlock_count: self.throttle.failed_attempts(),
            retry_at: self.throttle.retry_at(),
            account_escrowed: self.wrapped_account_key.is_some(),
        }
    }

    pub fn is_unlocked(&self) -> bool {
        self.state == RuntimeState::Unlocked && self.active_key.is_some()
    }

    pub fn set_item_count(&mut self, count: u64) {
        self.item_count = count;
    }

    pub fn set_auto_lock_minutes(&mut self, minutes: u32) {
        self.auto_lock_minutes = minutes;
    }

    /// Initialize a fresh Vault with both user and recovery wrapping. Returns the
    /// opaque slot payloads for `MahoCore` to persist transactionally; the caller
    /// must call [`Self::revert_uninitialized`] on persistence failure.
    pub fn initialize(
        &mut self,
        master_passphrase: &[u8],
        recovery_secret: &[u8],
        now: DateTime<Utc>,
    ) -> Result<VaultPersistedSlots, VaultManagerError> {
        if self.state != RuntimeState::Uninitialized {
            return Err(VaultManagerError::AlreadyInitialized);
        }
        let metadata = VaultKdfMetadata::generate()?;
        let user_wrapping = derive_user_wrapping_key(master_passphrase, &metadata)?;
        let recovery_wrapping = derive_recovery_wrapping_key(recovery_secret, &metadata)?;
        let key = VaultKey::generate(VaultKeyVersion::new(1))?;
        let wrapped_user = wrap_vault_key(&key, &user_wrapping, MANAGER_WRAP_AAD)?;
        let wrapped_recovery = wrap_vault_key(&key, &recovery_wrapping, MANAGER_WRAP_AAD)?;
        let slots = persistence::encode_slots(&metadata, &wrapped_user, &wrapped_recovery)?;
        self.kdf_metadata = Some(metadata);
        self.wrapped_user_key = Some(wrapped_user);
        self.wrapped_recovery_key = Some(wrapped_recovery);
        self.active_key = Some(key);
        self.state = RuntimeState::Unlocked;
        self.throttle.reset();
        self.last_activity = Some(now);
        Ok(slots)
    }

    /// Fail-closed revert used when initialization persistence fails: drops the
    /// active key (zeroized on drop) and returns to the uninitialized state.
    pub fn revert_uninitialized(&mut self) {
        self.active_key = None;
        self.kdf_metadata = None;
        self.wrapped_user_key = None;
        self.wrapped_recovery_key = None;
        self.wrapped_account_key = None;
        self.state = RuntimeState::Uninitialized;
        self.last_activity = None;
        self.throttle.reset();
    }

    pub fn lock(&mut self) {
        self.active_key = None;
        if self.state != RuntimeState::Uninitialized {
            self.state = RuntimeState::Locked;
        }
        self.last_activity = None;
    }

    pub fn auto_lock(&mut self) {
        self.active_key = None;
        if self.state != RuntimeState::Uninitialized {
            self.state = RuntimeState::AutoLocked;
        }
        self.last_activity = None;
    }

    pub fn record_activity(&mut self, now: DateTime<Utc>) {
        if self.state == RuntimeState::Unlocked {
            self.last_activity = Some(now);
        }
    }

    /// Deterministic auto-lock evaluation seam. Returns `true` when it locked.
    pub fn evaluate_auto_lock(&mut self, now: DateTime<Utc>) -> bool {
        if self.state != RuntimeState::Unlocked || self.auto_lock_minutes == 0 {
            return false;
        }
        let Some(last_activity) = self.last_activity else {
            return false;
        };
        let idle = now.signed_duration_since(last_activity);
        if idle >= Duration::minutes(i64::from(self.auto_lock_minutes)) {
            self.auto_lock();
            true
        } else {
            false
        }
    }

    pub fn active_key(&self) -> Result<&VaultKey, VaultManagerError> {
        if !self.is_unlocked() {
            return Err(VaultManagerError::VaultLocked);
        }
        self.active_key
            .as_ref()
            .ok_or(VaultManagerError::VaultLocked)
    }

    /// Hydrate metadata-only locked state from opaque storage slots. Fail-closed:
    /// any prior key/state is dropped first, so a non-fresh manager can never stay
    /// unlocked. All three fresh-schema slots present => Locked; all absent =>
    /// Uninitialized; any partial set or malformed bytes => `CorruptedVaultData`.
    /// No item payload is read.
    pub fn hydrate_locked(
        &mut self,
        slots: VaultHydrationSlots,
    ) -> Result<VaultHydrationOutcome, VaultManagerError> {
        self.revert_uninitialized();
        match persistence::decode_locked_state(&slots)? {
            persistence::HydrationDecode::Uninitialized => Ok(VaultHydrationOutcome::Uninitialized),
            persistence::HydrationDecode::Locked(decoded) => {
                self.kdf_metadata = Some(decoded.metadata);
                self.wrapped_user_key = decoded.wrapped_user_key;
                self.wrapped_recovery_key = decoded.wrapped_recovery_key;
                self.wrapped_account_key = decoded.wrapped_account_key;
                self.active_key = None;
                self.state = RuntimeState::Locked;
                Ok(VaultHydrationOutcome::Locked)
            }
        }
    }
}
