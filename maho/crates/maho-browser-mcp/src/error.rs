// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Error types for the MCP bridge layer.

use std::io;
use std::path::PathBuf;
use std::time::Duration;

/// Errors that can occur in the MCP bridge between the stdio-facing
/// rmcp server and the UDS-connected browser.
#[derive(Debug, thiserror::Error)]
pub enum McpBridgeError {
    /// Failed to connect to the browser's Unix domain socket.
    #[error("socket connect failed: {0}")]
    SocketConnect(#[source] io::Error),

    /// Newline-delimited JSON framing error (incomplete line, invalid UTF-8).
    #[error("framing error: {0}")]
    Framing(String),

    /// The browser returned a JSON-RPC error response.
    #[error("rpc error {code}: {message}")]
    Rpc {
        /// JSON-RPC error code.
        code: i32,
        /// Human-readable error message.
        message: String,
        /// Structured error data carried by the JSON-RPC error object.
        /// Policy decisions must use this, never `message`.
        data: Option<serde_json::Value>,
    },

    /// The initialize handshake response did not match expectations.
    #[error("handshake mismatch: expected {expected:?}, got {got:?}")]
    HandshakeMismatch {
        /// Expected server name.
        expected: String,
        /// Actual server name received.
        got: String,
    },

    /// A request timed out waiting for a response.
    #[error("request timed out after {0:?}")]
    Timeout(Duration),

    /// Generic I/O error on the socket stream.
    #[error("io error: {0}")]
    Io(#[from] io::Error),

    /// JSON serialization/deserialization failure.
    #[error("serde error: {0}")]
    Serde(#[from] serde_json::Error),

    /// The handler (read loop) has been closed.
    #[error("handler closed")]
    HandlerClosed,

    /// Failed to read the DPAPI session token file.
    #[error("token file read failed at {path}: {source}")]
    TokenFileRead {
        /// Session-token file path that could not be read.
        path: PathBuf,
        /// Underlying filesystem error.
        #[source]
        source: io::Error,
    },

    /// Failed to decrypt the session token (DPAPI).
    #[error("token decrypt failed: {0}")]
    TokenDecrypt(String),
}

/// Convenience result alias for MCP bridge operations.
pub type Result<T> = std::result::Result<T, McpBridgeError>;
