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
use crate::decrypt::chromium_keychain::{fetch_and_derive_key, MacOsKeychain};

use super::{is_cancelled, send_update, ImportWorker};

pub struct CookieWorker;

#[cfg(target_os = "macos")]
fn keychain_service_account(browser_type: BrowserType) -> Option<(&'static str, &'static str)> {
    match browser_type {
        BrowserType::Chrome => Some(("Chrome Safe Storage", "Chrome")),
        BrowserType::Brave => Some(("Brave Safe Storage", "Brave")),
        BrowserType::Arc => Some(("Arc Safe Storage", "Arc")),
        BrowserType::Edge => Some(("Microsoft Edge Safe Storage", "Microsoft Edge")),
        BrowserType::Vivaldi => Some(("Vivaldi Safe Storage", "Vivaldi")),
        BrowserType::Opera => Some(("Opera Safe Storage", "Opera")),
        _ => None,
    }
}

/// Fetches a Keychain entry on a background thread with a timeout.
///
/// macOS Security framework's `SecKeychainFindGenericPassword` blocks the
/// caller indefinitely when the process is ad-hoc signed / lacks a stable ACL
/// (the system prompt may be hidden behind the welcome window). Wrapping the
/// call in a worker thread + recv_timeout lets us abandon the attempt after
/// `timeout` and proceed with the rest of the import.
///
/// SAFETY: the spawned thread is leaked on timeout — the Keychain call will
/// eventually return and drop the receiver send. This is acceptable because
/// the worker is invoked at most once per import session.
#[cfg(target_os = "macos")]
fn fetch_keychain_with_timeout(
    service: &'static str,
    account: &'static str,
    timeout: std::time::Duration,
) -> Option<ChromiumKey> {
    let (tx, rx) = std::sync::mpsc::channel();
    std::thread::Builder::new()
        .name(format!("maho-import-keychain-{}", service))
        .spawn(move || {
            let result = fetch_and_derive_key(&MacOsKeychain, service, account).ok();
            let _ = tx.send(result);
        })
        .ok()?;
    rx.recv_timeout(timeout).ok().flatten()
}

impl ImportWorker for CookieWorker {
    fn run(
        &self,
        browser: &DetectedBrowser,
        destination: &dyn ImportDestination,
        cancelled: &Arc<AtomicBool>,
        progress: &Sender<ImportProgress>,
    ) -> ImportResult<u32> {
        if browser.browser_type == BrowserType::Safari {
            return Ok(0);
        }

        let profile_dir = browser
            .profile_path
            .parent()
            .unwrap_or(&browser.profile_path);

        let mut count = 0u32;

        match browser.browser_type {
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
                let decryption_key: Option<ChromiumKey> = keychain_service_account(browser.browser_type)
                    .and_then(|(service, account)| {
                        fetch_keychain_with_timeout(service, account, std::time::Duration::from_secs(5))
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
                let windows_decryptor = find_local_state(profile_dir)
                    .and_then(|local_state_path| {
                        let browser_name = match browser.browser_type {
                            BrowserType::Chrome => "Chrome",
                            BrowserType::Brave => "Brave",
                            BrowserType::Edge => "Edge",
                            BrowserType::Vivaldi => "Vivaldi",
                            BrowserType::Opera => "Opera",
                            _ => "Chromium",
                        };
                        crate::decrypt::win_app_bound::WindowsAppBoundDecryptor::for_browser(browser_name, &local_state_path).ok()
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
                                    decryption_key
                                        .as_ref()
                                        .and_then(|key| decrypt_cookie_v10(&entry.encrypted_value, key).ok())
                                }
                                #[cfg(target_os = "windows")]
                                {
                                    windows_decryptor
                                        .as_ref()
                                        .and_then(|dec| dec.decrypt_cookie(&entry.encrypted_value[3..]).ok())
                                }
                                #[cfg(not(any(target_os = "macos", target_os = "linux", target_os = "windows")))]
                                {
                                    None
                                }
                            }
                            EncryptionVersion::V20 => {
                                #[cfg(target_os = "windows")]
                                {
                                    windows_decryptor
                                        .as_ref()
                                        .and_then(|dec| dec.decrypt_cookie(&entry.encrypted_value[3..]).ok())
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

                    let Some(value) = decrypted_value else { continue };
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
            _ => {}
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
    fn test_cookie_worker_skips_safari() {
        let dest = Arc::new(MockDestination::new());
        let cancelled = Arc::new(AtomicBool::new(false));
        let (tx, rx) = mpsc::channel();

        let browser = DetectedBrowser {
            browser_type: BrowserType::Safari,
            display_name: "Safari".to_string(),
            profile_path: std::path::PathBuf::from("/nonexistent/profile"),
            services_supported: 0xFF,
            requires_full_disk_access: false,
        };

        let result = CookieWorker.run(&browser, &*dest, &cancelled, &tx);
        assert_eq!(result.unwrap(), 0);
        let msgs: Vec<_> = rx.try_iter().collect();
        assert!(msgs.is_empty());
    }

    #[test]
    fn test_cookie_destination_interaction() {
        let dest = Arc::new(MockDestination::new());

        dest.add_cookie(".example.com", "session", "abc123", "/", 0, true, true, 1);
        dest.add_cookie(".test.org", "pref", "dark", "/settings", 1700000000, false, false, 0);

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
