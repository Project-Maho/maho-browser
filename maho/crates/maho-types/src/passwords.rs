use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, Default)]
#[serde(rename_all = "snake_case")]
pub enum PasswordProviderKind {
    #[default]
    #[serde(alias = "chromium")]
    #[serde(alias = "builtin")]
    #[serde(alias = "disabled")]
    #[serde(alias = "system")]
    MahoNative,
    #[serde(rename = "onepassword")]
    #[serde(alias = "1password")]
    OnePassword,
    Bitwarden,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PasswordProviderCapabilities {
    pub can_list_saved_passwords: bool,
    pub can_search_saved_passwords: bool,
    pub can_add_saved_passwords: bool,
    pub can_delete_saved_passwords: bool,
    pub can_edit_saved_passwords: bool,
    pub can_reveal_passwords: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PasswordProviderStatus {
    pub provider: PasswordProviderKind,
    pub display_name: String,
    pub description: String,
    pub is_available: bool,
    pub is_enabled: bool,
    pub capabilities: PasswordProviderCapabilities,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SavedPassword {
    pub id: String,
    pub domain: String,
    pub username: String,
    pub created_at: String,
    pub last_used: Option<String>,
    #[serde(default)]
    pub password: Option<String>,
}
