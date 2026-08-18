use std::path::PathBuf;

use serde::Serialize;

use maho_types::common::DateTime;

#[derive(Clone, Debug, Serialize)]
pub enum ImportSource {
    Chrome,
    Firefox,
    Safari,
    Arc,
    Brave,
    Edge,
    Custom(String),
}

impl std::fmt::Display for ImportSource {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ImportSource::Chrome => write!(f, "Chrome"),
            ImportSource::Firefox => write!(f, "Firefox"),
            ImportSource::Safari => write!(f, "Safari"),
            ImportSource::Arc => write!(f, "Arc"),
            ImportSource::Brave => write!(f, "Brave"),
            ImportSource::Edge => write!(f, "Edge"),
            ImportSource::Custom(name) => write!(f, "{}", name),
        }
    }
}

#[derive(Clone, Debug, Serialize)]
pub enum ImportDataType {
    Bookmarks,
    History,
    Passwords,
    Settings,
    Extensions,
}

#[derive(Clone, Debug)]
pub struct ImportItem {
    pub title: String,
    pub url: String,
    pub folder: Option<String>,
    pub date_added: Option<DateTime>,
}

#[derive(Debug, Serialize)]
pub struct ImportResult {
    pub source: ImportSource,
    pub items_imported: usize,
    pub items_skipped: usize,
    pub errors: Vec<String>,
}

pub struct ExportData {
    pub bookmarks: Vec<ImportItem>,
    pub settings: Option<serde_json::Value>,
    pub notes: Vec<NoteExport>,
    pub boosts: Vec<BoostExport>,
}

pub struct NoteExport {
    pub content: String,
    pub linked_url: Option<String>,
}

pub struct BoostExport {
    pub domain: String,
    pub custom_css: Option<String>,
}

#[derive(Clone, Debug, Serialize)]
pub struct BrowserProfile {
    pub source: ImportSource,
    pub profile_name: String,
    pub profile_path: PathBuf,
    pub data_types_available: Vec<ImportDataType>,
}

#[derive(Clone, Debug, Serialize, serde::Deserialize)]
pub struct HistoryEntry {
    pub url: String,
    pub title: String,
    pub visit_count: u32,
    pub last_visited: Option<DateTime>,
}

const MAX_IMPORT_HISTORY: usize = 20;

pub struct ImportExportManager {
    import_history: Vec<ImportResult>,
}

impl Default for ImportExportManager {
    fn default() -> Self {
        Self::new()
    }
}

impl ImportExportManager {
    pub fn new() -> Self {
        Self {
            import_history: Vec::new(),
        }
    }

    pub fn import_bookmarks(
        &mut self,
        source: ImportSource,
        items: Vec<ImportItem>,
    ) -> ImportResult {
        let mut imported = 0;
        let mut skipped = 0;
        let mut errors: Vec<String> = Vec::new();

        for item in &items {
            if item.url.is_empty() {
                errors.push(format!("Empty URL for bookmark: {}", item.title));
                skipped += 1;
            } else if !item.url.starts_with("http://") && !item.url.starts_with("https://") {
                errors.push(format!("Invalid URL scheme: {}", item.url));
                skipped += 1;
            } else {
                imported += 1;
            }
        }

        let result = ImportResult {
            source,
            items_imported: imported,
            items_skipped: skipped,
            errors,
        };

        self.import_history.push(ImportResult {
            source: result.source.clone(),
            items_imported: result.items_imported,
            items_skipped: result.items_skipped,
            errors: result.errors.clone(),
        });
        if self.import_history.len() > MAX_IMPORT_HISTORY {
            let excess = self.import_history.len() - MAX_IMPORT_HISTORY;
            self.import_history.drain(0..excess);
        }

        result
    }

    pub fn export_bookmarks(&self, bookmarks: &[ImportItem]) -> String {
        let mut html = String::from(
            "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n\
             <META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n\
             <TITLE>Bookmarks</TITLE>\n\
             <H1>Bookmarks</H1>\n\
             <DL><p>\n",
        );

        for item in bookmarks {
            let date_attr = item
                .date_added
                .as_ref()
                .map(|d| format!(" ADD_DATE=\"{}\"", d))
                .unwrap_or_default();

            if let Some(folder) = &item.folder {
                html.push_str(&format!("    <DT><H3>{}</H3>\n    <DL><p>\n", folder));
                html.push_str(&format!(
                    "        <DT><A HREF=\"{}\"{}>{}</A>\n",
                    item.url, date_attr, item.title
                ));
                html.push_str("    </DL><p>\n");
            } else {
                html.push_str(&format!(
                    "    <DT><A HREF=\"{}\"{}>{}</A>\n",
                    item.url, date_attr, item.title
                ));
            }
        }

        html.push_str("</DL><p>\n");
        html
    }

