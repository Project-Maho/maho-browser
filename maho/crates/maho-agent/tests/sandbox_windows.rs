#![cfg(target_os = "windows")]

use maho_agent::sandbox::windows::run_confined;
use maho_agent::sandbox::{SandboxBackend, SandboxError, SandboxPolicy};
use std::time::Duration;

fn policy_for(root: &std::path::Path) -> SandboxPolicy {
    SandboxPolicy {
        writable_roots: vec![root.to_path_buf()],
        readable_roots: Vec::new(),
        denied_paths: Vec::new(),
        network: false,
    }
}

fn env_lock() -> std::sync::MutexGuard<'static, ()> {
    static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
    LOCK.lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
}

#[test]
fn write_inside_root_succeeds_and_outside_is_denied() {
    let _lock = env_lock();
    let root = tempfile::tempdir().unwrap();
    let outside = tempfile::tempdir().unwrap();
    let inside_file = root.path().join("inside.txt");
    let outside_file = outside.path().join("outside.txt");
    let command = format!(
        "cmd.exe /c \"echo x> \"{}\" & echo x> \"{}\"\"",
        inside_file.display(),
        outside_file.display()
    );
    let exit = run_confined(
        &command,
        root.path(),
        &policy_for(root.path()),
        Duration::from_secs(20),
    )
    .unwrap();
    assert!(!exit.timed_out);
    assert!(
        inside_file.exists(),
        "write inside the writable root failed"
    );
    assert!(
        !outside_file.exists(),
        "sandbox wrote outside its writable root"
    );
}

#[test]
fn network_is_denied() {
    let _lock = env_lock();
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    listener.set_nonblocking(true).unwrap();
    let root = tempfile::tempdir().unwrap();
    let started = root.path().join("started.txt");
    let marker = root.path().join("connected.txt");
    // PowerShell cannot start inside an AppContainer (0xc0000142), so the
    // probe is cmd.exe + the system curl.exe.
    let command = format!(
        "cmd.exe /c \"echo started> \"{}\" & curl.exe -s -m 5 http://127.0.0.1:{port}/ && echo ok> \"{}\"\"",
        started.display(),
        marker.display()
    );
    let exit = run_confined(
        &command,
        root.path(),
        &policy_for(root.path()),
        Duration::from_secs(60),
    )
    .unwrap();
    assert!(!exit.timed_out);
    assert!(started.exists(), "confined process never ran: {exit:?}");
    assert!(!marker.exists(), "confined process reached the network");
    assert!(
        listener.accept().is_err(),
        "listener received a connection from the sandbox"
    );
}

#[test]
fn timeout_kills_the_whole_process_tree() {
    let _lock = env_lock();
    let root = tempfile::tempdir().unwrap();
    let late = root.path().join("late.txt");
    // Console tools such as choice/timeout/ping fail to start inside an
    // AppContainer, so both processes spin in cmd's built-in loop. The
    // grandchild writes late.txt only after its loop finishes; the job must
    // kill it at the 2 s timeout first.
    let spin = "for /L %i in (1,1,400000000) do @rem";
    let command = format!(
        "cmd.exe /c \"start /b cmd.exe /c \"{spin} & echo x> \"{}\"\" & {spin}\"",
        late.display()
    );
    let exit = run_confined(
        &command,
        root.path(),
        &policy_for(root.path()),
        Duration::from_secs(2),
    )
    .unwrap();
    assert!(exit.timed_out, "{exit:?}");
    std::thread::sleep(Duration::from_secs(8));
    assert!(
        !late.exists(),
        "a grandchild survived the job object after timeout"
    );
}

#[test]
fn network_enabled_policy_fails_closed() {
    let _lock = env_lock();
    let root = tempfile::tempdir().unwrap();
    let mut policy = policy_for(root.path());
    policy.network = true;
    let result = maho_agent::sandbox::windows::WindowsSandbox.prepare(&policy);
    assert!(matches!(result, Err(SandboxError::Unavailable(_))));
    let marker = root.path().join("ran.txt");
    let result = run_confined(
        &format!("cmd.exe /c \"echo x> \"{}\"\"", marker.display()),
        root.path(),
        &policy,
        Duration::from_secs(10),
    );
    assert!(result.is_err());
    assert!(!marker.exists());
}

