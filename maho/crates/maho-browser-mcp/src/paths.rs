// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Canonical socket path resolution for the Maho Browser MCP transport.
//!
//! The browser listens on a Unix domain socket at a well-known path.
//! This module determines that path, respecting an environment variable
//! override for testing and non-standard deployments.

use std::path::PathBuf;

/// Environment variable that, when set, overrides the default socket path.
pub const ENV_SOCKET_PATH: &str = "MAHO_MCP_SOCKET_PATH";

/// Returns the path to the Maho Browser MCP Unix domain socket.
///
/// Resolution order:
/// 1. `MAHO_MCP_SOCKET_PATH` environment variable (if set and non-empty).
/// 2. Platform default:
///    - macOS: `~/Library/Application Support/Maho/maho.sock`
///    - Linux: `$XDG_RUNTIME_DIR/maho-browser.sock` or
///             `~/.local/state/maho/maho.sock` as fallback.
///    - Windows: `\\.\pipe\maho-browser-<username>` (stub for future Named Pipe support).
pub fn default_socket_path() -> PathBuf {
    // Environment override takes precedence.
    if let Ok(path) = std::env::var(ENV_SOCKET_PATH) {
        if !path.is_empty() {
            return PathBuf::from(path);
        }
    }

    platform_default_path()
}

#[cfg(target_os = "macos")]
fn platform_default_path() -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    PathBuf::from(home).join("Library/Application Support/Maho/maho.sock")
}

#[cfg(target_os = "linux")]
fn platform_default_path() -> PathBuf {
    if let Ok(xdg) = std::env::var("XDG_RUNTIME_DIR") {
        if !xdg.is_empty() {
            return PathBuf::from(xdg).join("maho-browser.sock");
        }
    }
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    PathBuf::from(home).join(".local/state/maho/maho.sock")
}

#[cfg(target_os = "windows")]
fn platform_default_path() -> PathBuf {
    let username = std::env::var("USERNAME").unwrap_or_else(|_| "unknown".to_string());
    PathBuf::from(format!(r"\\.\pipe\maho-browser-{}", username))
}

#[cfg(not(any(target_os = "macos", target_os = "linux", target_os = "windows")))]
fn platform_default_path() -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    PathBuf::from(home).join(".local/state/maho/maho.sock")
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    // Env var mutations are inherently racy across parallel tests.
    static ENV_LOCK: Mutex<()> = Mutex::new(());

    #[test]
    fn env_override_wins() {
        let _guard = ENV_LOCK.lock().unwrap();
        unsafe { std::env::set_var(ENV_SOCKET_PATH, "/foo/bar/test.sock") };
        let path = default_socket_path();
        assert_eq!(path, PathBuf::from("/foo/bar/test.sock"));
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };
    }

    #[test]
    fn empty_env_falls_through_to_platform() {
        let _guard = ENV_LOCK.lock().unwrap();
        unsafe { std::env::set_var(ENV_SOCKET_PATH, "") };
        let path = default_socket_path();
        assert!(!path.as_os_str().is_empty());
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };
    }

    #[test]
    fn platform_default_is_reasonable() {
        let _guard = ENV_LOCK.lock().unwrap();
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };
        let path = default_socket_path();
        let path_str = path.to_string_lossy();

        #[cfg(target_os = "macos")]
        assert!(
            path_str.contains("Library/Application Support/Maho/maho.sock"),
            "unexpected path: {}",
            path_str
        );

        #[cfg(target_os = "linux")]
        assert!(
            path_str.contains("maho"),
            "unexpected path: {}",
            path_str
        );
    }
}
