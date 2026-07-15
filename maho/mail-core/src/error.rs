// Copyright 2026 Maho Browser. All rights reserved.

//! Error type for the mail FFI backend. Wraps `maho_core::error::AppError`
//! plus FFI-boundary specific failure modes.

use thiserror::Error;

#[derive(Debug, Error)]
pub enum MailFfiError {
    #[error("core error: {0}")]
    Core(#[from] maho_core::error::AppError),

    #[error("database error: {0}")]
    Database(#[from] rusqlite::Error),

    #[error("serialization error: {0}")]
    Serialization(#[from] serde_json::Error),

    #[error("not initialized: call MahoMailInitialize/MahoMailInjectKeys first")]
    NotInitialized,

    #[error("keys not injected: call MahoMailInjectKeys first")]
    KeysMissing,

    #[error("invalid C string argument: {0}")]
    InvalidArg(&'static str),

    #[error("account {0} not found")]
    AccountNotFound(String),

    #[error("no usable IMAP credentials for account {0}")]
    MissingCredentials(String),

    #[error("credential decryption failed: {0}")]
    CredentialDecrypt(String),

    #[error("oauth2 refresh failed: {0}")]
    OAuth2Refresh(String),

    #[error("db pool error: {0}")]
    Pool(String),

    #[error("internal error: {0}")]
    Internal(String),
}

pub type Result<T> = std::result::Result<T, MailFfiError>;
