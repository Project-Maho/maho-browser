// Copyright 2026 The Maho Authors. All rights reserved.

use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use maho_browser_mcp::client::BrowserClient;
use maho_browser_mcp::protocol::ControllerKind;
use maho_cli::browser_pipe::{self, PipeResponse};
use serde_json::{json, Value};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixListener;

type MockHandler = Box<dyn Fn(Value) -> Result<Value, (i64, String, Option<Value>)> + Send + Sync>;

async fn spawn_mock_server(
    handler: Arc<Mutex<MockHandler>>,
) -> (tempfile::TempDir, PathBuf, tokio::task::JoinHandle<()>) {
    let dir = tempfile::tempdir().expect("failed to create tempdir");
    let sock_path = dir.path().join("mock_browser.sock");
    let listener = UnixListener::bind(&sock_path).expect("failed to bind socket");

    let handle = tokio::spawn(async move {
        let (stream, _) = listener.accept().await.expect("accept failed");
        let (reader, mut writer) = stream.into_split();
        let mut reader = BufReader::new(reader);
        let mut line = String::new();

        // 1. Read initialize request
        reader.read_line(&mut line).await.expect("read init failed");
        let req: Value = serde_json::from_str(&line).expect("parse init failed");
        assert_eq!(req["method"], "initialize");
        let id = req["id"].as_i64().unwrap_or(1);
        let init_resp = json!({
            "jsonrpc": "2.0",
            "id": id,
            "result": {
                "protocolVersion": "2025-03-26",
                "serverInfo": { "name": "maho-browser", "version": "0.1.0" },
                "capabilities": { "tools": {} }
            }
        });
        let mut buf = serde_json::to_vec(&init_resp).unwrap();
        buf.push(b'\n');
        writer.write_all(&buf).await.expect("write init failed");

        // 2. Serve subsequent JSON-RPC requests
        loop {
            line.clear();
            match reader.read_line(&mut line).await {
                Ok(0) => break, // EOF
                Ok(_) => {
                    let trimmed = line.trim();
                    if trimmed.is_empty() {
                        continue;
                    }
                    let req_val: Value = match serde_json::from_str(trimmed) {
                        Ok(v) => v,
                        Err(_) => continue,
                    };
                    let req_id = req_val.get("id").cloned().unwrap_or(Value::Null);

                    let res = {
                        let lock = handler.lock().unwrap();
                        (lock)(req_val)
                    };

                    let resp = match res {
                        Ok(val) => json!({
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": val,
                        }),
                        Err((code, msg, data)) => {
                            let mut err_obj = json!({
                                "code": code,
                                "message": msg,
                            });
                            if let Some(d) = data {
                                err_obj["data"] = d;
                            }
                            json!({
                                "jsonrpc": "2.0",
                                "id": req_id,
                                "error": err_obj,
                            })
                        }
                    };

                    let mut resp_buf = serde_json::to_vec(&resp).unwrap();
                    resp_buf.push(b'\n');
                    let _ = writer.write_all(&resp_buf).await;
                    let _ = writer.flush().await;
                }
                Err(_) => break,
            }
        }
    });

    (dir, sock_path, handle)
}

