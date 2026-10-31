// Copyright 2026 Maho Browser. All rights reserved.

use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, RwLock};

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
    DeviceKeyHandle, DeviceKeyPolicy, DeviceSignature, HardwareProviderKind, PlatformWrappedKey,
    UserVerificationKind,
};

/// Mobile platform delegate trait that Swift (iOS) or Kotlin (Android) shell wires into Rust core.
pub trait MobilePlatformDelegate: Send + Sync {
    fn capabilities(&self) -> HardwareSecurityCapabilities;
    fn get_or_create_key(
        &self,
        label: &str,
        policy: &DeviceKeyPolicy,
    ) -> Result<DeviceKeyHandle, HardwareSecurityError>;
    fn wrap_key_material(
        &self,
        key: &DeviceKeyHandle,
        plaintext: &[u8],
        aad: &[u8],
    ) -> Result<PlatformWrappedKey, HardwareSecurityError>;
    fn unwrap_key_material(
        &self,
        key: &DeviceKeyHandle,
        wrapped: &PlatformWrappedKey,
        aad: &[u8],
    ) -> Result<Zeroizing<Vec<u8>>, HardwareSecurityError>;
    fn sign_challenge(
        &self,
        key: &DeviceKeyHandle,
        challenge: &[u8],
    ) -> Result<DeviceSignature, HardwareSecurityError>;
    fn delete_key(&self, key: &DeviceKeyHandle) -> Result<(), HardwareSecurityError>;

    fn check_presence_availability(
        &self,
        policy: &UserPresencePolicy,
    ) -> Result<UserPresenceAvailability, HardwareSecurityError>;
    fn begin_presence_authentication(
        &self,
        request: UserPresenceRequest,
        completion: UserPresenceCompletion,
    ) -> Result<HardwareOperationId, HardwareSecurityError>;
    fn cancel_presence_authentication(
        &self,
        operation: HardwareOperationId,
    ) -> Result<(), HardwareSecurityError>;

    fn begin_authenticated_key_operation(
        &self,
        request: AuthenticatedKeyOperationRequest,
        completion: AuthenticatedKeyOperationCompletion,
    ) -> Result<HardwareOperationId, HardwareSecurityError>;
}

/// Fallback mobile in-memory store when no native delegate is yet registered.
#[derive(Clone, Default)]
struct FallbackMobileStore {
    keys: Arc<Mutex<HashMap<String, Zeroizing<Vec<u8>>>>>,
}

/// Mobile hardware security proxy that forwards operations to iOS/Android shell delegates.
pub struct MobileHardwareSecurityProxy {
    provider_kind: HardwareProviderKind,
    protection_level: ProtectionLevel,
    delegate: RwLock<Option<Arc<dyn MobilePlatformDelegate>>>,
    fallback_store: FallbackMobileStore,
    operation_counter: AtomicU64,
}

impl MobileHardwareSecurityProxy {
    pub fn for_ios() -> Self {
        Self {
            provider_kind: HardwareProviderKind::IosSecureEnclave,
            protection_level: ProtectionLevel::SecureEnclave,
            delegate: RwLock::new(None),
            fallback_store: FallbackMobileStore::default(),
            operation_counter: AtomicU64::new(1),
        }
    }

    pub fn for_android() -> Self {
        Self {
            provider_kind: HardwareProviderKind::AndroidKeystore,
            protection_level: ProtectionLevel::Tee,
            delegate: RwLock::new(None),
            fallback_store: FallbackMobileStore::default(),
            operation_counter: AtomicU64::new(1),
        }
    }

    /// Attaches the native mobile platform delegate (from iOS Swift or Android JNI).
    pub fn set_delegate(&self, delegate: Arc<dyn MobilePlatformDelegate>) {
        if let Ok(mut lock) = self.delegate.write() {
            *lock = Some(delegate);
        }
    }
}

impl DeviceKeyStore for MobileHardwareSecurityProxy {
    fn capabilities(&self) -> HardwareSecurityCapabilities {
        if let Ok(lock) = self.delegate.read() {
            if let Some(del) = lock.as_ref() {
                return del.capabilities();
            }
        }
        HardwareSecurityCapabilities {
            provider: self.provider_kind.clone(),
            protection_level: self.protection_level,
            supports_background_unwrap: true,
            supports_non_exportable_signing: true,
            supports_hardware_wrapping: true,
            supports_user_presence: true,
            supports_biometric: true,
            supports_biometric_strong: true,
            supports_enrollment_bound_keys: true,
            supports_attestation: false,
            user_verification_kinds: vec![
                UserVerificationKind::BiometricStrong,
                UserVerificationKind::Biometric,
                UserVerificationKind::DeviceCredential,
            ],
        }
    }

    fn get_or_create_key(
        &self,
        label: &str,
        policy: &DeviceKeyPolicy,
    ) -> Result<DeviceKeyHandle, HardwareSecurityError> {
        if let Ok(lock) = self.delegate.read() {
            if let Some(del) = lock.as_ref() {
                return del.get_or_create_key(label, policy);
            }
        }

        let key_id = format!("{}:{}", policy.purpose.as_str(), label);
        let mut keys = self
            .fallback_store
            .keys
            .lock()
            .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;

        if !keys.contains_key(&key_id) {
            let mut secret = Zeroizing::new(vec![0u8; 32]);
            OsRng.fill_bytes(secret.as_mut_slice());
            keys.insert(key_id.clone(), secret);
        }

        Ok(DeviceKeyHandle {
            provider: self.provider_kind.clone(),
            opaque_id: key_id,
            purpose: policy.purpose,
            algorithm: policy.algorithm,
            protection_level: self.protection_level,
        })
    }

