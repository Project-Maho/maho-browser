// These tests serve the CLI over a Unix domain socket; Windows uses a named pipe.
#![cfg(unix)]

use serde_json::{json, Value};
use std::path::PathBuf;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixListener;

fn golden_path(name: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("tests/golden")
        .join(name)
}

fn read_golden(name: &str) -> String {
    std::fs::read_to_string(golden_path(name)).unwrap()
}

async fn make_mock_server(
    responses: Vec<Value>,
) -> (tempfile::TempDir, PathBuf, tokio::task::JoinHandle<()>) {
    let dir = tempfile::tempdir().unwrap();
    let sock_path = dir.path().join("test.sock");
    let listener = UnixListener::bind(&sock_path).unwrap();

    let handle = tokio::spawn(async move {
        let (stream, _) = listener.accept().await.unwrap();
        let (reader, mut writer) = stream.into_split();
        let mut reader = BufReader::new(reader);
        let mut line = String::new();

        // Respond to initialize
        reader.read_line(&mut line).await.unwrap();
        let req: Value = serde_json::from_str(&line).unwrap();
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

        // Respond to subsequent calls with the provided responses
        for response_result in responses {
            line.clear();
            reader.read_line(&mut line).await.unwrap();
            let req: Value = serde_json::from_str(&line).unwrap();
            let id = req["id"].as_i64().unwrap();
            let resp = json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": {
                    "content": [{"type": "text", "text": serde_json::to_string(&response_result).unwrap()}]
                }
            });
            let mut buf = serde_json::to_vec(&resp).unwrap();
            buf.push(b'\n');
            writer.write_all(&buf).await.unwrap();
        }
    });

    (dir, sock_path.clone(), handle)
}

fn run_cli(args: &[&str], sock_path: &std::path::Path) -> std::process::Output {
    let bin = env!("CARGO_BIN_EXE_maho");
    std::process::Command::new(bin)
        .args(["--socket-path", sock_path.to_str().unwrap()])
        .args(args)
        .output()
        .expect("failed to run CLI binary")
}

// ─── Tab Info ─────────────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn tab_info_human_output() {
    let tab_list_resp = json!({
        "tabs": [
            {"id": 1, "title": "Example Domain", "url": "https://example.com", "is_active": true}
        ]
    });
    let (_dir, sock_path, server) = make_mock_server(vec![tab_list_resp]).await;

    let output = run_cli(&["tab", "info"], &sock_path);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert_eq!(stdout, read_golden("tab_info.txt"));
}

#[tokio::test(flavor = "multi_thread")]
async fn tab_info_json_output() {
    let tab_list_resp = json!({
        "tabs": [
            {"id": 1, "title": "Example Domain", "url": "https://example.com", "is_active": true}
        ]
    });
    let (_dir, sock_path, server) = make_mock_server(vec![tab_list_resp]).await;

    let output = run_cli(&["--json", "tab", "info"], &sock_path);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    let parsed: Value = serde_json::from_str(&stdout).unwrap();
    assert_eq!(parsed["id"], 1);
    assert_eq!(parsed["title"], "Example Domain");
    assert_eq!(parsed["is_active"], true);
}

// ─── Tab List ─────────────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn tab_list_human_output() {
    let tab_list_resp = json!({
        "tabs": [
            {"id": 1, "title": "Example Domain", "url": "https://example.com", "is_active": true},
            {"id": 2, "title": "Rust Programming Language", "url": "https://www.rust-lang.org", "is_active": false}
        ]
    });
    let (_dir, sock_path, server) = make_mock_server(vec![tab_list_resp]).await;

    let output = run_cli(&["tab", "list"], &sock_path);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert_eq!(stdout, read_golden("tab_list.txt"));
}

#[tokio::test(flavor = "multi_thread")]
async fn tab_list_json_output() {
    let tab_list_resp = json!({
        "tabs": [
            {"id": 1, "title": "Example Domain", "url": "https://example.com", "is_active": true}
        ]
    });
    let (_dir, sock_path, server) = make_mock_server(vec![tab_list_resp]).await;

    let output = run_cli(&["--json", "tab", "list"], &sock_path);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    let parsed: Value = serde_json::from_str(&stdout).unwrap();
    assert!(parsed["tabs"].is_array());
}

