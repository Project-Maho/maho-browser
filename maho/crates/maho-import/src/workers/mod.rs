//! Import workers — one per data type.
//!
//! Each worker implements `ImportWorker` and handles parsing + writing to the
//! `ImportDestination`. Workers NEVER send `AllComplete` (INV-7).

pub mod autofill;
pub mod bookmark;
pub mod cookie;
pub mod essential;
pub mod favicon;
pub mod history;
pub mod workspace;

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::Sender;
use std::sync::Arc;

use crate::orchestrator::{ImportProgress, ImportType};
use crate::{DetectedBrowser, ImportResult};

use super::orchestrator::ImportDestination;

/// Trait for import workers. Each worker handles one data type.
pub trait ImportWorker {
    /// Run the import. Returns the number of items imported.
    ///
    /// Workers may send `Update` progress events but MUST NOT send
    /// `AllComplete` or `TypeComplete` — that's the orchestrator's job (INV-7).
    fn run(
        &self,
        browser: &DetectedBrowser,
        destination: &dyn ImportDestination,
        cancelled: &Arc<AtomicBool>,
        progress: &Sender<ImportProgress>,
    ) -> ImportResult<u32>;
}

/// Helper: check if cancelled.
#[inline]
pub(crate) fn is_cancelled(flag: &AtomicBool) -> bool {
    flag.load(Ordering::SeqCst)
}

/// Helper: send a progress update (non-fatal if receiver dropped).
#[inline]
pub(crate) fn send_update(
    progress: &Sender<ImportProgress>,
    import_type: ImportType,
    imported: u32,
    status: &str,
) {
    let _ = progress.send(ImportProgress::Update {
        import_type,
        imported,
        status: status.to_string(),
    });
}

#[cfg(test)]
pub mod tests {
    use super::super::orchestrator::ImportDestination;
    use std::sync::Mutex;

    /// Mock destination for testing workers.
    #[derive(Debug)]
    pub struct MockDestination {
        pub spaces: Mutex<Vec<MockSpace>>,
        pub tabs: Mutex<Vec<MockTab>>,
        pub folders: Mutex<Vec<MockFolder>>,
        pub bookmarks: Mutex<Vec<MockBookmark>>,
        pub history: Mutex<Vec<MockHistory>>,
        pub cookies: Mutex<Vec<MockCookie>>,
        pub autofill: Mutex<Vec<MockAutofill>>,
        pub favicons: Mutex<Vec<MockFavicon>>,
        pub pinned: Mutex<Vec<String>>,
        pub favorited: Mutex<Vec<String>>,
        pub active_space: Mutex<Option<String>>,
        next_id: Mutex<u32>,
    }

    #[derive(Debug, Clone)]
    pub struct MockSpace {
        pub id: String,
        pub name: String,
        pub theme: String,
        pub icon: String,
    }

    #[derive(Debug, Clone)]
    pub struct MockTab {
        pub id: String,
        pub space_id: String,
        pub url: String,
        pub folder_id: Option<String>,
    }

    #[derive(Debug, Clone)]
    pub struct MockFolder {
        pub id: String,
        pub space_id: String,
        pub name: String,
        pub parent_id: String,
    }

    #[derive(Debug, Clone)]
    pub struct MockBookmark {
        pub title: String,
        pub url: String,
        pub folder_path: Vec<String>,
    }

    #[derive(Debug, Clone)]
    pub struct MockHistory {
        pub url: String,
        pub title: String,
        pub visit_time: f64,
        pub visit_count: u32,
    }

    #[derive(Debug, Clone)]
    pub struct MockCookie {
        pub host: String,
        pub name: String,
        pub value: String,
        pub path: String,
    }

    #[derive(Debug, Clone)]
    pub struct MockAutofill {
        pub field_name: String,
        pub value: String,
    }

    #[derive(Debug, Clone)]
    pub struct MockFavicon {
        pub url: String,
        pub data_len: usize,
    }

