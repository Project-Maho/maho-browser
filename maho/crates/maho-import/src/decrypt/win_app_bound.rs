// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Windows cookie decryption using DPAPI and Chrome's COM elevator service.

#![cfg(any(windows, test))]

use aes_gcm::aead::{Aead, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce};
#[cfg(windows)]
use serde_json::Value;
#[cfg(windows)]
use std::path::Path;
#[cfg(windows)]
use std::ptr;
#[cfg(windows)]
use windows::core::{IUnknown, Interface, GUID};
#[cfg(windows)]
use windows::Win32::Foundation::{LocalFree, HLOCAL};
#[cfg(windows)]
use windows::Win32::Security::Cryptography::{CryptUnprotectData, CRYPT_INTEGER_BLOB};
#[cfg(windows)]
use windows::Win32::System::Com::{
    CoCreateInstance, CoInitializeEx, CLSCTX_LOCAL_SERVER, COINIT_MULTITHREADED,
};

/// Decryptor for Windows Chrome / Edge / Brave cookies.
pub struct WindowsAppBoundDecryptor {
    aes_key: Vec<u8>,
}

impl WindowsAppBoundDecryptor {
    /// Attempts to decrypt the AES key from the browser's Local State.
    ///
    /// Tries the IElevator2 (Chrome 144+ v2) COM interface first, falling back
    /// to IElevator (v1) if available, and finally falling back to user-context DPAPI
    /// (for browsers that do not use App-Bound encryption).
    #[cfg(windows)]
    pub fn for_browser(browser_name: &str, local_state_path: &Path) -> Result<Self, String> {
        let _ = unsafe { CoInitializeEx(None, COINIT_MULTITHREADED) };

        let content = std::fs::read_to_string(local_state_path).map_err(|e| e.to_string())?;
        let json: Value = serde_json::from_str(&content).map_err(|e| e.to_string())?;

        // 1. Try to load the app_bound_encrypted_key (v20 Chrome/Edge/Brave)
        if let Some(app_bound_b64) = json["os_crypt"]["app_bound_encrypted_key"].as_str() {
            use base64::Engine as _;
            let encrypted_key = base64::engine::general_purpose::STANDARD
                .decode(app_bound_b64)
                .map_err(|e| e.to_string())?;

            if encrypted_key.starts_with(b"APPB") {
                let payload = &encrypted_key[4..];
                // Try COM decryption
                match decrypt_via_com(browser_name, payload) {
                    Ok(key) => return Ok(Self { aes_key: key }),
                    Err(e) => {
                        eprintln!("[maho-import] App-bound COM decryption failed for {}: {}. Falling back to DPAPI.", browser_name, e);
                    }
                }
            }
        }

        // 2. Fall back to standard DPAPI (v10 / pre-Chrome-117 key format)
        if let Some(encrypted_key_b64) = json["os_crypt"]["encrypted_key"].as_str() {
            use base64::Engine as _;
            let encrypted_key = base64::engine::general_purpose::STANDARD
                .decode(encrypted_key_b64)
                .map_err(|e| e.to_string())?;

            if encrypted_key.starts_with(b"DPAPI") {
                let payload = &encrypted_key[5..];
                let input_blob = CRYPT_INTEGER_BLOB {
                    cbData: payload.len() as u32,
                    pbData: payload.as_ptr() as *mut u8,
                };
                let mut output_blob = CRYPT_INTEGER_BLOB {
                    cbData: 0,
                    pbData: ptr::null_mut(),
                };

                let ok = unsafe {
                    CryptUnprotectData(&input_blob, None, None, None, None, 0, &mut output_blob)
                };

                if ok.is_ok() {
                    let key = unsafe {
                        let slice = std::slice::from_raw_parts(
                            output_blob.pbData,
                            output_blob.cbData as usize,
                        );
                        let vec = slice.to_vec();
                        let _ = LocalFree(HLOCAL(output_blob.pbData as *mut std::ffi::c_void));
                        vec
                    };
                    return Ok(Self { aes_key: key });
                }
            }
        }

        Err(format!(
            "Could not extract decryption key for browser {}",
            browser_name
        ))
    }

    /// Decrypts a cookie value using AES-256-GCM.
    pub fn decrypt_cookie(&self, encrypted_value: &[u8]) -> Result<String, String> {
        // Windows cookie format for GCM:
        // ciphertext starts with 3-byte prefix (e.g. "v10" or "v20"), which is stripped.
        // The remaining payload is: 12-byte IV + ciphertext + 16-byte authentication tag.
        if encrypted_value.len() < 12 + 16 {
            return Err("Encrypted cookie payload too short".into());
        }

        let iv = &encrypted_value[..12];
        let payload = &encrypted_value[12..];

        let cipher = Aes256Gcm::new_from_slice(&self.aes_key)
            .map_err(|_| "Invalid AES-256 key length".to_string())?;
        let nonce = Nonce::from_slice(iv);

        let decrypted = cipher
            .decrypt(nonce, payload)
            .map_err(|e| format!("AES-GCM decryption failed: {}", e))?;

        String::from_utf8(decrypted)
            .map_err(|e| format!("Invalid UTF-8 in decrypted cookie: {}", e))
    }
}

