// Copyright 2026 Maho Browser. All rights reserved.

use serde::{Deserialize, Serialize};
use std::fmt;

/// Identifies the underlying hardware/platform security provider backend.
#[derive(Clone, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HardwareProviderKind {
    MacOsKeychain,
    MacOsSecureEnclave,
    WindowsDpapi,
    WindowsDpapiNg,
    WindowsTpmCng,
    LinuxSecretService,
    LinuxTpm2Tss,
    LinuxPkcs11,
    IosKeychain,
    IosSecureEnclave,
    AndroidKeystore,
    AndroidStrongBox,
    SoftwareProcess,
    Mock,
    Custom(String),
}

/// The actual level of hardware or OS protection obtained for a secret or key.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ProtectionLevel {
    Unprotected,
    SoftwareProcess,
    OsSecretStore,
    Tee,
    Tpm20,
    SecureEnclave,
    StrongBox,
}

/// The kind of interactive user verification supported or performed.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum UserVerificationKind {
    None,
    OsUserVerification,
    Biometric,
    BiometricStrong,
    DeviceCredential,
    Fido2,
}

/// Capabilities reported by a hardware security provider or device key store.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HardwareSecurityCapabilities {
    pub provider: HardwareProviderKind,
    pub protection_level: ProtectionLevel,
    pub supports_background_unwrap: bool,
    pub supports_non_exportable_signing: bool,
    pub supports_hardware_wrapping: bool,
    pub supports_user_presence: bool,
    pub supports_biometric: bool,
    pub supports_biometric_strong: bool,
    pub supports_enrollment_bound_keys: bool,
    pub supports_attestation: bool,
    pub user_verification_kinds: Vec<UserVerificationKind>,
}

impl Default for HardwareSecurityCapabilities {
    fn default() -> Self {
        Self {
            provider: HardwareProviderKind::SoftwareProcess,
            protection_level: ProtectionLevel::SoftwareProcess,
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
}

/// Purpose domain for key separation across the browser.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DeviceKeyPurpose {
    DatabaseRoot,
    VaultDeviceWrap,
    InstallationIdentity,
    HighRiskPresence,
    RelaySession,
    ByokSecret,
    Recovery,
}

impl DeviceKeyPurpose {
    pub const fn as_str(&self) -> &'static str {
        match self {
            Self::DatabaseRoot => "database_root",
            Self::VaultDeviceWrap => "vault_device_wrap",
            Self::InstallationIdentity => "installation_identity",
            Self::HighRiskPresence => "high_risk_presence",
            Self::RelaySession => "relay_session",
            Self::ByokSecret => "byok_secret",
            Self::Recovery => "recovery",
        }
    }
}

impl fmt::Display for DeviceKeyPurpose {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.as_str())
    }
}

/// Cryptographic algorithm used by a device key.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DeviceKeyAlgorithm {
    Aes256Gcm,
    P256Ecdsa,
}

/// Hardware requirement level for key generation/usage policy.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HardwareRequirement {
    None,
    OsSecretStorePreferred,
    HardwarePreferred,
    HardwareRequired,
    StrongBoxPreferred,
    StrongBoxRequired,
}

/// User authentication requirement for key generation/usage policy.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum UserAuthRequirement {
    None,
    BiometricAny,
    BiometricCurrentSet,
    DeviceCredentialOrBiometric,
    InteractivePrompt,
}

/// Policy describing requirements for a device key.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceKeyPolicy {
    pub purpose: DeviceKeyPurpose,
    pub algorithm: DeviceKeyAlgorithm,
    pub hardware: HardwareRequirement,
    pub user_auth: UserAuthRequirement,
    pub invalidate_on_biometric_change: bool,
    pub background_use_allowed: bool,
}

impl DeviceKeyPolicy {
    pub const fn database_root() -> Self {
        Self {
            purpose: DeviceKeyPurpose::DatabaseRoot,
            algorithm: DeviceKeyAlgorithm::Aes256Gcm,
            hardware: HardwareRequirement::OsSecretStorePreferred,
            user_auth: UserAuthRequirement::None,
            invalidate_on_biometric_change: false,
            background_use_allowed: true,
        }
    }

