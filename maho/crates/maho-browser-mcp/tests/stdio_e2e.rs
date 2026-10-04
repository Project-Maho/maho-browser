// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(unix)]

use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use maho_browser_mcp::server::{MahoBrowserMcpServer, MutationApprovalCallback, MutationDecision};
use rmcp::ServiceExt;
use serde_json::{json, Value};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixListener;

/// Process-global test state: `MAHO_DB_PATH` mutations AND the process-wide
/// SQLCipher key injection performed by fixture seeding — `set_sqlcipher_key`
/// + `SqliteStorage::open` pairs must never interleave between tests, or a
/// fixture DB gets encrypted with another test's key (same pattern as
/// `paths.rs` tests).
static GLOBAL_STATE_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

/// Registry settings key that re-enables public dispatch — must stay in sync
/// with `maho_browser_mcp::settings::MCP_PUBLIC_ENABLED_KEY`.
const MCP_PUBLIC_ENABLED_KEY: &str = "maho.browser.mcp_public_enabled";

/// Materializes a real registry DB with the public-MCP toggle seeded, the
/// same way an operator would enable it. Returns the DB path for
/// `MAHO_DB_PATH`.
fn registry_db_fixture(enabled: bool) -> PathBuf {
    let dir = tempfile::tempdir().expect("registry temporary directory creates");
    let db_path = dir.keep().join("registry").join("maho.db");
    std::fs::create_dir_all(db_path.parent().unwrap()).expect("registry dir creates");
    let _guard = GLOBAL_STATE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let mut key_bytes = [0u8; 16];
    let mut status = 0;
    assert!(
        maho_core::oscrypt::derive_key(
            &db_path
                .parent()
                .unwrap()
                .join("maho_storage.key")
                .to_string_lossy(),
            &mut key_bytes,
            &mut status,
        ),
        "registry key derives"
    );
    let hex_key: String = key_bytes.iter().map(|byte| format!("{byte:02X}")).collect();
    maho_storage::sqlite::set_sqlcipher_key(&hex_key).expect("sqlcipher key sets");
    let storage = maho_storage::sqlite::SqliteStorage::open(&db_path.to_string_lossy())
        .expect("registry DB opens");
    storage
        .set_setting(
            MCP_PUBLIC_ENABLED_KEY,
            if enabled { "true" } else { "false" },
        )
        .expect("toggle seeds");
    db_path
}

struct FakeBrowser {
    path: PathBuf,
    calls: Arc<Mutex<Vec<String>>>,
    task: tokio::task::JoinHandle<()>,
}

impl FakeBrowser {
    async fn start() -> Self {
        let dir = tempfile::tempdir().expect("temporary socket directory creates");
        let path = dir.keep().join("fake-browser.sock");
        let listener = UnixListener::bind(&path).expect("fake browser socket binds");
        let calls = Arc::new(Mutex::new(Vec::new()));
        let calls_for_task = Arc::clone(&calls);
        let task = tokio::spawn(async move {
            while let Ok((stream, _)) = listener.accept().await {
                let calls = Arc::clone(&calls_for_task);
                tokio::spawn(async move {
                    let (reader, mut writer) = stream.into_split();
                    let mut reader = BufReader::new(reader);
                    let mut line = String::new();
                    loop {
                        line.clear();
                        let bytes = reader
                            .read_line(&mut line)
                            .await
                            .expect("fake browser reads request");
                        if bytes == 0 {
                            break;
                        }
                        let request: Value =
                            serde_json::from_str(&line).expect("browser request is JSON");
                        let id = request["id"].clone();
                        let response = match request["method"].as_str().unwrap_or_default() {
                            "initialize" => json!({
                                "jsonrpc": "2.0",
                                "id": id,
                                "result": {
                                    "protocolVersion": "2025-03-26",
                                    "serverInfo": {
                                        "name": "maho-browser",
                                        "version": "0.1.0"
                                    },
                                    "capabilities": {"tools": {}}
                                }
                            }),
                            "tools/list" => json!({
                                "jsonrpc": "2.0",
                                "id": id,
                                "result": discovery()
                            }),
                            "tools/call" => {
                                let name = request["params"]["name"]
                                    .as_str()
                                    .expect("tool call has a name")
                                    .to_string();
                                calls.lock().expect("call log lock").push(name.clone());
                                json!({
                                    "jsonrpc": "2.0",
                                    "id": id,
                                    "result": {"ok": true, "tool": name}
                                })
                            }
                            _ => json!({
                                "jsonrpc": "2.0",
                                "id": id,
                                "error": {
                                    "code": -32601,
                                    "message": "Method not found"
                                }
                            }),
                        };
                        let mut bytes = serde_json::to_vec(&response).expect("response serializes");
                        bytes.push(b'\n');
                        writer
                            .write_all(&bytes)
                            .await
                            .expect("fake browser writes response");
                    }
                });
            }
        });
        Self { path, calls, task }
    }
}

