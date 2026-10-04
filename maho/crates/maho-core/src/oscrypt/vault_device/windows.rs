use core::ffi::c_void;
use core::ptr;

use rand::RngCore;
use windows_sys::Win32::Foundation::LocalFree;
use windows_sys::Win32::Security::Cryptography::{
    CryptProtectData, CryptUnprotectData, CRYPTPROTECT_UI_FORBIDDEN, CRYPT_INTEGER_BLOB,
};
use zeroize::{Zeroize, Zeroizing};

use super::{
    VaultDeviceBinding, VaultDevicePlatform, VaultDeviceProtector, VaultDeviceProtectorError,
};

const DEVICE_SECRET_LENGTH: usize = 32;
const ENTROPY_DOMAIN: &[u8] = b"maho-vault-device-dpapi-v1";

#[derive(Clone, PartialEq, Eq)]
pub struct WindowsProtectedDeviceSecret {
    bytes: Vec<u8>,
}

impl core::fmt::Debug for WindowsProtectedDeviceSecret {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("WindowsProtectedDeviceSecret")
            .field("byte_len", &self.bytes.len())
            .finish()
    }
}

impl WindowsProtectedDeviceSecret {
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, VaultDeviceProtectorError> {
        if bytes.is_empty() {
            return Err(VaultDeviceProtectorError::OperationFailed);
        }

        Ok(Self {
            bytes: bytes.to_vec(),
        })
    }

    pub fn as_bytes(&self) -> &[u8] {
        &self.bytes
    }
}

#[derive(Debug, Default)]
pub struct WindowsVaultDeviceProtector {
    protected_secret: Option<WindowsProtectedDeviceSecret>,
    entropy: Vec<u8>,
}

impl WindowsVaultDeviceProtector {
    pub const fn new() -> Self {
        Self {
            protected_secret: None,
            entropy: Vec::new(),
        }
    }

    pub fn generate(
        entropy: Option<&[u8]>,
    ) -> Result<(Self, WindowsProtectedDeviceSecret), VaultDeviceProtectorError> {
        let mut secret = Zeroizing::new(vec![0_u8; DEVICE_SECRET_LENGTH]);
        rand::rngs::OsRng
            .try_fill_bytes(secret.as_mut_slice())
            .map_err(|_| VaultDeviceProtectorError::OperationFailed)?;
        let protected_secret = protect_data(secret.as_slice(), entropy)?;
        let protector = Self::from_protected_secret(protected_secret.clone(), entropy);
        Ok((protector, protected_secret))
    }

    pub fn from_protected_secret(
        protected_secret: WindowsProtectedDeviceSecret,
        entropy: Option<&[u8]>,
    ) -> Self {
        Self {
            protected_secret: Some(protected_secret),
            entropy: entropy.map_or_else(Vec::new, ToOwned::to_owned),
        }
    }

    pub fn generate_for_binding(
        binding: &VaultDeviceBinding,
    ) -> Result<(Self, WindowsProtectedDeviceSecret), VaultDeviceProtectorError> {
        let entropy = binding_entropy(binding);
        Self::generate(Some(&entropy))
    }

    pub fn from_protected_secret_for_binding(
        protected_secret: WindowsProtectedDeviceSecret,
        binding: &VaultDeviceBinding,
    ) -> Self {
        let entropy = binding_entropy(binding);
        Self::from_protected_secret(protected_secret, Some(&entropy))
    }
}

impl VaultDeviceProtector for WindowsVaultDeviceProtector {
    fn device_secret(&self) -> Result<Zeroizing<Vec<u8>>, VaultDeviceProtectorError> {
        let protected_secret = self
            .protected_secret
            .as_ref()
            .ok_or(VaultDeviceProtectorError::Unavailable)?;
        let secret = unprotect_data(protected_secret.as_bytes(), entropy(&self.entropy))?;
        if secret.len() != DEVICE_SECRET_LENGTH {
            return Err(VaultDeviceProtectorError::OperationFailed);
        }
        Ok(secret)
    }
}

fn protect_data(
    secret: &[u8],
    entropy: Option<&[u8]>,
) -> Result<WindowsProtectedDeviceSecret, VaultDeviceProtectorError> {
    let input = data_blob(secret)?;
    let entropy_blob = entropy.map(data_blob).transpose()?;
    let mut output = DpapiOutput::new(false);

    // SAFETY: [Category 8 - FFI boundary UB] `input` and optional entropy point to
    // live byte slices for this call, and `output` is a zeroed output structure.
    let protected = unsafe {
        CryptProtectData(
            &input,
            ptr::null(),
            entropy_blob.as_ref().map_or(ptr::null(), |blob| blob),
            ptr::null(),
            ptr::null(),
            CRYPTPROTECT_UI_FORBIDDEN,
            &mut output.blob,
        )
    };
    if protected == 0 {
        return Err(VaultDeviceProtectorError::OperationFailed);
    }

    let protected_secret = output.copy()?;
    WindowsProtectedDeviceSecret::from_bytes(&protected_secret)
}

