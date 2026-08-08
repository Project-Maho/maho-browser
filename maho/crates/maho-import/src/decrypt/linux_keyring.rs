// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Linux cookie/password decryption key retriever using Secret Service (libsecret/kwallet).

#[cfg(target_os = "linux")]
use crate::decrypt::chromium_keychain::derive_aes_key;
use crate::decrypt::chromium_keychain::ChromiumKey;

/// Retrieves the Chromium encryption key from the system keyring on Linux.
#[cfg(target_os = "linux")]
pub fn fetch_linux_keyring_key(browser_name: &str) -> Option<ChromiumKey> {
    // Standard Chromium service is usually "Chrome Safe Storage" or "Chromium Safe Storage"
    // with account "Chrome" or "Chromium".
    let service_name = match browser_name {
        "Chrome" | "google-chrome" => "Chrome Safe Storage",
        "Brave" | "brave-browser" => "Brave Safe Storage",
        "Edge" | "microsoft-edge" => "Microsoft Edge Safe Storage",
        "Vivaldi" | "vivaldi-stable" => "Vivaldi Safe Storage",
        "Opera" | "opera" => "Opera Safe Storage",
        _ => "Chromium Safe Storage",
    };
    let account_name = match browser_name {
        "Chrome" | "google-chrome" => "Chrome",
        "Brave" | "brave-browser" => "Brave",
        "Edge" | "microsoft-edge" => "Microsoft Edge",
        "Vivaldi" | "vivaldi-stable" => "Vivaldi",
        "Opera" | "opera" => "Opera",
        _ => "Chromium",
    };

    // Try to get password from Freedesktop Secret Service API via `secret-service` crate
    if let Ok(ss) =
        secret_service::blocking::SecretService::connect(secret_service::EncryptionType::Dh)
    {
        if let Ok(collection) = ss.get_default_collection() {
            let search_attributes = [("application", service_name)];
            // Secret Service search might match by application attribute
            if let Ok(items) = collection.search_items(search_attributes.iter().cloned().collect())
            {
                for item in items {
                    if let Ok(secret) = item.get_secret() {
                        if !secret.is_empty() {
                            return Some(derive_aes_key(&secret));
                        }
                    }
                }
            }
        }
    }

    // KWallet fallback via kwallet-query CLI tool
    // Command: kwallet-query -r "Chrome Safe Storage" -d "kdewallet" -f "Chrome Keys"
    let folder = format!("{} Keys", account_name);
    let key = service_name;
    if let Ok(output) = std::process::Command::new("kwallet-query")
        .args(&["-r", key, "-d", "kdewallet", "-f", &folder])
        .output()
    {
        if output.status.success() {
            let stdout = String::from_utf8_lossy(&output.stdout);
            let trimmed = stdout.trim();
            if !trimmed.is_empty() {
                return Some(derive_aes_key(trimmed.as_bytes()));
            }
        }
    }

    // Default fallback when keyring is not available: "peanuts"
    // See Chromium's os_crypt_linux.cc: fallback password is "peanuts"
    Some(derive_aes_key(b"peanuts"))
}

#[cfg(not(target_os = "linux"))]
pub fn fetch_linux_keyring_key(_browser_name: &str) -> Option<ChromiumKey> {
    None
}
