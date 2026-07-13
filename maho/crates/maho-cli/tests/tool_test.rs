use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::UnixListener;
use std::process::Command;
use std::sync::{Arc, Mutex};

fn run_cli(args: &[&str]) -> std::process::Output {
    let bin = env!("CARGO_BIN_EXE_maho");
    let mut cmd = Command::new(bin);
    cmd.args(args);
    // Point to non-existent UDS socket to guarantee no browser session is active
    cmd.arg("--socket-path")
        .arg("/tmp/nonexistent-maho-socket-path-12345");
    cmd.output().expect("failed to run CLI binary")
}

/// Run the CLI against an explicit socket path (used with a live mock).
fn run_cli_with_socket(args: &[&str], socket: &str) -> std::process::Output {
    let bin = env!("CARGO_BIN_EXE_maho");
    let mut cmd = Command::new(bin);
    cmd.args(args);
    cmd.arg("--socket-path").arg(socket);
    cmd.output().expect("failed to run CLI binary")
}

/// A minimal, REACHABLE mock of the browser MCP UDS server.
///
/// It completes the `initialize` handshake (so `BrowserClient::connect`
/// succeeds), and records the `name` of every `tools/call` it receives into a
/// shared vector. This lets a test assert whether the CLI actually forwarded a
/// tool invocation to the browser or rejected it before connecting.
struct MockBrowser {
    _dir: tempfile::TempDir,
    socket_path: std::path::PathBuf,
    tool_calls: Arc<Mutex<Vec<String>>>,
    _handle: std::thread::JoinHandle<()>,
}

impl MockBrowser {
    fn spawn() -> Self {
        let dir = tempfile::tempdir().expect("tempdir");
        let socket_path = dir.path().join("mock-browser.sock");
        let listener = UnixListener::bind(&socket_path).expect("bind mock UDS");

        let tool_calls: Arc<Mutex<Vec<String>>> = Arc::new(Mutex::new(Vec::new()));
        let tool_calls_thread = Arc::clone(&tool_calls);

        let handle = std::thread::spawn(move || {
            // Accept connections until the listener is dropped.
            for conn in listener.incoming() {
                let stream = match conn {
                    Ok(s) => s,
                    Err(_) => break,
                };
                let calls = Arc::clone(&tool_calls_thread);
                // Handle each connection inline (one-shot CLI uses a single one).
                let mut writer = stream.try_clone().expect("clone stream");
                let mut reader = BufReader::new(stream);
                let mut line = String::new();
                loop {
                    line.clear();
                    match reader.read_line(&mut line) {
                        Ok(0) => break, // peer closed
                        Ok(_) => {}
                        Err(_) => break,
                    }
                    let req: serde_json::Value = match serde_json::from_str(line.trim()) {
                        Ok(v) => v,
                        Err(_) => continue,
                    };
                    let id = req.get("id").cloned().unwrap_or(serde_json::Value::Null);
                    let method = req.get("method").and_then(|m| m.as_str()).unwrap_or("");
                    let resp = match method {
                        "initialize" => serde_json::json!({
                            "jsonrpc": "2.0",
                            "id": id,
                            "result": {
                                "protocolVersion": "2025-03-26",
                                "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                                "capabilities": {"tools": {}}
                            }
                        }),
                        "tools/call" => {
                            if let Some(name) = req
                                .get("params")
                                .and_then(|p| p.get("name"))
                                .and_then(|n| n.as_str())
                            {
                                calls.lock().unwrap().push(name.to_string());
                            }
                            serde_json::json!({
                                "jsonrpc": "2.0",
                                "id": id,
                                "result": {
                                    "content": [{"type": "text", "text": "{\"ok\":true}"}]
                                }
                            })
                        }
                        _ => serde_json::json!({
                            "jsonrpc": "2.0",
                            "id": id,
                            "result": {}
                        }),
                    };
                    let mut buf = serde_json::to_vec(&resp).expect("serialize resp");
                    buf.push(b'\n');
                    if writer.write_all(&buf).is_err() {
                        break;
                    }
                    let _ = writer.flush();
                }
            }
        });

        MockBrowser {
            _dir: dir,
            socket_path,
            tool_calls,
            _handle: handle,
        }
    }

