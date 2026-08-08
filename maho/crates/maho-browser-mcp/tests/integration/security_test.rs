// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod common;

use serde_json::json;

pub const SENTINEL_SECRET: &str = "S3NTINEL-maho-vault-9F4C";

fn fixture_base() -> Option<String> {
    std::env::var("MAHO_MCP_TEST_FIXTURE_BASE")
        .ok()
        .filter(|s| !s.is_empty())
}

async fn open_url(client: &maho_browser_mcp::client::BrowserClient, allow_host: &str, url: &str) {
    let _ = allow_host;
    common::acquire_active_lease(client, 60).await;
    let params = json!({"name": "browser_navigate", "arguments": {"url": url}});
    let _ = tokio::time::timeout(
        std::time::Duration::from_secs(8),
        client.call("tools/call", params),
    )
    .await;
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_evaluate_js_returns_method_not_found() {
    let client = common::connect_browser().await;
    let (code, _message) = common::call_tool_expect_error(
        &client,
        "evaluate_js",
        json!({"expression": "document.title"}),
    )
    .await;
    assert_eq!(
        code, -32601,
        "evaluate_js should return Method Not Found (-32601)"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_4vector_firewall_password_field() {
    let client = common::connect_browser().await;
    match fixture_base() {
        Some(base) => open_url(&client, "127.0.0.1", &format!("{base}/login")).await,
        None => open_url(&client, "example.com", "https://example.com").await,
    }
    let result = common::call_tool(&client, "browser_page_content", json!({})).await;
    let text = common::extract_text_content(&result);
    assert!(
        !text.contains(SENTINEL_SECRET) && !text.contains("password_value_here"),
        "Password field value must never be surfaced in page content: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_4vector_firewall_authorization_header() {
    let client = common::connect_browser().await;
    common::call_tool(&client, "browser_network_start_capture", json!({})).await;
    match fixture_base() {
        Some(base) => open_url(&client, "127.0.0.1", &format!("{base}/auth")).await,
        None => open_url(&client, "example.com", "https://example.com").await,
    }
    tokio::time::sleep(std::time::Duration::from_secs(2)).await;
    let har = common::call_tool(&client, "browser_network_stop_capture", json!({})).await;
    let text = common::extract_text_content(&har);
    assert!(
        !text.contains("bearersecret_ABC123"),
        "Authorization bearer token must never leak in HAR: {text}"
    );
    if text.contains("Authorization") {
        assert!(
            text.contains("[REDACTED]"),
            "Authorization header value must be redacted in HAR: {text}"
        );
    }
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_4vector_firewall_url_access_token() {
    let client = common::connect_browser().await;
    match fixture_base() {
        Some(base) => {
            open_url(
                &client,
                "127.0.0.1",
                &format!("{base}/callback?access_token=secret_xyz_123"),
            )
            .await
        }
        None => {
            open_url(
                &client,
                "example.com",
                "https://example.com/callback?access_token=secret_xyz_123",
            )
            .await
        }
    }
    let id = common::active_tab_id(&client).await;
    let result = common::call_tool(&client, "browser_tab_get", json!({"tab_id": id})).await;
    let text = common::extract_text_content(&result);
    assert!(
        !text.contains("secret_xyz_123"),
        "access_token must never be surfaced in tab_get output: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_4vector_firewall_ax_tree_autofill() {
    let client = common::connect_browser().await;
    match fixture_base() {
        Some(base) => open_url(&client, "127.0.0.1", &format!("{base}/form")).await,
        None => open_url(&client, "example.com", "https://example.com").await,
    }
    let result = common::call_tool(&client, "browser_accessibility_snapshot", json!({})).await;
    let text = common::extract_text_content(&result);
    assert!(
        !text.contains("4111111111111111") && !text.contains("555-12-3456"),
        "Autofill-sensitive data must never appear in the accessibility snapshot: {text}"
    );
}
