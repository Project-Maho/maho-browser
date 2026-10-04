use std::collections::HashMap;

use serde::{Deserialize, Serialize};

// === Extension Bridge ===

pub const DEFAULT_PROFILE_KEY: &str = "default";

type ExtensionSyncCallback = Box<dyn Fn(&str, bool, bool) + Send + Sync>;

pub struct ExtensionBridge {
    profiles: HashMap<String, ProfileExtensionState>,
    primary_sync_profile: Option<String>,
}

#[derive(Default)]
struct ProfileExtensionState {
    extensions: HashMap<String, InstalledExtension>,
    sync_callback: Option<ExtensionSyncCallback>,
    side_panels: HashMap<String, SidePanelOptions>,
    baseline_synced: bool,
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
            profiles: HashMap::new(),
            primary_sync_profile: None,
        }
    }

    fn profile_mut(&mut self, profile_key: &str) -> &mut ProfileExtensionState {
        self.profiles.entry(profile_key.to_string()).or_default()
    }

    fn profile(&self, profile_key: &str) -> Option<&ProfileExtensionState> {
        self.profiles.get(profile_key)
    }

    pub fn is_baseline_synced_for_profile(&self, profile_key: &str) -> bool {
        self.profile(profile_key)
            .map(|profile| profile.baseline_synced)
            .unwrap_or(false)
    }

    pub fn mark_baseline_synced_for_profile(&mut self, profile_key: &str) {
        self.profile_mut(profile_key).baseline_synced = true;
    }

    pub fn is_primary_sync_profile(&mut self, profile_key: &str) -> bool {
        match &self.primary_sync_profile {
            Some(primary) => primary == profile_key,
            None => {
                self.primary_sync_profile = Some(profile_key.to_string());
                true
            }
        }
    }

    pub fn register_side_panel(&mut self, extension_id: &str, options: SidePanelOptions) {
        self.register_side_panel_for_profile(DEFAULT_PROFILE_KEY, extension_id, options);
    }

    pub fn register_side_panel_for_profile(
        &mut self,
        profile_key: &str,
        extension_id: &str,
        options: SidePanelOptions,
    ) {
        self.profile_mut(profile_key)
            .side_panels
            .insert(extension_id.to_string(), options);
    }

    pub fn unregister_side_panel(&mut self, extension_id: &str) {
        self.unregister_side_panel_for_profile(DEFAULT_PROFILE_KEY, extension_id);
    }

    pub fn unregister_side_panel_for_profile(&mut self, profile_key: &str, extension_id: &str) {
        if let Some(profile) = self.profiles.get_mut(profile_key) {
            profile.side_panels.remove(extension_id);
        }
    }

    pub fn list_side_panels(&self) -> Vec<RegisteredSidePanel> {
        self.list_side_panels_for_profile(DEFAULT_PROFILE_KEY)
    }

    pub fn list_side_panels_for_profile(&self, profile_key: &str) -> Vec<RegisteredSidePanel> {
        self.profile(profile_key)
            .map(|profile| &profile.side_panels)
            .into_iter()
            .flat_map(|side_panels| side_panels.iter())
            .map(|(ext_id, opt)| RegisteredSidePanel {
                extension_id: ext_id.clone(),
                path: opt.path.clone(),
                layout: opt.layout.clone(),
                default_width: opt.default_width,
            })
            .collect()
    }

    pub fn side_panel_options(&self, extension_id: &str) -> Option<SidePanelOptions> {
        self.side_panel_options_for_profile(DEFAULT_PROFILE_KEY, extension_id)
    }

    pub fn side_panel_options_for_profile(
        &self,
        profile_key: &str,
        extension_id: &str,
    ) -> Option<SidePanelOptions> {
        self.profile(profile_key)
            .and_then(|profile| profile.side_panels.get(extension_id).cloned())
    }

    // === Extension management for settings ===

    pub fn register_extension(&mut self, ext: InstalledExtension) {
        self.register_extension_for_profile(DEFAULT_PROFILE_KEY, ext);
    }

    pub fn register_extension_for_profile(&mut self, profile_key: &str, ext: InstalledExtension) {
        self.profile_mut(profile_key)
            .extensions
            .insert(ext.id.clone(), ext);
    }

    pub fn set_installed_extensions(&mut self, exts: Vec<InstalledExtension>) {
        self.set_installed_extensions_for_profile(DEFAULT_PROFILE_KEY, exts);
    }

    pub fn set_installed_extensions_for_profile(
        &mut self,
        profile_key: &str,
        exts: Vec<InstalledExtension>,
    ) {
        let profile = self.profile_mut(profile_key);
        profile.extensions.clear();
        for ext in exts {
            profile.extensions.insert(ext.id.clone(), ext);
        }
    }

    pub fn clear_installed_extensions(&mut self) {
        self.clear_installed_extensions_for_profile(DEFAULT_PROFILE_KEY);
    }

    pub fn clear_installed_extensions_for_profile(&mut self, profile_key: &str) {
        if let Some(profile) = self.profiles.get_mut(profile_key) {
            profile.extensions.clear();
            profile.baseline_synced = false;
        }
    }

    pub fn toggle_extension(&mut self, extension_id: &str) -> Option<bool> {
        self.toggle_extension_for_profile(DEFAULT_PROFILE_KEY, extension_id)
    }

    pub fn toggle_extension_for_profile(
        &mut self,
        profile_key: &str,
        extension_id: &str,
    ) -> Option<bool> {
        let ext = self
            .profile_mut(profile_key)
            .extensions
            .get_mut(extension_id)?;
        ext.enabled = !ext.enabled;
        Some(ext.enabled)
    }

    pub fn remove_extension(&mut self, extension_id: &str) -> Option<InstalledExtension> {
        self.remove_extension_for_profile(DEFAULT_PROFILE_KEY, extension_id)
    }

    pub fn remove_extension_for_profile(
        &mut self,
        profile_key: &str,
        extension_id: &str,
    ) -> Option<InstalledExtension> {
        self.profiles
            .get_mut(profile_key)
            .and_then(|profile| profile.extensions.remove(extension_id))
    }

    pub fn get_installed_extensions(&self) -> Vec<&InstalledExtension> {
        self.get_installed_extensions_for_profile(DEFAULT_PROFILE_KEY)
    }

    pub fn get_installed_extensions_for_profile(
        &self,
        profile_key: &str,
    ) -> Vec<&InstalledExtension> {
        self.profile(profile_key)
            .map(|profile| profile.extensions.values().collect())
            .unwrap_or_default()
    }

    pub fn get_extension(&self, extension_id: &str) -> Option<&InstalledExtension> {
        self.get_extension_for_profile(DEFAULT_PROFILE_KEY, extension_id)
    }

    pub fn get_extension_for_profile(
        &self,
        profile_key: &str,
        extension_id: &str,
    ) -> Option<&InstalledExtension> {
        self.profile(profile_key)
            .and_then(|profile| profile.extensions.get(extension_id))
    }

    pub fn set_extension_enabled_for_profile(
        &mut self,
        profile_key: &str,
        extension_id: &str,
        enabled: bool,
    ) {
        if let Some(ext) = self
            .profiles
            .get_mut(profile_key)
            .and_then(|profile| profile.extensions.get_mut(extension_id))
        {
            ext.enabled = enabled;
        }
    }

    pub fn extension_ids_for_profile(&self, profile_key: &str) -> Vec<String> {
        self.profile(profile_key)
            .map(|profile| profile.extensions.keys().cloned().collect())
            .unwrap_or_default()
    }

    pub fn get_password_provider_extension_id(&self) -> Option<String> {
        self.get_password_provider_extension_id_for_profile(DEFAULT_PROFILE_KEY)
    }

    pub fn get_password_provider_extension_id_for_profile(
        &self,
        profile_key: &str,
    ) -> Option<String> {
        let exts: Vec<InstalledExtension> = self
            .profile(profile_key)
            .map(|profile| profile.extensions.values().cloned().collect())
            .unwrap_or_default();
        find_password_provider(&exts)
    }

    pub fn register_extension_sync_callback(
        &mut self,
        callback: Option<Box<dyn Fn(&str, bool, bool) + Send + Sync>>,
    ) {
        self.register_extension_sync_callback_for_profile(DEFAULT_PROFILE_KEY, callback);
    }

    pub fn register_extension_sync_callback_for_profile(
        &mut self,
        profile_key: &str,
        callback: Option<Box<dyn Fn(&str, bool, bool) + Send + Sync>>,
    ) {
        self.profile_mut(profile_key).sync_callback = callback;
    }

    pub fn sync_callback_for_profile(
        &self,
        profile_key: &str,
    ) -> Option<&(dyn Fn(&str, bool, bool) + Send + Sync)> {
        self.profile(profile_key)
            .and_then(|profile| profile.sync_callback.as_deref())
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
