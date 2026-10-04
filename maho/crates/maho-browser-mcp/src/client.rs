// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Reusable UDS JSON-RPC client for communicating with the Maho Browser MCP socket server.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicI64, Ordering};
use std::sync::Arc;
use std::time::Duration;

use serde_json::Value;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
#[cfg(unix)]
use tokio::net::unix::OwnedWriteHalf;
#[cfg(unix)]
use tokio::net::UnixStream;
use tokio::sync::{oneshot, Mutex};
use tokio::task::JoinHandle;

use crate::error::{McpBridgeError, Result};
use crate::protocol::{
    ClientInfo, ControllerKind, InitializeParams, InitializeResult, JsonRpcId, JsonRpcNotification,
    JsonRpcRequest, JsonRpcResponse, SessionInfo,
};

#[cfg(unix)]
type WriterHalf = OwnedWriteHalf;
#[cfg(windows)]
type WriterHalf = crate::win_transport::WriterHalf;

const DEFAULT_TIMEOUT: Duration = Duration::from_secs(90);
const EXPECTED_SERVER_NAME: &str = "maho-browser";

/// A response received over the wire together with wire byte count.
#[derive(Debug)]
struct PendingResponse {
    response: JsonRpcResponse,
    wire_bytes_in: usize,
}

/// JSON-RPC perf trace payload emitted to stderr when `MAHO_BROWSER_PERF_TRACE=1`.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct PerfTrace<'a> {
    /// Canonical tool name, capability ID, or JSON-RPC method name.
    pub tool: &'a str,
    /// Milliseconds spent establishing the underlying transport connection.
    pub connect_ms: f64,
    /// Milliseconds spent executing the initialize handshake.
    pub handshake_ms: f64,
    /// Total bytes received on the wire for this response.
    pub wire_bytes_in: usize,
    /// Total bytes sent on the wire for this request.
    pub wire_bytes_out: usize,
    /// Total milliseconds from request dispatch to response reception.
    pub total_ms: f64,
}

#[cfg(test)]
static TEST_TRACE_SINK: std::sync::Mutex<Option<std::sync::Arc<dyn Fn(&str) + Send + Sync>>> =
    std::sync::Mutex::new(None);

fn is_perf_trace_enabled() -> bool {
    std::env::var("MAHO_BROWSER_PERF_TRACE")
        .map(|v| v == "1")
        .unwrap_or(false)
}

fn emit_perf_trace(trace: PerfTrace<'_>) {
    if let Ok(line) = serde_json::to_string(&trace) {
        #[cfg(test)]
        {
            if let Ok(sink_guard) = TEST_TRACE_SINK.lock() {
                if let Some(ref cb) = *sink_guard {
                    cb(&line);
                }
            }
        }
        eprintln!("{line}");
    }
}

/// A JSON-RPC 2.0 client that communicates with the browser over a Unix domain socket.
///
/// The client spawns a background read loop to correlate responses by ID,
/// supporting out-of-order response delivery. If the socket connection dies
/// (e.g., the browser process restarts), the next `call()` attempt transparently
/// reconnects and re-runs the initialize handshake before retrying the request.
#[derive(Debug)]
pub struct BrowserClient {
    path: PathBuf,
    writer: Mutex<WriterHalf>,
    read_task: Mutex<JoinHandle<()>>,
    pending: Arc<Mutex<HashMap<i64, oneshot::Sender<PendingResponse>>>>,
    next_id: AtomicI64,
    timeout: Duration,
    controller_kind: ControllerKind,
    autonomous: bool,
    session_info: Mutex<Option<SessionInfo>>,
    connect_duration: Mutex<Duration>,
    handshake_duration: Mutex<Duration>,
}

impl BrowserClient {
    /// Connect to the browser's MCP socket, perform the `initialize` handshake,
    /// and return a ready-to-use client.
    pub async fn connect(path: &Path) -> Result<Self> {
        Self::connect_as(path, DEFAULT_TIMEOUT, ControllerKind::ThirdParty).await
    }

    /// Connect with a custom per-request timeout as an arbitrary MCP host.
    pub async fn connect_with_timeout(path: &Path, timeout: Duration) -> Result<Self> {
        Self::connect_as(path, timeout, ControllerKind::ThirdParty).await
    }

    /// Connect with a closed controller classification hint.
    pub async fn connect_as(
        path: &Path,
        timeout: Duration,
        controller_kind: ControllerKind,
    ) -> Result<Self> {
        Self::connect_with_identity(path, timeout, controller_kind, false).await
    }

    /// Connect as an autonomous controller. The browser must acknowledge the
    /// requested admission mode during every initialize handshake.
    pub async fn connect_autonomous_as(
        path: &Path,
        timeout: Duration,
        controller_kind: ControllerKind,
    ) -> Result<Self> {
        Self::connect_with_identity(path, timeout, controller_kind, true).await
    }

    async fn connect_with_identity(
        path: &Path,
        timeout: Duration,
        controller_kind: ControllerKind,
        autonomous: bool,
    ) -> Result<Self> {
        let pending: Arc<Mutex<HashMap<i64, oneshot::Sender<PendingResponse>>>> =
            Arc::new(Mutex::new(HashMap::new()));

        let connect_start = std::time::Instant::now();
        let (writer, read_task) = Self::open_stream(path, Arc::clone(&pending)).await?;
        let connect_duration = connect_start.elapsed();

        let client = Self {
            path: path.to_path_buf(),
            writer: Mutex::new(writer),
            read_task: Mutex::new(read_task),
            pending,
            next_id: AtomicI64::new(1),
            timeout,
            controller_kind,
            autonomous,
            session_info: Mutex::new(None),
            connect_duration: Mutex::new(connect_duration),
            handshake_duration: Mutex::new(Duration::ZERO),
        };

        let handshake_start = std::time::Instant::now();
        client.perform_handshake().await?;
        let handshake_duration = handshake_start.elapsed();
        *client.handshake_duration.lock().await = handshake_duration;

        Ok(client)
    }

