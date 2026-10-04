use async_trait::async_trait;
use maho_types::chat::{ChatContent, ChatMessage};

pub mod approval;
pub mod approval_audit;
pub mod browser_action_contract;
pub mod channels;
pub mod cli_tool_executor;
pub mod composite;

pub mod context_gatherer;
pub mod deep_research;
pub mod direct_api;
pub mod event_wait;
pub mod fallback_ladder;
pub mod fallback_ladder_browser;
pub mod image_search;
pub mod interaction;
pub mod introspection;

#[cfg(not(target_os = "ios"))]
pub mod mcp_client;
#[cfg(target_os = "ios")]
#[path = "mcp_client_ios_stub.rs"]
pub mod mcp_client;
pub mod model_routing;
pub mod omo;
pub mod page_adapters;
pub mod permission;
pub mod retry_policy;
pub mod run_journal;
pub mod runtime_isolation;
pub mod skill_creator;
pub mod subagents;
pub mod suggestions;
/// State of browser action tools exposed to the agent.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum BrowserActionToolsState {
    #[default]
    Disabled,
    Enabled,
    Mocked,
}

impl BrowserActionToolsState {
    /// Convert a boolean flag to the corresponding tool state.
    pub fn from_enabled(enabled: bool) -> Self {
        if enabled {
            Self::Enabled
        } else {
            Self::Disabled
        }
    }
}
pub mod system_prompt;
pub mod tool_registry;
pub mod trace_recorder;
pub mod turn_control;
pub mod vault_tools;
pub mod visual_cua;
pub mod web_search;

#[cfg(not(any(target_os = "ios", target_os = "android")))]
pub mod web_fetch;

pub use approval::{
    ApprovalDecisionMetadata, ApprovalDecisionReason, ApprovalDecisionSource, ApprovalPolicy,
    ToolSensitivity, ToolSensitivityDowngrade, ToolSensitivityResolution,
};
pub use approval_audit::{ApprovalAuditEventKind, ApprovalAuditRecord};
pub use context_gatherer::{
    AccountContextEntry, AccountContextSlot, ContextBlock, ContextBlockProvenance,
    ContextBlockSource, ContextGatherRequest, ContextGatherResponse,
};
pub use deep_research::{
    run_deep_research, AgentResearchStepper, Citation, DeepResearchError, DeepResearchReport,
    ResearchBudget, ResearchEntitlement, ResearchPolicy, ResearchStepper, StopReason,
};
pub use fallback_ladder::{
    FallbackDecision, FallbackDecisionReason, FallbackLadderError, FallbackLadderState,
    FallbackSafetyState, FallbackSelectionRequest, FallbackTier, ScreenshotCuaApproval,
};
pub use fallback_ladder_browser::browser_tool_fallback_request;
pub use image_search::{
    FreeImageSearchBackend, ImageSearchClient, ImageSearchEntitlementRouter, ImageSearchError,
    ImageSearchProvenance, ImageSearchQuery, ImageSearchResponse, ImageSearchResult,
};
pub use introspection::{
    collect_introspection, execute_introspection_tool, ActiveRunSummary, ModelRoutingSummary,
    SessionIntrospectionReport,
};

pub use permission::{
    authorize_action_consequence, authorize_final_confirm, authorize_mail_tool,
    classify_action_consequence, classify_mail_tool, sanitize_tool_arguments, sanitize_tool_result,
    ActionConsequence, ConsequenceApprovalAction, FinalConfirmDecision, MailAuthorizationAction,
    MailAuthorizationState, MailToolClass, PermissionCallback, PermissionDecision,
    PermissionRequest, MAIL_READ_TOOLS, MAIL_WRITE_ACCOUNT_TOOLS,
};
pub use runtime_isolation::SessionRuntime;
pub use skill_creator::{
    activate_skill_proposal, propose_skill_from_journal, request_skill_approval_interaction,
    update_skill, SkillActivationStatus, SkillCreatorError, SkillProposal, SkillProposalOutcome,
};
pub use subagents::ActiveSubagentSummary;
pub use subagents::{
    execute_subagent_wait_tool, merge_subagent_summary_to_parent, SubagentChildRun, SubagentError,
    SubagentResult, SubagentSpec, SubagentStatus, SubagentSupervisor, MAX_ACTIVE_SUBAGENTS,
    MAX_SUBAGENT_BUDGET_TURNS,
};
pub use suggestions::{
    generate_suggestions_from_journal, install_routine, suggest_routine, suggest_routine_proposal,
    validate_routine_manifest, RoutineManifest, RoutineValidationError, Suggestion,
    SuggestionSettings, MAX_PASSIVE_SUGGESTIONS,
};

