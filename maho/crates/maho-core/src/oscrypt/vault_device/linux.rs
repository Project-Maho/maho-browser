use std::collections::HashMap;
use std::sync::Mutex;

use secret_service::blocking::SecretService;
use secret_service::{EncryptionType, Error as SecretServiceError};
use zeroize::Zeroizing;

use super::{VaultDeviceProtector, VaultDeviceProtectorError};

const GENERATED_DEVICE_PROTECTOR_LEN: usize = 32;
const ITEM_LABEL: &str = "Maho Vault Device Protector";
const CONTENT_TYPE: &str = "application/octet-stream";
const ATTRIBUTE_APPLICATION: &str = "application";
const ATTRIBUTE_APPLICATION_VALUE: &str = "maho";
const ATTRIBUTE_KIND: &str = "maho.secret-kind";
const ATTRIBUTE_KIND_VALUE: &str = "vault-device-protector";
const ATTRIBUTE_ACCOUNT: &str = "maho.account-id";
const ATTRIBUTE_DEVICE: &str = "maho.device-id";
const DEFAULT_ACCOUNT_ID: &str = "local";
const DEFAULT_DEVICE_ID: &str = "local";

#[derive(Debug)]
pub struct LinuxVaultDeviceProtector {
    account_id: String,
    device_id: String,
    operation_lock: Mutex<()>,
}

impl Default for LinuxVaultDeviceProtector {
    fn default() -> Self {
        Self::new()
    }
}

impl LinuxVaultDeviceProtector {
    pub fn new() -> Self {
        Self::for_device(DEFAULT_ACCOUNT_ID, DEFAULT_DEVICE_ID)
    }

    pub fn for_device(account_id: &str, device_id: &str) -> Self {
        Self {
            account_id: account_id.to_owned(),
            device_id: device_id.to_owned(),
            operation_lock: Mutex::new(()),
        }
    }

    pub fn delete_device_secret(&self) -> Result<(), VaultDeviceProtectorError> {
        let _operation = self
            .operation_lock
            .lock()
            .map_err(|_| VaultDeviceProtectorError::OperationFailed)?;
        let service =
            SecretService::connect(EncryptionType::Dh).map_err(map_secret_service_error)?;
        let collection = service
            .get_default_collection()
            .map_err(map_secret_service_error)?;
        collection
            .ensure_unlocked()
            .map_err(map_secret_service_error)?;
        let items = collection
            .search_items(self.attributes())
            .map_err(map_secret_service_error)?;

        match items.as_slice() {
            [] => Ok(()),
            [item] => item.delete().map_err(map_secret_service_error),
            _ => Err(VaultDeviceProtectorError::OperationFailed),
        }
    }

    fn attributes(&self) -> HashMap<&str, &str> {
        HashMap::from([
            (ATTRIBUTE_APPLICATION, ATTRIBUTE_APPLICATION_VALUE),
            (ATTRIBUTE_KIND, ATTRIBUTE_KIND_VALUE),
            (ATTRIBUTE_ACCOUNT, self.account_id.as_str()),
            (ATTRIBUTE_DEVICE, self.device_id.as_str()),
        ])
    }
}

impl VaultDeviceProtector for LinuxVaultDeviceProtector {
    fn device_secret(&self) -> Result<Zeroizing<Vec<u8>>, VaultDeviceProtectorError> {
        let _operation = self
            .operation_lock
            .lock()
            .map_err(|_| VaultDeviceProtectorError::OperationFailed)?;
        let service =
            SecretService::connect(EncryptionType::Dh).map_err(map_secret_service_error)?;
        let collection = service
            .get_default_collection()
            .map_err(map_secret_service_error)?;
        collection
            .ensure_unlocked()
            .map_err(map_secret_service_error)?;
        let items = collection
            .search_items(self.attributes())
            .map_err(map_secret_service_error)?;

        match items.as_slice() {
            [] => create_device_secret(&collection, self.attributes()),
            [item] => {
                item.ensure_unlocked().map_err(map_secret_service_error)?;
                let secret = Zeroizing::new(item.get_secret().map_err(map_secret_service_error)?);
                validate_device_secret(secret)
            }
            _ => Err(VaultDeviceProtectorError::OperationFailed),
        }
    }
}

fn create_device_secret(
    collection: &secret_service::blocking::Collection<'_>,
    attributes: HashMap<&str, &str>,
) -> Result<Zeroizing<Vec<u8>>, VaultDeviceProtectorError> {
    let mut secret = Zeroizing::new(vec![0; GENERATED_DEVICE_PROTECTOR_LEN]);
    rand::RngCore::try_fill_bytes(&mut rand::rngs::OsRng, secret.as_mut())
        .map_err(|_| VaultDeviceProtectorError::OperationFailed)?;
    collection
        .create_item(
            ITEM_LABEL,
            attributes,
            secret.as_slice(),
            false,
            CONTENT_TYPE,
        )
        .map_err(map_secret_service_error)?;
    Ok(secret)
}

fn validate_device_secret(
    secret: Zeroizing<Vec<u8>>,
) -> Result<Zeroizing<Vec<u8>>, VaultDeviceProtectorError> {
    if secret.len() == GENERATED_DEVICE_PROTECTOR_LEN {
        Ok(secret)
    } else {
        Err(VaultDeviceProtectorError::OperationFailed)
    }
}

fn map_secret_service_error(error: SecretServiceError) -> VaultDeviceProtectorError {
    match error {
        SecretServiceError::Unavailable => VaultDeviceProtectorError::Unavailable,
        _ => VaultDeviceProtectorError::OperationFailed,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn attributes_include_only_maho_device_identity_metadata() {
        // Given: a provider bound to public account and device identifiers.
        let protector = LinuxVaultDeviceProtector::for_device("account-123", "device-456");

        // When: its Secret Service search attributes are constructed.
        let attributes = protector.attributes();

        // Then: they identify only Maho's device protector and its non-secret owner metadata.
        assert_eq!(attributes.len(), 4);
        assert_eq!(
            attributes.get(ATTRIBUTE_APPLICATION),
            Some(&ATTRIBUTE_APPLICATION_VALUE)
        );
        assert_eq!(attributes.get(ATTRIBUTE_KIND), Some(&ATTRIBUTE_KIND_VALUE));
        assert_eq!(attributes.get(ATTRIBUTE_ACCOUNT), Some(&"account-123"));
        assert_eq!(attributes.get(ATTRIBUTE_DEVICE), Some(&"device-456"));
    }

    #[test]
    fn malformed_device_secret_fails_closed() {
        // Given: a Secret Service payload with a non-empty but invalid protector length.
        let secret = Zeroizing::new(vec![0]);

        // When: the provider validates the payload.
        let result = validate_device_secret(secret);

        // Then: it refuses to use the malformed secret.
        assert_eq!(result, Err(VaultDeviceProtectorError::OperationFailed));
    }

    #[test]
    fn unavailable_secret_service_maps_to_unavailable() {
        // Given: the Secret Service client reports no provider.
        let error = SecretServiceError::Unavailable;

        // When: the provider maps that boundary error.
        let mapped = map_secret_service_error(error);

        // Then: callers receive the typed unavailable error.
        assert_eq!(mapped, VaultDeviceProtectorError::Unavailable);
    }
}
