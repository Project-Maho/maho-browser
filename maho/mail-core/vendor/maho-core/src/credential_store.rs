use std::path::PathBuf;
use std::sync::OnceLock;

use aes_gcm::aead::{Aead, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce};
use rand::RngCore;

use crate::error::AppError;

static DB_PATH: OnceLock<PathBuf> = OnceLock::new();
static DB_SQLCIPHER_KEY: OnceLock<String> = OnceLock::new();
static ENCRYPTION_KEY: OnceLock<[u8; 32]> = OnceLock::new();

fn get_service_name() -> &'static str {
    #[cfg(test)]
    {
        "maho-mail-test"
    }
    #[cfg(not(test))]
    {
        "maho-mail"
    }
}

#[cfg(test)]
static TEST_KEYRING: std::sync::Mutex<Option<std::collections::HashMap<String, String>>> =
    std::sync::Mutex::new(None);

#[cfg(test)]
fn get_test_keyring_key(service: &str, username: &str) -> String {
    format!("{}:{}", service, username)
}

fn set_keyring_password(service: &str, username: &str, password: &str) -> Result<(), AppError> {
    #[cfg(test)]
    {
        let mut lock = TEST_KEYRING.lock().unwrap();
        if lock.is_none() {
            *lock = Some(std::collections::HashMap::new());
        }
        lock.as_mut().unwrap().insert(
            get_test_keyring_key(service, username),
            password.to_string(),
        );
        Ok(())
    }
    #[cfg(not(test))]
    {
        let entry = keyring::Entry::new(service, username)
            .map_err(|e| AppError::Keyring(format!("Keyring entry creation failed: {}", e)))?;
        entry
            .set_password(password)
            .map_err(|e| AppError::Keyring(format!("Failed to write password: {}", e)))?;
        Ok(())
    }
}

fn get_keyring_password(service: &str, username: &str) -> Result<Option<String>, AppError> {
    #[cfg(test)]
    {
        let lock = TEST_KEYRING.lock().unwrap();
        if let Some(ref map) = *lock {
            if let Some(val) = map.get(&get_test_keyring_key(service, username)) {
                return Ok(Some(val.clone()));
            }
        }
        Ok(None)
    }
    #[cfg(not(test))]
    {
        let entry = keyring::Entry::new(service, username)
            .map_err(|e| AppError::Keyring(format!("Keyring entry creation failed: {}", e)))?;
        match entry.get_password() {
            Ok(pwd) => Ok(Some(pwd)),
            Err(keyring::Error::NoEntry) => Ok(None),
            Err(e) => Err(AppError::Keyring(format!("Failed to read password: {}", e))),
        }
    }
}

#[cfg(test)]
fn delete_keyring_credential(service: &str, username: &str) -> Result<(), AppError> {
    #[cfg(test)]
    {
        let mut lock = TEST_KEYRING.lock().unwrap();
        if let Some(ref mut map) = *lock {
            map.remove(&get_test_keyring_key(service, username));
        }
        Ok(())
    }
    #[cfg(not(test))]
    {
        let entry = keyring::Entry::new(service, username)
            .map_err(|e| AppError::Keyring(format!("Keyring entry creation failed: {}", e)))?;
        match entry.delete_credential() {
            Ok(_) | Err(keyring::Error::NoEntry) => Ok(()),
            Err(e) => Err(AppError::Keyring(format!(
                "Failed to delete credential: {}",
                e
            ))),
        }
    }
}

