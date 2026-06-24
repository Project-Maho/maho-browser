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

#[cfg(target_os = "macos")]
use crate::decrypt::chromium_keychain::{
    decrypt_cookie_v10, fetch_and_derive_key, ChromiumKey, MacOsKeychain,
};

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
                        fetch_and_derive_key(&MacOsKeychain, service, account).ok()
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
                    } else if entry.encryption_version == EncryptionVersion::V10
                        && !entry.encrypted_value.is_empty()
                    {
                        #[cfg(target_os = "macos")]
                        {
                            decryption_key
                                .as_ref()
                                .and_then(|key| decrypt_cookie_v10(&entry.encrypted_value, key).ok())
                        }
                        #[cfg(not(target_os = "macos"))]
                        {
                            None
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
