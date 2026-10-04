//! Opaque metadata-slot (de)serialization for locked-Vault hydration and
//! initialization persistence.
//!
//! `maho-core` owns the crypto; `MahoCore` owns storage. This module bridges the
//! two with private, versioned serde payloads that serialize to the stable
//! opaque slots persisted by Todo 9 storage (`kdf_params`, `wrapped_user_key`,
//! and a dedicated recovery-wrapped slot). No raw key or plaintext ever appears
//! here; only non-secret KDF parameters and ciphertext-only wrapped-key
//! envelopes are encoded. Hydration validates slot schema and converts
//! malformed or partial state into typed corruption / uninitialized outcomes
//! without reading any item payload.

use maho_types::vault::VaultSchemaVersion;
use serde::{Deserialize, Serialize};

use crate::vault_crypto::{VaultKdfMetadata, WrappedVaultKeyEnvelope};

use super::VaultManagerError;

/// Opaque slot holding versioned Argon2id KDF parameters.
pub const SLOT_KDF_PARAMS: &str = "kdf_params";
/// Opaque slot holding the user-passphrase-wrapped Vault key envelope.
pub const SLOT_WRAPPED_USER_KEY: &str = "wrapped_user_key";
/// Opaque slot holding the recovery-wrapped Vault key envelope.
pub const SLOT_WRAPPED_RECOVERY_KEY: &str = "wrapped_recovery_key";
/// Opaque slot holding the account-escrow-wrapped Vault key envelope. Absent on
/// passphrase-provisioned vaults until the account-escrow migration adds it.
pub const SLOT_WRAPPED_ACCOUNT_KEY: &str = "wrapped_account_key";

/// Serialized slot payloads produced at initialization for `MahoCore` to persist
/// transactionally. Every field is non-secret metadata / ciphertext-only bytes.
#[derive(Clone, Debug)]
pub struct VaultPersistedSlots {
    pub kdf_params: Vec<u8>,
    pub wrapped_user_key: Vec<u8>,
    pub wrapped_recovery_key: Vec<u8>,
}

/// Serialized slot payloads for an account-escrow provisioned Vault: KDF
/// parameters plus the account-wrapped key envelope. No passphrase or recovery
/// wrap exists for account-provisioned vaults.
#[derive(Clone, Debug)]
pub struct VaultAccountProvisionSlots {
    pub kdf_params: Vec<u8>,
    pub wrapped_account_key: Vec<u8>,
}

/// Raw slot bytes read back from storage for hydration. Any slot may be absent.
#[derive(Clone, Debug, Default)]
pub struct VaultHydrationSlots {
    pub kdf_params: Option<Vec<u8>>,
    pub wrapped_user_key: Option<Vec<u8>>,
    pub wrapped_recovery_key: Option<Vec<u8>>,
    pub wrapped_account_key: Option<Vec<u8>>,
}

/// Result of hydrating locked-Vault metadata from opaque slots.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum VaultHydrationOutcome {
    Uninitialized,
    Locked,
}

#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct KdfParamsPayload {
    schema_version: VaultSchemaVersion,
    metadata: VaultKdfMetadata,
}

#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct WrappedKeyPayload {
    schema_version: VaultSchemaVersion,
    envelope: WrappedVaultKeyEnvelope,
}

pub(super) struct DecodedLockedState {
    pub metadata: VaultKdfMetadata,
    pub wrapped_user_key: Option<WrappedVaultKeyEnvelope>,
    pub wrapped_recovery_key: Option<WrappedVaultKeyEnvelope>,
    pub wrapped_account_key: Option<WrappedVaultKeyEnvelope>,
}

pub(super) enum HydrationDecode {
    Uninitialized,
    Locked(Box<DecodedLockedState>),
}

pub(super) fn encode_slots(
    metadata: &VaultKdfMetadata,
    wrapped_user_key: &WrappedVaultKeyEnvelope,
    wrapped_recovery_key: &WrappedVaultKeyEnvelope,
) -> Result<VaultPersistedSlots, VaultManagerError> {
    let kdf = KdfParamsPayload {
        schema_version: VaultSchemaVersion::CURRENT,
        metadata: metadata.clone(),
    };
    let user = WrappedKeyPayload {
        schema_version: VaultSchemaVersion::CURRENT,
        envelope: wrapped_user_key.clone(),
    };
    let recovery = WrappedKeyPayload {
        schema_version: VaultSchemaVersion::CURRENT,
        envelope: wrapped_recovery_key.clone(),
    };
    Ok(VaultPersistedSlots {
        kdf_params: serialize(&kdf)?,
        wrapped_user_key: serialize(&user)?,
        wrapped_recovery_key: serialize(&recovery)?,
    })
}

