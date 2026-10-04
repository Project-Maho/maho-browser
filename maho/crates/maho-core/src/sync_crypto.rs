//! E2E encryption for cross-device sync.
//!
//! Uses AES-256-GCM for authenticated encryption and Argon2id for key derivation
//! from a shared seed. BIP39 mnemonics provide human-readable recovery phrases.

use aes_gcm::{
    aead::{Aead, KeyInit, OsRng},
    Aes256Gcm, Nonce,
};
use argon2::{Algorithm, Argon2, Params, Version};
use rand::RngCore;

/// 32-byte encryption key derived from seed via Argon2id
pub struct EncryptionKey([u8; 32]);

impl EncryptionKey {
    /// Get the raw key bytes
    pub fn as_bytes(&self) -> &[u8; 32] {
        &self.0
    }
}

/// Generate a new 32-byte random sync seed
pub fn generate_sync_seed() -> [u8; 32] {
    let mut seed = [0u8; 32];
    OsRng.fill_bytes(&mut seed);
    seed
}

/// Encode a 32-byte seed as a BIP39 24-word mnemonic recovery phrase
pub fn encode_recovery_phrase(seed: &[u8; 32]) -> Result<String, String> {
    let mnemonic =
        bip39::Mnemonic::from_entropy(seed).map_err(|e| format!("BIP39 encode error: {}", e))?;
    Ok(mnemonic.to_string())
}

/// Decode a BIP39 recovery phrase back to a 32-byte seed
pub fn decode_recovery_phrase(phrase: &str) -> Result<[u8; 32], String> {
    let mnemonic: bip39::Mnemonic = phrase
        .parse()
        .map_err(|e| format!("BIP39 decode error: {}", e))?;
    let entropy = mnemonic.to_entropy();
    if entropy.len() != 32 {
        return Err(format!("Expected 32 bytes, got {}", entropy.len()));
    }
    let mut seed = [0u8; 32];
    seed.copy_from_slice(&entropy);
    Ok(seed)
}

/// Derive an AES-256-GCM encryption key from a 32-byte seed using Argon2id.
///
/// Uses a fixed salt derived from "maho-sync-v1" to ensure deterministic key derivation
/// across devices sharing the same seed.
pub fn derive_encryption_key(seed: &[u8; 32]) -> Result<EncryptionKey, String> {
    // Fixed application salt — deterministic so same seed → same key on all devices
    let salt = b"maho-sync-v1-key";

    let params = Params::new(
        19456, // 19 MiB memory cost
        2,     // 2 iterations
        1,     // 1 degree of parallelism
        Some(32),
    )
    .map_err(|e| format!("Argon2 params error: {}", e))?;

    let argon2 = Argon2::new(Algorithm::Argon2id, Version::V0x13, params);

    let mut key = [0u8; 32];
    argon2
        .hash_password_into(seed, salt, &mut key)
        .map_err(|e| format!("Argon2 derivation error: {}", e))?;

    Ok(EncryptionKey(key))
}

/// Derive a room ID from the shared seed.
///
/// The room ID is safe to share with the server; the encryption key never leaves the device.
pub fn derive_room_id(seed: &[u8; 32]) -> Result<String, String> {
    let salt = b"maho-sync-v1-room";

    let params =
        Params::new(19456, 2, 1, Some(16)).map_err(|e| format!("Argon2 params error: {}", e))?;

    let argon2 = Argon2::new(Algorithm::Argon2id, Version::V0x13, params);

    let mut room_id = [0u8; 16];
    argon2
        .hash_password_into(seed, salt, &mut room_id)
        .map_err(|e| format!("Argon2 derivation error: {}", e))?;

    Ok(room_id.iter().map(|b| format!("{:02x}", b)).collect())
}

/// Encrypt data using AES-256-GCM.
///
/// Returns a Vec containing: 12-byte nonce || ciphertext || 16-byte auth tag.
/// The nonce is randomly generated for each encryption.
pub fn encrypt_update(data: &[u8], key: &EncryptionKey) -> Result<Vec<u8>, String> {
    let cipher = Aes256Gcm::new_from_slice(key.as_bytes())
        .map_err(|e| format!("Cipher init error: {}", e))?;

    let mut nonce_bytes = [0u8; 12];
    OsRng.fill_bytes(&mut nonce_bytes);
    let nonce = Nonce::from_slice(&nonce_bytes);

    let ciphertext = cipher
        .encrypt(nonce, data)
        .map_err(|e| format!("Encryption error: {}", e))?;

    // Prepend nonce to ciphertext: [12 bytes nonce][ciphertext + tag]
    let mut result = Vec::with_capacity(12 + ciphertext.len());
    result.extend_from_slice(&nonce_bytes);
    result.extend_from_slice(&ciphertext);
    Ok(result)
}

