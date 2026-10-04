//! Cookie import worker.
//!
//! Reads cookies from the source browser and writes them to the destination.
//!
//! For Chromium-derived browsers, v10-encrypted cookies are transparently
//! decrypted via macOS Keychain (`<Browser> Safe Storage` entry). v20 cookies
//! (Chrome 117+ App-Bound Encryption) are skipped — they require an extra
//! Win32 DPAPI hop / per-app entitlement that is out of scope here.

use std::sync::atomic::AtomicBool;
use std::sync::mpsc::Sender;
use std::sync::Arc;

use crate::orchestrator::{ImportDestination, ImportProgress, ImportType};
use crate::parsers;
use crate::parsers::chromium::cookies::EncryptionVersion;
use crate::{BrowserType, DetectedBrowser, ImportResult};

#[cfg(any(target_os = "macos", target_os = "linux"))]
use crate::decrypt::chromium_keychain::{decrypt_cookie_v10, ChromiumKey};

#[cfg(target_os = "macos")]
use super::{fetch_keychain_key_cancellable, keychain_service_account};
use super::{is_cancelled, send_update, ImportWorker};

pub struct CookieWorker;

impl ImportWorker for CookieWorker {
    fn run(
        &self,
        browser: &DetectedBrowser,
        destination: &dyn ImportDestination,
        cancelled: &Arc<AtomicBool>,
        progress: &Sender<ImportProgress>,
    ) -> ImportResult<u32> {
        let profile_dir = browser
            .profile_path
            .parent()
            .unwrap_or(&browser.profile_path);

        let mut count = 0u32;

        match browser.browser_type {
            BrowserType::Safari => {
                // Safari stores cookies in the proprietary binarycookies format
                // (plaintext values — no Keychain decryption needed).
                let cookies = parsers::safari::cookies::parse_safari_cookies(profile_dir)?;
                for cookie in &cookies {
                    if is_cancelled(cancelled) {
                        break;
                    }
                    if cookie.value.is_empty() || cookie.host.is_empty() {
                        continue;
                    }
                    destination.add_cookie(
                        &cookie.host,
                        &cookie.name,
                        &cookie.value,
                        &cookie.path,
                        cookie.expires,
                        cookie.is_secure,
                        cookie.is_httponly,
                        cookie.same_site,
                    );
                    count += 1;
                    if count % 500 == 0 {
                        send_update(progress, ImportType::Cookies, count, "Importing cookies...");
                    }
                }
            }
            BrowserType::Firefox | BrowserType::Zen => {
                let cookies = parsers::firefox::cookies::parse_firefox_cookies(profile_dir)?;
                for cookie in &cookies {
                    if is_cancelled(cancelled) {
                        break;
                    }
                    if cookie.value.is_empty() || cookie.host.is_empty() {
                        continue;
                    }
                    destination.add_cookie(
                        &cookie.host,
                        &cookie.name,
                        &cookie.value,
                        &cookie.path,
                        cookie.expires,
                        cookie.is_secure,
                        cookie.is_httponly,
                        cookie.same_site,
                    );
                    count += 1;
                    if count % 500 == 0 {
                        send_update(progress, ImportType::Cookies, count, "Importing cookies...");
                    }
                }
            }
            BrowserType::Chrome
            | BrowserType::Arc
            | BrowserType::Brave
            | BrowserType::Edge
            | BrowserType::Vivaldi
            | BrowserType::Opera => {
                let entries = parsers::chromium::cookies::parse_chromium_cookies(profile_dir)?;

                #[cfg(target_os = "macos")]
                let decryption_key: Option<ChromiumKey> = keychain_service_account(
                    browser.browser_type,
                )
                .and_then(|(service, account)| {
                    fetch_keychain_key_cancellable(
                        service,
                        account,
                        None,
                        Some(cancelled),
                    )
                });

                #[cfg(target_os = "linux")]
                let decryption_key: Option<ChromiumKey> = {
                    let browser_name = match browser.browser_type {
                        BrowserType::Chrome => "Chrome",
                        BrowserType::Brave => "Brave",
                        BrowserType::Edge => "Edge",
                        BrowserType::Vivaldi => "Vivaldi",
                        BrowserType::Opera => "Opera",
                        _ => "Chromium",
                    };
                    crate::decrypt::linux_keyring::fetch_linux_keyring_key(browser_name)
                };

                #[cfg(target_os = "windows")]
                let windows_decryptor =
                    find_local_state(profile_dir).and_then(|local_state_path| {
                        let browser_name = match browser.browser_type {
                            BrowserType::Chrome => "Chrome",
                            BrowserType::Brave => "Brave",
                            BrowserType::Edge => "Edge",
                            BrowserType::Vivaldi => "Vivaldi",
                            BrowserType::Opera => "Opera",
                            _ => "Chromium",
                        };
                        crate::decrypt::win_app_bound::WindowsAppBoundDecryptor::for_browser(
                            browser_name,
                            &local_state_path,
                        )
                        .ok()
                    });

                for entry in &entries {
                    if is_cancelled(cancelled) {
                        break;
                    }
                    let c = &entry.cookie;
                    if c.host.is_empty() {
                        continue;
                    }

                    let decrypted_value: Option<String> = if !c.value.is_empty() {
                        Some(c.value.clone())
                    } else if !entry.encrypted_value.is_empty() {
                        match entry.encryption_version {
                            EncryptionVersion::V10 => {
                                #[cfg(any(target_os = "macos", target_os = "linux"))]
                                {
                                    decryption_key.as_ref().and_then(|key| {
                                        decrypt_cookie_v10(&entry.encrypted_value, key, entry.database_version, &c.host).ok()
                                    })
                                }
                                #[cfg(target_os = "windows")]
                                {
                                    windows_decryptor.as_ref().and_then(|dec| {
                                        dec.decrypt_cookie(&entry.encrypted_value[3..]).ok()
                                    })
                                }
                                #[cfg(not(any(
                                    target_os = "macos",
                                    target_os = "linux",
                                    target_os = "windows"
                                )))]
                                {
                                    None
                                }
                            }
                            EncryptionVersion::V20 => {
                                #[cfg(target_os = "windows")]
                                {
                                    windows_decryptor.as_ref().and_then(|dec| {
                                        dec.decrypt_cookie(&entry.encrypted_value[3..]).ok()
                                    })
                                }
                                #[cfg(not(target_os = "windows"))]
                                {
                                    None
                                }
                            }
                            _ => None,
                        }
                    } else {
                        None
                    };