pub fn get_or_generate_sqlcipher_key(db_path: &std::path::Path) -> Result<String, AppError> {
    match get_keyring_password(get_service_name(), "sqlcipher_key")? {
        Some(key) => Ok(key.trim().to_string()),
        None => {
            let db_key_path = db_path.with_extension("db.key");
            if db_key_path.exists() {
                let key = std::fs::read_to_string(&db_key_path)
                    .map_err(|e| AppError::Keyring(format!("Failed to read legacy DB key: {}", e)))?
                    .trim()
                    .to_string();

                set_keyring_password(get_service_name(), "sqlcipher_key", &key)?;

                let _ = std::fs::remove_file(db_key_path);
                log::info!("Successfully migrated legacy DB key to keyring and removed file.");

                Ok(key)
            } else {
                let mut rng = rand::thread_rng();
                let mut bytes = [0u8; 32];
                rng.fill_bytes(&mut bytes);
                let hex_key: String = bytes.iter().map(|b| format!("{:02x}", b)).collect();

                set_keyring_password(get_service_name(), "sqlcipher_key", &hex_key)?;

                Ok(hex_key)
            }
        }
    }
}

pub fn init_with_key(
    app_data_dir: &std::path::Path,
    db_path: &std::path::Path,
    sqlcipher_key: String,
) -> Result<(), AppError> {
    let _ = DB_PATH.set(db_path.to_path_buf());
    let _ = DB_SQLCIPHER_KEY.set(sqlcipher_key);

    let key_path = app_data_dir.join(".encryption_key");
    let key = load_or_generate_key(&key_path)?;
    let _ = ENCRYPTION_KEY.set(key);
    Ok(())
}

fn load_or_generate_key(key_path: &std::path::Path) -> Result<[u8; 32], AppError> {
    match get_keyring_password(get_service_name(), "encryption_key")? {
        Some(pwd_str) => {
            let decoded = base64::Engine::decode(&base64::prelude::BASE64_STANDARD, pwd_str.trim())
                .map_err(|e| {
                    AppError::Keyring(format!("Failed to decode key from keyring: {}", e))
                })?;
            if decoded.len() != 32 {
                return Err(AppError::Keyring(
                    "Invalid key length in keyring".to_string(),
                ));
            }
            let mut key = [0u8; 32];
            key.copy_from_slice(&decoded);
            Ok(key)
        }
        None => {
            if key_path.exists() {
                let key_bytes = std::fs::read(key_path).map_err(|e| {
                    AppError::Keyring(format!("Failed to read legacy encryption key file: {}", e))
                })?;
                if key_bytes.len() != 32 {
                    return Err(AppError::Keyring("Invalid legacy key length".to_string()));
                }
                let mut key = [0u8; 32];
                key.copy_from_slice(&key_bytes);

                let encoded = base64::Engine::encode(&base64::prelude::BASE64_STANDARD, key);
                set_keyring_password(get_service_name(), "encryption_key", &encoded)?;

                let _ = std::fs::remove_file(key_path);
                log::info!(
                    "Successfully migrated legacy encryption key to keyring and removed file."
                );

                Ok(key)
            } else {
                let mut key = [0u8; 32];
                rand::thread_rng().fill_bytes(&mut key);

                let encoded = base64::Engine::encode(&base64::prelude::BASE64_STANDARD, key);
                set_keyring_password(get_service_name(), "encryption_key", &encoded)?;

                Ok(key)
            }
        }
    }
}

fn get_key() -> Result<&'static [u8; 32], AppError> {
    ENCRYPTION_KEY
        .get()
        .ok_or_else(|| AppError::Keyring("Credential store not initialised".to_string()))
}

fn encrypt(plaintext: &str) -> Result<(Vec<u8>, Vec<u8>), AppError> {
    encrypt_with_key(get_key()?, plaintext)
}

fn encrypt_with_key(key: &[u8; 32], plaintext: &str) -> Result<(Vec<u8>, Vec<u8>), AppError> {
    let cipher = Aes256Gcm::new_from_slice(key)
        .map_err(|e| AppError::Keyring(format!("Failed to create cipher: {}", e)))?;

    let mut nonce_bytes = [0u8; 12];
    rand::thread_rng().fill_bytes(&mut nonce_bytes);
    let nonce = Nonce::from_slice(&nonce_bytes);

    let ciphertext = cipher
        .encrypt(nonce, plaintext.as_bytes())
        .map_err(|e| AppError::Keyring(format!("Encryption failed: {}", e)))?;

    Ok((ciphertext, nonce_bytes.to_vec()))
}