    pub const fn vault_device_wrap() -> Self {
        Self {
            purpose: DeviceKeyPurpose::VaultDeviceWrap,
            algorithm: DeviceKeyAlgorithm::Aes256Gcm,
            hardware: HardwareRequirement::HardwarePreferred,
            user_auth: UserAuthRequirement::None,
            invalidate_on_biometric_change: false,
            background_use_allowed: true,
        }
    }

    pub const fn installation_identity() -> Self {
        Self {
            purpose: DeviceKeyPurpose::InstallationIdentity,
            algorithm: DeviceKeyAlgorithm::P256Ecdsa,
            hardware: HardwareRequirement::HardwarePreferred,
            user_auth: UserAuthRequirement::None,
            invalidate_on_biometric_change: false,
            background_use_allowed: true,
        }
    }

    pub const fn high_risk_presence() -> Self {
        Self {
            purpose: DeviceKeyPurpose::HighRiskPresence,
            algorithm: DeviceKeyAlgorithm::P256Ecdsa,
            hardware: HardwareRequirement::HardwarePreferred,
            user_auth: UserAuthRequirement::BiometricCurrentSet,
            invalidate_on_biometric_change: true,
            background_use_allowed: false,
        }
    }

    pub const fn byok_secret() -> Self {
        Self {
            purpose: DeviceKeyPurpose::ByokSecret,
            algorithm: DeviceKeyAlgorithm::Aes256Gcm,
            hardware: HardwareRequirement::OsSecretStorePreferred,
            user_auth: UserAuthRequirement::None,
            invalidate_on_biometric_change: false,
            background_use_allowed: true,
        }
    }
}

/// Logical handle to a platform-managed device key.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceKeyHandle {
    pub provider: HardwareProviderKind,
    pub opaque_id: String,
    pub purpose: DeviceKeyPurpose,
    pub algorithm: DeviceKeyAlgorithm,
    pub protection_level: ProtectionLevel,
}

/// Output of a platform wrapping operation.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PlatformWrappedKey {
    pub ciphertext: Vec<u8>,
    pub tag: Option<Vec<u8>>,
    pub iv_or_nonce: Option<Vec<u8>>,
    pub platform_metadata: Option<Vec<u8>>,
}

/// Output of a device signing operation.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceSignature {
    pub algorithm: DeviceKeyAlgorithm,
    pub signature: Vec<u8>,
    pub public_key: Option<Vec<u8>>,
}

/// Stable cross-platform hardware security errors.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HardwareSecurityError {
    Unavailable,
    HardwareUnavailable,
    SecretStoreUnavailable,
    NotEnrolled,
    SecureLockNotConfigured,
    AuthenticationRequired,
    AuthenticationFailed,
    UserCanceled,
    TemporaryLockout,
    PermanentLockout,
    AccessDenied,
    KeyNotFound,
    KeyInvalidated,
    PolicyUnsatisfied,
    UnsupportedAlgorithm,
    UnsupportedOperation,
    CorruptEnvelope,
    VersionMismatch,
    Timeout,
    BackendFailure(String),
}

impl fmt::Display for HardwareSecurityError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Unavailable => write!(f, "hardware security provider is unavailable"),
            Self::HardwareUnavailable => write!(f, "required security hardware is unavailable"),
            Self::SecretStoreUnavailable => write!(f, "OS secret store is unavailable"),
            Self::NotEnrolled => write!(f, "biometric verification is not enrolled"),
            Self::SecureLockNotConfigured => write!(f, "device lock / passcode is not configured"),
            Self::AuthenticationRequired => {
                write!(f, "user authentication is required for this operation")
            }
            Self::AuthenticationFailed => write!(f, "user authentication failed"),
            Self::UserCanceled => write!(f, "user canceled the authentication prompt"),
            Self::TemporaryLockout => write!(f, "biometric authentication is temporarily locked"),
            Self::PermanentLockout => write!(f, "biometric authentication is permanently locked"),
            Self::AccessDenied => write!(f, "access to hardware key was denied"),
            Self::KeyNotFound => write!(f, "requested device key was not found"),
            Self::KeyInvalidated => write!(
                f,
                "device key has been invalidated due to enrollment changes"
            ),
            Self::PolicyUnsatisfied => write!(
                f,
                "hardware security policy requirements could not be satisfied"
            ),
            Self::UnsupportedAlgorithm => {
                write!(f, "requested algorithm is unsupported by this provider")
            }
            Self::UnsupportedOperation => write!(
                f,
                "requested operation is unsupported by this key or provider"
            ),
            Self::CorruptEnvelope => write!(f, "protected key envelope is corrupted"),
            Self::VersionMismatch => write!(f, "unsupported envelope or ABI version"),
            Self::Timeout => write!(f, "hardware security operation timed out"),
            Self::BackendFailure(msg) => write!(f, "backend failure: {msg}"),
        }
    }
}