    pub fn export_data(&self, data: ExportData) -> serde_json::Value {
        let bookmarks: Vec<serde_json::Value> = data
            .bookmarks
            .iter()
            .map(|item| {
                serde_json::json!({
                    "title": item.title,
                    "url": item.url,
                    "folder": item.folder,
                    "date_added": item.date_added.as_ref().map(|d| d.0.clone()),
                })
            })
            .collect();

        let notes: Vec<serde_json::Value> = data
            .notes
            .iter()
            .map(|note| {
                serde_json::json!({
                    "content": note.content,
                    "linked_url": note.linked_url,
                })
            })
            .collect();

        let boosts: Vec<serde_json::Value> = data
            .boosts
            .iter()
            .map(|boost| {
                serde_json::json!({
                    "domain": boost.domain,
                    "custom_css": boost.custom_css,
                })
            })
            .collect();

        serde_json::json!({
            "version": 1,
            "exported_at": DateTime::now().0,
            "bookmarks": bookmarks,
            "settings": data.settings,
            "notes": notes,
            "boosts": boosts,
        })
    }

    pub fn import_from_json(&mut self, json: &str) -> Result<ImportResult, String> {
        let parsed: serde_json::Value =
            serde_json::from_str(json).map_err(|e| format!("Invalid JSON: {}", e))?;

        let bookmarks = parsed
            .get("bookmarks")
            .and_then(|v| v.as_array())
            .ok_or_else(|| "Missing or invalid 'bookmarks' field".to_string())?;

        let mut items: Vec<ImportItem> = Vec::new();
        let mut errors: Vec<String> = Vec::new();

        for (i, entry) in bookmarks.iter().enumerate() {
            let title = entry
                .get("title")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();
            let url = entry
                .get("url")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();

            if url.is_empty() {
                errors.push(format!("Bookmark at index {} has no URL", i));
                continue;
            }

            let folder = entry
                .get("folder")
                .and_then(|v| v.as_str())
                .map(|s| s.to_string());
            let date_added = entry
                .get("date_added")
                .and_then(|v| v.as_str())
                .map(DateTime::from_iso);

            items.push(ImportItem {
                title,
                url,
                folder,
                date_added,
            });
        }

        let imported = items.len();
        let skipped = errors.len();

        let result = ImportResult {
            source: ImportSource::Custom("JSON Backup".to_string()),
            items_imported: imported,
            items_skipped: skipped,
            errors: errors.clone(),
        };

        self.import_history.push(ImportResult {
            source: ImportSource::Custom("JSON Backup".to_string()),
            items_imported: imported,
            items_skipped: skipped,
            errors,
        });
        if self.import_history.len() > MAX_IMPORT_HISTORY {
            let excess = self.import_history.len() - MAX_IMPORT_HISTORY;
            self.import_history.drain(0..excess);
        }

        Ok(result)
    }

    pub fn get_import_history(&self) -> &[ImportResult] {
        &self.import_history
    }

