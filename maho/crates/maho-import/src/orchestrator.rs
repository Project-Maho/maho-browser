//! Import orchestrator — coordinates per-type workers and owns the progress channel.
//!
//! Ported from `maho-chromium/browser/importer/import_orchestrator.cc`.
//!
//! ## Design decisions
//! - Uses `std::sync::mpsc` (not tokio) to avoid forcing an async runtime on callers.
//! - `ImportDestination` trait decouples from `maho-core` for testability.
//! - INV-7 enforcement: only the orchestrator sends `AllComplete`.
//! - Workers run synchronously on the calling thread (no spawn) — the C++ side
//!   already posts to a background thread before calling into Rust.

use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::Sender;
use std::sync::Arc;

use crate::workers::{self, ImportWorker};
use crate::{DetectedBrowser, ImportResult, ImportServices, PasswordEntry};

// Re-export the trait from this module for convenience.
pub use crate::workers::ImportWorker as Worker;

/// The type of data being imported. Maps to `ImportServices` bitmask bits.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ImportType {
    History,
    Bookmarks,
    Passwords,
    Cookies,
    Autofill,
    Favicons,
    Workspaces,
    Essential,
}

impl ImportType {
    /// Convert bitmask bit to enum variant.
    pub fn from_bit(bit: u32) -> Option<Self> {
        match bit {
            ImportServices::HISTORY => Some(Self::History),
            ImportServices::BOOKMARKS => Some(Self::Bookmarks),
            ImportServices::PASSWORDS => Some(Self::Passwords),
            ImportServices::COOKIES => Some(Self::Cookies),
            ImportServices::AUTOFILL => Some(Self::Autofill),
            ImportServices::FAVICONS => Some(Self::Favicons),
            ImportServices::WORKSPACES => Some(Self::Workspaces),
            // Essential uses a separate bit (1 << 7) in our Rust side
            _ => None,
        }
    }

    /// Bitmask bit for this import type.
    pub fn to_bit(self) -> u32 {
        match self {
            Self::History => ImportServices::HISTORY,
            Self::Bookmarks => ImportServices::BOOKMARKS,
            Self::Passwords => ImportServices::PASSWORDS,
            Self::Cookies => ImportServices::COOKIES,
            Self::Autofill => ImportServices::AUTOFILL,
            Self::Favicons => ImportServices::FAVICONS,
            Self::Workspaces => ImportServices::WORKSPACES,
            Self::Essential => IMPORT_ESSENTIAL_BIT,
        }
    }
}

/// Essential import bit (not in `ImportServices` bitmask, custom for orchestrator).
pub const IMPORT_ESSENTIAL_BIT: u32 = 1 << 7;

/// Progress events sent from the orchestrator to the caller.
#[derive(Clone, Debug, PartialEq)]
pub enum ImportProgress {
    /// A specific import type is starting.
    Starting { import_type: ImportType },
    /// Progress update for an active import type.
    Update {
        import_type: ImportType,
        imported: u32,
        status: String,
    },
    /// A specific import type completed.
    TypeComplete { import_type: ImportType, count: u32 },
    /// All imports are complete. Only the orchestrator sends this (INV-7).
    AllComplete,
    /// An error occurred.
    Error { message: String },
}

