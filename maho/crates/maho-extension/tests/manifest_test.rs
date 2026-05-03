use maho_extension::{extract_content_scripts, parse_manifest, validate_manifest, ExtensionError};

/// Test 1: Parse valid minimal manifest (name, version, manifest_version=3)
#[test]
fn test_parse_minimal_manifest() {
    let json = r#"{
        "manifest_version": 3,
        "name": "My Extension",
        "version": "1.0.0"
    }"#;

    let manifest = parse_manifest(json).unwrap();
    assert_eq!(manifest.manifest_version, 3);
    assert_eq!(manifest.name, "My Extension");
    assert_eq!(manifest.version, "1.0.0");
    assert!(manifest.description.is_none());
}

/// Test 2: Parse full manifest with all fields
#[test]
fn test_parse_full_manifest() {
    let json = r#"{
        "manifest_version": 3,
        "name": "Full Extension",
        "version": "2.0.0",
        "description": "A comprehensive extension",
        "icons": {"16": "icon16.png", "48": "icon48.png", "128": "icon128.png"},
        "action": {
            "default_popup": "popup.html",
            "default_icon": {"16": "icon16.png"},
            "default_title": "Click me"
        },
        "background": {
            "service_worker": "background.js",
            "type": "module"
        },
        "content_scripts": [{
            "matches": ["<all_urls>"],
            "js": ["content.js"],
            "css": ["styles.css"],
            "run_at": "document_idle",
            "all_frames": false,
            "match_about_blank": false,
            "world": "ISOLATED"
        }],
        "permissions": ["storage", "tabs"],
        "host_permissions": ["*://*.example.com/*"],
        "web_accessible_resources": [{
            "resources": ["resource.js"],
            "matches": ["<all_urls>"]
        }],
        "content_security_policy": {
            "extension_pages": "script-src 'self'",
            "sandbox": "sandbox allow-scripts"
        },
        "options_page": "options.html",
        "options_ui": {
            "page": "options.html",
            "open_in_tab": true
        },
        "devtools_page": "devtools.html",
        "minimum_chrome_version": "88"
    }"#;

    let manifest = parse_manifest(json).unwrap();
    assert_eq!(manifest.manifest_version, 3);
    assert_eq!(manifest.name, "Full Extension");
    assert_eq!(manifest.version, "2.0.0");
    assert_eq!(
        manifest.description.as_deref(),
        Some("A comprehensive extension")
    );
    assert_eq!(manifest.icons.len(), 3);
    assert!(manifest.action.is_some());
    assert!(manifest.background.is_some());
    assert_eq!(manifest.content_scripts.len(), 1);
    assert_eq!(manifest.permissions.len(), 2);
    assert_eq!(manifest.host_permissions.len(), 1);
    assert_eq!(manifest.web_accessible_resources.len(), 1);
    assert!(manifest.content_security_policy.is_some());
    assert_eq!(manifest.options_page.as_deref(), Some("options.html"));
    assert!(manifest.options_ui.is_some());
    assert_eq!(manifest.devtools_page.as_deref(), Some("devtools.html"));
    assert_eq!(manifest.minimum_chrome_version.as_deref(), Some("88"));
}

/// Test 3: Parse manifest with action, background, content_scripts
#[test]
fn test_parse_manifest_with_scripts() {
    let json = r#"{
        "manifest_version": 3,
        "name": "Script Extension",
        "version": "1.0.0",
        "background": {"service_worker": "bg.js"},
        "content_scripts": [{
            "matches": ["https://*.example.com/*"],
            "js": ["script.js"]
        }]
    }"#;

    let manifest = parse_manifest(json).unwrap();
    assert!(manifest.background.is_some());
    assert_eq!(
        manifest.background.as_ref().unwrap().service_worker,
        "bg.js"
    );
    assert_eq!(manifest.content_scripts.len(), 1);
    assert_eq!(
        manifest.content_scripts[0].matches[0],
        "https://*.example.com/*"
    );
    assert_eq!(manifest.content_scripts[0].js[0], "script.js");
}

/// Test 4: Validate rejects manifest_version != 3
#[test]
fn test_validate_rejects_wrong_version() {
    let json = r#"{
        "manifest_version": 2,
        "name": "Old Extension",
        "version": "1.0.0"
    }"#;

    let manifest = parse_manifest(json).unwrap();
    let result = validate_manifest(&manifest);

    match result {
        Err(ExtensionError::InvalidManifestVersion(2)) => (), // Expected
        _ => panic!("Expected InvalidManifestVersion(2) error"),
    }
}