/// Exercise the shipped NDJSON entry point, not just its in-process dispatcher.
#[tokio::test(flavor = "multi_thread")]
async fn test_pipe_act_forwards_exact_args_and_preserves_rpc_denials() {
    let captured = Arc::new(Mutex::new(Vec::new()));
    let calls = Arc::clone(&captured);
    let handler: MockHandler = Box::new(move |req| {
        assert_eq!(req["method"], "tools/call");
        let params = req["params"].clone();
        calls.lock().unwrap().push(params.clone());
        match params["name"].as_str().unwrap() {
            "browser.act_and_observe" => {
                // Mirror the executing session handler: only object kind=type
                // selects typing; all other shapes dispatch a click.
                let args = &params["arguments"];
                let action = &args["action"];
                let locator = args
                    .get("locator")
                    .or_else(|| action.get("locator"))
                    .unwrap();
                assert!(locator.is_object());
                let kind = if action["kind"] == "type" {
                    "type"
                } else {
                    "click"
                };
                let text = args.get("text").or_else(|| action.get("text"));
                if let Some(code) = params["arguments"]["tab_id"].as_i64().filter(|id| *id < 0) {
                    return Err((
                        code,
                        "denied".into(),
                        Some(json!({
                            "hint": "request_approval", "reason": "test_denial", "redacted": true
                        })),
                    ));
                }
                Ok(json!({"structuredContent": {
                    "action": {"dispatched": true, "verified": true},
                    "fixture_receipt": {"kind": kind, "text": text, "locator": locator},
                    "observation": {"snapshot_token": "act-token"}
                }}))
            }
            "page.accessibility_snapshot_v2" => {
                if params["arguments"]["tab_id"] == -32602 {
                    return Err((-32602, "invalid".into(), None));
                }
                Ok(json!({"snapshot_token": "observe-token"}))
            }
            other => panic!("Unexpected tool call: {other}"),
        }
    });
    let (_dir, socket, server) = spawn_mock_server(Arc::new(Mutex::new(handler))).await;
    let exact_args = json!({
        "action": {"kind": "type", "locator": {"css": "#query"}, "text": "fixture text"},
        "wait": {"mode": "auto", "timeout_ms": 2000}, "observe": "diff", "tab_id": 7,
        "since_snapshot_token": "explicit-token", "lease": "scoped"
    });
    let mut first = exact_args.clone();
    first["id"] = json!("act-explicit");
    first["op"] = json!("act");
    first["consent"] = json!(true);
    first["tool"] = json!("must_not_forward");
    let click_args = json!({"action": {"kind": "click"}, "tab_id": 7, "locator": {"ref": 2}});
    let mut click = click_args.clone();
    click["id"] = json!(2);
    click["op"] = json!("act");
    let mut requests = vec![first, click];
    for code in [-32007, -32008, -32009, -32011] {
        requests.push(
            json!({"id": code, "op": "act", "action": {"kind": "click"}, "tab_id": code,
                             "locator": {"ref": 2}}),
        );
    }
    requests.extend([
        json!({"id": "after-denials", "op": "observe"}),
        json!({"id": "old-rpc", "op": "observe", "tab_id": -32602}),
        json!({"id": "missing", "op": "act"}),
        json!({"id": "no-tab-binding", "op": "act", "action": {"kind": "click"}, "locator": {"ref": 2}}),
        json!({"id": "native", "op": "act", "action": "native_key"}),
        json!({"id": "recovered", "op": "observe"}),
    ]);
    for (index, args) in [
        json!({"action": "type", "locator": {"ref": 2}, "text": "must not click"}),
        json!({"action": {"kind": "native_key"}, "locator": {"ref": 2}}),
        json!({"action": {"kind": "observe"}, "locator": {"ref": 2}}),
        json!({"action": {"kind": "navigate"}, "locator": {"ref": 2}}),
        json!({"action": {"kind": "click"}, "tab_id": 7}),
        json!({"action": {"kind": "click"}, "tab_id": 7, "locator": {"ref": "@e2"}}),
        json!({"action": {"kind": "click"}, "tab_id": 7, "locator": {"ref": 2}, "wait": "auto"}),
    ]
    .into_iter()
    .enumerate()
    {
        let mut request = args;
        request["id"] = json!(format!("invalid-{index}"));
        request["op"] = json!("act");
        requests.push(request);
    }
    let input = requests
        .iter()
        .map(Value::to_string)
        .collect::<Vec<_>>()
        .join("\n")
        + "\n";
    let mut child = tokio::process::Command::new(env!("CARGO_BIN_EXE_maho"))
        .args(["--socket-path", socket.to_str().unwrap(), "browser", "pipe"])
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped())
        .kill_on_drop(true)
        .spawn()
        .expect("spawn CLI");
    let output = tokio::time::timeout(Duration::from_secs(15), async {
        let mut stdin = child.stdin.take().unwrap();
        stdin.write_all(input.as_bytes()).await.unwrap();
        drop(stdin);
        child.wait_with_output().await.unwrap()
    })
    .await
    .expect("CLI completion");
    tokio::time::timeout(Duration::from_secs(5), server)
        .await
        .expect("server EOF")
        .expect("server task");
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let responses: Vec<Value> = String::from_utf8(output.stdout)
        .unwrap()
        .lines()
        .map(|line| serde_json::from_str(line).unwrap())
        .collect();
    assert_eq!(responses.len(), requests.len());
    for (response, request) in responses.iter().zip(&requests) {
        assert_eq!(response["id"], request["id"]);
    }
    assert_eq!(responses[0]["ok"], true, "{}", responses[0]);
    // This flag is forwarded dispatch evidence, not a verified page postcondition.
    assert_eq!(
        responses[0]["result"]["action"],
        json!({"dispatched": true, "verified": true})
    );
    assert_eq!(responses[1]["ok"], true);
    assert_eq!(
        responses[0]["result"]["fixture_receipt"],
        json!({
            "kind": "type", "text": "fixture text", "locator": {"css": "#query"}
        })
    );
    assert_eq!(
        responses[1]["result"]["fixture_receipt"],
        json!({
            "kind": "click", "text": null, "locator": {"ref": 2}
        })
    );
    for response in &responses[13..] {
        assert_eq!(response["error"]["code"], "invalid_params", "{response}");
        assert!(response["error"].get("rpc_code").is_none());
    }
    for (response, code) in responses[2..6].iter().zip([-32007, -32008, -32009, -32011]) {
        assert_eq!(response["ok"], false);
        assert_eq!(
            response["error"],
            json!({
                "code": "rpc_error", "message": "denied", "hint": "request_approval",
                "rpc_code": code, "rpc_data": {
                    "hint": "request_approval", "reason": "test_denial", "redacted": true
                }
            })
        );
    }
    assert_eq!(responses[6]["ok"], true);
    assert_eq!(
        responses[7]["error"],
        json!({"code": "invalid_params", "message": "invalid", "rpc_code": -32602})
    );
    // The act without tab_id is rejected client-side: no server call, and the
    // error names the missing binding.
    let no_binding = &responses[9];
    assert_eq!(no_binding["error"]["code"], "invalid_params", "{no_binding}");
    assert!(no_binding["error"]["message"]
        .as_str()
        .unwrap()
        .contains("tab_id"), "{no_binding}");
    assert!(no_binding["error"].get("rpc_data").is_none());
    assert_eq!(responses[10]["error"]["code"], "invalid_params");
    assert!(responses[10]["error"].get("rpc_code").is_none());
    assert!(responses[10]["error"].get("rpc_data").is_none());
    assert_eq!(responses[11]["ok"], true);
    let calls = captured.lock().unwrap();
    assert_eq!(
        calls.len(),
        9,
        "no implicit grants, lease calls, or retries"
    );
    assert_eq!(
        calls[0],
        json!({"name": "browser.act_and_observe", "arguments": exact_args})
    );
    assert_eq!(
        calls[1],
        json!({"name": "browser.act_and_observe", "arguments": click_args})
    );
    for (call, code) in calls[2..6].iter().zip([-32007, -32008, -32009, -32011]) {
        assert_eq!(
            call,
            &json!({"name": "browser.act_and_observe", "arguments": {
                "action": {"kind": "click"}, "tab_id": code, "locator": {"ref": 2}
            }})
        );
    }
    assert_eq!(
        calls[6]["arguments"],
        json!({"since_snapshot_token": "act-token"})
    );
    assert_eq!(
        calls[8]["arguments"],
        json!({"since_snapshot_token": "observe-token"})
    );
}

