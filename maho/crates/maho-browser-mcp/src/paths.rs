// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Canonical path resolution for the Maho Browser MCP transport.
//!
//! The browser listens on a Unix domain socket (named pipe on Windows) at a
//! well-known path derived from the product's user-data directory. This module
//! is the single source of truth for that directory so the socket path and the
//! CLI registry DB path (`maho-cli`) can never diverge — a past divergence put
//! the Linux socket in `$XDG_RUNTIME_DIR` where the browser never looks.

use std::path::PathBuf;

/// Environment variable that, when set, overrides the default socket path.
pub const ENV_SOCKET_PATH: &str = "MAHO_MCP_SOCKET_PATH";

/// Mirrors Chromium's `DIR_USER_DATA` for the Maho product per platform. The
/// C++ browser derives both its socket (`<DIR_USER_DATA>/maho.sock`) and its
/// registry DB (`<DIR_USER_DATA>/MahoCore/maho.db`) from this directory, so the
/// CLI/agent MUST resolve the same base. macOS is verified live
/// (`CrProductDirName=Maho`); Linux/Windows follow Chromium's per-platform
/// convention and are pending live confirmation on a real build of each.
#[cfg(target_os = "macos")]
pub fn user_data_dir() -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    PathBuf::from(home).join("Library/Application Support/Maho")
}

#[cfg(target_os = "linux")]
pub fn user_data_dir() -> PathBuf {
    if let Ok(xdg) = std::env::var("XDG_CONFIG_HOME") {
        if !xdg.is_empty() {
            return PathBuf::from(xdg).join("maho");
        }
    }
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    PathBuf::from(home).join(".config/maho")
}

#[cfg(target_os = "windows")]
pub fn user_data_dir() -> PathBuf {
    if let Ok(local) = std::env::var("LOCALAPPDATA") {
        if !local.is_empty() {
            return PathBuf::from(local).join("Maho").join("User Data");
        }
    }
    PathBuf::from(r"C:\Temp").join("Maho").join("User Data")
}

#[cfg(not(any(target_os = "macos", target_os = "linux", target_os = "windows")))]
pub fn user_data_dir() -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    PathBuf::from(home).join(".config/maho")
}

/// Returns the path to the Maho Browser MCP transport endpoint.
///
/// Resolution order:
/// 1. `MAHO_MCP_SOCKET_PATH` environment variable (if set and non-empty).
/// 2. Platform default:
///    - Unix (macOS/Linux): `<user_data_dir>/maho.sock` — matches the C++
///      browser's `ComputeSocketPath` = `<DIR_USER_DATA>/maho.sock`.
///    - Windows: `\\.\pipe\maho-browser-<username>` named pipe.
pub fn default_socket_path() -> PathBuf {
    // Environment override takes precedence.
    if let Ok(path) = std::env::var(ENV_SOCKET_PATH) {
        if !path.is_empty() {
            return PathBuf::from(path);
        }
    }

    platform_default_path()
}

/// Unix domain socket lives directly inside the user-data dir, next to the
/// `MahoCore/` subdir that holds the registry DB.
#[cfg(not(target_os = "windows"))]
fn platform_default_path() -> PathBuf {
    user_data_dir().join("maho.sock")
}

/// Mirror of the C++ server's `MahoMcpPipeServer::SanitizeUsername`: keep only
/// `[A-Za-z0-9_-]` so the client pipe name is byte-identical to the server's,
/// regardless of spaces / non-ASCII characters in the account name. Compiled in
/// test builds on every platform so the parity logic stays covered on macOS CI.
#[cfg(any(target_os = "windows", test))]
fn sanitize_pipe_username(username: &str) -> String {
    username
        .chars()
        .filter(|c| c.is_ascii_alphanumeric() || *c == '_' || *c == '-')
        .collect()
}

/// Windows has no Unix domain sockets; the dogfood transport is a named pipe.
/// The name MUST match the C++ server's `ComputePipeName` exactly:
/// `\\.\pipe\maho-browser-<sanitized-username>`, with a `default` fallback.
/// (The server derives the name from `GetUserNameW`; `%USERNAME%` matches it for
/// interactive sessions — confirm on a real Windows session per Path B.)
#[cfg(target_os = "windows")]
fn platform_default_path() -> PathBuf {
    let raw = std::env::var("USERNAME").unwrap_or_default();
    let mut name = sanitize_pipe_username(&raw);
    if name.is_empty() {
        name = "default".to_string();
    }
    PathBuf::from(format!(r"\\.\pipe\maho-browser-{}", name))
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
            path.ends_with("maho/maho.sock"),
            "Linux socket must be <user_data_dir>/maho.sock, got: {}",
            path_str
        );
    }

    // Regression guard for the divergence bug: on Unix the socket MUST sit
    // directly in the user-data dir the CLI derives the registry DB from, else
    // the CLI/agent cannot find the browser's socket.
    #[cfg(not(target_os = "windows"))]
    #[test]
    fn socket_parent_equals_user_data_dir() {
        let _guard = ENV_LOCK.lock().unwrap();
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };
        let path = default_socket_path();
        assert_eq!(path.parent().unwrap(), user_data_dir());
        assert_eq!(path.file_name().unwrap(), "maho.sock");
    }

    // Parity with the C++ server's SanitizeUsername: only [A-Za-z0-9_-] survive,
    // so the Windows client pipe name matches the server byte-for-byte.
    #[test]
    fn pipe_username_sanitization_matches_cpp() {
        assert_eq!(sanitize_pipe_username("JohnDoe"), "JohnDoe");
        assert_eq!(sanitize_pipe_username("John Doe"), "JohnDoe");
        assert_eq!(sanitize_pipe_username("a_b-c9"), "a_b-c9");
        assert_eq!(sanitize_pipe_username("user.name!"), "username");
        assert_eq!(sanitize_pipe_username("서준"), "");
    }
}