/// Decrypt data that was encrypted with `encrypt_update`.
///
/// Expects input format: 12-byte nonce || ciphertext || 16-byte auth tag.
pub fn decrypt_update(encrypted: &[u8], key: &EncryptionKey) -> Result<Vec<u8>, String> {
    if encrypted.len() < 12 + 16 {
        return Err("Encrypted data too short (need at least nonce + tag)".to_string());
    }

    let cipher = Aes256Gcm::new_from_slice(key.as_bytes())
        .map_err(|e| format!("Cipher init error: {}", e))?;

    let nonce = Nonce::from_slice(&encrypted[..12]);
    let ciphertext = &encrypted[12..];

    cipher
        .decrypt(nonce, ciphertext)
        .map_err(|e| format!("Decryption error: {} (wrong key or corrupted data)", e))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_roundtrip_encrypt_decrypt() {
        let seed = generate_sync_seed();
        let key = derive_encryption_key(&seed).unwrap();
        let data = b"hello world CRDT update";
        let encrypted = encrypt_update(data, &key).unwrap();
        let decrypted = decrypt_update(&encrypted, &key).unwrap();
        assert_eq!(data.as_slice(), decrypted.as_slice());
    }

    #[test]
    fn test_different_nonce_each_time() {
        let seed = generate_sync_seed();
        let key = derive_encryption_key(&seed).unwrap();
        let data = b"same data";
        let enc1 = encrypt_update(data, &key).unwrap();
        let enc2 = encrypt_update(data, &key).unwrap();
        // Different nonces → different ciphertext
        assert_ne!(enc1, enc2);
        // Both decrypt to same plaintext
        assert_eq!(
            decrypt_update(&enc1, &key).unwrap(),
            decrypt_update(&enc2, &key).unwrap()
        );
    }

    #[test]
    fn test_wrong_key_fails() {
        let seed1 = generate_sync_seed();
        let seed2 = generate_sync_seed();
        let key1 = derive_encryption_key(&seed1).unwrap();
        let key2 = derive_encryption_key(&seed2).unwrap();
        let encrypted = encrypt_update(b"secret", &key1).unwrap();
        assert!(decrypt_update(&encrypted, &key2).is_err());
    }

    #[test]
    fn test_bip39_roundtrip() {
        let seed = generate_sync_seed();
        let phrase = encode_recovery_phrase(&seed).unwrap();
        let words: Vec<&str> = phrase.split_whitespace().collect();
        assert_eq!(words.len(), 24);
        let recovered = decode_recovery_phrase(&phrase).unwrap();
        assert_eq!(seed, recovered);
    }

    #[test]
    fn test_deterministic_key_derivation() {
        let seed = generate_sync_seed();
        let key1 = derive_encryption_key(&seed).unwrap();
        let key2 = derive_encryption_key(&seed).unwrap();
        assert_eq!(key1.as_bytes(), key2.as_bytes());
    }

    #[test]
    fn test_room_id_differs_from_key() {
        let seed = generate_sync_seed();
        let key = derive_encryption_key(&seed).unwrap();
        let room_id = derive_room_id(&seed).unwrap();
        let key_hex: String = key
            .as_bytes()
            .iter()
            .map(|b| format!("{:02x}", b))
            .collect();
        assert_ne!(room_id, key_hex);
    }

    // CR-07
    #[test]
    fn test_truncated_ciphertext() {
        let seed = generate_sync_seed();
        let key = derive_encryption_key(&seed).unwrap();
        let short_data = vec![0u8; 27];
        assert!(decrypt_update(&short_data, &key).is_err());
    }

    // CR-08
    #[test]
    fn test_tampered_ciphertext() {
        let seed = generate_sync_seed();
        let key = derive_encryption_key(&seed).unwrap();
        let mut encrypted = encrypt_update(b"sensitive data", &key).unwrap();
        let mid = encrypted.len() / 2;
        encrypted[mid] ^= 0xFF;
        assert!(decrypt_update(&encrypted, &key).is_err());
    }

    // CR-09
    #[test]
    fn test_empty_plaintext() {
        let seed = generate_sync_seed();
        let key = derive_encryption_key(&seed).unwrap();
        let encrypted = encrypt_update(b"", &key).unwrap();
        let decrypted = decrypt_update(&encrypted, &key).unwrap();
        assert!(decrypted.is_empty());
    }

    // CR-10
    #[test]
    fn test_large_payload() {
        let seed = generate_sync_seed();
        let key = derive_encryption_key(&seed).unwrap();
        let large = vec![0xABu8; 1_000_000];
        let encrypted = encrypt_update(&large, &key).unwrap();
        let decrypted = decrypt_update(&encrypted, &key).unwrap();
        assert_eq!(decrypted, large);
    }

    // CR-11
    #[test]
    fn test_invalid_bip39_phrase() {
        let result = decode_recovery_phrase("xyzzy flurbo zazzle bloop wonk grizzle quux flarb snorkel plimbus glonk dweeb skrunk fizzle borp mizzle clangor sploot vroom noodle flick gribble zoink splarf");
        assert!(result.is_err());
    }

    // CR-12
    #[test]
    fn test_short_phrase() {
        let seed = generate_sync_seed();
        let phrase = encode_recovery_phrase(&seed).unwrap();
        let words: Vec<&str> = phrase.split_whitespace().collect();
        let short_phrase = words[..23].join(" ");
        assert!(decode_recovery_phrase(&short_phrase).is_err());
    }
}
