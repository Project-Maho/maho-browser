use crate::error::ExtensionError;
use crate::manifest::{ContentScript, ExtensionManifest};
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

/// Bundle of loaded extension data including manifest and all resources
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ExtensionBundle {
    pub manifest: ExtensionManifest,
    pub path: PathBuf,
    pub content_scripts: Vec<(String, String)>, // (filename, content)
    pub background_script: Option<String>,      // script content
}

/// Parse a manifest.json string into ExtensionManifest
pub fn parse_manifest(json: &str) -> Result<ExtensionManifest, ExtensionError> {
    let manifest: ExtensionManifest = serde_json::from_str(json)?;
    Ok(manifest)
}

/// Validate that a parsed manifest is a valid Manifest V3 extension
pub fn validate_manifest(manifest: &ExtensionManifest) -> Result<(), ExtensionError> {
    let mut errors = Vec::new();

    if manifest.manifest_version != 3 {
        return Err(ExtensionError::InvalidManifestVersion(
            manifest.manifest_version,
        ));
    }
    if manifest.name.is_empty() {
        errors.push("name must not be empty".to_string());
    }
    if manifest.version.is_empty() {
        errors.push("version must not be empty".to_string());
    }
    // Validate service worker background (MV3 requires service_worker, not scripts)
    if let Some(bg) = &manifest.background {
        if bg.service_worker.is_empty() {
            errors.push("background.service_worker must not be empty".to_string());
        }
    }
    // Validate content scripts have required matches
    for (i, cs) in manifest.content_scripts.iter().enumerate() {
        if cs.matches.is_empty() {
            errors.push(format!("content_scripts[{i}].matches must not be empty"));
        }
        if cs.js.is_empty() && cs.css.is_empty() {
            errors.push(format!(
                "content_scripts[{i}] must have at least one js or css file"
            ));
        }
    }
    // Validate permissions don't contain host-like patterns (should be in host_permissions)
    for perm in &manifest.permissions {
        if perm.contains("://") {
            errors.push(format!(
                "Permission '{perm}' looks like a host permission; use host_permissions instead"
            ));
        }
    }

    if errors.is_empty() {
        Ok(())
    } else {
        Err(ExtensionError::ValidationError(errors))
    }
}

/// Extract all content scripts from a manifest
pub fn extract_content_scripts(manifest: &ExtensionManifest) -> Vec<&ContentScript> {
    manifest.content_scripts.iter().collect()
}

/// Discover all valid extensions in a directory
///
/// Scans the given directory for subdirectories containing manifest.json files,
/// parses and validates each manifest, and returns a list of (path, manifest) tuples.
/// Invalid extensions are logged as warnings and skipped rather than causing errors.
pub fn discover_extensions(
    extensions_dir: &Path,
) -> Result<Vec<(PathBuf, ExtensionManifest)>, ExtensionError> {
    if !extensions_dir.exists() {
        return Err(ExtensionError::DirectoryNotFound(
            extensions_dir.to_string_lossy().to_string(),
        ));
    }

    let mut results = Vec::new();

    for entry in std::fs::read_dir(extensions_dir)? {
        let entry = entry?;
        let path = entry.path();

        // Only process subdirectories
        if !path.is_dir() {
            continue;
        }

        let manifest_path = path.join("manifest.json");
        if !manifest_path.exists() {
            continue;
        }

        let manifest_content = match std::fs::read_to_string(&manifest_path) {
            Ok(content) => content,
            Err(e) => {
                eprintln!(
                    "Warning: Failed to read manifest at {}: {}",
                    manifest_path.display(),
                    e
                );
                continue;
            }
        };

        let manifest = match parse_manifest(&manifest_content) {
            Ok(m) => m,
            Err(e) => {
                eprintln!(
                    "Warning: Failed to parse manifest at {}: {}",
                    manifest_path.display(),
                    e
                );
                continue;
            }
        };

        if let Err(e) = validate_manifest(&manifest) {
            eprintln!(
                "Warning: Invalid manifest at {}: {}",
                manifest_path.display(),
                e
            );
            continue;
        }

        results.push((path, manifest));
    }

    Ok(results)
}

/// Load all resources for an extension
///
/// Reads all JS/CSS files referenced in the manifest's content_scripts and
/// the background service worker script. Returns an ExtensionBundle containing
/// all loaded content.
pub fn load_extension_resources(
    ext_path: &Path,
    manifest: &ExtensionManifest,
) -> Result<ExtensionBundle, ExtensionError> {
    let mut content_scripts = Vec::new();

    // Load all JS and CSS files from content_scripts
    for cs in &manifest.content_scripts {
        for js_file in &cs.js {
            let file_path = ext_path.join(js_file);
            if !file_path.exists() {
                return Err(ExtensionError::MissingResource(
                    file_path.to_string_lossy().to_string(),
                ));
            }
            let content = std::fs::read_to_string(&file_path)?;
            content_scripts.push((js_file.clone(), content));
        }

        for css_file in &cs.css {
            let file_path = ext_path.join(css_file);
            if !file_path.exists() {
                return Err(ExtensionError::MissingResource(
                    file_path.to_string_lossy().to_string(),
                ));
            }
            let content = std::fs::read_to_string(&file_path)?;
            content_scripts.push((css_file.clone(), content));
        }
    }

    // Load background script if present
    let background_script = if let Some(bg) = &manifest.background {
        let script_path = ext_path.join(&bg.service_worker);
        if !script_path.exists() {
            return Err(ExtensionError::MissingResource(
                script_path.to_string_lossy().to_string(),
            ));
        }
        Some(std::fs::read_to_string(&script_path)?)
    } else {
        None
    };

    Ok(ExtensionBundle {
        manifest: manifest.clone(),
        path: ext_path.to_path_buf(),
        content_scripts,
        background_script,
    })
}
