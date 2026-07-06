use serde_json::{Value, json};
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

async fn make_headless_mock_server(
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

#[tokio::test(flavor = "multi_thread")]
async fn headless_extract_title() {
    // Responses: browser_tab_new, browser_tab_get (loaded), browser_tab_get (title extract), browser_tab_close
    let responses = vec![
        json!({"tab": {"id": 99, "title": "Example Domain", "url": "https://example.com", "is_active": false}}),
        json!({"id": 99, "title": "Example Domain", "url": "https://example.com", "is_active": false, "status": "complete"}),
        json!({"id": 99, "title": "Example Domain", "url": "https://example.com", "is_active": false, "status": "complete"}),
        json!({"success": true}),
    ];
    let (_dir, sock_path, server) = make_headless_mock_server(responses).await;

    let output = run_cli(
        &["headless", "--url", "https://example.com", "--extract", "title"],
        &sock_path,
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert_eq!(stdout, read_golden("headless_title.txt"));
    assert!(output.status.success());
}

#[tokio::test(flavor = "multi_thread")]
async fn headless_exits_1_when_no_browser() {
    let output = run_cli(
        &[
            "headless", "--url", "https://example.com", "--extract", "title",
        ],
        std::path::Path::new("/tmp/nonexistent-headless-test.sock"),
    );

    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("Browser not running"),
        "unexpected stderr: {stderr}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn headless_extract_text() {
    let responses = vec![
        json!({"tab": {"id": 99, "title": "Example", "url": "https://example.com", "is_active": false}}),
        json!({"id": 99, "title": "Example", "url": "https://example.com", "status": "complete"}),
        json!({"text": "Hello, World!"}),
        json!({"success": true}),
    ];
    let (_dir, sock_path, server) = make_headless_mock_server(responses).await;

    let output = run_cli(
        &["headless", "--url", "https://example.com", "--extract", "text"],
        &sock_path,
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert_eq!(stdout.trim(), "Hello, World!");
    assert!(output.status.success());
}

#[tokio::test(flavor = "multi_thread")]
async fn headless_extract_links() {
    let responses = vec![
        json!({"tab": {"id": 99, "title": "Example", "url": "https://example.com", "is_active": false}}),
        json!({"id": 99, "title": "Example", "url": "https://example.com", "status": "complete"}),
        json!({"url": "https://example.com", "title": "Example", "content": "", "links": ["https://a.com", "https://b.com"]}),
        json!({"success": true}),
    ];
    let (_dir, sock_path, server) = make_headless_mock_server(responses).await;

    let output = run_cli(
        &["headless", "--url", "https://example.com", "--extract", "links"],
        &sock_path,
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("https://a.com"));
    assert!(stdout.contains("https://b.com"));
    assert!(output.status.success());
}
