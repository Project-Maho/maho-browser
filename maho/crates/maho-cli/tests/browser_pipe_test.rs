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
        calls_clone.lock().unwrap().push((tool_name.clone(), arguments.clone()));

        match tool_name.as_str() {
            "page.accessibility_snapshot_v2" => {
                let since = arguments.get("since_snapshot_token").and_then(Value::as_str);
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
    let client = BrowserClient::connect_as(&sock_path, Duration::from_secs(5), ControllerKind::MahoCli)
        .await
        .expect("failed to connect");

    let stdin_data = [
        r#"{"id": "op-1", "op": "observe", "mode": "compact"}"#,
        r#"{"id": "op-2", "op": "click", "locator": {"ref": 2}}"#,
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
    assert_eq!(lines.len(), 3, "Expected 3 response lines, got: {output_str}");

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
    let client = BrowserClient::connect_as(&sock_path, Duration::from_secs(5), ControllerKind::MahoCli)
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
    assert_eq!(lines.len(), 3, "Expected 3 response lines, got: {output_str}");

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
    let client = BrowserClient::connect_as(&sock_path, Duration::from_secs(5), ControllerKind::MahoCli)
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
    assert_eq!(lines.len(), 3, "Expected 3 response lines, got: {output_str}");

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
            "navigation.navigate" => Ok(json!({ "navigated": true })),
            "page.wait_for_selector" => Ok(json!({ "found": true })),
            other => panic!("Unexpected tool call: {other}"),
        }
    });

    let (_dir, sock_path, server_handle) = spawn_mock_server(Arc::new(Mutex::new(handler))).await;
    let client = BrowserClient::connect_as(&sock_path, Duration::from_secs(5), ControllerKind::MahoCli)
        .await
        .expect("failed to connect");

    let stdin_data = [
        r##"{"id": 1, "op": "type", "locator": {"css": "input.query"}, "text": "search terms", "submit": true}"##,
        r##"{"id": 2, "op": "navigate", "url": "https://example.com"}"##,
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
    let client = BrowserClient::connect_as(&sock_path, Duration::from_secs(5), ControllerKind::MahoCli)
        .await
        .expect("failed to connect");

    let stdin_data = r#"{"id": 7, "op": "grant", "origin": "https://contest.aitestbed.kr"}"#.to_string() + "\n";

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
    let client = BrowserClient::connect_as(&sock_path, Duration::from_secs(5), ControllerKind::MahoCli)
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
