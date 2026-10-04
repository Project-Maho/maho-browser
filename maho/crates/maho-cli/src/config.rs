use std::fs;
use std::io::Write;
use std::path::PathBuf;

use anyhow::{Context, Result};
use directories::ProjectDirs;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct CliConfig {
    pub access_token: Option<String>,
    pub refresh_token: Option<String>,
    pub email: Option<String>,
    pub tier: Option<String>,
}

impl CliConfig {
    pub fn config_path() -> Result<PathBuf> {
        // Allow override via env var (used in tests and CI)
        if let Ok(dir) = std::env::var("MAHO_CONFIG_DIR") {
            let dir = PathBuf::from(dir);
            fs::create_dir_all(&dir)?;
            return Ok(dir.join("cli.toml"));
        }

        let proj = ProjectDirs::from("co", "maho", "maho-cli")
            .context("cannot determine config directory")?;
        let dir = proj.config_dir();
        fs::create_dir_all(dir)?;
        Ok(dir.join("cli.toml"))
    }

    pub fn load() -> Result<Self> {
        let path = Self::config_path()?;
        if !path.exists() {
            return Ok(Self::default());
        }
        let content = fs::read_to_string(&path)
            .with_context(|| format!("failed to read config at {}", path.display()))?;
        let cfg: Self = toml::from_str(&content)?;
        Ok(cfg)
    }

    pub fn save(&self) -> Result<()> {
        let path = Self::config_path()?;
        write_config_file(&path, self)
    }

    /// Save to a specific path (used in tests).
    pub fn save_to(&self, path: &std::path::Path) -> Result<()> {
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }
        write_config_file(path, self)
    }

    /// Load from a specific path (used in tests).
    pub fn load_from(path: &std::path::Path) -> Result<Self> {
        let content = fs::read_to_string(path)?;
        let cfg: Self = toml::from_str(&content)?;
        Ok(cfg)
    }

    pub fn wipe() -> Result<()> {
        let path = Self::config_path()?;
        if path.exists() {
            fs::remove_file(&path)?;
        }
        Ok(())
    }

    pub fn redacted_display(&self) -> String {
        // Count characters, never bytes: tokens may hold multi-byte text and a
        // byte slice panics on a char boundary. Tokens too short to mask
        // partially are masked whole - revealing a prefix of a short secret
        // exposes most of it.
        let redact = |s: &Option<String>| -> String {
            match s {
                Some(v) if v.chars().count() > 8 => {
                    let total = v.chars().count();
                    let head: String = v.chars().take(4).collect();
                    let tail: String = v.chars().skip(total - 4).collect();
                    format!("{head}...{tail}")
                }
                Some(_) => "(redacted)".to_string(),
                None => "(not set)".to_string(),
            }
        };

        format!(
            "email: {}\ntier: {}\naccess_token: {}\nrefresh_token: {}",
            self.email.as_deref().unwrap_or("(not set)"),
            self.tier.as_deref().unwrap_or("(not set)"),
            redact(&self.access_token),
            redact(&self.refresh_token),
        )
    }
}

#[cfg(unix)]
fn write_config_file(path: &std::path::Path, cfg: &CliConfig) -> Result<()> {
    use std::os::unix::fs::OpenOptionsExt;

    let content = toml::to_string_pretty(cfg)?;
    let mut file = fs::OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .mode(0o600)
        .open(path)
        .with_context(|| format!("failed to write config at {}", path.display()))?;
    file.write_all(content.as_bytes())?;
    Ok(())
}

#[cfg(not(unix))]
fn write_config_file(path: &std::path::Path, cfg: &CliConfig) -> Result<()> {
    let content = toml::to_string_pretty(cfg)?;
    let mut file = fs::OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(path)
        .with_context(|| format!("failed to write config at {}", path.display()))?;
    file.write_all(content.as_bytes())?;
    Ok(())
}
