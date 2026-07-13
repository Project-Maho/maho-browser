// Integration tests for `maho headless --batch` (D4).
//
// Batch mode connects once per URL (concurrency-bounded by --concurrency),
// runs extraction, and emits one NDJSON row per URL. This mock server accepts
// multiple connections, dispatches replies by tool name (so interleaving is
// safe), and records the peak number of simultaneous connections to prove the
// concurrency bound is honored.

use serde_json::{json, Value};
use std::path::PathBuf;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::time::Duration;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixListener;

struct MockStats {
    active: AtomicUsize,
    peak: AtomicUsize,
}

async fn make_batch_mock() -> (
    tempfile::TempDir,
    PathBuf,
    tokio::task::JoinHandle<()>,
    Arc<MockStats>,
) {
    let dir = tempfile::tempdir().unwrap();
    let sock_path = dir.path().join("test.sock");
    let listener = UnixListener::bind(&sock_path).unwrap();
    let stats = Arc::new(MockStats {
        active: AtomicUsize::new(0),
        peak: AtomicUsize::new(0),
    });
    let stats_task = Arc::clone(&stats);

    let handle = tokio::spawn(async move {
        loop {
            let (stream, _) = match listener.accept().await {
                Ok(s) => s,
                Err(_) => break,
            };
            let stats_conn = Arc::clone(&stats_task);
            tokio::spawn(async move {
                let (reader, mut writer) = stream.into_split();
                let mut reader = BufReader::new(reader);
                let mut line = String::new();

                loop {
                    line.clear();
                    match reader.read_line(&mut line).await {
                        Ok(0) => break,
                        Ok(_) => {}
                        Err(_) => break,
                    }
                    let req: Value = match serde_json::from_str(&line) {
                        Ok(v) => v,
                        Err(_) => break,
                    };
                    let id = req["id"].as_i64().unwrap_or(0);
                    let method = req["method"].as_str().unwrap_or("");

                    let reply: Value = if method == "initialize" {
                        json!({
                            "jsonrpc": "2.0",
                            "id": id,
                            "result": {
                                "protocolVersion": "2025-03-26",
                                "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                                "capabilities": {"tools": {}}
                            }
                        })
                    } else {
                        // Count in-flight tool calls. Each per-URL task holds its
                        // semaphore permit across its entire lifetime, so the peak
                        // number of simultaneous tool calls cannot exceed the
                        // configured --concurrency bound (no connection-teardown
                        // race, unlike connection-level counting).
                        let cur = stats_conn.active.fetch_add(1, Ordering::SeqCst) + 1;
                        stats_conn.peak.fetch_max(cur, Ordering::SeqCst);

                        let tool = req["params"]["name"].as_str().unwrap_or("");
                        let result_obj: Value = match tool {
                            "browser_set_allowed_domains" => json!({"success": true}),
                            "browser_tab_new" => {
                                // Widen the concurrency window so overlapping
                                // tasks are observable.
                                tokio::time::sleep(Duration::from_millis(120)).await;
                                json!({"tab": {"id": 1, "title": "Batch Title", "url": "u", "is_active": false}})
                            }
                            "browser_tab_get" => json!({
                                "id": 1, "title": "Batch Title", "url": "u", "status": "complete"
                            }),
                            "browser_page_text" => json!({"text": "batch body"}),
                            "browser_page_context" => {
                                json!({"url": "u", "title": "Batch Title", "content": "", "links": []})
                            }
                            "browser_tab_close" => json!({"success": true}),
                            _ => json!({}),
                        };
                        let out = json!({
                            "jsonrpc": "2.0",
                            "id": id,
                            "result": {
                                "content": [{"type": "text", "text": serde_json::to_string(&result_obj).unwrap()}]
                            }
                        });
                        stats_conn.active.fetch_sub(1, Ordering::SeqCst);
                        out
                    };

                    let mut buf = serde_json::to_vec(&reply).unwrap();
                    buf.push(b'\n');
                    if writer.write_all(&buf).await.is_err() {
                        break;
                    }
                }
            });
        }
    });

    (dir, sock_path.clone(), handle, stats)
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
async fn headless_batch_ndjson_rows_match_urls() {
    let (_dir, sock, server, _stats) = make_batch_mock().await;

    let work = tempfile::tempdir().unwrap();
    let urls_file = work.path().join("urls.txt");
    std::fs::write(
        &urls_file,
        "https://a.example\nhttps://b.example\nhttps://c.example\n",
    )
    .unwrap();
    let out_file = work.path().join("out.ndjson");

    let output = run_cli(
        &[
            "headless",
            "--batch",
            urls_file.to_str().unwrap(),
            "--extract",
            "title",
            "--out",
            out_file.to_str().unwrap(),
            "--concurrency",
            "2",
        ],
        &sock,
    );
    server.abort();

    assert!(
        output.status.success(),
        "stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    let content = std::fs::read_to_string(&out_file).expect("out file written");
    let mut urls: Vec<String> = Vec::new();
    for l in content.lines().filter(|l| !l.trim().is_empty()) {
        let row: Value = serde_json::from_str(l).expect("valid NDJSON row");
        assert_eq!(row["ok"], json!(true), "row not ok: {l}");
        urls.push(row["url"].as_str().unwrap().to_string());
    }
    urls.sort();
    assert_eq!(
        urls,
        vec![
            "https://a.example".to_string(),
            "https://b.example".to_string(),
            "https://c.example".to_string()
        ]
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn headless_batch_skip_completed_honored() {
    let (_dir, sock, server, _stats) = make_batch_mock().await;

    let work = tempfile::tempdir().unwrap();
    let urls_file = work.path().join("urls.txt");
    std::fs::write(
        &urls_file,
        "https://a.example\nhttps://b.example\nhttps://c.example\n",
    )
    .unwrap();

    // skip-completed file: prior NDJSON output listing b.example as done.
    let skip_file = work.path().join("done.ndjson");
    std::fs::write(
        &skip_file,
        "{\"url\":\"https://b.example\",\"ok\":true,\"title\":\"old\"}\n",
    )
    .unwrap();

    let out_file = work.path().join("out.ndjson");

    let output = run_cli(
        &[
            "headless",
            "--batch",
            urls_file.to_str().unwrap(),
            "--extract",
            "title",
            "--out",
            out_file.to_str().unwrap(),
            "--concurrency",
            "2",
            "--skip-completed",
            skip_file.to_str().unwrap(),
        ],
        &sock,
    );
    server.abort();

    assert!(
        output.status.success(),
        "stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    let content = std::fs::read_to_string(&out_file).expect("out file written");
    let mut urls: Vec<String> = content
        .lines()
        .filter(|l| !l.trim().is_empty())
        .map(|l| {
            let row: Value = serde_json::from_str(l).unwrap();
            row["url"].as_str().unwrap().to_string()
        })
        .collect();
    urls.sort();
    assert_eq!(
        urls,
        vec![
            "https://a.example".to_string(),
            "https://c.example".to_string()
        ],
        "b.example should have been skipped"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn headless_batch_concurrency_bounded() {
    let (_dir, sock, server, stats) = make_batch_mock().await;

    let work = tempfile::tempdir().unwrap();
    let urls_file = work.path().join("urls.txt");
    std::fs::write(
        &urls_file,
        "https://a.example\nhttps://b.example\nhttps://c.example\nhttps://d.example\n",
    )
    .unwrap();
    let out_file = work.path().join("out.ndjson");

    let output = run_cli(
        &[
            "headless",
            "--batch",
            urls_file.to_str().unwrap(),
            "--extract",
            "title",
            "--out",
            out_file.to_str().unwrap(),
            "--concurrency",
            "2",
        ],
        &sock,
    );
    server.abort();

    assert!(
        output.status.success(),
        "stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    let peak = stats.peak.load(Ordering::SeqCst);
    assert!(peak >= 1, "expected at least one connection");
    assert!(peak <= 2, "concurrency exceeded bound: peak={peak}");
}
