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

/// Serialized slot payloads produced at initialization for `MahoCore` to persist
/// transactionally. Every field is non-secret metadata / ciphertext-only bytes.
#[derive(Clone, Debug)]
pub struct VaultPersistedSlots {
    pub kdf_params: Vec<u8>,
    pub wrapped_user_key: Vec<u8>,
    pub wrapped_recovery_key: Vec<u8>,
}

/// Raw slot bytes read back from storage for hydration. Any slot may be absent.
#[derive(Clone, Debug, Default)]
pub struct VaultHydrationSlots {
    pub kdf_params: Option<Vec<u8>>,
    pub wrapped_user_key: Option<Vec<u8>>,
    pub wrapped_recovery_key: Option<Vec<u8>>,
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
    pub wrapped_user_key: WrappedVaultKeyEnvelope,
    pub wrapped_recovery_key: WrappedVaultKeyEnvelope,
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

pub(super) fn decode_locked_state(
    slots: &VaultHydrationSlots,
) -> Result<HydrationDecode, VaultManagerError> {
    // Classify by presence first, without reading any item payload. All three
    // fresh-schema slots must be present together. No missing-recovery
    // compatibility exists: there are no released legacy users.
    let present = [
        slots.kdf_params.is_some(),
        slots.wrapped_user_key.is_some(),
        slots.wrapped_recovery_key.is_some(),
    ]
    .into_iter()
    .filter(|present| *present)
    .count();
    match present {
        0 => return Ok(HydrationDecode::Uninitialized),
        3 => {}
        _ => return Err(VaultManagerError::CorruptedVaultData),
    }
    let (Some(kdf_bytes), Some(user_bytes), Some(recovery_bytes)) = (
        slots.kdf_params.as_ref(),
        slots.wrapped_user_key.as_ref(),
        slots.wrapped_recovery_key.as_ref(),
    ) else {
        return Err(VaultManagerError::CorruptedVaultData);
    };
    let kdf: KdfParamsPayload = deserialize(kdf_bytes)?;
    let user: WrappedKeyPayload = deserialize(user_bytes)?;
    let recovery: WrappedKeyPayload = deserialize(recovery_bytes)?;
    Ok(HydrationDecode::Locked(Box::new(DecodedLockedState {
        metadata: kdf.metadata,
        wrapped_user_key: user.envelope,
        wrapped_recovery_key: recovery.envelope,
    })))
}

fn serialize<T: Serialize>(value: &T) -> Result<Vec<u8>, VaultManagerError> {
    serde_json::to_vec(value).map_err(|_| VaultManagerError::CorruptedVaultData)
}

fn deserialize<T: for<'de> Deserialize<'de>>(bytes: &[u8]) -> Result<T, VaultManagerError> {
    serde_json::from_slice(bytes).map_err(|_| VaultManagerError::CorruptedVaultData)
}
