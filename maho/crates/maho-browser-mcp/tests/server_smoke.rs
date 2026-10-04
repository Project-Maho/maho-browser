// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(unix)]

//! End-to-end smoke test: spawns a fake browser socket, launches the
//! `maho-browser-mcp` binary, and verifies MCP protocol flow over stdio.

use std::process::Stdio;

use serde_json::{json, Value};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixListener;

/// SC8: End-to-end binary via fake browser.
#[tokio::test]
#[ignore = "requires built binary; run with --ignored"]
async fn end_to_end_binary_via_fake_browser() {
    let dir = tempfile::tempdir().unwrap();
    let sock_path = dir.path().join("fake-browser.sock");
    let listener = UnixListener::bind(&sock_path).unwrap();

    // Fake browser: handles initialize, tools/list is handled by rmcp on the server side,
    // and ping tool calls get forwarded.
    let sock_path_clone = sock_path.clone();
    let _browser_handle = tokio::spawn(async move {
        let (stream, _) = listener.accept().await.unwrap();
        let (reader, mut writer) = stream.into_split();
        let mut reader = BufReader::new(reader);
        let mut line = String::new();

        loop {
            line.clear();
            if reader.read_line(&mut line).await.unwrap() == 0 {
                break;
            }
            let req: Value = serde_json::from_str(&line).unwrap();
            let id = req["id"].as_i64().unwrap();
            let wire_method = req["method"].as_str().unwrap_or("");
            // Wave 9: the MCP server forwards browser tool calls wrapped in a
            // `tools/call` envelope (params.name = tool). Unwrap so this fake
            // browser matches on the effective tool/method name.
            let method = if wire_method == "tools/call" {
                req["params"]["name"].as_str().unwrap_or("").to_string()
            } else {
                wire_method.to_string()
            };

            let resp = match method.as_str() {
                "initialize" => json!({
                    "jsonrpc": "2.0",
                    "id": id,
                    "result": {
                        "protocolVersion": "2025-03-26",
                        "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                        "capabilities": {"tools": {}}
                    }
                }),
                "ping" => json!({
                    "jsonrpc": "2.0",
                    "id": id,
                    "result": {"pong": true}
                }),
                _ => json!({
                    "jsonrpc": "2.0",
                    "id": id,
                    "error": {"code": -32601, "message": "Method not found"}
                }),
            };
            let mut buf = serde_json::to_vec(&resp).unwrap();
            buf.push(b'\n');
            writer.write_all(&buf).await.unwrap();
        }
    });

    // Spawn the maho-browser-mcp binary.
    let binary = env!("CARGO_BIN_EXE_maho-browser-mcp");
    let mut child = tokio::process::Command::new(binary)
        .env("MAHO_MCP_SOCKET_PATH", &sock_path_clone)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()
        .expect("failed to spawn binary");

    let mut stdin = child.stdin.take().unwrap();
    let stdout = child.stdout.take().unwrap();
    let mut stdout_reader = BufReader::new(stdout);

    // Send initialize to the MCP server (rmcp on stdio).
    let init_req = json!({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2025-03-26",
            "capabilities": {},
            "clientInfo": {"name": "test-client", "version": "0.1.0"}
        }
    });
    let mut msg = serde_json::to_vec(&init_req).unwrap();
    msg.push(b'\n');
    stdin.write_all(&msg).await.unwrap();

    let mut resp_line = String::new();
    stdout_reader.read_line(&mut resp_line).await.unwrap();
    let init_resp: Value = serde_json::from_str(&resp_line).unwrap();
    assert_eq!(init_resp["id"], 1);
    assert!(init_resp["result"].is_object());

    // Send tools/list.
    let tools_req = json!({
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/list",
        "params": {}
    });
    let mut msg2 = serde_json::to_vec(&tools_req).unwrap();
    msg2.push(b'\n');
    stdin.write_all(&msg2).await.unwrap();

    resp_line.clear();
    stdout_reader.read_line(&mut resp_line).await.unwrap();
    let tools_resp: Value = serde_json::from_str(&resp_line).unwrap();
    assert_eq!(tools_resp["id"], 2);
    let tools = tools_resp["result"]["tools"].as_array().unwrap();
    assert!(tools.iter().any(|t| t["name"] == "browser_ping"));

    // Send tools/call browser_ping.
    let call_req = json!({
        "jsonrpc": "2.0",
        "id": 3,
        "method": "tools/call",
        "params": {
            "name": "browser_ping",
            "arguments": {}
        }
    });
    let mut msg3 = serde_json::to_vec(&call_req).unwrap();
    msg3.push(b'\n');
    stdin.write_all(&msg3).await.unwrap();

    resp_line.clear();
    stdout_reader.read_line(&mut resp_line).await.unwrap();
    let call_resp: Value = serde_json::from_str(&resp_line).unwrap();
    assert_eq!(call_resp["id"], 3);
    // The tool result contains the pong response as text content.
    let content = &call_resp["result"]["content"][0]["text"];
    let pong: Value = serde_json::from_str(content.as_str().unwrap()).unwrap();
    assert_eq!(pong, json!({"pong": true}));

    // Cleanup.
    drop(stdin);
    let _ = child.kill().await;
}
