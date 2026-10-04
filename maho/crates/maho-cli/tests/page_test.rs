// Integration tests for the `maho page ...` command family (Task 3 / D1).
//
// Each test drives the CLI against a mock UDS JSON-RPC server that returns
// canned tool results, mirroring the pattern in tab_history_bookmarks_test.rs.
// The CLI talks directly to the (mocked) C++ UDS server, so the tool names
// asserted here are the raw socket tool names: browser_page_content,
// browser_page_text, browser_search_in_page, browser_page_context,
// page_query_selector, page_get_text, page_get_attribute,
// page_wait_for_selector, browser_screenshot_full.

use serde_json::{json, Value};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixListener;

/// A mock server that records every tools/call `name` it receives and replies
/// positionally from `responses` (in order). Returns the recorded tool names
/// via the shared `Vec` so tests can assert the call sequence (e.g. the
/// 2-call get-text/get-attribute chain).
async fn make_mock_server(
    responses: Vec<Value>,
) -> (
    tempfile::TempDir,
    PathBuf,
    tokio::task::JoinHandle<()>,
    Arc<Mutex<Vec<String>>>,
) {
    let dir = tempfile::tempdir().unwrap();
    let sock_path = dir.path().join("test.sock");
    let listener = UnixListener::bind(&sock_path).unwrap();
    let calls: Arc<Mutex<Vec<String>>> = Arc::new(Mutex::new(Vec::new()));
    let calls_task = Arc::clone(&calls);

    let handle = tokio::spawn(async move {
        let (stream, _) = listener.accept().await.unwrap();
        let (reader, mut writer) = stream.into_split();
        let mut reader = BufReader::new(reader);
        let mut line = String::new();

        // Respond to initialize.
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
            if let Some(name) = req["params"]["name"].as_str() {
                calls_task.lock().unwrap().push(name.to_string());
            }
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

    (dir, sock_path.clone(), handle, calls)
}

fn run_cli(args: &[&str], sock_path: &std::path::Path) -> std::process::Output {
    let bin = env!("CARGO_BIN_EXE_maho");
    std::process::Command::new(bin)
        .args(["--socket-path", sock_path.to_str().unwrap()])
        .args(args)
        .output()
        .expect("failed to run CLI binary")
}

// ─── page content ───────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn page_content_prints_text() {
    let resp = json!({"text": "# Hello\n\nBody text here."});
    let (_dir, sock, server, calls) = make_mock_server(vec![resp]).await;

    let output = run_cli(&["page", "content"], &sock);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        output.status.success(),
        "stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert!(stdout.contains("# Hello"), "stdout: {stdout}");
    assert_eq!(calls.lock().unwrap().as_slice(), ["browser_page_content"]);
}

#[tokio::test(flavor = "multi_thread")]
async fn page_content_tab_flag_maps_tab_id() {
    let resp = json!({"text": "body"});
    let (_dir, sock, server, _calls) = make_mock_server(vec![resp]).await;

    let output = run_cli(&["page", "content", "--tab", "7"], &sock);
    server.abort();

    assert!(output.status.success());
}

#[tokio::test(flavor = "multi_thread")]
async fn page_content_json_output() {
    let resp = json!({"text": "body"});
    let (_dir, sock, server, _calls) = make_mock_server(vec![resp]).await;

    let output = run_cli(&["--json", "page", "content"], &sock);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    let parsed: Value = serde_json::from_str(&stdout).unwrap();
    assert_eq!(parsed["text"], "body");
}

// ─── page text ────────────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn page_text_prints_plaintext() {
    let resp = json!({"text": "Plain body text."});
    let (_dir, sock, server, calls) = make_mock_server(vec![resp]).await;

    let output = run_cli(&["page", "text"], &sock);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("Plain body text."), "stdout: {stdout}");
    assert_eq!(calls.lock().unwrap().as_slice(), ["browser_page_text"]);
}

// ─── page search ──────────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn page_search_reports_match_count() {
    let resp = json!({"match_count": 3, "active_match_index": 1});
    let (_dir, sock, server, calls) = make_mock_server(vec![resp]).await;

    let output = run_cli(&["page", "search", "--query", "hello"], &sock);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        output.status.success(),
        "stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert!(stdout.contains('3'), "expected match count in: {stdout}");
    assert_eq!(calls.lock().unwrap().as_slice(), ["browser_search_in_page"]);
}

// ─── page context ─────────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn page_context_prints_url_title() {
    let resp = json!({"url": "https://example.com", "title": "Example", "content": "hi"});
    let (_dir, sock, server, calls) = make_mock_server(vec![resp]).await;

    let output = run_cli(&["page", "context"], &sock);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("https://example.com"), "stdout: {stdout}");
    assert!(stdout.contains("Example"), "stdout: {stdout}");
    assert_eq!(calls.lock().unwrap().as_slice(), ["browser_page_context"]);
}

