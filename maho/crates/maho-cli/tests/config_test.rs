use maho_cli::config::CliConfig;
use tempfile::TempDir;

#[test]
fn config_save_load_roundtrip() {
    let tmp = TempDir::new().unwrap();
    let path = tmp.path().join("cli.toml");

    let cfg = CliConfig {
        access_token: Some("tok_abc123xyz".to_string()),
        refresh_token: Some("ref_987zyx".to_string()),
        email: Some("user@example.com".to_string()),
        tier: Some("pro".to_string()),
    };

    cfg.save_to(&path).unwrap();
    let loaded = CliConfig::load_from(&path).unwrap();

    assert_eq!(loaded.access_token.as_deref(), Some("tok_abc123xyz"));
    assert_eq!(loaded.refresh_token.as_deref(), Some("ref_987zyx"));
    assert_eq!(loaded.email.as_deref(), Some("user@example.com"));
    assert_eq!(loaded.tier.as_deref(), Some("pro"));
}

#[test]
fn config_save_load_with_none_fields() {
    let tmp = TempDir::new().unwrap();
    let path = tmp.path().join("cli.toml");

    let cfg = CliConfig {
        access_token: None,
        refresh_token: None,
        email: Some("test@maho.co".to_string()),
        tier: None,
    };

    cfg.save_to(&path).unwrap();
    let loaded = CliConfig::load_from(&path).unwrap();

    assert_eq!(loaded.access_token, None);
    assert_eq!(loaded.refresh_token, None);
    assert_eq!(loaded.email.as_deref(), Some("test@maho.co"));
    assert_eq!(loaded.tier, None);
}

#[test]
fn config_redacted_display_masks_tokens() {
    let cfg = CliConfig {
        access_token: Some("abcdefghijklmnop".to_string()),
        refresh_token: Some("short".to_string()),
        email: Some("dev@maho.co".to_string()),
        tier: Some("free".to_string()),
    };

    let display = cfg.redacted_display();
    assert!(display.contains("dev@maho.co"));
    assert!(display.contains("free"));
    // Long token should be redacted
    assert!(!display.contains("abcdefghijklmnop"));
    assert!(display.contains("abcd...mnop"));
}

#[cfg(unix)]
#[test]
fn config_file_permissions() {
    use std::os::unix::fs::MetadataExt;

    let tmp = TempDir::new().unwrap();
    let path = tmp.path().join("cli.toml");

    let cfg = CliConfig {
        access_token: Some("secret".to_string()),
        ..Default::default()
    };

    cfg.save_to(&path).unwrap();

    let metadata = std::fs::metadata(&path).unwrap();
    let mode = metadata.mode() & 0o777;
    assert_eq!(mode, 0o600, "config file should have 0600 permissions");
}
