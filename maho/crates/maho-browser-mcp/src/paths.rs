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

use std::path::{Path, PathBuf};

/// Environment variable that, when set, overrides the default socket path.
pub const ENV_SOCKET_PATH: &str = "MAHO_MCP_SOCKET_PATH";

/// Returns the Linux base configuration directory:
/// `$XDG_CONFIG_HOME` if set and non-empty, otherwise `~/.config` (or `/tmp/.config`).
pub fn linux_base_config_dir() -> PathBuf {
    if let Ok(xdg) = std::env::var("XDG_CONFIG_HOME") {
        if !xdg.is_empty() {
            return PathBuf::from(xdg);
        }
    }
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    PathBuf::from(home).join(".config")
}

/// Resolves the Linux socket path given a base config directory (e.g. `$XDG_CONFIG_HOME`
/// or `~/.config`).
///
/// Resolution order:
/// 1. `MAHO_MCP_SOCKET_PATH` environment variable override (if set and non-empty).
/// 2. `<base_config_dir>/chromium/maho.sock` if it exists (the unbranded Chromium
///    data dir basename where the real Linux browser binds).
/// 3. Legacy `<base_config_dir>/maho/maho.sock` if it exists (back-compat with the
///    documented symlink workaround).
/// 4. Default Chromium path: `<base_config_dir>/chromium/maho.sock`.
pub fn resolve_linux_socket_path(base_config_dir: &Path) -> PathBuf {
    if let Ok(path) = std::env::var(ENV_SOCKET_PATH) {
        if !path.is_empty() {
            return PathBuf::from(path);
        }
    }

    let chromium_socket = base_config_dir.join("chromium").join("maho.sock");
    if chromium_socket.exists() {
        return chromium_socket;
    }

    let legacy_socket = base_config_dir.join("maho").join("maho.sock");
    if legacy_socket.exists() {
        return legacy_socket;
    }

    chromium_socket
}

/// Platform-gated / Linux socket path resolution delegate.
pub fn linux_default_socket_path() -> PathBuf {
    let base = linux_base_config_dir();
    resolve_linux_socket_path(&base)
}

/// Ordered macOS socket candidates under one Application Support directory:
/// branded product data dir first, then the unbranded Chromium data dir.
#[cfg(target_os = "macos")]
fn macos_socket_candidates_in(app_support_dir: &Path) -> Vec<PathBuf> {
    vec![
        app_support_dir.join("Maho").join("maho.sock"),
        app_support_dir.join("Chromium").join("maho.sock"),
    ]
}

/// Ordered macOS socket candidates for diagnostics and self-probing (the
/// branded product data dir first, then the unbranded Chromium data dir where
/// real fork builds bind).
#[cfg(target_os = "macos")]
pub fn socket_path_candidates() -> Vec<PathBuf> {
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    macos_socket_candidates_in(&PathBuf::from(home).join("Library/Application Support"))
}

/// Resolves the macOS socket path given the Application Support directory.
///
/// Resolution order (mirrors [`resolve_linux_socket_path`]):
/// 1. `MAHO_MCP_SOCKET_PATH` environment variable override (if set and non-empty).
/// 2. `<base>/Maho/maho.sock` if it exists (the branded product data dir).
/// 3. `<base>/Chromium/maho.sock` if it exists (the unbranded Chromium data dir
///    basename where real fork builds actually bind — same situation the Linux
///    `chromium` probe covers).
/// 4. Default branded path: `<base>/Maho/maho.sock`.
#[cfg(target_os = "macos")]
fn resolve_macos_socket_path(app_support_dir: &Path) -> PathBuf {
    if let Ok(path) = std::env::var(ENV_SOCKET_PATH) {
        if !path.is_empty() {
            return PathBuf::from(path);
        }
    }
    if let Ok(dir) = std::env::var("MAHO_USER_DATA_DIR") {
        if !dir.is_empty() {
            return PathBuf::from(dir).join("maho.sock");
        }
    }

    let candidates = macos_socket_candidates_in(app_support_dir);
    let fallback = candidates[0].clone();
    candidates
        .into_iter()
        .find(|candidate| candidate.exists())
        .unwrap_or(fallback)
}

