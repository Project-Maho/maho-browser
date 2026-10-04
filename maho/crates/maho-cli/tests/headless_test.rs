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

async fn make_headless_mock_server() -> (tempfile::TempDir, PathBuf, tokio::task::JoinHandle<()>) {
    let dir = tempfile::tempdir().unwrap();
    let sock_path = dir.path().join("test.sock");
    let listener = UnixListener::bind(&sock_path).unwrap();

    let handle = tokio::spawn(async move {
        let (stream, _) = listener.accept().await.unwrap();
        let (reader, mut writer) = stream.into_split();
        let mut reader = BufReader::new(reader);
        let mut line = String::new();

        loop {
            line.clear();
            if reader.read_line(&mut line).await.unwrap() == 0 {
                break;
            }
            let req: Value = serde_json::from_str(&line).unwrap();
            let id = req["id"].as_i64().unwrap();
            let method = req["method"].as_str().unwrap_or("");

            let resp_data = if method == "initialize" {
                json!({
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "capabilities": {"tools": {}}
                })
            } else if method == "tools/call" {
                let tool_name = req["params"]["name"].as_str().unwrap_or("");
                let result = match tool_name {
                    "browser_get_allowed_domains" => json!({"domains": []}),
                    "browser_set_allowed_domains" => json!({"success": true}),
                    "browser_tab_new" => {
                        json!({"tab": {"id": 99, "title": "Example Domain", "url": "https://example.com", "is_active": false}})
                    }
                    "browser_tab_get" => {
                        json!({"id": 99, "title": "Example Domain", "url": "https://example.com", "is_active": false, "status": "complete"})
                    }
                    "browser_page_text" => json!({"text": "Hello, World!"}),
                    "browser_page_context" => json!({
                        "url": "https://example.com",
                        "title": "Example",
                        "content": "",
                        "links": ["https://a.com", "https://b.com"],
                        "meta_description": "Example Description",
                        "language": "en"
                    }),
                    "browser_tab_close" => json!({"success": true}),
                    _ => json!({"success": true}),
                };
                json!({
                    "content": [{"type": "text", "text": serde_json::to_string(&result).unwrap()}]
                })
            } else {
                json!({})
            };

            let resp = json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": resp_data
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
    let (_dir, sock_path, server) = make_headless_mock_server().await;

    let output = run_cli(
        &[
            "headless",
            "--url",
            "https://example.com",
            "--extract",
            "title",
        ],
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
            "headless",
            "--url",
            "https://example.com",
            "--extract",
            "title",
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
    let (_dir, sock_path, server) = make_headless_mock_server().await;

    let output = run_cli(
        &[
            "headless",
            "--url",
            "https://example.com",
            "--extract",
            "text",
        ],
        &sock_path,
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert_eq!(stdout.trim(), "Hello, World!");
    assert!(output.status.success());
}

#[tokio::test(flavor = "multi_thread")]
async fn headless_extract_links() {
    let (_dir, sock_path, server) = make_headless_mock_server().await;

    let output = run_cli(
        &[
            "headless",
            "--url",
            "https://example.com",
            "--extract",
            "links",
        ],
        &sock_path,
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("https://a.com"));
    assert!(stdout.contains("https://b.com"));
    assert!(output.status.success());
}
