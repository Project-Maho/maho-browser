use std::collections::HashMap;

use serde::{Deserialize, Serialize};

// === Extension Bridge ===

pub struct ExtensionBridge {
    /// Installed extensions registry
    pub extensions: HashMap<String, InstalledExtension>,
    /// Extension sync callback
    pub sync_callback: Option<Box<dyn Fn(&str, bool, bool) + Send + Sync>>,
    /// Registered side panels
    pub side_panels: HashMap<String, SidePanelOptions>,
    /// H6: false until the first installed-extensions snapshot has been absorbed.
    /// The first snapshot is a startup baseline and must NOT be re-pushed to sync
    /// (the in-memory mirror is empty at startup, so every entry would otherwise
    /// look "changed" and re-upload on every restart).
    pub baseline_synced: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum SidePanelLayout {
    Left,
    Right,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct SidePanelOptions {
    pub path: String,
    pub layout: SidePanelLayout,
    pub default_width: i32,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct RegisteredSidePanel {
    pub extension_id: String,
    pub path: String,
    pub layout: SidePanelLayout,
    pub default_width: i32,
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct InstalledExtension {
    pub id: String,
    pub name: String,
    pub version: String,
    #[serde(default)]
    pub description: String,
    pub enabled: bool,
    #[serde(default)]
    pub permissions: Vec<String>,
    #[serde(default)]
    pub manifest_version: u32,
}

impl Default for ExtensionBridge {
    fn default() -> Self {
        Self::new()
    }
}

impl ExtensionBridge {
    pub fn new() -> Self {
        Self {
            extensions: HashMap::new(),
            sync_callback: None,
            side_panels: HashMap::new(),
            baseline_synced: false,
        }
    }

    pub fn register_side_panel(&mut self, extension_id: &str, options: SidePanelOptions) {
        self.side_panels.insert(extension_id.to_string(), options);
    }

    pub fn unregister_side_panel(&mut self, extension_id: &str) {
        self.side_panels.remove(extension_id);
    }

    pub fn list_side_panels(&self) -> Vec<RegisteredSidePanel> {
        self.side_panels
            .iter()
            .map(|(ext_id, opt)| RegisteredSidePanel {
                extension_id: ext_id.clone(),
                path: opt.path.clone(),
                layout: opt.layout.clone(),
                default_width: opt.default_width,
            })
            .collect()
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
        self.extensions.remove(extension_id)
    }

    pub fn get_installed_extensions(&self) -> Vec<&InstalledExtension> {
        self.extensions.values().collect()
    }

    pub fn get_extension(&self, extension_id: &str) -> Option<&InstalledExtension> {
        self.extensions.get(extension_id)
    }

    pub fn get_password_provider_extension_id(&self) -> Option<String> {
        let exts: Vec<InstalledExtension> = self.extensions.values().cloned().collect();
        find_password_provider(&exts)
    }
}

pub fn find_password_provider(exts: &[InstalledExtension]) -> Option<String> {
    let mut exts_sorted = exts.to_vec();
    exts_sorted.sort_by(|a, b| a.id.cmp(&b.id));

    exts_sorted
        .iter()
        .find(|ext| {
            ext.enabled
                && (ext.id == "nngceckbapebfimnlniiiahkandclblb"
                    || ext.id == "aeblfdkhhhdcdjpifhhbdiojplfjncoa")
        })
        .map(|ext| ext.id.clone())
}

#[derive(Deserialize)]
#[serde(untagged)]
enum ExtensionPayload {
    V1 {
        schema_version: u32,
        extensions: Vec<InstalledExtension>,
    },
    V0(Vec<InstalledExtension>),
}

pub fn parse_extension_payload(json: &str) -> Result<Vec<InstalledExtension>, serde_json::Error> {
    match serde_json::from_str::<ExtensionPayload>(json)? {
        ExtensionPayload::V1 {
            schema_version,
            extensions,
        } => {
            if schema_version != 1 {
                return Err(serde::de::Error::custom(format!(
                    "Unsupported schema version: {}",
                    schema_version
                )));
            }
            Ok(extensions)
        }
        ExtensionPayload::V0(extensions) => Ok(extensions),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_v0_bare_array() {
        let json = r#"[{"id":"abc","name":"Test","version":"1.0","enabled":true}]"#;
        let result = parse_extension_payload(json);
        assert!(result.is_ok());
        assert_eq!(result.unwrap().len(), 1);
    }

    #[test]
    fn parse_v1_envelope() {
        let json = r#"{"schema_version":1,"extensions":[{"id":"abc","name":"Test","version":"1.0","enabled":true}]}"#;
        let result = parse_extension_payload(json);
        assert!(result.is_ok());
    }

    #[test]
    fn reject_unknown_version() {
        let json = r#"{"schema_version":99,"extensions":[]}"#;
        let result = parse_extension_payload(json);
        assert!(result.is_err());
    }

    #[test]
    fn detect_password_provider_by_permission() {
        let extensions = vec![
            InstalledExtension {
                id: "abc".into(),
                name: "Some Ext".into(),
                permissions: vec!["tabs".into()],
                enabled: true,
                ..Default::default()
            },
            InstalledExtension {
                id: "nngceckbapebfimnlniiiahkandclblb".into(),
                name: "Bitwarden".into(),
                permissions: vec!["passwords".into(), "storage".into()],
                enabled: true,
                ..Default::default()
            },
        ];
        let result = find_password_provider(&extensions);
        assert_eq!(result, Some("nngceckbapebfimnlniiiahkandclblb".to_string()));
    }

    #[test]
    fn detect_password_provider_deterministic_sorting() {
        let extensions = vec![
            InstalledExtension {
                id: "nngceckbapebfimnlniiiahkandclblb".into(),
                name: "Bitwarden".into(),
                permissions: vec!["passwords".into(), "storage".into()],
                enabled: true,
                ..Default::default()
            },
            InstalledExtension {
                id: "aeblfdkhhhdcdjpifhhbdiojplfjncoa".into(),
                name: "1Password".into(),
                permissions: vec!["passwords".into(), "storage".into()],
                enabled: true,
                ..Default::default()
            },
        ];
        let result = find_password_provider(&extensions);
        assert_eq!(result, Some("aeblfdkhhhdcdjpifhhbdiojplfjncoa".to_string()));
    }

    #[test]
    fn no_password_provider_when_none_have_permission() {
        let extensions = vec![InstalledExtension {
            id: "abc".into(),
            name: "Bitwarden Fake".into(),
            permissions: vec!["tabs".into()],
            enabled: true,
            ..Default::default()
        }];
        let result = find_password_provider(&extensions);
        assert_eq!(result, None);
    }
}
