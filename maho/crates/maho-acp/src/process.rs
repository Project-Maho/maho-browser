use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::sync::Once;

use serde::Deserialize;
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::{Child, Command};

use crate::Error;

#[derive(Debug, Clone, Default, Deserialize)]
pub struct AuthConfig {
    pub provider: String,
    pub base_url: String,
    pub api_key: String,
    pub model: String,
}

impl AuthConfig {
    pub fn is_present(&self) -> bool {
        !self.api_key.is_empty() && !self.provider.is_empty()
    }

    fn effective_provider_id(&self) -> &str {
        if self.provider == "openai-compatible" {
            "openai"
        } else {
            &self.provider
        }
    }

    pub fn to_opencode_auth_content_json(&self) -> Option<String> {
        if !self.is_present() {
            return None;
        }
        let pid = self.effective_provider_id();
        let value = serde_json::json!({
            pid: { "type": "api", "key": self.api_key }
        });
        Some(serde_json::to_string(&value).unwrap())
    }

    pub fn to_opencode_config_content_json(&self) -> Option<String> {
        if self.provider.is_empty() && self.model.is_empty() && self.base_url.is_empty() {
            return None;
        }
        let mut value = serde_json::Map::new();
        let pid = self.effective_provider_id();
        if !self.model.is_empty() {
            value.insert(
                "model".to_string(),
                serde_json::Value::String(format!("{}/{}", pid, self.model)),
            );
        }
        if !self.base_url.is_empty() {
            value.insert(
                "provider".to_string(),
                serde_json::json!({
                    pid: { "options": { "baseURL": self.base_url } }
                }),
            );
        }
        Some(serde_json::to_string(&serde_json::Value::Object(value)).unwrap())
    }
}

/// Returns `~/Library/Application Support/Maho/opencode` as the isolation root
/// for the embedded opencode instance. Uses only `std::env::var("HOME")` — no
/// new crate dependencies.
pub fn maho_opencode_xdg_root() -> io::Result<PathBuf> {
    let home = std::env::var("HOME").map_err(|_| {
        io::Error::new(io::ErrorKind::NotFound, "HOME environment variable is not set")
    })?;
    Ok(PathBuf::from(home)
        .join("Library")
        .join("Application Support")
        .join("Maho")
        .join("opencode"))
}

/// Ensures the XDG isolation directory tree exists and writes the README-AUTH.txt
/// on first call. Idempotent: safe to call on every spawn.
fn ensure_xdg_layout(root: &Path) -> io::Result<()> {
    for subdir in &["config", "data", "state", "cache"] {
        fs::create_dir_all(root.join(subdir))?;
    }

    let readme_path = root.join("README-AUTH.txt");
    if !readme_path.exists() {
        let root_str = root.display();
        let readme = format!(
            "Maho AI uses an isolated opencode configuration in this directory tree:\n\
             - config: {root_str}/config\n\
             - data:   {root_str}/data\n\
             - state:  {root_str}/state\n\
             - cache:  {root_str}/cache\n\
             \n\
             To set up provider auth for Maho's opencode (separate from your personal `opencode auth login`), run this in your terminal:\n\
             \n\
             \x20 XDG_CONFIG_HOME='{root_str}/config' \\\n\
             \x20 XDG_DATA_HOME='{root_str}/data' \\\n\
             \x20 XDG_STATE_HOME='{root_str}/state' \\\n\
             \x20 XDG_CACHE_HOME='{root_str}/cache' \\\n\
             \x20 opencode auth login\n\
             \n\
             After auth completes, restart Maho and try sending a prompt.\n",
        );
        fs::write(&readme_path, readme)?;
    }

    Ok(())
}

/// Spawn `opencode acp` as a child process with stdin/stdout/stderr piped.
///
/// Lifecycle contract (EOF-self-terminate):
/// - On graceful shutdown: caller drops stdin → child sees EOF → exits.
/// - On abnormal parent exit: OS closes pipe fd → child sees EOF → exits.
/// - `kill_on_drop(true)` is the final backstop: if the tokio runtime drops
///   the `Child` without waiting, tokio sends SIGKILL to the child.
///
/// stderr is forwarded line-by-line to tracing with `[acp stderr]` prefix.
pub struct AcpProcess {
    child: Child,
}

