use maho_extension::{
    discover_extensions, load_extension_resources, parse_manifest, ExtensionError,
    ExtensionLifecycle, LoadedExtension,
};
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

fn fixture_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/extensions")
}

fn test_extension_path() -> PathBuf {
    fixture_root().join("test-ext")
}

fn temp_dir(name: &str) -> PathBuf {
    let unique = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_nanos())
        .unwrap_or(0);

    std::env::temp_dir().join(format!("maho-extension-{name}-{unique}"))
}

#[test]
fn test_discover_extensions_finds_fixture_extension() {
    let discovered = discover_extensions(&fixture_root());

    assert!(discovered.is_ok());
    let discovered = match discovered {
        Ok(value) => value,
        Err(err) => panic!("discover_extensions failed: {err}"),
    };

    assert_eq!(discovered.len(), 1);
    let (ext_path, manifest) = &discovered[0];

    assert_eq!(ext_path.as_path(), test_extension_path().as_path());
    assert_eq!(manifest.manifest_version, 3);
    assert_eq!(manifest.name, "Test Extension");
    assert_eq!(manifest.version, "1.0.0");
    assert_eq!(
        manifest.description.as_deref(),
        Some("Test extension fixture for maho-extension integration tests")
    );
    assert_eq!(manifest.permissions, vec!["storage"]);
    assert_eq!(manifest.content_scripts.len(), 1);
    assert_eq!(
        manifest.content_scripts[0].matches,
        vec!["https://example.com/*"]
    );
    assert_eq!(manifest.content_scripts[0].js, vec!["content.js"]);
    assert_eq!(manifest.content_scripts[0].css, vec!["styles.css"]);
    assert_eq!(
        manifest
            .background
            .as_ref()
            .map(|bg| bg.service_worker.as_str()),
        Some("background.js")
    );
    assert_eq!(
        manifest.icons.get("16").map(|s| s.as_str()),
        Some("icon-16.png")
    );
    assert_eq!(
        manifest.icons.get("48").map(|s| s.as_str()),
        Some("icon-48.png")
    );
    assert_eq!(
        manifest.icons.get("128").map(|s| s.as_str()),
        Some("icon-128.png")
    );
}

#[test]
fn test_load_extension_resources_loads_manifest_js_and_css() {
    let manifest_path = test_extension_path().join("manifest.json");
    let manifest_json = match std::fs::read_to_string(&manifest_path) {
        Ok(content) => content,
        Err(err) => panic!("failed to read {}: {err}", manifest_path.display()),
    };
    let manifest = match parse_manifest(&manifest_json) {
        Ok(value) => value,
        Err(err) => panic!("failed to parse manifest: {err}"),
    };

    let bundle = match load_extension_resources(&test_extension_path(), &manifest) {
        Ok(value) => value,
        Err(err) => panic!("failed to load extension resources: {err}"),
    };

    assert_eq!(bundle.manifest.name, "Test Extension");
    assert_eq!(bundle.path, test_extension_path());
    assert_eq!(bundle.content_scripts.len(), 2);
    assert!(bundle
        .content_scripts
        .iter()
        .any(|(name, content)| name == "content.js" && content.contains("test extension loaded")));
    assert!(bundle
        .content_scripts
        .iter()
        .any(|(name, content)| name == "styles.css" && content.contains("background: red")));
    assert_eq!(
        bundle.background_script.as_deref(),
        Some("console.log(\"background script loaded\");\n")
    );
}

#[test]
fn test_extension_lifecycle_install_enable_match_disable_uninstall() {
    let manifest_path = test_extension_path().join("manifest.json");
    let manifest_json = match std::fs::read_to_string(&manifest_path) {
        Ok(content) => content,
        Err(err) => panic!("failed to read {}: {err}", manifest_path.display()),
    };
    let manifest = match parse_manifest(&manifest_json) {
        Ok(value) => value,
        Err(err) => panic!("failed to parse manifest: {err}"),
    };
    let bundle = match load_extension_resources(&test_extension_path(), &manifest) {
        Ok(value) => value,
        Err(err) => panic!("failed to load extension resources: {err}"),
    };

    let mut lifecycle = ExtensionLifecycle::new();
    let ext_id = lifecycle.install(LoadedExtension {
        manifest: bundle.manifest,
        content_scripts: bundle.content_scripts,
        background_script: bundle.background_script,
    });

    assert!(lifecycle.enable(&ext_id).is_ok());

    let matches = lifecycle.get_content_scripts_for_url("https://example.com");
    assert_eq!(matches.len(), 1);
    assert_eq!(matches[0].extension_id, ext_id);
    assert_eq!(
        matches[0].js,
        vec!["console.log(\"test extension loaded\");\n".to_string()]
    );
    assert_eq!(
        matches[0].css,
        vec!["body { background: red; }\n".to_string()]
    );

    assert!(lifecycle.disable(&ext_id).is_ok());
    assert!(lifecycle
        .get_content_scripts_for_url("https://example.com")
        .is_empty());

    assert!(lifecycle.enable(&ext_id).is_ok());
    assert!(lifecycle.uninstall(&ext_id).is_ok());
    assert!(lifecycle.get_state(&ext_id).is_none());
}

#[test]
fn test_discover_extensions_errors_on_missing_directory() {
    let missing_dir = temp_dir("missing-dir");
    let result = discover_extensions(&missing_dir);

    match result {
        Err(ExtensionError::DirectoryNotFound(path)) => {
            assert_eq!(path, missing_dir.to_string_lossy());
        }
        other => panic!("expected DirectoryNotFound, got {other:?}"),
    }
}

#[test]
fn test_discover_extensions_skips_directory_without_manifest() {
    let root = temp_dir("no-manifest-root");
    let ext_dir = root.join("broken-ext");
    assert!(std::fs::create_dir_all(&ext_dir).is_ok());
    assert!(std::fs::write(
        ext_dir.join("content.js"),
        "console.log('missing manifest');\n"
    )
    .is_ok());

    let discovered = match discover_extensions(&root) {
        Ok(value) => value,
        Err(err) => panic!("discover_extensions failed: {err}"),
    };

    assert!(discovered.is_empty());
}
