//! BYOK (Bring Your Own Key) entitlement helpers.
//!
//! A BYOK key lives in the settings table under `byok:<provider>`. It only
//! unlocks agent features (Routines, Skills, deep research, premium search)
//! after `validate_api_key` has succeeded for it once: the validation records
//! `byok_valid:<provider>` = SHA-256 fingerprint of the validated key. Replacing
//! the key invalidates the record because the fingerprint no longer matches.

use maho_storage::sqlite::SqliteStorage;
use sha2::{Digest, Sha256};

/// Providers a BYOK key can be stored for, in preference order.
pub const BYOK_PROVIDERS: [&str; 4] = ["openai", "anthropic", "gemini", "ollama"];

/// Settings key holding the raw BYOK key for `provider`.
pub fn key_setting(provider: &str) -> String {
    format!("byok:{provider}")
}

/// Settings key holding the fingerprint of the last successfully validated key.
pub fn valid_setting(provider: &str) -> String {
    format!("byok_valid:{provider}")
}

fn fingerprint(key: &str) -> String {
    format!("{:x}", Sha256::digest(key.as_bytes()))
}

/// True when `recorded` (a `byok_valid:<provider>` value) is the validation
/// fingerprint of `key`.
pub fn is_validated(key: &str, recorded: &str) -> bool {
    !key.trim().is_empty() && recorded == fingerprint(key)
}

/// Records that `key` passed `validate_api_key` for `provider`.
pub fn mark_validated(
    storage: &SqliteStorage,
    provider: &str,
    key: &str,
) -> Result<(), maho_storage::StorageError> {
    storage.set_setting(&valid_setting(provider), &fingerprint(key))
}

/// Stores `key` for `provider` and records it as validated. Callers must only
/// call this after `validate_api_key` returned true for this exact key.
pub fn store_validated_key(
    storage: &SqliteStorage,
    provider: &str,
    key: &str,
) -> Result<(), maho_storage::StorageError> {
    storage.set_setting(&key_setting(provider), key)?;
    mark_validated(storage, provider, key)
}

/// Removes the stored key and its validation record for `provider`.
pub fn clear_key(storage: &SqliteStorage, provider: &str) -> Result<(), maho_storage::StorageError> {
    storage.set_setting(&key_setting(provider), "")?;
    storage.set_setting(&valid_setting(provider), "")
}

/// A stored BYOK key that has been validated at least once.
#[derive(Clone)]
pub struct ByokCredentials {
    pub provider: String,
    pub key: String,
}

impl std::fmt::Debug for ByokCredentials {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("ByokCredentials")
            .field("provider", &self.provider)
            .field("key", &"[REDACTED KEY]")
            .finish()
    }
}

/// Returns the first provider whose stored key matches its validation record.
pub fn validated_credentials(storage: &SqliteStorage) -> Option<ByokCredentials> {
    BYOK_PROVIDERS.iter().find_map(|provider| {
        let key = storage.get_setting(&key_setting(provider)).ok()??;
        if key.trim().is_empty() {
            return None;
        }
        let recorded = storage.get_setting(&valid_setting(provider)).ok()??;
        is_validated(&key, &recorded).then(|| ByokCredentials {
            provider: (*provider).to_string(),
            key,
        })
    })
}

/// True when the user holds at least one validated BYOK key.
pub fn has_byok_key(storage: &SqliteStorage) -> bool {
    validated_credentials(storage).is_some()
}

/// OpenAI-compatible endpoint and default model used to run agent work
/// (e.g. routines) directly against the user's BYOK provider.
pub fn provider_endpoint(provider: &str) -> Option<(&'static str, &'static str)> {
    match provider {
        "openai" => Some(("https://api.openai.com/v1", "gpt-4o-mini")),
        "anthropic" => Some(("https://api.anthropic.com/v1", "claude-sonnet-4-20250514")),
        "gemini" => Some((
            "https://generativelanguage.googleapis.com/v1beta/openai",
            "gemini-2.5-flash",
        )),
        "ollama" => Some(("http://localhost:11434/v1", "llama3.2")),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unvalidated_key_does_not_count() {
        crate::install_test_sqlcipher_key();
        let storage = SqliteStorage::open_in_memory().unwrap();
        storage.set_setting("byok:openai", "sk-test-1234567890").unwrap();
        assert!(!has_byok_key(&storage));
    }

    #[test]
    fn validated_key_counts_until_replaced() {
        crate::install_test_sqlcipher_key();
        let storage = SqliteStorage::open_in_memory().unwrap();
        storage.set_setting("byok:anthropic", "sk-ant-aaaaaaaa").unwrap();
        mark_validated(&storage, "anthropic", "sk-ant-aaaaaaaa").unwrap();
        let creds = validated_credentials(&storage).expect("validated");
        assert_eq!(creds.provider, "anthropic");
        assert_eq!(creds.key, "sk-ant-aaaaaaaa");

        storage.set_setting("byok:anthropic", "sk-ant-bbbbbbbb").unwrap();
        assert!(!has_byok_key(&storage), "a replaced key needs revalidation");
    }

    #[test]
    fn store_then_clear_round_trips() {
        crate::install_test_sqlcipher_key();
        let storage = SqliteStorage::open_in_memory().unwrap();
        store_validated_key(&storage, "openai", "sk-test-1234567890").unwrap();
        assert!(has_byok_key(&storage));
        clear_key(&storage, "openai").unwrap();
        assert!(!has_byok_key(&storage));
        assert_eq!(storage.get_setting("byok_valid:openai").unwrap().as_deref(), Some(""));
    }

    #[test]
    fn deleted_key_does_not_count() {
        crate::install_test_sqlcipher_key();
        let storage = SqliteStorage::open_in_memory().unwrap();
        storage.set_setting("byok:openai", "sk-test-1234567890").unwrap();
        mark_validated(&storage, "openai", "sk-test-1234567890").unwrap();
        storage.set_setting("byok:openai", "").unwrap();
        assert!(!has_byok_key(&storage));
    }
}
