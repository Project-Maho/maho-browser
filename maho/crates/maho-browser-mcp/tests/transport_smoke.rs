// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(unix)]

//! Transport integration smoke test for the Maho Browser MCP socket server.
//!
//! These tests require a running Maho Browser instance with MCP enabled.
//! Run with: `cargo test --test transport_smoke -- --ignored`

use std::path::PathBuf;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixStream;

fn socket_path() -> PathBuf {
    if cfg!(target_os = "macos") {
        let home = std::env::var("HOME").expect("HOME not set");
        PathBuf::from(home).join("Library/Application Support/Maho/maho.sock")
    } else {
        // Linux: prefer XDG_RUNTIME_DIR.
        if let Ok(xdg) = std::env::var("XDG_RUNTIME_DIR") {
            PathBuf::from(xdg).join("maho-browser.sock")
        } else {
            let home = std::env::var("HOME").expect("HOME not set");
            PathBuf::from(home).join(".local/state/maho/maho.sock")
        }
    }
}

#[tokio::test]
#[ignore]
async fn initialize_handshake() -> anyhow::Result<()> {
    let path = socket_path();
    let stream = UnixStream::connect(&path).await?;
    let (reader, mut writer) = stream.into_split();
    let mut reader = BufReader::new(reader);

    // Send initialize request.
    let init_req = serde_json::json!({
        "jsonrpc": "2.0",
        "method": "initialize",
        "id": 1,
        "params": {
            "protocolVersion": "2025-03-26",
            "clientInfo": {
                "name": "maho-smoke-test",
                "version": "0.1.0"
            }
        }
    });
    let mut msg = serde_json::to_string(&init_req)?;
    msg.push('\n');
    writer.write_all(msg.as_bytes()).await?;

    // Read response.
    let mut response_line = String::new();
    reader.read_line(&mut response_line).await?;
    let response: serde_json::Value = serde_json::from_str(&response_line)?;

    assert_eq!(response["jsonrpc"], "2.0");
    assert_eq!(response["id"], 1);
    assert_eq!(response["result"]["serverInfo"]["name"], "maho-browser");
    assert_eq!(response["result"]["protocolVersion"], "2025-03-26");
    assert!(response["result"]["capabilities"]["tools"].is_object());

    Ok(())
}

#[tokio::test]
#[ignore]
async fn tools_list_returns_array() -> anyhow::Result<()> {
    let path = socket_path();
    let stream = UnixStream::connect(&path).await?;
    let (reader, mut writer) = stream.into_split();
    let mut reader = BufReader::new(reader);

    // Initialize first.
    let init_req = serde_json::json!({
        "jsonrpc": "2.0",
        "method": "initialize",
        "id": 1,
        "params": {
            "protocolVersion": "2025-03-26",
            "clientInfo": {"name": "smoke", "version": "0.1.0"}
        }
    });
    let mut msg = serde_json::to_string(&init_req)?;
    msg.push('\n');
    writer.write_all(msg.as_bytes()).await?;

    let mut line = String::new();
    reader.read_line(&mut line).await?;
    // Verify init succeeded.
    let init_resp: serde_json::Value = serde_json::from_str(&line)?;
    assert!(init_resp["result"].is_object());

    // Send tools/list.
    let tools_req = serde_json::json!({
        "jsonrpc": "2.0",
        "method": "tools/list",
        "id": 2
    });
    let mut msg2 = serde_json::to_string(&tools_req)?;
    msg2.push('\n');
    writer.write_all(msg2.as_bytes()).await?;

    let mut tools_line = String::new();
    reader.read_line(&mut tools_line).await?;
    let tools_resp: serde_json::Value = serde_json::from_str(&tools_line)?;

    assert_eq!(tools_resp["jsonrpc"], "2.0");
    assert_eq!(tools_resp["id"], 2);
    assert!(tools_resp["result"]["tools"].is_array());

    Ok(())
}

#[tokio::test]
#[ignore]
async fn hundred_cycles_no_leak() -> anyhow::Result<()> {
    let path = socket_path();

    for i in 0..100 {
        let stream = UnixStream::connect(&path).await?;
        let (reader, mut writer) = stream.into_split();
        let mut reader = BufReader::new(reader);

        let init_req = serde_json::json!({
            "jsonrpc": "2.0",
            "method": "initialize",
            "id": i,
            "params": {
                "protocolVersion": "2025-03-26",
                "clientInfo": {"name": "cycle-test", "version": "0.1.0"}
            }
        });
        let mut msg = serde_json::to_string(&init_req)?;
        msg.push('\n');
        writer.write_all(msg.as_bytes()).await?;

        let mut line = String::new();
        reader.read_line(&mut line).await?;
        let resp: serde_json::Value = serde_json::from_str(&line)?;
        assert!(resp["result"].is_object(), "Cycle {i} failed");

        drop(writer);
        drop(reader);
    }

    // Verify socket file still exists (no resource leak).
    assert!(
        std::fs::metadata(&path).is_ok(),
        "Socket path disappeared after 100 cycles"
    );

    Ok(())
}
