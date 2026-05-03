use thiserror::Error;

#[derive(Error, Debug)]
#[non_exhaustive]
pub enum ExtensionError {
    #[error("Failed to parse manifest JSON: {0}")]
    ParseError(#[from] serde_json::Error),

    #[error("Invalid manifest version: expected 3, got {0}")]
    InvalidManifestVersion(u8),

    #[error("Validation failed: {0:?}")]
    ValidationError(Vec<String>),

    #[error("Missing required field: {0}")]
    MissingRequiredField(String),

    #[error("IO error: {0}")]
    IoError(#[from] std::io::Error),

    #[error("Extension directory not found: {0}")]
    DirectoryNotFound(String),

    #[error("Missing resource file: {0}")]
    MissingResource(String),

    #[error("Extension not found: {0}")]
    ExtensionNotFound(String),
}
