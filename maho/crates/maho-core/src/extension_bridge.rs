use std::collections::HashMap;

use maho_types::identifiers::{ExtensionId, TabId};
use maho_types::traits::shell_renderer::TabViewModel;
use serde::{Deserialize, Serialize};

// === Request/Response types for extension API ===

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TabQuery {
    #[serde(default)]
    pub active: Option<bool>,
    #[serde(default)]
    pub url: Option<String>,
    #[serde(default)]
    pub title: Option<String>,
    #[serde(default)]
    pub pinned: Option<bool>,
    #[serde(default)]
    pub muted: Option<bool>,
    #[serde(default)]
    pub current_window: Option<bool>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TabInfo {
    pub id: String,
    pub url: String,
    pub title: String,
    pub active: bool,
    pub pinned: bool,
    pub muted: bool,
    pub space_id: String,
}

impl TabInfo {
    pub fn from_view_model(vm: &TabViewModel, is_active: bool) -> Self {
        Self {
            id: vm.id.to_string(),
            url: vm.url.clone(),
            title: vm.title.clone(),
            active: is_active,
            pinned: vm.is_pinned,
            muted: vm.is_muted,
            space_id: vm.space_id.to_string(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CreateTabOptions {
    #[serde(default)]
    pub url: Option<String>,
    #[serde(default)]
    pub active: Option<bool>,
    #[serde(default)]
    pub pinned: Option<bool>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RuntimeMessage {
    pub from: ExtensionId,
    pub data: serde_json::Value,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct StorageChange {
    pub old_value: Option<serde_json::Value>,
    pub new_value: Option<serde_json::Value>,
}

// === Extension Bridge ===

pub struct ExtensionBridge {
    /// Per-extension in-memory storage (chrome.storage.local)
    storage: HashMap<String, HashMap<String, serde_json::Value>>,
    /// Message listeners: extension_id -> list of pending messages
    message_queue: HashMap<String, Vec<RuntimeMessage>>,
    /// Tab provider callback
    get_tabs: Box<dyn Fn() -> Vec<TabViewModel>>,
    /// Active tab ID provider
    get_active_tab: Box<dyn Fn() -> Option<TabId>>,
    /// Installed extensions registry
    pub extensions: HashMap<String, InstalledExtension>,
    /// Extension sync callback
    pub sync_callback: Option<Box<dyn Fn(&str, bool, bool) + Send + Sync>>,
    /// Registered side panels
    pub side_panels: HashMap<String, SidePanelOptions>,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum SidePanelLayout {
    Left,
    Right,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct SidePanelOptions {
    pub path: String,
    pub layout: SidePanelLayout,
    pub default_width: i32,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct RegisteredSidePanel {
    pub extension_id: String,
    pub path: String,
    pub layout: SidePanelLayout,
    pub default_width: i32,
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct InstalledExtension {
    pub id: String,
    pub name: String,
    pub version: String,
    #[serde(default)]
    pub description: String,
    pub enabled: bool,
    #[serde(default)]
    pub permissions: Vec<String>,
    #[serde(default)]
    pub manifest_version: u32,
}

impl ExtensionBridge {
    pub fn new(
        get_tabs: Box<dyn Fn() -> Vec<TabViewModel>>,
        get_active_tab: Box<dyn Fn() -> Option<TabId>>,
    ) -> Self {
        Self {
            storage: HashMap::new(),
            message_queue: HashMap::new(),
            get_tabs,
            get_active_tab,
            extensions: HashMap::new(),
            sync_callback: None,
            side_panels: HashMap::new(),
        }
    }

    pub fn register_side_panel(&mut self, extension_id: &str, options: SidePanelOptions) {
        self.side_panels.insert(extension_id.to_string(), options);
    }

    pub fn unregister_side_panel(&mut self, extension_id: &str) {
        self.side_panels.remove(extension_id);
    }

    pub fn list_side_panels(&self) -> Vec<RegisteredSidePanel> {
        self.side_panels
            .iter()
            .map(|(ext_id, opt)| RegisteredSidePanel {
                extension_id: ext_id.clone(),
                path: opt.path.clone(),
                layout: opt.layout.clone(),
                default_width: opt.default_width,
            })
            .collect()
    }

    // === chrome.tabs API ===

    /// chrome.tabs.query(queryInfo)
    pub fn tabs_query(&self, query: &TabQuery) -> Vec<TabInfo> {
        let all_tabs = (self.get_tabs)();
        let active_tab_id = (self.get_active_tab)();

        all_tabs
            .iter()
            .filter(|tab| {
                if let Some(active) = query.active {
                    let is_active = active_tab_id
                        .as_ref()
                        .map(|id| *id == tab.id)
                        .unwrap_or(false);
                    if is_active != active {
                        return false;
                    }
                }
                if let Some(ref url_pattern) = query.url {
                    if !tab.url.contains(url_pattern) {
                        return false;
                    }
                }
                if let Some(ref title_pattern) = query.title {
                    if !tab
                        .title
                        .to_lowercase()
                        .contains(&title_pattern.to_lowercase())
                    {
                        return false;
                    }
                }
                if let Some(pinned) = query.pinned {
                    if tab.is_pinned != pinned {
                        return false;
                    }
                }
                if let Some(muted) = query.muted {
                    if tab.is_muted != muted {
                        return false;
                    }
                }
                true
            })
            .map(|tab| {
                let is_active = active_tab_id
                    .as_ref()
                    .map(|id| *id == tab.id)
                    .unwrap_or(false);
                TabInfo::from_view_model(tab, is_active)
            })
            .collect()
    }

    /// chrome.tabs.get(tabId)
    pub fn tabs_get(&self, tab_id: &str) -> Option<TabInfo> {
        let all_tabs = (self.get_tabs)();
        let active_tab_id = (self.get_active_tab)();
        let target = TabId::new(tab_id);

        all_tabs.iter().find(|t| t.id == target).map(|tab| {
            let is_active = active_tab_id
                .as_ref()
                .map(|id| *id == tab.id)
                .unwrap_or(false);
            TabInfo::from_view_model(tab, is_active)
        })
    }

    /// chrome.tabs.getCurrent() — returns active tab
    pub fn tabs_get_current(&self) -> Option<TabInfo> {
        let active_id = (self.get_active_tab)()?;
        self.tabs_get(active_id.as_ref())
    }

    // === chrome.runtime API ===

    /// chrome.runtime.sendMessage(extensionId, message)
    pub fn runtime_send_message(
        &mut self,
        from_extension: ExtensionId,
        target_extension: &str,
        data: serde_json::Value,
    ) {
        let msg = RuntimeMessage {
            from: from_extension,
            data,
        };
        self.message_queue
            .entry(target_extension.to_string())
            .or_default()
            .push(msg);
    }

    /// Poll pending messages for an extension (chrome.runtime.onMessage)
    pub fn runtime_get_messages(&mut self, extension_id: &str) -> Vec<RuntimeMessage> {
        self.message_queue.remove(extension_id).unwrap_or_default()
    }

    // === chrome.storage.local API ===

    /// chrome.storage.local.get(keys)
    pub fn storage_get(
        &self,
        extension_id: &str,
        keys: &[String],
    ) -> HashMap<String, serde_json::Value> {
        let ext_storage = match self.storage.get(extension_id) {
            Some(s) => s,
            None => return HashMap::new(),
        };

        if keys.is_empty() {
            return ext_storage.clone();
        }

        keys.iter()
            .filter_map(|key| ext_storage.get(key).map(|v| (key.clone(), v.clone())))
            .collect()
    }

    /// chrome.storage.local.set(items)
    pub fn storage_set(
        &mut self,
        extension_id: &str,
        items: HashMap<String, serde_json::Value>,
    ) -> Vec<(String, StorageChange)> {
        let ext_storage = self.storage.entry(extension_id.to_string()).or_default();

        let mut changes = Vec::new();
        for (key, new_value) in items {
            let old_value = ext_storage.get(&key).cloned();
            ext_storage.insert(key.clone(), new_value.clone());
            changes.push((
                key,
                StorageChange {
                    old_value,
                    new_value: Some(new_value),
                },
            ));
        }
        changes
    }

    /// chrome.storage.local.remove(keys)
    pub fn storage_remove(
        &mut self,
        extension_id: &str,
        keys: &[String],
    ) -> Vec<(String, StorageChange)> {
        let ext_storage = match self.storage.get_mut(extension_id) {
            Some(s) => s,
            None => return Vec::new(),
        };

        let mut changes = Vec::new();
        for key in keys {
            if let Some(old_value) = ext_storage.remove(key) {
                changes.push((
                    key.clone(),
                    StorageChange {
                        old_value: Some(old_value),
                        new_value: None,
                    },
                ));
            }
        }
        changes
    }

    /// chrome.storage.local.clear()
    pub fn storage_clear(&mut self, extension_id: &str) {
        self.storage.remove(extension_id);
    }

    /// Get storage byte usage for an extension
    pub fn storage_get_bytes_in_use(&self, extension_id: &str) -> usize {
        self.storage
            .get(extension_id)
            .map(|s| s.iter().map(|(k, v)| k.len() + v.to_string().len()).sum())
            .unwrap_or(0)
    }

    // === Extension management for settings ===

    pub fn register_extension(&mut self, ext: InstalledExtension) {
        self.extensions.insert(ext.id.clone(), ext);
    }

    pub fn set_installed_extensions(&mut self, exts: Vec<InstalledExtension>) {
        self.extensions.clear();
        for ext in exts {
            self.extensions.insert(ext.id.clone(), ext);
        }
    }

    pub fn clear_installed_extensions(&mut self) {
        self.extensions.clear();
    }

    pub fn toggle_extension(&mut self, extension_id: &str) -> Option<bool> {
        if let Some(ext) = self.extensions.get_mut(extension_id) {
            ext.enabled = !ext.enabled;
            Some(ext.enabled)
        } else {
            None
        }
    }

    pub fn remove_extension(&mut self, extension_id: &str) -> Option<InstalledExtension> {
        self.storage.remove(extension_id);
        self.message_queue.remove(extension_id);
        self.extensions.remove(extension_id)
    }

    pub fn get_installed_extensions(&self) -> Vec<&InstalledExtension> {
        self.extensions.values().collect()
    }

    pub fn get_extension(&self, extension_id: &str) -> Option<&InstalledExtension> {
        self.extensions.get(extension_id)
    }

    pub fn get_password_provider_extension_id(&self) -> Option<String> {
        let exts: Vec<InstalledExtension> = self.extensions.values().cloned().collect();
        find_password_provider(&exts)
    }
}

pub fn find_password_provider(exts: &[InstalledExtension]) -> Option<String> {
    for ext in exts {
        if ext.enabled && ext.permissions.iter().any(|p| p == "passwords") {
            if ext.id != "nngceckbapebfimnlniiiahkandclblb" && ext.id != "aeblfdkhhhdcdjpifhhbdiojplfjncoa" {
                eprintln!("Warning: Extension {} has passwords permission but is not an approved password manager provider.", ext.id);
            }
        }
    }

    let mut exts_sorted = exts.to_vec();
    exts_sorted.sort_by(|a, b| a.id.cmp(&b.id));

    exts_sorted.iter()
        .find(|ext| {
            ext.enabled
                && (ext.id == "nngceckbapebfimnlniiiahkandclblb"
                    || ext.id == "aeblfdkhhhdcdjpifhhbdiojplfjncoa")
        })
        .map(|ext| ext.id.clone())
}

#[derive(Deserialize)]
#[serde(untagged)]
enum ExtensionPayload {
    V1 {
        schema_version: u32,
        extensions: Vec<InstalledExtension>,
    },
    V0(Vec<InstalledExtension>),
}

pub fn parse_extension_payload(json: &str) -> Result<Vec<InstalledExtension>, serde_json::Error> {
    match serde_json::from_str::<ExtensionPayload>(json)? {
        ExtensionPayload::V1 {
            schema_version,
            extensions,
        } => {
            if schema_version != 1 {
                return Err(serde::de::Error::custom(format!(
                    "Unsupported schema version: {}",
                    schema_version
                )));
            }
            Ok(extensions)
        }
        ExtensionPayload::V0(extensions) => Ok(extensions),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_v0_bare_array() {
        let json = r#"[{"id":"abc","name":"Test","version":"1.0","enabled":true}]"#;
        let result = parse_extension_payload(json);
        assert!(result.is_ok());
        assert_eq!(result.unwrap().len(), 1);
    }

    #[test]
    fn parse_v1_envelope() {
        let json = r#"{"schema_version":1,"extensions":[{"id":"abc","name":"Test","version":"1.0","enabled":true}]}"#;
        let result = parse_extension_payload(json);
        assert!(result.is_ok());
    }

    #[test]
    fn reject_unknown_version() {
        let json = r#"{"schema_version":99,"extensions":[]}"#;
        let result = parse_extension_payload(json);
        assert!(result.is_err());
    }

    #[test]
    fn detect_password_provider_by_permission() {
        let extensions = vec![
            InstalledExtension {
                id: "abc".into(),
                name: "Some Ext".into(),
                permissions: vec!["tabs".into()],
                enabled: true,
                ..Default::default()
            },
            InstalledExtension {
                id: "nngceckbapebfimnlniiiahkandclblb".into(),
                name: "Bitwarden".into(),
                permissions: vec!["passwords".into(), "storage".into()],
                enabled: true,
                ..Default::default()
            },
        ];
        let result = find_password_provider(&extensions);
        assert_eq!(result, Some("nngceckbapebfimnlniiiahkandclblb".to_string()));
    }

    #[test]
    fn detect_password_provider_deterministic_sorting() {
        let extensions = vec![
            InstalledExtension {
                id: "nngceckbapebfimnlniiiahkandclblb".into(),
                name: "Bitwarden".into(),
                permissions: vec!["passwords".into(), "storage".into()],
                enabled: true,
                ..Default::default()
            },
            InstalledExtension {
                id: "aeblfdkhhhdcdjpifhhbdiojplfjncoa".into(),
                name: "1Password".into(),
                permissions: vec!["passwords".into(), "storage".into()],
                enabled: true,
                ..Default::default()
            },
        ];
        let result = find_password_provider(&extensions);
        assert_eq!(result, Some("aeblfdkhhhdcdjpifhhbdiojplfjncoa".to_string()));
    }

    #[test]
    fn no_password_provider_when_none_have_permission() {
        let extensions = vec![InstalledExtension {
            id: "abc".into(),
            name: "Bitwarden Fake".into(),
            permissions: vec!["tabs".into()],
            enabled: true,
            ..Default::default()
        }];
        let result = find_password_provider(&extensions);
        assert_eq!(result, None);
    }
}