fn decrypt(ciphertext: &[u8], nonce_bytes: &[u8]) -> Result<String, AppError> {
    decrypt_with_key(get_key()?, ciphertext, nonce_bytes)
}

fn decrypt_with_key(
    key: &[u8; 32],
    ciphertext: &[u8],
    nonce_bytes: &[u8],
) -> Result<String, AppError> {
    let cipher = Aes256Gcm::new_from_slice(key)
        .map_err(|e| AppError::Keyring(format!("Failed to create cipher: {}", e)))?;

    let nonce = Nonce::from_slice(nonce_bytes);

    let plaintext = cipher
        .decrypt(nonce, ciphertext)
        .map_err(|e| AppError::Keyring(format!("Decryption failed: {}", e)))?;

    String::from_utf8(plaintext)
        .map_err(|e| AppError::Keyring(format!("Decrypted data is not valid UTF-8: {}", e)))
}

fn open_db() -> Result<rusqlite::Connection, AppError> {
    let path = DB_PATH
        .get()
        .ok_or_else(|| AppError::Keyring("Credential store not initialised".to_string()))?;
    let conn = rusqlite::Connection::open(path)
        .map_err(|e| AppError::Keyring(format!("Failed to open credential DB: {}", e)))?;

    if let Some(key) = DB_SQLCIPHER_KEY.get() {
        conn.pragma_update(None, "key", key)
            .map_err(|e| AppError::Keyring(format!("Failed to set DB encryption key: {}", e)))?;
    }

    conn.execute_batch("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;")
        .map_err(|e| AppError::Keyring(format!("Failed to set DB pragmas: {}", e)))?;
    Ok(conn)
}

pub fn store_credential(
    account_id: &str,
    credential_type: &str,
    value: &str,
) -> Result<(), AppError> {
    let (ciphertext, nonce) = encrypt(value)?;
    let db = open_db()?;

    db.execute(
        "INSERT INTO encrypted_credentials (account_id, credential_type, encrypted_value, nonce, updated_at)
         VALUES (?1, ?2, ?3, ?4, datetime('now'))
         ON CONFLICT(account_id, credential_type)
         DO UPDATE SET encrypted_value = excluded.encrypted_value,
                       nonce = excluded.nonce,
                       updated_at = datetime('now')",
        rusqlite::params![account_id, credential_type, ciphertext, nonce],
    )
    .map_err(|e| AppError::Keyring(format!("Failed to store credential: {}", e)))?;

    Ok(())
}

pub fn get_credential(account_id: &str, credential_type: &str) -> Result<Option<String>, AppError> {
    let db = open_db()?;

    let result: Result<(Vec<u8>, Vec<u8>), rusqlite::Error> = db.query_row(
        "SELECT encrypted_value, nonce FROM encrypted_credentials WHERE account_id = ?1 AND credential_type = ?2",
        rusqlite::params![account_id, credential_type],
        |row| Ok((row.get(0)?, row.get(1)?)),
    );

    match result {
        Ok((ciphertext, nonce)) => {
            let plaintext = decrypt(&ciphertext, &nonce)?;
            Ok(Some(plaintext))
        }
        Err(rusqlite::Error::QueryReturnedNoRows) => Ok(None),
        Err(e) => Err(AppError::Keyring(format!(
            "Failed to get credential: {}",
            e
        ))),
    }
}

pub fn delete_credential(account_id: &str, credential_type: &str) -> Result<(), AppError> {
    let db = open_db()?;

    db.execute(
        "DELETE FROM encrypted_credentials WHERE account_id = ?1 AND credential_type = ?2",
        rusqlite::params![account_id, credential_type],
    )
    .map_err(|e| AppError::Keyring(format!("Failed to delete credential: {}", e)))?;

    Ok(())
}

