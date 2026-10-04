// Copyright 2026 Maho Browser. All rights reserved.

use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};

use aes_gcm::aead::{AeadInPlace, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce, Tag};
use rand::rngs::OsRng;
use rand::RngCore;
use sha2::{Digest, Sha256};
use zeroize::Zeroizing;

use crate::security::{
    AuthenticatedKeyOperationCompletion, AuthenticatedKeyOperationKind,
    AuthenticatedKeyOperationRequest, AuthenticatedKeyOperationResult, BiometricAuthenticator,
    DeviceKeyStore, HardwareOperationId, HardwareSecurityCapabilities, HardwareSecurityError,
    HardwareSecurityProvider, ProtectionLevel, UserPresenceAvailability, UserPresenceCompletion,
    UserPresencePolicy, UserPresenceRequest, UserPresenceResult,
};
use maho_types::hardware_security::{
    DeviceKeyAlgorithm, DeviceKeyHandle, DeviceKeyPolicy, DeviceKeyPurpose, DeviceSignature,
    HardwareProviderKind, PlatformWrappedKey, UserVerificationKind,
};

struct KeyEntry {
    purpose: DeviceKeyPurpose,
    algorithm: DeviceKeyAlgorithm,
    secret_key: Zeroizing<Vec<u8>>,
    public_key: Vec<u8>,
}

/// In-memory software implementation of `DeviceKeyStore` for testing and fallback.
#[derive(Clone, Default)]
pub struct SoftwareKeyStore {
    keys: Arc<Mutex<HashMap<String, KeyEntry>>>,
}

impl SoftwareKeyStore {
    pub fn new() -> Self {
        Self {
            keys: Arc::new(Mutex::new(HashMap::new())),
        }
    }
}

impl DeviceKeyStore for SoftwareKeyStore {
    fn capabilities(&self) -> HardwareSecurityCapabilities {
        HardwareSecurityCapabilities {
            provider: HardwareProviderKind::SoftwareProcess,
            protection_level: ProtectionLevel::SoftwareProcess,
            supports_background_unwrap: true,
            supports_non_exportable_signing: true,
            supports_hardware_wrapping: false,
            supports_user_presence: true,
            supports_biometric: false,
            supports_biometric_strong: false,
            supports_enrollment_bound_keys: false,
            supports_attestation: false,
            user_verification_kinds: vec![UserVerificationKind::OsUserVerification],
        }
    }

    fn get_or_create_key(
        &self,
        label: &str,
        policy: &DeviceKeyPolicy,
    ) -> Result<DeviceKeyHandle, HardwareSecurityError> {
        let mut keys = self
            .keys
            .lock()
            .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;

        if let Some(entry) = keys.get(label) {
            if entry.purpose != policy.purpose {
                return Err(HardwareSecurityError::PolicyUnsatisfied);
            }
            return Ok(DeviceKeyHandle {
                provider: HardwareProviderKind::SoftwareProcess,
                opaque_id: label.to_string(),
                purpose: entry.purpose,
                algorithm: entry.algorithm,
                protection_level: ProtectionLevel::SoftwareProcess,
            });
        }

        let mut secret = Zeroizing::new(vec![0u8; 32]);
        OsRng.fill_bytes(secret.as_mut_slice());

        // Derive simulated public key using SHA-256
        let mut hasher = Sha256::new();
        hasher.update(b"software_pubkey:");
        hasher.update(secret.as_slice());
        let public_key = hasher.finalize().to_vec();

        keys.insert(
            label.to_string(),
            KeyEntry {
                purpose: policy.purpose,
                algorithm: policy.algorithm,
                secret_key: secret,
                public_key,
            },
        );

        Ok(DeviceKeyHandle {
            provider: HardwareProviderKind::SoftwareProcess,
            opaque_id: label.to_string(),
            purpose: policy.purpose,
            algorithm: policy.algorithm,
            protection_level: ProtectionLevel::SoftwareProcess,
        })
    }

