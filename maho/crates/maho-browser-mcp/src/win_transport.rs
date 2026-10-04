// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Windows-specific Named Pipe transport for the Maho Browser MCP client.
//!
//! The browser's Windows MCP server (`maho_mcp_pipe_server_win.cc`) listens on
//! a byte-mode named pipe at `\\.\pipe\maho-browser-<username>` with framing
//! identical to the Unix domain socket transport: newline-delimited JSON-RPC
//! 2.0 messages. This module opens the client end of that pipe and exposes the
//! same split read/write halves the shared client read loop expects.

use std::path::Path;
use std::time::Duration;

use tokio::io::{ReadHalf, WriteHalf};
use tokio::net::windows::named_pipe::{ClientOptions, NamedPipeClient};

use crate::error::{McpBridgeError, Result};

/// Type alias for the read half of a Windows named pipe.
pub type ReaderHalf = ReadHalf<NamedPipeClient>;

/// Type alias for the write half of a Windows named pipe.
pub type WriterHalf = WriteHalf<NamedPipeClient>;

/// Delay between connection attempts while the server pipe instance is busy.
const PIPE_BUSY_RETRY_DELAY: Duration = Duration::from_millis(50);

/// Maximum total time to wait for a free pipe instance before giving up.
const PIPE_CONNECT_TIMEOUT: Duration = Duration::from_secs(10);

/// Connect to the browser's MCP named pipe, retrying while the pipe is busy.
///
/// A Windows named pipe server only exposes a bounded set of instances, and it
/// briefly has no free instance available in the window between accepting a
/// client and calling `CreateNamedPipeW` for the next one. During that window
/// `open` fails with `ERROR_PIPE_BUSY`. The canonical client pattern — mirrored
/// here — is to wait briefly and retry until an instance becomes available,
/// bounded by [`PIPE_CONNECT_TIMEOUT`] so a wedged server cannot hang the
/// client forever. Any other error (missing server, access denied, etc.) is
/// surfaced immediately as [`McpBridgeError::SocketConnect`].
pub async fn connect(path: &Path) -> Result<NamedPipeClient> {
    use windows_sys::Win32::Foundation::ERROR_PIPE_BUSY;

    let deadline = tokio::time::Instant::now() + PIPE_CONNECT_TIMEOUT;

    loop {
        match ClientOptions::new().open(path) {
            Ok(client) => return Ok(client),
            Err(err) if err.raw_os_error() == Some(ERROR_PIPE_BUSY as i32) => {
                if tokio::time::Instant::now() >= deadline {
                    return Err(McpBridgeError::SocketConnect(err));
                }
                tokio::time::sleep(PIPE_BUSY_RETRY_DELAY).await;
            }
            Err(err) => return Err(McpBridgeError::SocketConnect(err)),
        }
    }
}