    impl MockDestination {
        pub fn new() -> Self {
            Self {
                spaces: Mutex::new(Vec::new()),
                tabs: Mutex::new(Vec::new()),
                folders: Mutex::new(Vec::new()),
                bookmarks: Mutex::new(Vec::new()),
                history: Mutex::new(Vec::new()),
                cookies: Mutex::new(Vec::new()),
                autofill: Mutex::new(Vec::new()),
                favicons: Mutex::new(Vec::new()),
                pinned: Mutex::new(Vec::new()),
                favorited: Mutex::new(Vec::new()),
                active_space: Mutex::new(Some("default-space".to_string())),
                next_id: Mutex::new(1),
            }
        }

        fn next_id(&self) -> String {
            let mut id = self.next_id.lock().unwrap();
            let val = format!("id-{}", *id);
            *id += 1;
            val
        }
    }

    impl ImportDestination for MockDestination {
        fn create_space(&self, name: &str, theme: &str, icon: &str) -> Option<String> {
            let id = self.next_id();
            self.spaces.lock().unwrap().push(MockSpace {
                id: id.clone(),
                name: name.to_string(),
                theme: theme.to_string(),
                icon: icon.to_string(),
            });
            Some(id)
        }

        fn create_tab(&self, space_id: &str, url: &str) -> Option<String> {
            let id = self.next_id();
            self.tabs.lock().unwrap().push(MockTab {
                id: id.clone(),
                space_id: space_id.to_string(),
                url: url.to_string(),
                folder_id: None,
            });
            Some(id)
        }

        fn create_tab_in_folder(
            &self,
            space_id: &str,
            url: &str,
            folder_id: &str,
        ) -> Option<String> {
            let id = self.next_id();
            self.tabs.lock().unwrap().push(MockTab {
                id: id.clone(),
                space_id: space_id.to_string(),
                url: url.to_string(),
                folder_id: Some(folder_id.to_string()),
            });
            Some(id)
        }

        fn create_folder(&self, space_id: &str, name: &str, parent_id: &str) -> Option<String> {
            let id = self.next_id();
            self.folders.lock().unwrap().push(MockFolder {
                id: id.clone(),
                space_id: space_id.to_string(),
                name: name.to_string(),
                parent_id: parent_id.to_string(),
            });
            Some(id)
        }

        fn pin_tab(&self, tab_id: &str) {
            self.pinned.lock().unwrap().push(tab_id.to_string());
        }

        fn favorite_tab(&self, tab_id: &str) {
            self.favorited.lock().unwrap().push(tab_id.to_string());
        }

        fn activate_space(&self, space_id: &str) {
            *self.active_space.lock().unwrap() = Some(space_id.to_string());
        }

        fn get_active_space_id(&self) -> Option<String> {
            self.active_space.lock().unwrap().clone()
        }

        fn add_bookmark(&self, title: &str, url: &str, folder_path: &[String]) -> Option<String> {
            let id = self.next_id();
            self.bookmarks.lock().unwrap().push(MockBookmark {
                title: title.to_string(),
                url: url.to_string(),
                folder_path: folder_path.to_vec(),
            });
            Some(id)
        }

        fn add_history(
            &self,
            url: &str,
            title: &str,
            visit_time: f64,
            visit_count: u32,
        ) -> bool {
            self.history.lock().unwrap().push(MockHistory {
                url: url.to_string(),
                title: title.to_string(),
                visit_time,
                visit_count,
            });
            true
        }

        fn add_cookie(
            &self,
            host: &str,
            name: &str,
            value: &str,
            path: &str,
            _expires: i64,
            _is_secure: bool,
            _is_httponly: bool,
            _same_site: i32,
        ) -> bool {
            self.cookies.lock().unwrap().push(MockCookie {
                host: host.to_string(),
                name: name.to_string(),
                value: value.to_string(),
                path: path.to_string(),
            });
            true
        }

        fn add_autofill(
            &self,
            field_name: &str,
            value: &str,
            _times_used: i32,
            _first_used: i64,
            _last_used: i64,
        ) -> bool {
            self.autofill.lock().unwrap().push(MockAutofill {
                field_name: field_name.to_string(),
                value: value.to_string(),
            });
            true
        }

        fn add_favicon(&self, url: &str, png_bytes: &[u8]) -> bool {
            self.favicons.lock().unwrap().push(MockFavicon {
                url: url.to_string(),
                data_len: png_bytes.len(),
            });
            true
        }
    }
}
