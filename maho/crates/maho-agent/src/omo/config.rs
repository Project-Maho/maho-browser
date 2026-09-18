// Copyright 2026 Maho Browser. All rights reserved.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::{Path, PathBuf};

pub const BRAND_ENV_VAR: &str = "SENPI_BRAND";
pub const DEFAULT_BRAND_NAME: &str = "mahoagent";
pub const DEFAULT_CONFIG_DIR: &str = ".mahoagent";
pub const DEFAULT_ENV_PREFIX: &str = "MAHOAGENT";
pub const DEFAULT_PERMISSION_PRESET: &str = "full-access";
pub const READ_ONLY_PERMISSION_PRESET: &str = "read-only";

pub const SENPI_RPC_HOST_WATCH_PPID: &str = "SENPI_RPC_HOST_WATCH_PPID";
pub const SENPI_RPC_HOST_WATCH_FD: &str = "SENPI_RPC_HOST_WATCH_FD";
pub const SENPI_RPC_HOST_IDLE_EXIT_MS: &str = "SENPI_RPC_HOST_IDLE_EXIT_MS";
pub const SENPI_RPC_HOST_EMPTY_EXIT_MS: &str = "SENPI_RPC_HOST_EMPTY_EXIT_MS";
pub const SENPI_RPC_HOST_SCRATCH_DIR: &str = "SENPI_RPC_HOST_SCRATCH_DIR";
pub const SENPI_RPC_SOCKET_SECRET_FILE: &str = "SENPI_RPC_SOCKET_SECRET_FILE";
pub const SENPI_RPC_HOST_CLEANUP_PATHS: &str = "SENPI_RPC_HOST_CLEANUP_PATHS";
pub const SENPI_MCP_STARTUP_TIMEOUT_MS: &str = "SENPI_MCP_STARTUP_TIMEOUT_MS";

pub const DEFAULT_RPC_ENTRY_PATH: &str = "dist/rpc-entry.js";

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OmoConfigError {
    EmptyBrandName,
    UnsafeConfigDir(String),
    PathNotAbsolute(PathBuf),
    Serialization(String),
    MissingRpcEntry,
}

impl std::fmt::Display for OmoConfigError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::EmptyBrandName => write!(f, "brand name must not be empty"),
            Self::UnsafeConfigDir(dir) => write!(
                f,
                "config dir '{dir}' is unsafe: must not be '.' or '..' and cannot contain '/' or '\\'"
            ),
            Self::PathNotAbsolute(path) => {
                write!(f, "path must be absolute: {}", path.display())
            }
            Self::Serialization(err) => write!(f, "serialization failed: {err}"),
            Self::MissingRpcEntry => write!(f, "rpc_entry path must be explicitly configured"),
        }
    }
}

impl std::error::Error for OmoConfigError {}

impl From<serde_json::Error> for OmoConfigError {
    fn from(err: serde_json::Error) -> Self {
        Self::Serialization(err.to_string())
    }
}