/// Mirrors Chromium's `DIR_USER_DATA` for the Maho product per platform. The
/// C++ browser derives both its socket (`<DIR_USER_DATA>/maho.sock`) and its
/// registry DB (`<DIR_USER_DATA>/MahoCore/maho.db`) from this directory, so the
/// CLI/agent MUST resolve the same base. macOS is verified live
/// (`CrProductDirName=Maho`); Linux/Windows follow Chromium's per-platform
/// convention and are pending live confirmation on a real build of each.
#[cfg(target_os = "macos")]
pub fn user_data_dir() -> PathBuf {
    if let Ok(path) = std::env::var(ENV_SOCKET_PATH) {
        if !path.is_empty() {
            let p = PathBuf::from(path);
            let canonical = std::fs::canonicalize(&p).unwrap_or(p);
            if let Some(parent) = canonical.parent() {
                return parent.to_path_buf();
            }
        }
    }
    if let Ok(dir) = std::env::var("MAHO_USER_DATA_DIR") {
        if !dir.is_empty() {
            return PathBuf::from(dir);
        }
    }
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    let app_support = PathBuf::from(home).join("Library/Application Support");
    let sock = resolve_macos_socket_path(&app_support);
    let resolved_sock = std::fs::canonicalize(&sock).unwrap_or(sock);
    resolved_sock
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_else(|| app_support.join("Maho"))
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

/// Resolve the effective user data directory for a given socket path, or the default.
/// If an explicit socket path is provided, it is canonicalized (resolving symlinks)
/// and its parent is used as the user data directory.
/// If no explicit socket path is provided, but socket environment overrides are set,
/// the socket is canonicalized and its parent is used as the user data directory,
/// preventing divergence between tool bridge endpoint and user data directory.
pub fn resolve_user_data_dir(explicit_socket: Option<&str>) -> PathBuf {
    if let Some(sock_str) = explicit_socket {
        if !sock_str.is_empty() {
            let p = PathBuf::from(sock_str);
            let canonical = std::fs::canonicalize(&p).unwrap_or(p);
            if let Some(parent) = canonical.parent() {
                return parent.to_path_buf();
            }
        }
    }
    user_data_dir()
}

/// Returns the path to the Maho Browser MCP transport endpoint.
///
/// Resolution order:
/// 1. `MAHO_MCP_SOCKET_PATH` environment variable (if set and non-empty).
/// 2. Platform default:
///    - macOS: `<user_data_dir>/maho.sock` when it exists, else the unbranded
///      `Chromium` data-dir socket where real fork builds bind (mirrors the
///      Linux probe); falls back to `<user_data_dir>/maho.sock` — matches the
///      C++ browser's `ComputeSocketPath` = `<DIR_USER_DATA>/maho.sock`.
///    - Linux: delegates to [`linux_default_socket_path`].
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

/// macOS Unix domain socket lives directly inside the user-data dir; probe
/// branded and unbranded data-dir basenames exactly like the Linux resolver.
#[cfg(target_os = "macos")]
fn platform_default_path() -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    resolve_macos_socket_path(&PathBuf::from(home).join("Library/Application Support"))
}

/// Linux Unix domain socket resolution delegate.
#[cfg(target_os = "linux")]
fn platform_default_path() -> PathBuf {
    linux_default_socket_path()
}