/// Exercise the shipped NDJSON entry point for `op: "native"`:
/// - Verifies click, type, key actions forwarded to input.native_action
/// - Verifies RPC error code/data preservation (-32007, -32008, -32009, -32011)
/// - Verifies invalid parameter rejection (missing frame_token, rect bounds, etc.)
/// - Verifies snapshot token chaining
#[tokio::test(flavor = "multi_thread")]
async fn test_pipe_native_forwards_exact_args_and_preserves_rpc_denials() {
    let captured = Arc::new(Mutex::new(Vec::new()));
    let calls = Arc::clone(&captured);
    let handler: MockHandler = Box::new(move |req| {
        assert_eq!(req["method"], "tools/call");
        let params = req["params"].clone();
        calls.lock().unwrap().push(params.clone());
        match params["name"].as_str().unwrap() {
            "input.native_action" => {
                let args = &params["arguments"];
                let action = &args["action"];
                let kind = action["kind"].as_str().unwrap();
                if let Some(code) = args
                    .get("tab_id")
                    .and_then(Value::as_i64)
                    .filter(|id| *id < 0)
                {
                    let mut data = json!({
                        "hint": "request_approval",
                        "reason": "test_native_denial",
                        "redacted": true
                    });
                    if code == -32011 {
                        data["available"] = json!(false);
                        data["raw_os_status"] = json!(5);
                    }
                    return Err((code, "denied".into(), Some(data)));
                }
                Ok(json!({"structuredContent": {
                    "action": {"dispatched": true, "verified": true, "kind": kind},
                    "observation": {"snapshot_token": "native-token"}
                }}))
            }
            "page.accessibility_snapshot_v2" => Ok(json!({"snapshot_token": "observe-token"})),
            other => panic!("Unexpected tool call: {other}"),
        }
    });
    let (_dir, socket, server) = spawn_mock_server(Arc::new(Mutex::new(handler))).await;

    let click_req = json!({
        "id": "native-click",
        "op": "native",
        "frame_token": "frame-token-1",
        "tab_id": 7,
        "lease_epoch": 1,
        "action": {
            "kind": "click",
            "click_point_css": [120.0, 80.0],
            "target_rect_css": [100.0, 60.0, 50.0, 40.0],
            "motion_profile": "direct"
        }
    });

    let type_req = json!({
        "id": "native-type",
        "op": "native",
        "frame_token": "frame-token-1",
        "tab_id": 7,
        "lease_epoch": 1,
        "action": {
            "kind": "type",
            "text": "native keyboard input"
        }
    });

    let key_req = json!({
        "id": "native-key",
        "op": "native",
        "frame_token": "frame-token-1",
        "tab_id": 7,
        "lease_epoch": 1,
        "action": {
            "kind": "key",
            "key": "Enter",
            "modifiers": ["Control"]
        }
    });

    let mut requests = vec![click_req, type_req, key_req];

    // RPC denial codes: -32007, -32008, -32009, -32011
    for code in [-32007, -32008, -32009, -32011] {
        requests.push(json!({
            "id": code,
            "op": "native",
            "frame_token": "frame-token-1",
            "tab_id": code,
            "lease_epoch": 1,
            "action": {
                "kind": "click",
                "click_point_css": [120.0, 80.0],
                "target_rect_css": [100.0, 60.0, 50.0, 40.0]
            }
        }));
    }

    // Observe chaining
    requests.push(json!({"id": "after-native", "op": "observe"}));

    // Invalid requests (must fail before dispatch with invalid_params)
    for (index, bad) in [
        // Missing frame_token
        json!({"action": {"kind": "click", "click_point_css": [120.0, 80.0], "target_rect_css": [100.0, 60.0, 50.0, 40.0]}}),
        // Disallowed kind (generic tool op)
        json!({"frame_token": "f", "action": {"kind": "generic_tool"}}),
        // Target rect does not contain click point
        json!({"frame_token": "f", "action": {"kind": "click", "click_point_css": [500.0, 500.0], "target_rect_css": [0.0, 0.0, 50.0, 50.0]}}),
        // Target rect negative dimensions
        json!({"frame_token": "f", "action": {"kind": "click", "click_point_css": [10.0, 10.0], "target_rect_css": [0.0, 0.0, -50.0, 50.0]}}),
        // Non-finite click coords
        json!({"frame_token": "f", "action": {"kind": "click", "click_point_css": ["nan", 10.0], "target_rect_css": [0.0, 0.0, 50.0, 50.0]}}),
        // Empty text for type
        json!({"frame_token": "f", "action": {"kind": "type", "text": ""}}),
        // Empty key for key
        json!({"frame_token": "f", "action": {"kind": "key", "key": ""}}),
    ].into_iter().enumerate() {
        let mut request = bad;
        request["id"] = json!(format!("invalid-native-{index}"));
        request["op"] = json!("native");
        requests.push(request);
    }

    let input = requests
        .iter()
        .map(Value::to_string)
        .collect::<Vec<_>>()
        .join("\n")
        + "\n";

    let mut child = tokio::process::Command::new(env!("CARGO_BIN_EXE_maho"))
        .args(["--socket-path", socket.to_str().unwrap(), "browser", "pipe"])
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped())
        .kill_on_drop(true)
        .spawn()
        .expect("spawn CLI");

    let output = tokio::time::timeout(Duration::from_secs(15), async {
        let mut stdin = child.stdin.take().unwrap();
        stdin.write_all(input.as_bytes()).await.unwrap();
        drop(stdin);
        child.wait_with_output().await.unwrap()
    })
    .await
    .expect("CLI completion");

    tokio::time::timeout(Duration::from_secs(5), server)
        .await
        .expect("server EOF")
        .expect("server task");

    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );

    let responses: Vec<Value> = String::from_utf8(output.stdout)
        .unwrap()
        .lines()
        .map(|line| serde_json::from_str(line).unwrap())
        .collect();

    assert_eq!(responses.len(), requests.len());
    for (response, request) in responses.iter().zip(&requests) {
        assert_eq!(response["id"], request["id"]);
    }

    // 0: click success
    assert_eq!(responses[0]["ok"], true, "{}", responses[0]);
    assert_eq!(responses[0]["result"]["action"]["kind"], "click");

    // 1: type success
    assert_eq!(responses[1]["ok"], true, "{}", responses[1]);
    assert_eq!(responses[1]["result"]["action"]["kind"], "type");

    // 2: key success
    assert_eq!(responses[2]["ok"], true, "{}", responses[2]);
    assert_eq!(responses[2]["result"]["action"]["kind"], "key");

    // 3..7: denials with exact RPC codes
    for (response, code) in responses[3..7].iter().zip([-32007, -32008, -32009, -32011]) {
        assert_eq!(response["ok"], false);
        assert_eq!(response["error"]["code"], "rpc_error");
        assert_eq!(response["error"]["rpc_code"], code);
        assert!(response["error"]["rpc_data"].is_object());
        if code == -32011 {
            assert_eq!(response["error"]["rpc_data"]["available"], false);
            assert_eq!(response["error"]["rpc_data"]["raw_os_status"], 5);
        }
    }

    // 7: after-native observe chained snapshot token
    assert_eq!(responses[7]["ok"], true);

    // 8..: invalid parameter requests
    for response in &responses[8..] {
        assert_eq!(response["ok"], false);
        assert_eq!(response["error"]["code"], "invalid_params", "{response}");
        assert!(response["error"].get("rpc_code").is_none());
    }

    let calls_lock = captured.lock().unwrap();
    // 3 successful native + 4 denials + 1 observe = 8 calls reached the mock server
    assert_eq!(calls_lock.len(), 8);
    assert_eq!(calls_lock[0]["name"], "input.native_action");
    assert_eq!(
        calls_lock[7]["arguments"]["since_snapshot_token"],
        "native-token"
    );
}