/// Trait abstracting the destination for imported data.
///
/// In production, this will be implemented by the `maho-ffi` layer to call into
/// `maho-core`. For tests, use `MockDestination`.
pub trait ImportDestination: Send + Sync {
    /// Create a new space. Returns the new space ID, or None on failure.
    fn create_space(&self, name: &str, theme: &str, icon: &str) -> Option<String>;
    /// Create a tab in a space. Returns the new tab ID.
    fn create_tab(&self, space_id: &str, url: &str, title: &str) -> Option<String>;
    /// Create a tab in a specific folder. Returns the new tab ID.
    fn create_tab_in_folder(
        &self,
        space_id: &str,
        url: &str,
        title: &str,
        folder_id: &str,
    ) -> Option<String>;
    /// Create a folder. Returns the new folder ID.
    fn create_folder(&self, space_id: &str, name: &str, parent_id: &str) -> Option<String>;
    /// Pin a tab.
    fn pin_tab(&self, tab_id: &str);
    /// Favorite a tab.
    fn favorite_tab(&self, tab_id: &str);
    /// Activate a space (make it the current one).
    fn activate_space(&self, space_id: &str);
    /// Get the currently active space ID (for essential import).
    fn get_active_space_id(&self) -> Option<String>;
    /// Add a bookmark entry. Returns the new bookmark ID.
    fn add_bookmark(&self, title: &str, url: &str, folder_path: &[String]) -> Option<String>;
    /// Add a history entry. Returns true on success.
    fn add_history(&self, url: &str, title: &str, visit_time: f64, visit_count: u32) -> bool;
    /// Add a cookie. Returns true on success.
    fn add_cookie(
        &self,
        host: &str,
        name: &str,
        value: &str,
        path: &str,
        expires: i64,
        is_secure: bool,
        is_httponly: bool,
        same_site: i32,
    ) -> bool;
    /// Add an autofill entry. Returns true on success.
    fn add_autofill(
        &self,
        field_name: &str,
        value: &str,
        times_used: i32,
        first_used: i64,
        last_used: i64,
    ) -> bool;
    /// Add a favicon. Returns true on success.
    fn add_favicon(&self, url: &str, png_bytes: &[u8]) -> bool;
    /// Add a saved password (secret, never logged). Default no-op returns false.
    fn add_password(&self, entry: PasswordEntry) -> bool {
        let _ = entry;
        false
    }
}

/// The import orchestrator — coordinates workers and manages progress reporting.
pub struct Orchestrator {
    destination: Arc<dyn ImportDestination>,
    cancelled: Arc<AtomicBool>,
    selected_essentials: Vec<String>,
    password_csv_path: Option<PathBuf>,
}

impl Orchestrator {
    /// Create a new orchestrator with the given destination.
    pub fn new(destination: Arc<dyn ImportDestination>) -> Self {
        Self {
            destination,
            cancelled: Arc::new(AtomicBool::new(false)),
            selected_essentials: Vec::new(),
            password_csv_path: None,
        }
    }

    /// Set the selected essential site URLs (for welcome flow import).
    pub fn set_selected_essentials(&mut self, urls: Vec<String>) {
        self.selected_essentials = urls;
    }

    /// Set the user-provided passwords CSV path (Safari `Export → Passwords`).
    pub fn set_password_csv_path(&mut self, path: PathBuf) {
        self.password_csv_path = Some(path);
    }

    /// Start the import process. Runs workers sequentially for the requested types.
    ///
    /// The `items_bitmask` determines which import types to run, intersected with
    /// what the browser supports (`browser.services_supported`).
    ///
    /// Progress is reported via the `progress` channel sender. Only this method
    /// sends `AllComplete` (INV-7 enforcement).
    pub fn start_import(
        &self,
        browser: &DetectedBrowser,
        items_bitmask: u32,
        progress: Sender<ImportProgress>,
    ) -> ImportResult<()> {
        let effective_mask = items_bitmask & browser.services_supported;

        // Determine which types to import, in execution order.
        let types_to_import = Self::resolve_types(effective_mask);

        if types_to_import.is_empty() {
            let _ = progress.send(ImportProgress::AllComplete);
            return Ok(());
        }

        for import_type in &types_to_import {
            if self.cancelled.load(Ordering::SeqCst) {
                let _ = progress.send(ImportProgress::Error {
                    message: "Cancelled".to_string(),
                });
                let _ = progress.send(ImportProgress::AllComplete);
                return Ok(());
            }

            let _ = progress.send(ImportProgress::Starting {
                import_type: *import_type,
            });

            let result = self.run_worker(*import_type, browser, &progress);

            match result {
                Ok(count) => {
                    let _ = progress.send(ImportProgress::TypeComplete {
                        import_type: *import_type,
                        count,
                    });
                }
                Err(e) => {
                    let _ = progress.send(ImportProgress::Error {
                        message: format!("{}: {}", import_type_name(*import_type), e),
                    });
                    // Continue with next type on error (matches C++ behavior).
                }
            }
        }

        // INV-7: Only the orchestrator fires AllComplete.
        let _ = progress.send(ImportProgress::AllComplete);
        Ok(())
    }

    /// Cancel the current import. Workers check the cancellation flag.
    pub fn cancel(&self) {
        self.cancelled.store(true, Ordering::SeqCst);
    }

    /// Returns true if a cancellation has been requested.
    pub fn is_cancelled(&self) -> bool {
        self.cancelled.load(Ordering::SeqCst)
    }

