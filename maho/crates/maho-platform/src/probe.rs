//! Injectable command runner used by external-helper backends. Production uses
//! `SystemRunner`; tests inject fakes so Linux/Windows helper logic is fully
//! deterministic on any host.

use crate::error::PlatformError;
use std::path::{Path, PathBuf};
use std::process::Command;

/// Result of one external process invocation.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProcessOutput {
    pub status: i32,
    pub stdout: String,
    pub stderr: String,
}

/// Runs helper programs and resolves them on `PATH`.
pub trait CommandRunner {
    fn which(&self, program: &str) -> Option<PathBuf>;
    fn run(&self, program: &str, args: &[&str]) -> Result<ProcessOutput, PlatformError>;
    fn run_with_stdin(
        &self,
        program: &str,
        args: &[&str],
        stdin: &str,
    ) -> Result<ProcessOutput, PlatformError>;
}

fn map_io(err: std::io::Error) -> PlatformError {
    PlatformError::Io(err.to_string())
}

/// Production runner backed by `std::process`.
#[derive(Debug, Clone, Copy, Default)]
pub struct SystemRunner;

impl CommandRunner for SystemRunner {
    fn which(&self, program: &str) -> Option<PathBuf> {
        if program.contains(std::path::MAIN_SEPARATOR) {
            let p = Path::new(program);
            return p.is_file().then(|| p.to_path_buf());
        }
        let path = std::env::var_os("PATH")?;
        for dir in std::env::split_paths(&path) {
            let candidate = dir.join(program);
            if candidate.is_file() {
                return Some(candidate);
            }
        }
        None
    }

    fn run(&self, program: &str, args: &[&str]) -> Result<ProcessOutput, PlatformError> {
        let out = Command::new(program).args(args).output().map_err(map_io)?;
        Ok(ProcessOutput {
            status: out.status.code().unwrap_or(-1),
            stdout: String::from_utf8_lossy(&out.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&out.stderr).into_owned(),
        })
    }

    fn run_with_stdin(
        &self,
        program: &str,
        args: &[&str],
        stdin: &str,
    ) -> Result<ProcessOutput, PlatformError> {
        use std::io::Write;
        let mut child = Command::new(program)
            .args(args)
            .stdin(std::process::Stdio::piped())
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .spawn()
            .map_err(map_io)?;
        if let Some(pipe) = child.stdin.as_mut() {
            pipe.write_all(stdin.as_bytes()).map_err(map_io)?;
        }
        let out = child.wait_with_output().map_err(map_io)?;
        Ok(ProcessOutput {
            status: out.status.code().unwrap_or(-1),
            stdout: String::from_utf8_lossy(&out.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&out.stderr).into_owned(),
        })
    }
}
