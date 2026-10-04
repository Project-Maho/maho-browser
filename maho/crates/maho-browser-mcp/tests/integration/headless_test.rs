// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Integration tests for headless auto-launch orchestration.
//! All tests are `#[ignore]`d — they require the `maho` CLI binary available in PATH.

mod common;

use std::process::Stdio;
use std::time::Duration;
use tokio::process::Command;

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_headless_launches_browser_and_extracts_title() {
    // Spawn Maho.app in headless mode, wait for socket, extract page title
    let output = Command::new("maho")
        .args([
            "headless",
            "--url",
            "https://example.com",
            "--extract",
            "title",
        ])
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .output()
        .await
        .expect("Failed to run `maho headless`");

    assert!(
        output.status.success(),
        "maho headless exited with error: {}",
        String::from_utf8_lossy(&output.stderr)
    );

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("Example Domain"),
        "Expected page title in output: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_headless_no_zombie_processes() {
    // Run headless extraction
    let output = Command::new("maho")
        .args([
            "headless",
            "--url",
            "https://example.com",
            "--extract",
            "title",
        ])
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .output()
        .await
        .expect("Failed to run `maho headless`");

    assert!(output.status.success());

    // Brief pause to let cleanup happen
    tokio::time::sleep(Duration::from_secs(2)).await;

    // Check no lingering Maho processes from our headless run
    let ps_output = Command::new("pgrep")
        .args(["-f", "Maho.*--headless"])
        .output()
        .await
        .expect("Failed to run pgrep");

    let ps_stdout = String::from_utf8_lossy(&ps_output.stdout);
    assert!(
        ps_stdout.trim().is_empty(),
        "Found zombie Maho headless processes: {ps_stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_keep_alive_flag_leaves_browser_running() {
    // Spawn headless with --keep-alive
    let mut child = Command::new("maho")
        .args([
            "headless",
            "--url",
            "https://example.com",
            "--extract",
            "title",
            "--keep-alive",
        ])
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("Failed to spawn `maho headless --keep-alive`");

    // Wait for the CLI to finish (it should exit even with --keep-alive,
    // leaving the browser running)
    let output = tokio::time::timeout(Duration::from_secs(30), child.wait_with_output())
        .await
        .expect("Timed out waiting for maho headless")
        .expect("Failed to get output");

    assert!(output.status.success());

    // Check that the browser process is still running
    let socket_path = common::socket_path();
    let client_result = maho_browser_mcp::client::BrowserClient::connect_with_timeout(
        &socket_path,
        Duration::from_secs(5),
    )
    .await;

    assert!(
        client_result.is_ok(),
        "Browser should still be running with --keep-alive"
    );

    // Clean up: shut down the browser we left running
    if let Ok(client) = client_result {
        let _ = client.shutdown().await;
    }
}