/// Mirrors the safety constraint from senpi's `isSafeConfigDirName` in `dist/core/brand.js`.
pub fn is_safe_config_dir_name(dir: &str) -> bool {
    let trimmed = dir.trim();
    !trimmed.is_empty()
        && trimmed != "."
        && trimmed != ".."
        && !trimmed.contains('/')
        && !trimmed.contains('\\')
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BrandConfig {
    pub name: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub command: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub display_version: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub config_dir: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub flat_layout: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub env_prefix: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub user_agent: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub originator: Option<String>,
}

impl Default for BrandConfig {
    fn default() -> Self {
        Self {
            name: DEFAULT_BRAND_NAME.to_string(),
            command: None,
            display_version: None,
            config_dir: Some(DEFAULT_CONFIG_DIR.to_string()),
            flat_layout: Some(true),
            env_prefix: Some(DEFAULT_ENV_PREFIX.to_string()),
            user_agent: None,
            originator: None,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct OpenSessionParams {
    #[serde(rename = "type")]
    pub type_: String,
    pub permission_preset: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub session_path: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub cwd: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub provider: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub model_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub thinking_level: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OmoLaunchConfig {
    brand: BrandConfig,
    agent_dir: PathBuf,
    session_dir: PathBuf,
    runtime_binary: PathBuf,
    rpc_entry: PathBuf,
    socket_path: PathBuf,
    scratch_dir: Option<PathBuf>,
    socket_secret_file: Option<PathBuf>,
    watch_ppid: Option<u32>,
    watch_fd: Option<i32>,
    idle_exit_ms: Option<u64>,
    empty_exit_ms: Option<u64>,
    cleanup_paths: Vec<PathBuf>,
    mcp_startup_timeout_ms: Option<u64>,
    permission_preset: String,
    env_vars: HashMap<String, String>,
    provider: Option<String>,
    model_id: Option<String>,
    thinking_level: Option<String>,
    custom_provider: Option<CustomProvider>,
}

/// An OpenAI-compatible endpoint Maho declares for omo in `models.json`.
///
/// Maho's own provider settings (custom proxy base URL, key, model) cannot reach omo
/// through `login_api_key`, which carries only a provider name and key.
#[derive(Clone, PartialEq, Eq)]
pub struct CustomProvider {
    pub name: String,
    pub base_url: String,
    pub api_key: String,
    pub models: Vec<String>,
}

impl std::fmt::Debug for CustomProvider {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("CustomProvider")
            .field("name", &self.name)
            .field("base_url", &self.base_url)
            .field("api_key", &"[REDACTED]")
            .field("models", &self.models)
            .finish()
    }
}

/// Locations of the omo runtime pieces Maho ships beside its executable.
///
/// Mirrors the `MahoMailHelperLauncher` precedent: the build stages each artifact next
/// to the browser binary and the runtime resolves it from `<DIR_EXE>`, so nothing here
/// falls back to a developer's own install.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BundledRuntime {
    pub runtime_binary: PathBuf,
    pub rpc_entry: PathBuf,
    pub browser_mcp_binary: PathBuf,
}

impl BundledRuntime {
    pub fn from_executable_dir(dir: impl AsRef<Path>) -> Self {
        let dir = dir.as_ref();
        let helpers_mcp = dir
            .parent()
            .map(|p| p.join("Helpers").join("maho-browser-mcp"))
            .filter(|p| p.exists());
        let browser_mcp_binary = helpers_mcp.unwrap_or_else(|| dir.join("maho-browser-mcp"));
        let resources_rpc = dir
            .parent()
            .map(|p| p.join("Resources").join("omo").join("dist").join("rpc-entry.js"))
            .filter(|p| p.exists());
        let rpc_entry = resources_rpc.unwrap_or_else(|| dir.join("omo").join("dist").join("rpc-entry.js"));

        let macos_bun = dir
            .parent()
            .map(|p| p.join("MacOS").join("bun"))
            .filter(|p| p.exists());
        let helpers_bun = dir
            .parent()
            .map(|p| p.join("Helpers").join("bun"))
            .filter(|p| p.exists());
        let runtime_binary = if dir.join("bun").exists() {
            dir.join("bun")
        } else if let Some(p) = macos_bun {
            p
        } else if let Some(p) = helpers_bun {
            p
        } else {
            dir.join("bun")
        };

        Self {
            runtime_binary,
            rpc_entry,
            browser_mcp_binary,
        }
    }

    /// Every artifact that must exist before the agent can start.
    pub fn missing(&self) -> Vec<&Path> {
        [
            self.runtime_binary.as_path(),
            self.rpc_entry.as_path(),
            self.browser_mcp_binary.as_path(),
        ]
        .into_iter()
        .filter(|path| !path.exists())
        .collect()
    }
}

impl CustomProvider {
    pub fn models_json(&self) -> serde_json::Value {
        serde_json::json!({
            "providers": {
                &self.name: {
                    "baseUrl": &self.base_url,
                    "api": "openai-completions",
                    "apiKey": &self.api_key,
                    "models": self
                        .models
                        .iter()
                        .map(|id| serde_json::json!({ "id": id }))
                        .collect::<Vec<_>>(),
                }
            }
        })
    }
}

impl OmoLaunchConfig {
    pub fn builder(agent_dir: impl Into<PathBuf>) -> OmoLaunchConfigBuilder {
        OmoLaunchConfigBuilder::new(agent_dir)
    }

    pub fn brand(&self) -> &BrandConfig {
        &self.brand
    }

    pub fn brand_json(&self) -> Result<String, OmoConfigError> {
        Ok(serde_json::to_string(&self.brand)?)
    }

    pub fn agent_dir(&self) -> &Path {
        &self.agent_dir
    }

    pub fn session_dir(&self) -> &Path {
        &self.session_dir
    }

    pub fn runtime_binary(&self) -> &Path {
        &self.runtime_binary
    }

    pub fn rpc_entry(&self) -> &Path {
        &self.rpc_entry
    }

    pub fn socket_path(&self) -> &Path {
        &self.socket_path
    }

    pub fn permission_preset(&self) -> &str {
        &self.permission_preset
    }

    pub fn env_prefix(&self) -> &str {
        self.brand.env_prefix.as_deref().unwrap_or(&self.brand.name)
    }

    pub fn agent_dir_env_var_name(&self) -> String {
        format!(
            "{}_CODING_AGENT_DIR",
            self.env_prefix().to_ascii_uppercase()
        )
    }

    pub fn session_dir_env_var_name(&self) -> String {
        format!(
            "{}_CODING_AGENT_SESSION_DIR",
            self.env_prefix().to_ascii_uppercase()
        )
    }

    pub fn env_vars(&self) -> &HashMap<String, String> {
        &self.env_vars
    }

    pub fn env_map(&self) -> &HashMap<String, String> {
        &self.env_vars
    }

    pub fn open_session_params(&self) -> OpenSessionParams {
        OpenSessionParams {
            type_: "open_session".to_string(),
            permission_preset: self.permission_preset.clone(),
            session_path: None,
            cwd: None,
            provider: self.provider.clone(),
            model_id: self.model_id.clone(),
            thinking_level: self.thinking_level.clone(),
        }
    }

    pub fn custom_provider(&self) -> Option<&CustomProvider> {
        self.custom_provider.as_ref()
    }
}

fn is_safe_environment_entry(key: &str, val: &str) -> bool {
    let lower_key = key.to_ascii_lowercase();
    let lower_val = val.to_ascii_lowercase();

    if lower_val.contains(".senpi") || lower_val.contains(".omo") {
        return false;
    }

    if lower_key.contains("senpi")
        || lower_key.contains("omo")
        || lower_key.contains("coding_agent")
    {
        return false;
    }

    true
}

#[derive(Debug, Clone)]
pub struct OmoLaunchConfigBuilder {
    brand: BrandConfig,
    agent_dir: PathBuf,
    session_dir: Option<PathBuf>,
    runtime_binary: Option<PathBuf>,
    rpc_entry: Option<PathBuf>,
    socket_path: Option<PathBuf>,
    scratch_dir: Option<PathBuf>,
    socket_secret_file: Option<PathBuf>,
    watch_ppid: Option<u32>,
    watch_fd: Option<i32>,
    idle_exit_ms: Option<u64>,
    empty_exit_ms: Option<u64>,
    cleanup_paths: Vec<PathBuf>,
    mcp_startup_timeout_ms: Option<u64>,
    permission_preset: String,
    custom_env: HashMap<String, String>,
    provider: Option<String>,
    model_id: Option<String>,
    thinking_level: Option<String>,
    custom_provider: Option<CustomProvider>,
}

impl OmoLaunchConfigBuilder {
    pub fn new(agent_dir: impl Into<PathBuf>) -> Self {
        Self {
            brand: BrandConfig::default(),
            agent_dir: agent_dir.into(),
            session_dir: None,
            runtime_binary: None,
            rpc_entry: None,
            socket_path: None,
            scratch_dir: None,
            socket_secret_file: None,
            watch_ppid: None,
            watch_fd: None,
            idle_exit_ms: None,
            empty_exit_ms: None,
            cleanup_paths: Vec::new(),
            mcp_startup_timeout_ms: Some(15_000),
            permission_preset: DEFAULT_PERMISSION_PRESET.to_string(),
            custom_env: HashMap::new(),
            provider: None,
            model_id: None,
            thinking_level: None,
            custom_provider: None,
        }
    }

    pub fn provider(mut self, provider: impl Into<String>) -> Self {
        self.provider = Some(provider.into());
        self
    }

    pub fn model_id(mut self, model_id: impl Into<String>) -> Self {
        self.model_id = Some(model_id.into());
        self
    }

    pub fn thinking_level(mut self, level: impl Into<String>) -> Self {
        self.thinking_level = Some(level.into());
        self
    }

    pub fn custom_provider(mut self, provider: CustomProvider) -> Self {
        self.provider = Some(provider.name.clone());
        self.custom_provider = Some(provider);
        self
    }

    pub fn brand_name(mut self, name: impl Into<String>) -> Self {
        self.brand.name = name.into();
        self
    }

    pub fn config_dir(mut self, dir: impl Into<String>) -> Self {
        self.brand.config_dir = Some(dir.into());
        self
    }

    pub fn env_prefix(mut self, prefix: impl Into<String>) -> Self {
        self.brand.env_prefix = Some(prefix.into());
        self
    }

    pub fn flat_layout(mut self, flat: bool) -> Self {
        self.brand.flat_layout = Some(flat);
        self
    }

    pub fn agent_dir(mut self, path: impl Into<PathBuf>) -> Self {
        self.agent_dir = path.into();
        self
    }

    pub fn session_dir(mut self, path: impl Into<PathBuf>) -> Self {
        self.session_dir = Some(path.into());
        self
    }

    pub fn runtime_binary(mut self, path: impl Into<PathBuf>) -> Self {
        self.runtime_binary = Some(path.into());
        self
    }

    pub fn rpc_entry(mut self, path: impl Into<PathBuf>) -> Self {
        self.rpc_entry = Some(path.into());
        self
    }

    pub fn socket_path(mut self, path: impl Into<PathBuf>) -> Self {
        self.socket_path = Some(path.into());
        self
    }

    pub fn scratch_dir(mut self, path: impl Into<PathBuf>) -> Self {
        self.scratch_dir = Some(path.into());
        self
    }

    pub fn socket_secret_file(mut self, path: impl Into<PathBuf>) -> Self {
        self.socket_secret_file = Some(path.into());
        self
    }

    pub fn watch_ppid(mut self, ppid: u32) -> Self {
        self.watch_ppid = Some(ppid);
        self
    }

    pub fn watch_fd(mut self, fd: i32) -> Self {
        self.watch_fd = Some(fd);
        self
    }

    pub fn idle_exit_ms(mut self, ms: u64) -> Self {
        self.idle_exit_ms = Some(ms);
        self
    }

    pub fn empty_exit_ms(mut self, ms: u64) -> Self {
        self.empty_exit_ms = Some(ms);
        self
    }

    pub fn cleanup_paths(mut self, paths: Vec<PathBuf>) -> Self {
        self.cleanup_paths = paths;
        self
    }

    pub fn add_cleanup_path(mut self, path: impl Into<PathBuf>) -> Self {
        self.cleanup_paths.push(path.into());
        self
    }

    pub fn mcp_startup_timeout_ms(mut self, ms: u64) -> Self {
        self.mcp_startup_timeout_ms = Some(ms);
        self
    }

    pub fn permission_preset(mut self, preset: impl Into<String>) -> Self {
        self.permission_preset = preset.into();
        self
    }

    pub fn env_var(mut self, key: impl Into<String>, val: impl Into<String>) -> Self {
        self.custom_env.insert(key.into(), val.into());
        self
    }

    pub fn inherit_filtered_env(mut self, env: impl IntoIterator<Item = (String, String)>) -> Self {
        for (k, v) in env {
            if is_safe_environment_entry(&k, &v) {
                self.custom_env.insert(k, v);
            }
        }
        self
    }

    pub fn build(self) -> Result<OmoLaunchConfig, OmoConfigError> {
        if self.brand.name.trim().is_empty() {
            return Err(OmoConfigError::EmptyBrandName);
        }

        if let Some(ref dir) = self.brand.config_dir {
            if !is_safe_config_dir_name(dir) {
                return Err(OmoConfigError::UnsafeConfigDir(dir.clone()));
            }
        }

        if !self.agent_dir.is_absolute() {
            return Err(OmoConfigError::PathNotAbsolute(self.agent_dir));
        }

        let session_dir = match self.session_dir {
            Some(sd) => {
                if !sd.is_absolute() {
                    return Err(OmoConfigError::PathNotAbsolute(sd));
                }
                sd
            }
            None => self.agent_dir.join("sessions"),
        };

        if let Some(ref sc) = self.scratch_dir {
            if !sc.is_absolute() {
                return Err(OmoConfigError::PathNotAbsolute(sc.clone()));
            }
        }

        if let Some(ref ss) = self.socket_secret_file {
            if !ss.is_absolute() {
                return Err(OmoConfigError::PathNotAbsolute(ss.clone()));
            }
        }

        let socket_path = match self.socket_path {
            Some(sp) => {
                if !sp.is_absolute() {
                    return Err(OmoConfigError::PathNotAbsolute(sp));
                }
                sp
            }
            None => self.agent_dir.join("rpc").join("rpc.sock"),
        };

        let runtime_binary = self.runtime_binary.unwrap_or_else(|| PathBuf::from("bun"));

        let rpc_entry = match self.rpc_entry {
            Some(p) => p,
            None => {
                #[cfg(debug_assertions)]
                {
                    PathBuf::from(DEFAULT_RPC_ENTRY_PATH)
                }
                #[cfg(not(debug_assertions))]
                {
                    return Err(OmoConfigError::MissingRpcEntry);
                }
            }
        };

        let env_prefix = self
            .brand
            .env_prefix
            .as_deref()
            .unwrap_or(&self.brand.name)
            .to_ascii_uppercase();

        let agent_dir_key = format!("{env_prefix}_CODING_AGENT_DIR");
        let session_dir_key = format!("{env_prefix}_CODING_AGENT_SESSION_DIR");

        let brand_json = serde_json::to_string(&self.brand)?;

        let mut env_vars = HashMap::new();
        for (k, v) in &self.custom_env {
            if is_safe_environment_entry(k, v) {
                env_vars.insert(k.clone(), v.clone());
            }
        }

        env_vars.insert(BRAND_ENV_VAR.to_string(), brand_json);
        env_vars.insert(agent_dir_key, self.agent_dir.to_string_lossy().to_string());
        env_vars.insert(session_dir_key, session_dir.to_string_lossy().to_string());

        let dir_str = self.agent_dir.to_string_lossy().to_string();
        env_vars.insert("SENPI_CODING_AGENT_DIR".to_string(), dir_str.clone());
        env_vars.insert("OMO_CODING_AGENT_DIR".to_string(), dir_str);
        // omo/senpi builtin brand prefix fallback: rpc-entry scrubs SENPI_BRAND before
        // the engine reads it, falling back to the OMO brand prefix. We emit OMO_CODING_AGENT_DIR
        // and SENPI_CODING_AGENT_DIR pointing to our isolated agent_dir so omo's globalPath
        // mcp.json and settings.json resolve to Maho's isolated directory rather than ~/.omo.
        env_vars.insert("OMO_CODING_AGENT_DIR".to_string(), self.agent_dir.to_string_lossy().to_string());
        env_vars.insert("SENPI_CODING_AGENT_DIR".to_string(), self.agent_dir.to_string_lossy().to_string());
        env_vars.insert("OMO_CODING_AGENT_SESSION_DIR".to_string(), session_dir.to_string_lossy().to_string());
        env_vars.insert("SENPI_CODING_AGENT_SESSION_DIR".to_string(), session_dir.to_string_lossy().to_string());

        if let Some(ref scratch) = self.scratch_dir {
            env_vars.insert(
                SENPI_RPC_HOST_SCRATCH_DIR.to_string(),
                scratch.to_string_lossy().to_string(),
            );
        }
        if let Some(ref secret) = self.socket_secret_file {
            env_vars.insert(
                SENPI_RPC_SOCKET_SECRET_FILE.to_string(),
                secret.to_string_lossy().to_string(),
            );
        }
        if let Some(ppid) = self.watch_ppid {
            env_vars.insert(SENPI_RPC_HOST_WATCH_PPID.to_string(), ppid.to_string());
        }
        if let Some(fd) = self.watch_fd {
            env_vars.insert(SENPI_RPC_HOST_WATCH_FD.to_string(), fd.to_string());
        }
        if let Some(idle) = self.idle_exit_ms {
            env_vars.insert(SENPI_RPC_HOST_IDLE_EXIT_MS.to_string(), idle.to_string());
        }
        if let Some(empty) = self.empty_exit_ms {
            env_vars.insert(SENPI_RPC_HOST_EMPTY_EXIT_MS.to_string(), empty.to_string());
        }
        if !self.cleanup_paths.is_empty() {
            let joined = std::env::join_paths(&self.cleanup_paths)
                .map_err(|e| OmoConfigError::Serialization(e.to_string()))?;
            env_vars.insert(
                SENPI_RPC_HOST_CLEANUP_PATHS.to_string(),
                joined.to_string_lossy().to_string(),
            );
        }
        if let Some(timeout) = self.mcp_startup_timeout_ms {
            env_vars.insert(
                SENPI_MCP_STARTUP_TIMEOUT_MS.to_string(),
                timeout.to_string(),
            );
        }

        Ok(OmoLaunchConfig {
            brand: self.brand,
            agent_dir: self.agent_dir,
            session_dir,
            runtime_binary,
            rpc_entry,
            socket_path,
            scratch_dir: self.scratch_dir,
            socket_secret_file: self.socket_secret_file,
            watch_ppid: self.watch_ppid,
            watch_fd: self.watch_fd,
            idle_exit_ms: self.idle_exit_ms,
            empty_exit_ms: self.empty_exit_ms,
            cleanup_paths: self.cleanup_paths,
            mcp_startup_timeout_ms: self.mcp_startup_timeout_ms,
            permission_preset: self.permission_preset,
            env_vars,
            provider: self.provider,
            model_id: self.model_id,
            thinking_level: self.thinking_level,
            custom_provider: self.custom_provider,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_senpi_brand_emitted_is_valid_json_with_non_empty_name() {
        let config = OmoLaunchConfig::builder("/tmp/mahoagent-test")
            .build()
            .unwrap();
        let env = config.env_vars();
        let brand_str = env.get(BRAND_ENV_VAR).expect("SENPI_BRAND must be present");
        let parsed: serde_json::Value = serde_json::from_str(brand_str).expect("Valid JSON");
        let name = parsed
            .get("name")
            .and_then(|v| v.as_str())
            .expect("name field must be a string");
        assert!(!name.trim().is_empty());
        assert_eq!(name, "mahoagent");
    }

    #[test]
    fn test_config_dir_passes_safety_rule() {
        let config = OmoLaunchConfig::builder("/tmp/mahoagent-test")
            .build()
            .unwrap();
        let env = config.env_vars();
        let brand_str = env.get(BRAND_ENV_VAR).unwrap();
        let parsed: serde_json::Value = serde_json::from_str(brand_str).unwrap();
        let config_dir = parsed
            .get("configDir")
            .and_then(|v| v.as_str())
            .expect("configDir must be a string");

        assert!(is_safe_config_dir_name(config_dir));
        assert_ne!(config_dir, ".");
        assert_ne!(config_dir, "..");
        assert!(!config_dir.contains('/'));
        assert!(!config_dir.contains('\\'));

        assert!(!is_safe_config_dir_name("."));
        assert!(!is_safe_config_dir_name(".."));
        assert!(!is_safe_config_dir_name("/etc/omo"));
        assert!(!is_safe_config_dir_name("nested/dir"));
        assert!(!is_safe_config_dir_name("win\\path"));
        assert!(!is_safe_config_dir_name(""));

        let unsafe_res = OmoLaunchConfig::builder("/tmp/mahoagent-test")
            .config_dir("../unsafe")
            .build();
        assert!(matches!(
            unsafe_res,
            Err(OmoConfigError::UnsafeConfigDir(_))
        ));
    }

    #[test]
    fn bundled_runtime_prefers_resources_omo_when_present() {
        let temp = tempfile::tempdir().expect("tempdir");
        let macos = temp.path().join("Contents").join("MacOS");
        let resources = temp.path().join("Contents").join("Resources");
        let rpc_dir = resources.join("omo").join("dist");
        std::fs::create_dir_all(&macos).unwrap();
        std::fs::create_dir_all(&rpc_dir).unwrap();
        let rpc_file = rpc_dir.join("rpc-entry.js");
        std::fs::write(&rpc_file, b"// rpc\n").unwrap();

        let rt = BundledRuntime::from_executable_dir(&macos);
        assert_eq!(rt.rpc_entry, rpc_file);
    }

    #[test]
    fn bundled_runtime_prefers_helpers_mcp_when_present() {
        let temp = tempfile::tempdir().expect("tempdir");
        let macos = temp.path().join("Contents").join("MacOS");
        let helpers = temp.path().join("Contents").join("Helpers");
        std::fs::create_dir_all(&macos).unwrap();
        std::fs::create_dir_all(&helpers).unwrap();
        let helpers_mcp = helpers.join("maho-browser-mcp");
        std::fs::write(&helpers_mcp, b"#!/bin/sh\n").unwrap();

        let rt = BundledRuntime::from_executable_dir(&macos);
        assert_eq!(rt.browser_mcp_binary, helpers_mcp);
    }

    #[test]
    fn bundled_runtime_resolves_beside_the_executable_not_a_user_install() {
        let rt = BundledRuntime::from_executable_dir("/mock/nonexistent/Maho.app/Contents/MacOS");

        assert_eq!(
            rt.runtime_binary,
            PathBuf::from("/mock/nonexistent/Maho.app/Contents/MacOS/bun")
        );
        assert_eq!(
            rt.browser_mcp_binary,
            PathBuf::from("/mock/nonexistent/Maho.app/Contents/MacOS/maho-browser-mcp")
        );
        assert_eq!(
            rt.rpc_entry,
            PathBuf::from("/mock/nonexistent/Maho.app/Contents/MacOS/omo/dist/rpc-entry.js")
        );

        for path in [&rt.runtime_binary, &rt.rpc_entry, &rt.browser_mcp_binary] {
            let text = path.to_string_lossy();
            assert!(!text.contains(".omo"), "must not point at a user omo install: {text}");
            assert!(!text.contains(".senpi"), "must not point at a user senpi install: {text}");
            assert!(!text.contains("node_modules"), "must not point at a global install: {text}");
        }
    }

    #[test]
    fn bundled_runtime_reports_every_missing_artifact() {
        let rt = BundledRuntime::from_executable_dir("/nonexistent/Maho.app/Contents/MacOS");
        assert_eq!(rt.missing().len(), 3, "all three artifacts are absent");
    }

    #[test]
    fn custom_provider_emits_the_documented_models_json_shape() {
        let provider = CustomProvider {
            name: "maho-proxy".to_string(),
            base_url: "http://127.0.0.1:18801/v1".to_string(),
            api_key: "secret".to_string(),
            models: vec!["claude-opus-4-6-thinking".to_string()],
        };

        let json = provider.models_json();
        let entry = &json["providers"]["maho-proxy"];
        assert_eq!(entry["baseUrl"], "http://127.0.0.1:18801/v1");
        assert_eq!(entry["api"], "openai-completions");
        assert_eq!(entry["apiKey"], "secret");
        assert_eq!(entry["models"][0]["id"], "claude-opus-4-6-thinking");
    }

    #[test]
    fn open_session_params_serialize_accurately_to_rpc_command() {
        let config = OmoLaunchConfig::builder("/tmp/mahoagent-model-test")
            .provider("maho-proxy")
            .model_id("claude-opus-4-6-thinking")
            .thinking_level("high")
            .permission_preset("full")
            .build()
            .expect("config builds");

        let params = config.open_session_params();
        let cmd = crate::omo::proto::RpcCommand::OpenSession {
            id: Some("req-1".to_string()),
            session_path: None,
            cwd: Some("/workspace".to_string()),
            provider: params.provider,
            model_id: params.model_id,
            thinking_level: params.thinking_level.and_then(|s| s.parse().ok()),
            permission_preset: Some(params.permission_preset),
        };
        let value = serde_json::to_value(&cmd).expect("serializes to json");
        assert_eq!(value["type"], "open_session");
        assert_eq!(value["provider"], "maho-proxy");
        assert_eq!(value["modelId"], "claude-opus-4-6-thinking");
        assert_eq!(value["thinkingLevel"], "high");
        assert_eq!(value["permissionPreset"], "full");
        assert_eq!(value["cwd"], "/workspace");
    }

    #[test]
    fn test_built_env_does_not_point_at_user_senpi_or_omo_paths() {
        let parent_env = vec![
            (
                "SENPI_CODING_AGENT_DIR".to_string(),
                "/Users/user/.senpi/agent".to_string(),
            ),
            (
                "PI_CODING_AGENT_DIR".to_string(),
                "/Users/user/.omo/agent".to_string(),
            ),
            (
                "CUSTOM_LEAKY_PATH".to_string(),
                "/home/user/.senpi/settings.json".to_string(),
            ),
            (
                "ANOTHER_LEAK".to_string(),
                "/home/user/.omo/settings.json".to_string(),
            ),
            ("SAFE_CUSTOM_VAR".to_string(), "safe_value".to_string()),
        ];

        let config = OmoLaunchConfig::builder("/tmp/mahoagent-test")
            .inherit_filtered_env(parent_env)
            .build()
            .unwrap();
        let env = config.env_vars();

        for (k, v) in env {
            assert!(
                !v.contains(".senpi"),
                "Key '{k}' contains '.senpi' in value: '{v}'"
            );
            assert!(
                !v.contains(".omo"),
                "Key '{k}' contains '.omo' in value: '{v}'"
            );
        }

        assert_eq!(
            env.get("SAFE_CUSTOM_VAR").map(String::as_str),
            Some("safe_value")
        );
        assert_eq!(
            env.get("SENPI_CODING_AGENT_DIR").map(String::as_str),
            Some("/tmp/mahoagent-test")
        );
        assert_eq!(
            env.get("OMO_CODING_AGENT_DIR").map(String::as_str),
            Some("/tmp/mahoagent-test")
        );
        assert!(!env.contains_key("PI_CODING_AGENT_DIR"));
        assert!(!env.contains_key("CUSTOM_LEAKY_PATH"));
        assert!(!env.contains_key("ANOTHER_LEAK"));
    }

    #[test]
    fn test_open_session_params_carry_permission_preset_full_access() {
        let config = OmoLaunchConfig::builder("/tmp/mahoagent-test")
            .build()
            .unwrap();
        let params = config.open_session_params();
        assert_eq!(params.permission_preset, "full-access");
        assert_eq!(params.type_, "open_session");

        let json = serde_json::to_value(&params).unwrap();
        assert_eq!(
            json.get("permissionPreset").and_then(|v| v.as_str()),
            Some("full-access")
        );
        assert_eq!(
            json.get("type").and_then(|v| v.as_str()),
            Some("open_session")
        );
    }

    #[test]
    fn test_coding_agent_dir_derived_from_env_prefix_and_is_absolute() {
        let config = OmoLaunchConfig::builder("/tmp/mahoagent-test")
            .build()
            .unwrap();
        let env = config.env_vars();

        let agent_key = config.agent_dir_env_var_name();
        assert_eq!(agent_key, "MAHOAGENT_CODING_AGENT_DIR");
        let agent_val = env
            .get(&agent_key)
            .expect("MAHOAGENT_CODING_AGENT_DIR must be emitted");
        assert!(
            Path::new(agent_val).is_absolute(),
            "Agent dir must be an absolute path: {agent_val}"
        );

        let session_key = config.session_dir_env_var_name();
        assert_eq!(session_key, "MAHOAGENT_CODING_AGENT_SESSION_DIR");
        let session_val = env
            .get(&session_key)
            .expect("MAHOAGENT_CODING_AGENT_SESSION_DIR must be emitted");
        assert!(
            Path::new(session_val).is_absolute(),
            "Session dir must be an absolute path: {session_val}"
        );

        let custom_config = OmoLaunchConfig::builder("/tmp/custom-test")
            .brand_name("customagent")
            .env_prefix("CUSTOMAGENT")
            .build()
            .unwrap();
        let custom_env = custom_config.env_vars();
        assert!(custom_env.contains_key("CUSTOMAGENT_CODING_AGENT_DIR"));
        assert!(custom_env.contains_key("CUSTOMAGENT_CODING_AGENT_SESSION_DIR"));
        assert!(!custom_env.contains_key("MAHOAGENT_CODING_AGENT_DIR"));
        assert_eq!(
            custom_config.agent_dir_env_var_name(),
            "CUSTOMAGENT_CODING_AGENT_DIR"
        );
    }

    #[test]
    fn test_validation_rejects_empty_brand_and_relative_paths() {
        let empty_brand = OmoLaunchConfig::builder("/tmp/test")
            .brand_name("   ")
            .build();
        assert_eq!(empty_brand, Err(OmoConfigError::EmptyBrandName));

        let rel_agent = OmoLaunchConfig::builder("relative/path").build();
        assert_eq!(
            rel_agent,
            Err(OmoConfigError::PathNotAbsolute(PathBuf::from(
                "relative/path"
            )))
        );

        let rel_session = OmoLaunchConfig::builder("/tmp/test")
            .session_dir("relative/sessions")
            .build();
        assert_eq!(
            rel_session,
            Err(OmoConfigError::PathNotAbsolute(PathBuf::from(
                "relative/sessions"
            )))
        );
    }

    #[test]
    fn test_lifecycle_knobs_emitted() {
        let config = OmoLaunchConfig::builder("/tmp/mahoagent-test")
            .watch_ppid(12345)
            .watch_fd(7)
            .idle_exit_ms(60000)
            .empty_exit_ms(30000)
            .scratch_dir("/tmp/maho-scratch")
            .socket_secret_file("/tmp/maho-secret")
            .add_cleanup_path("/tmp/clean1")
            .add_cleanup_path("/tmp/clean2")
            .mcp_startup_timeout_ms(15000)
            .build()
            .unwrap();

        let env = config.env_vars();
        assert_eq!(
            env.get(SENPI_RPC_HOST_WATCH_PPID).map(String::as_str),
            Some("12345")
        );
        assert_eq!(
            env.get(SENPI_RPC_HOST_WATCH_FD).map(String::as_str),
            Some("7")
        );
        assert_eq!(
            env.get(SENPI_RPC_HOST_IDLE_EXIT_MS).map(String::as_str),
            Some("60000")
        );
        assert_eq!(
            env.get(SENPI_RPC_HOST_EMPTY_EXIT_MS).map(String::as_str),
            Some("30000")
        );
        assert_eq!(
            env.get(SENPI_RPC_HOST_SCRATCH_DIR).map(String::as_str),
            Some("/tmp/maho-scratch")
        );
        assert_eq!(
            env.get(SENPI_RPC_SOCKET_SECRET_FILE).map(String::as_str),
            Some("/tmp/maho-secret")
        );
        assert!(env.contains_key(SENPI_RPC_HOST_CLEANUP_PATHS));
        assert_eq!(
            env.get(SENPI_MCP_STARTUP_TIMEOUT_MS).map(String::as_str),
            Some("15000")
        );
    }

    #[test]
    fn test_process_integration_accessors() {
        let config = OmoLaunchConfig::builder("/tmp/mahoagent-test")
            .runtime_binary("/usr/local/bin/bun")
            .rpc_entry("/opt/senpi/rpc-entry.js")
            .socket_path("/tmp/test.sock")
            .build()
            .unwrap();

        assert_eq!(config.runtime_binary(), Path::new("/usr/local/bin/bun"));
        assert_eq!(config.rpc_entry(), Path::new("/opt/senpi/rpc-entry.js"));
        assert_eq!(config.socket_path(), Path::new("/tmp/test.sock"));
        assert!(config.env_vars().contains_key(BRAND_ENV_VAR));
    }
}
