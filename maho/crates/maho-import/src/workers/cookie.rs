//! Cookie import worker.
//!
//! Ports `importers/cookie_importer.cc`. Reads cookies from the source browser
//! and writes them to the destination.
//!
//! NOTE: Chromium cookies with encrypted values are imported with their plaintext
//! `value` field (populated only if the cookie was unencrypted/`v10` decryptable).
//! Full Keychain decrypt support is Phase 3.

use std::sync::atomic::AtomicBool;
use std::sync::mpsc::Sender;
use std::sync::Arc;

use crate::orchestrator::{ImportDestination, ImportProgress, ImportType};
use crate::parsers;
use crate::{BrowserType, DetectedBrowser, ImportResult};

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
        // Safari cookies not supported (matches C++ behavior).
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
                for entry in &entries {
                    if is_cancelled(cancelled) {
                        break;
                    }
                    let c = &entry.cookie;
                    if c.value.is_empty() || c.host.is_empty() {
                        continue;
                    }
                    destination.add_cookie(
                        &c.host,
                        &c.name,
                        &c.value,
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
