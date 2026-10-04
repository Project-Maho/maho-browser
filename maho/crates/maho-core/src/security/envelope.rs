// Copyright 2026 Maho Browser. All rights reserved.

use maho_types::hardware_security::{
    DeviceKeyHandle, DeviceKeyPolicy, DeviceKeyPurpose, HardwareSecurityError,
    ProtectedKeyEnvelopeV3,
};
use sha2::{Digest, Sha256};
use zeroize::Zeroizing;

use super::DeviceKeyStore;

const ENVELOPE_DOMAIN_PREFIX: &[u8] = b"maho-protected-key-envelope-v3";

/// Computes a canonical cryptographic digest of a key policy for envelope domain binding.
pub fn compute_policy_digest(policy: &DeviceKeyPolicy) -> Vec<u8> {
    let mut hasher = Sha256::new();
    hasher.update(b"policy_v1:");
    hasher.update(policy.purpose.as_str().as_bytes());
    hasher.update(b":");
    hasher.update(format!("{:?}", policy.algorithm).as_bytes());
    hasher.update(b":");
    hasher.update(format!("{:?}", policy.hardware).as_bytes());
    hasher.update(b":");
    hasher.update(format!("{:?}", policy.user_auth).as_bytes());
    if policy.invalidate_on_biometric_change {
        hasher.update(b":inv_true");
    } else {
        hasher.update(b":inv_false");
    }
    if policy.background_use_allowed {
        hasher.update(b":bg_true");
    } else {
        hasher.update(b":bg_false");
    }
    hasher.finalize().to_vec()
}

/// Builds the canonical domain-separated authenticated associated data (AAD) for key envelope operations.
pub fn build_envelope_aad(
    profile_id: &str,
    device_id: &str,
    purpose: DeviceKeyPurpose,
    format_version: u32,
    policy_digest: &[u8],
) -> Vec<u8> {
    let mut aad = Vec::with_capacity(128);
    aad.extend_from_slice(ENVELOPE_DOMAIN_PREFIX);
    aad.push(0x00);
    aad.extend_from_slice(format_version.to_le_bytes().as_ref());
    aad.push(0x00);
    aad.extend_from_slice(profile_id.as_bytes());
    aad.push(0x00);
    aad.extend_from_slice(device_id.as_bytes());
    aad.push(0x00);
    aad.extend_from_slice(purpose.as_str().as_bytes());
    aad.push(0x00);
    aad.extend_from_slice(policy_digest);
    aad
}

/// Creates a new `ProtectedKeyEnvelopeV3` wrapping the provided plaintext secret using the given `DeviceKeyStore`.
pub fn create_protected_envelope(
    key_store: &dyn DeviceKeyStore,
    key_handle: &DeviceKeyHandle,
    plaintext: &[u8],
    profile_id: &str,
    device_id: &str,
    policy: &DeviceKeyPolicy,
) -> Result<ProtectedKeyEnvelopeV3, HardwareSecurityError> {
    if plaintext.is_empty() {
        return Err(HardwareSecurityError::CorruptEnvelope);
    }

    let policy_digest = compute_policy_digest(policy);
    let aad = build_envelope_aad(
        profile_id,
        device_id,
        policy.purpose,
        ProtectedKeyEnvelopeV3::FORMAT_VERSION,
        &policy_digest,
    );

    let wrapped = key_store.wrap_key_material(key_handle, plaintext, &aad)?;

    Ok(ProtectedKeyEnvelopeV3 {
        format_version: ProtectedKeyEnvelopeV3::FORMAT_VERSION,
        purpose: policy.purpose,
        provider_kind: key_handle.provider.clone(),
        protection_level: key_handle.protection_level,
        key_algorithm: key_handle.algorithm,
        key_id: key_handle.opaque_id.clone(),
        policy_digest,
        aad_schema_version: ProtectedKeyEnvelopeV3::CURRENT_AAD_SCHEMA_VERSION,
        wrapped_key_or_ciphertext: wrapped.ciphertext,
        iv: wrapped.iv_or_nonce,
        tag: wrapped.tag,
        platform_metadata: wrapped.platform_metadata,
    })
}

/// Opens and unwraps a `ProtectedKeyEnvelopeV3` using the provided `DeviceKeyStore`.
pub fn open_protected_envelope(
    key_store: &dyn DeviceKeyStore,
    envelope: &ProtectedKeyEnvelopeV3,
    profile_id: &str,
    device_id: &str,
) -> Result<Zeroizing<Vec<u8>>, HardwareSecurityError> {
    if envelope.format_version != ProtectedKeyEnvelopeV3::FORMAT_VERSION {
        return Err(HardwareSecurityError::VersionMismatch);
    }
    if envelope.wrapped_key_or_ciphertext.is_empty() {
        return Err(HardwareSecurityError::CorruptEnvelope);
    }

    let aad = build_envelope_aad(
        profile_id,
        device_id,
        envelope.purpose,
        envelope.format_version,
        &envelope.policy_digest,
    );

    let handle = DeviceKeyHandle {
        provider: envelope.provider_kind.clone(),
        opaque_id: envelope.key_id.clone(),
        purpose: envelope.purpose,
        algorithm: envelope.key_algorithm,
        protection_level: envelope.protection_level,
    };

    let wrapped = maho_types::hardware_security::PlatformWrappedKey {
        ciphertext: envelope.wrapped_key_or_ciphertext.clone(),
        tag: envelope.tag.clone(),
        iv_or_nonce: envelope.iv.clone(),
        platform_metadata: envelope.platform_metadata.clone(),
    };

    key_store.unwrap_key_material(&handle, &wrapped, &aad)
}

#[cfg(test)]
mod tests {
    use super::*;
    use maho_types::hardware_security::*;

    #[test]
    fn test_build_envelope_aad_uniqueness() {
        let aad1 = build_envelope_aad(
            "prof1",
            "dev1",
            DeviceKeyPurpose::DatabaseRoot,
            3,
            &[1, 2, 3],
        );
        let aad2 = build_envelope_aad(
            "prof2",
            "dev1",
            DeviceKeyPurpose::DatabaseRoot,
            3,
            &[1, 2, 3],
        );
        let aad3 = build_envelope_aad(
            "prof1",
            "dev1",
            DeviceKeyPurpose::VaultDeviceWrap,
            3,
            &[1, 2, 3],
        );
        assert_ne!(aad1, aad2);
        assert_ne!(aad1, aad3);
    }

    #[test]
    fn test_policy_digest_consistency() {
        let policy1 = DeviceKeyPolicy::database_root();
        let policy2 = DeviceKeyPolicy::database_root();
        let policy3 = DeviceKeyPolicy::vault_device_wrap();

        assert_eq!(
            compute_policy_digest(&policy1),
            compute_policy_digest(&policy2)
        );
        assert_ne!(
            compute_policy_digest(&policy1),
            compute_policy_digest(&policy3)
        );
    }
}