impl Drop for FakeBrowser {
    fn drop(&mut self) {
        self.task.abort();
        let _ = std::fs::remove_file(&self.path);
        if let Some(parent) = self.path.parent() {
            let _ = std::fs::remove_dir(parent);
        }
    }
}

fn descriptor(name: &str, mutable: bool) -> Value {
    json!({
        "capabilityId": format!("test.{name}"),
        "name": name,
        "description": format!("Test {name}"),
        "inputSchema": {"type": "object", "properties": {}},
        "resultSchema": {"type": "object", "properties": {}},
        "schemaVersion": 1,
        "resultVersion": 1,
        "missingPolicy": "fail_closed",
        "policy": {
            "mutability": if mutable {"mutable"} else {"read_only"},
            "changesAuthority": false,
            "requiresApproval": mutable,
            "permission": if mutable {"always_ask"} else {"auto_approve"}
        },
        "category": "page"
    })
}

fn discovery() -> Value {
    let tools = vec![descriptor("read_page", false), descriptor("act_page", true)];
    json!({
        "catalogDiagnostics": {
            "catalogVersion": 1,
            "schemaVersion": 1,
            "resultVersion": 1,
            "canonicalCount": 67,
            "gates": {"mailBeta": true, "routines": true, "vault": true},
            "surfaces": {
                "publicMcp": {
                    "count": tools.len(),
                    "ids": ["test.read_page", "test.act_page"]
                }
            },
            "intentionalExclusions": ["control_plane_authority"]
        },
        "tools": tools
    })
}

async fn write_request<W: AsyncWriteExt + Unpin>(writer: &mut W, value: Value) {
    let mut bytes = serde_json::to_vec(&value).expect("request serializes");
    bytes.push(b'\n');
    writer.write_all(&bytes).await.expect("request writes");
}

async fn read_response<R: AsyncBufReadExt + Unpin>(reader: &mut R) -> Value {
    let mut line = String::new();
    tokio::time::timeout(Duration::from_secs(5), reader.read_line(&mut line))
        .await
        .expect("response arrives before timeout")
        .expect("response reads");
    serde_json::from_str(&line).expect("response is JSON")
}

fn initialize_request(id: i64) -> Value {
    json!({
        "jsonrpc": "2.0",
        "id": id,
        "method": "initialize",
        "params": {
            "protocolVersion": "2025-03-26",
            "capabilities": {},
            "clientInfo": {"name": "stdio-e2e", "version": "0.1.0"}
        }
    })
}

async fn assert_public_flow<R, W>(reader: &mut R, writer: &mut W)
where
    R: AsyncBufReadExt + Unpin,
    W: AsyncWriteExt + Unpin,
{
    write_request(writer, initialize_request(1)).await;
    assert!(read_response(reader).await["result"].is_object());

    write_request(
        writer,
        json!({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}),
    )
    .await;
    let listed = read_response(reader).await;
    let names: Vec<&str> = listed["result"]["tools"]
        .as_array()
        .expect("tools array")
        .iter()
        .filter_map(|tool| tool["name"].as_str())
        .collect();
    assert_eq!(names, ["read_page", "act_page"]);
    assert!(!names.iter().any(|name| name.starts_with("maho/control/")));

    write_request(
        writer,
        json!({
            "jsonrpc":"2.0",
            "id":3,
            "method":"tools/call",
            "params":{"name":"read_page","arguments":{}}
        }),
    )
    .await;
    let read = read_response(reader).await;
    let text = read["result"]["content"][0]["text"]
        .as_str()
        .expect("read result text");
    assert_eq!(
        serde_json::from_str::<Value>(text).expect("read result JSON"),
        json!({"ok":true,"tool":"read_page"})
    );
}

#[tokio::test]
async fn real_binary_lists_reads_and_denies_mutation_without_broker() {
    let browser = FakeBrowser::start().await;
    // The dispatch surface is frozen by default (row 15); this test pins the
    // pre-existing full-dispatch path with the toggle explicitly enabled.
    let registry_db = registry_db_fixture(true);
    let binary = env!("CARGO_BIN_EXE_maho-browser-mcp");
    let mut child = tokio::process::Command::new(binary)
        .env("MAHO_MCP_SOCKET_PATH", &browser.path)
        .env("MAHO_DB_PATH", &registry_db)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()
        .expect("real stdio binary spawns");
    let mut writer = child.stdin.take().expect("child stdin");
    let mut reader = BufReader::new(child.stdout.take().expect("child stdout"));

    assert_public_flow(&mut reader, &mut writer).await;
    write_request(
        &mut writer,
        json!({
            "jsonrpc":"2.0",
            "id":4,
            "method":"tools/call",
            "params":{"name":"act_page","arguments":{}}
        }),
    )
    .await;
    let denied = read_response(&mut reader).await;
    assert_eq!(denied["error"]["data"]["status"], "broker_unavailable");
    assert_eq!(
        browser.calls.lock().expect("call log lock").as_slice(),
        ["read_page"]
    );

    write_request(
        &mut writer,
        json!({
            "jsonrpc":"2.0",
            "id":5,
            "method":"tools/call",
            "params":{"name":"maho/control/list","arguments":{}}
        }),
    )
    .await;
    let control = read_response(&mut reader).await;
    assert_eq!(control["error"]["data"]["status"], "unknown_or_gated_tool");

    drop(writer);
    child.kill().await.expect("real stdio binary stops");
}

