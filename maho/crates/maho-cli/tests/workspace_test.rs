use maho_cli::workspace::resolve_workspace_id;
use maho_types::ai::AiWorkspace;

static DB_KEY_TEST_LOCK: tokio::sync::Mutex<()> = tokio::sync::Mutex::const_new(());

fn make_ws(id: &str, name: &str, root: Option<&str>, created_at: &str) -> AiWorkspace {
    AiWorkspace {
        id: id.to_string(),
        name: name.to_string(),
        space_id: None,
        profile_id: Some("blank".to_string()),
        workspace_root: root.map(|s| s.to_string()),
        created_at: created_at.to_string(),
        updated_at: created_at.to_string(),
    }
}

/// WebUI rule: workspaces[0] after ORDER BY created_at, id.
/// In a multi-workspace DB where cwd does NOT match any workspace_root, the CLI
/// default must resolve to the same id the WebUI would pick.
///
/// Under the OLD code the cwd heuristic silently tries every workspace_root before
/// falling back — meaning if ANY workspace had a root that was a parent of cwd, the
/// CLI would return a DIFFERENT workspace than workspaces[0]. This test constructs
/// that case and asserts the new behaviour: default == workspaces[0] regardless of
/// cwd, because the cwd heuristic has been removed from the default path.
#[tokio::test]
async fn cli_default_matches_webui_rule() {
    let _guard = DB_KEY_TEST_LOCK.lock().await;
    let dir = tempfile::tempdir().unwrap();
    let db_path = dir.path().join("maho.db");
    let storage = maho_cli::workspace::open_storage(&db_path).unwrap();

    // ws-oldest is created first (earliest created_at) → it is workspaces[0] after ORDER BY.
    let ws_oldest = make_ws("ws-oldest", "Oldest", None, "2024-01-01T00:00:00Z");
    // ws-cwd has workspace_root = "/" (parent of every possible cwd) so the OLD cwd
    // heuristic would erroneously pick it over ws-oldest.
    let ws_cwd = make_ws("ws-cwd", "CwdMatch", Some("/"), "2024-06-01T00:00:00Z");
    // ws-newer is created last.
    let ws_newer = make_ws("ws-newer", "Newer", None, "2025-01-01T00:00:00Z");

    storage.create_workspace(&ws_oldest).unwrap();
    storage.create_workspace(&ws_cwd).unwrap();
    storage.create_workspace(&ws_newer).unwrap();

    // No explicit --workspace flag → CLI must resolve to the WebUI rule: workspaces[0].
    // After ORDER BY created_at, id, workspaces[0] == ws-oldest.
    // The cwd heuristic (if still active) would wrongly pick ws-cwd because "/" is a
    // parent of cwd. This assertion catches that divergence.
    let resolved = resolve_workspace_id(&db_path, None).await.unwrap();
    assert_eq!(
        resolved, "ws-oldest",
        "CLI default must equal WebUI workspaces[0] (oldest by created_at). \
         Got '{resolved}' — cwd heuristic must not silently override the WebUI rule."
    );
}

/// list_workspaces must return rows in a stable, documented order (created_at, id)
/// across repeated calls regardless of insertion order.
#[tokio::test]
async fn list_workspaces_deterministic_order() {
    let _guard = DB_KEY_TEST_LOCK.lock().await;
    let dir = tempfile::tempdir().unwrap();
    let db_path = dir.path().join("maho.db");
    let storage = maho_cli::workspace::open_storage(&db_path).unwrap();

    // Insert in reverse chronological order to prove ORDER BY, not insertion order, governs.
    let ws_c = make_ws("ws-c", "C", None, "2025-03-01T00:00:00Z");
    let ws_a = make_ws("ws-a", "A", None, "2025-01-01T00:00:00Z");
    let ws_b = make_ws("ws-b", "B", None, "2025-02-01T00:00:00Z");

    storage.create_workspace(&ws_c).unwrap();
    storage.create_workspace(&ws_a).unwrap();
    storage.create_workspace(&ws_b).unwrap();

    let run1: Vec<String> = storage
        .list_workspaces()
        .unwrap()
        .into_iter()
        .map(|w| w.id)
        .collect();
    let run2: Vec<String> = storage
        .list_workspaces()
        .unwrap()
        .into_iter()
        .map(|w| w.id)
        .collect();

    assert_eq!(
        run1, run2,
        "list_workspaces must be deterministic across calls"
    );
    assert_eq!(
        run1,
        vec!["ws-a", "ws-b", "ws-c"],
        "list_workspaces must be ordered by created_at, id (ascending)"
    );
}