// ─── History List ─────────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn history_list_grouped_output() {
    let history_resp = json!({
        "entries": [
            {"url": "https://example.com/page1", "title": "Example Domain", "visited_at": "2026-07-01T10:00:00Z", "visit_count": 5},
            {"url": "https://example.com/page2", "title": "Example Other", "visited_at": "2026-07-01T09:00:00Z", "visit_count": 3},
            {"url": "https://rust-lang.org/learn", "title": "Rust Lang", "visited_at": "2026-07-01T08:00:00Z", "visit_count": 1}
        ]
    });
    let (_dir, sock_path, server) = make_mock_server(vec![history_resp]).await;

    let output = run_cli(&["history", "list", "--domain"], &sock_path);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert_eq!(stdout, read_golden("history_list_grouped.txt"));
}

#[tokio::test(flavor = "multi_thread")]
async fn history_list_flat_output() {
    let history_resp = json!({
        "entries": [
            {"url": "https://example.com/page1", "title": "Example Domain", "visited_at": "2026-07-01T10:00:00Z", "visit_count": 5},
            {"url": "https://example.com/page2", "title": "Example Other", "visited_at": "2026-07-01T09:00:00Z", "visit_count": 3},
            {"url": "https://rust-lang.org/learn", "title": "Rust Lang", "visited_at": "2026-07-01T08:00:00Z", "visit_count": 1}
        ]
    });
    let (_dir, sock_path, server) = make_mock_server(vec![history_resp]).await;

    let output = run_cli(&["history", "list"], &sock_path);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert_eq!(stdout, read_golden("history_list_flat.txt"));
}

#[tokio::test(flavor = "multi_thread")]
async fn history_list_json_output() {
    let history_resp = json!({
        "entries": [
            {"url": "https://example.com/page1", "title": "Example Domain", "visited_at": "2026-07-01T10:00:00Z", "visit_count": 5}
        ]
    });
    let (_dir, sock_path, server) = make_mock_server(vec![history_resp]).await;

    let output = run_cli(&["--json", "history", "list"], &sock_path);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    let parsed: Value = serde_json::from_str(&stdout).unwrap();
    assert!(parsed["entries"].is_array());
}

// ─── Bookmarks List ───────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn bookmarks_list_human_output() {
    let bookmarks_resp = json!({
        "bookmarks": [
            {"id": 1, "title": "Rust Documentation", "url": "https://doc.rust-lang.org", "folder": null},
            {"id": 2, "title": "The Book", "url": "https://doc.rust-lang.org/book", "folder": "Learning"}
        ]
    });
    let (_dir, sock_path, server) = make_mock_server(vec![bookmarks_resp]).await;

    let output = run_cli(&["bookmarks", "list"], &sock_path);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert_eq!(stdout, read_golden("bookmarks_list.txt"));
}

#[tokio::test(flavor = "multi_thread")]
async fn bookmarks_search_human_output() {
    let bookmarks_resp = json!({
        "bookmarks": [
            {"id": 1, "title": "Rust Documentation", "url": "https://doc.rust-lang.org", "folder": null}
        ]
    });
    let (_dir, sock_path, server) = make_mock_server(vec![bookmarks_resp]).await;

    let output = run_cli(&["bookmarks", "search", "rust"], &sock_path);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert_eq!(stdout, read_golden("bookmarks_search.txt"));
}

#[tokio::test(flavor = "multi_thread")]
async fn bookmarks_list_json_output() {
    let bookmarks_resp = json!({
        "bookmarks": [
            {"id": 1, "title": "Rust Documentation", "url": "https://doc.rust-lang.org", "folder": null}
        ]
    });
    let (_dir, sock_path, server) = make_mock_server(vec![bookmarks_resp]).await;

    let output = run_cli(&["--json", "bookmarks", "list"], &sock_path);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    let parsed: Value = serde_json::from_str(&stdout).unwrap();
    assert!(parsed["bookmarks"].is_array());
}

