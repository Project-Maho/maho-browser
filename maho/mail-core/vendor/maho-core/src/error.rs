use thiserror::Error;

#[derive(Debug, Error)]
pub enum AppError {
    #[error("Database error: {0}")]
    Database(#[from] rusqlite::Error),

    #[error("Serialization error: {0}")]
    Serialization(#[from] serde_json::Error),

    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),

    #[error("Not found: {0}")]
    NotFound(String),

    #[error("Validation error: {0}")]
    Validation(String),

    #[error("Auth error: {0}")]
    Auth(String),

    #[error("Network error: {0}")]
    Network(String),

    #[error("Delivery uncertain: {0}")]
    DeliveryUncertain(String),

    #[error("AI error: {0}")]
    Ai(String),

    #[error("Keyring error: {0}")]
    Keyring(String),

    #[error("Internal error: {0}")]
    Internal(String),
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_error_display() {
        let err = AppError::NotFound("test item".to_string());
        assert_eq!(err.to_string(), "Not found: test item");
    }

    #[test]
    fn test_io_error_conversion() {
        let io_err = std::io::Error::new(std::io::ErrorKind::NotFound, "file missing");
        let app_err: AppError = io_err.into();
        assert!(app_err.to_string().contains("IO error"));
    }
}
