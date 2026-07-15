use thiserror::Error;

#[derive(Debug, Error)]
pub enum StorageError {
    #[error("SQLite error: {0}")]
    Sqlite(#[from] rusqlite::Error),

    #[error("LMDB error: {0}")]
    Lmdb(#[from] heed::Error),

    #[error("Serialization error: {0}")]
    Serialization(#[from] serde_json::Error),

    #[error("Not found: {entity} with id {id}")]
    NotFound { entity: String, id: String },

    /// No SQLCipher key has been injected. The caller (e.g. the browser via
    /// OSCrypt) must supply one through `set_sqlcipher_key` or the `*_with_key`
    /// constructors before opening a database.
    #[error("SQLCipher key not configured: inject a key via set_sqlcipher_key or open_with_key")]
    KeyNotConfigured,

    #[error("SQLCipher key must not be empty")]
    EmptySqlCipherKey,

    #[error("SQLCipher key store unavailable")]
    KeyStoreUnavailable,

    #[error("SQLCipher key error: {0}")]
    Encryption(#[source] rusqlite::Error),

    /// The plaintext -> encrypted migration failed. The original database is
    /// left intact (restored from backup) when this is returned.
    #[error("SQLCipher migration failed: {0}")]
    Migration(String),

    #[error("Storage error: {0}")]
    Other(String),
}