fn unprotect_data(
    protected_secret: &[u8],
    entropy: Option<&[u8]>,
) -> Result<Zeroizing<Vec<u8>>, VaultDeviceProtectorError> {
    let input = data_blob(protected_secret)?;
    let entropy_blob = entropy.map(data_blob).transpose()?;
    let mut output = DpapiOutput::new(true);

    // SAFETY: [Category 8 - FFI boundary UB] `input` and optional entropy point to
    // live byte slices for this call, and `output` is a zeroed output structure.
    let unprotected = unsafe {
        CryptUnprotectData(
            &input,
            ptr::null_mut(),
            entropy_blob.as_ref().map_or(ptr::null(), |blob| blob),
            ptr::null(),
            ptr::null(),
            CRYPTPROTECT_UI_FORBIDDEN,
            &mut output.blob,
        )
    };
    if unprotected == 0 {
        return Err(VaultDeviceProtectorError::OperationFailed);
    }

    Ok(Zeroizing::new(output.copy()?))
}

fn data_blob(bytes: &[u8]) -> Result<CRYPT_INTEGER_BLOB, VaultDeviceProtectorError> {
    let byte_count =
        u32::try_from(bytes.len()).map_err(|_| VaultDeviceProtectorError::OperationFailed)?;
    Ok(CRYPT_INTEGER_BLOB {
        cbData: byte_count,
        pbData: bytes.as_ptr().cast_mut(),
    })
}

fn entropy(bytes: &[u8]) -> Option<&[u8]> {
    (!bytes.is_empty()).then_some(bytes)
}

fn binding_entropy(binding: &VaultDeviceBinding) -> Vec<u8> {
    let profile_id = binding.profile_id().as_bytes();
    let device_id = binding.device_id().as_bytes();
    let mut entropy =
        Vec::with_capacity(ENTROPY_DOMAIN.len() + 1 + 8 + profile_id.len() + 8 + device_id.len());
    entropy.extend_from_slice(ENTROPY_DOMAIN);
    entropy.push(match binding.platform() {
        VaultDevicePlatform::Desktop => 1,
    });
    append_entropy_component(&mut entropy, profile_id);
    append_entropy_component(&mut entropy, device_id);
    entropy
}

fn append_entropy_component(entropy: &mut Vec<u8>, component: &[u8]) {
    let length = u64::try_from(component.len()).map_or(u64::MAX, |length| length);
    entropy.extend_from_slice(&length.to_be_bytes());
    entropy.extend_from_slice(component);
}

struct DpapiOutput {
    blob: CRYPT_INTEGER_BLOB,
    zero_on_drop: bool,
}

impl DpapiOutput {
    const fn new(zero_on_drop: bool) -> Self {
        Self {
            blob: CRYPT_INTEGER_BLOB {
                cbData: 0,
                pbData: ptr::null_mut(),
            },
            zero_on_drop,
        }
    }

    fn copy(&self) -> Result<Vec<u8>, VaultDeviceProtectorError> {
        let byte_count = usize::try_from(self.blob.cbData)
            .map_err(|_| VaultDeviceProtectorError::OperationFailed)?;
        if self.blob.pbData.is_null() {
            return Err(VaultDeviceProtectorError::OperationFailed);
        }

        // SAFETY: [Category 10 - Out-of-bounds access] successful DPAPI calls
        // allocate `cbData` bytes at `pbData`; this object owns that allocation.
        Ok(unsafe { core::slice::from_raw_parts(self.blob.pbData, byte_count) }.to_vec())
    }
}

impl Drop for DpapiOutput {
    fn drop(&mut self) {
        if self.blob.pbData.is_null() {
            return;
        }

        if self.zero_on_drop {
            // SAFETY: [Category 10 - Out-of-bounds access] DPAPI allocated exactly
            // `cbData` bytes at `pbData`, and this owner frees it after zeroization.
            unsafe {
                core::slice::from_raw_parts_mut(self.blob.pbData, self.blob.cbData as usize)
                    .zeroize();
            }
        }

        // SAFETY: [Category 12 - Double free / invalid free] DPAPI allocated this
        // buffer with LocalAlloc and `DpapiOutput` is its sole owner.
        unsafe {
            LocalFree(self.blob.pbData.cast::<c_void>());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{
        VaultDeviceBinding, VaultDevicePlatform, VaultDeviceProtector, VaultDeviceProtectorError,
        WindowsProtectedDeviceSecret, WindowsVaultDeviceProtector,
    };

    #[test]
    fn protected_secret_rejects_empty_blob() {
        // Given an empty persisted DPAPI blob.
        let empty_blob = [];

        // When the provider accepts persisted ciphertext.
        let result = WindowsProtectedDeviceSecret::from_bytes(&empty_blob);

        // Then it rejects the malformed blob before it can reach DPAPI.
        assert!(result.is_err());
    }

    #[test]
    #[ignore = "requires a Windows user DPAPI context"]
    fn protected_secret_rejects_tampered_blob() {
        // Given a DPAPI-protected secret bound to non-secret device context.
        let binding = VaultDeviceBinding::new(
            "profile-example",
            "device-example",
            VaultDevicePlatform::Desktop,
        );
        let (_protector, protected_secret) =
            WindowsVaultDeviceProtector::generate_for_binding(&binding)
                .expect("DPAPI should protect");
        let mut tampered = protected_secret.clone();
        tampered.bytes[0] ^= 1;

        // When the persisted ciphertext has been modified.
        let protector =
            WindowsVaultDeviceProtector::from_protected_secret_for_binding(tampered, &binding);
        let result = protector.device_secret();

        // Then DPAPI failure is returned without plaintext fallback.
        assert_eq!(result, Err(VaultDeviceProtectorError::OperationFailed));
    }
}
