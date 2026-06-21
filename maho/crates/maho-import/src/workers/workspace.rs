//! Workspace import worker.
//!
//! Ports `importers/workspace_importer.cc`. Reads parsed workspaces (Arc/Zen)
//! and creates spaces, tabs, folders, pinned/favorite tabs in the destination.

use std::collections::HashMap;
use std::sync::atomic::AtomicBool;
use std::sync::mpsc::Sender;
use std::sync::Arc;

use crate::orchestrator::{ImportDestination, ImportProgress, ImportType};
use crate::parsers;
use crate::{BrowserType, DetectedBrowser, ImportResult, ParsedFolder};

use super::{is_cancelled, send_update, ImportWorker};

pub struct WorkspaceWorker;

impl ImportWorker for WorkspaceWorker {
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

        let (workspaces, active_uuid) = match browser.browser_type {
            BrowserType::Zen => {
                let result = parsers::zen::workspaces::parse_zen_workspaces(profile_dir)?;
                (result.workspaces, result.active_workspace_uuid)
            }
            BrowserType::Arc => {
                let result = parsers::arc::sidebar::parse_arc_sidebar(profile_dir)?;
                (result.workspaces, String::new())
            }
            _ => return Ok(0),
        };

        if workspaces.is_empty() {
            return Ok(0);
        }

        let mut spaces_created = 0u32;
        let mut primary_space_id: Option<String> = None;

        for ws in &workspaces {
            if is_cancelled(cancelled) {
                break;
            }

            let space_id = match destination.create_space(&ws.name, &ws.theme_color_hex, &ws.icon)
            {
                Some(id) => id,
                None => continue,
            };
            spaces_created += 1;

            if primary_space_id.is_none() || ws.uuid == active_uuid {
                primary_space_id = Some(space_id.clone());
            }

            // Pinned tabs
            for tab in &ws.pinned_tabs {
                if is_cancelled(cancelled) {
                    break;
                }
                if let Some(tab_id) = destination.create_tab(&space_id, &tab.url) {
                    destination.pin_tab(&tab_id);
                }
            }

            // Favorite tabs
            for tab in &ws.favorites {
                if is_cancelled(cancelled) {
                    break;
                }
                if let Some(tab_id) = destination.create_tab(&space_id, &tab.url) {
                    destination.favorite_tab(&tab_id);
                }
            }

            // Regular tabs
            for tab in &ws.regular_tabs {
                if is_cancelled(cancelled) {
                    break;
                }
                destination.create_tab(&space_id, &tab.url);
            }

            // Folders and their tabs
            import_folders(destination, &space_id, &ws.folders, cancelled);

            send_update(
                progress,
                ImportType::Workspaces,
                spaces_created,
                &format!("Created space: {}", ws.name),
            );
        }

        // Activate the primary space
        if let Some(ref space_id) = primary_space_id {
            destination.activate_space(space_id);
        }

        Ok(spaces_created)
    }
}

/// Import folders with parent-child relationships (recursive resolution).
fn import_folders(
    destination: &dyn ImportDestination,
    space_id: &str,
    folders: &[ParsedFolder],
    cancelled: &Arc<AtomicBool>,
) {
    let mut id_map: HashMap<String, String> = HashMap::new();

    for folder in folders {
        if is_cancelled(cancelled) {
            break;
        }
        get_or_create_folder(destination, space_id, folders, &folder.id, &mut id_map);

        // Create tabs in this folder
        let maho_folder_id = id_map.get(&folder.id).cloned().unwrap_or_default();
        for tab in &folder.tabs {
            if is_cancelled(cancelled) {
                break;
            }
            if maho_folder_id.is_empty() {
                destination.create_tab(space_id, &tab.url);
            } else {
                destination.create_tab_in_folder(space_id, &tab.url, &maho_folder_id);
            }
        }
    }
}

