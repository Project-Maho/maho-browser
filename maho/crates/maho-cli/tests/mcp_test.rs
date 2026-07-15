use std::process::Command;
use tempfile::NamedTempFile;

fn run_cli(args: &[&str], db_path: &std::path::Path) -> std::process::Output {
    let bin = env!("CARGO_BIN_EXE_maho");
    let mut cmd = Command::new(bin);
    cmd.args(args);
    cmd.env("MAHO_DB_PATH", db_path);
    cmd.output().expect("failed to run CLI binary")
}

#[test]
fn test_mcp_lifecycle() {
    let db_file = NamedTempFile::new().unwrap();
    let db_path = db_file.path();

    // Initialize default workspace in database first
    let storage = maho_cli::workspace::open_storage(db_path).unwrap();
    let ws = maho_types::ai::AiWorkspace {
        id: "default".to_string(),
        name: "Default Workspace".to_string(),
        space_id: None,
        profile_id: Some("blank".to_string()),
        workspace_root: None,
        created_at: "".to_string(),
        updated_at: "".to_string(),
    };
    storage.create_workspace(&ws).unwrap();

    // 1. Initial list should be empty (only shows header)
    let output = run_cli(&["mcp", "list"], db_path);
    assert_eq!(output.status.code(), Some(0));
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(!stdout.contains("my-mock-server"));

    // 2. Add an MCP server
    let output = run_cli(
        &[
            "mcp",
            "add",
            "my-mock-server",
            "--command",
            "python3 mcp.py",
        ],
        db_path,
    );
    assert_eq!(output.status.code(), Some(0));
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("Added MCP server"));

    // 3. List should now show the new server
    let output = run_cli(&["mcp", "list"], db_path);
    assert_eq!(output.status.code(), Some(0));
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("my-mock-server"));
    assert!(stdout.contains("stdio"));

    // 4. Remove the server
    let output = run_cli(&["mcp", "remove", "my-mock-server"], db_path);
    assert_eq!(output.status.code(), Some(0));
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("Removed MCP server"));

    // 5. List should be empty again
    let output = run_cli(&["mcp", "list"], db_path);
    assert_eq!(output.status.code(), Some(0));
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(!stdout.contains("my-mock-server"));
}
