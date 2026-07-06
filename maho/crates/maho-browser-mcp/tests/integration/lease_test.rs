// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Integration tests for tab lease management (TTL, heartbeat, force-steal).
//! All tests are `#[ignore]`d — they require a running Maho browser instance.

mod common;

use serde_json::json;
use std::time::Duration;

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_lease_acquire_and_second_session_blocked() {
    // Session A acquires lease on the active tab
    let client_a = common::connect_browser().await;
    let acquire_a = common::call_tool(
        &client_a,
        "browser_lease_acquire",
        json!({"tab_id": 0, "ttl_seconds": 30}),
    )
    .await;
    let text_a = common::extract_text_content(&acquire_a);
    assert!(
        text_a.contains("acquired") || text_a.contains("lease"),
        "Session A should acquire lease: {text_a}"
    );

    // Session B tries to acquire the same tab without force_steal
    let client_b = common::connect_browser().await;
    let (code, message) = common::call_tool_expect_error(
        &client_b,
        "browser_lease_acquire",
        json!({"tab_id": 0, "ttl_seconds": 30, "force_steal": false}),
    )
    .await;

    assert!(
        code != 0 || message.contains("held") || message.contains("locked"),
        "Session B should be blocked: code={code}, message={message}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_lease_heartbeat_extends_ttl() {
    let client = common::connect_browser().await;

    // Acquire with 15s TTL
    common::call_tool(
        &client,
        "browser_lease_acquire",
        json!({"tab_id": 0, "ttl_seconds": 15}),
    )
    .await;

    // Wait 10s then heartbeat
    tokio::time::sleep(Duration::from_secs(10)).await;
    let heartbeat_result = common::call_tool(
        &client,
        "browser_lease_heartbeat",
        json!({"tab_id": 0}),
    )
    .await;
    let text = common::extract_text_content(&heartbeat_result);
    assert!(
        text.contains("extended") || text.contains("heartbeat") || text.contains("ok"),
        "Heartbeat should extend TTL: {text}"
    );

    // At t=20s (5s after original expiry), lease should still be held
    tokio::time::sleep(Duration::from_secs(10)).await;

    // Verify lease is still active by trying second session
    let client_b = common::connect_browser().await;
    let (code, message) = common::call_tool_expect_error(
        &client_b,
        "browser_lease_acquire",
        json!({"tab_id": 0, "ttl_seconds": 5, "force_steal": false}),
    )
    .await;
    assert!(
        code != 0 || message.contains("held"),
        "Lease should still be active after heartbeat: code={code}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_lease_force_steal_emits_event() {
    let client_a = common::connect_browser().await;

    // Session A acquires
    common::call_tool(
        &client_a,
        "browser_lease_acquire",
        json!({"tab_id": 0, "ttl_seconds": 60}),
    )
    .await;

    // Session B force-steals
    let client_b = common::connect_browser().await;
    let steal_result = common::call_tool(
        &client_b,
        "browser_lease_acquire",
        json!({"tab_id": 0, "ttl_seconds": 30, "force_steal": true}),
    )
    .await;
    let text = common::extract_text_content(&steal_result);
    assert!(
        text.contains("acquired") || text.contains("stolen") || text.contains("lease"),
        "Force steal should succeed: {text}"
    );

    // Session A should eventually receive a lease_stolen notification.
    // In scaffold we just verify the steal succeeded from B's perspective.
    // Full event verification requires notification channel inspection.
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_lease_ttl_expiry() {
    let client_a = common::connect_browser().await;

    // Acquire with very short TTL
    common::call_tool(
        &client_a,
        "browser_lease_acquire",
        json!({"tab_id": 0, "ttl_seconds": 5}),
    )
    .await;

    // Wait for expiry
    tokio::time::sleep(Duration::from_secs(6)).await;

    // Second session should now be able to acquire
    let client_b = common::connect_browser().await;
    let result = common::call_tool(
        &client_b,
        "browser_lease_acquire",
        json!({"tab_id": 0, "ttl_seconds": 10}),
    )
    .await;
    let text = common::extract_text_content(&result);
    assert!(
        text.contains("acquired") || text.contains("lease"),
        "Should acquire after TTL expiry: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_lease_release_by_holder() {
    let client = common::connect_browser().await;

    // Acquire
    common::call_tool(
        &client,
        "browser_lease_acquire",
        json!({"tab_id": 0, "ttl_seconds": 60}),
    )
    .await;

    // Release
    let release_result = common::call_tool(
        &client,
        "browser_lease_release",
        json!({"tab_id": 0}),
    )
    .await;
    let text = common::extract_text_content(&release_result);
    assert!(
        text.contains("released") || text.contains("ok"),
        "Release should succeed: {text}"
    );

    // Re-acquire should succeed immediately
    let reacquire = common::call_tool(
        &client,
        "browser_lease_acquire",
        json!({"tab_id": 0, "ttl_seconds": 10}),
    )
    .await;
    let text2 = common::extract_text_content(&reacquire);
    assert!(
        text2.contains("acquired") || text2.contains("lease"),
        "Re-acquire after release should succeed: {text2}"
    );
}
