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
    pending: Arc<Mutex<HashMap<i64, oneshot::Sender<JsonRpcResponse>>>>,
    next_id: AtomicI64,
    timeout: Duration,
    controller_kind: ControllerKind,
    session_info: Mutex<Option<SessionInfo>>,
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
        let pending: Arc<Mutex<HashMap<i64, oneshot::Sender<JsonRpcResponse>>>> =
            Arc::new(Mutex::new(HashMap::new()));

        let (writer, read_task) = Self::open_stream(path, Arc::clone(&pending)).await?;

        let client = Self {
            path: path.to_path_buf(),
            writer: Mutex::new(writer),
            read_task: Mutex::new(read_task),
            pending,
            next_id: AtomicI64::new(1),
            timeout,
            controller_kind,
            session_info: Mutex::new(None),
        };

        client.perform_handshake().await?;
        Ok(client)
    }

    #[cfg(unix)]
    async fn open_stream(
        path: &Path,
        pending: Arc<Mutex<HashMap<i64, oneshot::Sender<JsonRpcResponse>>>>,
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
        pending: Arc<Mutex<HashMap<i64, oneshot::Sender<JsonRpcResponse>>>>,
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
    pending: Arc<Mutex<HashMap<i64, oneshot::Sender<JsonRpcResponse>>>>,
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
                Ok(_) => {
                    if let Ok(resp) = serde_json::from_str::<JsonRpcResponse>(&line) {
                        if let Some(ref id) = resp.id {
                            if let JsonRpcId::Num(n) = id {
                                let mut map = pending.lock().await;
                                if let Some(tx) = map.remove(n) {
                                    let _ = tx.send(resp);
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
        *self.session_info.lock().await = Some(init_result.session_info);
        Ok(())
    }

    async fn reconnect(&self) -> Result<()> {
        let (new_writer, new_read_task) =
            Self::open_stream(&self.path, Arc::clone(&self.pending)).await?;

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

        self.perform_handshake().await?;
        Ok(())
    }

    async fn call_no_retry(&self, method: &str, params: Value) -> Result<Value> {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed);
        let req = JsonRpcRequest::new(id, method, Some(params));

        let (tx, rx) = oneshot::channel();
        {
            let mut map = self.pending.lock().await;
            map.insert(id, tx);
        }

        if let Err(err) = self.write_message(&req).await {
            self.pending.lock().await.remove(&id);
            return Err(err);
        }

        let response = tokio::time::timeout(self.timeout, rx).await;
        self.pending.lock().await.remove(&id);
        let resp = response
            .map_err(|_| McpBridgeError::Timeout(self.timeout))?
            .map_err(|_| McpBridgeError::HandlerClosed)?;

        if let Some(err) = resp.error {
            return Err(McpBridgeError::Rpc {
                code: err.code,
                message: err.message,
                data: err.data,
            });
        }

        Ok(resp.result.unwrap_or(Value::Null))
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
        match self.call_no_retry(method, params.clone()).await {
            Err(err) if should_retry_after_reconnect(&err) => {
                tracing::warn!(
                    "BrowserClient: connection broken ({err:?}); attempting reconnect + retry"
                );
                self.reconnect().await?;
                self.call_no_retry(method, params).await
            }
            other => other,
        }
    }

    /// Send a request without reconnecting or replaying it after connection loss.
    ///
    /// Stateful callers use this when the MCP session owns domain approvals or
    /// tab leases, and when replaying a mutating request could duplicate effects.
    pub async fn call_without_reconnect(&self, method: &str, params: Value) -> Result<Value> {
        self.call_no_retry(method, params).await
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

    async fn write_message<T: serde::Serialize>(&self, msg: &T) -> Result<()> {
        let mut buf = serde_json::to_vec(msg)?;
        buf.push(b'\n');
        let mut writer = self.writer.lock().await;
        writer.write_all(&buf).await?;
        writer.flush().await?;
        Ok(())
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

        let client = BrowserClient::connect_with_timeout(&sock_path, Duration::from_millis(50))
            .await
            .unwrap();
        let call = client.call("slow_method", json!({}));
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
}
