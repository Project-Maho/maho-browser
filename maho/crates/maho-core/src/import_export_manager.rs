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

/// One folder level of a bookmark export tree.
#[derive(Clone, Debug, Default)]
pub struct BookmarkExportFolder {
    pub name: String,
    pub folders: Vec<BookmarkExportFolder>,
    pub items: Vec<ImportItem>,
}

/// A bookmark parsed from Netscape HTML with its full folder path.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NetscapeBookmark {
    pub title: String,
    pub url: String,
    pub folder_path: Vec<String>,
    pub add_date: Option<String>,
}

/// One plaintext credential row of a password CSV export. Plaintext lives
/// only for the duration of the export call.
pub struct PasswordExportRow {
    pub name: String,
    pub url: String,
    pub username: String,
    pub password: zeroize::Zeroizing<String>,
    pub note: String,
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

    /// Netscape bookmark HTML for a flat item list. Items sharing a folder
    /// name are grouped under one `<H3>` (first-appearance order); unfiled
    /// items are written at the root after the folders.
    pub fn export_bookmarks(&self, bookmarks: &[ImportItem]) -> String {
        let mut root = BookmarkExportFolder::default();
        for item in bookmarks {
            match &item.folder {
                Some(name) => {
                    let index = match root.folders.iter().position(|f| &f.name == name) {
                        Some(index) => index,
                        None => {
                            root.folders.push(BookmarkExportFolder {
                                name: name.clone(),
                                ..Default::default()
                            });
                            root.folders.len() - 1
                        }
                    };
                    root.folders[index].items.push(item.clone());
                }
                None => root.items.push(item.clone()),
            }
        }
        Self::export_bookmarks_tree(&root)
    }

    /// Netscape bookmark HTML for a nested folder tree. Titles, folder names
    /// and URLs are HTML-escaped.
    pub fn export_bookmarks_tree(root: &BookmarkExportFolder) -> String {
        let mut html = String::from(
            "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n\
             <META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n\
             <TITLE>Bookmarks</TITLE>\n\
             <H1>Bookmarks</H1>\n\
             <DL><p>\n",
        );
        write_bookmark_folder_body(root, 1, &mut html);
        html.push_str("</DL><p>\n");
        html
    }

    /// Parses Netscape bookmark HTML (the format every browser exports and
    /// `export_bookmarks_tree` writes). Each bookmark carries its full folder
    /// path from the root.
    pub fn parse_netscape_bookmarks(html: &str) -> Vec<NetscapeBookmark> {
        let mut out = Vec::new();
        let mut stack: Vec<String> = Vec::new();
        let mut pending_folder: Option<String> = None;
        let lower = html.to_ascii_lowercase();
        let mut pos = 0;
        while let Some(rel) = lower[pos..].find('<') {
            let tag_start = pos + rel;
            let Some(tag_len) = lower[tag_start..].find('>') else {
                break;
            };
            let tag_end = tag_start + tag_len + 1;
            let tag = &lower[tag_start..tag_end];
            if tag.starts_with("<h3") {
                let close = lower[tag_end..]
                    .find("</h3>")
                    .map(|i| tag_end + i)
                    .unwrap_or(lower.len());
                pending_folder = Some(html_unescape(html[tag_end..close].trim()));
                pos = close;
                continue;
            }
            if tag.starts_with("<dl") {
                if let Some(name) = pending_folder.take() {
                    stack.push(name);
                } else {
                    // Root list (or an anonymous list) still needs a frame so
                    // its closing tag pops symmetrically.
                    stack.push(String::new());
                }
            } else if tag.starts_with("</dl") {
                stack.pop();
            } else if tag.starts_with("<a ") || tag == "<a>" {
                let raw_tag = &html[tag_start..tag_end];
                let close = lower[tag_end..]
                    .find("</a>")
                    .map(|i| tag_end + i)
                    .unwrap_or(lower.len());
                if let Some(url) = attribute_value(raw_tag, "href") {
                    out.push(NetscapeBookmark {
                        title: html_unescape(html[tag_end..close].trim()),
                        url: html_unescape(&url),
                        folder_path: stack
                            .iter()
                            .filter(|name| !name.is_empty())
                            .cloned()
                            .collect(),
                        add_date: attribute_value(raw_tag, "add_date"),
                    });
                }
                pos = close;
                continue;
            }
            pos = tag_end;
        }
        out
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
            let last_visited = entry
                .last_visited
                .as_ref()
                .map(|d| d.0.clone())
                .unwrap_or_default();
            csv.push_str(&csv_row(&[
                &entry.url,
                &entry.title,
                &entry.visit_count.to_string(),
                &last_visited,
            ]));
        }
        csv
    }

    /// Chrome-compatible password CSV (`name,url,username,password,note`).
    pub fn export_passwords_csv(rows: &[PasswordExportRow]) -> String {
        let mut csv = String::from("name,url,username,password,note\n");
        for row in rows {
            csv.push_str(&csv_row(&[
                &row.name,
                &row.url,
                &row.username,
                &row.password,
                &row.note,
            ]));
        }
        csv
    }

    pub fn export_settings_json(settings: &serde_json::Value) -> String {
        serde_json::to_string_pretty(settings).unwrap_or_else(|_| "{}".to_string())
    }
}

