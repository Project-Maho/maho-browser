use serde_json::{json, Value};
use std::io::Write;
use std::path::PathBuf;
use std::process::{Command, Stdio};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixListener;

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

fn run_cli_with_stdin(
    args: &[&str],
    sock_path: &std::path::Path,
    stdin_data: &str,
) -> std::process::Output {
    let bin = env!("CARGO_BIN_EXE_maho");
    let mut child = Command::new(bin)
        .args(["--socket-path", sock_path.to_str().unwrap()])
        .args(args)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("failed to spawn CLI binary");

    if let Some(mut stdin) = child.stdin.take() {
        stdin.write_all(stdin_data.as_bytes()).unwrap();
    }

    child.wait_with_output().expect("failed to wait on CLI")
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_navigate_then_quit() {
    let nav_resp = json!({ "ok": true });
    let (_dir, sock_path, server) = make_mock_server(vec![nav_resp]).await;

    let output = run_cli_with_stdin(
        &["browser", "repl"],
        &sock_path,
        "navigate https://example.com\n.quit\n",
    );
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("ok"),
        "expected 'ok' in stdout, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_tabs_prints_list() {
    let tabs_resp = json!({
        "tabs": [
            {"id": 1, "title": "Test Page", "url": "https://test.com", "is_active": true},
            {"id": 2, "title": "Other", "url": "https://other.com", "is_active": false}
        ]
    });
    let (_dir, sock_path, server) = make_mock_server(vec![tabs_resp]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "tabs\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("Test Page"),
        "expected tab title in stdout, got: {stdout}"
    );
    assert!(
        stdout.contains("ID"),
        "expected header in stdout, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_help_prints_help() {
    let (_dir, sock_path, server) = make_mock_server(vec![]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "help\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("Commands:"),
        "expected help text, got: {stdout}"
    );
    assert!(
        stdout.contains("navigate"),
        "expected 'navigate' in help, got: {stdout}"
    );
    assert!(
        stdout.contains("screenshot"),
        "expected 'screenshot' in help, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn repl_unknown_command() {
    let (_dir, sock_path, server) = make_mock_server(vec![]).await;

    let output = run_cli_with_stdin(&["browser", "repl"], &sock_path, "foobar\n.quit\n");
    server.abort();

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("unknown command: foobar"),
        "expected unknown cmd msg, got: {stdout}"
    );
}
