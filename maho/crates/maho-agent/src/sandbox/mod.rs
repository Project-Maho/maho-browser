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
}

impl PreparedSandbox {
    /// Apply the prepared policy in the post-fork child before exec. Descendants inherit it.
    #[cfg(target_os = "linux")]
    pub(crate) fn apply(&self) -> Result<(), SandboxError> {
        linux::apply_policy(&self.policy)
    }
}

#[cfg(target_os = "linux")]
pub mod linux;
#[cfg(target_os = "windows")]
pub mod windows;
