// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Integration tests for the REPL interactive session mode.
//! All tests are `#[ignore]`d — they require a running Maho browser instance.

mod common;

use std::process::Stdio;
use std::time::Duration;
use tokio::io::AsyncWriteExt;
use tokio::process::Command;

#[tokio::test(flavor = "multi_thread")]
#[ignore = "requires running browser"]
async fn test_repl_persistent_connection_survives_multiple_commands() {
    let mut child = Command::new("maho")
        .args(["repl"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("Failed to spawn `maho repl`");

    let stdin = child.stdin.as_mut().expect("Failed to open stdin");

    // Send multiple commands
    stdin.write_all(b"tabs\n").await.expect("write tabs");
    stdin
        .write_all(b"navigate https://example.com\n")
        .await
        .expect("write navigate");
    stdin.write_all(b"text\n").await.expect("write text");
    stdin.write_all(b".quit\n").await.expect("write quit");

    let output = tokio::time::timeout(Duration::from_secs(30), child.wait_with_output())
        .await
        .expect("Timed out waiting for maho repl")
        .expect("Failed to get output");

    assert!(
        output.status.success(),
        "maho repl exited with error: {}",
        String::from_utf8_lossy(&output.stderr)
    );

    let stdout = String::from_utf8_lossy(&output.stdout);

    // All commands should produce output — verify multiple responses came through
    // The exact format depends on implementation, but we should see output from each
    assert!(
        stdout.lines().count() >= 3,
        "Expected output from multiple commands, got: {stdout}"
    );

    // Verify navigate output contains something about the target
    assert!(
        stdout.contains("example.com") || stdout.contains("Example Domain"),
        "Expected evidence of navigation in REPL output: {stdout}"
    );
}
