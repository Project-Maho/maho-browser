// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Shared test harness: detect/connect to a running Maho browser instance via
//! `BrowserClient`, provide helper utilities for teardown.

use std::path::PathBuf;
use std::time::Duration;

use maho_browser_mcp::client::BrowserClient;
use maho_browser_mcp::paths::default_socket_path;
use serde_json::{json, Value};

/// Default timeout for integration test calls.
const TEST_TIMEOUT: Duration = Duration::from_secs(15);

/// Connect to the running Maho browser's MCP socket.
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
    if let Ok(path) = std::env::var("MAHO_MCP_SOCKET_PATH") {
        if !path.is_empty() {
            return PathBuf::from(path);
        }
    }
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
        Err(maho_browser_mcp::error::McpBridgeError::Rpc { code, message, .. }) => (code, message),
        Err(other) => panic!("Expected RPC error for {tool}, got: {other}"),
        Ok(val) => panic!("Expected error for {tool}, got success: {val}"),
    }
}

/// The browser's `tools/call` responses are raw structured JSON (no MCP
/// `content[]` wrapper), so string-based assertions match against the whole
/// serialized result.
pub fn extract_text_content(response: &Value) -> String {
    serde_json::to_string(response).unwrap_or_default()
}

/// Resolve the real id of the active tab (mutations and `browser_tab_get`
/// require the concrete tab id, not the `0` placeholder).
pub async fn active_tab_id(client: &BrowserClient) -> i64 {
    let list = call_tool(client, "browser_tab_list", json!({})).await;
    let tabs = list["tabs"]
        .as_array()
        .expect("tab_list should return tabs");
    tabs.iter()
        .find(|t| t["is_active"].as_bool().unwrap_or(false))
        .or_else(|| tabs.first())
        .and_then(|t| t["id"].as_i64())
        .expect("no active tab id available")
}

/// Acquire the lease on the real active tab (required before any mutation).
/// Returns the leased tab id.
pub async fn acquire_active_lease(client: &BrowserClient, ttl_seconds: u64) -> i64 {
    let id = active_tab_id(client).await;
    let res = call_tool(
        client,
        "browser_acquire_lease",
        json!({"tab_id": id, "ttl_seconds": ttl_seconds, "force_steal": true}),
    )
    .await;
    assert_eq!(
        res["acquired"], true,
        "failed to acquire lease on tab {id}: {res}"
    );
    id
}

/// True only when a real display/GPU is available (set MAHO_MCP_TEST_INTERACTIVE=1).
/// Screenshot capture (copy-output) and synthetic input (click/type/hover/key_press)
/// crash a headless --disable-gpu browser, so those tests require this gate.
pub fn interactive_env() -> bool {
    std::env::var("MAHO_MCP_TEST_INTERACTIVE")
        .map(|v| v == "1" || v.eq_ignore_ascii_case("true"))
        .unwrap_or(false)
}
