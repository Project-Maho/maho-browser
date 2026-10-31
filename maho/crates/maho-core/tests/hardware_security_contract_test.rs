// Copyright 2026 Maho Browser. All rights reserved.

use maho_core::security::envelope::{create_protected_envelope, open_protected_envelope};
use maho_core::security::migration::migrate_database_root_key;
use maho_core::security::platform::software::SoftwareHardwareSecurityProvider;
use maho_core::security::platform::{
    linux::LinuxHardwareSecurityProvider, macos::MacOsHardwareSecurityProvider,
    windows::WindowsHardwareSecurityProvider,
};
use maho_core::security::{
    AuthenticatedKeyOperationKind, AuthenticatedKeyOperationRequest,
    AuthenticatedKeyOperationResult, DeviceKeyStore, HardwareOperationId,
    HardwareSecurityCapabilities, HardwareSecurityError, HardwareSecurityProvider, ProtectionLevel,
    UserPresenceAvailability, UserPresencePolicy, UserPresenceRequest,
};
use maho_types::hardware_security::{
    DeviceKeyHandle, DeviceKeyPolicy, DeviceKeyPurpose, ProtectedKeyEnvelopeV3,
    UserVerificationKind,
};

fn assert_platform_provider_fails_closed(provider: &dyn HardwareSecurityProvider) {
    let capabilities = provider.capabilities();
    assert_eq!(
        capabilities.protection_level,
        ProtectionLevel::OsSecretStore
    );
    assert!(!capabilities.supports_non_exportable_signing);
    assert!(!capabilities.supports_hardware_wrapping);
    assert!(!capabilities.supports_user_presence);
    assert!(!capabilities.supports_biometric);
    assert!(!capabilities.supports_biometric_strong);
    assert!(!capabilities.supports_enrollment_bound_keys);
    assert!(!capabilities.supports_attestation);
    assert!(capabilities.user_verification_kinds.is_empty());

    let authenticator = provider
        .authenticator()
        .expect("platform authenticator object exists");
    let policy = UserPresencePolicy {
        required_verification: UserVerificationKind::Biometric,
        allow_device_credential_fallback: true,
        timeout_ms: Some(1_000),
    };
    assert_eq!(
        authenticator.availability(&policy),
        Ok(UserPresenceAvailability::Unsupported)
    );

    let request = UserPresenceRequest {
        operation_id: HardwareOperationId(7),
        prompt_reason: "verify fail-closed behavior".into(),
        prompt_reason_id: None,
        purpose: DeviceKeyPurpose::HighRiskPresence,
        required_verification: UserVerificationKind::Biometric,
        transaction_binding: None,
    };
    let result = authenticator.begin_authentication(
        request,
        Box::new(|_| panic!("unsupported authenticator must not invoke a success completion")),
    );
    assert_eq!(result, Err(HardwareSecurityError::UnsupportedOperation));

    let key = provider
        .key_store()
        .get_or_create_key(
            "fail_closed_signing",
            &DeviceKeyPolicy::installation_identity(),
        )
        .expect("OS secret-store key creation remains supported");
    assert_eq!(
        provider.key_store().sign_challenge(&key, b"challenge"),
        Err(HardwareSecurityError::UnsupportedOperation)
    );

    let operation = AuthenticatedKeyOperationRequest {
        operation_id: HardwareOperationId(8),
        operation_kind: AuthenticatedKeyOperationKind::Sign,
        purpose: DeviceKeyPurpose::InstallationIdentity,
        key_handle: key,
        prompt_reason: "verify operation binding".into(),
        prompt_reason_id: None,
        aad: Vec::new(),
        input_payload: b"challenge".to_vec(),
    };
    let result = provider.begin_authenticated_key_operation(
        operation,
        Box::new(|_| panic!("unsupported authenticated operation must not invoke completion")),
    );
    assert_eq!(result, Err(HardwareSecurityError::UnsupportedOperation));
}

#[test]
fn platform_providers_never_synthesize_hardware_or_biometric_success() {
    assert_platform_provider_fails_closed(&MacOsHardwareSecurityProvider::new());
    assert_platform_provider_fails_closed(&WindowsHardwareSecurityProvider::new());
    assert_platform_provider_fails_closed(&LinuxHardwareSecurityProvider::new());
}

