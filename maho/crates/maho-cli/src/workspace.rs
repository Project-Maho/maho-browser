use maho_storage::sqlite::SqliteStorage;
use std::path::PathBuf;

/// Must resolve to the SAME file the browser core opens, else `maho mcp
/// add/list/remove` silently operate on a phantom DB the WebUI never reads.
/// Source of truth: `maho_browser_main_extra_parts.cc` opens
/// `DIR_USER_DATA/MahoCore`, and `maho-ffi/src/lib.rs` appends the fixed
/// `maho.db` filename, giving `<user_data_dir>/MahoCore/maho.db`. The
/// `user_data_dir` base is shared with the MCP socket path (see
/// `maho_browser_mcp::paths`) so the two can never diverge.
///
/// `MAHO_DB_PATH` overrides for tests / non-standard deployments.
pub fn default_db_path() -> PathBuf {
    resolve_db_path(None)
}

/// Resolve the effective database path for an optional explicit socket,
/// respecting `MAHO_DB_PATH` if configured.
pub fn resolve_db_path(socket_path: Option<&str>) -> PathBuf {
    if let Ok(p) = std::env::var("MAHO_DB_PATH") {
        if !p.is_empty() {
            return PathBuf::from(p);
        }
    }
    maho_browser_mcp::paths::resolve_user_data_dir(socket_path)
        .join("MahoCore")
        .join("maho.db")
}

pub fn open_storage(
    db_path: &std::path::Path,
) -> Result<SqliteStorage, maho_storage::StorageError> {
    if let Some(parent) = db_path.parent() {
        let key_path = parent.join("maho_storage.key");
        let mut key_bytes = [0u8; 16];
        let mut status = 0;
        if maho_core::oscrypt::derive_key(&key_path.to_string_lossy(), &mut key_bytes, &mut status)
        {
            let hex_key: String = key_bytes.iter().map(|b| format!("{:02X}", b)).collect();
            let _ = maho_storage::sqlite::set_sqlcipher_key(&hex_key);
        }
    }
    SqliteStorage::open(&db_path.to_string_lossy())
}

pub async fn resolve_workspace_id(
    db_path: &std::path::Path,
    override_workspace: Option<&str>,
) -> anyhow::Result<String> {
    let db_path_buf = db_path.to_path_buf();
    let override_workspace = override_workspace.map(|s| s.to_string());

    tokio::task::spawn_blocking(move || {
        let db_path_str = db_path_buf.to_string_lossy().to_string();
        let storage = open_storage(&db_path_buf)
            .map_err(|e| anyhow::anyhow!("Failed to open database at {}: {}", db_path_str, e))?;

        let workspaces = storage
            .list_workspaces()
            .map_err(|e| anyhow::anyhow!("Failed to list workspaces: {}", e))?;

        if workspaces.is_empty() {
            // A registry with no workspace is normal for a CLI-only or a
            // benchmark profile: the browser creates one lazily on first AI
            // use, and the CLI would otherwise be unusable until someone opens
            // the UI. Bootstrap the shared default instead of failing.
            if override_workspace.is_some() {
                anyhow::bail!(
                    "No AI workspace exists yet in the Maho registry at {db_path_str}, \
                     so --workspace cannot be resolved. Drop --workspace to bootstrap \
                     the default workspace, or point MAHO_DB_PATH at another registry."
                );
            }

            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_secs().to_string())
                .unwrap_or_else(|_| "0".to_string());
            let bootstrap = maho_types::ai::AiWorkspace {
                id: "default".to_string(),
                name: "Default".to_string(),
                space_id: None,
                profile_id: Some("blank".to_string()),
                workspace_root: None,
                created_at: now.clone(),
                updated_at: now,
            };
            storage
                .create_workspace(&bootstrap)
                .map_err(|e| anyhow::anyhow!("Failed to bootstrap default AI workspace: {}", e))?;
            return Ok(bootstrap.id);
        }

        if let Some(ref target) = override_workspace {
            if target != "current" {
                if let Some(ws) = workspaces
                    .iter()
                    .find(|w| &w.id == target || &w.name == target)
                {
                    return Ok(ws.id.clone());
                }
                anyhow::bail!("Workspace '{}' not found", target);
            }
        }

        // Default: workspaces[0] — mirrors the WebUI rule (store.ts: spaceId || workspaces[0]).
        // list_workspaces returns rows ordered by created_at, id so this is deterministic.
        // Prefer passing --workspace <id|name> for explicit selection.
        Ok(workspaces[0].id.clone())
    })
    .await?
}