/// Test 5: Validate rejects empty name
#[test]
fn test_validate_rejects_empty_name() {
    let json = r#"{
        "manifest_version": 3,
        "name": "",
        "version": "1.0.0"
    }"#;

    let manifest = parse_manifest(json).unwrap();
    let result = validate_manifest(&manifest);

    match result {
        Err(ExtensionError::ValidationError(errors)) => {
            assert!(errors.iter().any(|e| e.contains("name must not be empty")));
        }
        _ => panic!("Expected ValidationError for empty name"),
    }
}

/// Test 6: Validate rejects empty version
#[test]
fn test_validate_rejects_empty_version() {
    let json = r#"{
        "manifest_version": 3,
        "name": "Extension",
        "version": ""
    }"#;

    let manifest = parse_manifest(json).unwrap();
    let result = validate_manifest(&manifest);

    match result {
        Err(ExtensionError::ValidationError(errors)) => {
            assert!(errors
                .iter()
                .any(|e| e.contains("version must not be empty")));
        }
        _ => panic!("Expected ValidationError for empty version"),
    }
}

/// Test 7: Validate rejects content_script with no matches
#[test]
fn test_validate_rejects_content_script_no_matches() {
    let json = r#"{
        "manifest_version": 3,
        "name": "Bad Extension",
        "version": "1.0.0",
        "content_scripts": [{
            "matches": [],
            "js": ["script.js"]
        }]
    }"#;

    let manifest = parse_manifest(json).unwrap();
    let result = validate_manifest(&manifest);

    match result {
        Err(ExtensionError::ValidationError(errors)) => {
            assert!(errors
                .iter()
                .any(|e| e.contains("matches must not be empty")));
        }
        _ => panic!("Expected ValidationError for empty matches"),
    }
}

/// Test 8: Validate rejects content_script with no js/css
#[test]
fn test_validate_rejects_content_script_no_resources() {
    let json = r#"{
        "manifest_version": 3,
        "name": "Bad Extension",
        "version": "1.0.0",
        "content_scripts": [{
            "matches": ["<all_urls>"]
        }]
    }"#;

    let manifest = parse_manifest(json).unwrap();
    let result = validate_manifest(&manifest);

    match result {
        Err(ExtensionError::ValidationError(errors)) => {
            assert!(errors
                .iter()
                .any(|e| e.contains("must have at least one js or css file")));
        }
        _ => panic!("Expected ValidationError for missing js/css"),
    }
}

/// Test 9: Validate catches host-like permissions
#[test]
fn test_validate_catches_host_like_permissions() {
    let json = r#"{
        "manifest_version": 3,
        "name": "Bad Extension",
        "version": "1.0.0",
        "permissions": ["storage", "https://example.com/*"]
    }"#;

    let manifest = parse_manifest(json).unwrap();
    let result = validate_manifest(&manifest);

    match result {
        Err(ExtensionError::ValidationError(errors)) => {
            assert!(errors
                .iter()
                .any(|e| e.contains("host permission") || e.contains("://")));
        }
        _ => panic!("Expected ValidationError for host-like permission"),
    }
}

/// Test 10: Validate passes for well-formed manifest
#[test]
fn test_validate_passes_well_formed_manifest() {
    let json = r#"{
        "manifest_version": 3,
        "name": "Good Extension",
        "version": "1.0.0",
        "permissions": ["storage", "tabs"],
        "host_permissions": ["*://example.com/*"]
    }"#;

    let manifest = parse_manifest(json).unwrap();
    let result = validate_manifest(&manifest);
    assert!(result.is_ok());
}

/// Test 11: Extract content scripts returns correct count
#[test]
fn test_extract_content_scripts_count() {
    let json = r#"{
        "manifest_version": 3,
        "name": "Multi-Script Extension",
        "version": "1.0.0",
        "content_scripts": [
            {"matches": ["*://*.example1.com/*"], "js": ["script1.js"]},
            {"matches": ["*://*.example2.com/*"], "js": ["script2.js"]},
            {"matches": ["*://*.example3.com/*"], "js": ["script3.js"]}
        ]
    }"#;

    let manifest = parse_manifest(json).unwrap();
    let scripts = extract_content_scripts(&manifest);
    assert_eq!(scripts.len(), 3);
    assert_eq!(scripts[0].js[0], "script1.js");
    assert_eq!(scripts[1].js[0], "script2.js");
    assert_eq!(scripts[2].js[0], "script3.js");
}

/// Test 12: Parse with unknown extra fields (serde flatten)
#[test]
fn test_parse_with_extra_fields() {
    let json = r#"{
        "manifest_version": 3,
        "name": "Extended Extension",
        "version": "1.0.0",
        "custom_field": "custom_value",
        "nested_object": {"key": "value"}
    }"#;

    let manifest = parse_manifest(json).unwrap();
    assert_eq!(manifest.manifest_version, 3);
    assert_eq!(manifest.name, "Extended Extension");
    assert!(manifest.extra.contains_key("custom_field"));
    assert!(manifest.extra.contains_key("nested_object"));
    assert_eq!(
        manifest.extra.get("custom_field").unwrap().as_str(),
        Some("custom_value")
    );
}