/// Row 15 freeze contract over the real binary: with no registry DB at all
/// (fresh-install default), tools/list still serves the browser-owned
/// catalog, but every tools/call refuses fail-closed with the existing
/// broker-unavailable error shape.
#[tokio::test]
async fn default_frozen_binary_lists_but_refuses_dispatch() {
    let browser = FakeBrowser::start().await;
    // A registry path that cannot exist: the default must fail closed even
    // when no DB has ever been created.
    let absent_db = std::env::temp_dir()
        .join("maho-gate-e2e-absent")
        .join("maho.db");
    let binary = env!("CARGO_BIN_EXE_maho-browser-mcp");
    let mut child = tokio::process::Command::new(binary)
        .env("MAHO_MCP_SOCKET_PATH", &browser.path)
        .env("MAHO_DB_PATH", &absent_db)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()
        .expect("real stdio binary spawns");
    let mut writer = child.stdin.take().expect("child stdin");
    let mut reader = BufReader::new(child.stdout.take().expect("child stdout"));

    write_request(&mut writer, initialize_request(1)).await;
    assert!(read_response(&mut reader).await["result"].is_object());

    write_request(
        &mut writer,
        json!({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}),
    )
    .await;
    let listed = read_response(&mut reader).await;
    let names: Vec<&str> = listed["result"]["tools"]
        .as_array()
        .expect("tools array")
        .iter()
        .filter_map(|tool| tool["name"].as_str())
        .collect();
    assert_eq!(
        names,
        ["read_page", "act_page"],
        "discovery stays available while dispatch is frozen"
    );

    for (id, name) in [(3, "read_page"), (4, "act_page")] {
        write_request(
            &mut writer,
            json!({
                "jsonrpc":"2.0",
                "id":id,
                "method":"tools/call",
                "params":{"name":name,"arguments":{}}
            }),
        )
        .await;
        let refused = read_response(&mut reader).await;
        assert_eq!(
            refused["error"]["data"]["status"], "broker_unavailable",
            "frozen default must refuse '{name}' fail-closed"
        );
    }
    assert!(
        browser.calls.lock().expect("call log lock").is_empty(),
        "frozen dispatch must not reach the browser"
    );

    drop(writer);
    child.kill().await.expect("real stdio binary stops");
}

#[tokio::test]
async fn explicit_broker_fixture_allows_mutation_over_mcp_transport() {
    let browser = FakeBrowser::start().await;
    let decisions = Arc::new(Mutex::new(Vec::new()));
    let decisions_for_broker = Arc::clone(&decisions);
    let broker: MutationApprovalCallback = Arc::new(move |operation| {
        decisions_for_broker
            .lock()
            .expect("decision log lock")
            .push(operation.to_string());
        MutationDecision::Allow
    });
    let server = {
        // Seed first: the fixture takes GLOBAL_STATE_LOCK itself (this block
        // must not hold it while calling it — std Mutex is not reentrant).
        let registry_db = registry_db_fixture(true);
        let _guard = GLOBAL_STATE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        // SAFETY: env access serialized by ENV_LOCK; restored immediately.
        unsafe { std::env::set_var("MAHO_DB_PATH", &registry_db) };
        let server = MahoBrowserMcpServer::connect_with_approval(
            Path::new(&browser.path),
            Duration::from_secs(5),
            broker,
        )
        .await
        .expect("broker-backed server connects");
        // SAFETY: env access serialized by ENV_LOCK.
        unsafe { std::env::remove_var("MAHO_DB_PATH") };
        server
    };
    let (client_io, server_io) = tokio::io::duplex(16 * 1024);
    let service_task = tokio::spawn(async move {
        let service = server.serve(server_io).await.expect("MCP service starts");
        service.waiting().await.expect("MCP service completes");
    });
    let (reader, mut writer) = tokio::io::split(client_io);
    let mut reader = BufReader::new(reader);

    write_request(&mut writer, initialize_request(10)).await;
    assert!(read_response(&mut reader).await["result"].is_object());
    write_request(
        &mut writer,
        json!({
            "jsonrpc":"2.0",
            "id":11,
            "method":"tools/call",
            "params":{"name":"act_page","arguments":{}}
        }),
    )
    .await;
    let approved = read_response(&mut reader).await;
    let text = approved["result"]["content"][0]["text"]
        .as_str()
        .expect("approved result text");
    assert_eq!(
        serde_json::from_str::<Value>(text).expect("approved result JSON"),
        json!({"ok":true,"tool":"act_page"})
    );
    assert_eq!(
        decisions.lock().expect("decision log lock").as_slice(),
        ["act_page"]
    );
    assert_eq!(
        browser.calls.lock().expect("call log lock").as_slice(),
        ["act_page"]
    );

    drop(writer);
    service_task.abort();
}
