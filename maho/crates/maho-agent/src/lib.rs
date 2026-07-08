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

pub struct SecureKeyBundle {
    pub key: ZeroizedString,
    pub base_url: Option<String>,
    pub model: Option<String>,
}

impl SecureKeyBundle {
    pub fn from_key(key: ZeroizedString) -> Self {
        Self { key, base_url: None, model: None }
    }
}

pub trait BrowserToolBridge: Send + Sync {
    fn invoke(&self, tool_name: &str, args_json: &str) -> Result<String, String>;
}

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
    async fn get_setting(&self, key: &str) -> Result<Option<ZeroizedString>, AgentError>;
    async fn save_message(&self, session_id: &str, role: &str, content: &str) -> Result<(), AgentError>;
    async fn get_history(&self, session_id: &str) -> Result<Vec<ChatMessage>, AgentError>;
}

pub struct MutexAgentStorage(pub std::sync::Arc<std::sync::Mutex<maho_storage::sqlite::SqliteStorage>>);

#[async_trait]
impl AgentStorage for MutexAgentStorage {
    async fn get_byok_key(&self, provider: &str) -> Result<Option<ZeroizedString>, AgentError> {
        tracing::info!(
            target = "maho_agent::audit",
            event = "byok_key_access",
            provider = %provider,
        );
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

    async fn get_setting(&self, key: &str) -> Result<Option<ZeroizedString>, AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let key = key.to_string();
        let val_opt = tokio::task::spawn_blocking(move || {
            let lock = storage.lock().unwrap_or_else(std::sync::PoisonError::into_inner);
            lock.get_setting(&key)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))??;
        Ok(val_opt.map(ZeroizedString::new))
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
        callback: Box<dyn Fn(&str) -> Result<SecureKeyBundle, AgentError> + Send + Sync + 'static>,
    ) -> Result<(), AgentError>;

    fn set_browser_tool_bridge(
        &self,
        _bridge: std::sync::Arc<dyn BrowserToolBridge>,
    ) -> Result<(), AgentError> {
        Ok(())
    }
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
        _callback: Box<dyn Fn(&str) -> Result<SecureKeyBundle, AgentError> + Send + Sync + 'static>,
    ) -> Result<(), AgentError> {
        Ok(())
    }
}

pub fn init_audit_logging() {
    static INIT: std::sync::Once = std::sync::Once::new();
    INIT.call_once(|| {
        use tracing_subscriber::{prelude::*, EnvFilter};
        use std::fs::{create_dir_all, OpenOptions};
        use std::path::PathBuf;

        let mut paths: Vec<PathBuf> = Vec::new();

        #[cfg(target_os = "windows")]
        {
            if let Ok(local_app_data) = std::env::var("LOCALAPPDATA") {
                paths.push(PathBuf::from(local_app_data).join("Maho").join("Logs").join("audit.json"));
            }
            if let Ok(temp) = std::env::var("TEMP") {
                paths.push(PathBuf::from(temp).join("maho_audit.json"));
            }
        }

        #[cfg(target_os = "macos")]
        {
            if let Ok(home) = std::env::var("HOME") {
                paths.push(PathBuf::from(&home).join("Library").join("Logs").join("Maho").join("audit.json"));
            }
            paths.push(PathBuf::from("/var/log/maho/audit.json"));
            paths.push(PathBuf::from("/tmp/maho_audit.json"));
        }

        #[cfg(all(unix, not(target_os = "macos")))]
        {
            if let Ok(xdg_state) = std::env::var("XDG_STATE_HOME") {
                paths.push(PathBuf::from(xdg_state).join("maho").join("audit.json"));
            } else if let Ok(home) = std::env::var("HOME") {
                paths.push(PathBuf::from(&home).join(".local").join("state").join("maho").join("audit.json"));
            }
            paths.push(PathBuf::from("/var/log/maho/audit.json"));
            paths.push(PathBuf::from("/tmp/maho_audit.json"));
        }

        for path in paths {
            if let Some(parent) = path.parent() {
                let _ = create_dir_all(parent);
            }
            if let Ok(file) = OpenOptions::new()
                .create(true)
                .append(true)
                .open(&path)
            {
                let audit_layer = tracing_subscriber::fmt::layer()
                    .json()
                    .with_writer(file)
                    .with_filter(EnvFilter::new("maho_agent::audit=info"));
                let _ = tracing_subscriber::registry().with(audit_layer).try_init();
                break;
            }
        }
    });
}

pub fn proxy_url() -> String {
    std::env::var("MAHO_PROXY_URL")
        .unwrap_or_else(|_| "https://proxy.maho.co/v1".to_string())
}