    /// Resolve the bitmask to an ordered list of import types.
    fn resolve_types(mask: u32) -> Vec<ImportType> {
        let mut types = Vec::new();
        // Order: workspaces FIRST (most user-visible — defines sidebar spaces),
        // then bookmarks, history (12k+ entries are slow), cookies, autofill,
        // favicons, essential. This guarantees Arc/Zen sidebar spaces appear
        // even if a later worker stalls (e.g., Keychain prompt on cookies).
        let ordered = [
            (ImportServices::WORKSPACES, ImportType::Workspaces),
            (ImportServices::BOOKMARKS, ImportType::Bookmarks),
            (ImportServices::HISTORY, ImportType::History),
            (ImportServices::COOKIES, ImportType::Cookies),
            (ImportServices::PASSWORDS, ImportType::Passwords),
            (ImportServices::AUTOFILL, ImportType::Autofill),
            (ImportServices::FAVICONS, ImportType::Favicons),
            (IMPORT_ESSENTIAL_BIT, ImportType::Essential),
        ];
        for (bit, import_type) in ordered {
            if mask & bit != 0 {
                types.push(import_type);
            }
        }
        types
    }

    /// Run a single worker for the given import type.
    fn run_worker(
        &self,
        import_type: ImportType,
        browser: &DetectedBrowser,
        progress: &Sender<ImportProgress>,
    ) -> ImportResult<u32> {
        match import_type {
            ImportType::Bookmarks => {
                let worker = workers::bookmark::BookmarkWorker;
                worker.run(browser, &*self.destination, &self.cancelled, progress)
            }
            ImportType::History => {
                let worker = workers::history::HistoryWorker;
                worker.run(browser, &*self.destination, &self.cancelled, progress)
            }
            ImportType::Cookies => {
                let worker = workers::cookie::CookieWorker;
                worker.run(browser, &*self.destination, &self.cancelled, progress)
            }
            ImportType::Passwords => {
                let worker = workers::password::PasswordWorker::new(self.password_csv_path.clone());
                worker.run(browser, &*self.destination, &self.cancelled, progress)
            }
            ImportType::Autofill => {
                let worker = workers::autofill::AutofillWorker;
                worker.run(browser, &*self.destination, &self.cancelled, progress)
            }
            ImportType::Favicons => {
                let worker = workers::favicon::FaviconWorker;
                worker.run(browser, &*self.destination, &self.cancelled, progress)
            }
            ImportType::Workspaces => {
                let worker = workers::workspace::WorkspaceWorker;
                worker.run(browser, &*self.destination, &self.cancelled, progress)
            }
            ImportType::Essential => {
                let worker =
                    workers::essential::EssentialWorker::new(self.selected_essentials.clone());
                worker.run(browser, &*self.destination, &self.cancelled, progress)
            }
        }
    }
}

