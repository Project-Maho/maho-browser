use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ExtensionManifest {
    pub manifest_version: u8,
    pub name: String,
    pub version: String,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default)]
    pub icons: HashMap<String, String>,
    #[serde(default)]
    pub action: Option<ActionConfig>,
    #[serde(default)]
    pub background: Option<BackgroundConfig>,
    #[serde(default)]
    pub content_scripts: Vec<ContentScript>,
    #[serde(default)]
    pub permissions: Vec<String>,
    #[serde(default)]
    pub host_permissions: Vec<String>,
    #[serde(default)]
    pub web_accessible_resources: Vec<WebAccessibleResource>,
    #[serde(default)]
    pub content_security_policy: Option<ContentSecurityPolicy>,
    #[serde(default)]
    pub options_page: Option<String>,
    #[serde(default)]
    pub options_ui: Option<OptionsUI>,
    #[serde(default)]
    pub devtools_page: Option<String>,
    #[serde(default)]
    pub minimum_chrome_version: Option<String>,
    #[serde(flatten)]
    pub extra: HashMap<String, serde_json::Value>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ActionConfig {
    #[serde(default)]
    pub default_popup: Option<String>,
    #[serde(default)]
    pub default_icon: Option<serde_json::Value>,
    #[serde(default)]
    pub default_title: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BackgroundConfig {
    pub service_worker: String,
    #[serde(rename = "type", default)]
    pub module_type: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ContentScript {
    pub matches: Vec<String>,
    #[serde(default)]
    pub js: Vec<String>,
    #[serde(default)]
    pub css: Vec<String>,
    #[serde(default)]
    pub run_at: Option<String>,
    #[serde(default)]
    pub all_frames: bool,
    #[serde(default)]
    pub match_about_blank: bool,
    #[serde(default)]
    pub world: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WebAccessibleResource {
    pub resources: Vec<String>,
    pub matches: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ContentSecurityPolicy {
    #[serde(default)]
    pub extension_pages: Option<String>,
    #[serde(default)]
    pub sandbox: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OptionsUI {
    pub page: String,
    #[serde(default)]
    pub open_in_tab: bool,
}
