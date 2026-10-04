use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use base64::Engine as _;
use maho_types::vault::{VaultCiphertextPurpose, VaultKeyVersion, VaultSchemaVersion};
use serde_json::json;

use super::*;

const SENTINEL: &[u8] = b"S3NTINEL-maho-vault-task-8";
const RECORD_AAD: &[u8] = b"vault-item:018f47a6";
const WRAP_AAD: &[u8] = b"vault-profile:default";

fn metadata() -> VaultKdfMetadata {
    VaultKdfMetadata::generate().unwrap()
}

fn vault_key() -> VaultKey {
    VaultKey::generate(VaultKeyVersion::new(1)).unwrap()
}

#[test]
fn aes_gcm_record_round_trip_preserves_plaintext() {
    // Given: the baseline record key, plaintext, purpose, and AAD.
    let key = vault_key();

    // When: a record is encrypted and decrypted through the public envelope API.
    let encrypted = encrypt_record(
        SENTINEL,
        &key,
        VaultCiphertextPurpose::VaultItem,
        RECORD_AAD,
    )
    .unwrap();
    let decrypted = decrypt_record(&encrypted, &key, RECORD_AAD).unwrap();

    // Then: authenticated decryption returns exactly the original plaintext.
    assert_eq!(decrypted.as_slice(), SENTINEL);
}

#[test]
fn aes_gcm_record_rejects_tampered_ciphertext() {
    // Given: a valid baseline ciphertext envelope.
    let key = vault_key();
    let mut encrypted = encrypt_record(
        SENTINEL,
        &key,
        VaultCiphertextPurpose::VaultItem,
        RECORD_AAD,
    )
    .unwrap();

    // When: one ciphertext bit is flipped.
    encrypted.ciphertext[0] ^= 1;

    // Then: no plaintext is returned.
    assert!(decrypt_record(&encrypted, &key, RECORD_AAD).is_err());
}

#[test]
fn aes_gcm_record_rejects_wrong_aad() {
    // Given: a valid baseline ciphertext envelope.
    let key = vault_key();
    let encrypted = encrypt_record(
        SENTINEL,
        &key,
        VaultCiphertextPurpose::VaultItem,
        RECORD_AAD,
    )
    .unwrap();

    // When: decryption supplies different associated data.
    let result = decrypt_record(&encrypted, &key, b"vault-item:other");

    // Then: authentication fails closed.
    assert!(result.is_err());
}

#[test]
fn vault_kdf_metadata_uses_explicit_argon2id_v1_parameters() {
    // Given: newly generated persisted KDF metadata.
    let metadata = metadata();

    // When: its public parameter accessors are inspected.
    let parameters = metadata.parameters();

    // Then: the production Argon2id profile and salt length are explicit.
    assert_eq!(metadata.version(), VaultKdfVersion::V1);
    assert_eq!(parameters.memory_kib, 19_456);
    assert_eq!(parameters.iterations, 2);
    assert_eq!(parameters.parallelism, 1);
    assert_eq!(metadata.salt_len(), VAULT_SALT_LEN);
}

#[test]
fn vault_kdf_metadata_generates_a_unique_salt_per_vault() {
    // Given: two independent Vault metadata generations.
    let first = metadata();
    let second = metadata();

    // When/Then: their persisted random salts differ.
    assert_ne!(first.salt_for_test(), second.salt_for_test());
}

#[test]
fn same_passphrase_with_different_salts_derives_different_user_keys() {
    // Given: one passphrase and two independently salted Vaults.
    let first = metadata();
    let second = metadata();

    // When: user wrapping keys are derived.
    let first_key = derive_user_wrapping_key(SENTINEL, &first).unwrap();
    let second_key = derive_user_wrapping_key(SENTINEL, &second).unwrap();

    // Then: the key bytes differ despite identical passphrases.
    assert_ne!(first_key.bytes_for_test(), second_key.bytes_for_test());
}

#[test]
fn user_recovery_and_device_derivation_are_domain_separated() {
    // Given: identical secret input and one persisted KDF metadata value.
    let metadata = metadata();

    // When: each wrapping domain derives a key.
    let user = derive_user_wrapping_key(SENTINEL, &metadata).unwrap();
    let recovery = derive_recovery_wrapping_key(SENTINEL, &metadata).unwrap();
    let device = derive_device_wrapping_key(SENTINEL, &metadata).unwrap();

    // Then: no two domains produce the same key material.
    assert_ne!(user.bytes_for_test(), recovery.bytes_for_test());
    assert_ne!(user.bytes_for_test(), device.bytes_for_test());
    assert_ne!(recovery.bytes_for_test(), device.bytes_for_test());
}

