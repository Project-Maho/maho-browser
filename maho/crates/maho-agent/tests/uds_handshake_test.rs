// UDS MCP protocol-version handshake tests (FIX-2, abort-lesson).
//
// These exercise the REAL rmcp client (`McpClient::connect`) against a minimal
// in-test UDS server that mirrors the C++ server's strict, exact-match
// protocol-version handshake (maho_mcp_session.cc: kMcpProtocolVersion =
// "2025-03-26", EXACT match, close-on-mismatch). Framing is newline-delimited
// JSON-RPC on both sides, identical to the production transport.

use std::path::PathBuf;
use std::time::Duration;

use maho_types::ai::{AiMcpServer, McpTransport};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixListener;
use tokio::task::JoinHandle;

use maho_agent::mcp_client::{resolve_uds_permission, McpClient};
use maho_types::tool::ToolPermission;

fn make_uds_config(socket_path: &str) -> AiMcpServer {
    AiMcpServer {
        id: "test-uds".to_string(),
        workspace_id: "ws-test".to_string(),
        name: "strict-mock".to_string(),
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

/// Spawn a minimal UDS MCP server that EXACT-matches `expected_version` on the
/// `initialize` handshake, mirroring the strict C++ server. On mismatch it
/// returns a JSON-RPC error and closes the connection (no hang).
fn spawn_strict_server(socket_path: PathBuf, expected_version: &'static str) -> JoinHandle<()> {
    // Bind synchronously so the socket file exists before we return.
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

                // Read the `initialize` request line.
                if reader.read_line(&mut line).await.unwrap_or(0) == 0 {
                    return;
                }
                let req: serde_json::Value = match serde_json::from_str(&line) {
                    Ok(v) => v,
                    Err(_) => return,
                };
                let id = req.get("id").cloned().unwrap_or(serde_json::Value::Null);
                let version = req
                    .get("params")
                    .and_then(|p| p.get("protocolVersion"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("");

                if version != expected_version {
                    // Mirror the C++ server: reject and close on mismatch.
                    let err = serde_json::json!({
                        "jsonrpc": "2.0",
                        "id": id,
                        "error": {
                            "code": -32602,
                            "message": "Invalid params: unsupported protocolVersion"
                        }
                    });
                    let _ = write_half.write_all(format!("{}\n", err).as_bytes()).await;
                    let _ = write_half.flush().await;
                    return;
                }

                // Match: reply with a valid InitializeResult (mirrors the C++ shape).
                let ok = serde_json::json!({
                    "jsonrpc": "2.0",
                    "id": id,
                    "result": {
                        "serverInfo": { "name": "maho-browser", "version": "0.4.1" },
                        "capabilities": { "tools": {} },
                        "protocolVersion": expected_version
                    }
                });
                let _ = write_half.write_all(format!("{}\n", ok).as_bytes()).await;
                let _ = write_half.flush().await;

                // Keep the connection alive, draining subsequent client lines
                // (e.g. `notifications/initialized`) so the client handshake
                // completes cleanly instead of failing on a closed write.
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
async fn rmcp_connects_to_strict_2025_03_26() {
    let dir = tempfile::tempdir().unwrap();
    let sock = dir.path().join("strict.sock");
    let _server = spawn_strict_server(sock.clone(), "2025-03-26");

    let config = make_uds_config(sock.to_str().unwrap());
    let result = McpClient::connect(&config, None).await;

    assert!(
        result.is_ok(),
        "rmcp client must connect to a server pinning 2025-03-26, got: {:?}",
        result.err()
    );
}

#[tokio::test]
async fn rmcp_fails_clean_on_version_mismatch() {
    let dir = tempfile::tempdir().unwrap();
    let sock = dir.path().join("mismatch.sock");
    // Server pins a DIFFERENT version than the client sends.
    let _server = spawn_strict_server(sock.clone(), "2025-11-25");

    let config = make_uds_config(sock.to_str().unwrap());
    let outcome =
        tokio::time::timeout(Duration::from_secs(10), McpClient::connect(&config, None)).await;

    let result = outcome.expect("connect must fail cleanly, not hang/timeout");
    assert!(
        result.is_err(),
        "rmcp client must return a clean Err on protocol-version mismatch"
    );
}

#[test]
fn uds_autoapprove_only_for_browser_socket() {
    let browser_sock = "/tmp/maho-dogfood-autoapprove-test.sock";
    // The canonical dogfood socket resolver honors this env override.
    unsafe { std::env::set_var("MAHO_MCP_SOCKET_PATH", browser_sock) };

    assert_eq!(
        resolve_uds_permission(McpTransport::Uds, Some(browser_sock)),
        ToolPermission::AutoApprove,
        "the browser's own dogfood UDS socket must be AutoApprove"
    );

    assert_eq!(
        resolve_uds_permission(McpTransport::Uds, Some("/tmp/arbitrary-user-server.sock")),
        ToolPermission::AlwaysAsk,
        "an arbitrary user-imported UDS server must remain AlwaysAsk"
    );

    assert_eq!(
        resolve_uds_permission(McpTransport::Stdio, None),
        ToolPermission::AlwaysAsk,
        "non-UDS transports are never auto-approved by the UDS scoping rule"
    );

    unsafe { std::env::remove_var("MAHO_MCP_SOCKET_PATH") };
}
