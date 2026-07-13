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
async fn test_firewall_append_and_restore() {
    let prior_list = json!({ "domains": ["google.com", "*.google.com"] });
    let set_allowed_resp = json!({ "count": 4 });
    let new_tab_resp = json!({
        "tab": { "id": 1, "title": "Example", "url": "https://example.com" }
    });
    let restore_resp = json!({ "count": 2 });

    let (_dir, sock_path, server, requests) = make_mock_server(vec![
        prior_list,
        set_allowed_resp,
        new_tab_resp,
        restore_resp,
    ])
    .await;

    let output = run_cli(&["tab", "new", "--url", "https://example.com"], &sock_path);
    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    println!("stdout: {}", stdout);
    println!("stderr: {}", stderr);

    server.await.unwrap();

    let reqs = requests.lock().unwrap().clone();

    // Assert 5 total messages (1 initialize + 4 tool calls)
    assert_eq!(reqs.len(), 5);

    // reqs[1]: tools/call for browser_get_allowed_domains
    assert_eq!(reqs[1]["method"], "tools/call");
    assert_eq!(reqs[1]["params"]["name"], "browser_get_allowed_domains");

    // reqs[2]: tools/call for browser_set_allowed_domains with merged list
    assert_eq!(reqs[2]["method"], "tools/call");
    assert_eq!(reqs[2]["params"]["name"], "browser_set_allowed_domains");
    let set_domains: Vec<String> =
        serde_json::from_value(reqs[2]["params"]["arguments"]["domains"].clone()).unwrap();
    assert!(set_domains.contains(&"google.com".to_string()));
    assert!(set_domains.contains(&"*.google.com".to_string()));
    assert!(set_domains.contains(&"example.com".to_string()));
    assert!(set_domains.contains(&"*.example.com".to_string()));

    // reqs[3]: tools/call for browser_tab_new
    assert_eq!(reqs[3]["method"], "tools/call");
    assert_eq!(reqs[3]["params"]["name"], "browser_tab_new");

    // reqs[4]: tools/call for browser_set_allowed_domains with restored list
    assert_eq!(reqs[4]["method"], "tools/call");
    assert_eq!(reqs[4]["params"]["name"], "browser_set_allowed_domains");
    let restored_domains: Vec<String> =
        serde_json::from_value(reqs[4]["params"]["arguments"]["domains"].clone()).unwrap();
    assert!(restored_domains.contains(&"google.com".to_string()));
    assert!(restored_domains.contains(&"*.google.com".to_string()));
    assert_eq!(restored_domains.len(), 2);
}
