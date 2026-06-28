use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use tokio::fs;

#[derive(Debug)]
pub enum ReleaseError {
    Io(std::io::Error),
    Parse(serde_json::Error),
    NotFound,
}

impl std::fmt::Display for ReleaseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ReleaseError::Io(e) => write!(f, "io error: {e}"),
            ReleaseError::Parse(e) => write!(f, "json parse error: {e}"),
            ReleaseError::NotFound => write!(f, "release manifest not found"),
        }
    }
}

impl std::error::Error for ReleaseError {}

impl From<std::io::Error> for ReleaseError {
    fn from(e: std::io::Error) -> Self {
        if e.kind() == std::io::ErrorKind::NotFound {
            ReleaseError::NotFound
        } else {
            ReleaseError::Io(e)
        }
    }
}

impl From<serde_json::Error> for ReleaseError {
    fn from(e: serde_json::Error) -> Self {
        ReleaseError::Parse(e)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Platform {
    Macos,
    Windows,
    Linux,
}

impl Platform {
    pub fn as_dir(&self) -> &'static str {
        match self {
            Platform::Macos => "macos",
            Platform::Windows => "windows",
            Platform::Linux => "linux",
        }
    }

    pub fn parse(s: &str) -> Option<Self> {
        match s {
            "macos" | "mac" => Some(Platform::Macos),
            "windows" | "win" => Some(Platform::Windows),
            "linux" => Some(Platform::Linux),
            _ => None,
        }
    }
}

pub fn is_valid_channel(channel: &str) -> bool {
    matches!(channel, "stable" | "beta" | "dev" | "canary")
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ReleaseManifest {
    pub version: String,
    pub channel: String,
    pub binary_filename: String,
    pub binary_sha256: String,
    pub binary_size: u64,
    pub installer_signer_cn: String,
    pub rollout_bucket: u32,
    pub release_date: String,
    pub release_notes_url: Option<String>,
    pub binary_ed25519_signature_base64: Option<String>,
}

pub struct ReleaseStore {
    root: PathBuf,
}

impl ReleaseStore {
    pub fn new(root: impl Into<PathBuf>) -> Self {
        Self { root: root.into() }
    }

    pub fn root(&self) -> &Path {
        &self.root
    }

    pub fn manifest_path(&self, platform: Platform, channel: &str) -> PathBuf {
        self.root
            .join(platform.as_dir())
            .join(channel)
            .join("manifest.json")
    }

    pub fn binary_path(&self, platform: Platform, channel: &str, filename: &str) -> PathBuf {
        self.root
            .join(platform.as_dir())
            .join(channel)
            .join(filename)
    }

    pub async fn load(
        &self,
        platform: Platform,
        channel: &str,
    ) -> Result<ReleaseManifest, ReleaseError> {
        let path = self.manifest_path(platform, channel);
        let bytes = fs::read(&path).await?;
        Ok(serde_json::from_slice(&bytes)?)
    }

    pub async fn save(
        &self,
        platform: Platform,
        channel: &str,
        manifest: &ReleaseManifest,
    ) -> Result<(), ReleaseError> {
        let path = self.manifest_path(platform, channel);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).await?;
        }
        let bytes = serde_json::to_vec_pretty(manifest)?;
        fs::write(&path, bytes).await?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn save_then_load_roundtrip() {
        let dir = tempfile::tempdir().unwrap();
        let store = ReleaseStore::new(dir.path());
        let manifest = ReleaseManifest {
            version: "1.2.3".into(),
            channel: "stable".into(),
            binary_filename: "Maho-1.2.3.dmg".into(),
            binary_sha256: "deadbeef".into(),
            binary_size: 12345,
            installer_signer_cn: "Maho Browser Inc.".into(),
            rollout_bucket: 100,
            release_date: "2026-06-15T00:00:00Z".into(),
            release_notes_url: None,
            binary_ed25519_signature_base64: None,
        };
        store.save(Platform::Macos, "stable", &manifest).await.unwrap();
        let got = store.load(Platform::Macos, "stable").await.unwrap();
        assert_eq!(got.version, "1.2.3");
        assert_eq!(got.binary_filename, "Maho-1.2.3.dmg");
    }

    #[tokio::test]
    async fn missing_returns_not_found() {
        let dir = tempfile::tempdir().unwrap();
        let store = ReleaseStore::new(dir.path());
        let err = store.load(Platform::Macos, "stable").await.unwrap_err();
        assert!(matches!(err, ReleaseError::NotFound));
    }

    #[test]
    fn channel_validation() {
        assert!(is_valid_channel("stable"));
        assert!(is_valid_channel("beta"));
        assert!(is_valid_channel("dev"));
        assert!(is_valid_channel("canary"));
        assert!(!is_valid_channel("nightly"));
        assert!(!is_valid_channel("../etc/passwd"));
        assert!(!is_valid_channel(""));
    }
}
