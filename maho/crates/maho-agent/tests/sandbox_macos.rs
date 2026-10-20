#![cfg(target_os = "macos")]

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

async fn run(command: &str, cwd: &std::path::Path) -> serde_json::Value {
    executor(command, cwd.to_path_buf())
        .execute(serde_json::json!({}))
        .await
        .unwrap()
}

#[tokio::test]
async fn writes_inside_the_working_directory_succeed() {
    let _env_lock = crate_env_lock();
    let temp = tempfile::tempdir().unwrap();
    let output = temp.path().join("x");
    let result = run(&format!("echo ok > '{}'", output.display()), temp.path()).await;
    assert_eq!(result["exit_code"], 0, "{result}");
    assert_eq!(std::fs::read_to_string(output).unwrap(), "ok\n");
}

#[tokio::test]
async fn writes_into_library_are_denied() {
    let _env_lock = crate_env_lock();
    let temp = tempfile::tempdir().unwrap();
    let home = PathBuf::from(std::env::var_os("HOME").unwrap());
    let probe = home
        .join("Library")
        .join(format!("maho-sandbox-probe-{}", std::process::id()));
    let result = run(
        &format!("touch '{}' 2>/dev/null", probe.display()),
        temp.path(),
    )
    .await;
    assert_ne!(result["exit_code"], 0, "{result}");
    assert!(!probe.exists(), "sandbox wrote into ~/Library");
}

#[tokio::test]
async fn writes_outside_the_working_directory_are_denied() {
    let _env_lock = crate_env_lock();
    let temp = tempfile::tempdir().unwrap();
    let outside = tempfile::tempdir().unwrap();
    let target = outside.path().join("escaped");
    let result = run(
        &format!("echo x > '{}' 2>/dev/null", target.display()),
        temp.path(),
    )
    .await;
    assert_ne!(result["exit_code"], 0, "{result}");
    assert!(!target.exists(), "sandbox wrote outside its writable root");
}

#[tokio::test]
async fn network_is_denied_when_the_policy_is_closed() {
    let _env_lock = crate_env_lock();
    let temp = tempfile::tempdir().unwrap();
    let result = run(
        "/usr/bin/curl -sS --max-time 5 -o /dev/null http://1.1.1.1/ 2>&1",
        temp.path(),
    )
    .await;
    assert_ne!(
        result["exit_code"], 0,
        "network request succeeded: {result}"
    );
}

#[tokio::test]
async fn ssh_directory_is_unreadable() {
    let _env_lock = crate_env_lock();
    let home = PathBuf::from(std::env::var_os("HOME").unwrap());
    let ssh = home.join(".ssh");
    if !ssh.is_dir() {
        return;
    }
    let temp = tempfile::tempdir().unwrap();
    let result = run(&format!("ls '{}' 2>&1", ssh.display()), temp.path()).await;
    assert_ne!(result["exit_code"], 0, "~/.ssh was readable: {result}");
}

#[tokio::test]
async fn child_processes_inherit_the_sandbox() {
    let _env_lock = crate_env_lock();
    let temp = tempfile::tempdir().unwrap();
    let outside = tempfile::tempdir().unwrap();
    let inside = temp.path().join("child");
    let escaped = outside.path().join("grandchild");
    let result = run(
        &format!(
            "sh -c \"echo x > '{}'; echo y > '{}' 2>/dev/null\"",
            inside.display(),
            escaped.display()
        ),
        temp.path(),
    )
    .await;
    assert_ne!(result["exit_code"], 0, "{result}");
    assert_eq!(std::fs::read_to_string(&inside).unwrap_or_default(), "x\n");
    assert!(!escaped.exists(), "a grandchild escaped the sandbox");
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

fn crate_env_lock() -> std::sync::MutexGuard<'static, ()> {
    static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
    LOCK.lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
}
