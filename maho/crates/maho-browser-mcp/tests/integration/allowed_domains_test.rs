// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Integration tests for domain allowlist enforcement (D1).
//! All tests are `#[ignore]`d — they require a running Maho browser instance.

mod common;

use serde_json::json;

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_first_navigate_prompts_when_allowlist_empty() {
    let client = common::connect_browser().await;

    // Without setting allowed_domains, navigate should require approval
    let result = common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://untrusted-domain.example"}),
    )
    .await;
    let text = common::extract_text_content(&result);

    assert!(
        text.contains("domain_approval_required") || text.contains("not allowed"),
        "Navigate to unknown domain should require approval: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_navigate_succeeds_after_set_allowed_domains() {
    let client = common::connect_browser().await;

    // Set allowed domains
    common::call_tool(
        &client,
        "browser_set_allowed_domains",
        json!({"domains": ["example.com"]}),
    )
    .await;

    // Navigate should now succeed
    let result = common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;
    let text = common::extract_text_content(&result);

    assert!(
        !text.contains("domain_approval_required") && !text.contains("not allowed"),
        "Navigate to allowed domain should succeed: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_wildcard_matches_subdomain() {
    let client = common::connect_browser().await;

    // Set wildcard domain
    common::call_tool(
        &client,
        "browser_set_allowed_domains",
        json!({"domains": ["*.github.com"]}),
    )
    .await;

    // Subdomain should match
    let result = common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://api.github.com"}),
    )
    .await;
    let text = common::extract_text_content(&result);

    assert!(
        !text.contains("domain_approval_required") && !text.contains("not allowed"),
        "Wildcard should match subdomain: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_new_session_empty_allowlist() {
    let client_a = common::connect_browser().await;

    // Set allowed domains in session A
    common::call_tool(
        &client_a,
        "browser_set_allowed_domains",
        json!({"domains": ["example.com"]}),
    )
    .await;

    // New session should have empty allowlist (session-scoped)
    let client_b = common::connect_browser().await;
    let result = common::call_tool(
        &client_b,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;
    let text = common::extract_text_content(&result);

    assert!(
        text.contains("domain_approval_required") || text.contains("not allowed"),
        "New session should have empty allowlist: {text}"
    );
}