#[cfg(feature = "debug-test-hooks")]
#[test]
fn forced_unavailable_spawns_nothing() {
    let _lock = env_lock();
    let root = tempfile::tempdir().unwrap();
    let marker = root.path().join("ran.txt");
    std::env::set_var("MAHO_SANDBOX_FORCE_UNAVAILABLE", "1");
    let result = run_confined(
        &format!("cmd.exe /c \"echo x> \"{}\"\"", marker.display()),
        root.path(),
        &policy_for(root.path()),
        Duration::from_secs(10),
    );
    std::env::remove_var("MAHO_SANDBOX_FORCE_UNAVAILABLE");
    assert!(matches!(result, Err(SandboxError::Unavailable(_))));
    assert!(!marker.exists());
}

fn cli_tool(command_template: &str) -> maho_types::ai::AiCliTool {
    maho_types::ai::AiCliTool {
        id: "t".into(),
        workspace_id: "w".into(),
        name: "probe".into(),
        description: String::new(),
        command_template: command_template.into(),
        parameters_schema: serde_json::json!({
            "type": "object",
            "properties": {"path": {"type": "string"}}
        }),
        sensitive: false,
        timeout_ms: 20_000,
        output_cap_bytes: 64 * 1024,
        working_directory: None,
        created_at: String::new(),
        updated_at: String::new(),
    }
}

#[test]
fn shell_exec_runs_inside_the_app_container_and_captures_output() {
    let _lock = env_lock();
    let workdir = tempfile::tempdir().unwrap();
    let outside = tempfile::tempdir().unwrap();
    let outside_file = outside.path().join("escape.txt");
    let runtime = tokio::runtime::Runtime::new().unwrap();

    // Output is captured from the confined process.
    let echo = maho_agent::cli_tool_executor::CliToolExecutor::new(
        // A real executable (cmd built-ins such as echo cannot be invoked
        // through the quoted program slot).
        cli_tool(r#"["cmd.exe", "/c", "echo", "confined-ok"]"#),
        Some(workdir.path().to_path_buf()),
    );
    let result = runtime.block_on(echo.execute(serde_json::json!({}))).unwrap();
    assert!(
        result["stdout"].as_str().unwrap().contains("confined-ok"),
        "{result}"
    );
    assert_eq!(result["exit_code"], 0);

    // A model-supplied argument cannot chain a second command through cmd.exe.
    let chained = maho_agent::cli_tool_executor::CliToolExecutor::new(
        cli_tool(r#"["cmd.exe", "/c", "echo", "{path}"]"#),
        Some(workdir.path().to_path_buf()),
    );
    let chain = format!("x& echo pwned> {}", outside_file.display());
    let result = runtime.block_on(chained.execute(serde_json::json!({"path": chain})));
    assert!(result.is_err(), "{result:?}");
    assert!(!outside_file.exists());

    // A tool that copies a file outside the working directory runs, but the
    // container cannot write there.
    let copy = maho_agent::cli_tool_executor::CliToolExecutor::new(
        cli_tool(r#"["cmd.exe", "/c", "copy", "/y", "nul", "{path}"]"#),
        Some(workdir.path().to_path_buf()),
    );
    let result = runtime
        .block_on(copy.execute(serde_json::json!({"path": outside_file.to_string_lossy()})))
        .unwrap();
    assert_ne!(result["exit_code"], 0, "{result}");
    assert!(!outside_file.exists(), "confined shell_exec wrote outside its root");

    // The same copy into the working directory succeeds.
    let inside_file = workdir.path().join("inside.txt");
    let result = runtime
        .block_on(copy.execute(serde_json::json!({"path": inside_file.to_string_lossy()})))
        .unwrap();
    assert_eq!(result["exit_code"], 0, "{result}");
    assert!(inside_file.exists());
}
