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

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PasswordProviderDescriptor {
    pub provider_id: String,
    pub display_name: String,
    pub description: String,
    pub extension_ids: Vec<String>,
    pub capabilities: PasswordProviderCapabilities,
}

pub fn get_provider_registry() -> Vec<PasswordProviderDescriptor> {
    vec![
        PasswordProviderDescriptor {
            provider_id: "maho_native".to_string(),
            display_name: "Maho Native".to_string(),
            description: "Use Maho's native password manager.".to_string(),
            extension_ids: vec![],
            capabilities: PasswordProviderCapabilities {
                can_list_saved_passwords: true,
                can_search_saved_passwords: true,
                can_add_saved_passwords: true,
                can_delete_saved_passwords: true,
                can_edit_saved_passwords: true,
                can_reveal_passwords: true,
            },
        },
        PasswordProviderDescriptor {
            provider_id: "bitwarden".to_string(),
            display_name: "Bitwarden".to_string(),
            description: "Use Bitwarden as the password provider.".to_string(),
            extension_ids: vec!["nngceckbapebfimnlniiiahkandclblb".to_string()],
            capabilities: PasswordProviderCapabilities {
                can_list_saved_passwords: false,
                can_search_saved_passwords: false,
                can_add_saved_passwords: false,
                can_delete_saved_passwords: false,
                can_edit_saved_passwords: false,
                can_reveal_passwords: false,
            },
        },
        PasswordProviderDescriptor {
            provider_id: "onepassword".to_string(),
            display_name: "1Password".to_string(),
            description: "Use 1Password as the password provider.".to_string(),
            extension_ids: vec!["aeblfdkhhhdcdjpifhhbdiojplfjncoa".to_string()],
            capabilities: PasswordProviderCapabilities {
                can_list_saved_passwords: false,
                can_search_saved_passwords: false,
                can_add_saved_passwords: false,
                can_delete_saved_passwords: false,
                can_edit_saved_passwords: false,
                can_reveal_passwords: false,
            },
        },
    ]
}
