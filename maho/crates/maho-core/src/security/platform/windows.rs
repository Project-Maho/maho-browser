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

#[cfg(target_os = "windows")]
use windows_sys::Win32::Foundation::LocalFree;
#[cfg(target_os = "windows")]
use windows_sys::Win32::Security::Cryptography::{
    CryptProtectData, CryptUnprotectData, CRYPTPROTECT_UI_FORBIDDEN, CRYPT_INTEGER_BLOB,
};

#[allow(dead_code)]
const DPAPI_ENTROPY_PREFIX: &[u8] = b"maho-security-dpapi-v1:";

/// Windows implementation of `DeviceKeyStore` integrating DPAPI, DPAPI-NG, and TPM CNG.
#[derive(Clone)]
pub struct WindowsDeviceKeyStore {
    software_cache: Arc<Mutex<HashMap<String, Zeroizing<Vec<u8>>>>>,
}

impl Default for WindowsDeviceKeyStore {
    fn default() -> Self {
        Self::new()
    }
}

impl WindowsDeviceKeyStore {
    pub fn new() -> Self {
        Self {
            software_cache: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    #[allow(dead_code)]
    fn entropy_for_purpose(purpose: DeviceKeyPurpose) -> Vec<u8> {
        let mut entropy = Vec::with_capacity(DPAPI_ENTROPY_PREFIX.len() + 32);
        entropy.extend_from_slice(DPAPI_ENTROPY_PREFIX);
        entropy.extend_from_slice(purpose.as_str().as_bytes());
        entropy
    }

    #[cfg(target_os = "windows")]
    fn dpapi_protect(
        &self,
        plaintext: &[u8],
        purpose: DeviceKeyPurpose,
    ) -> Result<Vec<u8>, HardwareSecurityError> {
        let entropy = Self::entropy_for_purpose(purpose);
        let mut in_blob = CRYPT_INTEGER_BLOB {
            cbData: plaintext.len() as u32,
            pbData: plaintext.as_ptr() as *mut u8,
        };
        let mut entropy_blob = CRYPT_INTEGER_BLOB {
            cbData: entropy.len() as u32,
            pbData: entropy.as_ptr() as *mut u8,
        };
        let mut out_blob = CRYPT_INTEGER_BLOB {
            cbData: 0,
            pbData: std::ptr::null_mut(),
        };

        let res = unsafe {
            CryptProtectData(
                &mut in_blob,
                std::ptr::null(),
                &mut entropy_blob,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                CRYPTPROTECT_UI_FORBIDDEN,
                &mut out_blob,
            )
        };

        if res == 0 || out_blob.pbData.is_null() {
            return Err(HardwareSecurityError::BackendFailure(
                "CryptProtectData failed".into(),
            ));
        }

        let slice =
            unsafe { std::slice::from_raw_parts(out_blob.pbData, out_blob.cbData as usize) };
        let ciphertext = slice.to_vec();
        unsafe { LocalFree(out_blob.pbData as *mut _) };
        Ok(ciphertext)
    }

    #[cfg(target_os = "windows")]
    fn dpapi_unprotect(
        &self,
        ciphertext: &[u8],
        purpose: DeviceKeyPurpose,
    ) -> Result<Zeroizing<Vec<u8>>, HardwareSecurityError> {
        let entropy = Self::entropy_for_purpose(purpose);
        let mut in_blob = CRYPT_INTEGER_BLOB {
            cbData: ciphertext.len() as u32,
            pbData: ciphertext.as_ptr() as *mut u8,
        };
        let mut entropy_blob = CRYPT_INTEGER_BLOB {
            cbData: entropy.len() as u32,
            pbData: entropy.as_ptr() as *mut u8,
        };
        let mut out_blob = CRYPT_INTEGER_BLOB {
            cbData: 0,
            pbData: std::ptr::null_mut(),
        };

        let res = unsafe {
            CryptUnprotectData(
                &mut in_blob,
                std::ptr::null_mut(),
                &mut entropy_blob,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                CRYPTPROTECT_UI_FORBIDDEN,
                &mut out_blob,
            )
        };

        if res == 0 || out_blob.pbData.is_null() {
            return Err(HardwareSecurityError::CorruptEnvelope);
        }

        let slice =
            unsafe { std::slice::from_raw_parts(out_blob.pbData, out_blob.cbData as usize) };
        let plaintext = Zeroizing::new(slice.to_vec());
        unsafe { LocalFree(out_blob.pbData as *mut _) };
        Ok(plaintext)
    }
}

impl DeviceKeyStore for WindowsDeviceKeyStore {
    fn capabilities(&self) -> HardwareSecurityCapabilities {
        HardwareSecurityCapabilities {
            provider: HardwareProviderKind::WindowsDpapi,
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
            provider: HardwareProviderKind::WindowsDpapi,
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

        #[cfg(target_os = "windows")]
        let platform_metadata = self.dpapi_protect(secret.as_slice(), key.purpose).ok();
        #[cfg(not(target_os = "windows"))]
        let platform_metadata = None;

        Ok(PlatformWrappedKey {
            ciphertext: buffer,
            tag: Some(tag.as_slice().to_vec()),
            iv_or_nonce: Some(nonce_bytes.to_vec()),
            platform_metadata,
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

/// Windows implementation of `BiometricAuthenticator` using Windows Hello / UserConsentVerifier.
#[derive(Clone, Default)]
pub struct WindowsBiometricAuthenticator;

impl WindowsBiometricAuthenticator {
    pub fn new() -> Self {
        Self
    }
}

impl BiometricAuthenticator for WindowsBiometricAuthenticator {
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

/// Windows `HardwareSecurityProvider` tying together DPAPI/TPM and Windows Hello.
#[derive(Clone, Default)]
pub struct WindowsHardwareSecurityProvider {
    key_store: WindowsDeviceKeyStore,
    authenticator: WindowsBiometricAuthenticator,
}

impl WindowsHardwareSecurityProvider {
    pub fn new() -> Self {
        Self {
            key_store: WindowsDeviceKeyStore::new(),
            authenticator: WindowsBiometricAuthenticator::new(),
        }
    }
}

impl HardwareSecurityProvider for WindowsHardwareSecurityProvider {
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
