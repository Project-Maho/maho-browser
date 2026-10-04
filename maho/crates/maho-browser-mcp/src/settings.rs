// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Registry-backed runtime settings for the standalone MCP bridge.
//!
//! The browser persists dot-namespaced settings (`maho.<area>.<flag>`) in the
//! settings table of the shared registry DB
//! (`<user_data_dir>/MahoCore/maho.db`). This module reads that same source,
//! mirroring `maho-cli`'s `open_storage` (oscrypt key derivation + SQLCipher)
//! so the bridge and every other external process agree on one settings store.

use std::path::{Path, PathBuf};

use maho_storage::sqlite::SqliteStorage;

use crate::paths::user_data_dir;

/// Environment variable that overrides the registry DB path (same contract as
/// `maho_cli::workspace::default_db_path`).
pub(crate) const ENV_DB_PATH: &str = "MAHO_DB_PATH";

/// Registry settings key that re-enables the frozen public-MCP dispatch
/// surface. The key is absent by default, so dispatch stays fail-closed.
pub(crate) const MCP_PUBLIC_ENABLED_KEY: &str = "maho.browser.mcp_public_enabled";

/// Resolves the registry DB exactly like the CLI: `MAHO_DB_PATH` override,
/// else `<user_data_dir>/MahoCore/maho.db`.
fn registry_db_path() -> PathBuf {
    if let Ok(path) = std::env::var(ENV_DB_PATH) {
        if !path.is_empty() {
            return PathBuf::from(path);
        }
    }
    user_data_dir().join("MahoCore").join("maho.db")
}

/// Opens the shared registry DB, deriving the SQLCipher key the same way
/// `maho-cli` does. `None` on any failure — callers must fail closed.
fn open_registry(db_path: &Path) -> Option<SqliteStorage> {
    // Test builds only: `set_sqlcipher_key` is process-global, so the
    // set→open pair must never interleave with another test's injection or
    // an open decrypts with the wrong key (nondeterministic failures).
    #[cfg(test)]
    let _key_guard = REGISTRY_KEY_TEST_LOCK
        .lock()
        .unwrap_or_else(|e| e.into_inner());
    if let Some(parent) = db_path.parent() {
        let key_path = parent.join("maho_storage.key");
        let mut key_bytes = [0u8; 16];
        let mut status = 0;
        if maho_core::oscrypt::derive_key(&key_path.to_string_lossy(), &mut key_bytes, &mut status)
        {
            let hex_key: String = key_bytes.iter().map(|byte| format!("{byte:02X}")).collect();
            let _ = maho_storage::sqlite::set_sqlcipher_key(&hex_key);
        }
    }
    SqliteStorage::open(&db_path.to_string_lossy()).ok()
}

/// Reads the public-MCP toggle from the registry settings table. Absent key,
/// missing DB, or any error resolves to `false` (fail-closed, frozen).
pub(crate) fn public_mcp_enabled() -> bool {
    if let Ok(val) = std::env::var("MAHO_MCP_PUBLIC_ENABLED") {
        if val == "1" || val.eq_ignore_ascii_case("true") {
            return true;
        }
    }
    public_mcp_enabled_in_db(&registry_db_path())
}

/// `public_mcp_enabled` against an explicit DB path (test seam; identical
/// fail-closed semantics).
fn public_mcp_enabled_in_db(db_path: &Path) -> bool {
    open_registry(db_path)
        .and_then(|storage| storage.get_setting(MCP_PUBLIC_ENABLED_KEY).ok())
        .flatten()
        .is_some_and(|value| value == "true")
}

/// Serializes tests that mutate `MAHO_DB_PATH` around server construction
/// (the construction reads the setting synchronously). Distinct from
/// `REGISTRY_KEY_TEST_LOCK`, which `open_registry` takes inside itself; the
/// only nesting order is env lock → key lock, so no deadlock is possible.
#[cfg(test)]
pub(crate) static SETTINGS_TEST_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

/// Opens a registry DB at an explicit path for fixture setup.
#[cfg(test)]
pub(crate) fn open_registry_for_test(db_path: &Path) -> Option<SqliteStorage> {
    open_registry(db_path)
}

/// Seeds one settings row into a registry DB for fixture setup.
#[cfg(test)]
pub(crate) fn seed_setting(db_path: &Path, key: &str, value: &str) {
    let storage = open_registry(db_path).expect("registry must open for seeding");
    storage.set_setting(key, value).expect("seed set_setting");
}

/// Serializes the process-global SQLCipher key injection in test builds:
/// `set_sqlcipher_key` + `SqliteStorage::open` must never interleave with
/// another test's injection, or an open decrypts with the wrong key.
#[cfg(test)]
static REGISTRY_KEY_TEST_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn absent_setting_keeps_public_dispatch_frozen() {
        let dir = tempfile::tempdir().expect("tempdir");
        let db_path = dir.path().join("maho.db");
        // Materialize a real registry DB that does not carry the toggle key.
        assert!(open_registry(&db_path).is_some(), "registry DB opens");
        assert!(!public_mcp_enabled_in_db(&db_path));
    }

    #[test]
    fn explicit_true_enables_and_other_values_stay_frozen() {
        let dir = tempfile::tempdir().expect("tempdir");
        let db_path = dir.path().join("maho.db");

        seed_setting(&db_path, MCP_PUBLIC_ENABLED_KEY, "false");
        assert!(!public_mcp_enabled_in_db(&db_path));

        seed_setting(&db_path, MCP_PUBLIC_ENABLED_KEY, "TRUE");
        assert!(
            !public_mcp_enabled_in_db(&db_path),
            "only the exact 'true' spelling mirrors the open_external_links_in_maho_mini convention"
        );

        seed_setting(&db_path, MCP_PUBLIC_ENABLED_KEY, "true");
        assert!(public_mcp_enabled_in_db(&db_path));
    }

    #[test]
    fn unopenable_registry_fails_closed() {
        let dir = tempfile::tempdir().expect("tempdir");
        let db_path = dir.path().join("missing-dir").join("maho.db");
        assert!(!public_mcp_enabled_in_db(&db_path));
    }
}
