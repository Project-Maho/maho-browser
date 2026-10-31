// Copyright 2026 The Maho Authors. All rights reserved.

//! Tests verifying the browser-side V2 locator path and legacy fallback behavior.

use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::UnixListener;
use std::process::Command;
use std::sync::{Arc, Mutex};

/// Run the CLI with an explicit socket path and optional environment variables.
fn run_cli_with_env(args: &[&str], socket: &str, envs: &[(&str, &str)]) -> std::process::Output {
    let bin = env!("CARGO_BIN_EXE_maho");
    let mut cmd = Command::new(bin);
    cmd.args(args);
    cmd.arg("--socket-path").arg(socket);
    for (k, v) in envs {
        cmd.env(k, v);
    }
    cmd.output().expect("failed to run CLI binary")
}

/// Catalog payload advertising V2 capabilities (input.locator_click, input.locator_type, page.accessibility_snapshot_v2).
fn v2_catalog_discovery() -> serde_json::Value {
    serde_json::json!({
        "catalogDiagnostics": {
            "catalogVersion": 1,
            "schemaVersion": 1,
            "resultVersion": 1,
            "canonicalCount": 86,
            "gates": {"mailBeta": true, "routines": true, "vault": true},
            "surfaces": {
                "publicMcp": {
                    "count": 5,
                    "ids": [
                        "input.click",
                        "input.locator_click",
                        "input.locator_type",
                        "input.type",
                        "page.accessibility_snapshot_v2"
                    ]
                }
            },
            "intentionalExclusions": ["control_plane_authority"]
        },
        "tools": [
            {
                "capabilityId": "input.click",
                "name": "browser_click",
                "description": "Click an element by accessibility ref.",
                "inputSchema": {"type": "object", "properties": {}},
                "schemaVersion": 1,
                "resultVersion": 1,
                "policy": {"mutability": "mutable"}
            },
            {
                "capabilityId": "input.type",
                "name": "browser_type",
                "description": "Type text into element.",
                "inputSchema": {"type": "object", "properties": {}},
                "schemaVersion": 1,
                "resultVersion": 1,
                "policy": {"mutability": "mutable"}
            },
            {
                "capabilityId": "input.locator_click",
                "name": "input.locator_click",
                "description": "Click an element by locator.",
                "inputSchema": {"type": "object", "properties": {}},
                "schemaVersion": 1,
                "resultVersion": 1,
                "policy": {"mutability": "mutable"}
            },
            {
                "capabilityId": "input.locator_type",
                "name": "input.locator_type",
                "description": "Type into an element by locator.",
                "inputSchema": {"type": "object", "properties": {}},
                "schemaVersion": 1,
                "resultVersion": 1,
                "policy": {"mutability": "mutable"}
            },
            {
                "capabilityId": "page.accessibility_snapshot_v2",
                "name": "page_accessibility_snapshot_v2",
                "description": "Compact accessibility snapshot.",
                "inputSchema": {"type": "object", "properties": {}},
                "schemaVersion": 1,
                "resultVersion": 1,
                "policy": {"mutability": "read_only"}
            }
        ]
    })
}

/// Catalog payload for a legacy browser server (only V1 tools).
fn legacy_catalog_discovery() -> serde_json::Value {
    serde_json::json!({
        "catalogDiagnostics": {
            "catalogVersion": 1,
            "schemaVersion": 1,
            "resultVersion": 1,
            "canonicalCount": 75,
            "gates": {"mailBeta": true, "routines": true, "vault": true},
            "surfaces": {
                "publicMcp": {
                    "count": 2,
                    "ids": ["input.click", "input.type"]
                }
            },
            "intentionalExclusions": ["control_plane_authority"]
        },
        "tools": [
            {
                "capabilityId": "input.click",
                "name": "browser_click",
                "description": "Click an element by accessibility ref.",
                "inputSchema": {"type": "object", "properties": {}},
                "schemaVersion": 1,
                "resultVersion": 1,
                "policy": {"mutability": "mutable"}
            },
            {
                "capabilityId": "input.type",
                "name": "browser_type",
                "description": "Type text into element.",
                "inputSchema": {"type": "object", "properties": {}},
                "schemaVersion": 1,
                "resultVersion": 1,
                "policy": {"mutability": "mutable"}
            }
        ]
    })
}

