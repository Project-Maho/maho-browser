use async_trait::async_trait;
use maho_types::chat::ChatMessage;

pub mod approval;
pub mod approval_audit;
pub mod browser_action_contract;
pub mod cli_tool_executor;
pub mod deep_research;
pub mod fallback_ladder;
pub mod fallback_ladder_browser;
pub mod maho_reasoning_openai;
#[cfg(not(target_os = "ios"))]
pub mod mcp_client;
#[cfg(target_os = "ios")]
#[path = "mcp_client_ios_stub.rs"]
pub mod mcp_client;
pub mod permission;
pub mod runtime_isolation;
pub mod swiftide_backend;
pub mod tool_registry;
pub mod vault_tools;

pub use approval::{
    ApprovalDecisionMetadata, ApprovalDecisionReason, ApprovalDecisionSource, ApprovalPolicy,
    ToolSensitivity, ToolSensitivityDowngrade, ToolSensitivityResolution,
};
pub use approval_audit::{ApprovalAuditEventKind, ApprovalAuditRecord};
pub use fallback_ladder::{
    FallbackDecision, FallbackDecisionReason, FallbackLadderError, FallbackLadderState,
    FallbackSafetyState, FallbackSelectionRequest, FallbackTier, ScreenshotCuaApproval,
};
pub use fallback_ladder_browser::browser_tool_fallback_request;
pub use deep_research::{
    run_deep_research, AgentResearchStepper, Citation, DeepResearchError, DeepResearchReport,
    ResearchBudget, ResearchEntitlement, ResearchPolicy, ResearchStepper, StopReason,
};
pub use maho_reasoning_openai::MahoReasoningOpenAI;
pub use permission::{
    sanitize_tool_arguments, sanitize_tool_result, PermissionCallback, PermissionDecision,
    PermissionRequest,
};
pub use runtime_isolation::SessionRuntime;
pub use swiftide_backend::{BrowserActionToolsState, SwiftideBackend};
pub use vault_tools::register_vault_agent_tools;

pub use maho_types::tool::ToolDescriptor as ToolDefinition;

pub const DEFAULT_MANAGED_MODEL: &str = "google/gemini-3-flash-lite:free";

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

#[derive(Debug, Clone)]
pub enum AgentStreamEvent {
    Token(String),
    Thinking(String),
    ToolCall {
        id: String,
        name: String,
        args: String,
    },
    ToolResult {
        id: String,
        name: String,
        result: String,
    },
}

pub struct ZeroizedString(secrecy::SecretString);

pub struct SecureKeyBundle {
    pub key: ZeroizedString,
    pub base_url: Option<String>,
    pub model: Option<String>,
}

