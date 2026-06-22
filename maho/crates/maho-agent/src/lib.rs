use async_trait::async_trait;
use maho_types::chat::ChatMessage;

pub mod permission;
pub mod runtime_isolation;
pub mod swiftide_backend;
pub mod tool_registry;

pub use permission::{PermissionDecision, PermissionRequest, PermissionCallback};
pub use runtime_isolation::SessionRuntime;
pub use swiftide_backend::SwiftideBackend;

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct ToolDefinition {
    pub name: String,
    pub description: String,
    pub input_schema: serde_json::Value,
}

#[derive(Debug, thiserror::Error)]
pub enum AgentError {
    #[error("Session not found: {0}")]
    SessionNotFound(String),
    #[error("Execution error: {0}")]
    ExecutionError(String),
    #[error("Cancelled")]
    Cancelled,
    #[error("Permission denied")]
    PermissionDenied,
}

pub struct ZeroizedString(secrecy::SecretString);

impl ZeroizedString {
    pub fn new(s: String) -> Self {
        Self(secrecy::SecretString::new(s.into_boxed_str()))
    }

    pub fn as_str(&self) -> &str {
        use secrecy::ExposeSecret;
        self.0.expose_secret()
    }

    pub fn secret(&self) -> &secrecy::SecretString {
        &self.0
    }
}

impl std::ops::Deref for ZeroizedString {
    type Target = str;
    fn deref(&self) -> &Self::Target {
        self.as_str()
    }
}

impl std::fmt::Debug for ZeroizedString {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[REDACTED KEY]")
    }
}

impl std::fmt::Display for ZeroizedString {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[REDACTED KEY]")
    }
}

#[async_trait]
pub trait AgentStorage: Send + Sync {
    async fn get_byok_key(&self, provider: &str) -> Result<Option<ZeroizedString>, AgentError>;
    async fn save_message(&self, session_id: &str, role: &str, content: &str) -> Result<(), AgentError>;
    async fn get_history(&self, session_id: &str) -> Result<Vec<ChatMessage>, AgentError>;
}

pub struct MutexAgentStorage(pub std::sync::Arc<std::sync::Mutex<maho_storage::sqlite::SqliteStorage>>);

#[async_trait]
impl AgentStorage for MutexAgentStorage {
    async fn get_byok_key(&self, provider: &str) -> Result<Option<ZeroizedString>, AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let provider = provider.to_string();
        let key_opt = tokio::task::spawn_blocking(move || {
            let lock = storage.lock().unwrap_or_else(std::sync::PoisonError::into_inner);
            let key = format!("byok:{provider}");
            lock.get_setting(&key)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))??;
        Ok(key_opt.map(ZeroizedString::new))
    }

    async fn save_message(&self, session_id: &str, role: &str, content: &str) -> Result<(), AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let session_id = session_id.to_string();
        let role = role.to_string();
        let content = content.to_string();
        tokio::task::spawn_blocking(move || {
            let lock = storage.lock().unwrap_or_else(std::sync::PoisonError::into_inner);
            let id = uuid::Uuid::new_v4().to_string();
            lock.insert_conversation_turn(&id, &session_id, &role, &content, None)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))?
    }

    async fn get_history(&self, session_id: &str) -> Result<Vec<ChatMessage>, AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let session_id = session_id.to_string();
        tokio::task::spawn_blocking(move || {
            let lock = storage.lock().unwrap_or_else(std::sync::PoisonError::into_inner);
            let turns = lock.get_conversation_messages(&session_id)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))?;
            let history = turns.into_iter().map(|turn| {
                let role = turn.role;
                let content = maho_types::chat::ChatContent::text(turn.content);
                if role == "user" {
                    ChatMessage::user(content)
                } else if role == "assistant" {
                    ChatMessage::assistant(content)
                } else {
                    ChatMessage::system(content)
                }
            }).collect();
            Ok(history)
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))?
    }
}

#[async_trait]
pub trait AgentRuntime: Send + Sync {
    async fn run_turn(
        &self,
        session_id: &str,
        message: ChatMessage,
        on_token: Option<Box<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>>,
    ) -> Result<ChatMessage, AgentError>;
    async fn cancel(&self, session_id: &str) -> Result<(), AgentError>;
    async fn list_tools(&self) -> Result<Vec<ToolDefinition>, AgentError>;
    fn set_permission_callback(
        &self,
        callback: Box<dyn Fn(PermissionRequest) -> std::pin::Pin<Box<dyn std::future::Future<Output = PermissionDecision> + Send>> + Send + Sync + 'static>,
    ) -> Result<(), AgentError>;
    fn set_secure_storage_callback(
        &self,
        callback: Box<dyn Fn(&str) -> Result<ZeroizedString, AgentError> + Send + Sync + 'static>,
    ) -> Result<(), AgentError>;
}

pub struct MockAgentRuntime;

#[async_trait]
impl AgentRuntime for MockAgentRuntime {
    async fn run_turn(
        &self,
        _session_id: &str,
        _message: ChatMessage,
        _on_token: Option<Box<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>>,
    ) -> Result<ChatMessage, AgentError> {
        Ok(ChatMessage::assistant(maho_types::chat::ChatContent::text("Mock response")))
    }

    async fn cancel(&self, _session_id: &str) -> Result<(), AgentError> {
        Ok(())
    }

    async fn list_tools(&self) -> Result<Vec<ToolDefinition>, AgentError> {
        Ok(tool_registry::get_platform_tools())
    }

    fn set_permission_callback(
        &self,
        _callback: Box<dyn Fn(PermissionRequest) -> std::pin::Pin<Box<dyn std::future::Future<Output = PermissionDecision> + Send>> + Send + Sync + 'static>,
    ) -> Result<(), AgentError> {
        Ok(())
    }

    fn set_secure_storage_callback(
        &self,
        _callback: Box<dyn Fn(&str) -> Result<ZeroizedString, AgentError> + Send + Sync + 'static>,
    ) -> Result<(), AgentError> {
        Ok(())
    }
}
