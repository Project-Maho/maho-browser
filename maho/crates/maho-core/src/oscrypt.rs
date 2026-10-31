// Copyright 2026 Maho Browser. All rights reserved.

use pbkdf2::pbkdf2_hmac;
use rand::RngCore;
use sha1::Sha1;
use std::fs::File;
use std::io::Write;
use std::path::Path;

mod vault_device;

pub use vault_device::{
    VaultDeviceBinding, VaultDevicePlatform, VaultDeviceProtector, VaultDeviceProtectorError,
    VaultDeviceWrapBundle, VaultDeviceWrapBundleError,
};

pub fn derive_key(password_file_path: &str, out_key: &mut [u8; 16], out_status: &mut u32) -> bool {
    let path = Path::new(password_file_path);
    let password;

    if path.exists() {
        match std::fs::read(path) {
            Ok(content) => {
                if content.is_empty() {
                    *out_status = 4; // Corrupt
                    return false;
                }
                password = content;
            }
            Err(_) => {
                *out_status = 1; // IoError
                return false;
            }
        }
    } else {
        // Generate 32 random bytes
        let mut bytes = vec![0u8; 32];
        rand::thread_rng().fill_bytes(&mut bytes);
        password = bytes;

        if write_atomically(path, &password).is_err() {
            *out_status = 1; // IoError
            return false;
        }
    }

    // Derive key using PBKDF2-HMAC-SHA1
    let salt = b"saltysalt";
    pbkdf2_hmac::<Sha1>(&password, salt, 1003, out_key);

    *out_status = 0; // Ok
    true
}

pub fn key_path_is_secure(path_str: &str, out_status: &mut u32) -> bool {
    let path = Path::new(path_str);
    if !path.exists() {
        *out_status = 1; // IoError
        return false;
    }

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        match std::fs::metadata(path) {
            Ok(metadata) => {
                let mode = metadata.permissions().mode() & 0o777;
                if mode == 0o600 {
                    *out_status = 0;
                    true
                } else {
                    *out_status = 2; // PermissionError
                    false
                }
            }
            Err(_) => {
                *out_status = 1;
                false
            }
        }
    }

    #[cfg(not(unix))]
    {
        *out_status = 0;
        true
    }
}

/// Vault device protector backed by a 0600 file inside the profile storage
/// directory (next to `maho_storage.key`). It never touches the OS keystore
/// (no macOS Keychain, Secret Service or DPAPI prompts): the device secret has
/// the same protection as the storage key file that already guards the
/// profile database.
pub struct FileVaultDeviceProtector {
    path: std::path::PathBuf,
}

impl FileVaultDeviceProtector {
    pub const FILE_NAME: &'static str = "maho_vault_device.key";
    const SECRET_LENGTH: usize = 32;

    pub fn new(path: std::path::PathBuf) -> Self {
        Self { path }
    }
}

impl VaultDeviceProtector for FileVaultDeviceProtector {
    fn device_secret(&self) -> Result<zeroize::Zeroizing<Vec<u8>>, VaultDeviceProtectorError> {
        match std::fs::read(&self.path) {
            Ok(bytes) => {
                let secret = zeroize::Zeroizing::new(bytes);
                if secret.len() != Self::SECRET_LENGTH {
                    return Err(VaultDeviceProtectorError::OperationFailed);
                }
                Ok(secret)
            }
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                let mut secret = zeroize::Zeroizing::new(vec![0u8; Self::SECRET_LENGTH]);
                rand::thread_rng().fill_bytes(secret.as_mut_slice());
                write_atomically(&self.path, secret.as_slice())
                    .map_err(|_| VaultDeviceProtectorError::OperationFailed)?;
                Ok(secret)
            }
            Err(_) => Err(VaultDeviceProtectorError::OperationFailed),
        }
    }
}

fn write_atomically(path: &Path, content: &[u8]) -> std::io::Result<()> {
    let parent = path.parent().ok_or_else(|| {
        std::io::Error::new(std::io::ErrorKind::NotFound, "Parent directory not found")
    })?;
    let temp_name = format!(
        "{}.tmp.{}",
        path.file_name().unwrap_or_default().to_string_lossy(),
        rand::random::<u64>()
    );
    let temp_path = parent.join(temp_name);

    {
        let mut file = File::create(&temp_path)?;
        file.write_all(content)?;
        file.sync_all()?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::set_permissions(&temp_path, std::fs::Permissions::from_mode(0o600))?;
        }
    }

    std::fs::rename(&temp_path, path).inspect_err(|_| {
        let _ = std::fs::remove_file(&temp_path);
    })?;
    Ok(())
}
