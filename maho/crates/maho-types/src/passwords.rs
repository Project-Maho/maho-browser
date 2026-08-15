use serde::{Deserialize, Serialize};

mod import_sources;

pub use import_sources::{
    get_import_source_registry, PasswordImportFileFormat, PasswordImportFormatDescriptor,
    PasswordImportSourceDescriptor,
};

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, Default)]
#[non_exhaustive]
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
    #[serde(alias = "one_password")]
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

#[doc = "Legacy plaintext password contract retained only until the Vault migration in Todo 54."]
#[derive(Clone, Serialize, Deserialize)]
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

impl std::fmt::Debug for SavedPassword {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter
            .debug_struct("SavedPassword")
            .field("id", &self.id)
            .field("domain", &self.domain)
            .field("username", &self.username)
            .field("created_at", &self.created_at)
            .field("last_used", &self.last_used)
            .field("password", &self.password.as_ref().map(|_| "[REDACTED]"))
            .finish()
    }
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
            },
        },
    ]
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn provider_registry_serialization_preserves_existing_contract() {
        // Given: the existing shared provider registry.
        let registry = get_provider_registry();

        // When: consumers serialize it for settings and provider selection.
        let serialized = serde_json::to_value(registry).unwrap();

        // Then: provider identifiers, extension IDs, and capabilities remain stable.
        assert_eq!(
            serialized,
            json!([
                {
                    "providerId": "maho_native",
                    "displayName": "Maho Native",
                    "description": "Use Maho's native password manager.",
                    "extensionIds": [],
                    "capabilities": {
                        "canListSavedPasswords": true,
                        "canSearchSavedPasswords": true,
                        "canAddSavedPasswords": true,
                        "canDeleteSavedPasswords": true,
                        "canEditSavedPasswords": true,
                    }
                },
                {
                    "providerId": "bitwarden",
                    "displayName": "Bitwarden",
                    "description": "Use Bitwarden as the password provider.",
                    "extensionIds": ["nngceckbapebfimnlniiiahkandclblb"],
                    "capabilities": {
                        "canListSavedPasswords": false,
                        "canSearchSavedPasswords": false,
                        "canAddSavedPasswords": false,
                        "canDeleteSavedPasswords": false,
                        "canEditSavedPasswords": false,
                    }
                },
                {
                    "providerId": "onepassword",
                    "displayName": "1Password",
                    "description": "Use 1Password as the password provider.",
                    "extensionIds": ["aeblfdkhhhdcdjpifhhbdiojplfjncoa"],
                    "capabilities": {
                        "canListSavedPasswords": false,
                        "canSearchSavedPasswords": false,
                        "canAddSavedPasswords": false,
                        "canDeleteSavedPasswords": false,
                        "canEditSavedPasswords": false,
                    }
                }
            ])
        );
    }
}
