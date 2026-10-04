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
        for (k, _) in std::env::vars() {
            let lower = k.to_ascii_lowercase();
            if lower.contains("senpi") || lower.contains("omo") || lower.contains("coding_agent") {
                command.env_remove(&k);
            }
        }

        command.envs(config.env_vars());
        command.stdin(Stdio::piped());
        command.stdout(Stdio::piped());
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
                eprintln!("[omo-stderr] {line}");
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
                let status = child.wait().await.ok();
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
    fn ready_marker_matches_the_servers_actual_stderr_line() {
        let observed = "senpi rpc listening on unix:///tmp/omoprobe/rpc3.sock";
        assert!(observed.contains(READY_MARKER));
    }
}
