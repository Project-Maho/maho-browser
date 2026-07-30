use crate::errors::{AppError, Result};
use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use base64::Engine;
use sha2::{Digest, Sha256};

pub const ACCESS_TOKEN_BYTES: usize = 32;
pub const REFRESH_TOKEN_BYTES: usize = 32;

pub fn generate_access_token() -> Result<Vec<u8>> {
    generate_opaque_token(ACCESS_TOKEN_BYTES)
}

pub fn generate_refresh_token() -> Result<Vec<u8>> {
    generate_opaque_token(REFRESH_TOKEN_BYTES)
}

pub fn generate_access_token_string() -> Result<String> {
    Ok(encode_token(&generate_access_token()?))
}

pub fn generate_refresh_token_string() -> Result<String> {
    Ok(encode_token(&generate_refresh_token()?))
}

pub fn hash_token(token: &[u8]) -> [u8; 32] {
    let mut hasher = Sha256::new();
    hasher.update(token);
    hasher.finalize().into()
}

pub fn hash_token_str(token: &str) -> [u8; 32] {
    hash_token(token.as_bytes())
}

fn generate_opaque_token(byte_len: usize) -> Result<Vec<u8>> {
    let mut bytes = vec![0u8; byte_len];
    // A swallowed CSPRNG error would yield an all-zero, predictable token.
    getrandom::getrandom(&mut bytes)
        .map_err(|e| AppError::Internal(format!("csprng_failure: {e}")))?;
    Ok(bytes)
}

fn encode_token(bytes: &[u8]) -> String {
    URL_SAFE_NO_PAD.encode(bytes)
}