pub use visual_cua::*;
pub use web_search::{
    FreeWebSearchBackend, PremiumWebSearchBackend, SearchEntitlement, SearchTier, WebSearchClient,
    WebSearchEntitlementRouter, WebSearchError, WebSearchProvenance, WebSearchQuery,
    WebSearchResponse, WebSearchResult, DEFAULT_MAX_RESULTS, MAX_RESULTS_HARD_CAP,
};

pub use maho_types::tool::{
    BrowserToolBridgeError, BrowserToolDescriptor, BrowserToolExecution,
    BrowserToolExecutionReceipt, BrowserToolPolicy, ToolDescriptor as ToolDefinition,
};

pub const DEFAULT_MANAGED_MODEL: &str = "google/gemini-3-flash-lite:free";

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum CredentialError {
    #[error("provider_not_configured")]
    ProviderNotConfigured,
    #[error("credential_unusable")]
    CredentialUnusable,
    #[error("secure_store_unavailable")]
    SecureStoreUnavailable,
    #[error("credential_decrypt_failed")]
    CredentialDecryptFailed,
    #[error("managed_auth_unavailable")]
    ManagedAuthUnavailable,
    #[error("unsupported_provider")]
    UnsupportedProvider,
}

impl CredentialError {
    pub const fn code(self) -> &'static str {
        match self {
            Self::ProviderNotConfigured => "provider_not_configured",
            Self::CredentialUnusable => "credential_unusable",
            Self::SecureStoreUnavailable => "secure_store_unavailable",
            Self::CredentialDecryptFailed => "credential_decrypt_failed",
            Self::ManagedAuthUnavailable => "managed_auth_unavailable",
            Self::UnsupportedProvider => "unsupported_provider",
        }
    }
}

