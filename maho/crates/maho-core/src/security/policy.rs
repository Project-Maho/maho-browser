// Copyright 2026 Maho Browser. All rights reserved.

use maho_types::hardware_security::{
    FallbackPolicyKind, HardwareSecurityCapabilities, HardwareSecurityError, ProtectionLevel,
    UserVerificationKind,
};

/// Policy engine validating requested operations against hardware security capabilities.
pub struct HardwareSecurityPolicyEngine;

impl HardwareSecurityPolicyEngine {
    /// Validates whether the capabilities satisfy the requested fallback policy.
    pub fn evaluate_fallback_policy(
        policy: FallbackPolicyKind,
        capabilities: &HardwareSecurityCapabilities,
        requested_verification: Option<UserVerificationKind>,
    ) -> Result<ProtectionLevel, HardwareSecurityError> {
        match policy {
            FallbackPolicyKind::BackgroundDatabase => {
                // Background database requires background unwrap support and at minimum OS secret store protection
                if !capabilities.supports_background_unwrap {
                    return Err(HardwareSecurityError::PolicyUnsatisfied);
                }
                if capabilities.protection_level < ProtectionLevel::OsSecretStore {
                    return Err(HardwareSecurityError::SecretStoreUnavailable);
                }
                Ok(capabilities.protection_level)
            }
            FallbackPolicyKind::DeviceIdentityPreferred => {
                // Prefers hardware-backed signing, reports actual level obtained without spoofing
                if capabilities.supports_non_exportable_signing {
                    Ok(capabilities.protection_level)
                } else if capabilities.protection_level >= ProtectionLevel::OsSecretStore {
                    Ok(capabilities.protection_level)
                } else {
                    Ok(ProtectionLevel::SoftwareProcess)
                }
            }
            FallbackPolicyKind::HighRiskUserPresence => {
                // Requires interactive presence/biometrics
                if !capabilities.supports_user_presence {
                    return Err(HardwareSecurityError::Unavailable);
                }
                if let Some(req) = requested_verification {
                    match req {
                        UserVerificationKind::BiometricStrong => {
                            if !capabilities.supports_biometric_strong {
                                return Err(HardwareSecurityError::PolicyUnsatisfied);
                            }
                        }
                        UserVerificationKind::Biometric => {
                            if !capabilities.supports_biometric {
                                return Err(HardwareSecurityError::PolicyUnsatisfied);
                            }
                        }
                        UserVerificationKind::OsUserVerification
                        | UserVerificationKind::DeviceCredential
                        | UserVerificationKind::Fido2 => {
                            if !capabilities.user_verification_kinds.contains(&req)
                                && !capabilities.supports_user_presence
                            {
                                return Err(HardwareSecurityError::PolicyUnsatisfied);
                            }
                        }
                        UserVerificationKind::None => {}
                    }
                }
                Ok(capabilities.protection_level)
            }
            FallbackPolicyKind::StrictHardware => {
                // Hardware backing is mandatory (TEE, TPM2.0, Secure Enclave, or StrongBox)
                match capabilities.protection_level {
                    ProtectionLevel::Tee
                    | ProtectionLevel::Tpm20
                    | ProtectionLevel::SecureEnclave
                    | ProtectionLevel::StrongBox => Ok(capabilities.protection_level),
                    _ => Err(HardwareSecurityError::PolicyUnsatisfied),
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use maho_types::hardware_security::*;

    #[test]
    fn test_background_database_requires_os_secret_store() {
        let mut caps = HardwareSecurityCapabilities::default();
        caps.protection_level = ProtectionLevel::SoftwareProcess;
        caps.supports_background_unwrap = true;

        let res = HardwareSecurityPolicyEngine::evaluate_fallback_policy(
            FallbackPolicyKind::BackgroundDatabase,
            &caps,
            None,
        );
        assert_eq!(res, Err(HardwareSecurityError::SecretStoreUnavailable));

        caps.protection_level = ProtectionLevel::OsSecretStore;
        let res = HardwareSecurityPolicyEngine::evaluate_fallback_policy(
            FallbackPolicyKind::BackgroundDatabase,
            &caps,
            None,
        );
        assert_eq!(res, Ok(ProtectionLevel::OsSecretStore));
    }

    #[test]
    fn test_strict_hardware_rejects_software_and_secret_store() {
        let mut caps = HardwareSecurityCapabilities::default();
        caps.protection_level = ProtectionLevel::OsSecretStore;

        let res = HardwareSecurityPolicyEngine::evaluate_fallback_policy(
            FallbackPolicyKind::StrictHardware,
            &caps,
            None,
        );
        assert_eq!(res, Err(HardwareSecurityError::PolicyUnsatisfied));

        caps.protection_level = ProtectionLevel::SecureEnclave;
        let res = HardwareSecurityPolicyEngine::evaluate_fallback_policy(
            FallbackPolicyKind::StrictHardware,
            &caps,
            None,
        );
        assert_eq!(res, Ok(ProtectionLevel::SecureEnclave));
    }

    #[test]
    fn test_high_risk_presence_verification() {
        let mut caps = HardwareSecurityCapabilities::default();
        caps.supports_user_presence = true;
        caps.supports_biometric_strong = false;
        caps.supports_biometric = true;

        let res = HardwareSecurityPolicyEngine::evaluate_fallback_policy(
            FallbackPolicyKind::HighRiskUserPresence,
            &caps,
            Some(UserVerificationKind::BiometricStrong),
        );
        assert_eq!(res, Err(HardwareSecurityError::PolicyUnsatisfied));

        let res = HardwareSecurityPolicyEngine::evaluate_fallback_policy(
            FallbackPolicyKind::HighRiskUserPresence,
            &caps,
            Some(UserVerificationKind::Biometric),
        );
        assert!(res.is_ok());
    }
}
