#![cfg(target_os = "linux")]

use maho_agent::cli_tool_executor::CliToolExecutor;
use maho_types::ai::AiCliTool;
use std::path::PathBuf;

fn shell_tool(command: &str) -> AiCliTool {
    AiCliTool {
        id: "sandbox-test".into(),
        workspace_id: "sandbox-test".into(),
        name: "shell_exec".into(),
        description: String::new(),
        command_template: serde_json::json!(["/bin/sh", "-c", command]).to_string(),
        parameters_schema: serde_json::json!({"type":"object"}),
        sensitive: true,
        timeout_ms: 10_000,
        output_cap_bytes: 4096,
        working_directory: None,
        created_at: String::new(),
        updated_at: String::new(),
    }
}

fn executor(command: &str, cwd: PathBuf) -> CliToolExecutor {
    CliToolExecutor::new(shell_tool(command), Some(cwd))
}

#[tokio::test]
async fn writable_cwd_is_available_but_ssh_path_is_denied() {
    let _env_lock = crate_env_lock();
    let temp = tempfile::tempdir().unwrap();
    let output = temp.path().join("created");
    let result = executor(
        &format!("printf ok > '{}'", output.display()),
        temp.path().to_path_buf(),
    )
    .execute(serde_json::json!({}))
    .await
    .unwrap();
    assert_eq!(result["exit_code"], 0);
    assert_eq!(std::fs::read_to_string(output).unwrap(), "ok");
    let home = PathBuf::from(std::env::var_os("HOME").unwrap());
    let ssh = home.join(".ssh");
    if ssh.is_dir() {
        let probe = ssh.join(format!("maho-sandbox-probe-{}", std::process::id()));
        let result = executor(
            &format!("printf x > '{}' 2>/dev/null", probe.display()),
            temp.path().to_path_buf(),
        )
        .execute(serde_json::json!({}))
        .await
        .unwrap();
        assert_ne!(result["exit_code"], 0);
        assert!(!probe.exists(), "sandbox wrote into ~/.ssh");
    }
}

#[tokio::test]
async fn network_socket_denial_does_not_block_non_network_syscalls() {
    let _env_lock = crate_env_lock();
    let temp = tempfile::tempdir().unwrap();
    let result = executor(
        "python3 -c 'import socket; socket.socket()' 2>&1",
        temp.path().to_path_buf(),
    )
    .execute(serde_json::json!({}))
    .await
    .unwrap();
    // The command redirects stderr into stdout (2>&1).
    assert_ne!(result["exit_code"], 0);
    assert!(
        result["stdout"]
            .as_str()
            .unwrap()
            .contains("Permission denied"),
        "socket() must fail with EACCES: {result}"
    );
    let result = executor("printf ok", temp.path().to_path_buf())
        .execute(serde_json::json!({}))
        .await
        .unwrap();
    assert_eq!(result["exit_code"], 0);
    assert_eq!(result["stdout"], "ok");
}

#[tokio::test]
async fn child_process_inherits_filesystem_and_network_restrictions() {
    let _env_lock = crate_env_lock();
    let temp = tempfile::tempdir().unwrap();
    let output = temp.path().join("child-write");
    let result = executor(
        &format!(
            "sh -c \"printf x > '{}' 2>/dev/null; python3 -c 'import socket; socket.socket()' 2>&1\"",
            output.display()
        ),
        temp.path().to_path_buf(),
    )
    .execute(serde_json::json!({}))
    .await
    .unwrap();
    // The grandchild's socket() is denied, so the command (whose last step
    // is that python call) exits non-zero; its filesystem write still lands.
    assert_ne!(result["exit_code"], 0);
    assert_eq!(
        std::fs::read_to_string(&output).unwrap_or_default(),
        "x",
        "grandchild write inside the writable root failed: {result}"
    );
    assert!(result["stdout"]
        .as_str()
        .unwrap()
        .contains("Permission denied"));
}

#[tokio::test]
async fn dev_null_is_writable_but_other_devices_are_not() {
    let _env_lock = crate_env_lock();
    let temp = tempfile::tempdir().unwrap();
    let result = executor("printf x > /dev/null", temp.path().to_path_buf())
        .execute(serde_json::json!({}))
        .await
        .unwrap();
    assert_eq!(result["exit_code"], 0, "{result}");
    let result = executor(
        "printf x > /dev/shm/maho-sandbox-probe",
        temp.path().to_path_buf(),
    )
    .execute(serde_json::json!({}))
    .await
    .unwrap();
    assert_ne!(result["exit_code"], 0, "{result}");
    assert!(!std::path::Path::new("/dev/shm/maho-sandbox-probe").exists());
}

#[tokio::test]
async fn symlink_escape_from_writable_root_is_denied() {
    let _env_lock = crate_env_lock();
    let temp = tempfile::tempdir().unwrap();
    let outside = tempfile::tempdir().unwrap();
    let escape = temp.path().join("escape");
    std::os::unix::fs::symlink(outside.path(), &escape).unwrap();
    let target = outside.path().join("written");
    let result = executor(
        &format!("printf x > '{}/written' 2>/dev/null", escape.display()),
        temp.path().to_path_buf(),
    )
    .execute(serde_json::json!({}))
    .await
    .unwrap();
    assert_ne!(result["exit_code"], 0);
    assert!(
        !target.exists(),
        "symlink escaped the writable Landlock root"
    );
}

#[cfg(feature = "debug-test-hooks")]
#[tokio::test]
async fn forced_prepare_failure_returns_before_command_spawns() {
    let temp = tempfile::tempdir().unwrap();
    let marker = temp.path().join("spawned");
    let _env_lock = crate_env_lock();
    std::env::set_var("MAHO_SANDBOX_FORCE_UNAVAILABLE", "1");
    let result = executor(
        &format!("printf x > '{}'", marker.display()),
        temp.path().to_path_buf(),
    )
    .execute(serde_json::json!({}))
    .await;
    std::env::remove_var("MAHO_SANDBOX_FORCE_UNAVAILABLE");
    let error = result.unwrap_err().to_string();
    assert!(error.contains("SandboxUnavailable"), "{error}");
    assert!(
        !marker.exists(),
        "command spawned after sandbox prepare failed"
    );
}

/// Serializes tests in this binary: the forced-unavailable test sets a
/// process-wide environment variable that every sandbox prepare reads.
fn crate_env_lock() -> std::sync::MutexGuard<'static, ()> {
    static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
    LOCK.lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
}
