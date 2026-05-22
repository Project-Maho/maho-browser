pub mod client;
pub mod events;
pub mod jsonrpc;
pub mod process;
pub mod session;
pub mod transport;
pub mod turn;
pub mod types;

pub use client::AcpClient;
pub use events::{
    AcpUpdate, AvailableCommand, PermissionOption, PermissionOutcome, PermissionRequest,
    PermissionToolCall, PlanEntry, PromptResult, PromptUsage, StopReason, ToolCallLocation,
    ToolCallStatus, ToolKind, UsageCost,
};
pub use jsonrpc::JsonRpcError;
pub use session::{McpServer, NewSessionResult};
pub use types::{ClientCapabilities, FsCapabilities, InitializeResult};

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),

    #[error("JSON error: {0}")]
    Json(#[from] serde_json::Error),

    #[error("line exceeds maximum length ({0} bytes > 64 KiB)")]
    LineTooLong(usize),

    #[error("invalid UTF-8 in transport line")]
    InvalidUtf8,

    #[error("embedded newline in JSON-RPC message")]
    EmbeddedNewline,

    #[error("transport closed")]
    TransportClosed,

    #[error("JSON-RPC error {code}: {message}")]
    JsonRpc { code: i64, message: String },

    #[error("protocol version mismatch: expected {expected}, got {got}")]
    ProtocolVersionMismatch { expected: u32, got: u32 },

    #[error("cancelled")]
    Cancelled,
}
