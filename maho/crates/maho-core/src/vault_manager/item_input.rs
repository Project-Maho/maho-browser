//! Public per-kind input value objects for privileged Vault item creation and
//! update. Secrets are carried as owned `Zeroizing` primitives, never as a
//! serializable plaintext DTO in `maho-types`; the crate-private record enum is
//! built from these at the trust boundary.

use maho_types::vault::VaultItemPublicMetadata;
use zeroize::Zeroizing;

use super::credential_form::VaultCredentialFormDetails;

pub struct VaultLoginInput {
    pub metadata: VaultItemPublicMetadata,
    pub username: String,
    pub password: Zeroizing<String>,
    /// Browser-form detail persisted inside the encrypted record so a Chromium
    /// `PasswordForm` round-trips losslessly. `None` means "caller supplied no
    /// form detail" (settings-originated writes, legacy callers).
    pub form_details: Option<VaultCredentialFormDetails>,
}

pub struct VaultLoginUpdate {
    /// Absent preserves notes; an empty value clears them.
    pub notes: Option<Zeroizing<String>>,
    pub metadata: VaultItemPublicMetadata,
    pub username: String,
    pub password: Option<Zeroizing<String>>,
    /// `None` preserves whatever form detail the stored record already carries;
    /// `Some` replaces it wholesale.
    pub form_details: Option<VaultCredentialFormDetails>,
}

pub struct VaultTotpInput {
    pub metadata: VaultItemPublicMetadata,
    pub seed: Zeroizing<Vec<u8>>,
}

pub struct VaultPasskeyInput {
    pub metadata: VaultItemPublicMetadata,
    pub private_key: Zeroizing<Vec<u8>>,
}

pub struct VaultSecureItemInput {
    pub metadata: VaultItemPublicMetadata,
    pub bytes: Zeroizing<Vec<u8>>,
    pub notes: Option<Zeroizing<String>>,
}
