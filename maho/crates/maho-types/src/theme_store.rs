use serde::{Deserialize, Serialize};

/// A theme available in the community theme store
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ThemeStoreEntry {
    pub id: String,
    pub name: String,
    pub description: String,
    pub author: String,
    pub version: String,
    /// The SpaceTheme JSON payload — will be decoded on the Swift side
    pub theme: serde_json::Value,
    /// Preview image URL
    pub preview_url: Option<String>,
    /// Number of downloads/installs
    pub download_count: u64,
    /// ISO 8601 date
    pub created_at: String,
    /// ISO 8601 date
    pub updated_at: String,
    /// Tags for filtering
    pub tags: Vec<String>,
}

/// Response from the theme store API
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ThemeStoreManifest {
    pub version: u32,
    pub themes: Vec<ThemeStoreEntry>,
}