    #[cfg(unix)]
    async fn open_stream(
        path: &Path,
        pending: Arc<Mutex<HashMap<i64, oneshot::Sender<PendingResponse>>>>,
    ) -> Result<(WriterHalf, JoinHandle<()>)> {
        let stream = UnixStream::connect(path)
            .await
            .map_err(McpBridgeError::SocketConnect)?;

        let (read_half, write_half) = stream.into_split();
        let handle = spawn_read_loop(read_half, pending);

        Ok((write_half, handle))
    }

    #[cfg(windows)]
    async fn open_stream(
        path: &Path,
        pending: Arc<Mutex<HashMap<i64, oneshot::Sender<PendingResponse>>>>,
    ) -> Result<(WriterHalf, JoinHandle<()>)> {
        // Delegates to the named-pipe transport, which handles the
        // ERROR_PIPE_BUSY wait-and-retry handshake before returning a
        // connected client. Framing is byte-mode newline-delimited JSON,
        // identical to the Unix domain socket transport.
        let stream = crate::win_transport::connect(path).await?;

        let (read_half, write_half) = tokio::io::split(stream);
        let handle = spawn_read_loop(read_half, pending);

        Ok((write_half, handle))
    }
}

fn spawn_read_loop<R>(
    read_half: R,
    pending: Arc<Mutex<HashMap<i64, oneshot::Sender<PendingResponse>>>>,
) -> JoinHandle<()>
where
    R: tokio::io::AsyncRead + Unpin + Send + 'static,
{
    tokio::spawn(async move {
        let mut reader = BufReader::new(read_half);
        let mut line = String::new();
        loop {
            line.clear();
            match reader.read_line(&mut line).await {
                Ok(0) => break,
                Ok(bytes_read) => {
                    if let Ok(resp) = serde_json::from_str::<JsonRpcResponse>(&line) {
                        if let Some(ref id) = resp.id {
                            if let JsonRpcId::Num(n) = id {
                                let mut map = pending.lock().await;
                                if let Some(tx) = map.remove(n) {
                                    let _ = tx.send(PendingResponse {
                                        response: resp,
                                        wire_bytes_in: bytes_read,
                                    });
                                }
                            }
                        }
                    }
                }
                Err(_) => break,
            }
        }

        pending.lock().await.clear();
    })
}

impl BrowserClient {
    async fn perform_handshake(&self) -> Result<()> {
        let auth_token = Self::load_auth_token();
        let init_params = InitializeParams {
            protocol_version: "2025-03-26".to_string(),
            client_info: ClientInfo {
                name: self.controller_kind.client_name().to_string(),
                version: env!("CARGO_PKG_VERSION").to_string(),
            },
            controller_kind: self.controller_kind,
            autonomous: self.autonomous,
            auth_token,
        };
        let result_value = self
            .call_no_retry("initialize", serde_json::to_value(&init_params)?)
            .await?;

        let init_result: InitializeResult = serde_json::from_value(result_value)?;
        if init_result.server_info.name != EXPECTED_SERVER_NAME {
            return Err(McpBridgeError::HandshakeMismatch {
                expected: EXPECTED_SERVER_NAME.to_string(),
                got: init_result.server_info.name,
            });
        }
        if self.autonomous && !init_result.session_info.autonomous {
            return Err(McpBridgeError::HandshakeMismatch {
                expected: "sessionInfo.autonomous=true (autonomous lease admission supported)"
                    .to_string(),
                got: "autonomous acknowledgement missing/false; update or restart the browser"
                    .to_string(),
            });
        }
        *self.session_info.lock().await = Some(init_result.session_info);
        Ok(())
    }

    async fn reconnect(&self) -> Result<()> {
        let connect_start = std::time::Instant::now();
        let (new_writer, new_read_task) =
            Self::open_stream(&self.path, Arc::clone(&self.pending)).await?;
        let connect_duration = connect_start.elapsed();

        let old_task = {
            let mut read_task = self.read_task.lock().await;
            std::mem::replace(&mut *read_task, new_read_task)
        };
        old_task.abort();
        let _ = old_task.await;
        self.pending.lock().await.clear();

        {
            let mut writer = self.writer.lock().await;
            *writer = new_writer;
        }

        *self.connect_duration.lock().await = connect_duration;

        let handshake_start = std::time::Instant::now();
        self.perform_handshake().await?;
        let handshake_duration = handshake_start.elapsed();
        *self.handshake_duration.lock().await = handshake_duration;

        Ok(())
    }

    async fn call_no_retry_with_timeout(
        &self,
        method: &str,
        params: Value,
        timeout: Duration,
    ) -> Result<Value> {
        let req_start = std::time::Instant::now();
        let id = self.next_id.fetch_add(1, Ordering::Relaxed);
        let req = JsonRpcRequest::new(id, method, Some(params.clone()));

        let (tx, rx) = oneshot::channel();
        {
            let mut map = self.pending.lock().await;
            map.insert(id, tx);
        }

        let wire_bytes_out = match self.write_message(&req).await {
            Ok(bytes) => bytes,
            Err(err) => {
                self.pending.lock().await.remove(&id);
                return Err(err);
            }
        };

        let response = tokio::time::timeout(timeout, rx).await;
        self.pending.lock().await.remove(&id);
        let pending_resp = response
            .map_err(|_| McpBridgeError::Timeout(timeout))?
            .map_err(|_| McpBridgeError::HandlerClosed)?;

        let total_duration = req_start.elapsed();
        let wire_bytes_in = pending_resp.wire_bytes_in;
        let resp = pending_resp.response;

        if is_perf_trace_enabled() {
            let tool = params
                .get("name")
                .and_then(|v| v.as_str())
                .unwrap_or(method);
            let connect_ms = self.connect_duration.lock().await.as_secs_f64() * 1000.0;
            let handshake_ms = self.handshake_duration.lock().await.as_secs_f64() * 1000.0;
            let total_ms = total_duration.as_secs_f64() * 1000.0;
            emit_perf_trace(PerfTrace {
                tool,
                connect_ms,
                handshake_ms,
                wire_bytes_in,
                wire_bytes_out,
                total_ms,
            });
        }

        if let Some(err) = resp.error {
            return Err(McpBridgeError::Rpc {
                code: err.code,
                message: err.message,
                data: err.data,
            });
        }

        Ok(resp.result.unwrap_or(Value::Null))
    }