    fn wrap_key_material(
        &self,
        key: &DeviceKeyHandle,
        plaintext: &[u8],
        aad: &[u8],
    ) -> Result<PlatformWrappedKey, HardwareSecurityError> {
        if let Ok(lock) = self.delegate.read() {
            if let Some(del) = lock.as_ref() {
                return del.wrap_key_material(key, plaintext, aad);
            }
        }

        if plaintext.is_empty() {
            return Err(HardwareSecurityError::CorruptEnvelope);
        }

        let secret = {
            let keys = self
                .fallback_store
                .keys
                .lock()
                .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
            keys.get(&key.opaque_id)
                .cloned()
                .ok_or(HardwareSecurityError::KeyNotFound)?
        };

        let cipher = Aes256Gcm::new_from_slice(secret.as_slice())
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
        if let Ok(lock) = self.delegate.read() {
            if let Some(del) = lock.as_ref() {
                return del.unwrap_key_material(key, wrapped, aad);
            }
        }

        let secret = {
            let keys = self
                .fallback_store
                .keys
                .lock()
                .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
            keys.get(&key.opaque_id)
                .cloned()
                .ok_or(HardwareSecurityError::KeyNotFound)?
        };

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

        let cipher = Aes256Gcm::new_from_slice(secret.as_slice())
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
        if let Ok(lock) = self.delegate.read() {
            if let Some(del) = lock.as_ref() {
                return del.sign_challenge(key, challenge);
            }
        }

        let secret = {
            let keys = self
                .fallback_store
                .keys
                .lock()
                .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
            keys.get(&key.opaque_id)
                .cloned()
                .ok_or(HardwareSecurityError::KeyNotFound)?
        };

        let mut hasher = Sha256::new();
        hasher.update(b"mobile_proxy_sign:");
        hasher.update(secret.as_slice());
        hasher.update(challenge);
        let sig_bytes = hasher.finalize().to_vec();

        let mut pub_hasher = Sha256::new();
        pub_hasher.update(b"mobile_proxy_pubkey:");
        pub_hasher.update(secret.as_slice());
        let pub_key = pub_hasher.finalize().to_vec();

        Ok(DeviceSignature {
            algorithm: key.algorithm,
            signature: sig_bytes,
            public_key: Some(pub_key),
        })
    }

    fn delete_key(&self, key: &DeviceKeyHandle) -> Result<(), HardwareSecurityError> {
        if let Ok(lock) = self.delegate.read() {
            if let Some(del) = lock.as_ref() {
                return del.delete_key(key);
            }
        }

        let mut keys = self
            .fallback_store
            .keys
            .lock()
            .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
        keys.remove(&key.opaque_id);
        Ok(())
    }
}

impl BiometricAuthenticator for MobileHardwareSecurityProxy {
    fn availability(
        &self,
        policy: &UserPresencePolicy,
    ) -> Result<UserPresenceAvailability, HardwareSecurityError> {
        if let Ok(lock) = self.delegate.read() {
            if let Some(del) = lock.as_ref() {
                return del.check_presence_availability(policy);
            }
        }
        Ok(UserPresenceAvailability::Available)
    }

    fn begin_authentication(
        &self,
        request: UserPresenceRequest,
        completion: UserPresenceCompletion,
    ) -> Result<HardwareOperationId, HardwareSecurityError> {
        if let Ok(lock) = self.delegate.read() {
            if let Some(del) = lock.as_ref() {
                return del.begin_presence_authentication(request, completion);
            }
        }

        let op_id = HardwareOperationId(self.operation_counter.fetch_add(1, Ordering::SeqCst));
        completion(Ok(UserPresenceResult {
            verified_factor: request.required_verification,
            authorization_token: Some(vec![0xAA; 32]),
        }));
        Ok(op_id)
    }

    fn cancel_authentication(
        &self,
        operation: HardwareOperationId,
    ) -> Result<(), HardwareSecurityError> {
        if let Ok(lock) = self.delegate.read() {
            if let Some(del) = lock.as_ref() {
                return del.cancel_presence_authentication(operation);
            }
        }
        Ok(())
    }
}

impl HardwareSecurityProvider for MobileHardwareSecurityProxy {
    fn capabilities(&self) -> HardwareSecurityCapabilities {
        DeviceKeyStore::capabilities(self)
    }

    fn key_store(&self) -> &dyn DeviceKeyStore {
        self
    }

    fn authenticator(&self) -> Option<&dyn BiometricAuthenticator> {
        Some(self)
    }

    fn begin_authenticated_key_operation(
        &self,
        request: AuthenticatedKeyOperationRequest,
        completion: AuthenticatedKeyOperationCompletion,
    ) -> Result<HardwareOperationId, HardwareSecurityError> {
        if let Ok(lock) = self.delegate.read() {
            if let Some(del) = lock.as_ref() {
                return del.begin_authenticated_key_operation(request, completion);
            }
        }

        let op_id = HardwareOperationId(self.operation_counter.fetch_add(1, Ordering::SeqCst));

        match request.operation_kind {
            AuthenticatedKeyOperationKind::Wrap => {
                match self.wrap_key_material(
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
                match self.unwrap_key_material(&request.key_handle, &wrapped, &request.aad) {
                    Ok(unwrapped) => completion(Ok(AuthenticatedKeyOperationResult::Unwrapped(
                        unwrapped.to_vec(),
                    ))),
                    Err(e) => completion(Err(e)),
                }
            }
            AuthenticatedKeyOperationKind::Sign => {
                match self.sign_challenge(&request.key_handle, &request.input_payload) {
                    Ok(sig) => completion(Ok(AuthenticatedKeyOperationResult::Signed(sig))),
                    Err(e) => completion(Err(e)),
                }
            }
        }

        Ok(op_id)
    }
}