/// (a) Three sequential ops correlate IDs correctly and chain snapshot tokens.
#[tokio::test(flavor = "multi_thread")]
async fn test_sequential_ops_correlate_ids_and_chain_snapshot_tokens() {
    let captured_calls = Arc::new(Mutex::new(Vec::new()));
    let calls_clone = Arc::clone(&captured_calls);

    let handler: MockHandler = Box::new(move |req: Value| {
        let params = req.get("params").cloned().unwrap_or(Value::Null);
        let tool_name = params
            .get("name")
            .and_then(Value::as_str)
            .unwrap_or("")
            .to_string();
        let arguments = params.get("arguments").cloned().unwrap_or(Value::Null);
        calls_clone
            .lock()
            .unwrap()
            .push((tool_name.clone(), arguments.clone()));

        match tool_name.as_str() {
            "page.accessibility_snapshot_v2" => {
                let since = arguments
                    .get("since_snapshot_token")
                    .and_then(Value::as_str);
                if since == Some("token-beta") {
                    Ok(json!({
                        "snapshot_token": "token-gamma",
                        "diff": "~ node updated"
                    }))
                } else {
                    Ok(json!({
                        "snapshot_token": "token-alpha",
                        "tree": "Root [ref=1]\n  Button 'Click me' [ref=2]"
                    }))
                }
            }
            "input.locator_click" => Ok(json!({
                "action": { "dispatched": true, "verified": true, "method": "click" },
                "observation": {
                    "snapshot_token": "token-beta",
                    "diff": "+ Node 'New' [ref=3]"
                }
            })),
            other => panic!("Unexpected tool call: {other}"),
        }
    });

    let (_dir, sock_path, server_handle) = spawn_mock_server(Arc::new(Mutex::new(handler))).await;
    let client =
        BrowserClient::connect_as(&sock_path, Duration::from_secs(5), ControllerKind::MahoCli)
            .await
            .expect("failed to connect");

    let stdin_data = [
        r#"{"id": "op-1", "op": "observe", "mode": "compact"}"#,
        r#"{"id": "op-2", "op": "click", "tab_id": 7, "locator": {"ref": 2}}"#,
        r#"{"id": "op-3", "op": "observe"}"#,
    ]
    .join("\n")
        + "\n";

    let mut output = Vec::new();
    browser_pipe::run_pipe_loop(stdin_data.as_bytes(), &mut output, &client)
        .await
        .expect("run_pipe_loop failed");

    server_handle.abort();

    let output_str = String::from_utf8(output).expect("valid utf8");
    let lines: Vec<&str> = output_str.lines().collect();
    assert_eq!(
        lines.len(),
        3,
        "Expected 3 response lines, got: {output_str}"
    );

    let resp1: PipeResponse = serde_json::from_str(lines[0]).expect("parse line 1");
    assert_eq!(resp1.id, json!("op-1"));
    assert!(resp1.ok);
    let res1 = resp1.result.expect("result 1");
    assert_eq!(res1["snapshot_token"], "token-alpha");

    let resp2: PipeResponse = serde_json::from_str(lines[1]).expect("parse line 2");
    assert_eq!(resp2.id, json!("op-2"));
    assert!(resp2.ok);
    let res2 = resp2.result.expect("result 2");
    assert_eq!(res2["observation"]["snapshot_token"], "token-beta");

    let resp3: PipeResponse = serde_json::from_str(lines[2]).expect("parse line 3");
    assert_eq!(resp3.id, json!("op-3"));
    assert!(resp3.ok);
    let res3 = resp3.result.expect("result 3");
    assert_eq!(res3["snapshot_token"], "token-gamma");

    // Verify chained calls: Op 3 sent since_snapshot_token: token-beta
    let calls = captured_calls.lock().unwrap();
    assert_eq!(calls.len(), 3);
    assert_eq!(calls[0].0, "page.accessibility_snapshot_v2");
    assert_eq!(calls[0].1.get("since_snapshot_token"), None);
    assert_eq!(calls[1].0, "input.locator_click");
    assert_eq!(calls[1].1["tab_id"], 7);
    assert_eq!(calls[1].1["locator"]["ref"], 2);
    assert_eq!(calls[1].1["observe"], "diff");
    assert_eq!(calls[2].0, "page.accessibility_snapshot_v2");
    assert_eq!(calls[2].1["since_snapshot_token"], "token-beta");
}

