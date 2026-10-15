//! OS process sandbox backends for agent shell execution.

use std::path::PathBuf;

#[derive(Clone, Debug, Default)]
pub struct SandboxPolicy {
    pub writable_roots: Vec<PathBuf>,
    pub readable_roots: Vec<PathBuf>,
    pub denied_paths: Vec<PathBuf>,
    pub network: bool,
}

#[derive(Debug, thiserror::Error)]
pub enum SandboxError {
    #[error("sandbox unavailable: {0}")]
    Unavailable(String),
    #[error("sandbox preparation failed: {0}")]
    Prepare(String),
}

pub trait SandboxBackend {
    fn prepare(&self, policy: &SandboxPolicy) -> Result<PreparedSandbox, SandboxError>;
}

pub struct PreparedSandbox {
    #[cfg(target_os = "linux")]
    policy: SandboxPolicy,
    #[cfg(target_os = "macos")]
    profile: String,
}

impl PreparedSandbox {
    /// Apply the prepared policy in the post-fork child before exec. Descendants inherit it.
    #[cfg(target_os = "linux")]
    pub(crate) fn apply(&self) -> Result<(), SandboxError> {
        linux::apply_policy(&self.policy)
    }

    /// The command line that runs `program` under the prepared Seatbelt profile.
    #[cfg(target_os = "macos")]
    pub(crate) fn command_line(&self, program: &str, args: &[String]) -> (String, Vec<String>) {
        let mut wrapped = vec!["-p".to_string(), self.profile.clone(), program.to_string()];
        wrapped.extend(args.iter().cloned());
        (macos::SANDBOX_EXEC.to_string(), wrapped)
    }
}

#[cfg(target_os = "linux")]
pub mod linux;
#[cfg(target_os = "macos")]
pub mod macos;
#[cfg(target_os = "windows")]
pub mod windows;