/// Watchdog binary name, platform-specific.
#[cfg(windows)]
const WATCHDOG_BIN: &str = "maho-acp-watchdog.exe";
#[cfg(not(windows))]
const WATCHDOG_BIN: &str = "maho-acp-watchdog";

/// Resolve the path to the `maho-acp-watchdog` binary.
///
/// Resolution priority:
/// 1. `MAHO_ACP_WATCHDOG_PATH` env var (highest; used for tests/dev overrides).
/// 2. Sibling of `std::env::current_exe()` (production: watchdog ships next to
///    the Maho binary).
/// 3. `None` — watchdog not found; caller falls back to direct spawn.
pub(crate) fn resolve_watchdog_path() -> Option<PathBuf> {
    if let Ok(val) = std::env::var("MAHO_ACP_WATCHDOG_PATH") {
        return Some(PathBuf::from(val));
    }

    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            let candidate = dir.join(WATCHDOG_BIN);
            if candidate.exists() {
                return Some(candidate);
            }
        }
    }

    None
}

static WATCHDOG_WARN: Once = Once::new();
static XDG_ISOLATION_LOG: Once = Once::new();

impl AcpProcess {
    /// Spawn the ACP child process.
    pub fn spawn(command_path: &str, auth: Option<&AuthConfig>) -> Result<(Self, tokio::process::ChildStdin, tokio::process::ChildStdout), Error> {
        let xdg_root = maho_opencode_xdg_root().map_err(Error::Io)?;
        ensure_xdg_layout(&xdg_root).map_err(Error::Io)?;

        XDG_ISOLATION_LOG.call_once(|| {
            tracing::info!(
                "maho-acp: opencode isolation root = {}; if first prompt fails with auth error, see {}/README-AUTH.txt",
                xdg_root.display(),
                xdg_root.display(),
            );
        });

        let watchdog = resolve_watchdog_path();

        let mut cmd = match watchdog {
            Some(ref watchdog_path) => {
                let mut c = Command::new(watchdog_path);
                c.arg(command_path).arg("acp");
                c
            }
            None => {
                WATCHDOG_WARN.call_once(|| {
                    tracing::warn!(
                        "maho-acp: spawning opencode without watchdog — orphan cleanup on abrupt \
                         parent exit is not guaranteed (set MAHO_ACP_WATCHDOG_PATH or ship the \
                         watchdog binary alongside the parent executable)"
                    );
                });
                let mut c = Command::new(command_path);
                c.arg("acp");
                c
            }
        };

        cmd.env("XDG_CONFIG_HOME", xdg_root.join("config"))
           .env("XDG_DATA_HOME", xdg_root.join("data"))
           .env("XDG_STATE_HOME", xdg_root.join("state"))
           .env("XDG_CACHE_HOME", xdg_root.join("cache"));

        if let Some(ac) = auth {
            if let Some(auth_json) = ac.to_opencode_auth_content_json() {
                cmd.env("OPENCODE_AUTH_CONTENT", auth_json);
            }
            if let Some(config_json) = ac.to_opencode_config_content_json() {
                cmd.env("OPENCODE_CONFIG_CONTENT", config_json);
            }
        }

        let mut child = cmd
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .kill_on_drop(true)
            .spawn()
            .map_err(Error::Io)?;

        let stdin = child.stdin.take().expect("stdin was piped");
        let stdout = child.stdout.take().expect("stdout was piped");
        let stderr = child.stderr.take().expect("stderr was piped");

        // Forward stderr to tracing in a background task
        tokio::spawn(async move {
            let reader = BufReader::new(stderr);
            let mut lines = reader.lines();
            while let Ok(Some(line)) = lines.next_line().await {
                tracing::info!("[acp stderr] {}", line);
            }
        });

        Ok((Self { child }, stdin, stdout))
    }

