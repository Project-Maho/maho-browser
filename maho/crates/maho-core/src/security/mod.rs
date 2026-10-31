// Copyright 2026 Maho Browser. All rights reserved.

pub mod envelope;
pub mod migration;
pub mod platform;
pub mod policy;

pub use maho_types::hardware_security::*;
use std::sync::{Arc, RwLock};
use zeroize::Zeroizing;

pub type UserPresenceCompletion =
    Box<dyn FnOnce(Result<UserPresenceResult, HardwareSecurityError>) + Send + Sync + 'static>;
pub type AuthenticatedKeyOperationCompletion = Box<
    dyn FnOnce(Result<AuthenticatedKeyOperationResult, HardwareSecurityError>)
        + Send
        + Sync
        + 'static,
>;

/// Background-capable key store interface for wrapping, unwrapping, and non-exportable signing.
pub trait DeviceKeyStore: Send + Sync {
    /// Reports capabilities of this key store.
    fn capabilities(&self) -> HardwareSecurityCapabilities;

    /// Retrieves an existing key or creates a new one satisfying the specified policy.
    fn get_or_create_key(
        &self,
        label: &str,
        policy: &DeviceKeyPolicy,
    ) -> Result<DeviceKeyHandle, HardwareSecurityError>;

    /// Wraps plaintext key material using the specified device key.
    fn wrap_key_material(
        &self,
        key: &DeviceKeyHandle,
        plaintext: &[u8],
        aad: &[u8],
    ) -> Result<PlatformWrappedKey, HardwareSecurityError>;

    /// Unwraps wrapped key material using the specified device key.
    fn unwrap_key_material(
        &self,
        key: &DeviceKeyHandle,
        wrapped: &PlatformWrappedKey,
        aad: &[u8],
    ) -> Result<Zeroizing<Vec<u8>>, HardwareSecurityError>;

    /// Signs a challenge using a non-exportable device signing key.
    fn sign_challenge(
        &self,
        key: &DeviceKeyHandle,
        challenge: &[u8],
    ) -> Result<DeviceSignature, HardwareSecurityError>;

    /// Deletes a device key from the underlying platform store.
    fn delete_key(&self, key: &DeviceKeyHandle) -> Result<(), HardwareSecurityError>;
}

/// Interactive user verification interface (e.g. Touch ID, Face ID, Windows Hello, PAM).
pub trait BiometricAuthenticator: Send + Sync {
    /// Checks availability of user presence verification under the specified policy.
    fn availability(
        &self,
        policy: &UserPresencePolicy,
    ) -> Result<UserPresenceAvailability, HardwareSecurityError>;

    /// Initiates an interactive user verification prompt.
    fn begin_authentication(
        &self,
        request: UserPresenceRequest,
        completion: UserPresenceCompletion,
    ) -> Result<HardwareOperationId, HardwareSecurityError>;

    /// Cancels a pending user verification operation.
    fn cancel_authentication(
        &self,
        operation: HardwareOperationId,
    ) -> Result<(), HardwareSecurityError>;
}

/// Unified hardware security provider coordinating key storage and interactive authentication.
pub trait HardwareSecurityProvider: Send + Sync {
    /// Reports aggregate hardware and protection capabilities.
    fn capabilities(&self) -> HardwareSecurityCapabilities;

    /// Returns the background-capable device key store.
    fn key_store(&self) -> &dyn DeviceKeyStore;

    /// Returns the interactive biometric/presence authenticator, if available.
    fn authenticator(&self) -> Option<&dyn BiometricAuthenticator>;

    /// Coordinates an interactive presence prompt bound directly to a cryptographic operation
    /// (e.g. Android BiometricPrompt.CryptoObject or macOS/iOS SecAccessControl).
    fn begin_authenticated_key_operation(
        &self,
        request: AuthenticatedKeyOperationRequest,
        completion: AuthenticatedKeyOperationCompletion,
    ) -> Result<HardwareOperationId, HardwareSecurityError>;
}

// Global provider registry for platform shell integration
static GLOBAL_PROVIDER: RwLock<Option<Arc<dyn HardwareSecurityProvider>>> = RwLock::new(None);

/// Registers the process-wide hardware security provider (e.g. from native shell or startup).
pub fn register_hardware_security_provider(provider: Arc<dyn HardwareSecurityProvider>) {
    if let Ok(mut lock) = GLOBAL_PROVIDER.write() {
        *lock = Some(provider);
    }
}

/// Returns the registered hardware security provider, or instantiates the default platform provider.
pub fn get_hardware_security_provider() -> Arc<dyn HardwareSecurityProvider> {
    if let Ok(lock) = GLOBAL_PROVIDER.read() {
        if let Some(provider) = lock.as_ref() {
            return Arc::clone(provider);
        }
    }
    default_hardware_security_provider()
}

/// Instantiates the default hardware security provider for the current operating system.
pub fn default_hardware_security_provider() -> Arc<dyn HardwareSecurityProvider> {
    #[cfg(target_os = "macos")]
    {
        Arc::new(platform::macos::MacOsHardwareSecurityProvider::new())
    }
    #[cfg(target_os = "windows")]
    {
        Arc::new(platform::windows::WindowsHardwareSecurityProvider::new())
    }
    #[cfg(target_os = "linux")]
    {
        Arc::new(platform::linux::LinuxHardwareSecurityProvider::new())
    }
    #[cfg(not(any(target_os = "macos", target_os = "windows", target_os = "linux")))]
    {
        Arc::new(platform::software::SoftwareHardwareSecurityProvider::new())
    }
}
