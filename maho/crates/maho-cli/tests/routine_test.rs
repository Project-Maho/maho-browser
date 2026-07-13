use std::collections::HashMap;
use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::UnixListener;
use std::process::Command;

/// A canned response for a single `tools/call` name on the mock browser.
#[derive(Clone)]
enum ToolResponse {
    /// A successful result. `text` is placed verbatim into the MCP
    /// `content[0].text` field (the CLI parses it as JSON when possible).
    Ok { text: String },
    /// A JSON-RPC error response (e.g. the browser's tier-lock signal).
    Err { code: i32, message: String },
}

/// A minimal, REACHABLE mock of the browser MCP UDS server.
///
/// It completes the `initialize` handshake so `BrowserClient::connect` succeeds,
/// then answers each `tools/call` from a per-tool-name response table. This lets
/// a test prove the CLI sources routines from the RUNNING BROWSER (round-trip via
/// `browser_routines_list` / `browser_routines_run`) rather than a private local
/// SQLite DB.
struct MockBrowser {
    _dir: tempfile::TempDir,
    socket_path: std::path::PathBuf,
    _handle: std::thread::JoinHandle<()>,
}

impl MockBrowser {
    fn spawn(responses: HashMap<String, ToolResponse>) -> Self {
        let dir = tempfile::tempdir().expect("tempdir");
        let socket_path = dir.path().join("mock-browser.sock");
        let listener = UnixListener::bind(&socket_path).expect("bind mock UDS");

        let handle = std::thread::spawn(move || {
            for conn in listener.incoming() {
                let stream = match conn {
                    Ok(s) => s,
                    Err(_) => break,
                };
                let responses = responses.clone();
                let mut writer = stream.try_clone().expect("clone stream");
                let mut reader = BufReader::new(stream);
                let mut line = String::new();
                loop {
                    line.clear();
                    match reader.read_line(&mut line) {
                        Ok(0) => break,
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
                            let name = req
                                .get("params")
                                .and_then(|p| p.get("name"))
                                .and_then(|n| n.as_str())
                                .unwrap_or("");
                            match responses.get(name) {
                                Some(ToolResponse::Ok { text }) => serde_json::json!({
                                    "jsonrpc": "2.0",
                                    "id": id,
                                    "result": {
                                        "content": [{"type": "text", "text": text}]
                                    }
                                }),
                                Some(ToolResponse::Err { code, message }) => serde_json::json!({
                                    "jsonrpc": "2.0",
                                    "id": id,
                                    "error": {"code": code, "message": message}
                                }),
                                None => serde_json::json!({
                                    "jsonrpc": "2.0",
                                    "id": id,
                                    "error": {"code": -32601, "message": "unknown tool"}
                                }),
                            }
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
            _handle: handle,
        }
    }

    fn socket(&self) -> &str {
        self.socket_path.to_str().expect("utf8 socket path")
    }
}

fn run_cli_with_socket(args: &[&str], socket: &str) -> std::process::Output {
    let bin = env!("CARGO_BIN_EXE_maho");
    let mut cmd = Command::new(bin);
    cmd.args(args);
    cmd.arg("--socket-path").arg(socket);
    cmd.output().expect("failed to run CLI binary")
}

/// The core defect guard: `maho routine list` must reflect the routine set that
/// the RUNNING BROWSER exposes over MCP (`browser_routines_list`), NOT a private
/// `SqliteStorage(default_db_path)` opened in-process by the CLI.
///
/// The mock browser returns a custom routine (`webui_custom_routine`) that does
/// not exist in any built-in table or the CLI's local default DB. If the CLI is
/// sourcing from the browser, that id appears in the output. On the buggy path
/// (local DB), it never can.
#[test]
fn cli_routine_reflects_browser_not_local_db() {
    let mut responses = HashMap::new();
    responses.insert(
        "browser_routines_list".to_string(),
        ToolResponse::Ok {
            text: serde_json::json!({
                "routines": [
                    {
                        "id": "webui_custom_routine",
                        "name": "WebUI Custom Routine",
                        "description": "Created in the browser WebUI, absent from the CLI local DB."
                    }
                ]
            })
            .to_string(),
        },
    );
    let mock = MockBrowser::spawn(responses);
    std::thread::sleep(std::time::Duration::from_millis(50));

    let output = run_cli_with_socket(&["routine", "list"], mock.socket());
    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);

    assert!(
        output.status.success(),
        "routine list should succeed against a reachable browser; stderr: {stderr}"
    );
    assert!(
        stdout.contains("webui_custom_routine"),
        "routine list must reflect the BROWSER's routine set (round-trip via \
         browser_routines_list), not a private local DB; stdout: {stdout}"
    );
}

/// `routine list --json` renders exactly what the browser returned.
#[test]
fn cli_routine_list_json_from_browser() {
    let mut responses = HashMap::new();
    responses.insert(
        "browser_routines_list".to_string(),
        ToolResponse::Ok {
            text: serde_json::json!({
                "routines": [
                    {"id": "morning_briefing", "name": "Morning Briefing", "description": "d"}
                ]
            })
            .to_string(),
        },
    );
    let mock = MockBrowser::spawn(responses);
    std::thread::sleep(std::time::Duration::from_millis(50));

    let output = run_cli_with_socket(&["--json", "routine", "list"], mock.socket());
    assert!(output.status.success());
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("morning_briefing"));
}

/// Tier-lock clean error, routed through the browser: running a Max-locked
/// routine surfaces the browser's structured tier-lock JSON-RPC error as a
/// clean, actionable CLI error with exit code 2 — never a panic or raw failure.
#[test]
fn cli_routine_run_tier_locked_clean_error() {
    let mut responses = HashMap::new();
    responses.insert(
        "browser_routines_run".to_string(),
        ToolResponse::Err {
            code: -32000,
            message: "Routines are available on Max tier only".to_string(),
        },
    );
    let mock = MockBrowser::spawn(responses);
    std::thread::sleep(std::time::Duration::from_millis(50));

    let output = run_cli_with_socket(&["routine", "run", "morning_briefing"], mock.socket());
    let stderr = String::from_utf8_lossy(&output.stderr);

    assert_eq!(
        output.status.code(),
        Some(2),
        "tier-locked routine must exit with code 2; stderr: {stderr}"
    );
    assert!(
        stderr.contains("available on Max tier only"),
        "tier-lock error must be surfaced cleanly; stderr: {stderr}"
    );
    // A panic would print a backtrace / "panicked at" to stderr.
    assert!(
        !stderr.contains("panicked"),
        "must not panic on tier-lock; stderr: {stderr}"
    );
}

/// A not-found routine from the browser surfaces cleanly with a non-2 exit code
/// (distinct from the tier gate) and names the failure.
#[test]
fn cli_routine_run_not_found_clean_error() {
    let mut responses = HashMap::new();
    responses.insert(
        "browser_routines_run".to_string(),
        ToolResponse::Err {
            code: -32001,
            message: "No routine found with id: ghost_routine_1234".to_string(),
        },
    );
    let mock = MockBrowser::spawn(responses);
    std::thread::sleep(std::time::Duration::from_millis(50));

    let output = run_cli_with_socket(&["routine", "run", "ghost_routine_1234"], mock.socket());
    let stderr = String::from_utf8_lossy(&output.stderr);

    assert_ne!(
        output.status.code(),
        Some(2),
        "not-found must not use the tier-gate exit code; stderr: {stderr}"
    );
    assert_ne!(output.status.code(), Some(0), "not-found must be non-zero");
    assert!(
        stderr.contains("No routine found"),
        "not-found error must name the failure; stderr: {stderr}"
    );
}

/// A successful routine run renders the browser engine's result content.
#[test]
fn cli_routine_run_success_renders_browser_result() {
    let mut responses = HashMap::new();
    responses.insert(
        "browser_routines_run".to_string(),
        ToolResponse::Ok {
            text: serde_json::json!({
                "id": "morning_briefing",
                "content": "Here is your morning briefing."
            })
            .to_string(),
        },
    );
    let mock = MockBrowser::spawn(responses);
    std::thread::sleep(std::time::Duration::from_millis(50));

    let output = run_cli_with_socket(&["routine", "run", "morning_briefing"], mock.socket());
    assert!(
        output.status.success(),
        "successful run should exit 0; stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("Here is your morning briefing."));
}
