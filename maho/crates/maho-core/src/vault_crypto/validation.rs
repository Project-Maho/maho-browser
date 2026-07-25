use maho_types::vault::{
    VaultCiphertextEnvelope, VaultSchemaVersion, MAX_VAULT_ENVELOPE_CIPHERTEXT_LEN,
    MAX_VAULT_ENVELOPE_NONCE_LEN, MAX_VAULT_ENVELOPE_TAG_LEN,
};

use super::{
    VaultCryptoError, WrappedVaultKeyEnvelope, KEY_LEN, MAX_AAD_LEN, MAX_PLAINTEXT_LEN,
    MAX_WRAPPED_KEY_CIPHERTEXT_LEN, NONCE_LEN, TAG_LEN,
};

pub(super) fn record_input(plaintext: &[u8], aad: &[u8]) -> Result<(), VaultCryptoError> {
    if plaintext.len() > MAX_PLAINTEXT_LEN {
        return Err(VaultCryptoError::InputTooLarge);
    }
    aad_len(aad)
}

pub(super) fn record_envelope(
    envelope: &VaultCiphertextEnvelope,
    aad: &[u8],
) -> Result<(), VaultCryptoError> {
    schema(envelope.schema_version)?;
    aad_len(aad)?;
    if envelope.nonce.len() > MAX_VAULT_ENVELOPE_NONCE_LEN
        || envelope.ciphertext.len() > MAX_VAULT_ENVELOPE_CIPHERTEXT_LEN
        || envelope.tag.len() > MAX_VAULT_ENVELOPE_TAG_LEN
    {
        return Err(VaultCryptoError::InputTooLarge);
    }
    exact_nonce_and_tag(&envelope.nonce, &envelope.tag)
}

pub(super) fn wrapping_aad(aad: &[u8]) -> Result<(), VaultCryptoError> {
    aad_len(aad)
}

pub(super) fn wrapped_envelope(
    envelope: &WrappedVaultKeyEnvelope,
    aad: &[u8],
) -> Result<(), VaultCryptoError> {
    schema(envelope.schema_version)?;
    aad_len(aad)?;
    if envelope.nonce.len() > MAX_VAULT_ENVELOPE_NONCE_LEN
        || envelope.ciphertext.len() > MAX_WRAPPED_KEY_CIPHERTEXT_LEN
        || envelope.tag.len() > MAX_VAULT_ENVELOPE_TAG_LEN
    {
        return Err(VaultCryptoError::InputTooLarge);
    }
    exact_nonce_and_tag(&envelope.nonce, &envelope.tag)?;
    if envelope.ciphertext.len() != KEY_LEN {
        return Err(VaultCryptoError::InvalidEnvelope);
    }
    Ok(())
}

fn schema(schema_version: VaultSchemaVersion) -> Result<(), VaultCryptoError> {
    match schema_version {
        VaultSchemaVersion::V1 => Ok(()),
        VaultSchemaVersion::Unsupported(_) => Err(VaultCryptoError::InvalidEnvelope),
        _ => Err(VaultCryptoError::InvalidEnvelope),
    }
}

fn aad_len(aad: &[u8]) -> Result<(), VaultCryptoError> {
    if aad.len() > MAX_AAD_LEN {
        return Err(VaultCryptoError::InputTooLarge);
    }
    Ok(())
}

fn exact_nonce_and_tag(nonce: &[u8], tag: &[u8]) -> Result<(), VaultCryptoError> {
    if nonce.len() != NONCE_LEN || tag.len() != TAG_LEN {
        return Err(VaultCryptoError::InvalidEnvelope);
    }
    Ok(())
}
