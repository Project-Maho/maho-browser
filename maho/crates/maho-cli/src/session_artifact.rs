// Copyright 2026 The Maho Authors. All rights reserved.

//! Typed CLI access to shared agent sessions and browser-owned artifacts.
//!
//! Session inspection uses the same encrypted conversation store as the desktop
//! agent. Artifact bytes and export authorization remain browser-owned and use
//! the non-model-callable control plane.

use std::path::{Component, Path, PathBuf};
use std::time::Duration;

use maho_browser_mcp::client::BrowserClient;
use maho_browser_mcp::error::McpBridgeError;
use maho_browser_mcp::paths::default_socket_path;
use maho_browser_mcp::protocol::ControllerKind;
use maho_core::conversation_manager::ConversationManager;
use maho_storage::sqlite::SqliteStorage;
use maho_types::chat::ConversationListState;
use serde_json::{json, Value};
use thiserror::Error;

const CONTROL_CALL_METHOD: &str = "maho/control/call";

#[derive(Debug, Error)]
pub enum CliControlError {
    #[error("browser is offline; start Maho and retry")]
    BrowserOffline,
    #[error("invalid request: {0}")]
    InvalidRequest(String),
    #[error("{operation} failed ({code}): {message}")]
    BrowserRejected {
        operation: String,
        code: i64,
        message: String,
        data: Option<Value>,
    },
    #[error("{operation} failed: {message}")]
    Transport { operation: String, message: String },
}

impl CliControlError {
    pub fn status(&self) -> &str {
        match self {
            Self::BrowserOffline => "browser_offline",
            Self::InvalidRequest(_) => "invalid_request",
            Self::BrowserRejected { data, .. } => data
                .as_ref()
                .and_then(|value| value.get("status"))
                .and_then(Value::as_str)
                .unwrap_or("browser_rejected"),
            Self::Transport { .. } => "transport_error",
        }
    }