/// Test 13: Round-trip serialize/deserialize
#[test]
fn test_roundtrip_serialize_deserialize() {
    let original = r#"{
        "manifest_version": 3,
        "name": "Test Extension",
        "version": "1.0.0",
        "description": "A test extension",
        "permissions": ["storage"]
    }"#;

    let manifest = parse_manifest(original).unwrap();
    let serialized = serde_json::to_string(&manifest).unwrap();
    let deserialized: serde_json::Value = serde_json::from_str(&serialized).unwrap();

    assert_eq!(deserialized["manifest_version"], 3);
    assert_eq!(deserialized["name"], "Test Extension");
    assert_eq!(deserialized["version"], "1.0.0");
    assert_eq!(deserialized["description"], "A test extension");
    assert_eq!(deserialized["permissions"][0], "storage");
}

/// Test 14: Validate empty background service_worker
#[test]
fn test_validate_empty_service_worker() {
    let json = r#"{
        "manifest_version": 3,
        "name": "Bad Extension",
        "version": "1.0.0",
        "background": {"service_worker": ""}
    }"#;

    let manifest = parse_manifest(json).unwrap();
    let result = validate_manifest(&manifest);

    match result {
        Err(ExtensionError::ValidationError(errors)) => {
            assert!(errors
                .iter()
                .any(|e| e.contains("service_worker must not be empty")));
        }
        _ => panic!("Expected ValidationError for empty service_worker"),
    }
}

fn fixture_path() -> std::path::PathBuf {
    let mut p = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    p.push("../../tests/fixtures/test-extension");
    p
}

/// Test 15: Load fixture manifest.json from disk, parse, validate, extract
#[test]
fn test_fixture_manifest_load_and_validate() {
    let manifest_path = fixture_path().join("manifest.json");
    let json = std::fs::read_to_string(&manifest_path)
        .unwrap_or_else(|e| panic!("Failed to read {}: {e}", manifest_path.display()));

    let manifest = parse_manifest(&json).unwrap();
    assert_eq!(manifest.manifest_version, 3);
    assert_eq!(manifest.name, "Maho Test Extension");
    assert_eq!(manifest.version, "1.0.0");
    assert_eq!(
        manifest.description.as_deref(),
        Some("A test Chrome extension for validating Maho's ExtensionBridge")
    );

    validate_manifest(&manifest).unwrap();
}

/// Test 16: Fixture content scripts extraction
#[test]
fn test_fixture_extract_content_scripts() {
    let json = std::fs::read_to_string(fixture_path().join("manifest.json")).unwrap();
    let manifest = parse_manifest(&json).unwrap();
    let scripts = extract_content_scripts(&manifest);

    assert_eq!(scripts.len(), 1);
    assert_eq!(scripts[0].matches, vec!["<all_urls>"]);
    assert_eq!(scripts[0].js, vec!["content.js"]);
    assert!(scripts[0].css.is_empty());
    assert_eq!(scripts[0].run_at.as_deref(), Some("document_end"));
    assert!(!scripts[0].all_frames);
}

/// Test 17: Fixture background service worker
#[test]
fn test_fixture_background_config() {
    let json = std::fs::read_to_string(fixture_path().join("manifest.json")).unwrap();
    let manifest = parse_manifest(&json).unwrap();

    let bg = manifest
        .background
        .as_ref()
        .expect("background should be present");
    assert_eq!(bg.service_worker, "background.js");
    assert_eq!(bg.module_type.as_deref(), Some("module"));
}

/// Test 18: Fixture permissions and host_permissions
#[test]
fn test_fixture_permissions() {
    let json = std::fs::read_to_string(fixture_path().join("manifest.json")).unwrap();
    let manifest = parse_manifest(&json).unwrap();

    assert_eq!(manifest.permissions, vec!["tabs"]);
    assert_eq!(manifest.host_permissions, vec!["<all_urls>"]);
}

/// Test 19: Fixture action config
#[test]
fn test_fixture_action_config() {
    let json = std::fs::read_to_string(fixture_path().join("manifest.json")).unwrap();
    let manifest = parse_manifest(&json).unwrap();

    let action = manifest.action.as_ref().expect("action should be present");
    assert_eq!(action.default_title.as_deref(), Some("Maho Test Extension"));
    assert!(action.default_popup.is_none());
}

/// Test 20: Fixture JS files exist on disk
#[test]
fn test_fixture_js_files_exist() {
    let base = fixture_path();
    assert!(base.join("content.js").exists(), "content.js missing");
    assert!(base.join("background.js").exists(), "background.js missing");
}
