// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Windows DPAPI session token loader for MCP authentication.
//!
//! Reads the DPAPI-encrypted session token file written by the browser at
//! `%LOCALAPPDATA%\Maho\mcp-session-token`, decrypts it using
//! `CryptUnprotectData` (user-scope), and returns the 32-byte plaintext
//! for inclusion in the MCP `initialize` handshake.

use crate::error::{McpBridgeError, Result};

/// Expected size of the decrypted session token in bytes.
pub const TOKEN_SIZE: usize = 32;

/// Returns the canonical path to the session token file.
#[cfg(windows)]
pub fn session_token_path() -> std::path::PathBuf {
    if let Some(local_app_data) = std::env::var_os("LOCALAPPDATA") {
        std::path::PathBuf::from(local_app_data)
            .join("Maho")
            .join("mcp-session-token")
    } else {
        // Fallback: use directories crate
        directories::BaseDirs::new()
            .map(|dirs| dirs.data_local_dir().join("Maho").join("mcp-session-token"))
            .unwrap_or_default()
    }
}

/// Reads and decrypts the DPAPI-protected session token file.
///
/// Returns the 32-byte plaintext token on success. Fails if the file doesn't
/// exist, cannot be read, decryption fails, or the decrypted data is not
/// exactly 32 bytes.
#[cfg(windows)]
pub fn load_session_token() -> Result<Vec<u8>> {
    use std::fs;
    use std::ptr;

    use windows_sys::Win32::Foundation::LocalFree;
    use windows_sys::Win32::Security::Cryptography::{CryptUnprotectData, CRYPT_INTEGER_BLOB};

    let path = session_token_path();
    let ciphertext = fs::read(&path).map_err(|e| McpBridgeError::TokenFileRead {
        path: path.clone(),
        source: e,
    })?;

    if ciphertext.is_empty() {
        return Err(McpBridgeError::TokenDecrypt(
            "session token file is empty".into(),
        ));
    }

    let mut input_blob = CRYPT_INTEGER_BLOB {
        cbData: ciphertext.len() as u32,
        pbData: ciphertext.as_ptr() as *mut u8,
    };

    let mut output_blob = CRYPT_INTEGER_BLOB {
        cbData: 0,
        pbData: ptr::null_mut(),
    };

    // SAFETY: CryptUnprotectData is a well-defined Win32 API. We pass valid
    // pointers and handle the output allocation via LocalFree.
    let ok = unsafe {
        CryptUnprotectData(
            &mut input_blob,
            ptr::null_mut(), // ppszDataDescr
            ptr::null_mut(), // pOptionalEntropy
            ptr::null_mut(), // pvReserved
            ptr::null_mut(), // pPromptStruct
            0,               // dwFlags
            &mut output_blob,
        )
    };

    if ok == 0 {
        return Err(McpBridgeError::TokenDecrypt(
            "CryptUnprotectData failed".into(),
        ));
    }

    let plaintext = if output_blob.cbData > 0 && !output_blob.pbData.is_null() {
        // SAFETY: output_blob.pbData is allocated by CryptUnprotectData with
        // cbData valid bytes. We copy before freeing.
        let slice =
            unsafe { std::slice::from_raw_parts(output_blob.pbData, output_blob.cbData as usize) };
        let vec = slice.to_vec();
        // SAFETY: output_blob.pbData was allocated by LocalAlloc internally.
        unsafe { LocalFree(output_blob.pbData as _) };
        vec
    } else {
        // SAFETY: Even on empty result, free if non-null.
        if !output_blob.pbData.is_null() {
            unsafe { LocalFree(output_blob.pbData as _) };
        }
        return Err(McpBridgeError::TokenDecrypt(
            "CryptUnprotectData returned empty data".into(),
        ));
    };

    if plaintext.len() != TOKEN_SIZE {
        return Err(McpBridgeError::TokenDecrypt(format!(
            "decrypted token has wrong size: expected {TOKEN_SIZE}, got {}",
            plaintext.len()
        )));
    }

    Ok(plaintext)
}

/// Stub for non-Windows platforms — always returns an error.
#[cfg(not(windows))]
pub fn load_session_token() -> Result<Vec<u8>> {
    Err(McpBridgeError::TokenDecrypt(
        "DPAPI session token is only available on Windows".into(),
    ))
}

#[cfg(all(test, windows))]
mod tests {
    use super::*;

    #[test]
    fn session_token_path_is_non_empty() {
        let path = session_token_path();
        assert!(!path.as_os_str().is_empty());
        assert!(path.to_string_lossy().contains("Maho"));
        assert!(path.to_string_lossy().contains("mcp-session-token"));
    }

    #[test]
    fn load_session_token_fails_when_file_missing() {
        // With no browser running, the token file shouldn't exist in a test env.
        // This validates that we get a proper error, not a panic.
        let result = load_session_token();
        assert!(result.is_err());
    }
}

#[cfg(all(test, not(windows)))]
mod tests {
    use super::*;

    #[test]
    fn load_session_token_returns_error_on_non_windows() {
        let result = load_session_token();
        assert!(result.is_err());
    }
}