#[test]
fn test_purpose_domain_separation() {
    let provider = SoftwareHardwareSecurityProvider::new();
    let store = provider.key_store();

    let db_policy = DeviceKeyPolicy::database_root();
    let vault_policy = DeviceKeyPolicy::vault_device_wrap();
    let id_policy = DeviceKeyPolicy::installation_identity();
    let presence_policy = DeviceKeyPolicy::high_risk_presence();

    let db_handle = store.get_or_create_key("test_db", &db_policy).unwrap();
    let vault_handle = store
        .get_or_create_key("test_vault", &vault_policy)
        .unwrap();
    let id_handle = store.get_or_create_key("test_id", &id_policy).unwrap();
    let pres_handle = store
        .get_or_create_key("test_pres", &presence_policy)
        .unwrap();

    assert_eq!(db_handle.purpose, DeviceKeyPurpose::DatabaseRoot);
    assert_eq!(vault_handle.purpose, DeviceKeyPurpose::VaultDeviceWrap);
    assert_eq!(id_handle.purpose, DeviceKeyPurpose::InstallationIdentity);
    assert_eq!(pres_handle.purpose, DeviceKeyPurpose::HighRiskPresence);
}

#[test]
fn test_envelope_tamper_rejection() {
    let provider = SoftwareHardwareSecurityProvider::new();
    let store = provider.key_store();
    let policy = DeviceKeyPolicy::database_root();
    let handle = store.get_or_create_key("test_key", &policy).unwrap();

    let secret = b"sqlcipher_32_byte_secret_pass___";
    let profile_id = "profile_alpha";
    let device_id = "device_beta";

    let mut envelope =
        create_protected_envelope(store, &handle, secret, profile_id, device_id, &policy)
            .expect("envelope creation should succeed");

    // 1. Valid unwrap works
    let opened = open_protected_envelope(store, &envelope, profile_id, device_id).unwrap();
    assert_eq!(opened.as_slice(), secret);

    // 2. Tampered profile ID fails
    let err = open_protected_envelope(store, &envelope, "profile_other", device_id);
    assert_eq!(err, Err(HardwareSecurityError::CorruptEnvelope));

    // 3. Tampered device ID fails
    let err = open_protected_envelope(store, &envelope, profile_id, "device_other");
    assert_eq!(err, Err(HardwareSecurityError::CorruptEnvelope));

    // 4. Tampered ciphertext fails
    let orig_byte = envelope.wrapped_key_or_ciphertext[0];
    envelope.wrapped_key_or_ciphertext[0] ^= 0xFF;
    let err = open_protected_envelope(store, &envelope, profile_id, device_id);
    assert_eq!(err, Err(HardwareSecurityError::CorruptEnvelope));
    envelope.wrapped_key_or_ciphertext[0] = orig_byte;

    // 5. Tampered format version fails
    envelope.format_version = 99;
    let err = open_protected_envelope(store, &envelope, profile_id, device_id);
    assert_eq!(err, Err(HardwareSecurityError::VersionMismatch));
}

#[test]
fn test_stage_a_migration_success() {
    let custom_store = maho_core::security::platform::software::SoftwareKeyStore::new();

    // Wrap with software store adapted to OS Secret Store
    struct MockOsStore(maho_core::security::platform::software::SoftwareKeyStore);
    impl DeviceKeyStore for MockOsStore {
        fn capabilities(&self) -> HardwareSecurityCapabilities {
            let mut caps = self.0.capabilities();
            caps.protection_level = ProtectionLevel::OsSecretStore;
            caps.supports_background_unwrap = true;
            caps
        }
        fn get_or_create_key(
            &self,
            label: &str,
            policy: &DeviceKeyPolicy,
        ) -> Result<DeviceKeyHandle, HardwareSecurityError> {
            self.0.get_or_create_key(label, policy)
        }
        fn wrap_key_material(
            &self,
            key: &DeviceKeyHandle,
            plaintext: &[u8],
            aad: &[u8],
        ) -> Result<maho_types::hardware_security::PlatformWrappedKey, HardwareSecurityError>
        {
            self.0.wrap_key_material(key, plaintext, aad)
        }
        fn unwrap_key_material(
            &self,
            key: &DeviceKeyHandle,
            wrapped: &maho_types::hardware_security::PlatformWrappedKey,
            aad: &[u8],
        ) -> Result<zeroize::Zeroizing<Vec<u8>>, HardwareSecurityError> {
            self.0.unwrap_key_material(key, wrapped, aad)
        }
        fn sign_challenge(
            &self,
            key: &DeviceKeyHandle,
            challenge: &[u8],
        ) -> Result<maho_types::hardware_security::DeviceSignature, HardwareSecurityError> {
            self.0.sign_challenge(key, challenge)
        }
        fn delete_key(&self, key: &DeviceKeyHandle) -> Result<(), HardwareSecurityError> {
            self.0.delete_key(key)
        }
    }

    let os_store = MockOsStore(custom_store);
    let legacy_db_key = b"legacy_hex_or_raw_key_material_";
    let profile_id = "user_default";
    let device_id = "machine_uuid_1";

    let envelope =
        migrate_database_root_key(&os_store, legacy_db_key, profile_id, device_id, |key| {
            key == legacy_db_key
        })
        .expect("migration should succeed and verify DB key");

    assert_eq!(envelope.purpose, DeviceKeyPurpose::DatabaseRoot);
    assert_eq!(
        envelope.format_version,
        ProtectedKeyEnvelopeV3::FORMAT_VERSION
    );

    // Verify reopening
    let unwrapped = open_protected_envelope(&os_store, &envelope, profile_id, device_id).unwrap();
    assert_eq!(unwrapped.as_slice(), legacy_db_key);
}

