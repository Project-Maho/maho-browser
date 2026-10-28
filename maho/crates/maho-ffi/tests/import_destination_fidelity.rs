//! Import fidelity through the production `MahoCoreDestination`: bookmark
//! folder trees survive, history keeps visit time and count, and a private
//! active tab does not swallow imported data.

use std::ffi::CString;

use maho_core::maho_core::MahoCore;
use maho_ffi::import_destination::MahoCoreDestination;
use maho_import::orchestrator::ImportDestination;
use maho_types::common::Url;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::TabId;

// Same process-wide SQLCipher key as the other destination tests, so tests
// running in parallel never re-key each other's databases.
const KEY: &str = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

struct TestCore {
    core: *mut MahoCore,
    _dir: tempfile::TempDir,
}

impl TestCore {
    fn new() -> Self {
        let key = CString::new(KEY).unwrap();
        assert!(unsafe { maho_ffi::maho_storage_set_sqlcipher_key(key.as_ptr()) });
        let dir = tempfile::tempdir().unwrap();
        let path = CString::new(dir.path().to_string_lossy().as_bytes()).unwrap();
        let core = unsafe { maho_ffi::maho_core_new_with_storage(path.as_ptr()) };
        assert!(!core.is_null());
        assert!(unsafe { (*core).storage_ref() }.is_some(), "SQLite storage opened");
        Self { core, _dir: dir }
    }

    // Test-only accessor over the raw FFI core pointer this fixture owns.
    #[allow(clippy::mut_from_ref)]
    fn core(&self) -> &mut MahoCore {
        unsafe { &mut *self.core }
    }

    fn destination(&self) -> MahoCoreDestination {
        unsafe { MahoCoreDestination::new(self.core) }
    }
}

impl Drop for TestCore {
    fn drop(&mut self) {
        unsafe { maho_ffi::maho_core_free(self.core) };
    }
}

fn path(parts: &[&str]) -> Vec<String> {
    parts.iter().map(|p| p.to_string()).collect()
}

#[test]
fn nested_folder_path_yields_folder_b_under_a() {
    let t = TestCore::new();
    let dest = t.destination();

    let deep = dest
        .add_bookmark("Deep", "https://deep.example/", &path(&["A", "B"]))
        .expect("bookmark id");
    let deep2 = dest
        .add_bookmark("Deep 2", "https://deep2.example/", &path(&["A", "B"]))
        .expect("bookmark id");
    let mid = dest
        .add_bookmark("Mid", "https://mid.example/", &path(&["A"]))
        .expect("bookmark id");
    let root = dest
        .add_bookmark("Root", "https://root.example/", &[])
        .expect("bookmark id");
    // Same leaf name under a different parent is a different folder.
    let other_b = dest
        .add_bookmark("Other", "https://other.example/", &path(&["C", "B"]))
        .expect("bookmark id");

    let folders: Vec<_> = t.core().get_bookmark_folders().into_iter().cloned().collect();
    assert_eq!(folders.len(), 4, "A, A/B, C, C/B: {folders:?}");
    let a = folders.iter().find(|f| f.name == "A").unwrap();
    let c = folders.iter().find(|f| f.name == "C").unwrap();
    let b_under_a = folders
        .iter()
        .find(|f| f.name == "B" && f.parent_id.as_deref() == Some(a.id.as_str()))
        .expect("folder B under A");
    assert!(folders
        .iter()
        .any(|f| f.name == "B" && f.parent_id.as_deref() == Some(c.id.as_str())));
    assert_eq!(a.parent_id, None);

    let folder_of = |id: &str| {
        t.core()
            .get_all_bookmark_entries()
            .into_iter()
            .find(|b| b.id.0 == id)
            .unwrap_or_else(|| panic!("returned id {id} is stored"))
            .folder_id
            .clone()
    };
    assert_eq!(folder_of(&deep).as_deref(), Some(b_under_a.id.as_str()));
    assert_eq!(folder_of(&deep2).as_deref(), Some(b_under_a.id.as_str()));
    assert_eq!(folder_of(&mid).as_deref(), Some(a.id.as_str()));
    assert_eq!(folder_of(&root), None);
    assert_ne!(folder_of(&other_b).as_deref(), Some(b_under_a.id.as_str()));

    // The tree is persisted, not only held in memory.
    let storage = t.core().storage_ref().unwrap();
    let stored_folders = storage.load_bookmark_folders().unwrap();
    assert!(stored_folders
        .iter()
        .any(|(id, name, parent, _)| id == &b_under_a.id && name == "B" && parent.as_deref() == Some(a.id.as_str())));
    let stored = storage.get_all_bookmarks().unwrap();
    assert!(stored
        .iter()
        .any(|(id, _, _, folder, _)| id == &deep && folder.as_deref() == Some(b_under_a.id.as_str())));
}

#[test]
fn history_import_keeps_visit_time_and_count() {
    let t = TestCore::new();
    let dest = t.destination();

    assert!(dest.add_history("https://visited.example/", "Visited", 1_700_000_000.0, 7));

    let storage = t.core().storage_ref().unwrap();
    assert_eq!(
        storage.search_history("visited.example", 10).unwrap(),
        vec![(
            "https://visited.example/".to_string(),
            "Visited".to_string(),
            "2023-11-14 22:13:20".to_string()
        )]
    );
    assert_eq!(storage.history_visit_count("https://visited.example/").unwrap(), 7);
}

#[test]
fn history_import_with_zero_count_or_missing_time_stores_defaults() {
    let t = TestCore::new();
    let dest = t.destination();

    for (i, time) in [0.0, -5.0, f64::NAN, f64::INFINITY, 1.0e300].into_iter().enumerate() {
        let url = format!("https://defaults{i}.example/");
        assert!(dest.add_history(&url, "", time, 0), "time {time} must not fail");
        let storage = t.core().storage_ref().unwrap();
        assert_eq!(storage.history_visit_count(&url).unwrap(), 1, "count 0 stores one visit");
    }

    let now_ms = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_millis() as u64;
    let recent = t.core().storage_ref().unwrap().load_recent_history(10).unwrap();
    assert_eq!(recent.len(), 5);
    for (url, _, visited_ms) in recent {
        assert!(now_ms.abs_diff(visited_ms) < 120_000, "{url} stored as now, got {visited_ms}");
    }
}

#[test]
fn import_is_not_dropped_when_the_active_tab_is_private() {
    let t = TestCore::new();
    let space_id = t.core().get_active_space_id();
    t.core().handle_event(ShellEvent::CreateTab {
        space_id,
        url: Some(Url::new("https://private.example/")),
        parent_id: None,
        tab_id: Some(TabId::generate()),
        window_id: None,
        is_private: true,
    });
    // Precondition: the live path is gated by the private active tab.
    t.core().add_history_entry("https://live.example/", "Live");
    assert!(t.core().search_history("live.example", 10).is_empty());
    assert!(t
        .core()
        .add_bookmark_returning_id("https://live.example/", "Live", None)
        .is_none());

    let dest = t.destination();
    assert!(dest.add_history("https://imported.example/", "Imported", 1_700_000_000.0, 2));
    let id = dest
        .add_bookmark("Imported", "https://imported.example/", &path(&["Folder"]))
        .expect("imported bookmark stored");

    assert_eq!(t.core().search_history("imported.example", 10).len(), 1);
    assert!(t
        .core()
        .get_all_bookmark_entries()
        .iter()
        .any(|b| b.id.0 == id && b.folder_id.is_some()));
}
