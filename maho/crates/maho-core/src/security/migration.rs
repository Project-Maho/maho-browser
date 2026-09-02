// Copyright 2026 Maho Browser. All rights reserved.

use maho_types::hardware_security::{
    DeviceKeyPolicy, FallbackPolicyKind, HardwareSecurityError, ProtectedKeyEnvelopeV3,
};
use zeroize::Zeroizing;

use super::envelope::{create_protected_envelope, open_protected_envelope};
use super::policy::HardwareSecurityPolicyEngine;
use super::DeviceKeyStore;

/// Executes Stage A database root key migration (wrap without rekeying).
///
/// Flow:
/// 1. Verifies that the legacy key can open the database via `db_verifier`.
/// 2. Ensures the key store meets `BackgroundDatabase` policy requirements.
/// 3. Obtains or generates a dedicated `DatabaseRoot` key handle.
/// 4. Protects the legacy key material in a `ProtectedKeyEnvelopeV3`.
/// 5. Immediately unwraps the newly created envelope to verify round-trip fidelity.
/// 6. Verifies that the unwrapped key matches the legacy key and re-verifies via `db_verifier`.
/// 7. Returns the valid envelope. If any step fails, the legacy state remains untouched.
pub fn migrate_database_root_key<F>(
    key_store: &dyn DeviceKeyStore,
    legacy_key: &[u8],
    profile_id: &str,
    device_id: &str,
    db_verifier: F,
) -> Result<ProtectedKeyEnvelopeV3, HardwareSecurityError>
where
    F: Fn(&[u8]) -> bool,
{
    if legacy_key.is_empty() {
        return Err(HardwareSecurityError::CorruptEnvelope);
    }

    // Step 1: Prove legacy key opens/verifies the database
    if !db_verifier(legacy_key) {
        return Err(HardwareSecurityError::AccessDenied);
    }

    // Step 2: Ensure key store meets BackgroundDatabase fallback policy
    let caps = key_store.capabilities();
    let _ = HardwareSecurityPolicyEngine::evaluate_fallback_policy(
        FallbackPolicyKind::BackgroundDatabase,
        &caps,
        None,
    )?;

    // Step 3: Get or create dedicated DatabaseRoot key handle
    let policy = DeviceKeyPolicy::database_root();
    let key_handle = key_store.get_or_create_key("maho_database_root", &policy)?;

    // Step 4: Wrap legacy key material in ProtectedKeyEnvelopeV3
    let envelope = create_protected_envelope(
        key_store,
        &key_handle,
        legacy_key,
        profile_id,
        device_id,
        &policy,
    )?;

    // Step 5: Immediately unwrap to test round-trip integrity
    let unwrapped: Zeroizing<Vec<u8>> =
        open_protected_envelope(key_store, &envelope, profile_id, device_id)?;

    // Step 6: Verify unwrapped key is identical to legacy key and verifies database
    if unwrapped.as_slice() != legacy_key {
        return Err(HardwareSecurityError::CorruptEnvelope);
    }
    if !db_verifier(unwrapped.as_slice()) {
        return Err(HardwareSecurityError::AccessDenied);
    }

    // Step 7: Migration successful; envelope is verified and ready for atomic persistence
    Ok(envelope)
}