#[derive(Debug, thiserror::Error)]
pub enum AgentError {
    #[error("Session not found: {0}")]
    SessionNotFound(String),
    #[error("Session is busy: {0}")]
    SessionBusy(String),
    #[error("Execution error: {0}")]
    ExecutionError(String),
    #[error("Cancelled")]
    Cancelled,
    #[error("Permission denied")]
    PermissionDenied,
    #[error("{0}")]
    Credential(#[from] CredentialError),
}

#[derive(Debug, Clone, PartialEq, Eq)]
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
        succeeded: bool,
    },
    ArtifactCreated {
        artifact: maho_types::artifact::ArtifactInfo,
    },
    Status {
        elapsed_secs: u64,
        message: String,
    },
    ProofOrReason {
        proof_locator: Option<String>,
        reason: Option<String>,
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

#[async_trait]
pub trait BrowserToolBridge: Send + Sync {
    /// Discover browser-owned capabilities. The default is deliberately
    /// unavailable so embedders that have not implemented discovery expose no
    /// browser tools rather than falling back to guessed static capabilities.
    async fn list_tool_descriptors(
        &self,
    ) -> Result<Vec<BrowserToolDescriptor>, BrowserToolBridgeError> {
        Err(BrowserToolBridgeError::new(
            "discovery_unavailable",
            "browser tool discovery is not implemented by this embedder",
            false,
        ))
    }

    /// Execute a canonical browser capability and return its typed receipt.
    async fn execute_tool(
        &self,
        _capability_id: &str,
        _args_json: &str,
    ) -> Result<BrowserToolExecution, BrowserToolBridgeError> {
        Err(BrowserToolBridgeError::new(
            "execution_unavailable",
            "browser tool execution is not implemented by this embedder",
            false,
        ))
    }
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

pub struct RetrievedMemory {
    pub fact: String,
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
    /// Persist an artifact's metadata into the session artifact index.
    /// Default is a no-op so storage backends without an artifact index
    /// (CLI, mocks) keep compiling and streaming unchanged.
    async fn save_artifact(
        &self,
        _artifact: &maho_types::artifact::ArtifactInfo,
    ) -> Result<(), AgentError> {
        Ok(())
    }
    /// List a session's persisted artifacts, oldest first. Default empty.
    async fn list_artifacts(
        &self,
        _session_id: &str,
    ) -> Result<Vec<maho_types::artifact::ArtifactInfo>, AgentError> {
        Ok(Vec::new())
    }
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
    async fn get_memory_block(&self, _label: &str) -> Result<Option<String>, AgentError> {
        Ok(None)
    }
    async fn retrieve_memories(
        &self,
        _query: &str,
        _top_k: usize,
    ) -> Result<Vec<RetrievedMemory>, AgentError> {
        Ok(Vec::new())
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

    async fn save_artifact(
        &self,
        artifact: &maho_types::artifact::ArtifactInfo,
    ) -> Result<(), AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let artifact = artifact.clone();
        tokio::task::spawn_blocking(move || {
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            lock.insert_artifact(&artifact)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
        })
        .await
        .map_err(|e| AgentError::ExecutionError(e.to_string()))?
    }

    async fn list_artifacts(
        &self,
        session_id: &str,
    ) -> Result<Vec<maho_types::artifact::ArtifactInfo>, AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let session_id = session_id.to_string();
        tokio::task::spawn_blocking(move || {
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            lock.list_artifacts(&session_id)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
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

    async fn get_memory_block(&self, label: &str) -> Result<Option<String>, AgentError> {
        let storage = std::sync::Arc::clone(&self.0);
        let label = label.to_string();
        tokio::task::spawn_blocking(move || {
            let lock = storage
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            lock.get_memory_block(&label)
                .map_err(|e| AgentError::ExecutionError(e.to_string()))
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
    /// One-shot prompt runner defaulting to a fresh isolated session turn.
    async fn run_prompt(&self, prompt: &str) -> Result<String, AgentError> {
        let session_id = format!("turn-{}", uuid::Uuid::new_v4());
        let msg = ChatMessage::user(ChatContent::text(prompt.to_string()));
        let resp = self.run_turn(&session_id, msg, None, None).await?;
        match resp.content {
            ChatContent::Text(text) => Ok(text),
            _ => Ok(String::new()),
        }
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

    // Session-state accessors reached through `session.runtime` by maho-ffi.
    // Defaults are inert so a backend that does not own this state still
    // compiles; SwiftideBackend overrides every one of them.
    fn set_approval_policy(&self, _policy: String) {}

    fn set_runtime_tier(&self, _tier: Option<String>) {}

    fn runtime_tier(&self) -> Option<String> {
        None
    }

    fn set_fs_whitelist_roots(&self, _roots: Vec<String>) {}

    fn fs_whitelist_roots(&self) -> Vec<String> {
        Vec::new()
    }

    fn set_preferred_provider(&self, _provider: Option<String>) {}

    fn set_artifact_root(&self, _artifact_root: Option<std::path::PathBuf>) {}

    async fn list_artifacts(
        &self,
        _session_id: &str,
    ) -> Result<Vec<maho_types::artifact::ArtifactInfo>, AgentError> {
        Ok(Vec::new())
    }

    async fn artifact_storage_rel_path(
        &self,
        _session_id: &str,
        _artifact_id: &str,
    ) -> Result<Option<String>, AgentError> {
        Ok(None)
    }

    fn resolve_interaction(
        &self,
        req_id: &str,
        _answer: crate::interaction::InteractionAnswer,
    ) -> Result<(), crate::interaction::InteractionError> {
        Err(crate::interaction::InteractionError::NotFound(
            crate::interaction::InteractionRequestId::new(req_id),
        ))
    }

    fn timeout_interaction(
        &self,
        req_id: &str,
    ) -> Result<(), crate::interaction::InteractionError> {
        Err(crate::interaction::InteractionError::NotFound(
            crate::interaction::InteractionRequestId::new(req_id),
        ))
    }

    fn cancel_interaction(&self, req_id: &str) -> Result<(), crate::interaction::InteractionError> {
        Err(crate::interaction::InteractionError::NotFound(
            crate::interaction::InteractionRequestId::new(req_id),
        ))
    }

    fn interaction_broker(
        &self,
    ) -> std::sync::Arc<std::sync::Mutex<crate::interaction::InteractionBroker>> {
        static DUMMY: std::sync::OnceLock<
            std::sync::Arc<std::sync::Mutex<crate::interaction::InteractionBroker>>,
        > = std::sync::OnceLock::new();
        DUMMY
            .get_or_init(|| {
                std::sync::Arc::new(std::sync::Mutex::new(
                    crate::interaction::InteractionBroker::new(),
                ))
            })
            .clone()
    }

    fn run_journal(&self) -> std::sync::Arc<std::sync::Mutex<crate::run_journal::RunJournal>>;

    /// Points the runtime's journal at this session's on-disk file.
    ///
    /// Object-safe counterpart of the backend-specific helpers. Backends that treat the
    /// agent process as the source of session state keep the default no-op.
    fn attach_session_journal(&self, _session_id: &str, _storage_path: &std::path::Path) {}

    fn set_active_space_id(&self, _space_id: Option<String>) {}

    fn latest_run_id(&self, _session_id: &str) -> Option<crate::run_journal::AgentRunId> {
        None
    }

    /// Bind the composite (batch) execution gate (plan row 11). Default is
    /// unset: the composite gate stays fail-closed disabled.
    fn set_composite_config(&self, _config: crate::composite::CompositeExecutionConfig) {}

    fn composite_config(&self) -> crate::composite::CompositeExecutionConfig {
        crate::composite::CompositeExecutionConfig::default()
    }

    fn set_mail_authorization_state(&self, _state: MailAuthorizationState) {}

    fn set_browser_action_tools_enabled(&self, enabled: bool) {
        self.set_browser_action_tools_state(BrowserActionToolsState::from_enabled(enabled));
    }

    fn set_system_prompt(&self, _prompt: &str) {}

    fn system_prompt(&self) -> Option<String> {
        None
    }

    fn artifact_root(&self) -> Option<std::path::PathBuf> {
        None
    }
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

    fn run_journal(&self) -> std::sync::Arc<std::sync::Mutex<crate::run_journal::RunJournal>> {
        std::sync::Arc::new(std::sync::Mutex::new(
            crate::run_journal::RunJournal::default(),
        ))
    }
}

/// The runtime for platforms the omo agent cannot run on yet (Windows: omo's RPC runtime
/// is Unix-socket only). Every turn fails with an explicit error so the panel shows why,
/// instead of answering with fabricated text.
pub struct UnavailableAgentRuntime {
    reason: &'static str,
}

impl UnavailableAgentRuntime {
    pub const fn new(reason: &'static str) -> Self {
        Self { reason }
    }
}

#[async_trait]
impl AgentRuntime for UnavailableAgentRuntime {
    async fn run_turn(
        &self,
        _session_id: &str,
        _message: ChatMessage,
        _on_token: Option<Box<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>>,
        _on_event: Option<std::sync::Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static>>,
    ) -> Result<ChatMessage, AgentError> {
        Err(AgentError::ExecutionError(self.reason.to_string()))
    }

    async fn cancel(&self, _session_id: &str) -> Result<(), AgentError> {
        Ok(())
    }

    async fn list_tools(&self) -> Result<Vec<ToolDefinition>, AgentError> {
        Ok(Vec::new())
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

    fn run_journal(&self) -> std::sync::Arc<std::sync::Mutex<crate::run_journal::RunJournal>> {
        std::sync::Arc::new(std::sync::Mutex::new(
            crate::run_journal::RunJournal::default(),
        ))
    }
}

#[cfg(test)]
mod unavailable_runtime_tests {
    use super::*;

    #[tokio::test]
    async fn unavailable_runtime_fails_every_turn_with_its_reason() {
        let runtime = UnavailableAgentRuntime::new("agent runtime unavailable on this platform");
        let error = runtime
            .run_turn("s", ChatMessage::user(maho_types::chat::ChatContent::text("hi")), None, None)
            .await
            .unwrap_err();
        assert_eq!(
            error.to_string(),
            "Execution error: agent runtime unavailable on this platform"
        );
        assert!(runtime.list_tools().await.unwrap().is_empty());
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