/// (b) One op returns a structured error while the session survives and the next op succeeds.
#[tokio::test(flavor = "multi_thread")]
async fn test_structured_error_survival_and_subsequent_op() {
    let handler: MockHandler = Box::new(|req: Value| {
        let params = req.get("params").cloned().unwrap_or(Value::Null);
        let tool_name = params
            .get("name")
            .and_then(Value::as_str)
            .unwrap_or("")
            .to_string();

        match tool_name.as_str() {
            "page.accessibility_snapshot_v2" => Ok(json!({
                "snapshot_token": "token-recovered",
                "tree": "Heading 'Home'"
            })),
            other => panic!("Unexpected tool call: {other}"),
        }
    });

    let (_dir, sock_path, server_handle) = spawn_mock_server(Arc::new(Mutex::new(handler))).await;
    let client =
        BrowserClient::connect_as(&sock_path, Duration::from_secs(5), ControllerKind::MahoCli)
            .await
            .expect("failed to connect");

    // Line 1: unknown op
    // Line 2: click missing locator
    // Line 3: valid observe
    let stdin_data = [
        r#"{"id": 101, "op": "bogus_op"}"#,
        r#"{"id": 102, "op": "click"}"#,
        r#"{"id": 103, "op": "observe", "mode": "full"}"#,
    ]
    .join("\n")
        + "\n";

    let mut output = Vec::new();
    browser_pipe::run_pipe_loop(stdin_data.as_bytes(), &mut output, &client)
        .await
        .expect("run_pipe_loop failed");

    server_handle.abort();

    let output_str = String::from_utf8(output).expect("valid utf8");
    let lines: Vec<&str> = output_str.lines().collect();
    assert_eq!(
        lines.len(),
        3,
        "Expected 3 response lines, got: {output_str}"
    );

    let resp1: PipeResponse = serde_json::from_str(lines[0]).expect("parse line 1");
    assert_eq!(resp1.id, json!(101));
    assert!(!resp1.ok);
    let err1 = resp1.error.expect("error 1");
    assert_eq!(err1.code, "unknown_op");

    let resp2: PipeResponse = serde_json::from_str(lines[1]).expect("parse line 2");
    assert_eq!(resp2.id, json!(102));
    assert!(!resp2.ok);
    let err2 = resp2.error.expect("error 2");
    assert_eq!(err2.code, "invalid_params");

    let resp3: PipeResponse = serde_json::from_str(lines[2]).expect("parse line 3");
    assert_eq!(resp3.id, json!(103));
    assert!(resp3.ok);
    let res3 = resp3.result.expect("result 3");
    assert_eq!(res3["snapshot_token"], "token-recovered");
}

