//! Password decryption for imported browser data.
//!
//! Phase 3a: Chromium-family macOS Keychain path (AES-128-CBC via PBKDF2).
//! Phase 3b: Firefox NSS pure-Rust decryption (PBKDF2 + AES-256-CBC / 3DES-CBC).

pub mod asn1;
pub mod chromium_keychain;
pub mod firefox_nss;

pub use chromium_keychain::{
    ChromiumKey, DecryptError, KeychainError, KeychainProvider,
    decrypt_v10, derive_aes_key,
};

pub use firefox_nss::{NssError, NssMasterKey, decrypt_nss_value, unlock_nss_master_key};

#[cfg(target_os = "macos")]
pub use chromium_keychain::MacOsKeychain;