#[cfg(test)]
mod tests {
    use super::*;
    use maho_browser_mcp::paths::user_data_dir;
    use maho_types::ai::AiWorkspace;
    use std::sync::Mutex;

    static ENV_LOCK: Mutex<()> = Mutex::new(());

    // `open_storage` installs a *process-global* SQLCipher key
    // (`maho_storage::sqlite::SQLCIPHER_KEY`). Each test derives a distinct
    // key from its own tempdir, so two storage-opening tests running in
    // parallel clobber each other's global key and one open then fails with
    // "file is not a database". Serialize those tests through this lock.
    static DB_KEY_TEST_LOCK: tokio::sync::Mutex<()> = tokio::sync::Mutex::const_new(());

    #[test]
    fn default_db_path_targets_maho_core_registry() {
        let _guard = ENV_LOCK.lock().unwrap();
        unsafe { std::env::remove_var("MAHO_DB_PATH") };

        let path = default_db_path();
        assert!(
            path.ends_with("MahoCore/maho.db"),
            "default_db_path must resolve to the browser's registry (<user_data_dir>/MahoCore/maho.db), got {}",
            path.display()
        );

        let expected = user_data_dir().join("MahoCore").join("maho.db");
        assert_eq!(
            path, expected,
            "default_db_path must strictly equal <user_data_dir>/MahoCore/maho.db"
        );
    }

    #[test]
    fn maho_db_path_override_wins() {
        let _guard = ENV_LOCK.lock().unwrap();
        unsafe { std::env::set_var("MAHO_DB_PATH", "/tmp/override/custom.db") };
        let path = default_db_path();
        unsafe { std::env::remove_var("MAHO_DB_PATH") };
        assert_eq!(path, PathBuf::from("/tmp/override/custom.db"));
    }

    fn make_test_workspace(id: &str, name: &str, root: Option<&str>) -> AiWorkspace {
        let now = chrono::Utc::now().to_rfc3339();
        AiWorkspace {
            id: id.to_string(),
            name: name.to_string(),
            space_id: None,
            profile_id: Some("blank".to_string()),
            workspace_root: root.map(|s| s.to_string()),
            created_at: now.clone(),
            updated_at: now,
        }
    }

    #[tokio::test]
    async fn test_resolve_workspace_by_id_or_name() {
        let _db_guard = DB_KEY_TEST_LOCK.lock().await;
        let dir = tempfile::tempdir().unwrap();
        let db_path = dir.path().join("maho.db");
        let storage = open_storage(&db_path).unwrap();

        let ws1 = make_test_workspace("ws-1", "First Workspace", None);
        let ws2 = make_test_workspace("ws-2", "Second Workspace", Some("/tmp/project"));

        storage.create_workspace(&ws1).unwrap();
        storage.create_workspace(&ws2).unwrap();

        // 1. Resolve by ID
        let res = resolve_workspace_id(&db_path, Some("ws-2")).await.unwrap();
        assert_eq!(res, "ws-2");

        // 2. Resolve by Name
        let res = resolve_workspace_id(&db_path, Some("Second Workspace"))
            .await
            .unwrap();
        assert_eq!(res, "ws-2");

        // 3. Not found error
        let res = resolve_workspace_id(&db_path, Some("Nonexistent")).await;
        assert!(res.is_err());
    }

    #[tokio::test]
    async fn test_resolve_workspace_current_fallback() {
        let _db_guard = DB_KEY_TEST_LOCK.lock().await;
        let dir = tempfile::tempdir().unwrap();
        let db_path = dir.path().join("maho.db");
        let storage = open_storage(&db_path).unwrap();

        let ws1 = make_test_workspace("ws-1", "Default Workspace", None);
        storage.create_workspace(&ws1).unwrap();

        // Resolve current when no match
        let res = resolve_workspace_id(&db_path, None).await.unwrap();
        assert_eq!(res, "ws-1");

        let res = resolve_workspace_id(&db_path, Some("current"))
            .await
            .unwrap();
        assert_eq!(res, "ws-1");
    }

    #[test]
    fn sqlcipher_key_formatter_uses_uppercase_hex() {
        let sample_bytes: [u8; 16] = [
            0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60, 0x71, 0x82, 0x93, 0xa4, 0xb5, 0xc6, 0xd7,
            0xe8, 0xf9,
        ];
        let hex_key: String = sample_bytes.iter().map(|b| format!("{:02X}", b)).collect();
        assert_eq!(hex_key, "0A1B2C3D4E5F60718293A4B5C6D7E8F9");
        assert!(hex_key
            .chars()
            .all(|c| c.is_ascii_uppercase() || c.is_ascii_digit()));
    }
}