impl std::error::Error for HardwareSecurityError {}

/// Unique identifier for an asynchronous hardware security operation.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct HardwareOperationId(pub u64);

/// Policy specifying requirements for interactive user presence checks.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UserPresencePolicy {
    pub required_verification: UserVerificationKind,
    pub allow_device_credential_fallback: bool,
    pub timeout_ms: Option<u64>,
}

/// Availability of interactive user presence / biometric verification.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum UserPresenceAvailability {
    Available,
    NotEnrolled,
    NotConfigured,
    TemporarilyLocked,
    PermanentlyLocked,
    Unsupported,
}

/// Request for interactive user presence verification.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UserPresenceRequest {
    pub operation_id: HardwareOperationId,
    pub prompt_reason: String,
    pub prompt_reason_id: Option<String>,
    pub purpose: DeviceKeyPurpose,
    pub required_verification: UserVerificationKind,
    pub transaction_binding: Option<Vec<u8>>,
}

/// Result of interactive user presence verification.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UserPresenceResult {
    pub verified_factor: UserVerificationKind,
    pub authorization_token: Option<Vec<u8>>,
}

/// Kind of authenticated cryptographic operation bound to user presence.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AuthenticatedKeyOperationKind {
    Wrap,
    Unwrap,
    Sign,
}

/// Request for an operation combining interactive presence with cryptographic key use.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AuthenticatedKeyOperationRequest {
    pub operation_id: HardwareOperationId,
    pub operation_kind: AuthenticatedKeyOperationKind,
    pub purpose: DeviceKeyPurpose,
    pub key_handle: DeviceKeyHandle,
    pub prompt_reason: String,
    pub prompt_reason_id: Option<String>,
    pub aad: Vec<u8>,
    pub input_payload: Vec<u8>,
}

/// Result of an authenticated cryptographic key operation.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum AuthenticatedKeyOperationResult {
    Wrapped(PlatformWrappedKey),
    Unwrapped(Vec<u8>),
    Signed(DeviceSignature),
}

/// Versioned Maho protected key envelope (V3).
#[derive(Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ProtectedKeyEnvelopeV3 {
    pub format_version: u32,
    pub purpose: DeviceKeyPurpose,
    pub provider_kind: HardwareProviderKind,
    pub protection_level: ProtectionLevel,
    pub key_algorithm: DeviceKeyAlgorithm,
    pub key_id: String,
    pub policy_digest: Vec<u8>,
    pub aad_schema_version: u32,
    pub wrapped_key_or_ciphertext: Vec<u8>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub iv: Option<Vec<u8>>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tag: Option<Vec<u8>>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub platform_metadata: Option<Vec<u8>>,
}

impl fmt::Debug for ProtectedKeyEnvelopeV3 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ProtectedKeyEnvelopeV3")
            .field("format_version", &self.format_version)
            .field("purpose", &self.purpose)
            .field("provider_kind", &self.provider_kind)
            .field("protection_level", &self.protection_level)
            .field("key_algorithm", &self.key_algorithm)
            .field("key_id", &self.key_id)
            .field("policy_digest_len", &self.policy_digest.len())
            .field("aad_schema_version", &self.aad_schema_version)
            .field("wrapped_len", &self.wrapped_key_or_ciphertext.len())
            .finish_non_exhaustive()
    }
}

impl ProtectedKeyEnvelopeV3 {
    pub const FORMAT_VERSION: u32 = 3;
    pub const CURRENT_AAD_SCHEMA_VERSION: u32 = 1;
}

/// Named fallback policies from Proposal N / Part IV.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum FallbackPolicyKind {
    BackgroundDatabase,
    DeviceIdentityPreferred,
    HighRiskUserPresence,
    StrictHardware,
}
