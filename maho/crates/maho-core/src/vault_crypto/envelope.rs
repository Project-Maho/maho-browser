use aes_gcm::aead::{AeadInPlace, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce, Tag};
use maho_types::vault::{
    VaultCipherAlgorithm, VaultCiphertextEnvelope, VaultCiphertextPurpose, VaultSchemaVersion,
};
use zeroize::Zeroizing;

use super::{
    validation, VaultCryptoError, VaultKey, WrappedVaultKeyEnvelope, WrappingDomain, WrappingKey,
    KEY_LEN, NONCE_LEN, TAG_LEN,
};

pub fn encrypt_record(
    plaintext: &[u8],
    key: &VaultKey,
    purpose: VaultCiphertextPurpose,
    aad: &[u8],
) -> Result<VaultCiphertextEnvelope, VaultCryptoError> {
    validation::record_input(plaintext, aad)?;
    let mut buffer = Zeroizing::new(plaintext.to_vec());
    let nonce = random_nonce()?;
    let metadata_aad = record_aad(purpose, key.version, aad);
    let tag = encrypt_detached(&key.bytes, &nonce, &metadata_aad, buffer.as_mut())?;
    Ok(VaultCiphertextEnvelope {
        schema_version: VaultSchemaVersion::V1,
        algorithm: VaultCipherAlgorithm::Aes256Gcm,
        purpose,
        key_version: key.version,
        nonce: nonce.to_vec(),
        ciphertext: buffer.to_vec(),
        tag: tag.to_vec(),
    })
}

pub fn decrypt_record(
    envelope: &VaultCiphertextEnvelope,
    key: &VaultKey,
    aad: &[u8],
) -> Result<Zeroizing<Vec<u8>>, VaultCryptoError> {
    validation::record_envelope(envelope, aad)?;
    if envelope.key_version != key.version {
        return Err(VaultCryptoError::KeyVersionMismatch {
            envelope: envelope.key_version.value(),
            key: key.version.value(),
        });
    }
    match envelope.algorithm {
        VaultCipherAlgorithm::Aes256Gcm => {}
        _ => return Err(VaultCryptoError::InvalidEnvelope),
    }
    let metadata_aad = record_aad(envelope.purpose, envelope.key_version, aad);
    let mut plaintext = Zeroizing::new(envelope.ciphertext.clone());
    decrypt_detached(
        &key.bytes,
        &envelope.nonce,
        &metadata_aad,
        plaintext.as_mut(),
        &envelope.tag,
    )?;
    Ok(plaintext)
}

pub fn wrap_vault_key(
    key: &VaultKey,
    wrapping_key: &WrappingKey,
    aad: &[u8],
) -> Result<WrappedVaultKeyEnvelope, VaultCryptoError> {
    validation::wrapping_aad(aad)?;
    let mut buffer = Zeroizing::new(key.bytes.to_vec());
    let nonce = random_nonce()?;
    let metadata_aad = wrapping_aad(wrapping_key.domain, key.version, aad);
    let tag = encrypt_detached(&wrapping_key.bytes, &nonce, &metadata_aad, buffer.as_mut())?;
    Ok(WrappedVaultKeyEnvelope {
        schema_version: VaultSchemaVersion::V1,
        wrapping_domain: wrapping_key.domain,
        key_version: key.version,
        nonce: nonce.to_vec(),
        ciphertext: buffer.to_vec(),
        tag: tag.to_vec(),
    })
}

pub fn unwrap_vault_key(
    envelope: &WrappedVaultKeyEnvelope,
    wrapping_key: &WrappingKey,
    aad: &[u8],
) -> Result<VaultKey, VaultCryptoError> {
    validation::wrapped_envelope(envelope, aad)?;
    if envelope.wrapping_domain != wrapping_key.domain {
        return Err(VaultCryptoError::WrappingDomainMismatch);
    }
    let metadata_aad = wrapping_aad(envelope.wrapping_domain, envelope.key_version, aad);
    let mut bytes = Zeroizing::new(envelope.ciphertext.clone());
    decrypt_detached(
        &wrapping_key.bytes,
        &envelope.nonce,
        &metadata_aad,
        bytes.as_mut(),
        &envelope.tag,
    )?;
    let mut key_bytes = Zeroizing::new([0; KEY_LEN]);
    key_bytes.copy_from_slice(&bytes);
    Ok(VaultKey::from_bytes(*key_bytes, envelope.key_version))
}

fn random_nonce() -> Result<[u8; NONCE_LEN], VaultCryptoError> {
    let mut nonce = [0; NONCE_LEN];
    rand::RngCore::try_fill_bytes(&mut rand::rngs::OsRng, &mut nonce)
        .map_err(|_| VaultCryptoError::Randomness)?;
    Ok(nonce)
}

fn encrypt_detached(
    key: &[u8; KEY_LEN],
    nonce: &[u8; NONCE_LEN],
    aad: &[u8],
    buffer: &mut [u8],
) -> Result<[u8; TAG_LEN], VaultCryptoError> {
    let cipher = Aes256Gcm::new_from_slice(key).map_err(|_| VaultCryptoError::Encryption)?;
    let tag = cipher
        .encrypt_in_place_detached(Nonce::from_slice(nonce), aad, buffer)
        .map_err(|_| VaultCryptoError::Encryption)?;
    Ok(tag.into())
}

fn decrypt_detached(
    key: &[u8; KEY_LEN],
    nonce: &[u8],
    aad: &[u8],
    buffer: &mut [u8],
    tag: &[u8],
) -> Result<(), VaultCryptoError> {
    let cipher = Aes256Gcm::new_from_slice(key).map_err(|_| VaultCryptoError::InvalidEnvelope)?;
    cipher
        .decrypt_in_place_detached(Nonce::from_slice(nonce), aad, buffer, Tag::from_slice(tag))
        .map_err(|_| VaultCryptoError::AuthenticationFailed)
}

fn record_aad(
    purpose: VaultCiphertextPurpose,
    key_version: maho_types::vault::VaultKeyVersion,
    aad: &[u8],
) -> Vec<u8> {
    let purpose_byte = match purpose {
        VaultCiphertextPurpose::VaultItem => 1,
        VaultCiphertextPurpose::TotpSeed => 2,
        VaultCiphertextPurpose::PasskeyPrivateKey => 3,
        _ => 255,
    };
    contextual_aad(
        b"maho-vault-record-v1",
        purpose_byte,
        key_version.value(),
        aad,
    )
}

fn wrapping_aad(
    domain: WrappingDomain,
    key_version: maho_types::vault::VaultKeyVersion,
    aad: &[u8],
) -> Vec<u8> {
    let domain_byte = match domain {
        WrappingDomain::User => 1,
        WrappingDomain::Recovery => 2,
        WrappingDomain::Device => 3,
        WrappingDomain::Account => 4,
    };
    contextual_aad(
        b"maho-vault-key-wrap-v1",
        domain_byte,
        key_version.value(),
        aad,
    )
}

fn contextual_aad(prefix: &[u8], discriminator: u8, version: u32, aad: &[u8]) -> Vec<u8> {
    let mut contextual = Vec::with_capacity(prefix.len() + 1 + 4 + 8 + aad.len());
    contextual.extend_from_slice(prefix);
    contextual.push(discriminator);
    contextual.extend_from_slice(&version.to_be_bytes());
    contextual.extend_from_slice(&u64::try_from(aad.len()).unwrap_or(u64::MAX).to_be_bytes());
    contextual.extend_from_slice(aad);
    contextual
}
