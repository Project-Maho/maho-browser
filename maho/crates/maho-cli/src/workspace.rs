use maho_storage::sqlite::SqliteStorage;
use std::path::PathBuf;

/// Must resolve to the SAME file the browser core opens, else `maho mcp
/// add/list/remove` silently operate on a phantom DB the WebUI never reads.
/// Source of truth: `maho_browser_main_extra_parts.cc` opens
/// `DIR_USER_DATA/MahoCore`, and `maho-ffi/src/lib.rs` appends the fixed
/// `maho.db` filename, giving `<user_data_dir>/MahoCore/maho.db`.
///
/// `MAHO_DB_PATH` overrides for tests / non-standard deployments. This is NOT
/// derived from the socket's parent: on Linux the socket lives in a runtime
/// dir, not the user_data_dir, so that heuristic diverged from the browser.
pub fn default_db_path() -> PathBuf {
    if let Ok(p) = std::env::var("MAHO_DB_PATH") {
        return PathBuf::from(p);
    }
    default_user_data_dir().join("MahoCore").join("maho.db")
}

/// Mirrors Chromium's `DIR_USER_DATA` for the Maho product per platform so the
/// CLI and browser agree on where the registry DB lives. macOS is verified live
/// (`CrProductDirName=Maho` → `~/Library/Application Support/Maho`); Linux and
/// Windows follow Chromium's per-platform convention with the Maho product dir.
#[cfg(target_os = "macos")]
fn default_user_data_dir() -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    PathBuf::from(home).join("Library/Application Support/Maho")
}

#[cfg(target_os = "linux")]
fn default_user_data_dir() -> PathBuf {
    if let Ok(xdg) = std::env::var("XDG_CONFIG_HOME") {
        if !xdg.is_empty() {
            return PathBuf::from(xdg).join("maho");
        }
    }
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    PathBuf::from(home).join(".config/maho")
}

#[cfg(target_os = "windows")]
fn default_user_data_dir() -> PathBuf {
    if let Ok(local) = std::env::var("LOCALAPPDATA") {
        if !local.is_empty() {
            return PathBuf::from(local).join("Maho").join("User Data");
        }
    }
    PathBuf::from(r"C:\Temp").join("Maho").join("User Data")
}

#[cfg(not(any(target_os = "macos", target_os = "linux", target_os = "windows")))]
fn default_user_data_dir() -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    PathBuf::from(home).join(".config/maho")
}

pub async fn resolve_workspace_id(
    db_path: &std::path::Path,
    override_workspace: Option<&str>,
) -> anyhow::Result<String> {
    let db_path_str = db_path.to_string_lossy().to_string();
    let override_workspace = override_workspace.map(|s| s.to_string());

    tokio::task::spawn_blocking(move || {
        let storage = SqliteStorage::open(&db_path_str)
            .map_err(|e| anyhow::anyhow!("Failed to open database at {}: {}", db_path_str, e))?;

        let workspaces = storage.list_workspaces()
            .map_err(|e| anyhow::anyhow!("Failed to list workspaces: {}", e))?;

        if workspaces.is_empty() {
            anyhow::bail!(
                "No AI workspace exists yet in the Maho registry at {db_path_str}.\n\
                 AI workspaces are created by the browser the first time you open an AI space; \
                 a fresh profile has none. To manage MCP servers from the CLI:\n  \
                 1. Open Maho and use an AI space once (this creates the workspace the WebUI and CLI share), then retry; or\n  \
                 2. Point --workspace <id|name> at an existing workspace, or MAHO_DB_PATH at another registry."
            );
        }

        if let Some(ref target) = override_workspace {
            if target != "current" {
                if let Some(ws) = workspaces.iter().find(|w| &w.id == target || &w.name == target) {
                    return Ok(ws.id.clone());
                }
                anyhow::bail!("Workspace '{}' not found", target);
            }
        }

        // Default: workspaces[0] — mirrors the WebUI rule (store.ts: spaceId || workspaces[0]).
        // list_workspaces returns rows ordered by created_at, id so this is deterministic.
        // Prefer passing --workspace <id|name> for explicit selection.
        Ok(workspaces[0].id.clone())
    }).await?
}

#[cfg(test)]
mod tests {
    use super::*;
    use maho_types::ai::AiWorkspace;
    use std::sync::Mutex;

    static ENV_LOCK: Mutex<()> = Mutex::new(());

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

        #[cfg(target_os = "macos")]
        assert!(
            path.to_string_lossy()
                .contains("Library/Application Support/Maho/MahoCore/maho.db"),
            "macOS default must match the browser's CrProductDirName=Maho user_data_dir, got {}",
            path.display()
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
        let dir = tempfile::tempdir().unwrap();
        let db_path = dir.path().join("maho.db");
        let storage = SqliteStorage::open(&db_path.to_string_lossy()).unwrap();

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
        let dir = tempfile::tempdir().unwrap();
        let db_path = dir.path().join("maho.db");
        let storage = SqliteStorage::open(&db_path.to_string_lossy()).unwrap();

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
}