pub fn delete_account_credentials(account_id: &str) -> Result<(), AppError> {
    let db = open_db()?;

    db.execute(
        "DELETE FROM encrypted_credentials WHERE account_id = ?1",
        rusqlite::params![account_id],
    )
    .map_err(|e| AppError::Keyring(format!("Failed to delete account credentials: {}", e)))?;

    Ok(())
}

pub fn store_account_credentials(
    account_id: &str,
    password: Option<&str>,
    oauth2_access_token: Option<&str>,
    oauth2_refresh_token: Option<&str>,
    oauth2_client_secret: Option<&str>,
) -> Result<(), AppError> {
    if let Some(pwd) = password {
        if !pwd.is_empty() {
            store_credential(account_id, "password", pwd)?;
        }
    }
    if let Some(token) = oauth2_access_token {
        if !token.is_empty() {
            store_credential(account_id, "oauth2_access_token", token)?;
        }
    }
    if let Some(token) = oauth2_refresh_token {
        if !token.is_empty() {
            store_credential(account_id, "oauth2_refresh_token", token)?;
        }
    }
    if let Some(secret) = oauth2_client_secret {
        if !secret.is_empty() {
            store_credential(account_id, "oauth2_client_secret", secret)?;
        }
    }
    Ok(())
}

pub fn migrate_credentials_from_db(
    db: &rusqlite::Connection,
    account_id: &str,
) -> Result<(), AppError> {
    let row: (Option<String>, Option<String>, Option<String>, Option<String>) = db
        .query_row(
            "SELECT password, oauth2_access_token, oauth2_refresh_token, oauth2_client_secret FROM accounts WHERE id = ?1",
            [account_id],
            |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?, row.get(3)?)),
        )
        .map_err(|_| AppError::NotFound(format!("Account {} not found", account_id)))?;

    let (password, access_token, refresh_token, client_secret) = row;

    let has_db_creds = password.as_ref().is_some_and(|s| !s.is_empty())
        || access_token.as_ref().is_some_and(|s| !s.is_empty())
        || refresh_token.as_ref().is_some_and(|s| !s.is_empty())
        || client_secret.as_ref().is_some_and(|s| !s.is_empty());

    if !has_db_creds {
        return Ok(());
    }

    store_account_credentials(
        account_id,
        password.as_deref(),
        access_token.as_deref(),
        refresh_token.as_deref(),
        client_secret.as_deref(),
    )?;

    db.execute(
        "UPDATE accounts SET password = NULL, oauth2_access_token = NULL, oauth2_refresh_token = NULL, oauth2_client_secret = NULL WHERE id = ?1",
        [account_id],
    )
    .map_err(|e| AppError::Keyring(format!("Failed to clear DB credentials after migration: {}", e)))?;

    log::info!(
        "Migrated credentials for account {} from SQLite to encrypted store",
        account_id
    );

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Mutex, MutexGuard, Once};

    static KEYRING_MOCK: Once = Once::new();
    static TEST_SERIAL: Mutex<()> = Mutex::new(());

    fn isolated_keyring() -> MutexGuard<'static, ()> {
        let guard = TEST_SERIAL.lock().unwrap();
        let mut lock = TEST_KEYRING.lock().unwrap();
        *lock = Some(std::collections::HashMap::new());
        drop(lock);
        guard
    }

    fn use_mock_keyring() {
        KEYRING_MOCK.call_once(|| {
            keyring::set_default_credential_builder(keyring::mock::default_credential_builder());
        });
    }

    #[test]
    fn test_encrypt_decrypt_roundtrip() {
        let _guard = isolated_keyring();
        let mut key = [0u8; 32];
        rand::thread_rng().fill_bytes(&mut key);

        let plaintext = "secret_credentials_123";
        let (ciphertext, nonce) = encrypt_with_key(&key, plaintext).unwrap();
        assert_ne!(plaintext.as_bytes(), ciphertext.as_slice());

        let decrypted = decrypt_with_key(&key, &ciphertext, &nonce).unwrap();
        assert_eq!(plaintext, decrypted);
    }

    #[test]
    fn test_decrypt_wrong_nonce_fails() {
        let _guard = isolated_keyring();
        let mut key = [0u8; 32];
        rand::thread_rng().fill_bytes(&mut key);

        let plaintext = "secret_credentials_123";
        let (ciphertext, nonce) = encrypt_with_key(&key, plaintext).unwrap();

        let mut bad_nonce = nonce.clone();
        if !bad_nonce.is_empty() {
            bad_nonce[0] ^= 1;
        }
        let decrypted = decrypt_with_key(&key, &ciphertext, &bad_nonce);
        assert!(decrypted.is_err());
    }

    #[test]
    fn test_keyring_roundtrip() {
        let _guard = isolated_keyring();
        use_mock_keyring();
        let entry = keyring::Entry::new("maho-mail-test", "test_key").unwrap();

        let test_val = "test-value-xyz";
        entry.set_password(test_val).unwrap();

        let retrieved = entry.get_password().unwrap();
        assert_eq!(retrieved, test_val);

        entry.delete_credential().unwrap();
        assert!(matches!(entry.get_password(), Err(keyring::Error::NoEntry)));
    }

    #[test]
    fn test_invalid_legacy_key_length() {
        let _guard = isolated_keyring();
        let key_path =
            std::env::temp_dir().join(format!("maho_bad_legacy_{}.key", std::process::id()));
        std::fs::write(&key_path, vec![0u8; 16]).unwrap();

        let result = load_or_generate_key(&key_path);
        assert!(result.is_err());

        let _ = std::fs::remove_file(key_path);
    }

    #[test]
    fn test_keyring_bad_length() {
        let _guard = isolated_keyring();
        let encoded = base64::Engine::encode(&base64::prelude::BASE64_STANDARD, [0u8; 16]);
        set_keyring_password(get_service_name(), "encryption_key", &encoded).unwrap();

        let key_path = std::env::temp_dir().join("maho_nonexistent_badlen.key");
        let result = load_or_generate_key(&key_path);
        assert!(result.is_err());
        assert!(result
            .err()
            .unwrap()
            .to_string()
            .contains("Invalid key length in keyring"));

        let _ = delete_keyring_credential(get_service_name(), "encryption_key");
    }

    #[test]
    fn test_migrate_legacy_file_to_keyring() {
        let _guard = isolated_keyring();
        let _ = delete_keyring_credential(get_service_name(), "encryption_key");

        let mut original = [0u8; 32];
        rand::thread_rng().fill_bytes(&mut original);
        let key_path =
            std::env::temp_dir().join(format!("maho_migrate_{}.key", std::process::id()));
        std::fs::write(&key_path, original).unwrap();

        let migrated = load_or_generate_key(&key_path).unwrap();
        assert_eq!(migrated, original, "migrated key must equal original bytes");
        assert!(
            !key_path.exists(),
            "legacy key file must be deleted after migration"
        );

        let after_restart = load_or_generate_key(&key_path).unwrap();
        assert_eq!(
            after_restart, original,
            "key must survive restart via keyring"
        );

        let (ct, nonce) = encrypt_with_key(&after_restart, "hello").unwrap();
        assert_eq!(
            decrypt_with_key(&after_restart, &ct, &nonce).unwrap(),
            "hello"
        );

        let _ = delete_keyring_credential(get_service_name(), "encryption_key");
    }

    #[test]
    fn test_keyring_two_instances() {
        let _guard = isolated_keyring();
        set_keyring_password("service-test", "user-test", "val123").unwrap();
        let retrieved = get_keyring_password("service-test", "user-test")
            .unwrap()
            .unwrap();
        assert_eq!(retrieved, "val123");
        delete_keyring_credential("service-test", "user-test").unwrap();
        assert!(get_keyring_password("service-test", "user-test")
            .unwrap()
            .is_none());
    }
}