pub fn html_escape(value: &str) -> String {
    let mut out = String::with_capacity(value.len());
    for c in value.chars() {
        match c {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' => out.push_str("&quot;"),
            '\'' => out.push_str("&#39;"),
            _ => out.push(c),
        }
    }
    out
}

fn html_unescape(value: &str) -> String {
    value
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&quot;", "\"")
        .replace("&#39;", "'")
        .replace("&#x27;", "'")
        .replace("&amp;", "&")
}

fn attribute_value(raw_tag: &str, name: &str) -> Option<String> {
    let lower = raw_tag.to_ascii_lowercase();
    let needle = format!("{}=\"", name);
    let mut search = 0;
    while let Some(rel) = lower[search..].find(&needle) {
        let at = search + rel;
        let preceded_by_space = at > 0 && lower.as_bytes()[at - 1].is_ascii_whitespace();
        let value_start = at + needle.len();
        if preceded_by_space {
            let value_end = raw_tag[value_start..].find('"')? + value_start;
            return Some(raw_tag[value_start..value_end].to_string());
        }
        search = value_start;
    }
    None
}

fn write_bookmark_folder_body(folder: &BookmarkExportFolder, depth: usize, html: &mut String) {
    let indent = "    ".repeat(depth);
    for child in &folder.folders {
        html.push_str(&format!(
            "{}<DT><H3>{}</H3>\n{}<DL><p>\n",
            indent,
            html_escape(&child.name),
            indent
        ));
        write_bookmark_folder_body(child, depth + 1, html);
        html.push_str(&format!("{}</DL><p>\n", indent));
    }
    for item in &folder.items {
        let date_attr = item
            .date_added
            .as_ref()
            .map(|d| format!(" ADD_DATE=\"{}\"", html_escape(&d.0)))
            .unwrap_or_default();
        html.push_str(&format!(
            "{}<DT><A HREF=\"{}\"{}>{}</A>\n",
            indent,
            html_escape(&item.url),
            date_attr,
            html_escape(&item.title)
        ));
    }
}

/// One RFC 4180 CSV line with every field quoted.
pub fn csv_row(fields: &[&str]) -> String {
    let mut line = fields
        .iter()
        .map(|field| format!("\"{}\"", field.replace('"', "\"\"")))
        .collect::<Vec<_>>()
        .join(",");
    line.push('\n');
    line
}

fn dirs_or_home() -> Option<PathBuf> {
    std::env::var("HOME").ok().map(PathBuf::from)
}
