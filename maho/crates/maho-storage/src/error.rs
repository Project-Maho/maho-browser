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

    #[error("Storage error: {0}")]
    Other(String),
}