                    let Some(value) = decrypted_value else {
                        continue;
                    };
                    destination.add_cookie(
                        &c.host,
                        &c.name,
                        &value,
                        &c.path,
                        c.expires,
                        c.is_secure,
                        c.is_httponly,
                        c.same_site,
                    );
                    count += 1;
                    if count % 500 == 0 {
                        send_update(progress, ImportType::Cookies, count, "Importing cookies...");
                    }
                }
            }
        }
        Ok(count)
    }
}

#[cfg(target_os = "windows")]
fn find_local_state(start_dir: &std::path::Path) -> Option<std::path::PathBuf> {
    let mut current = start_dir.to_path_buf();
    for _ in 0..4 {
        let candidate = current.join("Local State");
        if candidate.exists() {
            return Some(candidate);
        }
        if let Some(parent) = current.parent() {
            current = parent.to_path_buf();
        } else {
            break;
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::workers::tests::MockDestination;
    use std::sync::mpsc;

    #[test]
    fn test_cookie_worker_nonexistent_safari() {
        let dest = Arc::new(MockDestination::new());
        let cancelled = Arc::new(AtomicBool::new(false));
        let (tx, _rx) = mpsc::channel();

        let browser = DetectedBrowser {
            browser_type: BrowserType::Safari,
            display_name: "Safari".to_string(),
            profile_path: std::path::PathBuf::from("/nonexistent/Library/Safari/Bookmarks.plist"),
            services_supported: 0xFF,
            requires_full_disk_access: false,
        };

        let result = CookieWorker.run(&browser, &*dest, &cancelled, &tx);
        assert!(result.is_err());
    }

    #[test]
    fn test_cookie_destination_interaction() {
        let dest = Arc::new(MockDestination::new());

        dest.add_cookie(".example.com", "session", "abc123", "/", 0, true, true, 1);
        dest.add_cookie(
            ".test.org",
            "pref",
            "dark",
            "/settings",
            1700000000,
            false,
            false,
            0,
        );

        let cookies = dest.cookies.lock().unwrap();
        assert_eq!(cookies.len(), 2);
        assert_eq!(cookies[0].host, ".example.com");
        assert_eq!(cookies[0].name, "session");
        assert_eq!(cookies[1].host, ".test.org");
    }

    #[test]
    fn test_cookie_worker_nonexistent_firefox() {
        let dest = Arc::new(MockDestination::new());
        let cancelled = Arc::new(AtomicBool::new(false));
        let (tx, _rx) = mpsc::channel();

        let browser = DetectedBrowser {
            browser_type: BrowserType::Firefox,
            display_name: "Firefox".to_string(),
            profile_path: std::path::PathBuf::from("/nonexistent/Default/prefs.js"),
            services_supported: 0xFF,
            requires_full_disk_access: false,
        };

        let result = CookieWorker.run(&browser, &*dest, &cancelled, &tx);
        assert!(result.is_err());
    }
}