    pub fn detect_browser_profiles() -> Vec<BrowserProfile> {
        let mut profiles = Vec::new();

        if let Some(path) = Self::detect_chrome_profile() {
            profiles.push(BrowserProfile {
                source: ImportSource::Chrome,
                profile_name: "Default".to_string(),
                profile_path: path,
                data_types_available: vec![ImportDataType::Bookmarks, ImportDataType::History],
            });
        }
        if let Some(path) = Self::detect_firefox_profile() {
            profiles.push(BrowserProfile {
                source: ImportSource::Firefox,
                profile_name: "Default".to_string(),
                profile_path: path,
                data_types_available: vec![ImportDataType::Bookmarks, ImportDataType::History],
            });
        }
        if let Some(path) = Self::detect_safari_profile() {
            profiles.push(BrowserProfile {
                source: ImportSource::Safari,
                profile_name: "Default".to_string(),
                profile_path: path,
                data_types_available: vec![ImportDataType::Bookmarks, ImportDataType::History],
            });
        }
        if let Some(path) = Self::detect_arc_profile() {
            profiles.push(BrowserProfile {
                source: ImportSource::Arc,
                profile_name: "Default".to_string(),
                profile_path: path,
                data_types_available: vec![ImportDataType::Bookmarks],
            });
        }
        if let Some(path) = Self::detect_brave_profile() {
            profiles.push(BrowserProfile {
                source: ImportSource::Brave,
                profile_name: "Default".to_string(),
                profile_path: path,
                data_types_available: vec![ImportDataType::Bookmarks, ImportDataType::History],
            });
        }
        if let Some(path) = Self::detect_edge_profile() {
            profiles.push(BrowserProfile {
                source: ImportSource::Edge,
                profile_name: "Default".to_string(),
                profile_path: path,
                data_types_available: vec![ImportDataType::Bookmarks, ImportDataType::History],
            });
        }

        profiles
    }

    fn detect_chrome_profile() -> Option<PathBuf> {
        let home = dirs_or_home()?;
        let path = home.join("Library/Application Support/Google/Chrome/Default");
        if path.exists() {
            Some(path)
        } else {
            None
        }
    }

    fn detect_firefox_profile() -> Option<PathBuf> {
        let home = dirs_or_home()?;
        let profiles_dir = home.join("Library/Application Support/Firefox/Profiles");
        if profiles_dir.exists() {
            if let Ok(entries) = std::fs::read_dir(&profiles_dir) {
                for entry in entries.flatten() {
                    let name = entry.file_name().to_string_lossy().to_string();
                    if name.ends_with(".default-release") || name.ends_with(".default") {
                        return Some(entry.path());
                    }
                }
            }
        }
        None
    }

    fn detect_safari_profile() -> Option<PathBuf> {
        let home = dirs_or_home()?;
        let path = home.join("Library/Safari");
        if path.exists() {
            Some(path)
        } else {
            None
        }
    }

    fn detect_arc_profile() -> Option<PathBuf> {
        let home = dirs_or_home()?;
        let path = home.join("Library/Application Support/Arc/User Data/Default");
        if path.exists() {
            Some(path)
        } else {
            None
        }
    }

    fn detect_brave_profile() -> Option<PathBuf> {
        let home = dirs_or_home()?;
        let path = home.join("Library/Application Support/BraveSoftware/Brave-Browser/Default");
        if path.exists() {
            Some(path)
        } else {
            None
        }
    }

    fn detect_edge_profile() -> Option<PathBuf> {
        let home = dirs_or_home()?;
        let path = home.join("Library/Application Support/Microsoft Edge/Default");
        if path.exists() {
            Some(path)
        } else {
            None
        }
    }

    pub fn import_chrome_bookmarks(
        profile_path: &std::path::Path,
    ) -> Result<Vec<ImportItem>, String> {
        let bookmarks_file = profile_path.join("Bookmarks");
        let content = std::fs::read_to_string(&bookmarks_file)
            .map_err(|e| format!("Failed to read Chrome bookmarks: {}", e))?;
        let parsed: serde_json::Value = serde_json::from_str(&content)
            .map_err(|e| format!("Invalid Chrome bookmarks JSON: {}", e))?;

        let mut items = Vec::new();
        if let Some(roots) = parsed.get("roots") {
            for (_key, node) in roots.as_object().into_iter().flatten() {
                Self::parse_chrome_bookmark_node(node, None, &mut items);
            }
        }
        Ok(items)
    }

    fn parse_chrome_bookmark_node(
        node: &serde_json::Value,
        folder: Option<&str>,
        items: &mut Vec<ImportItem>,
    ) {
        let node_type = node.get("type").and_then(|v| v.as_str()).unwrap_or("");
        let name = node.get("name").and_then(|v| v.as_str()).unwrap_or("");

        match node_type {
            "url" => {
                let url = node
                    .get("url")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                if !url.is_empty() {
                    items.push(ImportItem {
                        title: name.to_string(),
                        url,
                        folder: folder.map(|f| f.to_string()),
                        date_added: node
                            .get("date_added")
                            .and_then(|v| v.as_str())
                            .map(DateTime::from_iso),
                    });
                }
            }
            "folder" => {
                if let Some(children) = node.get("children").and_then(|v| v.as_array()) {
                    for child in children {
                        Self::parse_chrome_bookmark_node(child, Some(name), items);
                    }
                }
            }
            _ => {}
        }
    }