#[test]
fn wrapping_and_unwrapping_preserves_the_vault_key_version() {
    // Given: a data key and a user wrapping key.
    let metadata = metadata();
    let wrapping_key = derive_user_wrapping_key(SENTINEL, &metadata).unwrap();
    let key = vault_key();

    // When: the Vault key is wrapped and unwrapped.
    let wrapped = wrap_vault_key(&key, &wrapping_key, WRAP_AAD).unwrap();
    let unwrapped = unwrap_vault_key(&wrapped, &wrapping_key, WRAP_AAD).unwrap();

    // Then: both secret bytes and explicit key version survive.
    assert_eq!(unwrapped.version(), key.version());
    assert_eq!(unwrapped.bytes_for_test(), key.bytes_for_test());
}

#[test]
fn wrapped_key_rejects_wrong_salt_and_wrong_domain() {
    // Given: a Vault key wrapped by a user-domain key.
    let original_metadata = metadata();
    let wrong_metadata = metadata();
    let user_key = derive_user_wrapping_key(SENTINEL, &original_metadata).unwrap();
    let wrapped = wrap_vault_key(&vault_key(), &user_key, WRAP_AAD).unwrap();

    // When: keys are derived from the wrong salt or recovery domain.
    let wrong_salt_key = derive_user_wrapping_key(SENTINEL, &wrong_metadata).unwrap();
    let wrong_domain_key = derive_recovery_wrapping_key(SENTINEL, &original_metadata).unwrap();

    // Then: both unwrap attempts fail without key material.
    assert!(unwrap_vault_key(&wrapped, &wrong_salt_key, WRAP_AAD).is_err());
    assert!(unwrap_vault_key(&wrapped, &wrong_domain_key, WRAP_AAD).is_err());
}

#[test]
fn record_rejects_wrong_key_nonce_tag_purpose_and_key_version() {
    // Given: a valid record envelope and unrelated key.
    let key = vault_key();
    let wrong_key = vault_key();
    let encrypted = encrypt_record(
        SENTINEL,
        &key,
        VaultCiphertextPurpose::VaultItem,
        RECORD_AAD,
    )
    .unwrap();

    // When/Then: every authenticated component fails independently.
    assert!(decrypt_record(&encrypted, &wrong_key, RECORD_AAD).is_err());

    let mut wrong_nonce = encrypted.clone();
    wrong_nonce.nonce[0] ^= 1;
    assert!(decrypt_record(&wrong_nonce, &key, RECORD_AAD).is_err());

    let mut wrong_tag = encrypted.clone();
    wrong_tag.tag[0] ^= 1;
    assert!(decrypt_record(&wrong_tag, &key, RECORD_AAD).is_err());

    let mut wrong_purpose = encrypted.clone();
    wrong_purpose.purpose = VaultCiphertextPurpose::TotpSeed;
    assert!(decrypt_record(&wrong_purpose, &key, RECORD_AAD).is_err());

    let mut wrong_version = encrypted;
    wrong_version.key_version = VaultKeyVersion::new(2);
    assert!(matches!(
        decrypt_record(&wrong_version, &key, RECORD_AAD),
        Err(VaultCryptoError::KeyVersionMismatch { .. })
    ));
}

#[test]
fn record_rejects_malformed_lengths_and_unsupported_schema_version() {
    // Given: a valid envelope and its serialized boundary representation.
    let key = vault_key();
    let encrypted = encrypt_record(
        SENTINEL,
        &key,
        VaultCiphertextPurpose::VaultItem,
        RECORD_AAD,
    )
    .unwrap();

    // When/Then: malformed nonce and tag lengths are rejected.
    let mut short_nonce = encrypted.clone();
    short_nonce.nonce.pop();
    assert!(matches!(
        decrypt_record(&short_nonce, &key, RECORD_AAD),
        Err(VaultCryptoError::InvalidEnvelope)
    ));

    let mut short_tag = encrypted.clone();
    short_tag.tag.pop();
    assert!(matches!(
        decrypt_record(&short_tag, &key, RECORD_AAD),
        Err(VaultCryptoError::InvalidEnvelope)
    ));

    let mut serialized = serde_json::to_value(encrypted).unwrap();
    serialized["schemaVersion"] = json!(99);
    assert!(
        serde_json::from_value::<maho_types::vault::VaultCiphertextEnvelope>(serialized).is_err()
    );
}

