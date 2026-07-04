// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! rmcp `ServerHandler` implementation bridging stdio MCP to the browser's UDS socket.

use std::sync::Arc;

use rmcp::{
    ServerHandler,
    handler::server::{router::tool::ToolRouter, wrapper::Parameters},
    model::{ServerCapabilities, ServerInfo},
    schemars, tool, tool_handler, tool_router,
};
use serde::Deserialize;
use serde_json::json;

use crate::client::BrowserClient;
use crate::paths::default_socket_path;

#[derive(Debug, Deserialize, schemars::JsonSchema)]
/// Parameters for the `browser_ping` tool (empty).
pub struct BrowserPingParams {}

/// The MCP server that bridges stdio (rmcp) to the browser's UDS socket.
#[derive(Debug, Clone)]
pub struct MahoBrowserMcpServer {
    client: Arc<BrowserClient>,
    tool_router: ToolRouter<Self>,
}

#[tool_router]
impl MahoBrowserMcpServer {
    /// Create a new server, connecting to the browser's MCP socket.
    pub async fn new() -> crate::error::Result<Self> {
        let path = default_socket_path();
        let client = BrowserClient::connect(&path).await?;
        Ok(Self {
            client: Arc::new(client),
            tool_router: Self::tool_router(),
        })
    }

    #[cfg(test)]
    pub(crate) fn new_with_client(client: Arc<BrowserClient>) -> Self {
        Self {
            client,
            tool_router: Self::tool_router(),
        }
    }

    #[tool(description = "Ping the browser to verify the MCP bridge is operational")]
    async fn browser_ping(
        &self,
        #[allow(unused_variables)] Parameters(_params): Parameters<BrowserPingParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call("ping", json!({}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_else(|_| r#"{"pong":true}"#.to_string()))
    }
}

#[tool_handler]
impl ServerHandler for MahoBrowserMcpServer {
    fn get_info(&self) -> ServerInfo {
        ServerInfo::new(ServerCapabilities::builder().enable_tools().build())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
    use tokio::net::UnixListener;

    /// SC7: server.rs browser_ping via mock client — tool returns {"pong": true}.
    #[tokio::test]
    async fn browser_ping_via_mock_client() {
        let dir = tempfile::tempdir().unwrap();
        let sock_path = dir.path().join("test.sock");
        let listener = UnixListener::bind(&sock_path).unwrap();

        // Fake browser server.
        let handle = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);
            let mut line = String::new();

            // Handle initialize.
            reader.read_line(&mut line).await.unwrap();
            let req: serde_json::Value = serde_json::from_str(&line).unwrap();
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

            // Handle ping.
            line.clear();
            reader.read_line(&mut line).await.unwrap();
            let req2: serde_json::Value = serde_json::from_str(&line).unwrap();
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
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .browser_ping(Parameters(BrowserPingParams {}))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed, json!({"pong": true}));

        handle.await.unwrap();
    }
}
