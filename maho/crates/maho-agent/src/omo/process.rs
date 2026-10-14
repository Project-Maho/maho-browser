#![cfg(unix)]

use std::path::{Path, PathBuf};
use std::process::Stdio;

use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::{Child, Command};
use tokio::sync::oneshot;

use crate::omo::config::OmoLaunchConfig;
use crate::AgentError;

/// Emitted on stderr once the socket is bound and accepting connections.
const READY_MARKER: &str = "rpc listening on";

/// A runtime that neither reports readiness nor exits must not hang the caller.
const READY_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(30);

/// Bound on reaping a runtime that closed stderr without reporting readiness.
const REAP_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(5);

/// Parent-environment variables that configure an ambient omo/senpi/pi install and must
/// not leak into Maho's isolated runtime. Matched as exact key prefixes so unrelated
/// variables that merely contain "omo" (e.g. `TOMORROW`) survive.
const AMBIENT_AGENT_ENV_PREFIXES: [&str; 3] = ["OMO_", "SENPI_", "PI_CODING_AGENT"];

fn is_ambient_agent_env(key: &str) -> bool {
    let upper = key.to_ascii_uppercase();
    AMBIENT_AGENT_ENV_PREFIXES
        .iter()
        .any(|prefix| upper.starts_with(prefix))
}

#[derive(Debug, thiserror::Error)]
pub enum ProcessError {
    #[error("failed to spawn omo runtime: {0}")]
    Spawn(String),
    #[error("omo runtime exited before it became ready (status {0})")]
    ExitedEarly(String),
    #[error("omo runtime did not report readiness")]
    NoReadySignal,
    #[error("omo runtime did not report readiness within {0:?}")]
    ReadyTimeout(std::time::Duration),
    #[error("io error: {0}")]
    Io(#[from] std::io::Error),
}

impl From<ProcessError> for AgentError {
    fn from(value: ProcessError) -> Self {
        AgentError::ExecutionError(value.to_string())
    }
}

// `--multi-session` is deliberately absent: the arg parser sets it itself when it
// accepts `--listen`, so passing it again is redundant.
pub fn rpc_argv(entry: &Path, socket: &Path, system_prompt: Option<&Path>) -> Vec<String> {
    let mut args = vec![
        entry.display().to_string(),
        "--listen".to_string(),
        format!("unix://{}", socket.display()),
        "--no-builtin-tools".to_string(),
    ];
    if let Some(sp) = system_prompt {
        args.push("--system-prompt".to_string());
        args.push(sp.display().to_string());
    }
    args
}

pub struct OmoProcess {
    child: Child,
    socket_path: PathBuf,
}

impl OmoProcess {
    pub fn socket_path(&self) -> &Path {
        &self.socket_path
    }

    pub fn id(&self) -> Option<u32> {
        self.child.id()
    }

    // Readiness arrives on stderr. It cannot be detected by polling the socket path:
    // the bound path is a unix socket, which file-existence checks do not observe.
    pub async fn spawn(
        runtime_binary: &Path,
        rpc_entry: &Path,
        socket_path: &Path,
        config: &OmoLaunchConfig,
    ) -> Result<Self, ProcessError> {
        let system_prompt_file = config.agent_dir().join("system_prompt.txt");
        let sp_arg = if system_prompt_file.exists() {
            Some(system_prompt_file.as_path())
        } else {
            None
        };
        let argv = rpc_argv(rpc_entry, socket_path, sp_arg);
        let mut command = Command::new(runtime_binary);
        command.args(&argv);

        // Strip ambient omo/senpi environment variables from parent process
        for (k, _) in std::env::vars_os() {
            if let Some(key) = k.to_str() {
                if is_ambient_agent_env(key) {
                    command.env_remove(key);
                }
            }
        }

        command.envs(config.env_vars());
        command.stdin(Stdio::piped());
        // RPC runs over the unix socket and readiness over stderr; stdout is never read,
        // so a piped stdout would block the runtime once the pipe buffer fills.
        command.stdout(Stdio::null());
        command.stderr(Stdio::piped());
        command.kill_on_drop(true);

        let mut child = command
            .spawn()
            .map_err(|e| ProcessError::Spawn(e.to_string()))?;

        let stderr = child
            .stderr
            .take()
            .ok_or_else(|| ProcessError::Spawn("stderr was not captured".to_string()))?;

        let (ready_tx, ready_rx) = oneshot::channel::<bool>();
        tokio::spawn(async move {
            let mut lines = BufReader::new(stderr).lines();
            let mut tx = Some(ready_tx);
            while let Ok(Some(line)) = lines.next_line().await {
                tracing::debug!(target: "maho_agent::omo", "[omo-stderr] {line}");
                if line.contains(READY_MARKER) {
                    if let Some(tx) = tx.take() {
                        let _ = tx.send(true);
                    }
                }
            }
            if let Some(tx) = tx.take() {
                let _ = tx.send(false);
            }
        });

        match tokio::time::timeout(READY_TIMEOUT, ready_rx).await {
            Ok(Ok(true)) => Ok(Self {
                child,
                socket_path: socket_path.to_path_buf(),
            }),
            Ok(Ok(false)) => {
                // stderr closed without the marker; the process may still be alive, so
                // kill it rather than waiting on it indefinitely.
                let _ = child.start_kill();
                let status = tokio::time::timeout(REAP_TIMEOUT, child.wait())
                    .await
                    .ok()
                    .and_then(Result::ok);
                Err(ProcessError::ExitedEarly(
                    status.map_or_else(|| "unknown".to_string(), |s| s.to_string()),
                ))
            }
            Ok(Err(_)) => {
                let _ = child.start_kill();
                Err(ProcessError::NoReadySignal)
            }
            Err(_) => {
                let _ = child.start_kill();
                let _ = child.wait().await;
                Err(ProcessError::ReadyTimeout(READY_TIMEOUT))
            }
        }
    }