// ─── page query-selector ────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn page_query_selector_prints_ref_and_tag() {
    let resp = json!({"ref_id": "abc123", "tag": "h1"});
    let (_dir, sock, server, calls) = make_mock_server(vec![resp]).await;

    let output = run_cli(&["page", "query-selector", "h1"], &sock);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("abc123"), "stdout: {stdout}");
    assert!(stdout.contains("h1"), "stdout: {stdout}");
    assert_eq!(calls.lock().unwrap().as_slice(), ["page_query_selector"]);
}

// ─── page wait-for-selector ──────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn page_wait_for_selector_reports_found() {
    let resp = json!({"found": true});
    let (_dir, sock, server, calls) = make_mock_server(vec![resp]).await;

    let output = run_cli(
        &["page", "wait-for-selector", "#main", "--timeout", "1000"],
        &sock,
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("true"), "stdout: {stdout}");
    assert_eq!(calls.lock().unwrap().as_slice(), ["page_wait_for_selector"]);
}

// ─── page get-text (2-call chain) ────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn page_get_text_two_call_chain() {
    // First response: query-selector returns ref_id; second: get_text returns text.
    let responses = vec![
        json!({"ref_id": "ref-42", "tag": "p"}),
        json!({"text": "Extracted text"}),
    ];
    let (_dir, sock, server, calls) = make_mock_server(responses).await;

    let output = run_cli(&["page", "get-text", "p.intro"], &sock);
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        output.status.success(),
        "stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert!(stdout.contains("Extracted text"), "stdout: {stdout}");
    assert_eq!(
        calls.lock().unwrap().as_slice(),
        ["page_query_selector", "page_get_text"]
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn page_get_text_empty_ref_errors() {
    // query-selector returns an empty ref_id => element not found; CLI must
    // error out clearly and NOT issue a second call.
    let responses = vec![json!({"ref_id": "", "tag": ""})];
    let (_dir, sock, server, calls) = make_mock_server(responses).await;

    let output = run_cli(&["page", "get-text", ".missing"], &sock);
    server.abort();

    assert!(!output.status.success(), "expected nonzero exit");
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.to_lowercase().contains("not found") || stderr.to_lowercase().contains("no element"),
        "unexpected stderr: {stderr}"
    );
    // Only the query-selector call should have been made.
    assert_eq!(calls.lock().unwrap().as_slice(), ["page_query_selector"]);
}

// ─── page get-attribute (2-call chain) ───────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn page_get_attribute_two_call_chain() {
    let responses = vec![
        json!({"ref_id": "ref-7", "tag": "a"}),
        json!({"value": "https://target.example"}),
    ];
    let (_dir, sock, server, calls) = make_mock_server(responses).await;

    let output = run_cli(
        &["page", "get-attribute", "a.link", "--attr", "href"],
        &sock,
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        output.status.success(),
        "stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert!(
        stdout.contains("https://target.example"),
        "stdout: {stdout}"
    );
    assert_eq!(
        calls.lock().unwrap().as_slice(),
        ["page_query_selector", "page_get_attribute"]
    );
}

// ─── page screenshot ──────────────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn page_screenshot_writes_png_file() {
    // "iVBORw0KGgo=" is base64 for the 8-byte PNG magic header.
    let resp = json!({"data": "iVBORw0KGgo=", "mime_type": "image/png"});
    let (_dir, sock, server, calls) = make_mock_server(vec![resp]).await;

    let out_dir = tempfile::tempdir().unwrap();
    let out_path = out_dir.path().join("shot.png");
    let output = run_cli(
        &["page", "screenshot", "--out", out_path.to_str().unwrap()],
        &sock,
    );
    server.abort();

    assert!(
        output.status.success(),
        "stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(
        calls.lock().unwrap().as_slice(),
        ["browser_screenshot_full"]
    );

    let bytes = std::fs::read(&out_path).expect("screenshot file should be written");
    assert!(bytes.len() >= 8, "file too small: {} bytes", bytes.len());
    assert_eq!(
        &bytes[0..8],
        &[0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A],
        "file does not start with PNG magic"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn page_screenshot_writes_png_from_capability_envelope() {
    let resp = json!({
        "result": {"data": "iVBORw0KGgo=", "content_type": "image/png"}
    });
    let (_dir, sock, server, calls) = make_mock_server(vec![resp]).await;

    let out_dir = tempfile::tempdir().unwrap();
    let out_path = out_dir.path().join("shot.png");
    let output = run_cli(
        &["page", "screenshot", "--out", out_path.to_str().unwrap()],
        &sock,
    );
    server.abort();

    assert!(
        output.status.success(),
        "stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(
        calls.lock().unwrap().as_slice(),
        ["browser_screenshot_full"]
    );
    assert_eq!(
        std::fs::read(&out_path).expect("screenshot file should be written"),
        [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]
    );
}