pub(super) fn encode_account_provision_slots(
    metadata: &VaultKdfMetadata,
    wrapped_account_key: &WrappedVaultKeyEnvelope,
) -> Result<VaultAccountProvisionSlots, VaultManagerError> {
    let kdf = KdfParamsPayload {
        schema_version: VaultSchemaVersion::CURRENT,
        metadata: metadata.clone(),
    };
    let account = WrappedKeyPayload {
        schema_version: VaultSchemaVersion::CURRENT,
        envelope: wrapped_account_key.clone(),
    };
    Ok(VaultAccountProvisionSlots {
        kdf_params: serialize(&kdf)?,
        wrapped_account_key: serialize(&account)?,
    })
}

/// Serialized single-slot payload for persisting an account-escrow envelope on
/// an already-initialized vault (migration path). Ciphertext-only bytes.
#[derive(Clone, Debug)]
pub struct VaultAccountWrapSlot {
    pub wrapped_account_key: Vec<u8>,
}

pub fn encode_account_wrap_slot(
    wrapped_account_key: &WrappedVaultKeyEnvelope,
) -> Result<VaultAccountWrapSlot, VaultManagerError> {
    let account = WrappedKeyPayload {
        schema_version: VaultSchemaVersion::CURRENT,
        envelope: wrapped_account_key.clone(),
    };
    Ok(VaultAccountWrapSlot {
        wrapped_account_key: serialize(&account)?,
    })
}

pub(super) fn decode_locked_state(
    slots: &VaultHydrationSlots,
) -> Result<HydrationDecode, VaultManagerError> {
    // Classify by presence first, without reading any item payload. A Vault is
    // Locked when KDF params exist plus at least one usable wrap: the legacy
    // passphrase kit (user + recovery, account optional) or the account-escrow
    // kit (account, user/recovery optional). Any other combination is
    // corruption; everything absent is uninitialized.
    let kdf_present = slots.kdf_params.is_some();
    let user_present = slots.wrapped_user_key.is_some();
    let recovery_present = slots.wrapped_recovery_key.is_some();
    let account_present = slots.wrapped_account_key.is_some();
    match (kdf_present, user_present, recovery_present, account_present) {
        (false, false, false, false) => return Ok(HydrationDecode::Uninitialized),
        (true, true, true, _) | (true, false, false, true) => {}
        _ => return Err(VaultManagerError::CorruptedVaultData),
    }
    let kdf_bytes = slots
        .kdf_params
        .as_ref()
        .ok_or(VaultManagerError::CorruptedVaultData)?;
    let user: Option<WrappedKeyPayload> = match slots.wrapped_user_key.as_ref() {
        Some(bytes) => Some(deserialize(bytes)?),
        None => None,
    };
    let recovery: Option<WrappedKeyPayload> = match slots.wrapped_recovery_key.as_ref() {
        Some(bytes) => Some(deserialize(bytes)?),
        None => None,
    };
    let account: Option<WrappedKeyPayload> = match slots.wrapped_account_key.as_ref() {
        Some(bytes) => Some(deserialize(bytes)?),
        None => None,
    };
    let kdf: KdfParamsPayload = deserialize(kdf_bytes)?;
    Ok(HydrationDecode::Locked(Box::new(DecodedLockedState {
        metadata: kdf.metadata,
        wrapped_user_key: user.map(|payload| payload.envelope),
        wrapped_recovery_key: recovery.map(|payload| payload.envelope),
        wrapped_account_key: account.map(|payload| payload.envelope),
    })))
}

fn serialize<T: Serialize>(value: &T) -> Result<Vec<u8>, VaultManagerError> {
    serde_json::to_vec(value).map_err(|_| VaultManagerError::CorruptedVaultData)
}

fn deserialize<T: for<'de> Deserialize<'de>>(bytes: &[u8]) -> Result<T, VaultManagerError> {
    serde_json::from_slice(bytes).map_err(|_| VaultManagerError::CorruptedVaultData)
}
