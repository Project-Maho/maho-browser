//! User data export (GAP-5): bookmarks HTML, history CSV, Chrome-format
//! password CSV, AI conversations JSON, Spaces JSON and a full JSON backup.
//! Every exporter reads from the core's own managers/storage so callers never
//! have to supply the data they want exported.

use std::collections::HashMap;

use maho_types::common::DateTime;
use maho_types::vault::{VaultItemKind, VaultItemListRequest, VaultSchemaVersion, VaultTrashFilter};

use crate::import_export_manager::{
    BookmarkExportFolder, HistoryEntry, ImportExportManager, ImportItem, PasswordExportRow,
};
use crate::maho_core::MahoCore;
use crate::vault_manager::{VaultCrudError, VaultManagerError};

/// Stable error codes surfaced to the settings UI.
pub const EXPORT_ERROR_VAULT_LOCKED: &str = "vault_locked";
pub const EXPORT_ERROR_OS_AUTH_UNAVAILABLE: &str = "os_auth_unavailable";
pub const EXPORT_ERROR_PASSPHRASE_REQUIRED: &str = "passphrase_required";
pub const EXPORT_ERROR_INVALID_PASSPHRASE: &str = "invalid_passphrase";
pub const EXPORT_ERROR_RATE_LIMITED: &str = "rate_limited";
pub const EXPORT_ERROR_STORAGE_UNAVAILABLE: &str = "storage_unavailable";
pub const EXPORT_ERROR_UNKNOWN_KIND: &str = "unknown_kind";
pub const EXPORT_ERROR_WRITE_FAILED: &str = "write_failed";

/// What the settings "Export data" card can produce.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DataExportKind {
    Bookmarks,
    History,
    Passwords,
    Conversations,
    Spaces,
    FullBackup,
}

impl DataExportKind {
    pub fn parse(value: &str) -> Option<Self> {
        match value {
            "bookmarks" => Some(Self::Bookmarks),
            "history" => Some(Self::History),
            "passwords" => Some(Self::Passwords),
            "conversations" => Some(Self::Conversations),
            "spaces" => Some(Self::Spaces),
            "full_backup" => Some(Self::FullBackup),
            _ => None,
        }
    }
}

