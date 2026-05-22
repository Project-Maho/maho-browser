use std::path::PathBuf;
use std::process::Stdio;
use std::sync::Once;

use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::{Child, Command};

use crate::Error;

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

impl AcpProcess {
    /// Spawn the ACP child process.
    pub fn spawn(command_path: &str) -> Result<(Self, tokio::process::ChildStdin, tokio::process::ChildStdout), Error> {
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
}