    /// True once the runtime process has exited (or its status can no longer be read).
    pub fn has_exited(&mut self) -> bool {
        !matches!(self.child.try_wait(), Ok(None))
    }

    pub async fn shutdown(&mut self) -> Result<(), ProcessError> {
        self.child.start_kill()?;
        self.child.wait().await?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rpc_argv_omits_multi_session_and_uses_unix_scheme() {
        let argv = rpc_argv(
            Path::new("/opt/omo/rpc-entry.js"),
            Path::new("/tmp/x/rpc.sock"),
            None,
        );

        assert_eq!(argv[0], "/opt/omo/rpc-entry.js");
        assert_eq!(argv[1], "--listen");
        assert_eq!(argv[2], "unix:///tmp/x/rpc.sock");
        assert_eq!(argv[3], "--no-builtin-tools");
        assert!(
            !argv.iter().any(|a| a == "--multi-session"),
            "--listen already implies multi-session; passing it again is redundant: {argv:?}"
        );
    }

    #[test]
    fn rpc_argv_includes_system_prompt_when_provided() {
        let argv = rpc_argv(
            Path::new("/opt/omo/rpc-entry.js"),
            Path::new("/tmp/x/rpc.sock"),
            Some(Path::new("/tmp/system_prompt.txt")),
        );

        assert_eq!(argv[0], "/opt/omo/rpc-entry.js");
        assert_eq!(argv[1], "--listen");
        assert_eq!(argv[2], "unix:///tmp/x/rpc.sock");
        assert_eq!(argv[3], "--no-builtin-tools");
        assert_eq!(argv[4], "--system-prompt");
        assert_eq!(argv[5], "/tmp/system_prompt.txt");
    }

    #[test]
    fn env_scrub_matches_agent_prefixes_only() {
        for key in ["OMO_CODING_AGENT_DIR", "SENPI_BRAND", "senpi_rpc_x", "PI_CODING_AGENT_DIR"] {
            assert!(is_ambient_agent_env(key), "{key}");
        }
        for key in ["TOMORROW", "PROMO_CODE", "HOMEBREW_PREFIX", "MAHO_CODING_AGENT_DIR", "PATH"] {
            assert!(!is_ambient_agent_env(key), "{key}");
        }
    }

    fn scratch_config(dir: &Path) -> OmoLaunchConfig {
        OmoLaunchConfig::builder(dir.join("agent"))
            .runtime_binary("/bin/sh")
            .rpc_entry(dir.join("runtime.sh"))
            .socket_path(dir.join("rpc.sock"))
            .build()
            .unwrap()
    }

    #[tokio::test]
    async fn heavy_stdout_does_not_block_readiness() {
        let dir = tempfile::tempdir().unwrap();
        // 256 KiB on stdout exceeds any pipe buffer before the ready marker is written.
        std::fs::write(
            dir.path().join("runtime.sh"),
            "head -c 262144 /dev/zero\necho 'rpc listening on fake' >&2\nexec sleep 300\n",
        )
        .unwrap();
        let config = scratch_config(dir.path());
        let mut process = tokio::time::timeout(
            std::time::Duration::from_secs(10),
            OmoProcess::spawn(config.runtime_binary(), config.rpc_entry(), config.socket_path(), &config),
        )
        .await
        .expect("stdout output must not stall readiness")
        .expect("runtime reports ready");
        process.shutdown().await.unwrap();
    }

    #[tokio::test]
    async fn closed_stderr_without_ready_kills_instead_of_hanging() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("runtime.sh"), "exec 2>&-\nexec sleep 300\n").unwrap();
        let config = scratch_config(dir.path());
        let result = tokio::time::timeout(
            std::time::Duration::from_secs(10),
            OmoProcess::spawn(config.runtime_binary(), config.rpc_entry(), config.socket_path(), &config),
        )
        .await
        .expect("a live runtime with closed stderr must not hang spawn");
        assert!(matches!(result, Err(ProcessError::ExitedEarly(_))));
    }

    #[test]
    fn ready_marker_matches_the_servers_actual_stderr_line() {
        let observed = "senpi rpc listening on unix:///tmp/omoprobe/rpc3.sock";
        assert!(observed.contains(READY_MARKER));
    }
}
