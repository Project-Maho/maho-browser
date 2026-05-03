use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SearchEngine {
    pub id: String,
    pub name: String,
    pub url_template: String, // e.g. "https://www.google.com/search?q={query}"
    pub shortcut: Option<String>, // e.g. "@g" for Google
    pub icon_url: Option<String>,
    pub is_default: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SearchEngineViewModel {
    pub id: String,
    pub name: String,
    pub shortcut: Option<String>,
    pub icon_url: Option<String>,
    pub is_default: bool,
}