    pub fn data(&self) -> Option<&Value> {
        match self {
            Self::BrowserRejected { data, .. } => data.as_ref(),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ControlOperation {
    ArtifactList,
    ArtifactExport,
}

impl ControlOperation {
    pub const fn capability_id(self) -> &'static str {
        match self {
            Self::ArtifactList => "artifact.list",
            Self::ArtifactExport => "artifact.export",
        }
    }
}

pub fn list_sessions(storage: &SqliteStorage) -> Value {
    json!({
        "sessions": ConversationManager::list_v2(storage, ConversationListState::All, 1000)
    })
}

pub fn session_history(
    storage: &SqliteStorage,
    session_id: &str,
) -> Result<Value, CliControlError> {
    if !storage
        .conversation_exists(session_id)
        .map_err(|error| CliControlError::InvalidRequest(error.to_string()))?
    {
        return Err(CliControlError::InvalidRequest(format!(
            "unknown session '{session_id}'"
        )));
    }
    Ok(json!({
        "sessionId": session_id,
        "messages": ConversationManager::get_messages(storage, session_id),
    }))
}

pub fn resume_session(storage: &SqliteStorage, session_id: &str) -> Result<Value, CliControlError> {
    let sessions = ConversationManager::list_v2(storage, ConversationListState::All, 1000);
    let session = sessions
        .into_iter()
        .find(|session| session.id == session_id)
        .ok_or_else(|| {
            CliControlError::InvalidRequest(format!("unknown session '{session_id}'"))
        })?;
    if session.archived_at.is_some() {
        let now = chrono::Utc::now().to_rfc3339();
        ConversationManager::unarchive(storage, session_id, &now);
    }
    let messages = ConversationManager::get_messages(storage, session_id);
    Ok(json!({"session": session, "messages": messages, "resumed": true}))
}

pub async fn connect(override_path: Option<&str>) -> Result<BrowserClient, CliControlError> {
    let path = override_path
        .map(PathBuf::from)
        .unwrap_or_else(default_socket_path);
    BrowserClient::connect_as(&path, Duration::from_secs(15), ControllerKind::MahoCli)
        .await
        .map_err(|error| map_browser_error("connect", error))
}

pub async fn call(
    client: &BrowserClient,
    operation: ControlOperation,
    arguments: Value,
) -> Result<Value, CliControlError> {
    let capability_id = operation.capability_id();
    client
        .call_without_reconnect(
            CONTROL_CALL_METHOD,
            json!({"name": capability_id, "arguments": arguments}),
        )
        .await
        .map_err(|error| map_browser_error(capability_id, error))
}

fn map_browser_error(operation: &str, error: McpBridgeError) -> CliControlError {
    match error {
        McpBridgeError::SocketConnect(_) => CliControlError::BrowserOffline,
        McpBridgeError::Rpc {
            code,
            message,
            data,
        } => CliControlError::BrowserRejected {
            operation: operation.to_string(),
            code: i64::from(code),
            message,
            data,
        },
        other => CliControlError::Transport {
            operation: operation.to_string(),
            message: other.to_string(),
        },
    }
}

/// Validate an export target without creating it. The browser performs the
/// authoritative containment and write checks; this catches malformed CLI
/// input before any control request is sent.
pub fn validate_export_destination(path: &Path) -> Result<PathBuf, CliControlError> {
    if path.as_os_str().is_empty() {
        return Err(CliControlError::InvalidRequest(
            "export destination cannot be empty".to_string(),
        ));
    }
    if path
        .components()
        .any(|component| component == Component::ParentDir)
    {
        return Err(CliControlError::InvalidRequest(
            "export destination must not contain '..'".to_string(),
        ));
    }

    let absolute = if path.is_absolute() {
        path.to_path_buf()
    } else {
        std::env::current_dir()
            .map_err(|error| CliControlError::InvalidRequest(error.to_string()))?
            .join(path)
    };
    let parent = absolute.parent().ok_or_else(|| {
        CliControlError::InvalidRequest("export destination has no parent directory".to_string())
    })?;
    let canonical_parent = parent.canonicalize().map_err(|error| {
        CliControlError::InvalidRequest(format!(
            "export destination parent '{}' is unavailable: {error}",
            parent.display()
        ))
    })?;
    let file_name = absolute.file_name().ok_or_else(|| {
        CliControlError::InvalidRequest("export destination must name a file".to_string())
    })?;
    Ok(canonical_parent.join(file_name))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn operation_ids_are_stable() {
        assert_eq!(
            ControlOperation::ArtifactList.capability_id(),
            "artifact.list"
        );
        assert_eq!(
            ControlOperation::ArtifactExport.capability_id(),
            "artifact.export"
        );
    }

    #[test]
    fn shared_storage_sessions_include_history() {
        let storage = SqliteStorage::open_in_memory_with_key("cli-session-test-key").unwrap();
        storage
            .create_conversation("session-1", Some("Research"), None, Some("test-model"))
            .unwrap();
        storage
            .insert_conversation_turn("turn-1", "session-1", "user", "hello", None)
            .unwrap();

        let listed = list_sessions(&storage);
        assert_eq!(listed["sessions"][0]["id"], "session-1");
        let history = session_history(&storage, "session-1").unwrap();
        assert_eq!(history["messages"][0]["content"], "hello");
        let resumed = resume_session(&storage, "session-1").unwrap();
        assert_eq!(resumed["resumed"], true);
        assert_eq!(resumed["messages"][0]["role"], "user");
    }

    #[test]
    fn export_destination_rejects_parent_traversal() {
        let error = validate_export_destination(Path::new("../escape.txt"))
            .expect_err("parent traversal must fail");
        assert_eq!(error.status(), "invalid_request");
    }

    #[test]
    fn export_destination_resolves_against_existing_parent() {
        let dir = tempfile::tempdir().expect("tempdir");
        let destination = dir.path().join("report.txt");
        let validated = validate_export_destination(&destination).expect("valid destination");
        assert_eq!(validated.file_name(), destination.file_name());
        let canonical_parent = dir.path().canonicalize().unwrap();
        assert_eq!(validated.parent(), Some(canonical_parent.as_path()));
    }
}
