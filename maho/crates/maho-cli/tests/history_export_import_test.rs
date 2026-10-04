// Integration tests for `maho history export` (D3) and `maho history import`
// deprecation (D5).

use serde_json::{json, Value};
use std::path::PathBuf;
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

fn run_cli(args: &[&str], sock_path: &std::path::Path) -> std::process::Output {
    let bin = env!("CARGO_BIN_EXE_maho");
    std::process::Command::new(bin)
        .args(["--socket-path", sock_path.to_str().unwrap()])
        .args(args)
        .output()
        .expect("failed to run CLI binary")
}

// ─── history export (NDJSON) ─────────────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn history_export_ndjson_to_stdout() {
    let resp = json!({
        "entries": [
            {"url": "https://example.com/a", "title": "A", "visited_at": "2026-07-01T10:00:00Z"},
            {"url": "https://example.com/b", "title": "B", "visited_at": "2026-07-01T09:00:00Z"},
            {"url": "https://rust-lang.org", "title": "Rust", "visited_at": "2026-07-01T08:00:00Z"}
        ]
    });
    let (_dir, sock, server) = make_mock_server(vec![resp]).await;

    let output = run_cli(&["history", "export"], &sock);
    server.abort();

    assert!(
        output.status.success(),
        "stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    let stdout = String::from_utf8_lossy(&output.stdout);
    let lines: Vec<&str> = stdout.lines().filter(|l| !l.trim().is_empty()).collect();
    assert_eq!(lines.len(), 3, "expected 3 NDJSON rows, got: {stdout}");
    for l in &lines {
        let parsed: Value = serde_json::from_str(l).expect("each line must be valid JSON");
        assert!(parsed["url"].is_string(), "row missing url: {l}");
    }
}

#[tokio::test(flavor = "multi_thread")]
async fn history_export_ndjson_to_file() {
    let resp = json!({
        "entries": [
            {"url": "https://example.com/a", "title": "A", "visited_at": "2026-07-01T10:00:00Z"},
            {"url": "https://example.com/b", "title": "B", "visited_at": "2026-07-01T09:00:00Z"}
        ]
    });
    let (_dir, sock, server) = make_mock_server(vec![resp]).await;

    let out_dir = tempfile::tempdir().unwrap();
    let out_path = out_dir.path().join("history.ndjson");
    let output = run_cli(
        &["history", "export", "--out", out_path.to_str().unwrap()],
        &sock,
    );
    server.abort();

    assert!(
        output.status.success(),
        "stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    let content = std::fs::read_to_string(&out_path).expect("output file written");
    let lines: Vec<&str> = content.lines().filter(|l| !l.trim().is_empty()).collect();
    assert_eq!(lines.len(), 2, "expected 2 rows in file, got: {content}");
    for l in &lines {
        let _parsed: Value = serde_json::from_str(l).expect("valid JSON row");
    }
}

// ─── history import (unregistered) ───────────────────────────────────────────

#[tokio::test(flavor = "multi_thread")]
async fn history_import_is_not_a_registered_command() {
    // `history import` was registered in clap but its handler was a pure
    // hard-exit stub: the browser MCP server exposes no history-import tool,
    // so the command taught users a capability that does not exist. It is
    // now unregistered — clap rejects it at parse time, keeping the nonzero
    // exit while removing it from `maho history --help`.
    let import_dir = tempfile::tempdir().unwrap();
    let ndjson = import_dir.path().join("in.ndjson");
    std::fs::write(&ndjson, "{\"url\":\"https://x.com\"}\n").unwrap();

    let output = run_cli(
        &["history", "import", ndjson.to_str().unwrap()],
        std::path::Path::new("/tmp/nonexistent-import-test.sock"),
    );

    assert!(
        !output.status.success(),
        "expected nonzero exit for the unregistered import subcommand"
    );

    let help = run_cli(
        &["history", "--help"],
        std::path::Path::new("/tmp/nonexistent-import-test.sock"),
    );
    let help_stdout = String::from_utf8_lossy(&help.stdout);
    assert!(
        !help_stdout.contains("import"),
        "`maho history --help` must not advertise import, got: {help_stdout}"
    );
}