#[test]
fn crypto_entrypoints_reject_unsupported_in_process_schema_versions() {
    // Given: valid record and wrapped-key envelopes carrying an unsupported in-process schema.
    let key = vault_key();
    let metadata = metadata();
    let wrapping_key = derive_user_wrapping_key(SENTINEL, &metadata).unwrap();
    let mut record = encrypt_record(
        SENTINEL,
        &key,
        VaultCiphertextPurpose::VaultItem,
        RECORD_AAD,
    )
    .unwrap();
    let mut wrapped = wrap_vault_key(&key, &wrapping_key, WRAP_AAD).unwrap();
    record.schema_version = VaultSchemaVersion::Unsupported(99);
    wrapped.schema_version = VaultSchemaVersion::Unsupported(99);

    // When: the envelopes bypass serde and enter crypto directly.
    let record_result = decrypt_record(&record, &key, RECORD_AAD);
    let wrapped_result = unwrap_vault_key(&wrapped, &wrapping_key, WRAP_AAD);

    // Then: both entrypoints reject the unsupported schema before authentication.
    assert_eq!(record_result, Err(VaultCryptoError::InvalidEnvelope));
    assert!(matches!(
        wrapped_result,
        Err(VaultCryptoError::InvalidEnvelope)
    ));
}

#[test]
fn record_crypto_rejects_oversized_inputs_before_processing() {
    // Given: a valid key plus record inputs one byte beyond the supported bounds.
    let key = vault_key();
    let oversized_plaintext = vec![0; MAX_PLAINTEXT_LEN + 1];
    let oversized_aad = vec![0; MAX_AAD_LEN + 1];
    let mut oversized_envelope = encrypt_record(
        SENTINEL,
        &key,
        VaultCiphertextPurpose::VaultItem,
        RECORD_AAD,
    )
    .unwrap();
    oversized_envelope.ciphertext = vec![0; MAX_PLAINTEXT_LEN + 1];

    // When/Then: encrypt and decrypt reject before proportional crypto allocations.
    assert_eq!(
        encrypt_record(
            &oversized_plaintext,
            &key,
            VaultCiphertextPurpose::VaultItem,
            RECORD_AAD,
        ),
        Err(VaultCryptoError::InputTooLarge)
    );
    assert_eq!(
        encrypt_record(
            SENTINEL,
            &key,
            VaultCiphertextPurpose::VaultItem,
            &oversized_aad,
        ),
        Err(VaultCryptoError::InputTooLarge)
    );
    assert_eq!(
        decrypt_record(&oversized_envelope, &key, RECORD_AAD),
        Err(VaultCryptoError::InputTooLarge)
    );
    assert_eq!(
        decrypt_record(&oversized_envelope, &key, &oversized_aad),
        Err(VaultCryptoError::InputTooLarge)
    );
}

#[test]
fn key_wrapping_rejects_oversized_aad_and_malformed_ciphertext() {
    // Given: a valid wrapped key, oversized AAD, and malformed wrapped-key ciphertext.
    let key = vault_key();
    let wrapping_key = derive_user_wrapping_key(SENTINEL, &metadata()).unwrap();
    let oversized_aad = vec![0; MAX_AAD_LEN + 1];
    let mut malformed = wrap_vault_key(&key, &wrapping_key, WRAP_AAD).unwrap();
    malformed.ciphertext.push(0);

    // When/Then: wrap/unwrap reject bounded-input and envelope failures explicitly.
    assert_eq!(
        wrap_vault_key(&key, &wrapping_key, &oversized_aad),
        Err(VaultCryptoError::InputTooLarge)
    );
    assert!(matches!(
        unwrap_vault_key(&malformed, &wrapping_key, WRAP_AAD),
        Err(VaultCryptoError::InvalidEnvelope)
    ));
    assert!(matches!(
        unwrap_vault_key(&malformed, &wrapping_key, &oversized_aad),
        Err(VaultCryptoError::InputTooLarge)
    ));
}

#[test]
fn malformed_kdf_metadata_is_rejected_before_derivation() {
    // Given: serialized KDF metadata with unsupported version and weakened parameters.
    let metadata = serde_json::to_value(metadata()).unwrap();
    let mut unsupported = metadata.clone();
    unsupported["version"] = json!(99);
    let mut weakened = metadata;
    weakened["memoryKib"] = json!(8);

    // When/Then: both boundary values fail closed.
    assert!(serde_json::from_value::<VaultKdfMetadata>(unsupported).is_err());
    assert!(serde_json::from_value::<VaultKdfMetadata>(weakened).is_err());
}

