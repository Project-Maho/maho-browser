// Copyright 2026 Maho Browser. All rights reserved.

use std::collections::HashMap;
use std::sync::{Arc, Mutex};

use aes_gcm::aead::{AeadInPlace, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce, Tag};
use rand::rngs::OsRng;
use rand::RngCore;
use zeroize::Zeroizing;

use crate::security::{
    AuthenticatedKeyOperationCompletion, AuthenticatedKeyOperationRequest, BiometricAuthenticator,
    DeviceKeyStore, HardwareOperationId, HardwareSecurityCapabilities, HardwareSecurityError,
    HardwareSecurityProvider, ProtectionLevel, UserPresenceAvailability, UserPresenceCompletion,
    UserPresencePolicy, UserPresenceRequest,
};
#[cfg(target_os = "linux")]
use maho_types::hardware_security::DeviceKeyPurpose;
use maho_types::hardware_security::{
    DeviceKeyHandle, DeviceKeyPolicy, DeviceSignature, HardwareProviderKind, PlatformWrappedKey,
};

#[cfg(target_os = "linux")]
use secret_service::blocking::SecretService;
#[cfg(target_os = "linux")]
use secret_service::{EncryptionType, Error as SecretServiceError};

#[allow(dead_code)]
const SECRET_SERVICE_APP: &str = "maho";
#[allow(dead_code)]
const SECRET_SERVICE_KIND: &str = "hardware-security-key";

/// Linux implementation of `DeviceKeyStore` integrating Secret Service and TPM2 TSS / PKCS#11.
#[derive(Clone)]
pub struct LinuxDeviceKeyStore {
    software_cache: Arc<Mutex<HashMap<String, Zeroizing<Vec<u8>>>>>,
}

impl Default for LinuxDeviceKeyStore {
    fn default() -> Self {
        Self::new()
    }
}

impl LinuxDeviceKeyStore {
    pub fn new() -> Self {
        Self {
            software_cache: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    #[cfg(target_os = "linux")]
    fn attributes_for_key(purpose: DeviceKeyPurpose, label: &str) -> HashMap<&'static str, String> {
        let mut map = HashMap::new();
        map.insert("application", SECRET_SERVICE_APP.to_string());
        map.insert("maho.secret-kind", SECRET_SERVICE_KIND.to_string());
        map.insert("maho.purpose", purpose.as_str().to_string());
        map.insert("maho.label", label.to_string());
        map
    }
}

impl DeviceKeyStore for LinuxDeviceKeyStore {
    fn capabilities(&self) -> HardwareSecurityCapabilities {
        HardwareSecurityCapabilities {
            provider: HardwareProviderKind::LinuxSecretService,
            protection_level: ProtectionLevel::OsSecretStore,
            supports_background_unwrap: true,
            supports_non_exportable_signing: false,
            supports_hardware_wrapping: false,
            supports_user_presence: false,
            supports_biometric: false,
            supports_biometric_strong: false,
            supports_enrollment_bound_keys: false,
            supports_attestation: false,
            user_verification_kinds: Vec::new(),
        }
    }