    /// Graceful shutdown: close stdin (EOF), wait briefly, escalate SIGTERM, then SIGKILL.
    pub async fn shutdown(mut self) -> Result<(), Error> {
        // stdin is already dropped by the caller (transport owns it).
        // Wait up to 1.5s for natural exit.
        match tokio::time::timeout(
            std::time::Duration::from_millis(1500),
            self.child.wait(),
        )
        .await
        {
            Ok(Ok(_)) => return Ok(()),
            Ok(Err(e)) => return Err(Error::Io(e)),
            Err(_) => {} // timed out
        }

        // SIGTERM
        #[cfg(unix)]
        {
            let pid = self.child.id();
            if let Some(pid) = pid {
                let _ = unsafe { libc::kill(pid as i32, libc::SIGTERM) };
            }
        }

        match tokio::time::timeout(
            std::time::Duration::from_millis(1000),
            self.child.wait(),
        )
        .await
        {
            Ok(Ok(_)) => return Ok(()),
            Ok(Err(e)) => return Err(Error::Io(e)),
            Err(_) => {} // timed out
        }

        // SIGKILL
        let _ = self.child.kill().await;
        let _ = self.child.wait().await;
        Ok(())
    }

    /// Get the child's PID.
    pub fn id(&self) -> Option<u32> {
        self.child.id()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn resolve_watchdog_path_uses_env_var() {
        let fake_path = "/tmp/fake-maho-acp-watchdog";
        std::env::set_var("MAHO_ACP_WATCHDOG_PATH", fake_path);
        let result = resolve_watchdog_path();
        std::env::remove_var("MAHO_ACP_WATCHDOG_PATH");

        assert_eq!(result, Some(PathBuf::from(fake_path)));
    }

    #[test]
    fn maho_opencode_xdg_root_returns_expected_path() {
        std::env::set_var("HOME", "/tmp/test-home");
        let result = maho_opencode_xdg_root().unwrap();
        assert_eq!(
            result,
            PathBuf::from("/tmp/test-home/Library/Application Support/Maho/opencode")
        );
    }

    #[test]
    fn auth_config_empty_returns_none() {
        let ac = AuthConfig::default();
        assert!(!ac.is_present());
        assert!(ac.to_opencode_auth_content_json().is_none());
        assert!(ac.to_opencode_config_content_json().is_none());
    }

    #[test]
    fn auth_config_provider_key_only() {
        let ac = AuthConfig {
            provider: "anthropic".to_string(),
            api_key: "sk-test".to_string(),
            ..Default::default()
        };
        assert!(ac.is_present());
        let auth = ac.to_opencode_auth_content_json().unwrap();
        let v: serde_json::Value = serde_json::from_str(&auth).unwrap();
        assert_eq!(v["anthropic"]["type"], "api");
        assert_eq!(v["anthropic"]["key"], "sk-test");
        let config = ac.to_opencode_config_content_json();
        assert!(config.is_some());
    }

    #[test]
    fn auth_config_full() {
        let ac = AuthConfig {
            provider: "openai".to_string(),
            base_url: "https://custom.api/v1".to_string(),
            api_key: "sk-123".to_string(),
            model: "gpt-4o".to_string(),
        };
        let auth = ac.to_opencode_auth_content_json().unwrap();
        let v: serde_json::Value = serde_json::from_str(&auth).unwrap();
        assert_eq!(v["openai"]["key"], "sk-123");

        let config = ac.to_opencode_config_content_json().unwrap();
        let cv: serde_json::Value = serde_json::from_str(&config).unwrap();
        assert_eq!(cv["model"], "openai/gpt-4o");
        assert_eq!(cv["provider"]["openai"]["options"]["baseURL"], "https://custom.api/v1");
    }

    #[test]
    fn auth_config_openai_compatible_maps_to_openai() {
        let ac = AuthConfig {
            provider: "openai-compatible".to_string(),
            base_url: "https://my-llm.example.com/v1".to_string(),
            api_key: "key-abc".to_string(),
            model: "my-model".to_string(),
        };
        let auth = ac.to_opencode_auth_content_json().unwrap();
        let v: serde_json::Value = serde_json::from_str(&auth).unwrap();
        assert!(v.get("openai").is_some());
        assert!(v.get("openai-compatible").is_none());

        let config = ac.to_opencode_config_content_json().unwrap();
        let cv: serde_json::Value = serde_json::from_str(&config).unwrap();
        assert_eq!(cv["model"], "openai/my-model");
    }

    #[test]
    fn auth_config_deserialize_from_json() {
        let json = r#"{"provider":"google","base_url":"","api_key":"gk-1","model":"gemini-pro"}"#;
        let ac: AuthConfig = serde_json::from_str(json).unwrap();
        assert_eq!(ac.provider, "google");
        assert!(ac.is_present());
    }
}
