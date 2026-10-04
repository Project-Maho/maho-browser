// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};

use maho_browser_mcp::client::BrowserClient;
use maho_browser_mcp::error::McpBridgeError;
use serde_json::{json, Value};
use tempfile::TempDir;
use tokio::time::{sleep, Duration};

const FIXTURE_TITLE: &str = "Maho Agent Action Test Fixture";
const TYPED_TEXT: &str = "Maho action QA text";
const SECRET_SENTINEL: &str = "S3NTINEL-owner-secret-do-not-log";

#[tokio::test(flavor = "multi_thread")]
#[ignore = "launches local Maho.app and drives the action fixture"]
async fn agent_browser_actions_fixture_e2e() {
    let maho = LaunchedMaho::start().await;
    let client = BrowserClient::connect_with_timeout(&maho.socket_path, Duration::from_secs(15))
        .await
        .expect("launched Maho socket connects");

    let tabs = wait_for_fixture_tab_list(&client).await;
    let tab_entries = tabs["tabs"]
        .as_array()
        .expect("browser_tab_list returns tabs array");
    assert!(
        !tab_entries.is_empty(),
        "browser_tab_list must return at least the fixture tab: {tabs}"
    );

    let fixture_tab = tab_entries
        .iter()
        .find(|tab| tab["targetable"] == true && tab["title"] == FIXTURE_TITLE)
        .unwrap_or_else(|| panic!("targetable fixture tab not found: {tabs}"));
    let tab_id = fixture_tab["id"]
        .as_i64()
        .expect("targetable tab has numeric id");
    assert!(tab_id > 0, "targetable tab id must be non-zero");
    assert!(
        fixture_tab["stable_id"]
            .as_str()
            .is_some_and(|id| !id.is_empty()),
        "targetable tab exposes stable_id"
    );

    let before =
        result_payload(call_tool(&client, "browser_page_text", json!({"tab_id": tab_id})).await);
    let before_text = before["text"].as_str().expect("page text is string");
    assert!(before_text.contains("Not clicked"));
    assert!(before_text.contains("Empty"));

    call_tool(
        &client,
        "browser_acquire_lease",
        json!({"tab_id": tab_id, "ttl_seconds": 60}),
    )
    .await;
    call_tool(
        &client,
        "browser_accessibility_snapshot",
        json!({"tab_id": tab_id}),
    )
    .await;

    assert_success(
        call_tool(
            &client,
            "browser_click",
            json!({"tab_id": tab_id, "ref": 1}),
        )
        .await,
        "clicked",
    );
    assert_success(
        call_tool(
            &client,
            "browser_type",
            json!({"tab_id": tab_id, "ref": 2, "text": TYPED_TEXT}),
        )
        .await,
        "typed",
    );
    assert_success(
        call_tool(
            &client,
            "browser_select",
            json!({"tab_id": tab_id, "ref": 4, "value": "jp"}),
        )
        .await,
        "selected",
    );
    assert_success(
        call_tool(
            &client,
            "browser_scroll",
            json!({"tab_id": tab_id, "direction": "down", "pixels": 160}),
        )
        .await,
        "scrolled",
    );
    assert_success(
        call_tool(
            &client,
            "browser_hover",
            json!({"tab_id": tab_id, "ref": 6}),
        )
        .await,
        "hovered",
    );
    assert_success(
        call_tool(
            &client,
            "browser_key_press",
            json!({"tab_id": tab_id, "key": "Enter"}),
        )
        .await,
        "key_pressed",
    );

    let (password_code, password_message) = call_tool_expect_error(
        &client,
        "browser_type",
        json!({"tab_id": tab_id, "ref": 3, "text": SECRET_SENTINEL}),
    )
    .await;
    assert_eq!(password_code, -32002);
    assert!(password_message.contains("Credential fields"));

    let after_text = wait_for_page_text(&client, tab_id, |text| {
        text.contains("Clicked!") && text.contains(TYPED_TEXT) && text.contains("jp")
    })
    .await;
    assert!(
        after_text.contains("Clicked!"),
        "click result missing: {after_text}"
    );
    assert!(
        after_text.contains(TYPED_TEXT),
        "typed text missing: {after_text}"
    );
    assert!(
        after_text.contains("jp"),
        "select value missing: {after_text}"
    );
    assert!(!after_text.contains(SECRET_SENTINEL));

    let contender = BrowserClient::connect_with_timeout(&maho.socket_path, Duration::from_secs(15))
        .await
        .expect("second launched Maho socket client connects");
    let (lease_code, lease_message) = call_tool_expect_error(
        &contender,
        "browser_scroll",
        json!({"tab_id": tab_id, "direction": "down", "pixels": 10}),
    )
    .await;
    assert_eq!(lease_code, -32007);
    assert!(lease_message.contains("lease required"));
}