#[test]
fn test_stage_a_migration_non_destructive_on_verifier_failure() {
    let custom_store = maho_core::security::platform::software::SoftwareKeyStore::new();
    struct MockOsStore(maho_core::security::platform::software::SoftwareKeyStore);
    impl DeviceKeyStore for MockOsStore {
        fn capabilities(&self) -> HardwareSecurityCapabilities {
            let mut caps = self.0.capabilities();
            caps.protection_level = ProtectionLevel::OsSecretStore;
            caps.supports_background_unwrap = true;
            caps
        }
        fn get_or_create_key(
            &self,
            label: &str,
            policy: &DeviceKeyPolicy,
        ) -> Result<DeviceKeyHandle, HardwareSecurityError> {
            self.0.get_or_create_key(label, policy)
        }
        fn wrap_key_material(
            &self,
            key: &DeviceKeyHandle,
            plaintext: &[u8],
            aad: &[u8],
        ) -> Result<maho_types::hardware_security::PlatformWrappedKey, HardwareSecurityError>
        {
            self.0.wrap_key_material(key, plaintext, aad)
        }
        fn unwrap_key_material(
            &self,
            key: &DeviceKeyHandle,
            wrapped: &maho_types::hardware_security::PlatformWrappedKey,
            aad: &[u8],
        ) -> Result<zeroize::Zeroizing<Vec<u8>>, HardwareSecurityError> {
            self.0.unwrap_key_material(key, wrapped, aad)
        }
        fn sign_challenge(
            &self,
            key: &DeviceKeyHandle,
            challenge: &[u8],
        ) -> Result<maho_types::hardware_security::DeviceSignature, HardwareSecurityError> {
            self.0.sign_challenge(key, challenge)
        }
        fn delete_key(&self, key: &DeviceKeyHandle) -> Result<(), HardwareSecurityError> {
            self.0.delete_key(key)
        }
    }

    let os_store = MockOsStore(custom_store);
    let legacy_db_key = b"legacy_invalid_key";

    // Fails on step 1 because verifier rejects it
    let result = migrate_database_root_key(&os_store, legacy_db_key, "prof", "dev", |_| false);
    assert_eq!(result, Err(HardwareSecurityError::AccessDenied));
}

#[test]
fn test_authenticated_key_operations() {
    let provider = SoftwareHardwareSecurityProvider::new();
    let store = provider.key_store();
    let policy = DeviceKeyPolicy::installation_identity();
    let handle = store.get_or_create_key("agent_auth_key", &policy).unwrap();

    let (tx, rx) = std::sync::mpsc::channel();

    let request = AuthenticatedKeyOperationRequest {
        operation_id: HardwareOperationId(101),
        operation_kind: AuthenticatedKeyOperationKind::Sign,
        purpose: DeviceKeyPurpose::InstallationIdentity,
        key_handle: handle,
        prompt_reason: "Authenticate high risk action".into(),
        prompt_reason_id: Some("reason_high_risk".into()),
        aad: vec![],
        input_payload: b"challenge_payload_123".to_vec(),
    };

    let _op_id = provider
        .begin_authenticated_key_operation(
            request,
            Box::new(move |res| {
                tx.send(res).unwrap();
            }),
        )
        .expect("should begin operation");

    let result = rx
        .recv()
        .expect("should complete operation")
        .expect("should succeed");
    match result {
        AuthenticatedKeyOperationResult::Signed(sig) => {
            assert!(!sig.signature.is_empty());
        }
        _ => panic!("expected Signed result"),
    }
}
