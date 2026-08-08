use serde_json::{json, Value};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixListener;

async fn make_mock_server(
    responses: Vec<Value>,
) -> (
    tempfile::TempDir,
    PathBuf,
    tokio::task::JoinHandle<()>,
    Arc<Mutex<Vec<Value>>>,
) {
    let dir = tempfile::tempdir().unwrap();
    let sock_path = dir.path().join("test.sock");
    let listener = UnixListener::bind(&sock_path).unwrap();
    let requests = Arc::new(Mutex::new(Vec::new()));
    let requests_clone = Arc::clone(&requests);

    let handle = tokio::spawn(async move {
        let (stream, _) = listener.accept().await.unwrap();
        let (reader, mut writer) = stream.into_split();
        let mut reader = BufReader::new(reader);
        let mut line = String::new();

        // 1. Respond to initialize
        if reader.read_line(&mut line).await.is_err() {
            return;
        }
        if line.is_empty() {
            return;
        }
        let req: Value = serde_json::from_str(&line).unwrap();
        requests_clone.lock().unwrap().push(req.clone());
        let id = req["id"].as_i64().unwrap();
        let init_resp = json!({
            "jsonrpc": "2.0",
            "id": id,
            "result": {
                "protocolVersion": "2025-03-26",
                "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                "capabilities": {"tools": {}}
            }
        });
        let mut buf = serde_json::to_vec(&init_resp).unwrap();
        buf.push(b'\n');
        writer.write_all(&buf).await.unwrap();

        // 2. Respond to subsequent tools/call RPCs
        for response_val in responses {
            line.clear();
            if reader.read_line(&mut line).await.is_err() {
                break;
            }
            if line.is_empty() {
                break;
            }
            let req: Value = serde_json::from_str(&line).unwrap();
            requests_clone.lock().unwrap().push(req.clone());
            let id = req["id"].as_i64().unwrap();
            let resp = json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": {
                    "content": [{"type": "text", "text": serde_json::to_string(&response_val).unwrap()}]
                }
            });
            let mut buf = serde_json::to_vec(&resp).unwrap();
            buf.push(b'\n');
            writer.write_all(&buf).await.unwrap();
        }
    });

    (dir, sock_path, handle, requests)
}

fn run_cli(args: &[&str], sock_path: &std::path::Path) -> std::process::Output {
    let bin = env!("CARGO_BIN_EXE_maho");
    std::process::Command::new(bin)
        .args(["--socket-path", sock_path.to_str().unwrap()])
        .args(args)
        .output()
        .expect("failed to run CLI binary")
}

#[tokio::test(flavor = "multi_thread")]
async fn test_tab_new_does_not_touch_domain_lists() {
    let new_tab_resp = json!({
        "tab": { "id": 1, "title": "Example", "url": "https://example.com" }
    });

    let (_dir, sock_path, server, requests) = make_mock_server(vec![new_tab_resp]).await;

    let output = run_cli(&["tab", "new", "--url", "https://example.com"], &sock_path);
    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    println!("stdout: {}", stdout);
    println!("stderr: {}", stderr);

    server.await.unwrap();

    let reqs = requests.lock().unwrap().clone();

    assert_eq!(reqs.len(), 2);
    assert_eq!(reqs[1]["method"], "tools/call");
    assert_eq!(reqs[1]["params"]["name"], "browser_tab_new");

    for r in &reqs {
        let name = r["params"]["name"].as_str().unwrap_or("");
        assert!(
            !name.contains("allowed_domains") && !name.contains("blocked_domains"),
            "default-allow CLI must not touch domain lists, saw: {name}"
        );
    }
}