    fn wrap_key_material(
        &self,
        key: &DeviceKeyHandle,
        plaintext: &[u8],
        aad: &[u8],
    ) -> Result<PlatformWrappedKey, HardwareSecurityError> {
        let keys = self
            .keys
            .lock()
            .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
        let entry = keys
            .get(&key.opaque_id)
            .ok_or(HardwareSecurityError::KeyNotFound)?;

        if plaintext.is_empty() {
            return Err(HardwareSecurityError::CorruptEnvelope);
        }

        let cipher = Aes256Gcm::new_from_slice(entry.secret_key.as_slice())
            .map_err(|_| HardwareSecurityError::BackendFailure("cipher init failed".into()))?;

        let mut nonce_bytes = [0u8; 12];
        OsRng.fill_bytes(&mut nonce_bytes);
        let nonce = Nonce::from_slice(&nonce_bytes);

        let mut buffer = plaintext.to_vec();
        let tag = cipher
            .encrypt_in_place_detached(nonce, aad, &mut buffer)
            .map_err(|_| HardwareSecurityError::BackendFailure("encryption failed".into()))?;

        Ok(PlatformWrappedKey {
            ciphertext: buffer,
            tag: Some(tag.as_slice().to_vec()),
            iv_or_nonce: Some(nonce_bytes.to_vec()),
            platform_metadata: None,
        })
    }

    fn unwrap_key_material(
        &self,
        key: &DeviceKeyHandle,
        wrapped: &PlatformWrappedKey,
        aad: &[u8],
    ) -> Result<Zeroizing<Vec<u8>>, HardwareSecurityError> {
        let keys = self
            .keys
            .lock()
            .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
        let entry = keys
            .get(&key.opaque_id)
            .ok_or(HardwareSecurityError::KeyNotFound)?;

        let iv = wrapped
            .iv_or_nonce
            .as_ref()
            .ok_or(HardwareSecurityError::CorruptEnvelope)?;
        if iv.len() != 12 {
            return Err(HardwareSecurityError::CorruptEnvelope);
        }
        let tag_bytes = wrapped
            .tag
            .as_ref()
            .ok_or(HardwareSecurityError::CorruptEnvelope)?;
        if tag_bytes.len() != 16 {
            return Err(HardwareSecurityError::CorruptEnvelope);
        }

        let cipher = Aes256Gcm::new_from_slice(entry.secret_key.as_slice())
            .map_err(|_| HardwareSecurityError::BackendFailure("cipher init failed".into()))?;
        let nonce = Nonce::from_slice(iv);
        let tag = Tag::from_slice(tag_bytes);

        let mut buffer = wrapped.ciphertext.clone();
        cipher
            .decrypt_in_place_detached(nonce, aad, &mut buffer, tag)
            .map_err(|_| HardwareSecurityError::CorruptEnvelope)?;

        Ok(Zeroizing::new(buffer))
    }

    fn sign_challenge(
        &self,
        key: &DeviceKeyHandle,
        challenge: &[u8],
    ) -> Result<DeviceSignature, HardwareSecurityError> {
        let keys = self
            .keys
            .lock()
            .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
        let entry = keys
            .get(&key.opaque_id)
            .ok_or(HardwareSecurityError::KeyNotFound)?;

        let mut hasher = Sha256::new();
        hasher.update(b"software_sign:");
        hasher.update(entry.secret_key.as_slice());
        hasher.update(challenge);
        let sig_bytes = hasher.finalize().to_vec();

        Ok(DeviceSignature {
            algorithm: entry.algorithm,
            signature: sig_bytes,
            public_key: Some(entry.public_key.clone()),
        })
    }

    fn delete_key(&self, key: &DeviceKeyHandle) -> Result<(), HardwareSecurityError> {
        let mut keys = self
            .keys
            .lock()
            .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
        keys.remove(&key.opaque_id);
        Ok(())
    }
}

/// Software implementation of `BiometricAuthenticator` for testing and simulated environments.
#[derive(Clone, Default)]
pub struct SoftwareBiometricAuthenticator {
    operation_counter: Arc<AtomicU64>,
}

impl SoftwareBiometricAuthenticator {
    pub fn new() -> Self {
        Self {
            operation_counter: Arc::new(AtomicU64::new(1)),
        }
    }
}

impl BiometricAuthenticator for SoftwareBiometricAuthenticator {
    fn availability(
        &self,
        _policy: &UserPresencePolicy,
    ) -> Result<UserPresenceAvailability, HardwareSecurityError> {
        Ok(UserPresenceAvailability::Available)
    }

    fn begin_authentication(
        &self,
        request: UserPresenceRequest,
        completion: UserPresenceCompletion,
    ) -> Result<HardwareOperationId, HardwareSecurityError> {
        let op_id = HardwareOperationId(self.operation_counter.fetch_add(1, Ordering::SeqCst));
        completion(Ok(UserPresenceResult {
            verified_factor: request.required_verification,
            authorization_token: Some(vec![0xAA; 32]),
        }));
        Ok(op_id)
    }

    fn cancel_authentication(
        &self,
        _operation: HardwareOperationId,
    ) -> Result<(), HardwareSecurityError> {
        Ok(())
    }
}

