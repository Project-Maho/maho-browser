use serde::{Deserialize, Serialize};

use crate::identifiers::ExtensionId;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum Permission {
    #[serde(rename = "tabs")]
    Tabs,
    #[serde(rename = "activeTab")]
    ActiveTab,
    #[serde(rename = "storage")]
    Storage,
    #[serde(rename = "notifications")]
    Notifications,
    #[serde(rename = "contextMenus")]
    ContextMenus,
    #[serde(rename = "webRequest")]
    WebRequest,
    #[serde(rename = "cookies")]
    Cookies,
    #[serde(rename = "history")]
    History,
    #[serde(rename = "bookmarks")]
    Bookmarks,
    #[serde(rename = "downloads")]
    Downloads,
    #[serde(rename = "management")]
    Management,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ContentScriptConfig {
    pub matches: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub js: Option<Vec<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub css: Option<Vec<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub run_at: Option<ScriptRunAt>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ScriptRunAt {
    DocumentStart,
    DocumentEnd,
    DocumentIdle,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BackgroundConfig {
    pub service_worker: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ExtensionManifest {
    pub manifest_version: u32,
    pub name: String,
    pub version: String,
    pub permissions: Vec<Permission>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub content_scripts: Option<Vec<ContentScriptConfig>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub background: Option<BackgroundConfig>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Extension {
    pub id: ExtensionId,
    pub name: String,
    pub version: String,
    pub manifest: ExtensionManifest,
    pub enabled: bool,
    pub permissions: Vec<Permission>,
    pub storage_quota_bytes: u64,
}