#[test]
fn key_rotation_keeps_old_records_decryptable_by_explicit_version() {
    // Given: a keyring with one encrypted version-one record.
    let mut keyring = VaultKeyring::generate().unwrap();
    let old = keyring
        .encrypt(SENTINEL, VaultCiphertextPurpose::VaultItem, RECORD_AAD)
        .unwrap();

    // When: the keyring rotates and encrypts a second record.
    let new_version = keyring.rotate().unwrap();
    let current = keyring
        .encrypt(b"rotated", VaultCiphertextPurpose::VaultItem, RECORD_AAD)
        .unwrap();

    // Then: envelope metadata selects the right retained key for both records.
    assert_eq!(new_version, VaultKeyVersion::new(2));
    assert_eq!(old.key_version, VaultKeyVersion::new(1));
    assert_eq!(current.key_version, VaultKeyVersion::new(2));
    assert_eq!(
        keyring.decrypt(&old, RECORD_AAD).unwrap().as_slice(),
        SENTINEL
    );
    assert_eq!(
        keyring.decrypt(&current, RECORD_AAD).unwrap().as_slice(),
        b"rotated"
    );
}

#[test]
fn keyring_rejects_an_unavailable_explicit_key_version() {
    // Given: a valid record whose version metadata is changed to an unavailable key.
    let keyring = VaultKeyring::generate().unwrap();
    let mut encrypted = keyring
        .encrypt(SENTINEL, VaultCiphertextPurpose::VaultItem, RECORD_AAD)
        .unwrap();
    encrypted.key_version = VaultKeyVersion::new(99);

    // When: decryption resolves the explicit key version.
    let result = keyring.decrypt(&encrypted, RECORD_AAD);

    // Then: stale/unknown state is rejected rather than falling back to the current key.
    assert!(matches!(
        result,
        Err(VaultCryptoError::UnknownKeyVersion { .. })
    ));
}

#[test]
fn secure_nonce_generation_is_unique_for_repeated_encryptions() {
    // Given: one key and identical plaintext/AAD.
    let key = vault_key();

    // When: the record is encrypted twice.
    let first = encrypt_record(
        SENTINEL,
        &key,
        VaultCiphertextPurpose::VaultItem,
        RECORD_AAD,
    )
    .unwrap();
    let second = encrypt_record(
        SENTINEL,
        &key,
        VaultCiphertextPurpose::VaultItem,
        RECORD_AAD,
    )
    .unwrap();

    // Then: independent 96-bit nonces and ciphertexts are produced.
    assert_ne!(first.nonce, [0; NONCE_LEN]);
    assert_ne!(first.nonce, second.nonce);
    assert_ne!(first.ciphertext, second.ciphertext);
}

#[test]
fn key_debug_and_serialized_metadata_do_not_expose_secrets() {
    // Given: sentinel-bearing user input, derived keys, and wrapped metadata.
    let metadata = metadata();
    let wrapping_key = derive_user_wrapping_key(SENTINEL, &metadata).unwrap();
    let key = vault_key();
    let wrapped = wrap_vault_key(&key, &wrapping_key, WRAP_AAD).unwrap();

    // When: diagnostic and persistence-safe representations are produced.
    let debug = format!("{key:?} {wrapping_key:?} {wrapped:?}");
    let serialized = serde_json::to_string(&(metadata, wrapped)).unwrap();

    // Then: neither input secrets nor raw key bytes are present.
    assert!(debug.contains("[REDACTED]"));
    assert!(!debug.contains("S3NTINEL"));
    assert!(!serialized.contains("S3NTINEL"));
    assert!(!serialized.contains(&base64::prelude::BASE64_STANDARD.encode(key.bytes_for_test())));
    assert!(!serialized
        .contains(&base64::prelude::BASE64_STANDARD.encode(wrapping_key.bytes_for_test())));
}

#[test]
fn vault_key_drop_runs_zeroization_instrumentation() {
    // Given: a Vault key carrying a test-only zeroization probe.
    let zeroized = Arc::new(AtomicBool::new(false));
    let key =
        VaultKey::generate_with_probe(VaultKeyVersion::new(1), Arc::clone(&zeroized)).unwrap();

    // When: the key leaves scope.
    drop(key);

    // Then: ZeroizeOnDrop invoked the probe during destruction.
    assert!(zeroized.load(Ordering::SeqCst));
}
