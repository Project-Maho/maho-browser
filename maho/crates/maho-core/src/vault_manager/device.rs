//! Device-wrapped root-key operations for desktop provider integrations.

use chrono::{DateTime, Utc};
use maho_types::vault::VaultSchemaVersion;

use crate::oscrypt::{VaultDeviceBinding, VaultDeviceProtector, VaultDeviceWrapBundle};
use crate::vault_crypto::{
    derive_device_wrapping_key, unwrap_vault_key, wrap_vault_key, VaultCryptoError,
};

use super::state::RuntimeState;
use super::{VaultManager, VaultManagerError};

const DEVICE_WRAP_AAD_PREFIX: &[u8] = b"maho-vault-device-wrap-v1";

impl VaultManager {
    pub fn wrap_active_key_for_device(
        &self,
        binding: &VaultDeviceBinding,
        protector: &dyn VaultDeviceProtector,
    ) -> Result<VaultDeviceWrapBundle, VaultManagerError> {
        let key = self.active_key()?;
        let metadata = self
            .kdf_metadata
            .as_ref()
            .ok_or(VaultManagerError::Uninitialized)?;
        let secret = protector.device_secret()?;
        let wrapping_key = derive_device_wrapping_key(secret.as_slice(), metadata)?;
        let envelope = wrap_vault_key(key, &wrapping_key, &device_wrap_aad(binding))?;
        Ok(VaultDeviceWrapBundle::new(binding, envelope))
    }

    pub fn unlock_with_device(
        &mut self,
        bundle: &VaultDeviceWrapBundle,
        binding: &VaultDeviceBinding,
        protector: &dyn VaultDeviceProtector,
        now: DateTime<Utc>,
    ) -> Result<(), VaultManagerError> {
        let metadata = self
            .kdf_metadata
            .clone()
            .ok_or(VaultManagerError::Uninitialized)?;
        self.active_key = None;
        self.last_activity = None;
        self.state = RuntimeState::Locked;
        if bundle.schema_version() != VaultSchemaVersion::CURRENT
            || bundle.device_id() != binding.device_id()
            || bundle.platform() != binding.platform()
        {
            return Err(VaultManagerError::DeviceBindingMismatch);
        }
        let secret = protector.device_secret()?;
        let wrapping_key = derive_device_wrapping_key(secret.as_slice(), &metadata)?;
        self.state = RuntimeState::Unlocking;
        let key =
            match unwrap_vault_key(bundle.envelope(), &wrapping_key, &device_wrap_aad(binding)) {
                Ok(key) => key,
                Err(VaultCryptoError::AuthenticationFailed) => {
                    self.state = RuntimeState::Locked;
                    return Err(VaultManagerError::InvalidCredentials);
                }
                Err(
                    VaultCryptoError::InvalidEnvelope
                    | VaultCryptoError::InputTooLarge
                    | VaultCryptoError::WrappingDomainMismatch,
                ) => {
                    self.state = RuntimeState::Locked;
                    return Err(VaultManagerError::CorruptedVaultData);
                }
                Err(error) => {
                    self.state = RuntimeState::Locked;
                    return Err(VaultManagerError::Crypto(error));
                }
            };
        self.active_key = Some(key);
        self.state = RuntimeState::Unlocked;
        self.throttle.reset();
        self.last_activity = Some(now);
        Ok(())
    }
}

fn device_wrap_aad(binding: &VaultDeviceBinding) -> Vec<u8> {
    let profile_id = binding.profile_id().as_bytes();
    let device_id = binding.device_id().as_bytes();
    let mut aad = Vec::with_capacity(
        DEVICE_WRAP_AAD_PREFIX.len() + 1 + 8 + profile_id.len() + 8 + device_id.len(),
    );
    aad.extend_from_slice(DEVICE_WRAP_AAD_PREFIX);
    aad.push(match binding.platform() {
        crate::oscrypt::VaultDevicePlatform::Desktop => 1,
    });
    append_aad_component(&mut aad, profile_id);
    append_aad_component(&mut aad, device_id);
    aad
}

fn append_aad_component(aad: &mut Vec<u8>, component: &[u8]) {
    let length = u64::try_from(component.len()).map_or(u64::MAX, |length| length);
    aad.extend_from_slice(&length.to_be_bytes());
    aad.extend_from_slice(component);
}
