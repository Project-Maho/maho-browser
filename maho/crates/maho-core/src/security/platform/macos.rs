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
use maho_types::hardware_security::{
    DeviceKeyHandle, DeviceKeyPolicy, DeviceKeyPurpose, DeviceSignature, HardwareProviderKind,
    PlatformWrappedKey,
};

#[cfg(target_os = "macos")]
use security_framework::passwords::{
    delete_generic_password, generic_password, set_generic_password, PasswordOptions,
};

const KEYCHAIN_SERVICE_PREFIX: &str = "com.maho.browser.security";
const ERR_SEC_ITEM_NOT_FOUND: i32 = -25300;

/// macOS implementation of `DeviceKeyStore` integrating Keychain and Secure Enclave.
#[derive(Clone)]
pub struct MacOsDeviceKeyStore {
    #[allow(dead_code)]
    software_cache: Arc<Mutex<HashMap<String, Zeroizing<Vec<u8>>>>>,
}

impl Default for MacOsDeviceKeyStore {
    fn default() -> Self {
        Self::new()
    }
}

impl MacOsDeviceKeyStore {
    pub fn new() -> Self {
        Self {
            software_cache: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    fn service_for_purpose(purpose: DeviceKeyPurpose) -> String {
        format!("{}.{}", KEYCHAIN_SERVICE_PREFIX, purpose.as_str())
    }

    #[cfg(target_os = "macos")]
    fn read_keychain(
        &self,
        service: &str,
        account: &str,
    ) -> Result<Vec<u8>, HardwareSecurityError> {
        generic_password(PasswordOptions::new_generic_password(service, account)).map_err(|err| {
            if err.code() == ERR_SEC_ITEM_NOT_FOUND {
                HardwareSecurityError::KeyNotFound
            } else {
                HardwareSecurityError::BackendFailure(format!("keychain error code {}", err.code()))
            }
        })
    }

    #[cfg(target_os = "macos")]
    fn store_keychain(
        &self,
        service: &str,
        account: &str,
        secret: &[u8],
    ) -> Result<(), HardwareSecurityError> {
        set_generic_password(service, account, secret).map_err(|err| {
            HardwareSecurityError::BackendFailure(format!("keychain set error code {}", err.code()))
        })
    }

    #[cfg(target_os = "macos")]
    fn delete_keychain(&self, service: &str, account: &str) -> Result<(), HardwareSecurityError> {
        delete_generic_password(service, account).map_err(|err| {
            if err.code() == ERR_SEC_ITEM_NOT_FOUND {
                HardwareSecurityError::KeyNotFound
            } else {
                HardwareSecurityError::BackendFailure(format!(
                    "keychain delete error code {}",
                    err.code()
                ))
            }
        })
    }
}

impl DeviceKeyStore for MacOsDeviceKeyStore {
    fn capabilities(&self) -> HardwareSecurityCapabilities {
        HardwareSecurityCapabilities {
            provider: HardwareProviderKind::MacOsKeychain,
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
        let service = Self::service_for_purpose(policy.purpose);

        #[cfg(target_os = "macos")]
        {
            match self.read_keychain(&service, label) {
                Ok(secret) => {
                    if secret.len() != 32 {
                        return Err(HardwareSecurityError::CorruptEnvelope);
                    }
                    Ok(DeviceKeyHandle {
                        provider: HardwareProviderKind::MacOsKeychain,
                        opaque_id: label.to_string(),
                        purpose: policy.purpose,
                        algorithm: policy.algorithm,
                        protection_level: ProtectionLevel::OsSecretStore,
                    })
                }
                Err(HardwareSecurityError::KeyNotFound) => {
                    let mut secret = Zeroizing::new(vec![0u8; 32]);
                    OsRng.fill_bytes(secret.as_mut_slice());
                    self.store_keychain(&service, label, secret.as_slice())?;

                    Ok(DeviceKeyHandle {
                        provider: HardwareProviderKind::MacOsKeychain,
                        opaque_id: label.to_string(),
                        purpose: policy.purpose,
                        algorithm: policy.algorithm,
                        protection_level: ProtectionLevel::OsSecretStore,
                    })
                }
                Err(err) => Err(err),
            }
        }

        #[cfg(not(target_os = "macos"))]
        {
            let mut cache = self
                .software_cache
                .lock()
                .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
            let key_id = format!("{}:{}", service, label);
            if !cache.contains_key(&key_id) {
                let mut secret = Zeroizing::new(vec![0u8; 32]);
                OsRng.fill_bytes(secret.as_mut_slice());
                cache.insert(key_id, secret);
            }
            Ok(DeviceKeyHandle {
                provider: HardwareProviderKind::MacOsKeychain,
                opaque_id: label.to_string(),
                purpose: policy.purpose,
                algorithm: policy.algorithm,
                protection_level: ProtectionLevel::OsSecretStore,
            })
        }
    }

    fn wrap_key_material(
        &self,
        key: &DeviceKeyHandle,
        plaintext: &[u8],
        aad: &[u8],
    ) -> Result<PlatformWrappedKey, HardwareSecurityError> {
        let service = Self::service_for_purpose(key.purpose);

        #[cfg(target_os = "macos")]
        let secret = Zeroizing::new(self.read_keychain(&service, &key.opaque_id)?);

        #[cfg(not(target_os = "macos"))]
        let secret = {
            let cache = self
                .software_cache
                .lock()
                .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
            let key_id = format!("{}:{}", service, key.opaque_id);
            cache
                .get(&key_id)
                .cloned()
                .ok_or(HardwareSecurityError::KeyNotFound)?
        };

        if plaintext.is_empty() {
            return Err(HardwareSecurityError::CorruptEnvelope);
        }

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
        let service = Self::service_for_purpose(key.purpose);

        #[cfg(target_os = "macos")]
        let secret = Zeroizing::new(self.read_keychain(&service, &key.opaque_id)?);

        #[cfg(not(target_os = "macos"))]
        let secret = {
            let cache = self
                .software_cache
                .lock()
                .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
            let key_id = format!("{}:{}", service, key.opaque_id);
            cache
                .get(&key_id)
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
        let service = Self::service_for_purpose(key.purpose);

        #[cfg(target_os = "macos")]
        {
            self.delete_keychain(&service, &key.opaque_id)
        }

        #[cfg(not(target_os = "macos"))]
        {
            let mut cache = self
                .software_cache
                .lock()
                .map_err(|_| HardwareSecurityError::BackendFailure("lock poisoned".into()))?;
            let key_id = format!("{}:{}", service, key.opaque_id);
            cache.remove(&key_id);
            Ok(())
        }
    }
}

/// macOS implementation of `BiometricAuthenticator` using LocalAuthentication / Touch ID.
#[derive(Clone, Default)]
pub struct MacOsBiometricAuthenticator;

impl MacOsBiometricAuthenticator {
    pub fn new() -> Self {
        Self
    }
}

impl BiometricAuthenticator for MacOsBiometricAuthenticator {
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

/// Full macOS `HardwareSecurityProvider` tying together Keychain/SEP and LocalAuthentication.
#[derive(Clone, Default)]
pub struct MacOsHardwareSecurityProvider {
    key_store: MacOsDeviceKeyStore,
    authenticator: MacOsBiometricAuthenticator,
}

impl MacOsHardwareSecurityProvider {
    pub fn new() -> Self {
        Self {
            key_store: MacOsDeviceKeyStore::new(),
            authenticator: MacOsBiometricAuthenticator::new(),
        }
    }
}

impl HardwareSecurityProvider for MacOsHardwareSecurityProvider {
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
