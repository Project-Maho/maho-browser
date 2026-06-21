//! Favicon import worker.
//!
//! Ports `importers/favicon_importer.cc`. Reads favicons from the source browser
//! and writes them to the destination.

use std::sync::atomic::AtomicBool;
use std::sync::mpsc::Sender;
use std::sync::Arc;

use crate::orchestrator::{ImportDestination, ImportProgress, ImportType};
use crate::parsers;
use crate::{BrowserType, DetectedBrowser, ImportResult};

use super::{is_cancelled, send_update, ImportWorker};

pub struct FaviconWorker;

impl ImportWorker for FaviconWorker {
    fn run(
        &self,
        browser: &DetectedBrowser,
        destination: &dyn ImportDestination,
        cancelled: &Arc<AtomicBool>,
        progress: &Sender<ImportProgress>,
    ) -> ImportResult<u32> {
        // Only Chromium-based browsers supported (Firefox favicon parser not yet ported).
        match browser.browser_type {
            BrowserType::Chrome
            | BrowserType::Arc
            | BrowserType::Brave
            | BrowserType::Edge
            | BrowserType::Vivaldi
            | BrowserType::Opera => {}
            _ => return Ok(0),
        }

        let profile_dir = browser
            .profile_path
            .parent()
            .unwrap_or(&browser.profile_path);

        let favicons = parsers::chromium::favicons::parse_chromium_favicons(profile_dir)?;

        if favicons.is_empty() {
            return Ok(0);
        }

        let mut count = 0u32;
        for entry in &favicons {
            if is_cancelled(cancelled) {
                break;
            }
            // Write favicon for each associated page URL.
            for page_url in &entry.page_urls {
                destination.add_favicon(page_url, &entry.image_data);
            }
            count += 1;
            if count % 100 == 0 {
                send_update(
                    progress,
                    ImportType::Favicons,
                    count,
                    "Importing favicons...",
                );
            }
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
    fn test_favicon_worker_skips_safari() {
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

        let result = FaviconWorker.run(&browser, &*dest, &cancelled, &tx);
        assert_eq!(result.unwrap(), 0);
        let msgs: Vec<_> = rx.try_iter().collect();
        assert!(msgs.is_empty());
    }

    #[test]
    fn test_favicon_destination_interaction() {
        let dest = Arc::new(MockDestination::new());

        let png_data = vec![0x89, 0x50, 0x4E, 0x47]; // PNG magic bytes (mock)
        dest.add_favicon("https://example.com", &png_data);

        let favicons = dest.favicons.lock().unwrap();
        assert_eq!(favicons.len(), 1);
        assert_eq!(favicons[0].url, "https://example.com");
        assert_eq!(favicons[0].data_len, 4);
    }

    #[test]
    fn test_favicon_worker_nonexistent_chromium() {
        let dest = Arc::new(MockDestination::new());
        let cancelled = Arc::new(AtomicBool::new(false));
        let (tx, _rx) = mpsc::channel();

        let browser = DetectedBrowser {
            browser_type: BrowserType::Chrome,
            display_name: "Chrome".to_string(),
            profile_path: std::path::PathBuf::from("/nonexistent/Default/Preferences"),
            services_supported: 0xFF,
            requires_full_disk_access: false,
        };

        let result = FaviconWorker.run(&browser, &*dest, &cancelled, &tx);
        assert!(result.is_err());
    }
}