/// Software hardware security provider coordinating in-memory key store and authenticator.
#[derive(Clone, Default)]
pub struct SoftwareHardwareSecurityProvider {
    key_store: SoftwareKeyStore,
    authenticator: SoftwareBiometricAuthenticator,
}

impl SoftwareHardwareSecurityProvider {
    pub fn new() -> Self {
        Self {
            key_store: SoftwareKeyStore::new(),
            authenticator: SoftwareBiometricAuthenticator::new(),
        }
    }
}

impl HardwareSecurityProvider for SoftwareHardwareSecurityProvider {
    fn capabilities(&self) -> HardwareSecurityCapabilities {
        self.key_store.capabilities()
    }

    fn key_store(&self) -> &dyn DeviceKeyStore {
        &self.key_store
    }

    fn authenticator(&self) -> Option<&dyn BiometricAuthenticator> {
        Some(&self.authenticator)
    }

    fn begin_authenticated_key_operation(
        &self,
        request: AuthenticatedKeyOperationRequest,
        completion: AuthenticatedKeyOperationCompletion,
    ) -> Result<HardwareOperationId, HardwareSecurityError> {
        let op_id = HardwareOperationId(
            self.authenticator
                .operation_counter
                .fetch_add(1, Ordering::SeqCst),
        );

        match request.operation_kind {
            AuthenticatedKeyOperationKind::Wrap => {
                match self.key_store.wrap_key_material(
                    &request.key_handle,
                    &request.input_payload,
                    &request.aad,
                ) {
                    Ok(wrapped) => {
                        completion(Ok(AuthenticatedKeyOperationResult::Wrapped(wrapped)))
                    }
                    Err(e) => completion(Err(e)),
                }
            }
            AuthenticatedKeyOperationKind::Unwrap => {
                let wrapped = PlatformWrappedKey {
                    ciphertext: request.input_payload,
                    tag: None,
                    iv_or_nonce: None,
                    platform_metadata: None,
                };
                match self.key_store.unwrap_key_material(
                    &request.key_handle,
                    &wrapped,
                    &request.aad,
                ) {
                    Ok(unwrapped) => completion(Ok(AuthenticatedKeyOperationResult::Unwrapped(
                        unwrapped.to_vec(),
                    ))),
                    Err(e) => completion(Err(e)),
                }
            }
            AuthenticatedKeyOperationKind::Sign => {
                match self
                    .key_store
                    .sign_challenge(&request.key_handle, &request.input_payload)
                {
                    Ok(sig) => completion(Ok(AuthenticatedKeyOperationResult::Signed(sig))),
                    Err(e) => completion(Err(e)),
                }
            }
        }

        Ok(op_id)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_software_provider_wrap_unwrap_roundtrip() {
        let provider = SoftwareHardwareSecurityProvider::new();
        let store = provider.key_store();
        let policy = DeviceKeyPolicy::database_root();
        let handle = store.get_or_create_key("test_db_key", &policy).unwrap();

        let plaintext = b"super_secret_db_key_material_123";
        let aad = b"profile_1:device_1:database_root";

        let wrapped = store.wrap_key_material(&handle, plaintext, aad).unwrap();
        let unwrapped = store.unwrap_key_material(&handle, &wrapped, aad).unwrap();

        assert_eq!(unwrapped.as_slice(), plaintext);
    }

    #[test]
    fn test_software_provider_tampered_aad_fails() {
        let provider = SoftwareHardwareSecurityProvider::new();
        let store = provider.key_store();
        let policy = DeviceKeyPolicy::database_root();
        let handle = store.get_or_create_key("test_db_key", &policy).unwrap();

        let plaintext = b"super_secret_db_key_material_123";
        let aad1 = b"profile_1:device_1:database_root";
        let aad2 = b"profile_2:device_1:database_root";

        let wrapped = store.wrap_key_material(&handle, plaintext, aad1).unwrap();
        let err = store.unwrap_key_material(&handle, &wrapped, aad2);

        assert!(err.is_err());
    }

    #[test]
    fn test_software_provider_sign_challenge() {
        let provider = SoftwareHardwareSecurityProvider::new();
        let store = provider.key_store();
        let policy = DeviceKeyPolicy::installation_identity();
        let handle = store.get_or_create_key("test_identity", &policy).unwrap();

        let challenge = b"random_challenge_nonce_456";
        let sig = store.sign_challenge(&handle, challenge).unwrap();

        assert!(!sig.signature.is_empty());
        assert!(sig.public_key.is_some());
    }
}