    fn socket(&self) -> &str {
        self.socket_path.to_str().expect("utf8 socket path")
    }

    fn recorded_calls(&self) -> Vec<String> {
        self.tool_calls.lock().unwrap().clone()
    }
}

#[test]
fn test_tool_call_stateful_without_session() {
    // A session-binding tool is rejected pre-execution even with no browser
    // reachable (dead socket): the rejection does not depend on connectivity.
    let output = run_cli(&["tool", "call", "browser_acquire_lease", "--args", "{}"]);
    if output.status.code() != Some(2) {
        println!("STDOUT: {}", String::from_utf8_lossy(&output.stdout));
        println!("STDERR: {}", String::from_utf8_lossy(&output.stderr));
    }
    assert_eq!(output.status.code(), Some(2));
}

#[test]
fn test_tool_call_stateless_without_session() {
    let output = run_cli(&["tool", "call", "browser_ping", "--args", "{}"]);
    if output.status.code() != Some(1) {
        println!("STDOUT: {}", String::from_utf8_lossy(&output.stdout));
        println!("STDERR: {}", String::from_utf8_lossy(&output.stderr));
    }
    assert_eq!(output.status.code(), Some(1));
}

/// The core defect: a one-shot stateful `tool call` must be rejected BEFORE any
/// execution — even when the browser is fully reachable — so it never orphans
/// browser-side session state (an acquired lease, an in-flight capture).
///
/// On the buggy code this FAILS: the CLI connects to the reachable mock and
/// forwards `browser_acquire_lease`, so `recorded_calls()` is non-empty.
#[test]
fn stateful_oneshot_rejected_even_when_browser_reachable() {
    let mock = MockBrowser::spawn();
    // Give the listener thread a beat to start accepting.
    std::thread::sleep(std::time::Duration::from_millis(50));

    let output = run_cli_with_socket(
        &["tool", "call", "browser_acquire_lease", "--args", "{}"],
        mock.socket(),
    );

    let stderr = String::from_utf8_lossy(&output.stderr);
    // Rejected before execution: non-zero exit with a helpful message.
    assert_eq!(
        output.status.code(),
        Some(2),
        "expected exit code 2 (pre-execution rejection); stderr: {stderr}"
    );
    assert!(
        stderr.contains("browser_acquire_lease"),
        "error message should name the rejected tool; stderr: {stderr}"
    );

    // Most important: the tool must NOT have been forwarded to the browser.
    let calls = mock.recorded_calls();
    assert!(
        calls.is_empty(),
        "stateful tool must be rejected before execution; mock received calls: {calls:?}"
    );
}

/// A stateless one-shot tool call still works end-to-end against a reachable
/// browser: it connects and forwards the call.
#[test]
fn stateless_oneshot_forwarded_when_browser_reachable() {
    let mock = MockBrowser::spawn();
    std::thread::sleep(std::time::Duration::from_millis(50));

    let output = run_cli_with_socket(
        &["tool", "call", "browser_ping", "--args", "{}"],
        mock.socket(),
    );

    assert!(
        output.status.success(),
        "stateless call should succeed against a reachable mock; stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    let calls = mock.recorded_calls();
    assert_eq!(
        calls,
        vec!["browser_ping".to_string()],
        "stateless tool should be forwarded to the browser exactly once"
    );
}

/// The stateful set must be EXACTLY these 6 session-binding tool names, and the
/// bogus `browser_refresh_lease` (never a real tool) must be absent.
#[test]
fn stateful_list_is_exact_six() {
    let mut actual: Vec<&str> = maho_cli::STATEFUL_TOOLS.to_vec();
    actual.sort_unstable();

    let mut expected = vec![
        "browser_network_start_capture",
        "browser_network_stop_capture",
        "browser_network_get_har",
        "browser_acquire_lease",
        "browser_heartbeat_lease",
        "browser_release_lease",
    ];
    expected.sort_unstable();

    assert_eq!(
        actual, expected,
        "stateful set must be exactly these 6 names"
    );
    assert!(
        !maho_cli::STATEFUL_TOOLS.contains(&"browser_refresh_lease"),
        "bogus 'browser_refresh_lease' must not be in the stateful set"
    );
}
