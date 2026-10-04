use std::fmt;
use std::sync::Arc;

use rand::rngs::OsRng;
use rand::RngCore;
use security_framework::passwords::{
    delete_generic_password, generic_password, set_generic_password, PasswordOptions,
};
use zeroize::Zeroizing;

use super::{VaultDeviceProtector, VaultDeviceProtectorError};

const KEYCHAIN_SERVICE: &str = "com.maho.browser.vault-device-protector.v1";
const DEVICE_PROTECTOR_LENGTH: usize = 32;
const ERR_SEC_ITEM_NOT_FOUND: i32 = -25_300;

pub struct MacOsVaultDeviceProtector {
    device_id: Option<String>,
    keychain: Arc<dyn DeviceProtectorKeychain>,
}

impl fmt::Debug for MacOsVaultDeviceProtector {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("MacOsVaultDeviceProtector")
            .field("configured", &self.device_id.is_some())
            .finish_non_exhaustive()
    }
}

impl Default for MacOsVaultDeviceProtector {
    fn default() -> Self {
        Self::new()
    }
}

impl MacOsVaultDeviceProtector {
    /// Returns an unconfigured protector that cannot access the Keychain.
    pub fn new() -> Self {
        Self {
            device_id: None,
            keychain: Arc::new(SystemKeychain),
        }
    }

    /// Returns a protector scoped to the supplied persistent device identifier.
    pub fn for_device_id(device_id: impl Into<String>) -> Self {
        Self::with_keychain(device_id.into(), Arc::new(SystemKeychain))
    }

    /// Deletes this device's generated protector from the macOS Keychain.
    pub fn delete_device_secret(&self) -> Result<(), VaultDeviceProtectorError> {
        let device_id = self.device_id()?;
        self.keychain
            .delete(KEYCHAIN_SERVICE, device_id)
            .map_err(|_| VaultDeviceProtectorError::OperationFailed)
    }

    fn with_keychain(device_id: String, keychain: Arc<dyn DeviceProtectorKeychain>) -> Self {
        Self {
            device_id: (!device_id.trim().is_empty()).then_some(device_id),
            keychain,
        }
    }

    fn device_id(&self) -> Result<&str, VaultDeviceProtectorError> {
        self.device_id
            .as_deref()
            .ok_or(VaultDeviceProtectorError::Unavailable)
    }

    fn generate_device_secret() -> Zeroizing<Vec<u8>> {
        let mut secret = Zeroizing::new(vec![0; DEVICE_PROTECTOR_LENGTH]);
        OsRng.fill_bytes(secret.as_mut_slice());
        secret
    }
}

impl VaultDeviceProtector for MacOsVaultDeviceProtector {
    fn device_secret(&self) -> Result<Zeroizing<Vec<u8>>, VaultDeviceProtectorError> {
        let device_id = self.device_id()?;
        match self.keychain.read(KEYCHAIN_SERVICE, device_id) {
            Ok(secret) => {
                let secret = Zeroizing::new(secret);
                if secret.len() != DEVICE_PROTECTOR_LENGTH {
                    return Err(VaultDeviceProtectorError::OperationFailed);
                }
                Ok(secret)
            }
            Err(KeychainError::ItemNotFound) => {
                let secret = Self::generate_device_secret();
                self.keychain
                    .store(KEYCHAIN_SERVICE, device_id, secret.as_slice())
                    .map_err(|_| VaultDeviceProtectorError::OperationFailed)?;
                Ok(secret)
            }
            Err(KeychainError::OperationFailed) => Err(VaultDeviceProtectorError::OperationFailed),
        }
    }
}

trait DeviceProtectorKeychain: Send + Sync {
    fn read(&self, service: &str, account: &str) -> Result<Vec<u8>, KeychainError>;
    fn store(&self, service: &str, account: &str, secret: &[u8]) -> Result<(), KeychainError>;
    fn delete(&self, service: &str, account: &str) -> Result<(), KeychainError>;
}

#[derive(Clone, Copy)]
enum KeychainError {
    ItemNotFound,
    OperationFailed,
}

struct SystemKeychain;

impl DeviceProtectorKeychain for SystemKeychain {
    fn read(&self, service: &str, account: &str) -> Result<Vec<u8>, KeychainError> {
        generic_password(PasswordOptions::new_generic_password(service, account)).map_err(|error| {
            match error.code() {
                ERR_SEC_ITEM_NOT_FOUND => KeychainError::ItemNotFound,
                _ => KeychainError::OperationFailed,
            }
        })
    }

    fn store(&self, service: &str, account: &str, secret: &[u8]) -> Result<(), KeychainError> {
        set_generic_password(service, account, secret).map_err(|_| KeychainError::OperationFailed)
    }

    fn delete(&self, service: &str, account: &str) -> Result<(), KeychainError> {
        delete_generic_password(service, account).map_err(|_| KeychainError::OperationFailed)
    }
}

#[cfg(test)]
mod tests {
    use std::sync::Mutex;

