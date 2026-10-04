// Isolated probe to run the FaviconWorker via Orchestrator::start_import in pure Rust.
// Bypasses C++/FFI bridge — confirms whether the cancel-at-20 is a Rust bug or C++ bug.

use maho_import::orchestrator::{ImportDestination, Orchestrator};
use maho_import::{BrowserType, DetectedBrowser};
use std::path::PathBuf;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::mpsc;
use std::sync::Arc;

#[derive(Default)]
struct CountingDest {
    favicons: AtomicU32,
}

impl ImportDestination for CountingDest {
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
    fn add_bookmark(&self, _: &str, _: &str, _: &[String]) -> Option<String> {
        None
    }
    fn add_history(&self, _: &str, _: &str, _: f64, _: u32) -> bool {
        false
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
        self.favicons.fetch_add(1, Ordering::Relaxed);
        true
    }
    fn activate_space(&self, _: &str) {}
    fn get_active_space_id(&self) -> Option<String> {
        None
    }
}

fn main() {
    let dir: PathBuf = std::env::args().nth(1).unwrap().into();
    let browser = DetectedBrowser {
        browser_type: BrowserType::Arc,
        display_name: "Arc".to_string(),
        profile_path: dir.join("Bookmarks"),
        services_supported: 0xFF,
        requires_full_disk_access: false,
    };
    let dest = Arc::new(CountingDest::default());
    let orch = Orchestrator::new(dest.clone());
    let (tx, rx) = mpsc::channel();
    const FAVICONS: u32 = 0x40;
    let h = std::thread::spawn(move || {
        let _ = orch.start_import(&browser, FAVICONS, tx);
    });
    while let Ok(p) = rx.recv() {
        eprintln!("PROGRESS: {:?}", p);
    }
    h.join().unwrap();
    eprintln!(
        "Total add_favicon calls: {}",
        dest.favicons.load(Ordering::Relaxed)
    );
}
