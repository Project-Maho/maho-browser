use std::fmt;

use maho_types::vault::VaultSchemaVersion;
use serde::{Deserialize, Serialize};
use zeroize::Zeroizing;

use crate::vault_crypto::WrappedVaultKeyEnvelope;

#[cfg(target_os = "linux")]
mod linux;
#[cfg(target_os = "macos")]
mod macos;
#[cfg(target_os = "windows")]
mod windows;

#[cfg(target_os = "linux")]
pub use linux::LinuxVaultDeviceProtector;
#[cfg(target_os = "macos")]
pub use macos::MacOsVaultDeviceProtector;
#[cfg(target_os = "windows")]
pub use windows::WindowsVaultDeviceProtector;

/// Device protector seam implemented by future platform-specific providers.
pub trait VaultDeviceProtector {
    fn device_secret(&self) -> Result<Zeroizing<Vec<u8>>, VaultDeviceProtectorError>;
}

#[derive(Clone, Debug, thiserror::Error, PartialEq, Eq)]
pub enum VaultDeviceProtectorError {
    #[error("device protector is unavailable")]
    Unavailable,
    #[error("device protector operation failed")]
    OperationFailed,
}

#[derive(Clone, Debug, thiserror::Error, PartialEq, Eq)]
pub enum VaultDeviceWrapBundleError {
    #[error("device wrapper serialization failed")]
    Serialization,
    #[error("device wrapper is corrupted or unsupported")]
    Corrupted,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum VaultDevicePlatform {
    Desktop,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VaultDeviceBinding {
    profile_id: String,
    device_id: String,
    platform: VaultDevicePlatform,
}

impl VaultDeviceBinding {
    pub fn new(profile_id: &str, device_id: &str, platform: VaultDevicePlatform) -> Self {
        Self {
            profile_id: profile_id.to_owned(),
            device_id: device_id.to_owned(),
            platform,
        }
    }

    pub(crate) fn profile_id(&self) -> &str {
        &self.profile_id
    }

    pub(crate) fn device_id(&self) -> &str {
        &self.device_id
    }

    pub(crate) const fn platform(&self) -> VaultDevicePlatform {
        self.platform
    }
}

#[derive(Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct VaultDeviceWrapBundle {
    schema_version: VaultSchemaVersion,
    device_id: String,
    platform: VaultDevicePlatform,
    envelope: WrappedVaultKeyEnvelope,
}

impl fmt::Debug for VaultDeviceWrapBundle {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("VaultDeviceWrapBundle")
            .field("schema_version", &self.schema_version)
            .field("device_id", &self.device_id)
            .field("platform", &self.platform)
            .field("envelope", &self.envelope)
            .finish()
    }
}

impl VaultDeviceWrapBundle {
    pub(crate) fn new(binding: &VaultDeviceBinding, envelope: WrappedVaultKeyEnvelope) -> Self {
        Self {
            schema_version: VaultSchemaVersion::CURRENT,
            device_id: binding.device_id.to_owned(),
            platform: binding.platform,
            envelope,
        }
    }

    pub(crate) const fn schema_version(&self) -> VaultSchemaVersion {
        self.schema_version
    }

    pub(crate) fn device_id(&self) -> &str {
        &self.device_id
    }

    pub(crate) const fn platform(&self) -> VaultDevicePlatform {
        self.platform
    }

    pub(crate) fn envelope(&self) -> &WrappedVaultKeyEnvelope {
        &self.envelope
    }

    pub fn to_opaque_bytes(&self) -> Result<Vec<u8>, VaultDeviceWrapBundleError> {
        serde_json::to_vec(self).map_err(|_| VaultDeviceWrapBundleError::Serialization)
    }

    pub fn from_opaque_bytes(bytes: &[u8]) -> Result<Self, VaultDeviceWrapBundleError> {
        serde_json::from_slice(bytes).map_err(|_| VaultDeviceWrapBundleError::Corrupted)
    }
}