/// (c) Malformed line yields bad_request and stream continues.
#[tokio::test(flavor = "multi_thread")]
async fn test_malformed_line_yields_bad_request_and_stream_continues() {
    let handler: MockHandler = Box::new(|req: Value| {
        let params = req.get("params").cloned().unwrap_or(Value::Null);
        let tool_name = params
            .get("name")
            .and_then(Value::as_str)
            .unwrap_or("")
            .to_string();

        match tool_name.as_str() {
            "page.accessibility_snapshot_v2" => Ok(json!({
                "snapshot_token": "token-c1",
                "tree": "Content"
            })),
            other => panic!("Unexpected tool call: {other}"),
        }
    });

    let (_dir, sock_path, server_handle) = spawn_mock_server(Arc::new(Mutex::new(handler))).await;
    let client =
        BrowserClient::connect_as(&sock_path, Duration::from_secs(5), ControllerKind::MahoCli)
            .await
            .expect("failed to connect");

    let stdin_data = [
        "not json at all {{{{",
        "12345",
        r#"{"id": 42, "op": "observe"}"#,
    ]
    .join("\n")
        + "\n";

    let mut output = Vec::new();
    browser_pipe::run_pipe_loop(stdin_data.as_bytes(), &mut output, &client)
        .await
        .expect("run_pipe_loop failed");

    server_handle.abort();

    let output_str = String::from_utf8(output).expect("valid utf8");
    let lines: Vec<&str> = output_str.lines().collect();
    assert_eq!(
        lines.len(),
        3,
        "Expected 3 response lines, got: {output_str}"
    );

    let resp1: PipeResponse = serde_json::from_str(lines[0]).expect("parse line 1");
    assert_eq!(resp1.id, Value::Null);
    assert!(!resp1.ok);
    assert_eq!(resp1.error.unwrap().code, "bad_request");

    let resp2: PipeResponse = serde_json::from_str(lines[1]).expect("parse line 2");
    assert_eq!(resp2.id, Value::Null);
    assert!(!resp2.ok);
    assert_eq!(resp2.error.unwrap().code, "bad_request");

    let resp3: PipeResponse = serde_json::from_str(lines[2]).expect("parse line 3");
    assert_eq!(resp3.id, json!(42));
    assert!(resp3.ok);
    assert_eq!(resp3.result.unwrap()["snapshot_token"], "token-c1");
}

/// Test type, navigate, wait ops through the pipe loop.
#[tokio::test(flavor = "multi_thread")]
async fn test_type_navigate_wait_ops() {
    let handler: MockHandler = Box::new(|req: Value| {
        let params = req.get("params").cloned().unwrap_or(Value::Null);
        let tool_name = params
            .get("name")
            .and_then(Value::as_str)
            .unwrap_or("")
            .to_string();

        match tool_name.as_str() {
            "input.locator_type" => Ok(json!({
                "action": { "dispatched": true, "verified": true, "method": "type" },
                "observation": { "snapshot_token": "token-type-1" }
            })),
            // Mirror the live session dispatch: navigate and wait_for_selector
            // are matched by MCP tool name only; their canonical ids return
            // -32601 "unknown tool" from a real browser.
            "browser_acquire_lease" => Ok(json!({ "acquired": true })),
            "browser_navigate" => Ok(json!({ "navigated": true })),
            "page_wait_for_selector" => Ok(json!({ "found": true })),
            other => panic!("Unexpected tool call: {other}"),
        }
    });

    let (_dir, sock_path, server_handle) = spawn_mock_server(Arc::new(Mutex::new(handler))).await;
    let client =
        BrowserClient::connect_as(&sock_path, Duration::from_secs(5), ControllerKind::MahoCli)
            .await
            .expect("failed to connect");

    let stdin_data = [
        r##"{"id": 1, "op": "type", "tab_id": 7, "locator": {"css": "input.query"}, "text": "search terms", "submit": true}"##,
        r##"{"id": 2, "op": "navigate", "tab_id": 7, "url": "https://example.com"}"##,
        r##"{"id": 3, "op": "wait", "selector": "#results", "timeout_ms": 2000}"##,
    ]
    .join("\n")
        + "\n";

    let mut output = Vec::new();
    browser_pipe::run_pipe_loop(stdin_data.as_bytes(), &mut output, &client)
        .await
        .expect("run_pipe_loop failed");

    server_handle.abort();

    let output_str = String::from_utf8(output).expect("valid utf8");
    let lines: Vec<&str> = output_str.lines().collect();
    assert_eq!(lines.len(), 3);

    let resp1: PipeResponse = serde_json::from_str(lines[0]).expect("parse line 1");
    assert_eq!(resp1.id, json!(1));
    assert!(resp1.ok);

    let resp2: PipeResponse = serde_json::from_str(lines[1]).expect("parse line 2");
    assert_eq!(resp2.id, json!(2));
    assert!(resp2.ok);

    let resp3: PipeResponse = serde_json::from_str(lines[2]).expect("parse line 3");
    assert_eq!(resp3.id, json!(3));
    assert!(resp3.ok);
    assert_eq!(resp3.result.unwrap()["found"], true);
}

// ─── D4: public `grant` pipe op (browser_grant_exact_origin) ─────────────────