    async fn call_no_retry(&self, method: &str, params: Value) -> Result<Value> {
        self.call_no_retry_with_timeout(method, params, self.timeout)
            .await
    }

    /// Return the browser-owned identity for the current transport session.
    pub async fn session_info(&self) -> Option<SessionInfo> {
        self.session_info.lock().await.clone()
    }

    /// Send a JSON-RPC request and await the correlated response.
    ///
    /// On broken pipe or handler-closed errors (browser restarted / socket
    /// invalidated), the client transparently reconnects and retries the
    /// request exactly once before propagating the failure.
    pub async fn call(&self, method: &str, params: Value) -> Result<Value> {
        self.call_with_timeout(method, params, self.timeout).await
    }

    /// Send a JSON-RPC request with a per-call timeout while preserving the
    /// reconnect-and-retry semantics of `call`.
    pub async fn call_with_timeout(
        &self,
        method: &str,
        params: Value,
        timeout: Duration,
    ) -> Result<Value> {
        match self
            .call_no_retry_with_timeout(method, params.clone(), timeout)
            .await
        {
            Err(err) if should_retry_after_reconnect(&err) => {
                tracing::warn!(
                    "BrowserClient: connection broken ({err:?}); attempting reconnect + retry"
                );
                self.reconnect().await?;
                self.call_no_retry_with_timeout(method, params, timeout)
                    .await
            }
            other => other,
        }
    }

    /// Send a request without reconnecting or replaying it after connection loss.
    ///
    /// Stateful callers use this when the MCP session owns domain approvals or
    /// tab leases, and when replaying a mutating request could duplicate effects.
    pub async fn call_without_reconnect(&self, method: &str, params: Value) -> Result<Value> {
        self.call_without_reconnect_with_timeout(method, params, self.timeout)
            .await
    }

    /// Send a non-replayable request with a per-call timeout. Timeout cleanup is
    /// handled inside the client so abandoned requests are removed from pending
    /// correlation state before this method returns.
    pub async fn call_without_reconnect_with_timeout(
        &self,
        method: &str,
        params: Value,
        timeout: Duration,
    ) -> Result<Value> {
        self.call_no_retry_with_timeout(method, params, timeout)
            .await
    }

    /// Convenience wrapper that packages `name` and `arguments` into the
    /// browser server's `tools/call` envelope. Tool calls are never replayed:
    /// discovery metadata may be stale or unavailable, and replaying a
    /// mutation after an ambiguous disconnect can duplicate browser effects.
    pub async fn call_tool(&self, name: &str, arguments: Value) -> Result<Value> {
        self.call_without_reconnect(
            "tools/call",
            serde_json::json!({ "name": name, "arguments": arguments }),
        )
        .await
    }

    /// Invokes a browser capability directly by its canonical capability ID without
    /// performing a live `tools/list` discovery round-trip.
    ///
    /// # Assumptions
    /// This method assumes that the canonical capability ID matches the tool method
    /// name expected by the browser server's `tools/call` handler (i.e. capability ID == method name).
    ///
    /// Like `call_tool`, capability calls are sent without reconnect replay to prevent
    /// unintended duplicate mutations if a connection drops mid-call.
    pub async fn call_capability(&self, capability_id: &str, arguments: Value) -> Result<Value> {
        self.call_capability_with_timeout(capability_id, arguments, self.timeout)
            .await
    }

    /// Invokes a browser capability directly by its canonical capability ID with a per-call
    /// timeout, without performing a live `tools/list` discovery round-trip.
    ///
    /// # Assumptions
    /// This method assumes that the canonical capability ID matches the tool method
    /// name expected by the browser server's `tools/call` handler (i.e. capability ID == method name).
    pub async fn call_capability_with_timeout(
        &self,
        capability_id: &str,
        arguments: Value,
        timeout: Duration,
    ) -> Result<Value> {
        self.call_without_reconnect_with_timeout(
            "tools/call",
            serde_json::json!({ "name": capability_id, "arguments": arguments }),
            timeout,
        )
        .await
    }

    /// Send a JSON-RPC notification (fire-and-forget, no response expected).
    pub async fn notify(&self, method: &str, params: Value) -> Result<()> {
        let notif = JsonRpcNotification {
            jsonrpc: "2.0".to_string(),
            method: method.to_string(),
            params: Some(params),
        };
        let mut buf = serde_json::to_vec(&notif)?;
        buf.push(b'\n');
        let mut writer = self.writer.lock().await;
        writer.write_all(&buf).await?;
        Ok(())
    }

    /// Gracefully shut down the client connection.
    pub async fn shutdown(self) -> Result<()> {
        let mut writer = self.writer.lock().await;
        let shutdown_result = writer.shutdown().await.map_err(McpBridgeError::Io);
        self.pending.lock().await.clear();
        self.read_task.lock().await.abort();
        shutdown_result
    }

    /// Load the DPAPI session token on Windows, base64-encode it.
    /// Returns None on non-Windows or if the token cannot be loaded.
    fn load_auth_token() -> Option<String> {
        #[cfg(windows)]
        {
            use base64::Engine as _;
            match crate::win_token::load_session_token() {
                Ok(token) => Some(base64::engine::general_purpose::STANDARD.encode(&token)),
                Err(e) => {
                    tracing::warn!("Failed to load session token: {e}");
                    None
                }
            }
        }
        #[cfg(not(windows))]
        {
            None
        }
    }

    async fn write_message<T: serde::Serialize>(&self, msg: &T) -> Result<usize> {
        let mut buf = serde_json::to_vec(msg)?;
        buf.push(b'\n');
        let len = buf.len();
        let mut writer = self.writer.lock().await;
        writer.write_all(&buf).await?;
        writer.flush().await?;
        Ok(len)
    }
}

impl Drop for BrowserClient {
    fn drop(&mut self) {
        self.read_task.get_mut().abort();
    }
}

