// In-process ToolAvailabilityChanged emit coverage (FIX-5).
//
// This is the coverage that was entirely missing. It proves the REAL, in-process
// propagation path: a consumer that registers a `CoreUpdate` callback via
// `set_core_update_callback` (as the browser process does) DOES observe a
// `ToolAvailabilityChanged` event when `McpClient::connect` succeeds.
//
// This is exactly what the CLI-side `cmd_mcp_add` emit could NEVER achieve:
// the CLI never registers a callback, so its emit was a dead no-op. Cross-process
// propagation to a running browser happens via the shared SQLite registry + the
// browser's next `McpClient::connect`, not via a CLI emit.
//
// The mock UDS server mirrors the strict, exact-match protocol-version handshake
// of the C++ server (kMcpProtocolVersion = "2025-03-26"), identical framing to
// the production transport. Kept in a separate test binary from
// `uds_handshake_test.rs` so the global core-update callback state does not
// interleave across the two suites.

use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use maho_types::ai::{AiMcpServer, McpTransport};
use maho_types::events::core_update::{set_core_update_callback, CoreUpdate};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixListener;
use tokio::task::JoinHandle;

use maho_agent::mcp_client::McpClient;

fn make_uds_config(socket_path: &str) -> AiMcpServer {
    AiMcpServer {
        id: "test-emit".to_string(),
        workspace_id: "ws-emit".to_string(),
        name: "emit-mock".to_string(),
        transport: McpTransport::Uds,
        command: None,
        url: None,
        auth_keychain_id: None,
        trusted: false,
        trusted_tools: None,
        timeout_ms: 5_000,
        output_cap_bytes: 10_240,
        socket_path: Some(socket_path.to_string()),
        created_at: "now".to_string(),
        updated_at: "now".to_string(),
    }
}

/// Minimal UDS MCP server that EXACT-matches `2025-03-26` on the `initialize`
/// handshake, mirroring the strict C++ server, then drains subsequent client
/// lines so the handshake completes cleanly.
fn spawn_strict_server(socket_path: PathBuf) -> JoinHandle<()> {
    let listener = UnixListener::bind(&socket_path).expect("bind test UDS server");
    tokio::spawn(async move {
        loop {
            let stream = match listener.accept().await {
                Ok((stream, _)) => stream,
                Err(_) => break,
            };
            tokio::spawn(async move {
                let (read_half, mut write_half) = stream.into_split();
                let mut reader = BufReader::new(read_half);
                let mut line = String::new();

                if reader.read_line(&mut line).await.unwrap_or(0) == 0 {
                    return;
                }
                let req: serde_json::Value = match serde_json::from_str(&line) {
                    Ok(v) => v,
                    Err(_) => return,
                };
                let id = req.get("id").cloned().unwrap_or(serde_json::Value::Null);

                let ok = serde_json::json!({
                    "jsonrpc": "2.0",
                    "id": id,
                    "result": {
                        "serverInfo": { "name": "maho-browser", "version": "0.4.1" },
                        "capabilities": { "tools": {} },
                        "protocolVersion": "2025-03-26"
                    }
                });
                let _ = write_half.write_all(format!("{}\n", ok).as_bytes()).await;
                let _ = write_half.flush().await;

                loop {
                    line.clear();
                    match reader.read_line(&mut line).await {
                        Ok(0) => break,
                        Ok(_) => continue,
                        Err(_) => break,
                    }
                }
            });
        }
    })
}

#[tokio::test]
async fn connect_emits_tool_availability_changed() {
    // A consumer (like the browser process) registers a core-update callback.
    let captured: Arc<Mutex<Vec<CoreUpdate>>> = Arc::new(Mutex::new(Vec::new()));
    let sink = Arc::clone(&captured);
    set_core_update_callback(move |update| {
        sink.lock().unwrap().push(update);
    });

    let dir = tempfile::tempdir().unwrap();
    let sock = dir.path().join("emit.sock");
    let _server = spawn_strict_server(sock.clone());

    let config = make_uds_config(sock.to_str().unwrap());
    let client = McpClient::connect(&config, None)
        .await
        .expect("connect must succeed against a strict 2025-03-26 server");

    // The REAL in-process path fires ToolAvailabilityChanged on connect success.
    let events = captured.lock().unwrap();
    let matched = events.iter().any(|u| {
        matches!(
            u,
            CoreUpdate::ToolAvailabilityChanged { workspace_id, server_name, available }
                if workspace_id == "ws-emit" && server_name == "emit-mock" && *available
        )
    });
    assert!(
        matched,
        "connect success must emit ToolAvailabilityChanged{{available:true}} in-process to a registered callback; captured: {:?}",
        *events
    );

    drop(client);
}
