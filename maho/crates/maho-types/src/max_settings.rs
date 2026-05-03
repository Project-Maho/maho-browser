use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct MaxSettings {
    pub enabled: bool,
    pub page_previews: bool,
    pub tidy_tab_titles: bool,
    pub tidy_downloads: bool,
    pub tidy_tabs: bool,
    pub ai_command_bar: bool,
    pub instant_links: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct MaxSettingsUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub page_previews: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tidy_tab_titles: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tidy_downloads: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tidy_tabs: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ai_command_bar: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub instant_links: Option<bool>,
}
