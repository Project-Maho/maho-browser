// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod common;

use serde_json::json;

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_tab_list() {
    let client = common::connect_browser().await;
    let result = common::call_tool(&client, "browser_tab_list", json!({})).await;
    assert!(result["tabs"].is_array(), "Expected tabs array: {result}");
    assert!(
        !result["tabs"].as_array().unwrap().is_empty(),
        "Expected at least one tab: {result}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_tab_get() {
    let client = common::connect_browser().await;
    let id = common::active_tab_id(&client).await;
    let result = common::call_tool(&client, "browser_tab_get", json!({"tab_id": id})).await;
    assert!(
        result["url"].is_string() && result["title"].is_string(),
        "Expected tab url/title: {result}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_tab_new() {
    let client = common::connect_browser().await;
    let result = common::call_tool(
        &client,
        "browser_tab_new",
        json!({"url": "https://example.com"}),
    )
    .await;
    let new_id = result["tab"]["id"].as_i64();
    assert!(new_id.is_some(), "Expected new tab id: {result}");
    common::call_tool(
        &client,
        "browser_tab_close",
        json!({"tab_id": new_id.unwrap()}),
    )
    .await;
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_tab_close() {
    let client = common::connect_browser().await;
    let new_tab = common::call_tool(
        &client,
        "browser_tab_new",
        json!({"url": "https://example.com"}),
    )
    .await;
    let new_id = new_tab["tab"]["id"]
        .as_i64()
        .expect("new tab should have id");
    let result = common::call_tool(&client, "browser_tab_close", json!({"tab_id": new_id})).await;
    assert_eq!(result["closed"], true, "Expected closed: {result}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_navigate() {
    let client = common::connect_browser().await;
    common::acquire_active_lease(&client, 60).await;
    let result = common::call_tool(
        &client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;
    assert_eq!(result["navigated"], true, "Expected navigated: {result}");
}

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
    assert!(
        result["entries"].is_array(),
        "Expected entries array: {result}"
    );
}

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
    assert!(
        result["bookmarks"].is_array(),
        "Expected bookmarks array: {result}"
    );
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
    assert!(
        result["bookmark"]["url"].is_string(),
        "Expected created bookmark: {result}"
    );
}

async fn navigate_example(client: &maho_browser_mcp::client::BrowserClient) {
    common::acquire_active_lease(client, 60).await;
    common::call_tool(
        client,
        "browser_navigate",
        json!({"url": "https://example.com"}),
    )
    .await;
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_page_content() {
    let client = common::connect_browser().await;
    navigate_example(&client).await;
    let result = common::call_tool(&client, "browser_page_content", json!({})).await;
    assert!(
        result["text"]
            .as_str()
            .unwrap_or("")
            .contains("Example Domain"),
        "Expected page content: {result}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_page_text() {
    let client = common::connect_browser().await;
    navigate_example(&client).await;
    let result = common::call_tool(&client, "browser_page_text", json!({})).await;
    assert!(
        result["text"]
            .as_str()
            .unwrap_or("")
            .contains("Example Domain"),
        "Expected plain text content: {result}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_search_in_page() {
    let client = common::connect_browser().await;
    navigate_example(&client).await;
    let result = common::call_tool(
        &client,
        "browser_search_in_page",
        json!({"query": "Example"}),
    )
    .await;
    assert!(
        result["match_count"].is_number(),
        "Expected match_count: {result}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_page_context() {
    let client = common::connect_browser().await;
    navigate_example(&client).await;
    let result = common::call_tool(&client, "browser_page_context", json!({})).await;
    assert!(
        result["content"]
            .as_str()
            .unwrap_or("")
            .contains("Example Domain"),
        "Expected page context content: {result}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_page_query_selector() {
    let client = common::connect_browser().await;
    navigate_example(&client).await;
    let result = common::call_tool(&client, "page_query_selector", json!({"selector": "h1"})).await;
    assert_eq!(result["tag"], "h1", "Expected h1 tag: {result}");
    assert!(result["ref_id"].is_string(), "Expected ref_id: {result}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_page_get_text() {
    let client = common::connect_browser().await;
    if !common::interactive_env() {
        return;
    }
    navigate_example(&client).await;
    let qs = common::call_tool(&client, "page_query_selector", json!({"selector": "p"})).await;
    let ref_id = qs["ref_id"].as_str().expect("ref_id");
    let result = common::call_tool(&client, "page_get_text", json!({"ref_id": ref_id})).await;
    assert!(result.is_object(), "Expected object result: {result}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_page_get_attribute() {
    let client = common::connect_browser().await;
    if !common::interactive_env() {
        return;
    }
    navigate_example(&client).await;
    let qs = common::call_tool(&client, "page_query_selector", json!({"selector": "a"})).await;
    let ref_id = qs["ref_id"].as_str().expect("ref_id");
    let result = common::call_tool(
        &client,
        "page_get_attribute",
        json!({"ref_id": ref_id, "attribute": "href"}),
    )
    .await;
    assert!(result.is_object(), "Expected object result: {result}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_page_wait_for_selector() {
    let client = common::connect_browser().await;
    navigate_example(&client).await;
    let result = common::call_tool(
        &client,
        "page_wait_for_selector",
        json!({"selector": "h1", "timeout_ms": 5000}),
    )
    .await;
    assert_eq!(result["found"], true, "Expected found: {result}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_accessibility_snapshot() {
    let client = common::connect_browser().await;
    navigate_example(&client).await;
    let result = common::call_tool(&client, "browser_accessibility_snapshot", json!({})).await;
    let text = common::extract_text_content(&result);
    assert!(
        text.contains("heading") || text.contains("link") || text.contains("Example"),
        "Expected accessibility tree content: {text}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_screenshot_full() {
    let client = common::connect_browser().await;
    if !common::interactive_env() {
        return;
    }
    navigate_example(&client).await;
    let result = common::call_tool(&client, "browser_screenshot_full", json!({})).await;
    assert_eq!(
        result["content_type"], "image/png",
        "Expected png: {result}"
    );
    let data = result["data"].as_str().expect("Expected base64 data");
    let decoded = base64_decode(data);
    assert!(decoded.len() > 8, "PNG data too short");
    assert_eq!(&decoded[0..4], &[0x89, 0x50, 0x4E, 0x47], "Not a valid PNG");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_network_start_capture() {
    let client = common::connect_browser().await;
    let result = common::call_tool(&client, "browser_network_start_capture", json!({})).await;
    assert_eq!(result["capturing"], true, "Expected capturing: {result}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_network_get_har() {
    let client = common::connect_browser().await;
    common::call_tool(&client, "browser_network_start_capture", json!({})).await;
    navigate_example(&client).await;
    let har = common::call_tool(&client, "browser_network_stop_capture", json!({})).await;
    assert_eq!(har["version"], "1.2", "Expected HAR version 1.2: {har}");
    assert!(har["entries"].is_array(), "Expected entries array: {har}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_network_stop_capture() {
    let client = common::connect_browser().await;
    common::call_tool(&client, "browser_network_start_capture", json!({})).await;
    let result = common::call_tool(&client, "browser_network_stop_capture", json!({})).await;
    assert!(
        result["version"].is_string() || result["entries"].is_array(),
        "Expected HAR result: {result}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_console_messages() {
    let client = common::connect_browser().await;
    let result = common::call_tool(
        &client,
        "browser_console_messages",
        json!({"tab_id": 0, "limit": 10}),
    )
    .await;
    assert!(
        result["messages"].is_array(),
        "Expected messages array: {result}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_wait_for_navigation() {
    let client = common::connect_browser().await;
    let result = common::call_tool(
        &client,
        "browser_wait_for_navigation",
        json!({"tab_id": 0, "timeout_ms": 1000}),
    )
    .await;
    assert!(
        result["navigated"].is_boolean(),
        "Expected navigated flag: {result}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_tab_switch() {
    let client = common::connect_browser().await;
    let id = common::active_tab_id(&client).await;
    let result = common::call_tool(&client, "browser_tab_switch", json!({"tab_id": id})).await;
    assert_eq!(result["activated"], true, "Expected activated: {result}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_set_viewport_size() {
    let client = common::connect_browser().await;
    let result = common::call_tool(
        &client,
        "browser_set_viewport_size",
        json!({"width_px": 800, "height_px": 600}),
    )
    .await;
    assert_eq!(result["width"], 800, "Expected width echoed: {result}");
    assert_eq!(result["height"], 600, "Expected height echoed: {result}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_browser_scroll() {
    let client = common::connect_browser().await;
    navigate_example(&client).await;
    let result = common::call_tool(
        &client,
        "browser_scroll",
        json!({"direction": "down", "pixels": 100}),
    )
    .await;
    assert_eq!(result["scrolled"], true, "Expected scrolled: {result}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser; interaction path unstable in headless"]
async fn test_browser_click() {
    let client = common::connect_browser().await;
    if !common::interactive_env() {
        return;
    }
    navigate_example(&client).await;
    let ax = common::call_tool(&client, "browser_accessibility_snapshot", json!({})).await;
    let click_ref = first_ref(&ax).unwrap_or(1);
    let result = common::call_tool(&client, "browser_click", json!({"ref": click_ref})).await;
    assert!(result.is_object(), "Expected object result: {result}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser; interaction path unstable in headless"]
async fn test_browser_key_press() {
    let client = common::connect_browser().await;
    if !common::interactive_env() {
        return;
    }
    common::acquire_active_lease(&client, 60).await;
    let result = common::call_tool(&client, "browser_key_press", json!({"key": "Enter"})).await;
    assert_eq!(
        result["key_pressed"], true,
        "Expected key_pressed: {result}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser; interaction path unstable in headless"]
async fn test_browser_hover() {
    let client = common::connect_browser().await;
    if !common::interactive_env() {
        return;
    }
    navigate_example(&client).await;
    let ax = common::call_tool(&client, "browser_accessibility_snapshot", json!({})).await;
    let hover_ref = first_ref(&ax).unwrap_or(1);
    let result = common::call_tool(&client, "browser_hover", json!({"ref": hover_ref})).await;
    assert!(result.is_object(), "Expected object result: {result}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser; interaction path unstable in headless"]
async fn test_browser_type() {
    let client = common::connect_browser().await;
    navigate_example(&client).await;
    let (code, _msg) = common::call_tool_expect_error(
        &client,
        "browser_type",
        json!({"ref": 999999, "text": "hello world"}),
    )
    .await;
    assert!(code != 0, "Expected error for missing ref");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser; interaction path unstable in headless"]
async fn test_browser_select() {
    let client = common::connect_browser().await;
    navigate_example(&client).await;
    let (code, _msg) = common::call_tool_expect_error(
        &client,
        "browser_select",
        json!({"ref": 999999, "value": "option1"}),
    )
    .await;
    assert!(code != 0, "Expected error for missing ref");
}

fn first_ref(ax: &serde_json::Value) -> Option<i64> {
    fn walk(v: &serde_json::Value) -> Option<i64> {
        if let Some(r) = v.get("ref").and_then(|r| r.as_i64()) {
            return Some(r);
        }
        if let Some(arr) = v.get("children").and_then(|c| c.as_array()) {
            for child in arr {
                if let Some(r) = walk(child) {
                    return Some(r);
                }
            }
        }
        None
    }
    walk(ax)
}

fn base64_decode(input: &str) -> Vec<u8> {
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
            continue;
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
