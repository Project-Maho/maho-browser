// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Shared test harness: detect/connect to a running Maho browser instance via
//! `BrowserClient`, provide helper utilities for teardown.

use std::path::PathBuf;
use std::time::Duration;

use maho_browser_mcp::client::BrowserClient;
use maho_browser_mcp::paths::default_socket_path;
use serde_json::{Value, json};

/// Default timeout for integration test calls.
const TEST_TIMEOUT: Duration = Duration::from_secs(10);

/// Connect to the running Maho browser's MCP socket.
///
/// Returns a connected `BrowserClient` ready to invoke tools.
/// Panics with a descriptive message if the socket is not available.
pub async fn connect_browser() -> BrowserClient {
    let path = socket_path();
    BrowserClient::connect_with_timeout(&path, TEST_TIMEOUT)
        .await
        .unwrap_or_else(|e| {
            panic!(
                "Failed to connect to Maho MCP socket at {}: {e}\n\
                 Ensure Maho.app is running with MCP enabled.",
                path.display()
            )
        })
}

/// Resolve the socket path, respecting `MAHO_MCP_SOCKET_PATH` env override.
pub fn socket_path() -> PathBuf {
    default_socket_path()
}

/// Invoke `tools/call` for a given tool name with the specified arguments.
pub async fn call_tool(client: &BrowserClient, tool: &str, args: Value) -> Value {
    let params = json!({
        "name": tool,
        "arguments": args,
    });
    client
        .call("tools/call", params)
        .await
        .unwrap_or_else(|e| panic!("tools/call({tool}) failed: {e}"))
}

/// Invoke `tools/call` and expect an RPC error. Returns the error code.
pub async fn call_tool_expect_error(
    client: &BrowserClient,
    tool: &str,
    args: Value,
) -> (i32, String) {
    let params = json!({
        "name": tool,
        "arguments": args,
    });
    match client.call("tools/call", params).await {
        Err(maho_browser_mcp::error::McpBridgeError::Rpc { code, message }) => (code, message),
        Err(other) => panic!("Expected RPC error for {tool}, got: {other}"),
        Ok(val) => panic!("Expected error for {tool}, got success: {val}"),
    }
}

/// Extract text content from a `tools/call` response that returns `content[]`.
pub fn extract_text_content(response: &Value) -> String {
    response["content"]
        .as_array()
        .and_then(|arr| arr.first())
        .and_then(|item| item["text"].as_str())
        .unwrap_or("")
        .to_string()
}