fn should_retry_after_reconnect(err: &McpBridgeError) -> bool {
    match err {
        McpBridgeError::Io(io_err) => matches!(
            io_err.kind(),
            std::io::ErrorKind::BrokenPipe
                | std::io::ErrorKind::ConnectionReset
                | std::io::ErrorKind::ConnectionAborted
                | std::io::ErrorKind::UnexpectedEof
                | std::io::ErrorKind::NotConnected
                | std::io::ErrorKind::WriteZero
        ),
        McpBridgeError::HandlerClosed => true,
        _ => false,
    }
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;
    use serde_json::json;
    use std::io;
    use std::pin::Pin;
    use std::task::{Context, Poll};
    use std::time::Duration;
    use tokio::io::{AsyncBufReadExt, AsyncRead, AsyncWriteExt, BufReader, ReadBuf};
    use tokio::net::UnixListener;
    use tokio::sync::oneshot;

    /// Helper: create a temp socket and UnixListener.
    async fn make_listener() -> (tempfile::TempDir, std::path::PathBuf, UnixListener) {
        let dir = tempfile::tempdir().expect("tempdir");
        let sock_path = dir.path().join("test.sock");
        let listener = UnixListener::bind(&sock_path).expect("bind");
        (dir, sock_path, listener)
    }

    /// SC3: client.rs happy call — response matches expected.
    #[tokio::test]
    async fn happy_call() {
        let (_dir, sock_path, listener) = make_listener().await;

        // Fake browser: responds to initialize then to "ping".
        let server_handle = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);

            // Read initialize request.
            let mut line = String::new();
            reader.read_line(&mut line).await.unwrap();
            let req: Value = serde_json::from_str(&line).unwrap();
            let id = req["id"].as_i64().unwrap();
            let resp = json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "capabilities": {"tools": {}}
                }
            });
            let mut buf = serde_json::to_vec(&resp).unwrap();
            buf.push(b'\n');
            writer.write_all(&buf).await.unwrap();

            // Read ping request.
            line.clear();
            reader.read_line(&mut line).await.unwrap();
            let req2: Value = serde_json::from_str(&line).unwrap();
            let id2 = req2["id"].as_i64().unwrap();
            let resp2 = json!({
                "jsonrpc": "2.0",
                "id": id2,
                "result": {"pong": true}
            });
            let mut buf2 = serde_json::to_vec(&resp2).unwrap();
            buf2.push(b'\n');
            writer.write_all(&buf2).await.unwrap();
        });

        let client = BrowserClient::connect(&sock_path).await.unwrap();
        let result = client.call("ping", json!({})).await.unwrap();
        assert_eq!(result, json!({"pong": true}));

        server_handle.await.unwrap();
    }

    #[tokio::test]
    async fn autonomous_connect_sends_flag_and_requires_ack() {
        let (_dir, sock_path, listener) = make_listener().await;
        let server_handle = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);
            let mut line = String::new();
            reader.read_line(&mut line).await.unwrap();
            let req: Value = serde_json::from_str(&line).unwrap();
            assert_eq!(req["method"], "initialize");
            assert_eq!(req["params"]["controllerKind"], "maho-cli");
            assert_eq!(req["params"]["autonomous"], true);
            let resp = json!({
                "jsonrpc": "2.0",
                "id": req["id"],
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "sessionInfo": {
                        "id": "agent-session",
                        "displayLabel": "Maho Agent",
                        "controllerKind": "maho-cli",
                        "autonomous": true
                    },
                    "capabilities": {"tools": {}}
                }
            });
            writer
                .write_all(format!("{resp}\n").as_bytes())
                .await
                .unwrap();
        });

        let client = BrowserClient::connect_autonomous_as(
            &sock_path,
            Duration::from_secs(1),
            ControllerKind::MahoCli,
        )
        .await
        .unwrap();
        let info = client.session_info().await.unwrap();
        assert!(info.autonomous);
        assert_eq!(info.display_label, "Maho Agent");
        server_handle.await.unwrap();
    }

    #[tokio::test]
    async fn autonomous_connect_rejects_legacy_server_without_ack() {
        let (_dir, sock_path, listener) = make_listener().await;
        let server_handle = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);
            let mut line = String::new();
            reader.read_line(&mut line).await.unwrap();
            let req: Value = serde_json::from_str(&line).unwrap();
            assert_eq!(req["params"]["autonomous"], true);
            let resp = json!({
                "jsonrpc": "2.0",
                "id": req["id"],
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "sessionInfo": {
                        "id": "legacy-session",
                        "displayLabel": "Maho CLI",
                        "controllerKind": "maho-cli"
                    },
                    "capabilities": {"tools": {}}
                }
            });
            writer
                .write_all(format!("{resp}\n").as_bytes())
                .await
                .unwrap();
        });

        let error = match BrowserClient::connect_autonomous_as(
            &sock_path,
            Duration::from_secs(1),
            ControllerKind::MahoCli,
        )
        .await
        {
            Ok(_) => panic!("legacy browser must not admit an autonomous client"),
            Err(error) => error,
        };
        assert!(matches!(error, McpBridgeError::HandshakeMismatch { .. }));
        assert!(error.to_string().contains("sessionInfo.autonomous=true"));
        server_handle.await.unwrap();
    }

    #[tokio::test]
    async fn human_connect_accepts_legacy_server_without_autonomy_ack() {
        let (_dir, sock_path, listener) = make_listener().await;
        let server_handle = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);
            let mut line = String::new();
            reader.read_line(&mut line).await.unwrap();
            let req: Value = serde_json::from_str(&line).unwrap();
            assert!(req["params"].get("autonomous").is_none());
            let resp = json!({
                "jsonrpc": "2.0",
                "id": req["id"],
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "sessionInfo": {
                        "id": "legacy-session",
                        "displayLabel": "Maho CLI",
                        "controllerKind": "maho-cli"
                    },
                    "capabilities": {"tools": {}}
                }
            });
            writer
                .write_all(format!("{resp}\n").as_bytes())
                .await
                .unwrap();
        });

        let client =
            BrowserClient::connect_as(&sock_path, Duration::from_secs(1), ControllerKind::MahoCli)
                .await
                .unwrap();
        assert!(!client.session_info().await.unwrap().autonomous);
        server_handle.await.unwrap();
    }

    #[tokio::test]
    async fn autonomous_reconnect_fails_closed_without_ack() {
        let (_dir, sock_path, listener) = make_listener().await;
        let server_handle = tokio::spawn(async move {
            let (first, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = first.into_split();
            let mut reader = BufReader::new(reader);
            let mut line = String::new();
            reader.read_line(&mut line).await.unwrap();
            let init: Value = serde_json::from_str(&line).unwrap();
            assert_eq!(init["params"]["autonomous"], true);
            let ack = json!({
                "jsonrpc": "2.0",
                "id": init["id"],
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "sessionInfo": {
                        "id": "first-agent-session",
                        "displayLabel": "Maho Agent",
                        "controllerKind": "maho-cli",
                        "autonomous": true
                    },
                    "capabilities": {"tools": {}}
                }
            });
            writer
                .write_all(format!("{ack}\n").as_bytes())
                .await
                .unwrap();

            line.clear();
            reader.read_line(&mut line).await.unwrap();
            let request: Value = serde_json::from_str(&line).unwrap();
            assert_eq!(request["method"], "retry_me");
            drop(reader);
            drop(writer);

            let (second, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = second.into_split();
            let mut reader = BufReader::new(reader);
            let mut line = String::new();
            reader.read_line(&mut line).await.unwrap();
            let reinit: Value = serde_json::from_str(&line).unwrap();
            assert_eq!(reinit["params"]["autonomous"], true);
            let legacy = json!({
                "jsonrpc": "2.0",
                "id": reinit["id"],
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "sessionInfo": {
                        "id": "legacy-reconnect",
                        "displayLabel": "Maho CLI",
                        "controllerKind": "maho-cli"
                    },
                    "capabilities": {"tools": {}}
                }
            });
            writer
                .write_all(format!("{legacy}\n").as_bytes())
                .await
                .unwrap();
        });

        let client = BrowserClient::connect_autonomous_as(
            &sock_path,
            Duration::from_secs(1),
            ControllerKind::MahoCli,
        )
        .await
        .unwrap();
        let error = client.call("retry_me", json!({})).await.unwrap_err();
        assert!(matches!(error, McpBridgeError::HandshakeMismatch { .. }));
        assert!(error.to_string().contains("sessionInfo.autonomous=true"));
        server_handle.await.unwrap();
    }

    /// SC4: client.rs timeout — timed-out requests are removed from correlation state.
    #[tokio::test]
    async fn timeout_on_non_responsive_server_cleans_pending_request() {
        let (_dir, sock_path, listener) = make_listener().await;
        let (request_seen_tx, request_seen_rx) = oneshot::channel();
        let (release_tx, release_rx) = oneshot::channel();

        let server_handle = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);

            let mut line = String::new();
            reader.read_line(&mut line).await.unwrap();
            let req: Value = serde_json::from_str(&line).unwrap();
            let id = req["id"].as_i64().unwrap();
            let resp = json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "capabilities": {"tools": {}}
                }
            });
            let mut buf = serde_json::to_vec(&resp).unwrap();
            buf.push(b'\n');
            writer.write_all(&buf).await.unwrap();

            line.clear();
            reader.read_line(&mut line).await.unwrap();
            request_seen_tx.send(()).unwrap();
            let _ = release_rx.await;
        });

        let client = BrowserClient::connect_with_timeout(&sock_path, Duration::from_secs(5))
            .await
            .unwrap();
        let call = client.call_without_reconnect_with_timeout(
            "slow_method",
            json!({}),
            Duration::from_millis(50),
        );
        let (err, ()) = tokio::join!(async { call.await.unwrap_err() }, async {
            request_seen_rx.await.unwrap()
        });
        assert!(
            matches!(err, McpBridgeError::Timeout(_)),
            "expected Timeout, got: {err:?}"
        );
        assert!(client.pending.lock().await.is_empty());

        release_tx.send(()).unwrap();
        server_handle.await.unwrap();
    }

    struct FailingReader;

    impl AsyncRead for FailingReader {
        fn poll_read(
            self: Pin<&mut Self>,
            _cx: &mut Context<'_>,
            _buf: &mut ReadBuf<'_>,
        ) -> Poll<io::Result<()>> {
            Poll::Ready(Err(io::Error::new(
                io::ErrorKind::ConnectionReset,
                "injected read failure",
            )))
        }
    }

    #[tokio::test]
    async fn read_failure_drains_pending_requests() {
        let pending = Arc::new(Mutex::new(HashMap::new()));
        let (tx, rx) = oneshot::channel();
        pending.lock().await.insert(7, tx);

        spawn_read_loop(FailingReader, Arc::clone(&pending))
            .await
            .unwrap();

        assert!(pending.lock().await.is_empty());
        assert!(rx.await.is_err(), "pending receiver must be closed");
    }

    #[tokio::test]
    async fn eof_reconnects_and_retries_without_waiting_for_request_timeout() {
        let (_dir, sock_path, listener) = make_listener().await;

        let server_handle = tokio::spawn(async move {
            let (first, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = first.into_split();
            let mut reader = BufReader::new(reader);
            let mut line = String::new();
            reader.read_line(&mut line).await.unwrap();
            let init: Value = serde_json::from_str(&line).unwrap();
            let response = json!({
                "jsonrpc": "2.0",
                "id": init["id"],
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "capabilities": {"tools": {}}
                }
            });
            let mut bytes = serde_json::to_vec(&response).unwrap();
            bytes.push(b'\n');
            writer.write_all(&bytes).await.unwrap();
            line.clear();
            reader.read_line(&mut line).await.unwrap();
            drop(reader);
            drop(writer);

            let (second, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = second.into_split();
            let mut reader = BufReader::new(reader);
            line.clear();
            reader.read_line(&mut line).await.unwrap();
            let init: Value = serde_json::from_str(&line).unwrap();
            let response = json!({
                "jsonrpc": "2.0",
                "id": init["id"],
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "capabilities": {"tools": {}}
                }
            });
            let mut bytes = serde_json::to_vec(&response).unwrap();
            bytes.push(b'\n');
            writer.write_all(&bytes).await.unwrap();

            line.clear();
            reader.read_line(&mut line).await.unwrap();
            let retry: Value = serde_json::from_str(&line).unwrap();
            let response = json!({
                "jsonrpc": "2.0",
                "id": retry["id"],
                "result": {"reconnected": true}
            });
            let mut bytes = serde_json::to_vec(&response).unwrap();
            bytes.push(b'\n');
            writer.write_all(&bytes).await.unwrap();
        });

        let client = BrowserClient::connect_with_timeout(&sock_path, Duration::from_secs(5))
            .await
            .unwrap();
        assert_eq!(
            client.call("retry_me", json!({})).await.unwrap(),
            json!({"reconnected": true})
        );
        assert!(client.pending.lock().await.is_empty());
        server_handle.await.unwrap();
    }

    #[tokio::test]
    async fn tool_call_is_not_replayed_after_disconnect() {
        let (_dir, sock_path, listener) = make_listener().await;

        let server_handle = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);
            let mut line = String::new();
            reader.read_line(&mut line).await.unwrap();
            let init: Value = serde_json::from_str(&line).unwrap();
            let response = json!({
                "jsonrpc": "2.0",
                "id": init["id"],
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "capabilities": {"tools": {}}
                }
            });
            let mut bytes = serde_json::to_vec(&response).unwrap();
            bytes.push(b'\n');
            writer.write_all(&bytes).await.unwrap();
            line.clear();
            reader.read_line(&mut line).await.unwrap();
            drop(reader);
            drop(writer);

            assert!(
                tokio::time::timeout(Duration::from_millis(100), listener.accept())
                    .await
                    .is_err()
            );
        });

        let client = BrowserClient::connect_with_timeout(&sock_path, Duration::from_secs(1))
            .await
            .unwrap();
        let err = client
            .call_tool("browser_navigate", json!({"url":"https://example.com"}))
            .await
            .unwrap_err();
        assert!(matches!(err, McpBridgeError::HandlerClosed));
        server_handle.await.unwrap();
    }

    #[tokio::test]
    async fn write_failure_cleans_pending_request() {
        let (_dir, sock_path, listener) = make_listener().await;
        let server_handle = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);
            let mut line = String::new();
            reader.read_line(&mut line).await.unwrap();
            let init: Value = serde_json::from_str(&line).unwrap();
            let response = json!({
                "jsonrpc": "2.0",
                "id": init["id"],
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "capabilities": {"tools": {}}
                }
            });
            let mut bytes = serde_json::to_vec(&response).unwrap();
            bytes.push(b'\n');
            writer.write_all(&bytes).await.unwrap();
            reader.read_line(&mut line).await.unwrap();
        });

        let client = BrowserClient::connect(&sock_path).await.unwrap();
        client.writer.lock().await.shutdown().await.unwrap();
        let err = client
            .call_without_reconnect("cannot_write", json!({}))
            .await
            .unwrap_err();
        assert!(matches!(err, McpBridgeError::Io(_)));
        assert!(client.pending.lock().await.is_empty());
        server_handle.await.unwrap();
    }

    #[tokio::test]
    async fn shutdown_drains_pending_requests() {
        let (_dir, sock_path, listener) = make_listener().await;
        let server_handle = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);
            let mut line = String::new();
            reader.read_line(&mut line).await.unwrap();
            let init: Value = serde_json::from_str(&line).unwrap();
            let response = json!({
                "jsonrpc": "2.0",
                "id": init["id"],
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "capabilities": {"tools": {}}
                }
            });
            let mut bytes = serde_json::to_vec(&response).unwrap();
            bytes.push(b'\n');
            writer.write_all(&bytes).await.unwrap();
            assert_eq!(reader.read_line(&mut line).await.unwrap(), 0);
        });

        let client = BrowserClient::connect(&sock_path).await.unwrap();
        let pending = Arc::clone(&client.pending);
        let (tx, rx) = oneshot::channel();
        pending.lock().await.insert(99, tx);

        client.shutdown().await.unwrap();

        assert!(pending.lock().await.is_empty());
        assert!(rx.await.is_err(), "shutdown must close pending receiver");
        server_handle.await.unwrap();
    }

    #[test]
    fn reconnectable_transport_errors_are_classified() {
        for kind in [
            io::ErrorKind::BrokenPipe,
            io::ErrorKind::ConnectionReset,
            io::ErrorKind::ConnectionAborted,
            io::ErrorKind::UnexpectedEof,
            io::ErrorKind::NotConnected,
            io::ErrorKind::WriteZero,
        ] {
            let err = McpBridgeError::Io(io::Error::from(kind));
            assert!(should_retry_after_reconnect(&err), "{kind:?}");
        }
        assert!(should_retry_after_reconnect(&McpBridgeError::HandlerClosed));
        assert!(!should_retry_after_reconnect(&McpBridgeError::Io(
            io::Error::from(io::ErrorKind::PermissionDenied)
        )));
    }

    /// SC5: client.rs error response — -32601 → Rpc { code: -32601, .. }.
    #[tokio::test]
    async fn error_response_maps_to_rpc_error() {
        let (_dir, sock_path, listener) = make_listener().await;

        tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);

            // Respond to initialize.
            let mut line = String::new();
            reader.read_line(&mut line).await.unwrap();
            let req: Value = serde_json::from_str(&line).unwrap();
            let id = req["id"].as_i64().unwrap();
            let resp = json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "capabilities": {"tools": {}}
                }
            });
            let mut buf = serde_json::to_vec(&resp).unwrap();
            buf.push(b'\n');
            writer.write_all(&buf).await.unwrap();

            // Respond with JSON-RPC error to next request.
            line.clear();
            reader.read_line(&mut line).await.unwrap();
            let req2: Value = serde_json::from_str(&line).unwrap();
            let id2 = req2["id"].as_i64().unwrap();
            let err_resp = json!({
                "jsonrpc": "2.0",
                "id": id2,
                "error": {"code": -32601, "message": "Method not found"}
            });
            let mut buf2 = serde_json::to_vec(&err_resp).unwrap();
            buf2.push(b'\n');
            writer.write_all(&buf2).await.unwrap();
        });

        let client = BrowserClient::connect(&sock_path).await.unwrap();
        let err = client.call("nonexistent", json!({})).await.unwrap_err();
        match err {
            McpBridgeError::Rpc { code, .. } => assert_eq!(code, -32601),
            other => panic!("expected Rpc error, got: {other:?}"),
        }
    }

    /// SC6: structured `error.data` is preserved end-to-end so policy
    /// classification never depends on English prose.
    #[tokio::test]
    async fn rpc_error_preserves_structured_data() {
        let (_dir, sock_path, listener) = make_listener().await;

        tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);

            // Initialize.
            let mut line = String::new();
            reader.read_line(&mut line).await.unwrap();
            let req: Value = serde_json::from_str(&line).unwrap();
            let id = req["id"].as_i64().unwrap();
            let resp = json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "capabilities": {"tools": {}}
                }
            });
            let mut buf = serde_json::to_vec(&resp).unwrap();
            buf.push(b'\n');
            writer.write_all(&buf).await.unwrap();

            // Respond with a domain-approval error that includes structured data.
            line.clear();
            reader.read_line(&mut line).await.unwrap();
            let req2: Value = serde_json::from_str(&line).unwrap();
            let id2 = req2["id"].as_i64().unwrap();
            let err_resp = json!({
                "jsonrpc": "2.0",
                "id": id2,
                "error": {
                    "code": -32000,
                    "message": "Navigation blocked: host not in allowed_domains",
                    "data": {
                        "status": "domain_not_allowed",
                        "host": "example.com"
                    }
                }
            });
            let mut buf2 = serde_json::to_vec(&err_resp).unwrap();
            buf2.push(b'\n');
            writer.write_all(&buf2).await.unwrap();
        });

        let client = BrowserClient::connect(&sock_path).await.unwrap();
        let err = client
            .call(
                "tools/call",
                json!({"name":"browser_navigate","arguments":{"url":"https://example.com"}}),
            )
            .await
            .unwrap_err();
        match err {
            McpBridgeError::Rpc { code, data, .. } => {
                assert_eq!(code, -32000);
                // Structured data must survive the transport.
                let d = data.expect("rpc error must carry structured data");
                assert_eq!(d["status"].as_str(), Some("domain_not_allowed"));
                assert_eq!(d["host"].as_str(), Some("example.com"));
            }
            other => panic!("expected Rpc error, got: {other:?}"),
        }
    }

    /// SC7: client.rs out-of-order correlation — fire 3 requests, server responds in reverse.
    #[tokio::test]
    async fn out_of_order_correlation() {
        let (_dir, sock_path, listener) = make_listener().await;

        tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);

            // Handle initialize.
            let mut line = String::new();
            reader.read_line(&mut line).await.unwrap();
            let req: Value = serde_json::from_str(&line).unwrap();
            let id = req["id"].as_i64().unwrap();
            let resp = json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                    "capabilities": {"tools": {}}
                }
            });
            let mut buf = serde_json::to_vec(&resp).unwrap();
            buf.push(b'\n');
            writer.write_all(&buf).await.unwrap();

            // Read 3 requests, store them all.
            let mut requests = Vec::new();
            for _ in 0..3 {
                line.clear();
                reader.read_line(&mut line).await.unwrap();
                let r: Value = serde_json::from_str(&line).unwrap();
                requests.push(r);
            }

            // Respond in REVERSE order.
            for r in requests.iter().rev() {
                let rid = r["id"].as_i64().unwrap();
                let method = r["method"].as_str().unwrap();
                let result_resp = json!({
                    "jsonrpc": "2.0",
                    "id": rid,
                    "result": {"echo": method}
                });
                let mut b = serde_json::to_vec(&result_resp).unwrap();
                b.push(b'\n');
                writer.write_all(&b).await.unwrap();
            }
        });

        let client = BrowserClient::connect(&sock_path).await.unwrap();

        // Fire 3 calls concurrently.
        let (r1, r2, r3) = tokio::join!(
            client.call("method_a", json!({})),
            client.call("method_b", json!({})),
            client.call("method_c", json!({})),
        );

        assert_eq!(r1.unwrap(), json!({"echo": "method_a"}));
        assert_eq!(r2.unwrap(), json!({"echo": "method_b"}));
        assert_eq!(r3.unwrap(), json!({"echo": "method_c"}));
    }

    /// Proves that `call_capability` directly sends the `tools/call` envelope with the
    /// canonical capability ID without performing any `tools/list` discovery round-trip.
    #[tokio::test]
    async fn direct_capability_call_bypasses_discovery_roundtrip() {
        let (_dir, sock_path, listener) = make_listener().await;
        let recorded_requests = Arc::new(Mutex::new(Vec::new()));
        let recorded_for_server = Arc::clone(&recorded_requests);

        let server_handle = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);
            let mut line = String::new();

            loop {
                line.clear();
                let bytes = reader.read_line(&mut line).await.unwrap();
                if bytes == 0 {
                    break;
                }
                let req: Value = serde_json::from_str(&line).unwrap();
                let id = req["id"].as_i64().unwrap();
                let method = req["method"].as_str().unwrap().to_string();
                recorded_for_server
                    .lock()
                    .await
                    .push((method.clone(), req.clone()));

                let resp = match method.as_str() {
                    "initialize" => json!({
                        "jsonrpc": "2.0",
                        "id": id,
                        "result": {
                            "protocolVersion": "2025-03-26",
                            "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                            "capabilities": {"tools": {}}
                        }
                    }),
                    "tools/call" => {
                        let name = req["params"]["name"].as_str().unwrap();
                        json!({
                            "jsonrpc": "2.0",
                            "id": id,
                            "result": {"status": "ok", "capability": name}
                        })
                    }
                    other => panic!("unexpected method sent to server: {other}"),
                };
                let mut buf = serde_json::to_vec(&resp).unwrap();
                buf.push(b'\n');
                writer.write_all(&buf).await.unwrap();
            }
        });

        let client = BrowserClient::connect(&sock_path).await.unwrap();
        let result = client
            .call_capability("tab.list", json!({"filter": "active"}))
            .await
            .unwrap();

        assert_eq!(result, json!({"status": "ok", "capability": "tab.list"}));

        // Inspect all received frames on the stub server
        let requests = recorded_requests.lock().await;
        let methods: Vec<&str> = requests.iter().map(|(m, _)| m.as_str()).collect();

        // Must ONLY have received initialize and tools/call - NO tools/list discovery frame
        assert_eq!(methods, vec!["initialize", "tools/call"]);
        assert!(
            !methods.contains(&"tools/list"),
            "direct capability call path must never send tools/list discovery frame"
        );

        let call_frame = &requests[1].1;
        assert_eq!(call_frame["params"]["name"], "tab.list");
        assert_eq!(
            call_frame["params"]["arguments"],
            json!({"filter": "active"})
        );

        drop(client);
        server_handle.await.unwrap();
    }

    static PERF_ENV_MUTEX: std::sync::Mutex<()> = std::sync::Mutex::new(());

    /// Proves that when `MAHO_BROWSER_PERF_TRACE=1`, JSON perf trace lines are emitted to stderr
    /// per request with the required keys (tool, connect_ms, handshake_ms, wire_bytes_in, wire_bytes_out, total_ms).
    #[tokio::test]
    async fn perf_trace_emitted_when_env_flag_set() {
        let _guard = match PERF_ENV_MUTEX.lock() {
            Ok(g) => g,
            Err(p) => p.into_inner(),
        };
        std::env::set_var("MAHO_BROWSER_PERF_TRACE", "1");

        let recorded_traces = Arc::new(std::sync::Mutex::new(Vec::new()));
        let traces_for_sink = Arc::clone(&recorded_traces);
        {
            let mut sink = TEST_TRACE_SINK.lock().unwrap();
            *sink = Some(Arc::new(move |line: &str| {
                traces_for_sink.lock().unwrap().push(line.to_string());
            }));
        }

        let (_dir, sock_path, listener) = make_listener().await;
        let server_handle = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);
            let mut line = String::new();

            loop {
                line.clear();
                let bytes = reader.read_line(&mut line).await.unwrap();
                if bytes == 0 {
                    break;
                }
                let req: Value = serde_json::from_str(&line).unwrap();
                let id = req["id"].as_i64().unwrap();
                let method = req["method"].as_str().unwrap();
                let resp = match method {
                    "initialize" => json!({
                        "jsonrpc": "2.0",
                        "id": id,
                        "result": {
                            "protocolVersion": "2025-03-26",
                            "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                            "capabilities": {"tools": {}}
                        }
                    }),
                    "tools/call" => json!({
                        "jsonrpc": "2.0",
                        "id": id,
                        "result": {"status": "ok"}
                    }),
                    other => panic!("unexpected method: {other}"),
                };
                let mut buf = serde_json::to_vec(&resp).unwrap();
                buf.push(b'\n');
                writer.write_all(&buf).await.unwrap();
            }
        });

        let client = BrowserClient::connect(&sock_path).await.unwrap();
        let result = client
            .call_capability("tab.list", json!({"filter": "all"}))
            .await
            .unwrap();
        assert_eq!(result, json!({"status": "ok"}));

        // Clear trace sink and reset env
        {
            let mut sink = TEST_TRACE_SINK.lock().unwrap();
            *sink = None;
        }
        std::env::remove_var("MAHO_BROWSER_PERF_TRACE");

        let traces = recorded_traces.lock().unwrap().clone();
        assert!(
            !traces.is_empty(),
            "expected at least one perf trace JSON line"
        );

        let parsed_traces: Vec<Value> = traces
            .iter()
            .map(|l| serde_json::from_str::<Value>(l).expect("trace line must be valid JSON"))
            .collect();

        let cap_trace = parsed_traces
            .iter()
            .find(|t| t["tool"] == "tab.list")
            .expect("should find perf trace for tool 'tab.list'");

        assert_eq!(cap_trace["tool"], "tab.list");
        assert!(cap_trace["connect_ms"].as_f64().is_some());
        assert!(cap_trace["handshake_ms"].as_f64().is_some());
        assert!(cap_trace["wire_bytes_in"].as_u64().unwrap() > 0);
        assert!(cap_trace["wire_bytes_out"].as_u64().unwrap() > 0);
        assert!(cap_trace["total_ms"].as_f64().is_some());

        drop(client);
        server_handle.await.unwrap();
    }

    /// Proves that when `MAHO_BROWSER_PERF_TRACE` is unset (or not 1), NO trace lines are emitted to stderr.
    #[tokio::test]
    async fn perf_trace_absent_when_env_flag_unset() {
        let _guard = match PERF_ENV_MUTEX.lock() {
            Ok(g) => g,
            Err(p) => p.into_inner(),
        };
        std::env::remove_var("MAHO_BROWSER_PERF_TRACE");

        let recorded_traces = Arc::new(std::sync::Mutex::new(Vec::new()));
        let traces_for_sink = Arc::clone(&recorded_traces);
        {
            let mut sink = TEST_TRACE_SINK.lock().unwrap();
            *sink = Some(Arc::new(move |line: &str| {
                traces_for_sink.lock().unwrap().push(line.to_string());
            }));
        }

        let (_dir, sock_path, listener) = make_listener().await;
        let server_handle = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);
            let mut line = String::new();

            loop {
                line.clear();
                let bytes = reader.read_line(&mut line).await.unwrap();
                if bytes == 0 {
                    break;
                }
                let req: Value = serde_json::from_str(&line).unwrap();
                let id = req["id"].as_i64().unwrap();
                let method = req["method"].as_str().unwrap();
                let resp = match method {
                    "initialize" => json!({
                        "jsonrpc": "2.0",
                        "id": id,
                        "result": {
                            "protocolVersion": "2025-03-26",
                            "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                            "capabilities": {"tools": {}}
                        }
                    }),
                    "tools/call" => json!({
                        "jsonrpc": "2.0",
                        "id": id,
                        "result": {"status": "ok"}
                    }),
                    other => panic!("unexpected method: {other}"),
                };
                let mut buf = serde_json::to_vec(&resp).unwrap();
                buf.push(b'\n');
                writer.write_all(&buf).await.unwrap();
            }
        });

        let client = BrowserClient::connect(&sock_path).await.unwrap();
        let result = client
            .call_capability("tab.list", json!({"filter": "all"}))
            .await
            .unwrap();
        assert_eq!(result, json!({"status": "ok"}));

        // Clear trace sink
        {
            let mut sink = TEST_TRACE_SINK.lock().unwrap();
            *sink = None;
        }

        let traces = recorded_traces.lock().unwrap().clone();
        assert!(
            traces.is_empty(),
            "expected NO perf trace lines when env is unset, got: {traces:?}"
        );

        drop(client);
        server_handle.await.unwrap();
    }
}
