#![cfg(windows)]

use serde_json::{json, Value};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::windows::named_pipe::ServerOptions;

async fn navigate(refuse_lease: bool) {
    let pipe = format!(r"\\.\pipe\maho-lease-test-{}-{}", std::process::id(), refuse_lease);
    let server = ServerOptions::new().first_pipe_instance(true).create(&pipe).unwrap();
    let server_task = tokio::spawn(async move {
        server.connect().await.unwrap();
        let (reader, mut writer) = tokio::io::split(server);
        let mut reader = BufReader::new(reader);
        let mut calls = Vec::new();
        loop {
            let mut line = String::new();
            if reader.read_line(&mut line).await.unwrap() == 0 {
                break;
            }
            let request: Value = serde_json::from_str(&line).unwrap();
            let method = request["method"].as_str().unwrap();
            let name = request["params"]["name"].as_str().unwrap_or_default();
            let mut response = json!({"jsonrpc":"2.0", "id":request["id"]});
            if method == "initialize" {
                response["result"] = json!({"protocolVersion":"2025-03-26",
                    "serverInfo":{"name":"maho-browser","version":"0.1.0"},
                    "capabilities":{}});
            } else {
                calls.push(format!("{method} {name}"));
                if method == "maho/control/call" && name == "browser_acquire_lease" && refuse_lease {
                    response["error"] = json!({"code":-32003,"message":"lease refused"});
                } else if (method == "maho/control/call" && name == "browser_acquire_lease")
                    || (method == "tools/call" && name == "browser_navigate") {
                    response["result"] = json!({"content":[{"type":"text","text":"{}"}]});
                } else {
                    response["error"] = json!({"code":-32601,"message":"unknown tool"});
                }
            }
            let mut bytes = serde_json::to_vec(&response).unwrap();
            bytes.push(b'\n');
            writer.write_all(&bytes).await.unwrap();
        }
        calls
    });
    let output = tokio::process::Command::new(env!("CARGO_BIN_EXE_maho"))
        .args(["--socket-path", &pipe, "tab", "navigate", "7", "--url", "https://example.com"])
        .kill_on_drop(true)
        .output();
    let output = tokio::time::timeout(std::time::Duration::from_secs(30), output)
        .await.unwrap().unwrap();
    let calls = tokio::time::timeout(std::time::Duration::from_secs(5), server_task)
        .await.unwrap().unwrap();
    assert_eq!(output.status.success(), !refuse_lease, "{}", String::from_utf8_lossy(&output.stderr));
    let mut expected = vec!["maho/control/call browser_acquire_lease"];
    if !refuse_lease {
        expected.push("tools/call browser_navigate");
    }
    assert_eq!(calls, expected);
}

#[tokio::test]
async fn navigate_acquires_control_plane_lease() {
    navigate(false).await;
}

#[tokio::test]
async fn navigate_stops_after_lease_refusal() {
    navigate(true).await;
}