impl MahoCore {
    /// Netscape bookmark HTML of every bookmark, nested by bookmark folder,
    /// followed by a "Favorites" folder (favorite tabs) and one
    /// "Pinned - <space>" folder per space with pinned tabs.
    pub fn export_bookmarks_html(&self) -> String {
        let folders = self.bookmark_manager.get_folders();
        let mut by_parent: HashMap<Option<String>, Vec<&crate::bookmark_manager::BookmarkFolder>> =
            HashMap::new();
        for folder in &folders {
            by_parent
                .entry(folder.parent_id.clone())
                .or_default()
                .push(folder);
        }
        for children in by_parent.values_mut() {
            children.sort_by(|a, b| (&a.created_at.0, &a.name).cmp(&(&b.created_at.0, &b.name)));
        }
        let known: std::collections::HashSet<&str> =
            folders.iter().map(|f| f.id.as_str()).collect();
        let mut by_folder: HashMap<Option<String>, Vec<ImportItem>> = HashMap::new();
        let mut bookmarks = self.bookmark_manager.get_all_bookmarks();
        bookmarks.sort_by(|a, b| (&a.created_at.0, &a.title).cmp(&(&b.created_at.0, &b.title)));
        for bookmark in bookmarks {
            // A bookmark pointing at a deleted folder is exported at the root
            // rather than dropped.
            let folder = bookmark
                .folder_id
                .clone()
                .filter(|id| known.contains(id.as_str()));
            by_folder.entry(folder).or_default().push(ImportItem {
                title: bookmark.title.clone(),
                url: bookmark.url.clone(),
                folder: None,
                date_added: Some(bookmark.created_at.clone()),
            });
        }

        fn build(
            id: Option<String>,
            name: String,
            by_parent: &HashMap<Option<String>, Vec<&crate::bookmark_manager::BookmarkFolder>>,
            by_folder: &mut HashMap<Option<String>, Vec<ImportItem>>,
        ) -> BookmarkExportFolder {
            let folders = by_parent
                .get(&id)
                .map(|children| {
                    children
                        .iter()
                        .map(|child| {
                            build(Some(child.id.clone()), child.name.clone(), by_parent, by_folder)
                        })
                        .collect()
                })
                .unwrap_or_default();
            BookmarkExportFolder {
                name,
                folders,
                items: by_folder.remove(&id).unwrap_or_default(),
            }
        }
        let mut root = build(None, String::new(), &by_parent, &mut by_folder);

        let tab_item = |tab: &maho_types::traits::shell_renderer::TabViewModel| ImportItem {
            title: tab.custom_title.clone().unwrap_or_else(|| tab.title.clone()),
            url: tab.pinned_url.clone().unwrap_or_else(|| tab.url.clone()),
            folder: None,
            date_added: None,
        };
        let mut favorites = Vec::new();
        let mut seen = std::collections::HashSet::new();
        let mut pinned_folders = Vec::new();
        for space in self.space_manager.get_all_spaces() {
            for tab in self.get_favorite_tabs(&space.id) {
                if seen.insert(tab.id.clone()) {
                    favorites.push(tab_item(&tab));
                }
            }
            let pinned: Vec<ImportItem> =
                self.get_pinned_tabs(&space.id).iter().map(tab_item).collect();
            if !pinned.is_empty() {
                pinned_folders.push(BookmarkExportFolder {
                    name: format!("Pinned - {}", space.name),
                    folders: Vec::new(),
                    items: pinned,
                });
            }
        }
        if !favorites.is_empty() {
            root.folders.push(BookmarkExportFolder {
                name: "Favorites".to_string(),
                folders: Vec::new(),
                items: favorites,
            });
        }
        root.folders.extend(pinned_folders);
        ImportExportManager::export_bookmarks_tree(&root)
    }

    /// History CSV read straight from storage (all rows, newest first).
    pub fn export_history_csv(&self) -> Result<String, String> {
        let storage = self
            .storage
            .as_ref()
            .ok_or_else(|| EXPORT_ERROR_STORAGE_UNAVAILABLE.to_string())?;
        let entries: Vec<HistoryEntry> = storage
            .list_history_for_export()
            .map_err(|e| e.to_string())?
            .into_iter()
            .map(|(url, title, visit_count, visited_at)| HistoryEntry {
                url,
                title,
                visit_count: u32::try_from(visit_count.max(0)).unwrap_or(u32::MAX),
                last_visited: Some(DateTime::from_iso(visited_at)),
            })
            .collect();
        Ok(self.import_export.export_history_csv(&entries))
    }

    /// Chrome-compatible password CSV (`name,url,username,password,note`) of
    /// every non-trashed login. Fails with `vault_locked` unless the Vault is
    /// unlocked. The caller (browser UI) must have completed OS
    /// re-authentication first; on platforms without one, use
    /// [`Self::authorize_password_export_with_passphrase`].
    pub fn export_passwords_csv(&self) -> Result<String, String> {
        if self.vault_runtime.ensure_unlocked().is_err() {
            return Err(EXPORT_ERROR_VAULT_LOCKED.to_string());
        }
        let request = VaultItemListRequest {
            trash: VaultTrashFilter::Exclude,
            favorites_only: false,
            schema_version: VaultSchemaVersion::CURRENT,
            provider: None,
            kinds: vec![VaultItemKind::Login],
            cursor: None,
            limit: 0,
        };
        let items = self.vault_list_items(&request).map_err(crud_code)?;
        let repository = self.vault_repository().map_err(|e| e.to_string())?;
        let mut rows = Vec::with_capacity(items.len());
        for item in items {
            let password = self
                .vault_runtime
                .peek_login_password_at_revision(&repository, item.id, item.revision)
                .map_err(crud_code)?;
            let username = self
                .vault_runtime
                .get_username(&repository, item.id)
                .map_err(crud_code)?;
            let note = if item.has_notes {
                self.vault_runtime
                    .get_notes(&repository, item.id)
                    .map_err(crud_code)?
                    .to_string()
            } else {
                String::new()
            };
            rows.push(PasswordExportRow {
                name: item.title.clone(),
                url: item
                    .origins
                    .first()
                    .map(|o| o.as_str().to_string())
                    .unwrap_or_default(),
                username: username.to_string(),
                password,
                note,
            });
        }
        Ok(ImportExportManager::export_passwords_csv(&rows))
    }

