use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[non_exhaustive]
#[serde(rename_all = "snake_case")]
pub enum PasswordImportFileFormat {
    Csv,
    OnePux,
    Json,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PasswordImportFormatDescriptor {
    pub format_id: String,
    pub label: String,
    pub file_format: PasswordImportFileFormat,
    pub file_extensions: Vec<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PasswordImportSourceDescriptor {
    pub source_id: String,
    pub display_name: String,
    pub description: String,
    pub file_import: bool,
    pub platform_independent: bool,
    pub formats: Vec<PasswordImportFormatDescriptor>,
}

pub fn get_import_source_registry() -> Vec<PasswordImportSourceDescriptor> {
    vec![
        PasswordImportSourceDescriptor {
            source_id: "onepassword".to_string(),
            display_name: "1Password".to_string(),
            description: "Import a user-exported 1Password CSV or 1PUX file.".to_string(),
            file_import: true,
            platform_independent: true,
            formats: vec![
                PasswordImportFormatDescriptor {
                    format_id: "onepassword_csv".to_string(),
                    label: "1Password CSV".to_string(),
                    file_format: PasswordImportFileFormat::Csv,
                    file_extensions: vec![".csv".to_string()],
                },
                PasswordImportFormatDescriptor {
                    format_id: "onepassword_1pux".to_string(),
                    label: "1PUX".to_string(),
                    file_format: PasswordImportFileFormat::OnePux,
                    file_extensions: vec![".1pux".to_string()],
                },
            ],
        },
        PasswordImportSourceDescriptor {
            source_id: "bitwarden".to_string(),
            display_name: "Bitwarden".to_string(),
            description: "Import a user-exported Bitwarden CSV or JSON file.".to_string(),
            file_import: true,
            platform_independent: true,
            formats: vec![
                PasswordImportFormatDescriptor {
                    format_id: "bitwarden_individual_csv".to_string(),
                    label: "Bitwarden individual CSV".to_string(),
                    file_format: PasswordImportFileFormat::Csv,
                    file_extensions: vec![".csv".to_string()],
                },
                PasswordImportFormatDescriptor {
                    format_id: "bitwarden_organization_csv".to_string(),
                    label: "Bitwarden organization CSV".to_string(),
                    file_format: PasswordImportFileFormat::Csv,
                    file_extensions: vec![".csv".to_string()],
                },
                PasswordImportFormatDescriptor {
                    format_id: "bitwarden_json".to_string(),
                    label: "Bitwarden JSON".to_string(),
                    file_format: PasswordImportFileFormat::Json,
                    file_extensions: vec![".json".to_string()],
                },
            ],
        },
        PasswordImportSourceDescriptor {
            source_id: "apple_passwords".to_string(),
            display_name: "Apple Passwords".to_string(),
            description: "Import a user-exported Apple Passwords CSV file.".to_string(),
            file_import: true,
            platform_independent: true,
            formats: vec![PasswordImportFormatDescriptor {
                format_id: "apple_passwords_csv".to_string(),
                label: "Apple Passwords CSV".to_string(),
                file_format: PasswordImportFileFormat::Csv,
                file_extensions: vec![".csv".to_string()],
            }],
        },
        PasswordImportSourceDescriptor {
            source_id: "keepassxc".to_string(),
            display_name: "KeePassXC".to_string(),
            description: "Import a user-exported KeePassXC CSV file.".to_string(),
            file_import: true,
            platform_independent: true,
            formats: vec![PasswordImportFormatDescriptor {
                format_id: "keepassxc_csv".to_string(),
                label: "KeePassXC CSV".to_string(),
                file_format: PasswordImportFileFormat::Csv,
                file_extensions: vec![".csv".to_string()],
            }],
        },
        PasswordImportSourceDescriptor {
            source_id: "keepass_classic".to_string(),
            display_name: "KeePass classic".to_string(),
            description: "Import a user-exported KeePass classic CSV file.".to_string(),
            file_import: true,
            platform_independent: true,
            formats: vec![PasswordImportFormatDescriptor {
                format_id: "keepass_classic_csv".to_string(),
                label: "KeePass classic CSV".to_string(),
                file_format: PasswordImportFileFormat::Csv,
                file_extensions: vec![".csv".to_string()],
            }],
        },
    ]
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn import_source_registry_is_separate_from_fill_providers() {
        // Given: file-based password import sources from the parser contract.
        let registry = get_import_source_registry();

        // When: consumers serialize source descriptors for Settings import cards.
        let serialized = serde_json::to_value(registry).unwrap();

        // Then: Apple is an import source only, and every file source is platform-independent.
        assert_eq!(
            serialized,
            json!([
                {
                    "sourceId": "onepassword",
                    "displayName": "1Password",
                    "description": "Import a user-exported 1Password CSV or 1PUX file.",
                    "fileImport": true,
                    "platformIndependent": true,
                    "formats": [
                        {
                            "formatId": "onepassword_csv",
                            "label": "1Password CSV",
                            "fileFormat": "csv",
                            "fileExtensions": [".csv"]
                        },
                        {
                            "formatId": "onepassword_1pux",
                            "label": "1PUX",
                            "fileFormat": "one_pux",
                            "fileExtensions": [".1pux"]
                        }
                    ]
                },
                {
                    "sourceId": "bitwarden",
                    "displayName": "Bitwarden",
                    "description": "Import a user-exported Bitwarden CSV or JSON file.",
                    "fileImport": true,
                    "platformIndependent": true,
                    "formats": [
                        {
                            "formatId": "bitwarden_individual_csv",
                            "label": "Bitwarden individual CSV",
                            "fileFormat": "csv",
                            "fileExtensions": [".csv"]
                        },
                        {
                            "formatId": "bitwarden_organization_csv",
                            "label": "Bitwarden organization CSV",
                            "fileFormat": "csv",
                            "fileExtensions": [".csv"]
                        },
                        {
                            "formatId": "bitwarden_json",
                            "label": "Bitwarden JSON",
                            "fileFormat": "json",
                            "fileExtensions": [".json"]
                        }
                    ]
                },
                {
                    "sourceId": "apple_passwords",
                    "displayName": "Apple Passwords",
                    "description": "Import a user-exported Apple Passwords CSV file.",
                    "fileImport": true,
                    "platformIndependent": true,
                    "formats": [
                        {
                            "formatId": "apple_passwords_csv",
                            "label": "Apple Passwords CSV",
                            "fileFormat": "csv",
                            "fileExtensions": [".csv"]
                        }
                    ]
                },
                {
                    "sourceId": "keepassxc",
                    "displayName": "KeePassXC",
                    "description": "Import a user-exported KeePassXC CSV file.",
                    "fileImport": true,
                    "platformIndependent": true,
                    "formats": [
                        {
                            "formatId": "keepassxc_csv",
                            "label": "KeePassXC CSV",
                            "fileFormat": "csv",
                            "fileExtensions": [".csv"]
                        }
                    ]
                },
                {
                    "sourceId": "keepass_classic",
                    "displayName": "KeePass classic",
                    "description": "Import a user-exported KeePass classic CSV file.",
                    "fileImport": true,
                    "platformIndependent": true,
                    "formats": [
                        {
                            "formatId": "keepass_classic_csv",
                            "label": "KeePass classic CSV",
                            "fileFormat": "csv",
                            "fileExtensions": [".csv"]
                        }
                    ]
                }
            ])
        );
    }
}