struct LaunchedMaho {
    socket_path: PathBuf,
    child: Child,
    _profile: TempDir,
}

impl LaunchedMaho {
    async fn start() -> Self {
        let profile = tempfile::tempdir().expect("temp profile creates");
        let socket_path = profile.path().join("maho.sock");
        let log_path = profile.path().join("maho.log");
        let fixture_url = fixture_url();
        let log = std::fs::File::create(&log_path).expect("log file creates");
        let log_err = log.try_clone().expect("log file clones");
        let child = Command::new(maho_binary())
            .arg(format!("--user-data-dir={}", profile.path().display()))
            .arg("--enable-features=MahoAgentBrowserActions")
            .arg("--maho-disable-login-gate")
            .arg("--no-first-run")
            .arg("--no-default-browser-check")
            .arg("--new-window")
            .arg(fixture_url)
            .stdout(Stdio::from(log))
            .stderr(Stdio::from(log_err))
            .spawn()
            .expect("Maho.app launches");

        wait_for_socket(&socket_path, &log_path).await;

        Self {
            socket_path,
            child,
            _profile: profile,
        }
    }
}

impl Drop for LaunchedMaho {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

fn maho_binary() -> PathBuf {
    if let Some(path) = std::env::var_os("MAHO_APP_PATH") {
        return PathBuf::from(path);
    }
    workspace_root().join("chromium/src/out/Default/Maho.app/Contents/MacOS/Maho")
}

fn fixture_url() -> String {
    format!(
        "file://{}",
        workspace_root()
            .join(".omo/evidence/maho-agent-feature-catchup/local-action-fixture.html")
            .display()
    )
}

fn workspace_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .and_then(Path::parent)
        .expect("maho-browser-mcp lives under maho/crates")
        .to_path_buf()
}

async fn wait_for_socket(socket_path: &Path, log_path: &Path) {
    for _ in 0..30 {
        if socket_path.exists() {
            return;
        }
        sleep(Duration::from_millis(500)).await;
    }
    let log = std::fs::read_to_string(log_path).unwrap_or_default();
    panic!(
        "Maho socket did not appear at {}. Log:\n{log}",
        socket_path.display()
    );
}

async fn wait_for_fixture_tab_list(client: &maho_browser_mcp::client::BrowserClient) -> Value {
    let mut last = Value::Null;
    for _ in 0..30 {
        let tabs = result_payload(call_tool(client, "browser_tab_list", json!({})).await);
        if tabs["tabs"].as_array().is_some_and(|items| {
            items
                .iter()
                .any(|tab| tab["targetable"] == true && tab["title"] == FIXTURE_TITLE)
        }) {
            return tabs;
        }
        last = tabs;
        sleep(Duration::from_millis(500)).await;
    }
    panic!("targetable fixture tab did not appear: {last}");
}

async fn wait_for_page_text(
    client: &BrowserClient,
    tab_id: i64,
    predicate: impl Fn(&str) -> bool,
) -> String {
    let mut last = String::new();
    for _ in 0..20 {
        let page =
            result_payload(call_tool(client, "browser_page_text", json!({"tab_id": tab_id})).await);
        let text = page["text"].as_str().unwrap_or_default().to_string();
        if predicate(&text) {
            return text;
        }
        last = text;
        sleep(Duration::from_millis(250)).await;
    }
    last
}

async fn call_tool(client: &BrowserClient, name: &str, arguments: Value) -> Value {
    client
        .call_tool(name, arguments)
        .await
        .unwrap_or_else(|error| panic!("tools/call({name}) failed: {error}"))
}

async fn call_tool_expect_error(
    client: &BrowserClient,
    name: &str,
    arguments: Value,
) -> (i32, String) {
    match client.call_tool(name, arguments).await {
        Err(McpBridgeError::Rpc { code, message, .. }) => (code, message),
        Err(other) => panic!("expected RPC error for {name}, got: {other}"),
        Ok(value) => panic!("expected RPC error for {name}, got success: {value}"),
    }
}

fn result_payload(response: Value) -> Value {
    if let Some(text) = response
        .get("content")
        .and_then(Value::as_array)
        .and_then(|items| items.first())
        .and_then(|item| item.get("text"))
        .and_then(Value::as_str)
    {
        return serde_json::from_str(text).expect("content text is JSON");
    }
    response
}

fn assert_success(response: Value, key: &str) {
    let payload = result_payload(response);
    assert_eq!(payload[key], true, "expected {key}=true in {payload}");
}
