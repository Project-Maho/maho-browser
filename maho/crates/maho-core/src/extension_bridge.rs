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
    extensions: HashMap<String, InstalledExtension>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct InstalledExtension {
    pub id: String,
    pub name: String,
    pub version: String,
    #[serde(default)]
    pub description: String,
    pub enabled: bool,
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
        }
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
}