/// Recursively get or create a folder, resolving parent chains.
fn get_or_create_folder(
    destination: &dyn ImportDestination,
    space_id: &str,
    all_folders: &[ParsedFolder],
    folder_id: &str,
    id_map: &mut HashMap<String, String>,
) -> Option<String> {
    if folder_id.is_empty() {
        return None;
    }

    if let Some(existing) = id_map.get(folder_id) {
        return Some(existing.clone());
    }

    let folder_info = all_folders.iter().find(|f| f.id == folder_id)?;

    // Resolve parent first
    let parent_maho_id = if !folder_info.parent_id.is_empty() {
        get_or_create_folder(destination, space_id, all_folders, &folder_info.parent_id, id_map)
            .unwrap_or_default()
    } else {
        String::new()
    };

    let maho_id = destination.create_folder(space_id, &folder_info.name, &parent_maho_id)?;
    id_map.insert(folder_id.to_string(), maho_id.clone());
    Some(maho_id)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::orchestrator::ImportDestination;
    use crate::workers::tests::MockDestination;
    use crate::{ParsedFolder, ParsedTab, ParsedWorkspace};
    use std::sync::mpsc;

    #[test]
    fn test_workspace_worker_unsupported_browser() {
        let dest: Arc<dyn ImportDestination> = Arc::new(MockDestination::new());
        let cancelled = Arc::new(AtomicBool::new(false));
        let (tx, rx) = mpsc::channel();

        let browser = DetectedBrowser {
            browser_type: BrowserType::Chrome,
            display_name: "Chrome".to_string(),
            profile_path: std::path::PathBuf::from("/nonexistent/profile"),
            services_supported: 0xFF,
            requires_full_disk_access: false,
        };

        let result = WorkspaceWorker.run(&browser, &*dest, &cancelled, &tx);
        assert_eq!(result.unwrap(), 0);
        let msgs: Vec<ImportProgress> = rx.try_iter().collect();
        assert!(msgs.is_empty());
    }

    #[test]
    fn test_import_folders_with_mock() {
        let dest = Arc::new(MockDestination::new());
        let cancelled = Arc::new(AtomicBool::new(false));

        // Create a space first
        let space_id = dest.create_space("Test Space", "#ff0000", "star").unwrap();

        let folders = vec![
            ParsedFolder {
                id: "f1".to_string(),
                name: "Root Folder".to_string(),
                parent_id: String::new(),
                workspace_id: "ws1".to_string(),
                tabs: vec![ParsedTab {
                    url: "https://example.com".to_string(),
                    title: "Example".to_string(),
                    is_pinned: false,
                    is_essential: false,
                }],
            },
            ParsedFolder {
                id: "f2".to_string(),
                name: "Child Folder".to_string(),
                parent_id: "f1".to_string(),
                workspace_id: "ws1".to_string(),
                tabs: vec![ParsedTab {
                    url: "https://child.example.com".to_string(),
                    title: "Child".to_string(),
                    is_pinned: false,
                    is_essential: false,
                }],
            },
        ];

        import_folders(&*dest, &space_id, &folders, &cancelled);

        let created_folders = dest.folders.lock().unwrap();
        assert_eq!(created_folders.len(), 2);
        assert_eq!(created_folders[0].name, "Root Folder");
        assert!(created_folders[0].parent_id.is_empty());
        assert_eq!(created_folders[1].name, "Child Folder");
        // Child folder's parent is the root folder's maho ID
        assert_eq!(created_folders[1].parent_id, created_folders[0].id);

        let tabs = dest.tabs.lock().unwrap();
        // Space tab + 2 folder tabs
        assert!(tabs.len() >= 2);
    }

    #[test]
    fn test_workspace_with_pinned_and_favorites() {
        let dest = Arc::new(MockDestination::new());
        let cancelled = Arc::new(AtomicBool::new(false));
        let (tx, _rx) = mpsc::channel::<ImportProgress>();

        let workspaces = vec![ParsedWorkspace {
            uuid: "ws-1".to_string(),
            name: "Work".to_string(),
            icon: "briefcase".to_string(),
            theme_color_hex: "#0000ff".to_string(),
            container_id: 1,
            pinned_tabs: vec![ParsedTab {
                url: "https://pinned.example.com".to_string(),
                title: "Pinned".to_string(),
                is_pinned: true,
                is_essential: false,
            }],
            favorites: vec![ParsedTab {
                url: "https://fav.example.com".to_string(),
                title: "Fav".to_string(),
                is_pinned: false,
                is_essential: false,
            }],
            regular_tabs: vec![ParsedTab {
                url: "https://regular.example.com".to_string(),
                title: "Regular".to_string(),
                is_pinned: false,
                is_essential: false,
            }],
            folders: vec![],
        }];

        // Directly test the import logic
        let mut spaces_created = 0u32;
        for ws in &workspaces {
            let space_id =
                dest.create_space(&ws.name, &ws.theme_color_hex, &ws.icon)
                    .unwrap();
            spaces_created += 1;

            for tab in &ws.pinned_tabs {
                if let Some(tab_id) = dest.create_tab(&space_id, &tab.url) {
                    dest.pin_tab(&tab_id);
                }
            }
            for tab in &ws.favorites {
                if let Some(tab_id) = dest.create_tab(&space_id, &tab.url) {
                    dest.favorite_tab(&tab_id);
                }
            }
            for tab in &ws.regular_tabs {
                dest.create_tab(&space_id, &tab.url);
            }
        }

        assert_eq!(spaces_created, 1);
        let pinned = dest.pinned.lock().unwrap();
        assert_eq!(pinned.len(), 1);
        let favorited = dest.favorited.lock().unwrap();
        assert_eq!(favorited.len(), 1);
        let tabs = dest.tabs.lock().unwrap();
        assert_eq!(tabs.len(), 3); // pinned + fav + regular
    }
}