#[tokio::test(flavor = "multi_thread")]
async fn test_pipe_grant_op_dispatches_grant_exact_origin() {
    let handler: MockHandler = Box::new(|req: Value| {
        // The browser exposes this capability on the control plane only
        // (catalog surface ControlPlane); a `tools/call` is rejected live
        // with -32601 "Capability is not available on the public MCP".
        assert_eq!(
            req["method"], "maho/control/call",
            "grant op must use the control plane, got: {}",
            req["method"]
        );
        let params = req.get("params").cloned().unwrap_or(Value::Null);
        let tool_name = params
            .get("name")
            .and_then(Value::as_str)
            .unwrap_or("")
            .to_string();
        assert_eq!(
            tool_name, "browser_grant_exact_origin",
            "grant op must call browser_grant_exact_origin, got: {tool_name}"
        );
        assert_eq!(
            params["arguments"]["origin"],
            "https://contest.aitestbed.kr"
        );
        Ok(json!({ "granted": true }))
    });

    let (_dir, sock_path, server_handle) = spawn_mock_server(Arc::new(Mutex::new(handler))).await;
    let client =
        BrowserClient::connect_as(&sock_path, Duration::from_secs(5), ControllerKind::MahoCli)
            .await
            .expect("failed to connect");

    let stdin_data =
        r#"{"id": 7, "op": "grant", "origin": "https://contest.aitestbed.kr"}"#.to_string() + "\n";

    let mut output = Vec::new();
    browser_pipe::run_pipe_loop(stdin_data.as_bytes(), &mut output, &client)
        .await
        .expect("run_pipe_loop failed");

    server_handle.abort();

    let output_str = String::from_utf8(output).expect("valid utf8");
    let resp: PipeResponse =
        serde_json::from_str(output_str.lines().next().unwrap()).expect("parse response");
    assert_eq!(resp.id, json!(7));
    assert!(resp.ok, "grant op must succeed: {output_str}");
}

#[tokio::test(flavor = "multi_thread")]
async fn test_pipe_grant_missing_origin_is_invalid_params() {
    let handler: MockHandler = Box::new(|_req: Value| Ok(json!({})));

    let (_dir, sock_path, server_handle) = spawn_mock_server(Arc::new(Mutex::new(handler))).await;
    let client =
        BrowserClient::connect_as(&sock_path, Duration::from_secs(5), ControllerKind::MahoCli)
            .await
            .expect("failed to connect");

    let stdin_data = r#"{"id": 8, "op": "grant"}"#.to_string() + "\n";

    let mut output = Vec::new();
    browser_pipe::run_pipe_loop(stdin_data.as_bytes(), &mut output, &client)
        .await
        .expect("run_pipe_loop failed");

    server_handle.abort();

    let output_str = String::from_utf8(output).expect("valid utf8");
    let resp: PipeResponse =
        serde_json::from_str(output_str.lines().next().unwrap()).expect("parse response");
    assert_eq!(resp.id, json!(8));
    assert!(!resp.ok);
    let err = resp.error.expect("missing origin must be an error");
    assert_eq!(err.code, "invalid_params");
}

// ─── D5: `upload` pipe op tests (pending chooser & selector string) ───────────