/// Generic Unix fallback delegates to Linux resolution.
#[cfg(not(any(target_os = "macos", target_os = "linux", target_os = "windows")))]
fn platform_default_path() -> PathBuf {
    linux_default_socket_path()
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

    struct TempDirGuard(PathBuf);
    impl Drop for TempDirGuard {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
            println!("[cleanup receipt] removed temp dir: {:?}", self.0);
        }
    }

    struct EnvVarGuard {
        key: &'static str,
        original: Option<String>,
    }
    impl EnvVarGuard {
        fn set(key: &'static str, val: &std::path::Path) -> Self {
            let original = std::env::var(key).ok();
            unsafe { std::env::set_var(key, val) };
            Self { key, original }
        }
        fn remove(key: &'static str) -> Self {
            let original = std::env::var(key).ok();
            unsafe { std::env::remove_var(key) };
            Self { key, original }
        }
    }
    impl Drop for EnvVarGuard {
        fn drop(&mut self) {
            unsafe {
                match &self.original {
                    Some(v) => std::env::set_var(self.key, v),
                    None => std::env::remove_var(self.key),
                }
            }
            println!("[cleanup receipt] restored env var: {}", self.key);
        }
    }

    fn lock_env() -> std::sync::MutexGuard<'static, ()> {
        ENV_LOCK.lock().unwrap_or_else(|e| e.into_inner())
    }

    #[test]
    fn linux_socket_path_prefers_chromium_socket() {
        let _guard = lock_env();
        let temp_dir_path = std::env::temp_dir().join(format!(
            "maho_linux_sock_test_{}_{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&temp_dir_path).unwrap();
        let _temp_dir_guard = TempDirGuard(temp_dir_path.clone());

        let chromium_dir = temp_dir_path.join("chromium");
        std::fs::create_dir_all(&chromium_dir).unwrap();
        let chromium_sock = chromium_dir.join("maho.sock");
        std::fs::write(&chromium_sock, b"").unwrap();

        let _xdg_guard = EnvVarGuard::set("XDG_CONFIG_HOME", &temp_dir_path);
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };

        let resolved = linux_default_socket_path();
        assert_eq!(resolved, chromium_sock);
    }

    #[test]
    fn linux_socket_path_falls_back_to_legacy_maho_socket() {
        let _guard = lock_env();
        let temp_dir_path = std::env::temp_dir().join(format!(
            "maho_linux_legacy_test_{}_{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&temp_dir_path).unwrap();
        let _temp_dir_guard = TempDirGuard(temp_dir_path.clone());

        let maho_dir = temp_dir_path.join("maho");
        std::fs::create_dir_all(&maho_dir).unwrap();
        let maho_sock = maho_dir.join("maho.sock");
        std::fs::write(&maho_sock, b"").unwrap();

        let _xdg_guard = EnvVarGuard::set("XDG_CONFIG_HOME", &temp_dir_path);
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };

        let resolved = linux_default_socket_path();
        assert_eq!(resolved, maho_sock);
    }

    #[test]
    fn linux_socket_path_defaults_to_chromium_when_neither_exists() {
        let _guard = lock_env();
        let temp_dir_path = std::env::temp_dir().join(format!(
            "maho_linux_default_test_{}_{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&temp_dir_path).unwrap();
        let _temp_dir_guard = TempDirGuard(temp_dir_path.clone());

        let _xdg_guard = EnvVarGuard::set("XDG_CONFIG_HOME", &temp_dir_path);
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };

        let expected = temp_dir_path.join("chromium").join("maho.sock");
        let resolved = linux_default_socket_path();
        assert_eq!(resolved, expected);
    }

    #[test]
    fn linux_socket_path_prefers_chromium_when_both_exist() {
        let _guard = lock_env();
        let temp_dir_path = std::env::temp_dir().join(format!(
            "maho_linux_both_test_{}_{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&temp_dir_path).unwrap();
        let _temp_dir_guard = TempDirGuard(temp_dir_path.clone());

        let chromium_dir = temp_dir_path.join("chromium");
        std::fs::create_dir_all(&chromium_dir).unwrap();
        let chromium_sock = chromium_dir.join("maho.sock");
        std::fs::write(&chromium_sock, b"").unwrap();

        let maho_dir = temp_dir_path.join("maho");
        std::fs::create_dir_all(&maho_dir).unwrap();
        let maho_sock = maho_dir.join("maho.sock");
        std::fs::write(&maho_sock, b"").unwrap();

        let _xdg_guard = EnvVarGuard::set("XDG_CONFIG_HOME", &temp_dir_path);
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };

        let resolved = linux_default_socket_path();
        assert_eq!(resolved, chromium_sock);
    }

    #[test]
    fn linux_socket_path_env_override_precedes_existing_files() {
        let _guard = lock_env();
        let temp_dir_path = std::env::temp_dir().join(format!(
            "maho_linux_env_test_{}_{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&temp_dir_path).unwrap();
        let _temp_dir_guard = TempDirGuard(temp_dir_path.clone());

        let chromium_dir = temp_dir_path.join("chromium");
        std::fs::create_dir_all(&chromium_dir).unwrap();
        let chromium_sock = chromium_dir.join("maho.sock");
        std::fs::write(&chromium_sock, b"").unwrap();

        let _xdg_guard = EnvVarGuard::set("XDG_CONFIG_HOME", &temp_dir_path);
        let override_sock = PathBuf::from("/custom/explicit/maho.sock");
        let _env_guard = EnvVarGuard::set(ENV_SOCKET_PATH, &override_sock);

        let resolved = linux_default_socket_path();
        assert_eq!(resolved, override_sock);
    }

    #[test]
    fn env_override_wins() {
        let _guard = lock_env();
        unsafe { std::env::set_var(ENV_SOCKET_PATH, "/foo/bar/test.sock") };
        let path = default_socket_path();
        assert_eq!(path, PathBuf::from("/foo/bar/test.sock"));
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };
    }

    #[test]
    fn empty_env_falls_through_to_platform() {
        let _guard = lock_env();
        unsafe { std::env::set_var(ENV_SOCKET_PATH, "") };
        let path = default_socket_path();
        assert!(!path.as_os_str().is_empty());
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };
    }

    #[test]
    fn platform_default_is_reasonable() {
        let _guard = lock_env();
        let _user_data_guard = EnvVarGuard::remove("MAHO_USER_DATA_DIR");
        let _mcp_sock_guard = EnvVarGuard::remove("MAHO_MCP_SOCKET_PATH");
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };
        let path = default_socket_path();
        let path_str = path.to_string_lossy();

        #[cfg(target_os = "macos")]
        assert!(
            path_str.contains("Library/Application Support/Maho/maho.sock")
                || path_str.contains("Library/Application Support/Chromium/maho.sock"),
            "unexpected path: {}",
            path_str
        );

        #[cfg(target_os = "linux")]
        assert!(
            path.ends_with("chromium/maho.sock"),
            "Linux socket default must be <base_config_dir>/chromium/maho.sock, got: {}",
            path_str
        );
    }

    // On macOS the socket sits directly in the user-data dir (<user_data_dir>/maho.sock).
    // Pin HOME to an empty temp dir so no live Maho/Chromium socket can win the
    // probe and the branded fallback is exercised deterministically.
    #[cfg(target_os = "macos")]
    #[test]
    fn socket_parent_equals_user_data_dir() {
        let _guard = lock_env();
        let _user_data_guard = EnvVarGuard::remove("MAHO_USER_DATA_DIR");
        let _mcp_sock_guard = EnvVarGuard::remove("MAHO_MCP_SOCKET_PATH");
        let temp_dir_path = std::env::temp_dir().join(format!(
            "maho_macos_parent_test_{}_{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&temp_dir_path).unwrap();
        let _temp_dir_guard = TempDirGuard(temp_dir_path.clone());

        let _home_guard = EnvVarGuard::set("HOME", &temp_dir_path);
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };
        let path = default_socket_path();
        assert_eq!(path.parent().unwrap(), user_data_dir());
        assert_eq!(path.file_name().unwrap(), "maho.sock");
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn macos_socket_path_prefers_branded_when_both_exist() {
        let _guard = lock_env();
        let _user_data_guard = EnvVarGuard::remove("MAHO_USER_DATA_DIR");
        let _mcp_sock_guard = EnvVarGuard::remove("MAHO_MCP_SOCKET_PATH");
        let temp_dir_path = std::env::temp_dir().join(format!(
            "maho_macos_both_test_{}_{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&temp_dir_path).unwrap();
        let _temp_dir_guard = TempDirGuard(temp_dir_path.clone());

        for base in ["Maho", "Chromium"] {
            let dir = temp_dir_path.join("Library/Application Support").join(base);
            std::fs::create_dir_all(&dir).unwrap();
            std::fs::write(dir.join("maho.sock"), b"").unwrap();
        }

        let _home_guard = EnvVarGuard::set("HOME", &temp_dir_path);
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };

        let resolved = default_socket_path();
        assert_eq!(
            resolved,
            temp_dir_path
                .join("Library/Application Support/Maho")
                .join("maho.sock")
        );
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn macos_socket_path_falls_back_to_unbranded_chromium_socket() {
        let _guard = lock_env();
        let _user_data_guard = EnvVarGuard::remove("MAHO_USER_DATA_DIR");
        let _mcp_sock_guard = EnvVarGuard::remove("MAHO_MCP_SOCKET_PATH");
        let temp_dir_path = std::env::temp_dir().join(format!(
            "maho_macos_unbranded_test_{}_{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&temp_dir_path).unwrap();
        let _temp_dir_guard = TempDirGuard(temp_dir_path.clone());

        let chromium_dir = temp_dir_path
            .join("Library/Application Support/Chromium")
            .join("maho.sock");
        std::fs::create_dir_all(chromium_dir.parent().unwrap()).unwrap();
        std::fs::write(&chromium_dir, b"").unwrap();

        let _home_guard = EnvVarGuard::set("HOME", &temp_dir_path);
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };

        let resolved = default_socket_path();
        assert_eq!(resolved, chromium_dir);
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn macos_socket_path_defaults_to_branded_when_neither_exists() {
        let _guard = lock_env();
        let _user_data_guard = EnvVarGuard::remove("MAHO_USER_DATA_DIR");
        let _mcp_sock_guard = EnvVarGuard::remove("MAHO_MCP_SOCKET_PATH");
        let temp_dir_path = std::env::temp_dir().join(format!(
            "maho_macos_neither_test_{}_{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&temp_dir_path).unwrap();
        let _temp_dir_guard = TempDirGuard(temp_dir_path.clone());

        let _home_guard = EnvVarGuard::set("HOME", &temp_dir_path);
        unsafe { std::env::remove_var(ENV_SOCKET_PATH) };

        let resolved = default_socket_path();
        assert_eq!(
            resolved,
            temp_dir_path
                .join("Library/Application Support/Maho")
                .join("maho.sock")
        );
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn macos_socket_path_env_override_precedes_existing_files() {
        let _guard = lock_env();
        let _user_data_guard = EnvVarGuard::remove("MAHO_USER_DATA_DIR");
        let _mcp_sock_guard = EnvVarGuard::remove("MAHO_MCP_SOCKET_PATH");
        let temp_dir_path = std::env::temp_dir().join(format!(
            "maho_macos_env_test_{}_{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&temp_dir_path).unwrap();
        let _temp_dir_guard = TempDirGuard(temp_dir_path.clone());

        let chromium_dir = temp_dir_path
            .join("Library/Application Support/Chromium")
            .join("maho.sock");
        std::fs::create_dir_all(chromium_dir.parent().unwrap()).unwrap();
        std::fs::write(&chromium_dir, b"").unwrap();

        let _home_guard = EnvVarGuard::set("HOME", &temp_dir_path);
        let override_sock = PathBuf::from("/custom/explicit/maho.sock");
        let _env_guard = EnvVarGuard::set(ENV_SOCKET_PATH, &override_sock);

        let resolved = default_socket_path();
        assert_eq!(resolved, override_sock);
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn macos_user_data_dir_env_override_is_respected() {
        let _guard = lock_env();
        let _sock_guard = EnvVarGuard::remove(ENV_SOCKET_PATH);
        let _mcp_guard = EnvVarGuard::remove("MAHO_MCP_SOCKET_PATH");
        let custom_dir = PathBuf::from("/tmp/explicit_user_data_dir_test");
        let _user_data_guard = EnvVarGuard::set("MAHO_USER_DATA_DIR", &custom_dir);

        let udir = user_data_dir();
        assert_eq!(udir, custom_dir);
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn macos_socket_env_override_precedes_user_data_dir() {
        let _guard = lock_env();
        let custom_sock = PathBuf::from("/tmp/sock_dir_override/maho.sock");
        let _sock_guard = EnvVarGuard::set(ENV_SOCKET_PATH, &custom_sock);
        let custom_user_dir = PathBuf::from("/tmp/custom_user_dir");
        let _user_data_guard = EnvVarGuard::set("MAHO_USER_DATA_DIR", &custom_user_dir);

        let udir = user_data_dir();
        assert_eq!(udir, PathBuf::from("/tmp/sock_dir_override"));
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