    use super::*;

    #[derive(Default)]
    struct InMemoryKeychain {
        secret: Mutex<Option<Vec<u8>>>,
    }

    impl InMemoryKeychain {
        fn with_secret(secret: &[u8]) -> Self {
            Self {
                secret: Mutex::new(Some(secret.to_vec())),
            }
        }

        fn stored_secret(&self) -> Option<Vec<u8>> {
            self.secret
                .lock()
                .expect("test keychain lock poisoned")
                .clone()
        }
    }

    impl DeviceProtectorKeychain for InMemoryKeychain {
        fn read(&self, _: &str, _: &str) -> Result<Vec<u8>, KeychainError> {
            self.secret
                .lock()
                .expect("test keychain lock poisoned")
                .clone()
                .ok_or(KeychainError::ItemNotFound)
        }

        fn store(&self, _: &str, _: &str, secret: &[u8]) -> Result<(), KeychainError> {
            *self.secret.lock().expect("test keychain lock poisoned") = Some(secret.to_vec());
            Ok(())
        }

        fn delete(&self, _: &str, _: &str) -> Result<(), KeychainError> {
            self.secret
                .lock()
                .expect("test keychain lock poisoned")
                .take()
                .map(|_| ())
                .ok_or(KeychainError::ItemNotFound)
        }
    }

    struct FailingKeychain;

    impl DeviceProtectorKeychain for FailingKeychain {
        fn read(&self, _: &str, _: &str) -> Result<Vec<u8>, KeychainError> {
            Err(KeychainError::OperationFailed)
        }

        fn store(&self, _: &str, _: &str, _: &[u8]) -> Result<(), KeychainError> {
            Err(KeychainError::OperationFailed)
        }

        fn delete(&self, _: &str, _: &str) -> Result<(), KeychainError> {
            Err(KeychainError::OperationFailed)
        }
    }

    struct KeychainCleanup {
        protector: MacOsVaultDeviceProtector,
    }

    impl Drop for KeychainCleanup {
        fn drop(&mut self) {
            let _ = self.protector.delete_device_secret();
        }
    }

    fn protector_with_store(store: Arc<dyn DeviceProtectorKeychain>) -> MacOsVaultDeviceProtector {
        MacOsVaultDeviceProtector::with_keychain("device-123".to_owned(), store)
    }

    #[test]
    fn macos_vault_device_rejects_an_empty_device_id() {
        let protector = MacOsVaultDeviceProtector::for_device_id("");

        assert_eq!(
            protector.device_secret(),
            Err(VaultDeviceProtectorError::Unavailable)
        );
    }

    #[test]
    fn macos_vault_device_generates_and_stores_a_missing_protector() {
        let store = Arc::new(InMemoryKeychain::default());
        let protector = protector_with_store(store.clone());

        let secret = protector.device_secret().expect("generated protector");

        assert_eq!(secret.len(), DEVICE_PROTECTOR_LENGTH);
        assert_eq!(store.stored_secret(), Some(secret.to_vec()));
    }

    #[test]
    fn macos_vault_device_returns_an_existing_protector() {
        let stored_secret = b"0123456789abcdef0123456789abcdef";
        let store = Arc::new(InMemoryKeychain::with_secret(stored_secret));
        let protector = protector_with_store(store);

        let secret: Zeroizing<Vec<u8>> = protector.device_secret().expect("stored protector");

        assert_eq!(secret.as_slice(), stored_secret);
    }

    #[test]
    fn macos_vault_device_rejects_a_malformed_keychain_protector() {
        let store = Arc::new(InMemoryKeychain::with_secret(b"existing-protector"));
        let protector = protector_with_store(store);

        let result = protector.device_secret();

        assert_eq!(result, Err(VaultDeviceProtectorError::OperationFailed));
    }

    #[test]
    fn macos_vault_device_fails_closed_on_keychain_errors() {
        let protector = protector_with_store(Arc::new(FailingKeychain));

        let result = protector.device_secret();

        assert_eq!(result, Err(VaultDeviceProtectorError::OperationFailed));
    }

    #[test]
    fn macos_vault_device_deletes_its_protector() {
        let store = Arc::new(InMemoryKeychain::with_secret(b"existing-protector"));
        let protector = protector_with_store(store.clone());

        protector.delete_device_secret().expect("delete protector");

        assert_eq!(store.stored_secret(), None);
    }

    #[test]
    #[ignore = "requires access to the macOS login Keychain"]
    fn macos_vault_device_keychain_roundtrip() {
        let cleanup = KeychainCleanup {
            protector: MacOsVaultDeviceProtector::for_device_id(format!(
                "maho-core-test-{}",
                uuid::Uuid::new_v4()
            )),
        };

        let first = cleanup
            .protector
            .device_secret()
            .expect("store generated protector in Keychain");
        let second = cleanup
            .protector
            .device_secret()
            .expect("retrieve generated protector from Keychain");

        assert_eq!(first, second);
    }
}