#[cfg(test)]
mod review_tests {
    use super::*;

    #[test]
    fn review_invalid_windows_key_returns_error_without_panicking() {
        for len in [0, 16, 31, 33] {
            let decryptor = WindowsAppBoundDecryptor { aes_key: vec![0; len] };
            assert!(decryptor.decrypt_cookie(&[0; 28]).is_err());
        }
    }
}

// Browser config containing CLSID and vtable slot offset for IElevator.
#[cfg(windows)]
struct BrowserComConfig {
    clsid: &'static str,
    iid_v1: &'static str,
    iid_v2: &'static str,
    vtable_slot: usize,
}

#[cfg(windows)]
fn get_browser_config(browser_name: &str) -> Option<BrowserComConfig> {
    match browser_name {
        "Chrome" | "google-chrome" => Some(BrowserComConfig {
            clsid: "708860E0-F641-4611-8895-7D867DD3675B",
            iid_v1: "A99222FE-D225-45C6-896B-D9A3B5EE5250",
            iid_v2: "8F7B6792-784D-4047-845D-1782EFBEF205",
            vtable_slot: 5,
        }),
        "Edge" | "microsoft-edge" => Some(BrowserComConfig {
            clsid: "1FCBE96C-1697-43AF-9140-2897C7C69767",
            iid_v1: "A99222FE-D225-45C6-896B-D9A3B5EE5250",
            iid_v2: "8F7B6792-784D-4047-845D-1782EFBEF205",
            vtable_slot: 8,
        }),
        "Brave" | "brave-browser" => Some(BrowserComConfig {
            clsid: "576B31AF-6369-4B6B-8560-E4B203A97A8B",
            iid_v1: "A99222FE-D225-45C6-896B-D9A3B5EE5250",
            iid_v2: "8F7B6792-784D-4047-845D-1782EFBEF205",
            vtable_slot: 5,
        }),
        _ => None,
    }
}

#[cfg(windows)]
fn decrypt_via_com(browser_name: &str, encrypted_key: &[u8]) -> Result<Vec<u8>, String> {
    let config = get_browser_config(browser_name)
        .ok_or_else(|| format!("COM configuration missing for browser {}", browser_name))?;

    unsafe {
        let clsid = GUID::from(config.clsid);
        let iid_v2 = GUID::from(config.iid_v2);
        let iid_v1 = GUID::from(config.iid_v1);

        let unknown: IUnknown = CoCreateInstance(&clsid, None, CLSCTX_LOCAL_SERVER)
            .map_err(|e| format!("CoCreateInstance failed: {}", e))?;

        // Query for the IElevator interface (v2 first, then v1 fallback).
        let mut elevator_ptr: *mut std::ffi::c_void = std::ptr::null_mut();
        let mut hr = unknown.query(&iid_v2, &mut elevator_ptr);
        if hr.is_err() {
            hr = unknown.query(&iid_v1, &mut elevator_ptr);
        }

        if hr.is_err() || elevator_ptr.is_null() {
            return Err(format!(
                "QueryInterface for IElevator failed with HRESULT 0x{:08X}",
                hr.0
            ));
        }

        let result = call_decrypt_data(elevator_ptr, config.vtable_slot, encrypted_key);

        // Balance the reference added by QueryInterface; `unknown` releases on drop.
        let _ = IUnknown::from_raw(elevator_ptr);

        result
    }
}

#[cfg(windows)]
unsafe fn call_decrypt_data(
    unknown_ptr: *mut std::ffi::c_void,
    slot: usize,
    input: &[u8],
) -> Result<Vec<u8>, String> {
    let vtable_ptr = *(unknown_ptr as *mut *mut *mut std::ffi::c_void);
    let func_ptr = *vtable_ptr.add(slot);

    type DecryptDataFn = unsafe extern "system" fn(
        this: *mut std::ffi::c_void,
        input_buffer: *const u8,
        input_buffer_size: u32,
        output_buffer: *mut *mut u8,
        output_buffer_size: *mut u32,
        last_error: *mut i32,
    ) -> i32;

    let decrypt_fn: DecryptDataFn = std::mem::transmute(func_ptr);

    let mut output_ptr: *mut u8 = std::ptr::null_mut();
    let mut output_size: u32 = 0;
    let mut last_error: i32 = 0;

    let hr = decrypt_fn(
        unknown_ptr,
        input.as_ptr(),
        input.len() as u32,
        &mut output_ptr,
        &mut output_size,
        &mut last_error,
    );

    if hr < 0 {
        return Err(format!(
            "COM DecryptData failed with HRESULT 0x{:08X}, last_error={}",
            hr, last_error
        ));
    }

    if output_ptr.is_null() || output_size == 0 {
        return Err("COM DecryptData returned null or empty output".into());
    }

    let result = std::slice::from_raw_parts(output_ptr, output_size as usize).to_vec();
    let _ = LocalFree(HLOCAL(output_ptr as *mut std::ffi::c_void));

    Ok(result)
}