    /// Re-authentication for password export on platforms without an OS
    /// authenticator (Linux). Account-escrow unlock is zero-input, so an
    /// unlocked Vault alone is not proof of presence: require the master
    /// passphrase when one exists, otherwise refuse.
    pub fn authorize_password_export_with_passphrase(
        &self,
        passphrase: Option<&[u8]>,
    ) -> Result<(), &'static str> {
        if self.vault_runtime.ensure_unlocked().is_err() {
            return Err(EXPORT_ERROR_VAULT_LOCKED);
        }
        if !self.vault_runtime.has_user_wrap() {
            return Err(EXPORT_ERROR_OS_AUTH_UNAVAILABLE);
        }
        let passphrase = match passphrase {
            Some(p) if !p.is_empty() => p,
            _ => return Err(EXPORT_ERROR_PASSPHRASE_REQUIRED),
        };
        self.vault_runtime
            .verify_user_passphrase(passphrase, chrono::Utc::now())
            .map_err(|error| match error {
                VaultManagerError::RateLimited => EXPORT_ERROR_RATE_LIMITED,
                VaultManagerError::VaultLocked | VaultManagerError::Uninitialized => {
                    EXPORT_ERROR_VAULT_LOCKED
                }
                _ => EXPORT_ERROR_INVALID_PASSPHRASE,
            })
    }

    /// Every AI conversation (including archived) with all of its turns.
    pub fn export_conversations_json(&self) -> Result<String, String> {
        let storage = self
            .storage
            .as_ref()
            .ok_or_else(|| EXPORT_ERROR_STORAGE_UNAVAILABLE.to_string())?;
        let conversations = storage
            .list_conversations(maho_types::chat::ConversationListState::All, usize::MAX >> 1)
            .map_err(|e| e.to_string())?;
        let mut out = Vec::with_capacity(conversations.len());
        for conversation in conversations {
            let messages = storage
                .get_all_conversation_messages(&conversation.id)
                .map_err(|e| e.to_string())?;
            out.push(serde_json::json!({
                "conversation": conversation,
                "messages": messages,
            }));
        }
        serde_json::to_string_pretty(&serde_json::json!({
            "version": 1,
            "exported_at": DateTime::now().0,
            "conversations": out,
        }))
        .map_err(|e| e.to_string())
    }

    /// Spaces with their folders, pinned tabs and the profile favorites.
    pub fn export_spaces_json(&self) -> String {
        serde_json::to_string_pretty(&self.spaces_export_value())
            .unwrap_or_else(|_| "{}".to_string())
    }

    fn spaces_export_value(&self) -> serde_json::Value {
        let tab_json = |tab: &maho_types::traits::shell_renderer::TabViewModel| {
            serde_json::json!({
                "id": tab.id.as_ref(),
                "title": tab.title,
                "custom_title": tab.custom_title,
                "custom_icon": tab.custom_icon,
                "url": tab.url,
                "pinned_url": tab.pinned_url,
                "favorite_order": tab.favorite_order,
            })
        };
        let mut spaces = Vec::new();
        let mut favorites = Vec::new();
        let mut seen = std::collections::HashSet::new();
        for space in self.space_manager.get_all_spaces() {
            for tab in self.get_favorite_tabs(&space.id) {
                if seen.insert(tab.id.clone()) {
                    favorites.push(serde_json::json!({
                        "profile_id": space.profile_id,
                        "tab": tab_json(&tab),
                    }));
                }
            }
            let pinned: Vec<serde_json::Value> =
                self.get_pinned_tabs(&space.id).iter().map(tab_json).collect();
            spaces.push(serde_json::json!({
                "id": space.id,
                "profile_id": space.profile_id,
                "name": space.name,
                "color": space.color,
                "icon": space.icon,
                "theme": space.theme,
                "folders": space.folders,
                "root_order": space.root_order,
                "pinned_tabs": pinned,
            }));
        }
        serde_json::json!({
            "version": 1,
            "exported_at": DateTime::now().0,
            "spaces": spaces,
            "favorites": favorites,
        })
    }

    /// Full JSON backup: bookmarks, settings, notes and boosts in the
    /// `import_from_json` shape, plus spaces. Secrets (passwords) are never
    /// included; they have their own re-authenticated export.
    pub fn export_full_backup_json(&self) -> String {
        let folder_names: HashMap<&str, &str> = self
            .bookmark_manager
            .get_folders()
            .into_iter()
            .map(|f| (f.id.as_str(), f.name.as_str()))
            .collect();
        let bookmarks = self
            .bookmark_manager
            .get_all_bookmarks()
            .into_iter()
            .map(|b| ImportItem {
                title: b.title.clone(),
                url: b.url.clone(),
                folder: b
                    .folder_id
                    .as_deref()
                    .and_then(|id| folder_names.get(id))
                    .map(|name| name.to_string()),
                date_added: Some(b.created_at.clone()),
            })
            .collect();
        let notes = self
            .note_manager
            .get_all_notes()
            .into_iter()
            .map(|n| crate::import_export_manager::NoteExport {
                content: n.content.clone(),
                linked_url: n.linked_url.as_ref().map(|u| u.0.clone()),
            })
            .collect();
        let boosts = self
            .boost_manager
            .get_all_boosts()
            .into_iter()
            .map(|b| crate::import_export_manager::BoostExport {
                domain: b.domain.clone(),
                custom_css: Some(b.custom_css.clone()).filter(|css| !css.is_empty()),
            })
            .collect();
        let mut backup = self
            .import_export
            .export_data(crate::import_export_manager::ExportData {
                bookmarks,
                settings: serde_json::to_value(self.settings_manager.get_settings()).ok(),
                notes,
                boosts,
            });
        if let Some(map) = backup.as_object_mut() {
            map.insert("spaces".to_string(), self.spaces_export_value());
        }
        serde_json::to_string_pretty(&backup).unwrap_or_else(|_| "{}".to_string())
    }

    /// Produces `kind` and writes it to `path`. Nothing is written unless the
    /// export itself succeeded. Password files are created owner-only.
    /// Password authorization (OS auth / passphrase) is the caller's job.
    pub fn write_data_export(&self, kind: DataExportKind, path: &std::path::Path) -> Result<(), String> {
        let content = match kind {
            DataExportKind::Bookmarks => self.export_bookmarks_html(),
            DataExportKind::History => self.export_history_csv()?,
            DataExportKind::Passwords => self.export_passwords_csv()?,
            DataExportKind::Conversations => self.export_conversations_json()?,
            DataExportKind::Spaces => self.export_spaces_json(),
            DataExportKind::FullBackup => self.export_full_backup_json(),
        };
        write_export_file(path, content.as_bytes(), kind == DataExportKind::Passwords)
            .map_err(|_| EXPORT_ERROR_WRITE_FAILED.to_string())
    }
}

fn crud_code(error: VaultCrudError) -> String {
    match error {
        VaultCrudError::Locked => EXPORT_ERROR_VAULT_LOCKED.to_string(),
        other => other.to_string(),
    }
}

fn write_export_file(path: &std::path::Path, bytes: &[u8], private: bool) -> std::io::Result<()> {
    use std::io::Write;
    let mut options = std::fs::OpenOptions::new();
    options.write(true).create(true).truncate(true);
    #[cfg(unix)]
    if private {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    #[cfg(not(unix))]
    let _ = private;
    let mut file = options.open(path)?;
    file.write_all(bytes)?;
    file.sync_all()
}

#[cfg(test)]
mod tests;
