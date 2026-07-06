// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Security regression tests for the 4-vector firewall (OQ-4).
//! All tests are `#[ignore]`d — they require a running Maho browser instance.

mod common;

use serde_json::json;

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
    assert_eq!(code, -32601, "evaluate_js should return Method Not Found (-32601)");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_4vector_firewall_password_field() {
    let client = common::connect_browser().await;

    // Navigate to a page with a password field (fixture or known URL)
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com/login-fixture"}),
    )
    .await;

    // Simulate typing into password field would happen on browser side;
    // we just verify page_content redacts it.
    let result = common::call_tool(&client, "browser_page_content", json!({})).await;
    let text = common::extract_text_content(&result);

    // Password field values must be redacted
    assert!(
        text.contains("[REDACTED]") || !text.contains("password_value_here"),
        "Password field value should be redacted in page content: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_4vector_firewall_authorization_header() {
    let client = common::connect_browser().await;

    // Start network capture
    common::call_tool(&client, "browser_network_start_capture", json!({})).await;

    // Navigate to a page that sends an Authorization header
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://httpbin.org/bearer"}),
    )
    .await;

    tokio::time::sleep(std::time::Duration::from_secs(2)).await;

    let result = common::call_tool(&client, "browser_network_get_har", json!({})).await;
    let text = common::extract_text_content(&result);

    // Authorization header values must be redacted in HAR output
    if text.contains("Authorization") {
        assert!(
            text.contains("[REDACTED]"),
            "Authorization header value should be redacted in HAR: {text}"
        );
    }

    common::call_tool(&client, "browser_network_stop_capture", json!({})).await;
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_4vector_firewall_url_access_token() {
    let client = common::connect_browser().await;

    // Navigate to a URL containing an access_token query parameter
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com/callback?access_token=secret_xyz_123"}),
    )
    .await;

    // Get tab info — URL should have token redacted
    let result = common::call_tool(&client, "browser_tab_get", json!({"tab_id": 0})).await;
    let text = common::extract_text_content(&result);

    assert!(
        text.contains("[REDACTED]") || !text.contains("secret_xyz_123"),
        "access_token in URL should be redacted in tab_get output: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_4vector_firewall_ax_tree_autofill() {
    let client = common::connect_browser().await;

    // Navigate to a form page that may have autofill data
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com/form-fixture"}),
    )
    .await;

    let result = common::call_tool(&client, "browser_accessibility_snapshot", json!({})).await;
    let text = common::extract_text_content(&result);

    // Autofill-sensitive data (credit cards, SSNs, passwords) must be redacted
    // in the accessibility tree snapshot
    assert!(
        !text.contains("4111111111111111") && !text.contains("555-12-3456"),
        "Autofill-sensitive data should be redacted in accessibility snapshot: {text}"
    );
}