fn import_type_name(t: ImportType) -> &'static str {
    match t {
        ImportType::History => "history",
        ImportType::Bookmarks => "bookmarks",
        ImportType::Passwords => "passwords",
        ImportType::Cookies => "cookies",
        ImportType::Autofill => "autofill",
        ImportType::Favicons => "favicons",
        ImportType::Workspaces => "workspaces",
        ImportType::Essential => "essential",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::workers::tests::MockDestination;
    use crate::BrowserType;
    use std::path::PathBuf;
    use std::sync::mpsc;

    fn make_browser(browser_type: BrowserType, services: u32) -> DetectedBrowser {
        DetectedBrowser {
            browser_type,
            display_name: "Test Browser".to_string(),
            profile_path: PathBuf::from("/nonexistent/profile"),
            services_supported: services,
            requires_full_disk_access: false,
        }
    }

    #[test]
    fn test_empty_mask_sends_all_complete_immediately() {
        let dest = Arc::new(MockDestination::new());
        let orch = Orchestrator::new(dest);
        let (tx, rx) = mpsc::channel();

        orch.start_import(&make_browser(BrowserType::Chrome, 0xFF), 0, tx)
            .unwrap();

        let msgs: Vec<_> = rx.try_iter().collect();
        assert_eq!(msgs.len(), 1);
        assert_eq!(msgs[0], ImportProgress::AllComplete);
    }

    #[test]
    fn test_unsupported_services_filtered() {
        // Browser supports only history, but we request bookmarks+history
        let dest = Arc::new(MockDestination::new());
        let orch = Orchestrator::new(dest);
        let (tx, rx) = mpsc::channel();

        let browser = make_browser(BrowserType::Chrome, ImportServices::HISTORY);
        // Request bookmarks + history, only history should run
        orch.start_import(
            &browser,
            ImportServices::BOOKMARKS | ImportServices::HISTORY,
            tx,
        )
        .unwrap();

        let msgs: Vec<_> = rx.try_iter().collect();
        // Should see Starting(History), then an error (file not found) or TypeComplete, then AllComplete
        assert!(msgs.iter().any(|m| matches!(
            m,
            ImportProgress::Starting {
                import_type: ImportType::History
            }
        )));
        assert!(!msgs.iter().any(|m| matches!(
            m,
            ImportProgress::Starting {
                import_type: ImportType::Bookmarks
            }
        )));
        assert_eq!(msgs.last(), Some(&ImportProgress::AllComplete));
    }

    #[test]
    fn test_cancellation_stops_further_work() {
        let dest = Arc::new(MockDestination::new());
        let orch = Orchestrator::new(dest);
        let (tx, rx) = mpsc::channel();

        // Cancel before starting
        orch.cancel();

        let browser = make_browser(
            BrowserType::Chrome,
            ImportServices::BOOKMARKS | ImportServices::HISTORY,
        );
        orch.start_import(
            &browser,
            ImportServices::BOOKMARKS | ImportServices::HISTORY,
            tx,
        )
        .unwrap();

        let msgs: Vec<_> = rx.try_iter().collect();
        // Should get Error("Cancelled") + AllComplete, nothing else
        assert!(msgs
            .iter()
            .any(|m| matches!(m, ImportProgress::Error { message } if message == "Cancelled")));
        assert_eq!(msgs.last(), Some(&ImportProgress::AllComplete));
        // No Starting events
        assert!(!msgs
            .iter()
            .any(|m| matches!(m, ImportProgress::Starting { .. })));
    }

    #[test]
    fn test_mid_import_cancellation_terminates_early() {
        use std::sync::atomic::AtomicU32;
        use std::time::{Duration, Instant};

        struct SlowDestination {
            history_calls: AtomicU32,
            sleep_per_call: Duration,
        }
        impl ImportDestination for SlowDestination {
            fn create_space(&self, _: &str, _: &str, _: &str) -> Option<String> {
                Some("s".into())
            }
            fn create_tab(&self, _: &str, _: &str, _: &str) -> Option<String> {
                Some("t".into())
            }
            fn create_tab_in_folder(&self, _: &str, _: &str, _: &str, _: &str) -> Option<String> {
                Some("t".into())
            }
            fn create_folder(&self, _: &str, _: &str, _: &str) -> Option<String> {
                Some("f".into())
            }
            fn pin_tab(&self, _: &str) {}
            fn favorite_tab(&self, _: &str) {}
            fn activate_space(&self, _: &str) {}
            fn get_active_space_id(&self) -> Option<String> {
                None
            }
            fn add_bookmark(&self, _: &str, _: &str, _: &[String]) -> Option<String> {
                None
            }
            fn add_history(&self, _: &str, _: &str, _: f64, _: u32) -> bool {
                self.history_calls.fetch_add(1, Ordering::SeqCst);
                std::thread::sleep(self.sleep_per_call);
                true
            }
            fn add_cookie(
                &self,
                _: &str,
                _: &str,
                _: &str,
                _: &str,
                _: i64,
                _: bool,
                _: bool,
                _: i32,
            ) -> bool {
                false
            }
            fn add_autofill(&self, _: &str, _: &str, _: i32, _: i64, _: i64) -> bool {
                false
            }
            fn add_favicon(&self, _: &str, _: &[u8]) -> bool {
                false
            }
        }

        let dest = Arc::new(SlowDestination {
            history_calls: AtomicU32::new(0),
            sleep_per_call: Duration::from_millis(50),
        });
        let dest_ref = dest.clone();
        let orch = Arc::new(Orchestrator::new(dest));
        let orch_for_thread = orch.clone();
        let orch_for_cancel = orch.clone();

        let (tx, rx) = mpsc::channel();
        let browser = make_browser(BrowserType::Chrome, ImportServices::HISTORY);

        let import_start = Instant::now();

        let worker = std::thread::spawn(move || {
            orch_for_thread.start_import(&browser, ImportServices::HISTORY, tx)
        });

        std::thread::sleep(Duration::from_millis(200));
        orch_for_cancel.cancel();

        let _ = worker.join().expect("worker thread completes");
        let elapsed = import_start.elapsed();

        let msgs: Vec<_> = rx.try_iter().collect();

        assert!(
            elapsed < Duration::from_secs(5),
            "cancel should terminate quickly, took {:?}",
            elapsed
        );
        let calls = dest_ref.history_calls.load(Ordering::SeqCst);
        assert!(
            calls < 1000,
            "mid-import cancel should stop history additions early, got {} calls",
            calls
        );
        assert_eq!(
            msgs.last(),
            Some(&ImportProgress::AllComplete),
            "orchestrator must always emit AllComplete (INV-7), even on cancel"
        );
    }

    #[test]
    fn test_inv7_only_orchestrator_sends_all_complete() {
        let dest = Arc::new(MockDestination::new());
        let orch = Orchestrator::new(dest);
        let (tx, rx) = mpsc::channel();

        let browser = make_browser(BrowserType::Chrome, 0xFF);
        // Import with nonexistent paths — workers will error but AllComplete still fires exactly once
        orch.start_import(
            &browser,
            ImportServices::HISTORY | ImportServices::BOOKMARKS,
            tx,
        )
        .unwrap();

        let msgs: Vec<_> = rx.try_iter().collect();
        let all_complete_count = msgs
            .iter()
            .filter(|m| matches!(m, ImportProgress::AllComplete))
            .count();
        assert_eq!(all_complete_count, 1, "AllComplete must fire exactly once");
    }

    #[test]
    fn test_resolve_types_order() {
        let mask = ImportServices::HISTORY
            | ImportServices::BOOKMARKS
            | ImportServices::PASSWORDS
            | ImportServices::COOKIES
            | ImportServices::AUTOFILL
            | ImportServices::WORKSPACES
            | ImportServices::FAVICONS
            | IMPORT_ESSENTIAL_BIT;
        let types = Orchestrator::resolve_types(mask);
        assert_eq!(
            types,
            vec![
                ImportType::Workspaces,
                ImportType::Bookmarks,
                ImportType::History,
                ImportType::Cookies,
                ImportType::Passwords,
                ImportType::Autofill,
                ImportType::Favicons,
                ImportType::Essential,
            ]
        );
    }

    #[test]
    fn test_passwords_bit_roundtrip() {
        assert_eq!(
            ImportType::from_bit(ImportServices::PASSWORDS),
            Some(ImportType::Passwords)
        );
        assert_eq!(ImportType::Passwords.to_bit(), ImportServices::PASSWORDS);
    }

    #[test]
    fn test_orchestrator_imports_passwords_from_csv_path() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("safari-passwords.csv");
        std::fs::write(
            &path,
            "Title,URL,Username,Password,Notes,OTPAuth\n\
             Apple,https://apple.com,user@example.com,hunter2,,\n\
             GitHub,https://github.com,octocat,s3cr3t,,\n",
        )
        .unwrap();

        let dest = Arc::new(MockDestination::new());
        let mut orch = Orchestrator::new(dest.clone());
        orch.set_password_csv_path(path);

        let browser = make_browser(BrowserType::Safari, ImportServices::PASSWORDS);
        let (tx, rx) = mpsc::channel();
        orch.start_import(&browser, ImportServices::PASSWORDS, tx)
            .unwrap();

        let msgs: Vec<_> = rx.try_iter().collect();
        assert!(msgs.iter().any(|m| matches!(
            m,
            ImportProgress::Starting {
                import_type: ImportType::Passwords
            }
        )));
        assert!(msgs.iter().any(|m| matches!(
            m,
            ImportProgress::TypeComplete {
                import_type: ImportType::Passwords,
                count: 2
            }
        )));
        assert_eq!(msgs.last(), Some(&ImportProgress::AllComplete));

        let passwords = dest.passwords.lock().unwrap();
        assert_eq!(passwords.len(), 2);
        assert_eq!(passwords[0].origin_url, "https://apple.com");
        assert_eq!(passwords[0].username, "user@example.com");
        assert_eq!(passwords[1].origin_url, "https://github.com");
    }
}