// ─── Connection failure ───────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn tab_info_fails_when_no_browser() {
    let output = run_cli(
        &["tab", "info"],
        std::path::Path::new("/tmp/nonexistent-maho-test-sock.sock"),
    );
    assert!(!output.status.success());
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("Browser not running") || stderr.contains("Failed to connect"),
        "unexpected stderr: {stderr}"
    );
}

// ─── Tab Navigate lease path ─────────────────────────────────────────────────

/// Serves initialize, then answers each request with `reply(method, name)` and
/// records "<method> <name>" so tests can assert the exact call sequence.
async fn make_recording_server(
    reply: fn(&str, &str) -> Result<Value, (i64, &'static str)>,
) -> (
    tempfile::TempDir,
    PathBuf,
    tokio::task::JoinHandle<()>,
    std::sync::Arc<std::sync::Mutex<Vec<String>>>,
) {
    let dir = tempfile::tempdir().unwrap();
    let sock_path = dir.path().join("test.sock");
    let listener = UnixListener::bind(&sock_path).unwrap();
    let calls = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
    let calls_task = std::sync::Arc::clone(&calls);

    let handle = tokio::spawn(async move {
        let (stream, _) = listener.accept().await.unwrap();
        let (reader, mut writer) = stream.into_split();
        let mut reader = BufReader::new(reader);
        let mut line = String::new();
        loop {
            line.clear();
            if reader.read_line(&mut line).await.unwrap_or(0) == 0 {
                break;
            }
            let req: Value = serde_json::from_str(&line).unwrap();
            let id = req["id"].clone();
            let method = req["method"].as_str().unwrap_or_default().to_string();
            let resp = if method == "initialize" {
                json!({"jsonrpc": "2.0", "id": id, "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "capabilities": {"tools": {}}
                }})
            } else {
                let name = req["params"]["name"].as_str().unwrap_or_default().to_string();
                calls_task.lock().unwrap().push(format!("{method} {name}"));
                match reply(&method, &name) {
                    Ok(result) => json!({"jsonrpc": "2.0", "id": id, "result": {
                        "content": [{"type": "text", "text": result.to_string()}]
                    }}),
                    Err((code, message)) => json!({"jsonrpc": "2.0", "id": id,
                        "error": {"code": code, "message": message}}),
                }
            };
            let mut buf = serde_json::to_vec(&resp).unwrap();
            buf.push(b'\n');
            writer.write_all(&buf).await.unwrap();
        }
    });

    (dir, sock_path, handle, calls)
}

#[tokio::test(flavor = "multi_thread")]
async fn tab_navigate_acquires_lease_on_the_control_plane() {
    // The browser rejects lease capabilities on tools/call (-32601) and gates
    // browser_navigate on a held lease, so the lease must go through control/call.
    let (_dir, sock_path, server, calls) = make_recording_server(|method, name| {
        match (method, name) {
            ("maho/control/call", "browser_acquire_lease") => Ok(json!({"acquired": true})),
            ("tools/call", "browser_navigate") => Ok(json!({"navigated": true})),
            _ => Err((-32601, "unknown tool")),
        }
    })
    .await;

    let output = run_cli(
        &["tab", "navigate", "7", "--url", "https://example.com"],
        &sock_path,
    );
    server.abort();

    assert!(
        output.status.success(),
        "stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(
        calls.lock().unwrap().as_slice(),
        [
            "maho/control/call browser_acquire_lease",
            "tools/call browser_navigate"
        ]
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn tab_navigate_stops_when_the_lease_is_refused() {
    let (_dir, sock_path, server, calls) = make_recording_server(|_, name| {
        if name == "browser_acquire_lease" {
            Err((-32003, "lease held by another session"))
        } else {
            Ok(json!({"navigated": true}))
        }
    })
    .await;

    let output = run_cli(
        &["tab", "navigate", "7", "--url", "https://example.com"],
        &sock_path,
    );
    server.abort();

    assert!(!output.status.success());
    assert!(
        !calls
            .lock()
            .unwrap()
            .iter()
            .any(|call| call.ends_with("browser_navigate")),
        "navigate must not run without a lease: {:?}",
        calls.lock().unwrap()
    );
}