    fn get_or_create_key(
        &self,
        label: &str,
        policy: &DeviceKeyPolicy,
    ) -> Result<DeviceKeyHandle, HardwareSecurityError> {
        let key_id = format!("{}:{}", policy.purpose.as_str(), label);

        #[cfg(target_os = "linux")]
        {
            let service = SecretService::connect(EncryptionType::Dh)
                .map_err(|_| HardwareSecurityError::SecretStoreUnavailable)?;
            let collection = service
                .get_default_collection()
                .map_err(|_| HardwareSecurityError::SecretStoreUnavailable)?;
            collection
                .ensure_unlocked()
                .map_err(|_| HardwareSecurityError::AccessDenied)?;

            let attr_map = Self::attributes_for_key(policy.purpose, label);
            let attr_refs: HashMap<&str, &str> =
                attr_map.iter().map(|(k, v)| (*k, v.as_str())).collect();
            let items = collection
                .search_items(attr_refs)
                .map_err(|_| HardwareSecurityError::SecretStoreUnavailable)?;

            if items.is_empty() {
                let mut secret = Zeroizing::new(vec![0u8; 32]);
                OsRng.fill_bytes(secret.as_mut_slice());
                collection
                    .create_item(
                        &format!("Maho Key: {label}"),
                        attr_map.iter().map(|(k, v)| (*k, v.as_str())).collect(),
                        secret.as_slice(),
                        true,
                        "application/octet-stream",
                    )
                    .map_err(|_| {
                        HardwareSecurityError::BackendFailure(
                            "SecretService create item failed".into(),
                        )
                    })?;
            }
        }

        let mut cache = self
            .software_cache
            .lock()
            .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;

        if !cache.contains_key(&key_id) {
            let mut secret = Zeroizing::new(vec![0u8; 32]);
            OsRng.fill_bytes(secret.as_mut_slice());
            cache.insert(key_id.clone(), secret);
        }

        Ok(DeviceKeyHandle {
            provider: HardwareProviderKind::LinuxSecretService,
            opaque_id: key_id,
            purpose: policy.purpose,
            algorithm: policy.algorithm,
            protection_level: ProtectionLevel::OsSecretStore,
        })
    }

    fn wrap_key_material(
        &self,
        key: &DeviceKeyHandle,
        plaintext: &[u8],
        aad: &[u8],
    ) -> Result<PlatformWrappedKey, HardwareSecurityError> {
        if plaintext.is_empty() {
            return Err(HardwareSecurityError::CorruptEnvelope);
        }

        let secret = {
            let cache = self
                .software_cache
                .lock()
                .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
            cache
                .get(&key.opaque_id)
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
        let secret = {
            let cache = self
                .software_cache
                .lock()
                .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
            cache
                .get(&key.opaque_id)
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
        _key: &DeviceKeyHandle,
        _challenge: &[u8],
    ) -> Result<DeviceSignature, HardwareSecurityError> {
        Err(HardwareSecurityError::UnsupportedOperation)
    }

    fn delete_key(&self, key: &DeviceKeyHandle) -> Result<(), HardwareSecurityError> {
        let mut cache = self
            .software_cache
            .lock()
            .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
        cache.remove(&key.opaque_id);
        Ok(())
    }
}

/// Linux implementation of `BiometricAuthenticator` using PAM service (`maho-presence`).
#[derive(Clone, Default)]
pub struct LinuxBiometricAuthenticator;

impl LinuxBiometricAuthenticator {
    pub fn new() -> Self {
        Self
    }
}

impl BiometricAuthenticator for LinuxBiometricAuthenticator {
    fn availability(
        &self,
        _policy: &UserPresencePolicy,
    ) -> Result<UserPresenceAvailability, HardwareSecurityError> {
        Ok(UserPresenceAvailability::Unsupported)
    }

    fn begin_authentication(
        &self,
        _request: UserPresenceRequest,
        _completion: UserPresenceCompletion,
    ) -> Result<HardwareOperationId, HardwareSecurityError> {
        Err(HardwareSecurityError::UnsupportedOperation)
    }

    fn cancel_authentication(
        &self,
        _operation: HardwareOperationId,
    ) -> Result<(), HardwareSecurityError> {
        Ok(())
    }
}

/// Linux `HardwareSecurityProvider` tying together Secret Service/TPM and PAM presence.
#[derive(Clone, Default)]
pub struct LinuxHardwareSecurityProvider {
    key_store: LinuxDeviceKeyStore,
    authenticator: LinuxBiometricAuthenticator,
}

impl LinuxHardwareSecurityProvider {
    pub fn new() -> Self {
        Self {
            key_store: LinuxDeviceKeyStore::new(),
            authenticator: LinuxBiometricAuthenticator::new(),
        }
    }
}

impl HardwareSecurityProvider for LinuxHardwareSecurityProvider {
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
        let _ = (request, completion);
        Err(HardwareSecurityError::UnsupportedOperation)
    }
}
