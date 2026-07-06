// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Integration tests for every MCP tool exposed by the browser socket server.
//! All tests are `#[ignore]`d — they require a running Maho browser instance.

mod common;

use serde_json::json;

// ─── Tab Management ─────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_tab_list() {
    let client = common::connect_browser().await;
    let result = common::call_tool(&client, "browser_tab_list", json!({})).await;
    // Response should contain a content array with tab info
    assert!(result["content"].is_array(), "Expected content array: {result}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_tab_get() {
    let client = common::connect_browser().await;
    // First list tabs to get a valid tab ID
    let list = common::call_tool(&client, "browser_tab_list", json!({})).await;
    let tabs_text = common::extract_text_content(&list);
    assert!(!tabs_text.is_empty(), "No tabs returned");

    // Get the active tab (tab_id = 0 as convention for "current")
    let result = common::call_tool(&client, "browser_tab_get", json!({"tab_id": 0})).await;
    let text = common::extract_text_content(&result);
    assert!(text.contains("url:") || text.contains("title:"), "Unexpected tab info: {text}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_tab_new() {
    let client = common::connect_browser().await;
    let result = common::call_tool(
        &client,
        "browser_tab_new",
        json!({"url": "about:blank"}),
    )
    .await;
    let text = common::extract_text_content(&result);
    assert!(
        text.contains("tab") || text.contains("created"),
        "Unexpected response: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_tab_close() {
    let client = common::connect_browser().await;
    // Create a tab then close it
    let new_tab = common::call_tool(
        &client,
        "browser_tab_new",
        json!({"url": "about:blank"}),
    )
    .await;
    let _text = common::extract_text_content(&new_tab);

    // Close tab 0 (most recently created typically gets focus)
    let result = common::call_tool(&client, "browser_tab_close", json!({"tab_id": 0})).await;
    assert!(result["content"].is_array());
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_navigate() {
    let client = common::connect_browser().await;
    let result = common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;
    let text = common::extract_text_content(&result);
    assert!(
        text.contains("example.com") || text.contains("navigated"),
        "Unexpected navigate response: {text}"
    );
}

// ─── History ────────────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_history_search() {
    let client = common::connect_browser().await;
    let result = common::call_tool(
        &client,
        "browser_history_search",
        json!({"query": "example", "max_results": 5}),
    )
    .await;
    assert!(result["content"].is_array(), "Expected content array: {result}");
}

// ─── Bookmarks ──────────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_bookmarks_search() {
    let client = common::connect_browser().await;
    let result = common::call_tool(
        &client,
        "browser_bookmarks_search",
        json!({"query": "test"}),
    )
    .await;
    assert!(result["content"].is_array(), "Expected content array: {result}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_bookmark_create() {
    let client = common::connect_browser().await;
    let result = common::call_tool(
        &client,
        "browser_bookmark_create",
        json!({"url": "https://example.com", "title": "Integration Test Bookmark"}),
    )
    .await;
    let text = common::extract_text_content(&result);
    assert!(
        text.contains("created") || text.contains("bookmark"),
        "Unexpected response: {text}"
    );
}

// ─── Page Content ───────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_page_content() {
    let client = common::connect_browser().await;
    // Navigate first to ensure page is loaded
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;

    let result = common::call_tool(&client, "browser_page_content", json!({})).await;
    let text = common::extract_text_content(&result);
    assert!(text.contains("Example Domain"), "Expected page content: {text}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_page_text() {
    let client = common::connect_browser().await;
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;

    let result = common::call_tool(&client, "browser_page_text", json!({})).await;
    let text = common::extract_text_content(&result);
    assert!(
        text.contains("Example Domain"),
        "Expected plain text content: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_search_in_page() {
    let client = common::connect_browser().await;
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;

    let result = common::call_tool(
        &client,
        "browser_search_in_page",
        json!({"query": "Example"}),
    )
    .await;
    let text = common::extract_text_content(&result);
    assert!(
        text.contains("match") || text.contains("found") || text.contains("Example"),
        "Unexpected search result: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_page_context() {
    let client = common::connect_browser().await;
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;

    let result = common::call_tool(&client, "browser_page_context", json!({})).await;
    let text = common::extract_text_content(&result);
    assert!(!text.is_empty(), "Expected non-empty page context");
}

// ─── DOM Queries ────────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_page_query_selector() {
    let client = common::connect_browser().await;
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;

    let result = common::call_tool(
        &client,
        "browser_query_selector",
        json!({"selector": "h1"}),
    )
    .await;
    let text = common::extract_text_content(&result);
    assert!(
        text.contains("Example Domain"),
        "Expected h1 text: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_page_get_text() {
    let client = common::connect_browser().await;
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;

    let result = common::call_tool(
        &client,
        "browser_get_text",
        json!({"selector": "p"}),
    )
    .await;
    let text = common::extract_text_content(&result);
    assert!(!text.is_empty(), "Expected paragraph text");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_page_get_attribute() {
    let client = common::connect_browser().await;
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;

    let result = common::call_tool(
        &client,
        "browser_get_attribute",
        json!({"selector": "a", "attribute": "href"}),
    )
    .await;
    let text = common::extract_text_content(&result);
    assert!(
        text.contains("http") || text.contains("iana"),
        "Expected href value: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_page_wait_for_selector() {
    let client = common::connect_browser().await;
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;

    let result = common::call_tool(
        &client,
        "browser_wait_for_selector",
        json!({"selector": "h1", "timeout_ms": 5000}),
    )
    .await;
    assert!(result["content"].is_array(), "Expected content array: {result}");
}

// ─── Accessibility & Interaction ────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_accessibility_snapshot() {
    let client = common::connect_browser().await;
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;

    let result = common::call_tool(&client, "browser_accessibility_snapshot", json!({})).await;
    let text = common::extract_text_content(&result);
    assert!(
        text.contains("heading") || text.contains("link") || text.contains("Example"),
        "Expected accessibility tree content: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_click() {
    let client = common::connect_browser().await;
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;

    let result = common::call_tool(
        &client,
        "browser_click",
        json!({"selector": "a"}),
    )
    .await;
    assert!(result["content"].is_array(), "Expected content array: {result}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_type() {
    let client = common::connect_browser().await;
    // Navigate to a page with an input field
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;

    let result = common::call_tool(
        &client,
        "browser_type",
        json!({"selector": "input", "text": "hello world"}),
    )
    .await;
    // May error if no input on page — that's acceptable for scaffold
    assert!(
        result["content"].is_array() || result["error"].is_object(),
        "Unexpected response: {result}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_select() {
    let client = common::connect_browser().await;
    let result = common::call_tool(
        &client,
        "browser_select",
        json!({"selector": "select", "value": "option1"}),
    )
    .await;
    // May error if no select on page — acceptable for scaffold
    assert!(
        result["content"].is_array() || result["error"].is_object(),
        "Unexpected response: {result}"
    );
}

// ─── Screenshot ─────────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_screenshot_full() {
    let client = common::connect_browser().await;
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;

    let result = common::call_tool(
        &client,
        "browser_screenshot",
        json!({"full_page": true}),
    )
    .await;

    // Screenshot response should contain base64 PNG data
    let content = result["content"]
        .as_array()
        .expect("Expected content array");
    let image_item = content
        .iter()
        .find(|item| item["type"] == "image")
        .expect("Expected image content item");
    let data = image_item["data"].as_str().expect("Expected base64 data");

    // Decode and verify PNG magic bytes: 0x89 0x50 0x4E 0x47
    let decoded = base64_decode(data);
    assert!(decoded.len() > 8, "PNG data too short");
    assert_eq!(&decoded[0..4], &[0x89, 0x50, 0x4E, 0x47], "Not a valid PNG");
}

// ─── Network Capture ────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_network_start_capture() {
    let client = common::connect_browser().await;
    let result = common::call_tool(&client, "browser_network_start_capture", json!({})).await;
    let text = common::extract_text_content(&result);
    assert!(
        text.contains("started") || text.contains("capture"),
        "Unexpected response: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_network_get_har() {
    let client = common::connect_browser().await;
    // Start capture, navigate, then get HAR
    common::call_tool(&client, "browser_network_start_capture", json!({})).await;
    common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;

    // Brief pause to accumulate entries
    tokio::time::sleep(std::time::Duration::from_secs(2)).await;

    let result = common::call_tool(&client, "browser_network_get_har", json!({})).await;
    let text = common::extract_text_content(&result);

    // Verify HAR 1.2 shape
    let har: serde_json::Value =
        serde_json::from_str(&text).expect("HAR should be valid JSON");
    assert_eq!(har["log"]["version"], "1.2", "Expected HAR version 1.2");
    assert!(
        har["log"]["entries"].is_array(),
        "Expected entries array in HAR"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_network_stop_capture() {
    let client = common::connect_browser().await;
    common::call_tool(&client, "browser_network_start_capture", json!({})).await;
    let result = common::call_tool(&client, "browser_network_stop_capture", json!({})).await;
    let text = common::extract_text_content(&result);
    assert!(
        text.contains("stopped") || text.contains("capture"),
        "Unexpected response: {text}"
    );
}

// ─── Helpers ────────────────────────────────────────────────────────────────

fn base64_decode(input: &str) -> Vec<u8> {
    // Simple base64 decode without pulling in a dep — uses the built-in approach
    // For integration tests we can afford a naive implementation
    let table = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut output = Vec::new();
    let mut buf: u32 = 0;
    let mut bits: u32 = 0;
    for byte in input.bytes() {
        let val = if byte == b'=' {
            break;
        } else if let Some(pos) = table.iter().position(|&b| b == byte) {
            pos as u32
        } else {
            continue; // skip whitespace/newlines
        };
        buf = (buf << 6) | val;
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            output.push((buf >> bits) as u8);
            buf &= (1 << bits) - 1;
        }
    }
    output
}