#[tokio::test(flavor = "multi_thread")]
async fn test_pipe_upload_dispatches_exact_requests() {
    let captured_calls = Arc::new(Mutex::new(Vec::new()));
    let calls_clone = Arc::clone(&captured_calls);

    let handler: MockHandler = Box::new(move |req: Value| {
        let params = req.get("params").cloned().unwrap_or(Value::Null);
        let tool_name = params
            .get("name")
            .and_then(Value::as_str)
            .unwrap_or("")
            .to_string();
        let arguments = params.get("arguments").cloned().unwrap_or(Value::Null);
        calls_clone
            .lock()
            .unwrap()
            .push((tool_name, arguments));
        Ok(json!({}))
    });

    let (_dir, sock_path, server_handle) = spawn_mock_server(Arc::new(Mutex::new(handler))).await;
    let client =
        BrowserClient::connect_as(&sock_path, Duration::from_secs(5), ControllerKind::MahoCli)
            .await
            .expect("failed to connect");

    let stdin_data = [
        r#"{"id": 1, "op": "upload", "tab_id": 7, "path": "/tmp/upload.png"}"#,
        r##"{"id": 2, "op": "upload", "tab_id": 7, "path": "/tmp/package.msix", "selector": "#upload-file"}"##,
    ]
    .join("\n")
        + "\n";

    let mut output = Vec::new();
    browser_pipe::run_pipe_loop(stdin_data.as_bytes(), &mut output, &client)
        .await
        .expect("run_pipe_loop failed");

    server_handle.abort();

    let output_str = String::from_utf8(output).expect("valid utf8");
    let responses: Vec<PipeResponse> = output_str
        .lines()
        .map(|line| serde_json::from_str(line).expect("parse response line"))
        .collect();

    assert_eq!(responses.len(), 2);
    assert_eq!(responses[0].id, json!(1));
    assert!(responses[0].ok);
    assert_eq!(responses[1].id, json!(2));
    assert!(responses[1].ok);

    let calls = captured_calls.lock().unwrap();
    assert_eq!(calls.len(), 4);

    assert_eq!(calls[0].0, "browser_acquire_lease");
    assert_eq!(calls[0].1, json!({ "tab_id": 7, "ttl_seconds": 60 }));
    assert_eq!(calls[1].0, "browser_file_upload_select");
    assert_eq!(
        calls[1].1,
        json!({ "tab_id": 7, "path": "/tmp/upload.png" })
    );

    assert_eq!(calls[2].0, "browser_acquire_lease");
    assert_eq!(calls[2].1, json!({ "tab_id": 7, "ttl_seconds": 60 }));
    assert_eq!(calls[3].0, "browser_file_upload_select");
    assert_eq!(
        calls[3].1,
        json!({ "tab_id": 7, "path": "/tmp/package.msix", "selector": "#upload-file" })
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn test_pipe_upload_does_not_dispatch_when_lease_acquisition_fails() {
    let captured = Arc::new(Mutex::new(Vec::new()));
    let calls = Arc::clone(&captured);
    let handler: MockHandler = Box::new(move |req: Value| {
        let name = req["params"]["name"].as_str().unwrap_or("").to_string();
        calls.lock().unwrap().push(name);
        Err((-32007, "active tab lease required".to_string(), None))
    });
    let (_dir, socket, server) = spawn_mock_server(Arc::new(Mutex::new(handler))).await;
    let client = BrowserClient::connect_as(&socket, Duration::from_secs(5), ControllerKind::MahoCli)
        .await
        .expect("failed to connect");

    let mut output = Vec::new();
    browser_pipe::run_pipe_loop(
        &b"{\"id\":1,\"op\":\"upload\",\"tab_id\":7,\"path\":\"/tmp/upload.png\",\"selector\":\"#upload\"}\n"[..],
        &mut output,
        &client,
    )
    .await
    .expect("run_pipe_loop failed");
    server.abort();

    let response: PipeResponse = serde_json::from_slice(&output).expect("parse response");
    assert!(!response.ok);
    assert_eq!(response.error.expect("lease error").code, "lease_required");
    assert_eq!(*captured.lock().unwrap(), ["browser_acquire_lease"]);
}

#[tokio::test(flavor = "multi_thread")]
async fn test_pipe_navigate_does_not_dispatch_without_lease() {
    let captured = Arc::new(Mutex::new(Vec::new()));
    let calls = Arc::clone(&captured);
    let handler: MockHandler = Box::new(move |req: Value| {
        calls.lock().unwrap().push(req["params"]["name"].as_str().unwrap_or("").to_string());
        Err((-32007, "active tab lease required".to_string(), None))
    });
    let (_dir, socket, server) = spawn_mock_server(Arc::new(Mutex::new(handler))).await;
    let client = BrowserClient::connect_as(&socket, Duration::from_secs(5), ControllerKind::MahoCli)
        .await
        .expect("failed to connect");
    let mut output = Vec::new();
    browser_pipe::run_pipe_loop(
        &b"{\"id\":1,\"op\":\"navigate\",\"tab_id\":7,\"url\":\"https://example.com/spa\"}\n"[..],
        &mut output,
        &client,
    )
    .await
    .expect("run_pipe_loop failed");
    server.abort();
    let response: PipeResponse = serde_json::from_slice(&output).expect("parse response");
    assert!(!response.ok);
    assert_eq!(response.error.expect("lease error").code, "lease_required");
    assert_eq!(*captured.lock().unwrap(), ["browser_acquire_lease"]);
}

#[tokio::test(flavor = "multi_thread")]
async fn test_pipe_upload_invalid_params() {
    let captured_calls = Arc::new(Mutex::new(Vec::new()));
    let calls_clone = Arc::clone(&captured_calls);

    let handler: MockHandler = Box::new(move |req: Value| {
        let params = req.get("params").cloned().unwrap_or(Value::Null);
        let tool_name = params
            .get("name")
            .and_then(Value::as_str)
            .unwrap_or("")
            .to_string();
        let arguments = params.get("arguments").cloned().unwrap_or(Value::Null);
        calls_clone
            .lock()
            .unwrap()
            .push((tool_name, arguments));
        Ok(json!({}))
    });

    let (_dir, sock_path, server_handle) = spawn_mock_server(Arc::new(Mutex::new(handler))).await;
    let client =
        BrowserClient::connect_as(&sock_path, Duration::from_secs(5), ControllerKind::MahoCli)
            .await
            .expect("failed to connect");

    let stdin_data = [
        r#"{"id": 1, "op": "upload", "path": "/tmp/upload.png"}"#,
        r#"{"id": 2, "op": "upload", "tab_id": 7}"#,
        r#"{"id": 3, "op": "upload", "tab_id": 7, "path": "   "}"#,
        r#"{"id": 4, "op": "upload", "tab_id": 7, "path": "/tmp/upload.png", "selector": 12345}"#,
        r#"{"id": 5, "op": "upload", "tab_id": 7, "path": "/tmp/upload.png", "selector": ""}"#,
        r#"{"id": 6, "op": "upload", "tab_id": 7, "path": "/tmp/upload.png", "selector": "   "}"#,
    ]
    .join("\n")
        + "\n";

    let mut output = Vec::new();
    browser_pipe::run_pipe_loop(stdin_data.as_bytes(), &mut output, &client)
        .await
        .expect("run_pipe_loop failed");

    server_handle.abort();

    let output_str = String::from_utf8(output).expect("valid utf8");
    let responses: Vec<PipeResponse> = output_str
        .lines()
        .map(|line| serde_json::from_str(line).expect("parse response line"))
        .collect();

    assert_eq!(responses.len(), 6);
    for resp in &responses {
        assert!(!resp.ok);
        assert_eq!(resp.error.as_ref().unwrap().code, "invalid_params");
    }

    let calls = captured_calls.lock().unwrap();
    assert_eq!(calls.len(), 0, "No invalid input should reach the server");
}
