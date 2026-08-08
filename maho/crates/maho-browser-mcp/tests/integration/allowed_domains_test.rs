// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod common;

use serde_json::json;
use std::time::Duration;

async fn attempt_navigate(client: &maho_browser_mcp::client::BrowserClient, url: &str) -> bool {
    let params = json!({"name": "browser_navigate", "arguments": {"url": url}});
    match tokio::time::timeout(Duration::from_secs(5), client.call("tools/call", params)).await {
        Ok(Ok(v)) => v["navigated"] == true,
        _ => false,
    }
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_navigation_allowed_by_default_when_blocklist_empty() {
    let client = common::connect_browser().await;
    let blocked = common::call_tool(&client, "browser_get_blocked_domains", json!({})).await;
    assert_eq!(
        blocked["domains"].as_array().map(|a| a.len()),
        Some(0),
        "Fresh session should start with an empty disallow list: {blocked}"
    );
    common::call_tool(&client, "browser_tab_new", json!({"url": "about:blank"})).await;
    common::acquire_active_lease(&client, 60).await;
    let navigated = attempt_navigate(&client, "https://example.com").await;
    assert!(
        navigated,
        "Navigation must be allowed by default when the disallow list is empty"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_navigate_blocked_after_set_blocked_domains() {
    let client = common::connect_browser().await;
    common::call_tool(&client, "browser_tab_new", json!({"url": "about:blank"})).await;
    common::acquire_active_lease(&client, 60).await;
    common::call_tool(
        &client,
        "browser_set_blocked_domains",
        json!({"domains": ["example.com"]}),
    )
    .await;
    let blocked = attempt_navigate(&client, "https://example.com").await;
    assert!(!blocked, "Navigate to a disallowed domain must fail");
    let allowed = attempt_navigate(&client, "https://example.org").await;
    assert!(
        allowed,
        "Navigate to a domain not on the disallow list must succeed"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_wildcard_blocklist_entry_is_stored() {
    let client = common::connect_browser().await;
    common::call_tool(
        &client,
        "browser_set_blocked_domains",
        json!({"domains": ["*.github.com"]}),
    )
    .await;
    let blocked = common::call_tool(&client, "browser_get_blocked_domains", json!({})).await;
    let domains = blocked["domains"].as_array().expect("domains array");
    assert!(
        domains.iter().any(|d| d == "*.github.com"),
        "Wildcard disallow entry should be stored: {blocked}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_new_session_empty_blocklist() {
    let client_a = common::connect_browser().await;
    common::call_tool(
        &client_a,
        "browser_set_blocked_domains",
        json!({"domains": ["example.com"]}),
    )
    .await;
    let client_b = common::connect_browser().await;
    let blocked_b = common::call_tool(&client_b, "browser_get_blocked_domains", json!({})).await;
    assert_eq!(
        blocked_b["domains"].as_array().map(|a| a.len()),
        Some(0),
        "A new session must not inherit another session's disallow list: {blocked_b}"
    );
}
