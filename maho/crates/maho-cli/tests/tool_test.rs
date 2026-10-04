use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::UnixListener;
use std::process::Command;
use std::sync::{Arc, Mutex};

fn catalog_discovery() -> serde_json::Value {
    catalog_discovery_with_session_tools(&[])
}

/// Discovery as sent to a trusted Maho CLI session: catalog capabilities plus
/// session-scoped tools that carry no `capabilityId`.
fn catalog_discovery_with_session_tools(session_tools: &[&str]) -> serde_json::Value {
    let mut discovery = catalog_discovery_base();
    let tools = discovery["tools"].as_array_mut().expect("tools array");
    for name in session_tools {
        tools.push(serde_json::json!({
            "name": name,
            "description": "Session-scoped tool without a catalog capability.",
            "inputSchema": {"type": "object", "properties": {}}
        }));
    }
    discovery
}

fn catalog_discovery_base() -> serde_json::Value {
    serde_json::json!({
        "catalogDiagnostics": {
            "catalogVersion": 1,
            "schemaVersion": 1,
            "resultVersion": 1,
            "canonicalCount": 75,
            "gates": {"mailBeta": true, "routines": true, "vault": true},
            "surfaces": {"publicMcp": {"count": 1, "ids": ["page.content"]}},
            "intentionalExclusions": ["control_plane_authority"]
        },
        "tools": [{
            "capabilityId": "page.content",
            "name": "browser_page_content",
            "description": "Get redacted page content.",
            "inputSchema": {"type": "object", "properties": {}},
            "schemaVersion": 1,
            "resultVersion": 1,
            "policy": {"mutability": "read_only"}
        }]
    })
}

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
        Self::spawn_with_discovery(catalog_discovery())
    }

    fn spawn_with_discovery(discovery: serde_json::Value) -> Self {
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
                let discovery = discovery.clone();
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
                        "tools/list" => serde_json::json!({
                            "jsonrpc": "2.0",
                            "id": id,
                            "result": discovery
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
    let output = run_cli(&["tool", "run", "browser_acquire_lease", "--args", "{}"]);
    if output.status.code() != Some(2) {
        println!("STDOUT: {}", String::from_utf8_lossy(&output.stdout));
        println!("STDERR: {}", String::from_utf8_lossy(&output.stderr));
    }
    assert_eq!(output.status.code(), Some(2));
}

#[test]
fn test_tool_call_stateless_without_session() {
    let output = run_cli(&["tool", "run", "page.content", "--args", "{}"]);
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

    let output = run_cli_with_socket(
        &["tool", "run", "browser_acquire_lease", "--args", "{}"],
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

    let output = run_cli_with_socket(
        &["tool", "run", "page.content", "--args", "{}"],
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
        vec!["browser_page_content".to_string()],
        "stateless tool should be forwarded to the browser exactly once"
    );
}

/// A trusted CLI session's `tools/list` also carries session-scoped tools
/// without a `capabilityId` (e.g. `maho_agent_delegate`). They are not catalog
/// capabilities, so they must not trip the diagnostics count check.
#[test]
fn stateless_oneshot_forwarded_when_trusted_session_adds_session_tools() {
    let mock = MockBrowser::spawn_with_discovery(catalog_discovery_with_session_tools(&[
        "maho_agent_delegate",
    ]));

    let output = run_cli_with_socket(
        &["tool", "run", "page.content", "--args", "{}"],
        mock.socket(),
    );

    assert!(
        output.status.success(),
        "session-scoped tools must not fail discovery validation; stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(
        mock.recorded_calls(),
        vec!["browser_page_content".to_string()],
        "stateless tool should be forwarded to the browser exactly once"
    );
}

/// The stateful set must be EXACTLY these 6 session-binding tool names, and the
/// bogus `browser_refresh_lease` (never a real tool) must be absent.
#[test]
fn tool_list_json_uses_packaged_catalog_without_browser() {
    let output = run_cli(&["--json", "tool", "list"]);
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let value: serde_json::Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(value["source"], "packaged_snapshot");
    assert_eq!(value["runtimeAvailable"], false);
    assert_eq!(value["catalogDiagnostics"]["catalogVersion"], 1);
    assert_eq!(value["catalogDiagnostics"]["schemaVersion"], 1);
    assert_eq!(value["catalogDiagnostics"]["resultVersion"], 1);
    assert_eq!(value["catalogDiagnostics"]["canonicalCount"], 92);
    let tools = value["tools"].as_array().unwrap();
    let tool_ids = tools
        .iter()
        .map(|tool| tool["capabilityId"].as_str().unwrap())
        .collect::<Vec<_>>();
    assert_eq!(tool_ids.len(), 58);
    assert_eq!(tool_ids[0], "tab.list");
    assert_eq!(tool_ids[57], "browser.request_help");

    let diagnostic_ids = value["catalogDiagnostics"]["surfaces"]["publicMcp"]["ids"]
        .as_array()
        .unwrap()
        .iter()
        .map(|id| id.as_str().unwrap())
        .collect::<Vec<_>>();
    let mut sorted_tool_ids = tool_ids.clone();
    sorted_tool_ids.sort_unstable();
    assert_eq!(diagnostic_ids, sorted_tool_ids);
    assert_eq!(
        value["catalogDiagnostics"]["surfaces"]["publicMcp"]["count"],
        tool_ids.len()
    );
    assert!(tools.iter().all(|tool| tool.get("surface").is_none()));
    assert!(value.to_string().find("maho/control").is_none());
    let public_names = tools
        .iter()
        .filter_map(|tool| tool["name"].as_str())
        .collect::<std::collections::HashSet<_>>();
    for control_only in [
        "browser_acquire_lease",
        "mail_add_account",
        "mail_delete_account",
        "mail_start_oauth",
        "mail_import_migration_archive",
        "vault_request_credential_use",
        "vault_fill_credential",
        "vault_fill_totp",
    ] {
        assert!(!public_names.contains(control_only));
    }
}

#[test]
fn packaged_snapshot_tracks_every_canonical_count_and_id() {
    let snapshot: serde_json::Value =
        serde_json::from_str(include_str!("../src/tool_catalog_snapshot.json")).unwrap();
    let canonical_source = std::fs::read_to_string(
        std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../../maho-chromium/browser/ai/maho_browser_capability_catalog.def"),
    )
    .unwrap();
    let mut canonical_ids = canonical_source
        .lines()
        .filter(|line| {
            line.starts_with("MAHO_BROWSER_CAPABILITY(")
                || line.starts_with("MAHO_BROWSER_ACTION_CAPABILITY(")
        })
        .map(|line| {
            line.split('"')
                .nth(1)
                .expect("canonical catalog row must contain an ID")
        })
        .collect::<std::collections::BTreeSet<_>>();
    canonical_ids.insert("input.locator_click");
    canonical_ids.insert("input.locator_type");

    assert_eq!(
        snapshot["catalogDiagnostics"]["canonicalCount"],
        canonical_ids.len()
    );
    let projected_ids = snapshot["catalogDiagnostics"]["surfaces"]
        .as_object()
        .unwrap()
        .values()
        .flat_map(|surface| surface["ids"].as_array().unwrap())
        .map(|id| id.as_str().unwrap())
        .collect::<std::collections::BTreeSet<_>>();
    assert_eq!(projected_ids, canonical_ids);
}

#[test]
fn tool_describe_resolves_packaged_canonical_id_offline() {
    let output = run_cli(&["--json", "tool", "describe", "page.content"]);
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let value: serde_json::Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(value["source"], "packaged_snapshot");
    assert_eq!(value["runtimeAvailable"], false);
    assert_eq!(value["tool"]["capabilityId"], "page.content");
}

#[test]
fn tool_run_json_returns_typed_offline_error() {
    let output = run_cli(&["--json", "tool", "run", "page.content"]);
    assert_eq!(output.status.code(), Some(1));
    assert!(output.stdout.is_empty());
    let value: serde_json::Value = serde_json::from_slice(&output.stderr).unwrap();
    assert_eq!(value["ok"], false);
    assert_eq!(value["error"]["status"], "runtime_unavailable");
    assert_eq!(value["error"]["data"]["capabilityId"], "page.content");
    assert_eq!(value["error"]["data"]["runtimeAvailable"], false);
}

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

// ─── C4-DEFECT-1: --json must be honored on error paths ──────────────────────

/// The global `--json` flag promises "Output structured JSON for all
/// commands", but the capability-resolution error path bypassed it entirely:
/// stdout was EMPTY and the only diagnostic was a human sentence on stderr.
/// A machine consumer piping stdout got unparseable nothing and no stable
/// reason code. With --json the error must be a structured object on stdout
/// carrying a stable reason field plus the message; exit codes are unchanged.
#[test]
fn json_flag_emits_structured_error_on_unknown_capability() {
    // `tool describe` resolves against the packaged catalog, so the
    // unknown-capability path is reachable with no browser running.
    // `tool run` needs a reachable browser to get past connect, so it is
    // exercised against the mock below.
    let mock = MockBrowser::spawn();
    let describe = run_cli(&["--json", "tool", "describe", "nonexistent.capability.id"]);
    let run = run_cli_with_socket(
        &["--json", "tool", "run", "nonexistent.capability.id"],
        mock.socket(),
    );

    for (verb, output) in [("describe", describe), ("run", run)] {
        let stdout = String::from_utf8_lossy(&output.stdout);

        assert_eq!(
            output.status.code(),
            Some(1),
            "`--json tool {verb}` must keep its exit code; stdout: {stdout}"
        );

        let parsed: serde_json::Value = serde_json::from_str(stdout.trim()).unwrap_or_else(|e| {
            panic!("`--json tool {verb}` must emit parseable JSON on stdout, got {stdout:?}: {e}")
        });

        assert_eq!(parsed["ok"], serde_json::json!(false));
        assert_eq!(
            parsed["error"]["reason"], "unknown_capability",
            "structured error must carry a stable reason token, got: {parsed}"
        );
        assert!(
            parsed["error"]["message"]
                .as_str()
                .is_some_and(|m| m.contains("nonexistent.capability.id")),
            "structured error must carry the human message, got: {parsed}"
        );
    }
}

/// Without --json the human path is unchanged: the sentence stays on stderr
/// and stdout stays empty, so existing terminal usage and scripts that read
/// stderr keep working.
#[test]
fn human_error_path_is_unchanged_without_json_flag() {
    let output = run_cli(&["tool", "describe", "nonexistent.capability.id"]);
    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);

    assert_eq!(output.status.code(), Some(1));
    assert!(
        stdout.trim().is_empty(),
        "non-json stdout must stay empty, got: {stdout}"
    );
    assert!(
        stderr.contains("nonexistent.capability.id"),
        "non-json error must stay on stderr, got: {stderr}"
    );
}
