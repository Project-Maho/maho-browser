use crate::error::ExtensionError;
use crate::manifest::ExtensionManifest;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

static EXTENSION_ID_COUNTER: AtomicU64 = AtomicU64::new(1);

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoadedExtension {
    pub manifest: ExtensionManifest,
    pub content_scripts: Vec<(String, String)>,
    pub background_script: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[non_exhaustive]
pub enum ExtensionState {
    Enabled(LoadedExtension),
    Disabled(ExtensionManifest),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ContentScriptMatch {
    pub extension_id: String,
    pub js: Vec<String>,
    pub css: Vec<String>,
    pub run_at: String,
    pub all_frames: bool,
}

pub struct ExtensionLifecycle {
    extensions: HashMap<String, ExtensionState>,
    archived: HashMap<String, LoadedExtension>,
}

impl Default for ExtensionLifecycle {
    fn default() -> Self {
        Self::new()
    }
}

impl ExtensionLifecycle {
    pub fn new() -> Self {
        Self {
            extensions: HashMap::new(),
            archived: HashMap::new(),
        }
    }

    pub fn install(&mut self, ext: LoadedExtension) -> String {
        let id = Self::generate_extension_id();
        self.archived.insert(id.clone(), ext.clone());
        self.extensions
            .insert(id.clone(), ExtensionState::Enabled(ext));
        id
    }

    pub fn enable(&mut self, ext_id: &str) -> Result<(), ExtensionError> {
        match self.extensions.get(ext_id) {
            Some(ExtensionState::Enabled(_)) => Ok(()),
            Some(ExtensionState::Disabled(_)) => {
                let loaded = self
                    .archived
                    .get(ext_id)
                    .cloned()
                    .ok_or_else(|| ExtensionError::ExtensionNotFound(ext_id.to_string()))?;
                self.extensions
                    .insert(ext_id.to_string(), ExtensionState::Enabled(loaded));
                Ok(())
            }
            None => Err(ExtensionError::ExtensionNotFound(ext_id.to_string())),
        }
    }

    pub fn disable(&mut self, ext_id: &str) -> Result<(), ExtensionError> {
        match self.extensions.get(ext_id) {
            Some(ExtensionState::Disabled(_)) => Ok(()),
            Some(ExtensionState::Enabled(loaded)) => {
                self.archived.insert(ext_id.to_string(), loaded.clone());
                self.extensions.insert(
                    ext_id.to_string(),
                    ExtensionState::Disabled(loaded.manifest.clone()),
                );
                Ok(())
            }
            None => Err(ExtensionError::ExtensionNotFound(ext_id.to_string())),
        }
    }

    pub fn uninstall(&mut self, ext_id: &str) -> Result<(), ExtensionError> {
        let removed =
            self.extensions.remove(ext_id).is_some() || self.archived.remove(ext_id).is_some();
        if removed {
            Ok(())
        } else {
            Err(ExtensionError::ExtensionNotFound(ext_id.to_string()))
        }
    }

    pub fn get_enabled(&self) -> Vec<(&str, &LoadedExtension)> {
        self.extensions
            .iter()
            .filter_map(|(id, state)| match state {
                ExtensionState::Enabled(loaded) => Some((id.as_str(), loaded)),
                ExtensionState::Disabled(_) => None,
            })
            .collect()
    }

    pub fn get_content_scripts_for_url(&self, url: &str) -> Vec<ContentScriptMatch> {
        let mut results = Vec::new();

        for (ext_id, state) in &self.extensions {
            let loaded = match state {
                ExtensionState::Enabled(loaded) => loaded,
                ExtensionState::Disabled(_) => continue,
            };

            for script in &loaded.manifest.content_scripts {
                if !script
                    .matches
                    .iter()
                    .any(|pattern| Self::url_matches_pattern(url, pattern))
                {
                    continue;
                }

                let js = script
                    .js
                    .iter()
                    .filter_map(|name| Self::lookup_resource(&loaded.content_scripts, name))
                    .collect::<Vec<_>>();
                let css = script
                    .css
                    .iter()
                    .filter_map(|name| Self::lookup_resource(&loaded.content_scripts, name))
                    .collect::<Vec<_>>();

                results.push(ContentScriptMatch {
                    extension_id: ext_id.clone(),
                    js,
                    css,
                    run_at: script
                        .run_at
                        .clone()
                        .unwrap_or_else(|| "document_idle".to_string()),
                    all_frames: script.all_frames,
                });
            }
        }

        results
    }

    pub fn get_state(&self, ext_id: &str) -> Option<&ExtensionState> {
        self.extensions.get(ext_id)
    }

    pub fn list_all(&self) -> Vec<(&str, bool)> {
        self.extensions
            .iter()
            .map(|(id, state)| (id.as_str(), matches!(state, ExtensionState::Enabled(_))))
            .collect()
    }

    fn lookup_resource(resources: &[(String, String)], name: &str) -> Option<String> {
        resources
            .iter()
            .find(|(filename, _)| filename == name)
            .map(|(_, content)| content.clone())
    }

    fn generate_extension_id() -> String {
        let millis = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|duration| duration.as_millis())
            .unwrap_or(0);
        let counter = EXTENSION_ID_COUNTER.fetch_add(1, Ordering::Relaxed);
        format!("ext-{millis}-{counter}")
    }

    fn url_matches_pattern(url: &str, pattern: &str) -> bool {
        if pattern == "<all_urls>" {
            return true;
        }

        let (url_scheme, url_rest) = match url.split_once("://") {
            Some(parts) => parts,
            None => return false,
        };
        let (pattern_scheme, pattern_rest) = match pattern.split_once("://") {
            Some(parts) => parts,
            None => return false,
        };

        if pattern_scheme != "*" && pattern_scheme != url_scheme {
            return false;
        }

        let (url_host, url_path) = Self::split_host_path(url_rest);
        let (pattern_host, pattern_path) = Self::split_host_path(pattern_rest);

        Self::host_matches(url_host, pattern_host) && Self::path_matches(url_path, pattern_path)
    }

    fn split_host_path(input: &str) -> (&str, &str) {
        match input.find('/') {
            Some(idx) => (&input[..idx], &input[idx..]),
            None => (input, "/"),
        }
    }

    fn host_matches(host: &str, pattern: &str) -> bool {
        if pattern == "*" {
            return true;
        }

        if let Some(suffix) = pattern.strip_prefix("*.") {
            host == suffix || host.ends_with(&format!(".{suffix}"))
        } else {
            host == pattern
        }
    }

    fn path_matches(path: &str, pattern: &str) -> bool {
        if pattern == "*" || pattern == "/*" {
            return true;
        }

        if let Some(prefix) = pattern.strip_suffix("*") {
            path.starts_with(prefix)
        } else {
            path == pattern
        }
    }
}