impl SecureKeyBundle {
    pub fn from_key(key: ZeroizedString) -> Self {
        Self {
            key,
            base_url: None,
            model: None,
        }
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
    async fn save_message(
        &self,
        session_id: &str,
        role: &str,
        content: &str,
    ) -> Result<(), AgentError>;
    async fn get_history(&self, session_id: &str) -> Result<Vec<ChatMessage>, AgentError>;
    async fn list_mcp_servers(
        &self,
        _workspace_id: &str,
    ) -> Result<Vec<maho_types::ai::AiMcpServer>, AgentError> {
        Ok(vec![])
    }
    async fn list_cli_tools(
        &self,
        _workspace_id: &str,
    ) -> Result<Vec<maho_types::ai::AiCliTool>, AgentError> {
        Ok(vec![])
    }
    async fn get_mcp_server(
        &self,
        _id: &str,
    ) -> Result<Option<maho_types::ai::AiMcpServer>, AgentError> {
        Ok(None)
    }
    async fn get_cli_tool(
        &self,
        _id: &str,
    ) -> Result<Option<maho_types::ai::AiCliTool>, AgentError> {
        Ok(None)
    }
    async fn list_workspaces(&self) -> Result<Vec<maho_types::ai::AiWorkspace>, AgentError> {
        Ok(vec![])
    }
    async fn get_profile(
        &self,
        _id: &str,
    ) -> Result<Option<maho_types::ai::AiProfile>, AgentError> {
        Ok(None)
    }
    async fn set_system_prompt_snapshot_if_absent(
        &self,
        _conversation_id: &str,
        _system_prompt: &str,
    ) -> Result<(), AgentError> {
        Ok(())
    }
    async fn get_system_prompt_snapshot(
        &self,
        _conversation_id: &str,
    ) -> Result<Option<String>, AgentError> {
        Ok(None)
    }
    async fn fts_relevant_memories(
        &self,
        _query: &str,
        _top_k: usize,
    ) -> Result<Vec<(String, String, f64)>, AgentError> {
        Ok(Vec::new())
    }
}

pub struct MutexAgentStorage(
    pub std::sync::Arc<std::sync::Mutex<maho_storage::sqlite::SqliteStorage>>,
);

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
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
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
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            lock.get_setting(&key)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))??;
        Ok(val_opt.map(ZeroizedString::new))
    }

    async fn save_message(
        &self,
        session_id: &str,
        role: &str,
        content: &str,
    ) -> Result<(), AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let session_id = session_id.to_string();
        let role = role.to_string();
        let content = content.to_string();
        tokio::task::spawn_blocking(move || {
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
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
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            let turns = lock
                .get_conversation_messages(&session_id)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))?;
            let history = turns
                .into_iter()
                .map(|turn| {
                    let role = turn.role;
                    let content = maho_types::chat::ChatContent::text(turn.content);
                    if role == "user" {
                        ChatMessage::user(content)
                    } else if role == "assistant" {
                        ChatMessage::assistant(content)
                    } else {
                        ChatMessage::system(content)
                    }
                })
                .collect();
            Ok(history)
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))?
    }

    async fn list_mcp_servers(
        &self,
        workspace_id: &str,
    ) -> Result<Vec<maho_types::ai::AiMcpServer>, AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let workspace_id = workspace_id.to_string();
        tokio::task::spawn_blocking(move || {
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            lock.list_mcp_servers(&workspace_id)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))?
    }

    async fn list_cli_tools(
        &self,
        workspace_id: &str,
    ) -> Result<Vec<maho_types::ai::AiCliTool>, AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let workspace_id = workspace_id.to_string();
        tokio::task::spawn_blocking(move || {
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            lock.list_cli_tools(&workspace_id)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))?
    }

    async fn get_mcp_server(
        &self,
        id: &str,
    ) -> Result<Option<maho_types::ai::AiMcpServer>, AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let id = id.to_string();
        tokio::task::spawn_blocking(move || {
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            lock.get_mcp_server(&id)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))?
    }

    async fn get_cli_tool(
        &self,
        id: &str,
    ) -> Result<Option<maho_types::ai::AiCliTool>, AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let id = id.to_string();
        tokio::task::spawn_blocking(move || {
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            lock.get_cli_tool(&id)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))?
    }

    async fn list_workspaces(&self) -> Result<Vec<maho_types::ai::AiWorkspace>, AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        tokio::task::spawn_blocking(move || {
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            lock.list_workspaces()
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))?
    }

    async fn get_profile(&self, id: &str) -> Result<Option<maho_types::ai::AiProfile>, AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let id = id.to_string();
        tokio::task::spawn_blocking(move || {
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            lock.get_ai_profile(&id)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))?
    }

    async fn set_system_prompt_snapshot_if_absent(
        &self,
        conversation_id: &str,
        system_prompt: &str,
    ) -> Result<(), AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let conv_id = conversation_id.to_string();
        let prompt = system_prompt.to_string();
        tokio::task::spawn_blocking(move || {
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            lock.set_system_prompt_snapshot_if_absent(&conv_id, &prompt)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))?
    }

    async fn get_system_prompt_snapshot(
        &self,
        conversation_id: &str,
    ) -> Result<Option<String>, AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let conv_id = conversation_id.to_string();
        tokio::task::spawn_blocking(move || {
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            lock.get_system_prompt_snapshot(&conv_id)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))?
    }

    async fn fts_relevant_memories(
        &self,
        query: &str,
        top_k: usize,
    ) -> Result<Vec<(String, String, f64)>, AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let query = query.to_string();
        tokio::task::spawn_blocking(move || {
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            const COLD_BUDGET_TOKENS: usize = 1000;
            const CHARS_PER_TOKEN: usize = 4;
            let budget_chars = COLD_BUDGET_TOKENS * CHARS_PER_TOKEN;
            let raw = lock
                .search_memories_fts(&query, top_k)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))?;
            let mut out = Vec::new();
            let mut used = 0usize;
            for (id, content, score) in raw {
                let next = used + content.len() + 4;
                if next > budget_chars && !out.is_empty() {
                    break;
                }
                used = next;
                out.push((id, content, score));
            }
            Ok(out)
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
        on_event: Option<std::sync::Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static>>,
    ) -> Result<ChatMessage, AgentError>;
    async fn run_turn_simple(
        &self,
        session_id: &str,
        message: ChatMessage,
        on_token: Option<Box<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>>,
        on_event: Option<std::sync::Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static>>,
    ) -> Result<ChatMessage, AgentError> {
        self.run_turn(session_id, message, on_token, on_event).await
    }
    async fn cancel(&self, session_id: &str) -> Result<(), AgentError>;
    async fn list_tools(&self) -> Result<Vec<ToolDefinition>, AgentError>;
    fn set_permission_callback(
        &self,
        callback: Box<
            dyn Fn(
                    PermissionRequest,
                ) -> std::pin::Pin<
                    Box<dyn std::future::Future<Output = PermissionDecision> + Send>,
                > + Send
                + Sync
                + 'static,
        >,
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

    fn set_browser_action_tools_state(&self, _state: BrowserActionToolsState) {}

    fn set_browser_action_tools_enabled(&self, enabled: bool) {
        self.set_browser_action_tools_state(BrowserActionToolsState::from_enabled(enabled));
    }

    fn set_system_prompt(&self, _prompt: &str) {}
    fn set_allowed_tools(&self, _tools: Option<Vec<String>>) {}
}