#[derive(Debug, Clone)]
struct RecordedCall {
    method: String,
    tool_name: Option<String>,
    arguments: Option<serde_json::Value>,
}

struct MockBrowserServer {
    _dir: tempfile::TempDir,
    socket_path: std::path::PathBuf,
    recorded_calls: Arc<Mutex<Vec<RecordedCall>>>,
    _handle: std::thread::JoinHandle<()>,
}

impl MockBrowserServer {
    fn spawn(is_v2: bool) -> Self {
        Self::spawn_with_match_count(is_v2, 1)
    }

    fn spawn_with_match_count(is_v2: bool, match_count: i64) -> Self {
        let dir = tempfile::tempdir().expect("tempdir");
        let socket_path = dir.path().join("mock-browser.sock");
        let listener = UnixListener::bind(&socket_path).expect("bind mock UDS");

        let recorded_calls: Arc<Mutex<Vec<RecordedCall>>> = Arc::new(Mutex::new(Vec::new()));
        let calls_thread = Arc::clone(&recorded_calls);

        let handle = std::thread::spawn(move || {
            for conn in listener.incoming() {
                let stream = match conn {
                    Ok(s) => s,
                    Err(_) => break,
                };
                let calls = Arc::clone(&calls_thread);
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
                    let method = req
                        .get("method")
                        .and_then(|m| m.as_str())
                        .unwrap_or("")
                        .to_string();

                    let tool_name = req
                        .get("params")
                        .and_then(|p| p.get("name"))
                        .and_then(|n| n.as_str())
                        .map(str::to_string);
                    let arguments = req.get("params").and_then(|p| p.get("arguments")).cloned();

                    calls.lock().unwrap().push(RecordedCall {
                        method: method.clone(),
                        tool_name: tool_name.clone(),
                        arguments: arguments.clone(),
                    });

                    let resp = match method.as_str() {
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
                            "result": if is_v2 { v2_catalog_discovery() } else { legacy_catalog_discovery() }
                        }),
                        "maho/control/call" => {
                            serde_json::json!({
                                "jsonrpc": "2.0",
                                "id": id,
                                "result": {
                                    "status": "granted",
                                    "lease_id": "mock_lease_1"
                                }
                            })
                        }
                        "tools/call" => {
                            let name = tool_name.as_deref().unwrap_or("");
                            let result_content = match name {
                                "input.locator_click" => serde_json::json!({
                                    "action": {"dispatched": true, "verified": true, "method": "trusted_input"},
                                    "wait": {"reason": "dom_stable", "elapsed_ms": 5},
                                    "observation": {"snapshot_token": "snap_v2", "diff": ""}
                                }),
                                "input.locator_type" => serde_json::json!({
                                    "action": {"dispatched": true, "verified": true, "method": "trusted_input"},
                                    "wait": {"reason": "dom_stable", "elapsed_ms": 5},
                                    "observation": {"snapshot_token": "snap_v2", "diff": ""}
                                }),
                                "browser_tab_list" => serde_json::json!({
                                    "tabs": [
                                        {"id": 1, "is_active": true, "title": "Test Tab", "url": "https://example.test"}
                                    ]
                                }),
                                "page_query_selector" => serde_json::json!({
                                    "ref_id": "dom_node_1",
                                    "tag": "button",
                                    "match_count": match_count
                                }),
                                "page_get_text" => serde_json::json!({
                                    "text": "Submit"
                                }),
                                "page_get_attribute" => {
                                    let attr = arguments
                                        .as_ref()
                                        .and_then(|a| a.get("attribute"))
                                        .and_then(|v| v.as_str())
                                        .unwrap_or("");
                                    let val = if attr == "id" { "submit-button" } else { "" };
                                    serde_json::json!({
                                        "attribute": attr,
                                        "value": val
                                    })
                                }
                                "browser_accessibility_snapshot" => serde_json::json!({
                                    "ref": 42,
                                    "role": "button",
                                    "name": "Submit",
                                    "elementId": "submit-button",
                                    "children": []
                                }),
                                "browser_click" => serde_json::json!({
                                    "ok": true
                                }),
                                "browser_type" => serde_json::json!({
                                    "ok": true
                                }),
                                "browser_key_press" => serde_json::json!({
                                    "ok": true
                                }),
                                _ => serde_json::json!({"ok": true}),
                            };
                            serde_json::json!({
                                "jsonrpc": "2.0",
                                "id": id,
                                "result": {
                                    "content": [{"type": "text", "text": serde_json::to_string(&result_content).unwrap()}]
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

        MockBrowserServer {
            _dir: dir,
            socket_path,
            recorded_calls,
            _handle: handle,
        }
    }

    fn socket(&self) -> &str {
        self.socket_path.to_str().expect("utf8 socket path")
    }

    fn calls(&self) -> Vec<RecordedCall> {
        self.recorded_calls.lock().unwrap().clone()
    }
}

/// Parse lines of JSON perf traces emitted on stderr when MAHO_BROWSER_PERF_TRACE=1.
fn parse_perf_traces(stderr: &[u8]) -> Vec<serde_json::Value> {
    let text = String::from_utf8_lossy(stderr);
    text.lines()
        .filter_map(|line| serde_json::from_str::<serde_json::Value>(line.trim()).ok())
        .filter(|json| json.get("tool").is_some() && json.get("total_ms").is_some())
        .collect()
}

#[test]
fn test_v2_click_uses_direct_locator_and_no_ax_snapshot() {
    let mock = MockBrowserServer::spawn(true);

    let output = run_cli_with_env(
        &["click", "#submit"],
        mock.socket(),
        &[("MAHO_BROWSER_PERF_TRACE", "1")],
    );

    assert!(
        output.status.success(),
        "CLI failed with stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );

    let calls = mock.calls();
    let tool_call_names: Vec<String> = calls.iter().filter_map(|c| c.tool_name.clone()).collect();

    // 1. Direct locator call was dispatched
    assert!(
        tool_call_names.contains(&"input.locator_click".to_string()),
        "expected input.locator_click in tool calls, got: {tool_call_names:?}"
    );

    // 2. NO accessibility snapshot was requested
    assert!(
        !tool_call_names.contains(&"browser_accessibility_snapshot".to_string()),
        "V2 path must not take a browser_accessibility_snapshot"
    );

    // 3. NO DOM query or attribute fan-out occurred
    assert!(
        !tool_call_names.contains(&"page_query_selector".to_string()),
        "V2 path must not invoke page_query_selector"
    );
    assert!(
        !tool_call_names.contains(&"page_get_attribute".to_string()),
        "V2 path must not invoke page_get_attribute fan-out"
    );
    assert!(
        !tool_call_names.contains(&"page_get_text".to_string()),
        "V2 path must not invoke page_get_text"
    );

    // 4. Verify arguments sent to input.locator_click
    let locator_call = calls
        .iter()
        .find(|c| c.tool_name.as_deref() == Some("input.locator_click"))
        .expect("input.locator_click call must exist");
    let args = locator_call
        .arguments
        .as_ref()
        .expect("arguments must be present");
    assert_eq!(args["locator"]["css"], "#submit");
    assert_eq!(args["lease"], "scoped");

    // 5. Total RPC count via MAHO_BROWSER_PERF_TRACE is <= 4
    let perf_traces = parse_perf_traces(&output.stderr);
    assert!(
        !perf_traces.is_empty(),
        "perf traces must be emitted when MAHO_BROWSER_PERF_TRACE=1"
    );
    assert!(
        perf_traces.len() <= 4,
        "Total RPCs including initialize must be <= 4; observed: {}",
        perf_traces.len()
    );
}

#[test]
fn test_v2_type_submit_uses_direct_locator_type() {
    let mock = MockBrowserServer::spawn(true);

    let output = run_cli_with_env(
        &["type", "--submit", "hello world"],
        mock.socket(),
        &[("MAHO_BROWSER_PERF_TRACE", "1")],
    );

    assert!(
        output.status.success(),
        "CLI failed with stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );

    let calls = mock.calls();
    let tool_call_names: Vec<String> = calls.iter().filter_map(|c| c.tool_name.clone()).collect();

    assert!(
        tool_call_names.contains(&"input.locator_type".to_string()),
        "expected input.locator_type in tool calls, got: {tool_call_names:?}"
    );
    assert!(
        !tool_call_names.contains(&"browser_accessibility_snapshot".to_string()),
        "V2 path must not take a browser_accessibility_snapshot"
    );

    let type_call = calls
        .iter()
        .find(|c| c.tool_name.as_deref() == Some("input.locator_type"))
        .expect("input.locator_type call must exist");
    let args = type_call.arguments.as_ref().expect("arguments");
    assert_eq!(args["text"], "hello world");
    assert_eq!(args["submit"], true);
    assert_eq!(args["lease"], "scoped");
    assert_eq!(args["locator"]["css"], ":focus");

    let perf_traces = parse_perf_traces(&output.stderr);
    assert!(
        perf_traces.len() <= 4,
        "Total RPCs including initialize must be <= 4; observed: {}",
        perf_traces.len()
    );
}

#[test]
fn test_v2_click_ref_selector_passes_numeric_ref() {
    let mock = MockBrowserServer::spawn(true);

    let output = run_cli_with_env(
        &["click", "@42"],
        mock.socket(),
        &[("MAHO_BROWSER_PERF_TRACE", "1")],
    );

    assert!(
        output.status.success(),
        "CLI failed with stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );

    let calls = mock.calls();
    let locator_call = calls
        .iter()
        .find(|c| c.tool_name.as_deref() == Some("input.locator_click"))
        .expect("input.locator_click call must exist");
    let args = locator_call.arguments.as_ref().expect("arguments");
    assert_eq!(args["locator"]["ref"], 42);
    assert_eq!(args["lease"], "scoped");
}

#[test]
fn test_legacy_fallback_executes_full_fan_out() {
    let mock = MockBrowserServer::spawn(false); // Legacy stub (no V2 advertised)

    let output = run_cli_with_env(
        &["click", "#submit"],
        mock.socket(),
        &[("MAHO_BROWSER_PERF_TRACE", "1")],
    );

    assert!(
        output.status.success(),
        "CLI failed with stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );

    let calls = mock.calls();
    let tool_call_names: Vec<String> = calls.iter().filter_map(|c| c.tool_name.clone()).collect();

    // 1. Accessibility snapshot WAS requested on legacy path
    assert!(
        tool_call_names.contains(&"browser_accessibility_snapshot".to_string()),
        "Legacy path MUST take browser_accessibility_snapshot"
    );

    // 2. DOM query selector and attributes fan-out WAS performed
    assert!(
        tool_call_names.contains(&"page_query_selector".to_string()),
        "Legacy path MUST call page_query_selector"
    );
    assert!(
        tool_call_names.contains(&"page_get_attribute".to_string()),
        "Legacy path MUST call page_get_attribute"
    );
    assert!(
        tool_call_names.contains(&"page_get_text".to_string()),
        "Legacy path MUST call page_get_text"
    );

    // 3. Lease control calls were issued
    let control_calls: Vec<String> = calls
        .iter()
        .filter(|c| c.method == "maho/control/call")
        .filter_map(|c| c.tool_name.clone())
        .collect();
    assert!(
        control_calls.contains(&"browser_acquire_lease".to_string()),
        "Legacy path MUST acquire lease"
    );
    assert!(
        control_calls.contains(&"browser_release_lease".to_string()),
        "Legacy path MUST release lease"
    );

    // 4. Final browser_click was dispatched
    assert!(
        tool_call_names.contains(&"browser_click".to_string()),
        "Legacy path MUST dispatch browser_click"
    );

    // 5. Total RPC traces on legacy path reflect fan-out
    let perf_traces = parse_perf_traces(&output.stderr);
    assert!(
        perf_traces.len() > 4,
        "Legacy path should have fan-out RPCs (> 4), observed: {}",
        perf_traces.len()
    );
}

#[test]
fn test_legacy_selector_rejects_multiple_dom_matches_before_dispatch() {
    let mock = MockBrowserServer::spawn_with_match_count(false, 2);
    let output = run_cli_with_env(&["click", "button"], mock.socket(), &[]);
    assert!(!output.status.success());
    assert!(String::from_utf8_lossy(&output.stderr).contains("multiple DOM elements"));
    let names: Vec<_> = mock.calls().into_iter().filter_map(|call| call.tool_name).collect();
    assert!(names.contains(&"page_query_selector".to_string()));
    assert!(!names.contains(&"browser_click".to_string()));
}