    pub fn import_firefox_bookmarks(
        profile_path: &std::path::Path,
    ) -> Result<Vec<ImportItem>, String> {
        let backup_dir = profile_path.join("bookmarkbackups");
        if !backup_dir.exists() {
            return Err("Firefox bookmark backups directory not found".to_string());
        }

        let mut latest_backup: Option<std::path::PathBuf> = None;
        if let Ok(entries) = std::fs::read_dir(&backup_dir) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path
                    .extension()
                    .is_some_and(|ext| ext == "jsonlz4" || ext == "json")
                    && latest_backup.as_ref().is_none_or(|prev| path > *prev)
                {
                    latest_backup = Some(path);
                }
            }
        }

        let backup_path =
            latest_backup.ok_or_else(|| "No Firefox bookmark backup files found".to_string())?;

        if backup_path.extension().is_some_and(|ext| ext == "jsonlz4") {
            return Err("Firefox LZ4 compressed backups not yet supported. Export bookmarks as HTML from Firefox first.".to_string());
        }

        let content = std::fs::read_to_string(&backup_path)
            .map_err(|e| format!("Failed to read Firefox bookmarks: {}", e))?;
        let parsed: serde_json::Value = serde_json::from_str(&content)
            .map_err(|e| format!("Invalid Firefox bookmarks JSON: {}", e))?;

        let mut items = Vec::new();
        Self::parse_firefox_bookmark_node(&parsed, None, &mut items);
        Ok(items)
    }

    fn parse_firefox_bookmark_node(
        node: &serde_json::Value,
        folder: Option<&str>,
        items: &mut Vec<ImportItem>,
    ) {
        let type_code = node.get("type").and_then(|v| v.as_u64()).unwrap_or(0);
        let title = node.get("title").and_then(|v| v.as_str()).unwrap_or("");

        match type_code {
            1 => {
                let uri = node.get("uri").and_then(|v| v.as_str()).unwrap_or("");
                if !uri.is_empty() && (uri.starts_with("http://") || uri.starts_with("https://")) {
                    let date_added = node
                        .get("dateAdded")
                        .and_then(|v| v.as_u64())
                        .map(|micros| {
                            let secs = (micros / 1_000_000) as i64;
                            let dt = chrono::DateTime::from_timestamp(secs, 0).unwrap_or_default();
                            DateTime::from_iso(dt.to_rfc3339())
                        });
                    items.push(ImportItem {
                        title: title.to_string(),
                        url: uri.to_string(),
                        folder: folder.map(|f| f.to_string()),
                        date_added,
                    });
                }
            }
            2 => {
                if let Some(children) = node.get("children").and_then(|v| v.as_array()) {
                    for child in children {
                        Self::parse_firefox_bookmark_node(child, Some(title), items);
                    }
                }
            }
            _ => {}
        }
    }

    pub fn import_safari_bookmarks(
        _profile_path: &std::path::Path,
    ) -> Result<Vec<ImportItem>, String> {
        Err("Safari bookmark import requires plist parsing. Export bookmarks as HTML from Safari first.".to_string())
    }

    pub fn export_history_csv(&self, entries: &[HistoryEntry]) -> String {
        let mut csv = String::from("URL,Title,Visit Count,Last Visited\n");
        for entry in entries {
            let title_escaped = entry.title.replace('"', "\"\"");
            let url_escaped = entry.url.replace('"', "\"\"");
            let last_visited = entry
                .last_visited
                .as_ref()
                .map(|d| d.0.clone())
                .unwrap_or_default();
            csv.push_str(&format!(
                "\"{}\",\"{}\",{},\"{}\"\n",
                url_escaped, title_escaped, entry.visit_count, last_visited
            ));
        }
        csv
    }

    pub fn export_settings_json(settings: &serde_json::Value) -> String {
        serde_json::to_string_pretty(settings).unwrap_or_else(|_| "{}".to_string())
    }
}

fn dirs_or_home() -> Option<PathBuf> {
    std::env::var("HOME").ok().map(PathBuf::from)
}