pub struct MockAgentRuntime;

#[async_trait]
impl AgentRuntime for MockAgentRuntime {
    async fn run_turn(
        &self,
        _session_id: &str,
        _message: ChatMessage,
        _on_token: Option<Box<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>>,
        _on_event: Option<std::sync::Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static>>,
    ) -> Result<ChatMessage, AgentError> {
        Ok(ChatMessage::assistant(maho_types::chat::ChatContent::text(
            "Mock response",
        )))
    }

    async fn cancel(&self, _session_id: &str) -> Result<(), AgentError> {
        Ok(())
    }

    async fn list_tools(&self) -> Result<Vec<ToolDefinition>, AgentError> {
        Ok(tool_registry::get_platform_tools())
    }

    fn set_permission_callback(
        &self,
        _callback: Box<
            dyn Fn(
                    PermissionRequest,
                ) -> std::pin::Pin<
                    Box<dyn std::future::Future<Output = PermissionDecision> + Send>,
                > + Send
                + Sync
                + 'static,
        >,
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
        use std::fs::{create_dir_all, OpenOptions};
        use std::path::PathBuf;
        use tracing_subscriber::{prelude::*, EnvFilter};

        let mut paths: Vec<PathBuf> = Vec::new();

        #[cfg(target_os = "windows")]
        {
            if let Ok(local_app_data) = std::env::var("LOCALAPPDATA") {
                paths.push(
                    PathBuf::from(local_app_data)
                        .join("Maho")
                        .join("Logs")
                        .join("audit.json"),
                );
            }
            if let Ok(temp) = std::env::var("TEMP") {
                paths.push(PathBuf::from(temp).join("maho_audit.json"));
            }
        }

        #[cfg(target_os = "macos")]
        {
            if let Ok(home) = std::env::var("HOME") {
                paths.push(
                    PathBuf::from(&home)
                        .join("Library")
                        .join("Logs")
                        .join("Maho")
                        .join("audit.json"),
                );
            }
            paths.push(PathBuf::from("/var/log/maho/audit.json"));
            paths.push(PathBuf::from("/tmp/maho_audit.json"));
        }

        #[cfg(all(unix, not(target_os = "macos")))]
        {
            if let Ok(xdg_state) = std::env::var("XDG_STATE_HOME") {
                paths.push(PathBuf::from(xdg_state).join("maho").join("audit.json"));
            } else if let Ok(home) = std::env::var("HOME") {
                paths.push(
                    PathBuf::from(&home)
                        .join(".local")
                        .join("state")
                        .join("maho")
                        .join("audit.json"),
                );
            }
            paths.push(PathBuf::from("/var/log/maho/audit.json"));
            paths.push(PathBuf::from("/tmp/maho_audit.json"));
        }

        for path in paths {
            if let Some(parent) = path.parent() {
                let _ = create_dir_all(parent);
            }
            if let Ok(file) = OpenOptions::new().create(true).append(true).open(&path) {
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
    std::env::var("MAHO_PROXY_URL").unwrap_or_else(|_| "https://proxy.maho.co/v1".to_string())
}
