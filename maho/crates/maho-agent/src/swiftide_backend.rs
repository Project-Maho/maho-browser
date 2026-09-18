use async_trait::async_trait;

use maho_types::chat::{ChatContent, ChatMessage as MahoChatMessage};

use std::borrow::Cow;

use std::sync::{Arc, Mutex, MutexGuard};

use swiftide_core::{
    chat_completion::{
        ChatCompletion, ChatCompletionRequest, ChatMessage as SwiftideChatMessage, ToolCall,
        ToolOutput, ToolSpec,
    },
    AgentContext, Tool,
};

use crate::approval::{
    emit_audit_record, ApprovalAuditEventKind, ApprovalAuditRecord, ApprovalDecisionMetadata,
    ApprovalDecisionReason, ApprovalDecisionSource, ApprovalPolicy, ToolSensitivity,
    ToolSensitivityDowngrade, ToolSensitivityResolution,
};

#[cfg(test)]
use crate::browser_action_contract::canonical_browser_action_contracts;
use crate::browser_action_contract::{
    BrowserActionContract, BrowserToolKind, BrowserToolSensitivity,
};

use crate::fallback_ladder::{FallbackLadderError, FallbackLadderState};

use crate::fallback_ladder_browser::browser_tool_fallback_request;

use crate::{
    AgentError, AgentRuntime, AgentStorage, AgentStreamEvent, CredentialError, PermissionCallback,
    PermissionDecision, PermissionRequest, ToolDefinition,
};

use maho_types::tool::{ToolPermission, ToolProvenance};

const FS_READ_MAX_BYTES: u64 = 1024 * 1024;
const FS_WRITE_MAX_BYTES: u64 = 50 * 1024 * 1024;

const OPENAI_DEFAULT_API_BASE: &str = "https://api.openai.com/v1";

const PERMISSION_DECISION_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(30);

/// Internal swiftide control-flow tools — never surfaced to the UI as tool_call/tool_result.

/// Force one final-answer synthesis turn when the agent loop ends without a
/// usable textual answer.
///
/// A weak model frequently calls `stop` with no value, runs out of turns right
/// after a tool call, or otherwise leaves the last message as a raw tool output.
/// Surfacing that raw output (a `browser_navigate` `{"navigated":true}`, a
/// find-in-page `{"active_match_index":..}`, or a raw accessibility tree) as the
/// user-visible answer is never correct. Re-prompting the model once, with no
/// tools attached so it must answer in prose, turns the context it already
/// gathered into a real answer. Returns `None` on any error or empty reply so
/// the caller can fall back to the previous behavior.
async fn synthesize_final_answer(
    llm: &dyn ChatCompletion,
    history: &[SwiftideChatMessage],
) -> Option<String> {
    if history.is_empty() {
        return None;
    }
    let mut messages: Vec<SwiftideChatMessage> = history.to_vec();
    messages.push(SwiftideChatMessage::new_user(
        "You have finished using tools. Using only the information already gathered \
         above, write the final answer to the user's original request now. Give the \
         specific requested details directly in prose. Do not call any tools, do not \
         describe what you will do next, and do not output raw JSON or accessibility-tree \
         data. If the requested information could not be obtained, state plainly what was \
         missing.",
    ));
    let request = ChatCompletionRequest::from(messages);
    match llm.complete(&request).await {
        Ok(response) => response.message.filter(|s| !s.trim().is_empty()),
        Err(_) => None,
    }
}

/// Heuristic: the model announced a next action instead of doing it or answering.
///
/// Weak models frequently end a turn with plain narration ("I will navigate to
/// ...", "I will call `stop`...") and no tool call, which makes the agent loop
/// stop with `StopReason::NoNewMessages`, treating that narration as the final
/// answer. It never is. Detecting that shape lets the caller force a synthesis
/// turn instead. Deliberately narrow — only clear future-action openers — so it
/// never rewrites a genuine answer, which does not start this way.
fn looks_like_unfinished_intent(text: &str) -> bool {
    let lower = text.trim().to_lowercase();
    const INTENT_OPENERS: &[&str] = &[
        "i will ",
        "i'll ",
        "let me ",
        "i am going to ",
        "i'm going to ",
        "i am now going to ",
        "i will now ",
        "now i will ",
        "next, i",
    ];
    INTENT_OPENERS.iter().any(|p| lower.starts_with(p))
}

const INTERNAL_CONTROL_TOOLS: &[&str] = &["stop", "task_failed"];

fn is_internal_control_tool(name: &str) -> bool {
    INTERNAL_CONTROL_TOOLS.contains(&name)
}

trait MutexExt<T> {
    fn lock_unpoison(&self) -> MutexGuard<'_, T>;
}

impl<T> MutexExt<T> for Mutex<T> {
    fn lock_unpoison(&self) -> MutexGuard<'_, T> {
        self.lock().unwrap_or_else(|e| {
            tracing::error!(
                target = "maho_agent::mutex",
                mutex_type = std::any::type_name::<T>(),
                "Mutex poisoned by prior panic; recovering with possibly inconsistent state"
            );

            e.into_inner()
        })
    }
}

fn should_attempt_mcp_connect(server: &maho_types::ai::AiMcpServer) -> bool {
    server.trusted
        || crate::mcp_client::resolve_uds_permission(
            server.transport,
            server.socket_path.as_deref(),
        ) == ToolPermission::AutoApprove
}

/// Decide whether a registered MCP tool must be gated by the permission callback.
/// Browser-authoritative discovery already resolved `discovered_sensitive`,
/// including fail-closed handling for missing or malformed policy metadata.
/// Explicit server trust (`server.trusted` + `trusted_tools`) remains an
/// independent, user-opted-in path.
fn mcp_tool_is_sensitive(
    discovered_sensitive: bool,

    raw_name: &str,

    tool_name: &str,

    server_trusted: bool,

    trusted_tools: Option<&Vec<String>>,
) -> bool {
    let explicitly_trusted = server_trusted
        && match trusted_tools {
            Option::None => true,

            Some(list) => list.iter().any(|n| n == raw_name || n == tool_name),
        };

    discovered_sensitive && !explicitly_trusted
}

#[derive(Clone)]

struct MahoOpenAIConfig {
    // SAFETY: contains BYOK plaintext, exclude from crash dumps
    api_key: Arc<secrecy::SecretString>,

    api_base: String,
}

impl Default for MahoOpenAIConfig {
    fn default() -> Self {
        // Required by GenericOpenAIBuilder bound only; never reaches HTTP at runtime

        // because callers override the config before `.client(...)`. Empty "Bearer " on the wire = bug.

        Self {
            api_key: Arc::new(secrecy::SecretString::from(String::new())),

            api_base: OPENAI_DEFAULT_API_BASE.to_string(),
        }
    }
}

impl std::fmt::Debug for MahoOpenAIConfig {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("MahoOpenAIConfig")
            .field("api_key", &"[REDACTED]")
            .field("api_base", &self.api_base)
            .finish()
    }
}

impl async_openai::config::Config for MahoOpenAIConfig {
    fn headers(&self) -> reqwest::header::HeaderMap {
        use secrecy::ExposeSecret;

        let mut map = reqwest::header::HeaderMap::new();

        let mut bearer = zeroize::Zeroizing::new(String::with_capacity(
            "Bearer ".len() + self.api_key.expose_secret().len(),
        ));

        bearer.push_str("Bearer ");

        bearer.push_str(self.api_key.expose_secret());

        match reqwest::header::HeaderValue::from_str(&bearer) {
            Ok(v) => {
                map.insert(reqwest::header::AUTHORIZATION, v);
            }

            Err(_) => {
                tracing::error!(
                    target = "maho_agent::config",
                    "BYOK API key produced invalid Authorization header bytes; \

                     request will be sent without auth and will likely receive 401"
                );
            }
        }

        map
    }

    fn url(&self, path: &str) -> String {
        format!("{}{}", self.api_base, path)
    }

    fn query(&self) -> Vec<(&str, &str)> {
        vec![]
    }

    fn api_base(&self) -> &str {
        &self.api_base
    }

    fn api_key(&self) -> &secrecy::SecretString {
        &self.api_key
    }
}

#[derive(Clone)]

struct PermissionGatedTool {
    inner_tool: Arc<dyn Tool>,

    sensitivity: ToolSensitivity,

    sensitivity_downgrade: Option<ToolSensitivityDowngrade>,

    permission_callback: Arc<Mutex<Option<PermissionCallback>>>,

    denied_flag: Arc<Mutex<bool>>,

    session_id: String,

    approval_policy: Arc<Mutex<ApprovalPolicy>>,

    // Wave 1D (D7): runtime-tier gate inputs, shared with the backend so the
    // CLI can bind them per session. `runtime_tier: None` leaves the gate
    // inert (browser surfaces stay broker-gated, plan R-F8).
    runtime_tier: Arc<Mutex<Option<String>>>,

    fs_whitelist_roots: Arc<Mutex<Vec<String>>>,

    workspace_root: Arc<std::path::PathBuf>,

    // Snapshot input for the tier gate's fs_path resolution (see
    // permission::kernel_fs_request). Shared Arc with the backend so invoke
    // reads the live value per call — and so building a gated tool never
    // re-locks a mutex the caller may already hold (the FsWriteTool
    // construction sites lock artifact_root within the same statement).
    artifact_root: Arc<Mutex<Option<std::path::PathBuf>>>,
}

fn permission_error(reason: &str) -> swiftide_core::chat_completion::errors::ToolError {
    swiftide_core::chat_completion::errors::ToolError::unknown(anyhow::anyhow!(
        "Permission Denied: {reason}"
    ))
}

fn audit_decision(
    kind: ApprovalAuditEventKind,

    session_id: &str,

    tool_name: &str,

    sensitivity: ToolSensitivity,

    metadata: ApprovalDecisionMetadata,
) {
    emit_audit_record(&ApprovalAuditRecord::decision(
        kind,
        session_id,
        tool_name,
        sensitivity,
        metadata,
    ));
}

fn audit_execution_result(
    session_id: &str,

    tool_name: &str,

    sensitivity: ToolSensitivity,

    policy: ApprovalPolicy,

    result: &Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError>,
) {
    let raw_result = match result {
        Ok(output) => format!("{output}"),

        Err(error) => error.to_string(),
    };

    let sanitized_result = crate::permission::sanitize_tool_result(&raw_result);

    emit_audit_record(&ApprovalAuditRecord::execution_result(
        session_id,
        tool_name,
        sensitivity,
        policy,
        &sanitized_result,
    ));
}

fn audit_fallback_decision(
    session_id: &str,

    tool_name: &str,

    sensitivity: ToolSensitivity,

    policy: ApprovalPolicy,

    decision: &crate::fallback_ladder::FallbackDecision,
) {
    emit_audit_record(&ApprovalAuditRecord::fallback_decision(
        session_id,
        tool_name,
        sensitivity,
        policy,
        decision,
    ));
}

fn fallback_error(
    error: &FallbackLadderError,
) -> swiftide_core::chat_completion::errors::ToolError {
    let reason = error.decision().reason.as_str();

    swiftide_core::chat_completion::errors::ToolError::unknown(anyhow::anyhow!(
        "Browser fallback ladder denied dispatch: {reason}"
    ))
}

struct PermissionCancelAuditGuard {
    session_id: String,

    tool_name: String,

    sensitivity: ToolSensitivity,

    policy: ApprovalPolicy,

    armed: bool,
}

impl PermissionCancelAuditGuard {
    fn new(
        session_id: &str,

        tool_name: &str,

        sensitivity: ToolSensitivity,

        policy: ApprovalPolicy,
    ) -> Self {
        Self {
            session_id: session_id.to_string(),

            tool_name: tool_name.to_string(),

            sensitivity,

            policy,

            armed: true,
        }
    }

    fn complete(&mut self) {
        self.armed = false;
    }
}

impl Drop for PermissionCancelAuditGuard {
    fn drop(&mut self) {
        if self.armed {
            emit_audit_record(&ApprovalAuditRecord::cancellation(
                &self.session_id,
                &self.tool_name,
                self.sensitivity,
                self.policy,
            ));
        }
    }
}

#[async_trait]

impl Tool for PermissionGatedTool {
    async fn invoke(
        &self,

        agent_context: &dyn AgentContext,

        tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        let tool_name = self.name().to_string();

        let policy = *self.approval_policy.lock_unpoison();

        let sanitized_arguments =
            crate::permission::sanitize_tool_arguments(tool_call.args().unwrap_or("{}"));

        if let Some(downgrade) = self.sensitivity_downgrade {
            emit_audit_record(&ApprovalAuditRecord::sensitivity_downgrade(
                &self.session_id,
                &tool_name,
                downgrade.from,
                downgrade.to,
                policy,
            ));
        }

        emit_audit_record(&ApprovalAuditRecord::permission_request(
            &self.session_id,
            &tool_name,
            self.sensitivity,
            policy,
            &sanitized_arguments,
        ));

        let mut cancel_guard =
            PermissionCancelAuditGuard::new(&self.session_id, &tool_name, self.sensitivity, policy);

        if *self.denied_flag.lock_unpoison() {
            let metadata = ApprovalDecisionMetadata::new(
                PermissionDecision::Deny,
                policy,
                ApprovalDecisionSource::PriorDeny,
                ApprovalDecisionReason::PriorDeny,
            );

            audit_decision(
                ApprovalAuditEventKind::PermissionDefaultFailClosed,
                &self.session_id,
                &tool_name,
                self.sensitivity,
                metadata,
            );

            cancel_guard.complete();

            return Err(permission_error("prior tool denial in this turn"));
        }

        // Wave 1D (D7): runtime-tier gate for kernel tool calls. Inert unless
        // the session carries a tier — CLI task sessions always do; tierless
        // sessions (browser surfaces) keep the capability broker as the sole
        // tier authority (plan R-F8). Deny refuses with the machine-consumed
        // tier reason code; RequireApproval forces the call onto the callback
        // path (the guard ask-gate outside the whitelist) even when the
        // approval policy would auto-allow; Allow falls through untouched.
        let tier_forces_prompt = {
            let tier_opt = self.runtime_tier.lock_unpoison().clone();
            match tier_opt {
                Some(ref raw_tier) => {
                    let roots = self.fs_whitelist_roots.lock_unpoison().clone();
                    let root_refs: Vec<&str> = roots.iter().map(String::as_str).collect();
                    // Read live per call: no stale snapshot, and the lock is
                    // never held across an await or a nested build call.
                    let artifact_root = self.artifact_root.lock_unpoison().clone();
                    let (is_write, fs_path) = crate::permission::kernel_fs_request(
                        &tool_name,
                        tool_call.args().unwrap_or("{}"),
                        &self.workspace_root,
                        artifact_root.as_deref(),
                    );
                    match crate::permission::authorize_kernel_tool_request(
                        raw_tier, &tool_name, is_write, &fs_path, &root_refs,
                    ) {
                        crate::permission::KernelTierDecision::Deny(reason) => {
                            let metadata = ApprovalDecisionMetadata::new(
                                PermissionDecision::Deny,
                                policy,
                                ApprovalDecisionSource::Runtime,
                                ApprovalDecisionReason::TierDeny,
                            );
                            audit_decision(
                                ApprovalAuditEventKind::PermissionDeny,
                                &self.session_id,
                                &tool_name,
                                self.sensitivity,
                                metadata,
                            );
                            *self.denied_flag.lock_unpoison() = true;
                            cancel_guard.complete();
                            // Arguments are never echoed on a tier denial.
                            return Err(permission_error(&format!(
                                "permission tier denied the tool ({reason})"
                            )));
                        }
                        crate::permission::KernelTierDecision::RequireApproval => true,
                        crate::permission::KernelTierDecision::Allow => false,
                    }
                }
                None => false,
            }
        };

        let mail_requires_typed_approval = crate::classify_mail_tool(&tool_name).is_some()
            && self.sensitivity == ToolSensitivity::Sensitive;
        let action_approval = crate::authorize_action_consequence(
            &tool_name,
            tool_call.args().unwrap_or("{}"),
            policy,
        );
        if action_approval == crate::ConsequenceApprovalAction::Deny {
            let metadata = ApprovalDecisionMetadata::new(
                PermissionDecision::Deny,
                policy,
                ApprovalDecisionSource::Policy,
                ApprovalDecisionReason::PolicyDenySensitive,
            );
            audit_decision(
                ApprovalAuditEventKind::PermissionDeny,
                &self.session_id,
                &tool_name,
                self.sensitivity,
                metadata,
            );
            *self.denied_flag.lock_unpoison() = true;
            cancel_guard.complete();
            return Err(permission_error("approval policy denied the tool"));
        }
        let consequence_requires_approval =
            action_approval == crate::ConsequenceApprovalAction::RequireApproval;
        if !tier_forces_prompt
            && !mail_requires_typed_approval
            && action_approval == crate::ConsequenceApprovalAction::Allow
        {
            let metadata = ApprovalDecisionMetadata::new(
                PermissionDecision::Allow,
                policy,
                ApprovalDecisionSource::Policy,
                ApprovalDecisionReason::PolicyReadOnlyAutoAllow,
            );
            audit_decision(
                ApprovalAuditEventKind::PermissionAllow,
                &self.session_id,
                &tool_name,
                self.sensitivity,
                metadata,
            );
            let result = self.inner_tool.invoke(agent_context, tool_call).await;
            audit_execution_result(
                &self.session_id,
                &tool_name,
                self.sensitivity,
                policy,
                &result,
            );
            cancel_guard.complete();
            return result;
        }

        if !tier_forces_prompt
            && !mail_requires_typed_approval
            && action_approval != crate::ConsequenceApprovalAction::Allow
            && !consequence_requires_approval
        {
            if let Some(metadata) = policy.immediate_decision(&tool_name, self.sensitivity) {
                match metadata.decision {
                    PermissionDecision::Allow => {
                        audit_decision(
                            ApprovalAuditEventKind::PermissionAllow,
                            &self.session_id,
                            &tool_name,
                            self.sensitivity,
                            metadata,
                        );

                        let result = self.inner_tool.invoke(agent_context, tool_call).await;

                        audit_execution_result(
                            &self.session_id,
                            &tool_name,
                            self.sensitivity,
                            policy,
                            &result,
                        );

                        cancel_guard.complete();

                        return result;
                    }

                    PermissionDecision::Deny => {
                        audit_decision(
                            ApprovalAuditEventKind::PermissionDeny,
                            &self.session_id,
                            &tool_name,
                            self.sensitivity,
                            metadata,
                        );

                        *self.denied_flag.lock_unpoison() = true;

                        cancel_guard.complete();

                        return Err(permission_error("approval policy denied the tool"));
                    }
                }
            }
        }

        let decision_fut = {
            let callback_opt = self.permission_callback.lock_unpoison();

            if let Some(ref callback) = *callback_opt {
                let request = PermissionRequest::new(
                    tool_name.clone(),
                    tool_call.args().unwrap_or("{}"),
                    self.sensitivity,
                );

                Some(callback(request))
            } else {
                None
            }
        };

        let decision_metadata = if let Some(fut) = decision_fut {
            match tokio::time::timeout(PERMISSION_DECISION_TIMEOUT, fut).await {
                Ok(PermissionDecision::Allow) => ApprovalDecisionMetadata::new(
                    PermissionDecision::Allow,
                    policy,
                    ApprovalDecisionSource::Callback,
                    ApprovalDecisionReason::UserAllow,
                ),

                Ok(PermissionDecision::Deny) => ApprovalDecisionMetadata::new(
                    PermissionDecision::Deny,
                    policy,
                    ApprovalDecisionSource::Callback,
                    ApprovalDecisionReason::UserDeny,
                ),

                Err(_) => ApprovalDecisionMetadata::new(
                    PermissionDecision::Deny,
                    policy,
                    ApprovalDecisionSource::Timeout,
                    ApprovalDecisionReason::CallbackTimeout,
                ),
            }
        } else {
            ApprovalDecisionMetadata::new(
                PermissionDecision::Deny,
                policy,
                ApprovalDecisionSource::MissingCallback,
                ApprovalDecisionReason::MissingCallback,
            )
        };

        match decision_metadata.decision {
            PermissionDecision::Allow => {
                audit_decision(
                    ApprovalAuditEventKind::PermissionAllow,
                    &self.session_id,
                    &tool_name,
                    self.sensitivity,
                    decision_metadata,
                );

                let result = self.inner_tool.invoke(agent_context, tool_call).await;

                audit_execution_result(
                    &self.session_id,
                    &tool_name,
                    self.sensitivity,
                    policy,
                    &result,
                );

                cancel_guard.complete();

                result
            }

            PermissionDecision::Deny => {
                let event_kind = match decision_metadata.source {
                    ApprovalDecisionSource::Policy => ApprovalAuditEventKind::PermissionDeny,

                    ApprovalDecisionSource::Callback => ApprovalAuditEventKind::PermissionDeny,

                    ApprovalDecisionSource::MissingCallback => {
                        ApprovalAuditEventKind::PermissionDefaultFailClosed
                    }

                    ApprovalDecisionSource::Timeout => ApprovalAuditEventKind::PermissionTimeout,

                    ApprovalDecisionSource::PriorDeny => {
                        ApprovalAuditEventKind::PermissionDefaultFailClosed
                    }

                    ApprovalDecisionSource::Runtime => ApprovalAuditEventKind::PermissionCancel,
                };

                audit_decision(
                    event_kind,
                    &self.session_id,
                    &tool_name,
                    self.sensitivity,
                    decision_metadata,
                );

                *self.denied_flag.lock_unpoison() = true;

                cancel_guard.complete();

                Err(permission_error("approval decision denied the tool"))
            }
        }
    }

    fn name(&self) -> Cow<'_, str> {
        self.inner_tool.name()
    }

    fn tool_spec(&self) -> ToolSpec {
        self.inner_tool.tool_spec()
    }
}

// Manually implement some basic tools

#[derive(Clone)]

struct FsReadTool {
    workspace_root: std::path::PathBuf,
}

#[async_trait]

impl Tool for FsReadTool {
    async fn invoke(
        &self,

        _agent_context: &dyn AgentContext,

        tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        let args: serde_json::Value = serde_json::from_str(tool_call.args().unwrap_or("{}"))
            .map_err(swiftide_core::chat_completion::errors::ToolError::unknown)?;

        let path_str = args.get("path").and_then(|v| v.as_str()).ok_or_else(|| {
            swiftide_core::chat_completion::errors::ToolError::unknown(anyhow::anyhow!(
                "Missing 'path' parameter"
            ))
        })?;

        let target_path = std::path::Path::new(path_str);

        let target_path_absolute = if target_path.is_absolute() {
            target_path.to_path_buf()
        } else {
            self.workspace_root.join(target_path)
        };

        let canonical_workspace = self.workspace_root.canonicalize().map_err(|e| {
            swiftide_core::chat_completion::errors::ToolError::unknown(anyhow::anyhow!(
                "Workspace error: {e}"
            ))
        })?;

        let canonical_target = match target_path_absolute.canonicalize() {
            Ok(p) => p,

            Err(e) => {
                return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                    anyhow::anyhow!("Invalid path or file does not exist: {e}"),
                ));
            }
        };

        if !canonical_target.starts_with(&canonical_workspace) {
            return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!("Security violation: Access denied to path outside workspace"),
            ));
        }

        let metadata = tokio::fs::metadata(&canonical_target)
            .await
            .map_err(swiftide_core::chat_completion::errors::ToolError::unknown)?;

        if metadata.len() > FS_READ_MAX_BYTES {
            return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!(
                    "File too large: {} bytes (max {} bytes)",
                    metadata.len(),
                    FS_READ_MAX_BYTES
                ),
            ));
        }

        let content = tokio::fs::read_to_string(canonical_target)
            .await
            .map_err(swiftide_core::chat_completion::errors::ToolError::unknown)?;

        Ok(ToolOutput::Text(content))
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("fs_read")
    }

    fn tool_spec(&self) -> ToolSpec {
        ToolSpec {
            name: "fs_read".to_string(),

            description: "Read a file from the filesystem".to_string(),

            parameters_schema: Some(
                serde_json::from_value(serde_json::json!({

                    "type": "object",

                    "properties": {

                        "path": { "type": "string" }

                    },

                    "required": ["path"]

                }))
                .unwrap(),
            ),
        }
    }
}

#[derive(Clone)]
struct FsWriteTool {
    artifact_root: Option<std::path::PathBuf>,
}

#[derive(serde::Serialize, serde::Deserialize)]
struct FsWriteOutput {
    relative_path: String,
    mime_type: String,
    size_bytes: u64,
    display_name: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    kind: Option<String>,
}

fn deduce_artifact_kind(display_name: &str, storage_rel_path: &str, mime_type: &str) -> String {
    let lower_disp = display_name.to_ascii_lowercase();
    let lower_path = storage_rel_path.to_ascii_lowercase();
    if mime_type == "application/pdf"
        || lower_disp.ends_with(".pdf")
        || lower_path.ends_with(".pdf")
    {
        "pdf".to_string()
    } else if mime_type == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"
        || mime_type == "application/vnd.ms-excel"
        || mime_type == "text/csv"
        || lower_disp.ends_with(".xlsx")
        || lower_disp.ends_with(".xls")
        || lower_disp.ends_with(".csv")
        || lower_path.ends_with(".xlsx")
        || lower_path.ends_with(".xls")
        || lower_path.ends_with(".csv")
    {
        "xlsx".to_string()
    } else if mime_type == "text/html"
        || mime_type == "application/xhtml+xml"
        || lower_disp.ends_with(".html")
        || lower_disp.ends_with(".htm")
        || lower_path.ends_with(".html")
        || lower_path.ends_with(".htm")
    {
        "html".to_string()
    } else {
        "generic".to_string()
    }
}

fn fs_write_error(
    message: impl std::fmt::Display,
) -> swiftide_core::chat_completion::errors::ToolError {
    swiftide_core::chat_completion::errors::ToolError::unknown(anyhow::anyhow!(message.to_string()))
}

/// Derive the ordered stream events for a completed tool call.
///
/// On a successful `fs_write` whose structured output parses, an
/// [`AgentStreamEvent::ArtifactCreated`] is emitted BEFORE the
/// [`AgentStreamEvent::ToolResult`], and the artifact to persist in the
/// session index is returned. For any other tool, a failed call, or an
/// unparseable output, only the `ToolResult` is produced (the malformed
/// output is logged and skipped — the turn is never failed). The returned
/// events are in emission order.
fn build_after_tool_events(
    session_id: &str,
    tool_id: String,
    tool_name: String,
    succeeded: bool,
    raw_result: &str,
) -> (
    Vec<AgentStreamEvent>,
    Option<maho_types::artifact::ArtifactInfo>,
) {
    let mut events = Vec::new();
    let mut artifact_to_persist = None;
    if succeeded && tool_name == "fs_write" {
        match serde_json::from_str::<FsWriteOutput>(raw_result) {
            Ok(fw) => {
                let created_at_ms = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .map(|d| d.as_millis() as i64)
                    .unwrap_or(0);
                let kind = fw.kind.unwrap_or_else(|| {
                    deduce_artifact_kind(&fw.display_name, &fw.relative_path, &fw.mime_type)
                });
                let artifact = maho_types::artifact::ArtifactInfo {
                    artifact_id: maho_types::artifact::ArtifactId::new(),
                    session_id: session_id.to_string(),
                    display_name: fw.display_name,
                    mime_type: fw.mime_type,
                    size_bytes: fw.size_bytes,
                    storage_rel_path: fw.relative_path,
                    created_at_ms,
                    kind: Some(kind),
                };
                events.push(AgentStreamEvent::ArtifactCreated {
                    artifact: artifact.clone(),
                });
                artifact_to_persist = Some(artifact);
            }
            Err(err) => {
                tracing::warn!(
                    target = "maho_agent::swiftide_backend",
                    error = %err,
                    "fs_write output not parseable; skipping ArtifactCreated (ToolResult still emitted)"
                );
            }
        }
    }
    let sanitized = crate::permission::sanitize_tool_result(raw_result);
    events.push(AgentStreamEvent::ToolResult {
        id: tool_id,
        name: tool_name,
        result: sanitized,
        succeeded,
    });
    (events, artifact_to_persist)
}

#[async_trait]
impl Tool for FsWriteTool {
    async fn invoke(
        &self,
        _agent_context: &dyn AgentContext,
        tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        let args: serde_json::Value = serde_json::from_str(tool_call.args().unwrap_or("{}"))
            .map_err(swiftide_core::chat_completion::errors::ToolError::unknown)?;
        let relative_path = args
            .get("relative_path")
            .and_then(serde_json::Value::as_str)
            .ok_or_else(|| fs_write_error("Missing 'relative_path' parameter"))?;

        let content_opt = args.get("content").and_then(serde_json::Value::as_str);
        let content_b64_opt = args.get("content_b64").and_then(serde_json::Value::as_str);

        let content_bytes: Vec<u8> = match (content_opt, content_b64_opt) {
            (Some(_), Some(_)) => {
                return Err(fs_write_error(
                    "Invalid arguments: cannot provide both 'content' and 'content_b64'",
                ));
            }
            (None, None) => {
                return Err(fs_write_error(
                    "Invalid arguments: must provide either 'content' or 'content_b64'",
                ));
            }
            (Some(text), None) => text.as_bytes().to_vec(),
            (None, Some(b64)) => {
                use base64::Engine;
                base64::engine::general_purpose::STANDARD
                    .decode(b64.trim())
                    .map_err(|error| fs_write_error(format!("Invalid base64 payload: {error}")))?
            }
        };

        let mime_type = args
            .get("mime_type")
            .and_then(serde_json::Value::as_str)
            .unwrap_or("application/octet-stream");

        let size_bytes = u64::try_from(content_bytes.len())
            .map_err(|error| fs_write_error(format!("Artifact size is invalid: {error}")))?;
        if size_bytes > FS_WRITE_MAX_BYTES {
            return Err(fs_write_error(format!(
                "Artifact too large: {size_bytes} bytes (max {FS_WRITE_MAX_BYTES} bytes)"
            )));
        }

        let root = self
            .artifact_root
            .as_ref()
            .ok_or_else(|| fs_write_error("Artifact root is not configured"))?;
        let relative = std::path::Path::new(relative_path);
        if relative_path.is_empty()
            || relative.is_absolute()
            || relative
                .components()
                .any(|component| !matches!(component, std::path::Component::Normal(_)))
        {
            return Err(fs_write_error(
                "Security violation: relative_path must be a non-empty contained relative path",
            ));
        }
        let display_name = relative
            .file_name()
            .and_then(std::ffi::OsStr::to_str)
            .ok_or_else(|| fs_write_error("relative_path must end in a valid UTF-8 file name"))?
            .to_string();

        let canonical_root = tokio::fs::canonicalize(root)
            .await
            .map_err(|error| fs_write_error(format!("Artifact root error: {error}")))?;
        let mut canonical_parent = canonical_root.clone();
        if let Some(relative_parent) = relative.parent() {
            for component in relative_parent.components() {
                let std::path::Component::Normal(component) = component else {
                    return Err(fs_write_error("Security violation: invalid parent path"));
                };
                let next = canonical_parent.join(component);
                match tokio::fs::symlink_metadata(&next).await {
                    Ok(metadata) if metadata.file_type().is_symlink() => {
                        return Err(fs_write_error(
                            "Security violation: symbolic links are not allowed in artifact paths",
                        ));
                    }
                    Ok(metadata) if !metadata.is_dir() => {
                        return Err(fs_write_error("Artifact path parent is not a directory"));
                    }
                    Ok(_) => {}
                    Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                        tokio::fs::create_dir(&next)
                            .await
                            .map_err(swiftide_core::chat_completion::errors::ToolError::unknown)?;
                    }
                    Err(error) => {
                        return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                            error,
                        ));
                    }
                }
                canonical_parent = tokio::fs::canonicalize(&next)
                    .await
                    .map_err(|error| fs_write_error(format!("Artifact parent error: {error}")))?;
                if !canonical_parent.starts_with(&canonical_root) {
                    return Err(fs_write_error(
                        "Security violation: Access denied to path outside artifact root",
                    ));
                }
            }
        }
        let target = canonical_parent.join(
            relative
                .file_name()
                .ok_or_else(|| fs_write_error("relative_path has no file name"))?,
        );
        if let Ok(metadata) = tokio::fs::symlink_metadata(&target).await {
            if metadata.file_type().is_symlink() {
                return Err(fs_write_error(
                    "Security violation: refusing to replace a symbolic link",
                ));
            }
        }

        let temp_path =
            canonical_parent.join(format!(".maho-fs-write-{}.tmp", uuid::Uuid::new_v4()));
        let write_result = async {
            tokio::fs::write(&temp_path, &content_bytes).await?;
            tokio::fs::rename(&temp_path, &target).await
        }
        .await;
        if let Err(error) = write_result {
            let _ = tokio::fs::remove_file(&temp_path).await;
            return Err(fs_write_error(format!(
                "Failed to write artifact atomically: {error}"
            )));
        }

        let kind = deduce_artifact_kind(&display_name, relative_path, mime_type);
        let output = serde_json::to_string(&FsWriteOutput {
            relative_path: relative_path.to_string(),
            mime_type: mime_type.to_string(),
            size_bytes,
            display_name,
            kind: Some(kind),
        })
        .map_err(swiftide_core::chat_completion::errors::ToolError::unknown)?;
        Ok(ToolOutput::Text(output))
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("fs_write")
    }

    fn tool_spec(&self) -> ToolSpec {
        ToolSpec {
            name: "fs_write".to_string(),
            description: "Write an artifact inside the session artifact directory".to_string(),
            parameters_schema: serde_json::from_str(
                r#"{
                    "type": "object",
                    "additionalProperties": false,
                    "properties": {
                        "relative_path": { "type": "string" },
                        "content": { "type": "string" },
                        "content_b64": { "type": "string" },
                        "mime_type": { "type": "string" }
                    },
                    "required": ["relative_path"]
                }"#,
            )
            .ok(),
        }
    }
}

#[derive(Clone)]

struct ShellExecTool {
    workspace_root: std::path::PathBuf,
}

#[async_trait]

impl Tool for ShellExecTool {
    async fn invoke(
        &self,

        _agent_context: &dyn AgentContext,

        tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        let args: serde_json::Value = serde_json::from_str(tool_call.args().unwrap_or("{}"))
            .map_err(swiftide_core::chat_completion::errors::ToolError::unknown)?;

        let command = args
            .get("command")
            .and_then(|v| v.as_str())
            .ok_or_else(|| {
                swiftide_core::chat_completion::errors::ToolError::unknown(anyhow::anyhow!(
                    "Missing 'command' parameter"
                ))
            })?;

        // 1. Safety check: reject shell metacharacters

        let forbidden_chars = ['|', ';', '&', '$', '`', '>', '<', '(', ')', '*', '\\'];

        if command.chars().any(|c| forbidden_chars.contains(&c)) {
            return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!("Security violation: Shell metacharacters are not allowed"),
            ));
        }

        // 2. Parse command into arguments (quote-aware so string flags like -e "..." work)

        let mut parts: Vec<String> = Vec::new();
        let mut current = String::new();
        let mut in_single = false;
        let mut in_double = false;
        for ch in command.chars() {
            match ch {
                '\'' if !in_double => in_single = !in_single,
                '"' if !in_single => in_double = !in_double,
                ch if ch.is_whitespace() && !in_single && !in_double => {
                    if !current.is_empty() {
                        parts.push(std::mem::take(&mut current));
                    }
                }
                _ => current.push(ch),
            }
        }
        if !current.is_empty() {
            parts.push(current);
        }

        if parts.is_empty() {
            return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!("Empty command"),
            ));
        }

        let exe = parts[0].as_str();

        // Strict validation: only git, lsof, and the Maho browser CLI.

        if exe != "git" && exe != "lsof" && exe != "maho" {
            return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!(format!(
                    "Security violation: Command '{exe}' is not allowed"
                )),
            ));
        }

        if exe == "git" {
            if parts.len() < 2 {
                return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                    anyhow::anyhow!("Security violation: git subcommand required"),
                ));
            }

            let sub = parts[1].as_str();

            let allowed_subs = ["status", "diff", "log", "rev-parse"];

            if !allowed_subs.contains(&sub) {
                return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                    anyhow::anyhow!(format!(
                        "Security violation: git subcommand '{sub}' is not allowed"
                    )),
                ));
            }

            if sub == "diff" {
                if parts.len() == 2 {
                    return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                        anyhow::anyhow!(
                            "Security violation: bare 'git diff' is not allowed (would expose uncommitted working-tree content)"
                        ),
                    ));
                }

                for arg in &parts[2..] {
                    if !arg.starts_with('-') {
                        return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                            anyhow::anyhow!(
                                "Security violation: git diff with positional refs is not allowed (would enable committed-content exfiltration)"
                            ),
                        ));
                    }
                }
            }

            for arg in &parts[2..] {
                if arg.starts_with('-') {
                    let allowed_flags = ["-n", "--oneline", "--show-toplevel"];

                    if !allowed_flags.contains(&arg.as_str())
                        && !arg.chars().skip(1).all(|c| c.is_ascii_digit())
                    {
                        return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                            anyhow::anyhow!(format!(
                                "Security violation: git argument '{arg}' is not allowed"
                            )),
                        ));
                    }
                } else {
                    if !arg.chars().all(|c| {
                        c.is_ascii_alphanumeric() || c == '.' || c == '_' || c == '-' || c == '/'
                    }) {
                        return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                            anyhow::anyhow!(format!(
                                "Security violation: git argument '{arg}' contains invalid characters"
                            )),
                        ));
                    }
                }
            }
        } else if exe == "lsof" {
            if parts.len() != 2 {
                return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                    anyhow::anyhow!(
                        "Security violation: lsof must be run with exactly one argument"
                    ),
                ));
            }

            let arg = parts[1].as_str();

            let lock_path_1 = format!("{}/.build.lock", self.workspace_root.display());

            if arg != ".build.lock" && arg != lock_path_1 {
                return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                    anyhow::anyhow!(
                        "Security violation: lsof is only allowed on the workspace lock file"
                    ),
                ));
            }
        }

        // 3. Scrub environment (preserve PATH and HOME; HOME is required by
        // the Maho CLI to resolve its socket and user-data directories)

        let path_env = std::env::var("PATH").unwrap_or_default();
        let home_env = std::env::var("HOME").unwrap_or_default();

        // 4. Execute command with a timeout (e.g., 5 seconds) and cap output

        let mut cmd = tokio::process::Command::new(exe);

        cmd.args(&parts[1..])
            .current_dir(&self.workspace_root)
            .env_clear()
            .env("PATH", path_env)
            .env("HOME", home_env);

        let output_fut = cmd.output();

        let timeout_secs = if exe == "maho" { 30 } else { 5 };
        let output = match tokio::time::timeout(std::time::Duration::from_secs(timeout_secs), output_fut).await
        {
            Ok(Ok(out)) => out,

            Ok(Err(e)) => {
                return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                    e,
                ));
            }

            Err(_) => {
                return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                    anyhow::anyhow!("Command execution timed out after 5 seconds"),
                ));
            }
        };

        let mut result = String::from_utf8_lossy(&output.stdout).into_owned()
            + &String::from_utf8_lossy(&output.stderr).into_owned();

        // 5. Cap output to 10k characters to prevent self-exfiltration of huge logs/files

        if result.len() > 10000 {
            truncate_at_char_boundary(&mut result, 10000);

            result.push_str("\n... [Output truncated to 10,000 characters]");
        }

        Ok(ToolOutput::Text(result))
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("shell_exec")
    }

    fn tool_spec(&self) -> ToolSpec {
        ToolSpec {
            name: "shell_exec".to_string(),

            description: "Execute a command in the shell".to_string(),

            parameters_schema: Some(
                serde_json::from_value(serde_json::json!({

                    "type": "object",

                    "properties": {

                        "command": { "type": "string" }

                    },

                    "required": ["command"]

                }))
                .unwrap(),
            ),
        }
    }
}

#[derive(Clone)]

struct CameraTool;

#[async_trait]

impl Tool for CameraTool {
    async fn invoke(
        &self,

        _agent_context: &dyn AgentContext,

        _tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        Ok(ToolOutput::Text(
            "Picture captured successfully. [Mock Image Data]".to_string(),
        ))
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("camera")
    }

    fn tool_spec(&self) -> ToolSpec {
        ToolSpec {
            name: "camera".to_string(),

            description: "Capture a photo using the mobile camera".to_string(),

            parameters_schema: Some(
                serde_json::from_value(serde_json::json!({

                    "type": "object",

                    "properties": {}

                }))
                .unwrap(),
            ),
        }
    }
}

#[derive(Clone)]

struct PhotoLibraryTool;

#[async_trait]

impl Tool for PhotoLibraryTool {
    async fn invoke(
        &self,

        _agent_context: &dyn AgentContext,

        _tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        Ok(ToolOutput::Text(
            "Photo selected from mobile library. [Mock Photo]".to_string(),
        ))
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("photo_library")
    }

    fn tool_spec(&self) -> ToolSpec {
        ToolSpec {
            name: "photo_library".to_string(),

            description: "Select a photo from the mobile photo library".to_string(),

            parameters_schema: Some(
                serde_json::from_value(serde_json::json!({

                    "type": "object",

                    "properties": {}

                }))
                .unwrap(),
            ),
        }
    }
}

#[derive(Clone)]

struct ClipboardTool;

#[async_trait]

impl Tool for ClipboardTool {
    async fn invoke(
        &self,

        _agent_context: &dyn AgentContext,

        tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        let args: serde_json::Value = serde_json::from_str(tool_call.args().unwrap_or("{}"))
            .map_err(swiftide_core::chat_completion::errors::ToolError::unknown)?;

        let action = args.get("action").and_then(|v| v.as_str()).ok_or_else(|| {
            swiftide_core::chat_completion::errors::ToolError::unknown(anyhow::anyhow!(
                "Missing 'action' parameter"
            ))
        })?;

        if action == "write" {
            let text = args
                .get("text")
                .and_then(|v: &serde_json::Value| v.as_str())
                .unwrap_or("");

            Ok(ToolOutput::Text(format!("Copied to clipboard: {text}")))
        } else {
            Ok(ToolOutput::Text(
                "Content read from clipboard: [Mock Clipboard Content]".to_string(),
            ))
        }
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("clipboard")
    }

    fn tool_spec(&self) -> ToolSpec {
        ToolSpec {
            name: "clipboard".to_string(),

            description: "Read or write to the system clipboard".to_string(),

            parameters_schema: Some(
                serde_json::from_value(serde_json::json!({

                    "type": "object",

                    "properties": {

                        "action": { "type": "string", "enum": ["read", "write"] },

                        "text": { "type": "string" }

                    },

                    "required": ["action"]

                }))
                .unwrap(),
            ),
        }
    }
}

#[derive(Clone)]
pub struct WebSearchTool {
    pub router: crate::web_search::WebSearchEntitlementRouter,
    pub entitlement: Arc<Mutex<crate::web_search::SearchEntitlement>>,
}

#[async_trait]
impl Tool for WebSearchTool {
    async fn invoke(
        &self,
        _agent_context: &dyn AgentContext,
        tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        let raw_args = tool_call.args().unwrap_or("{}");
        let args: serde_json::Value = match serde_json::from_str(raw_args) {
            Ok(v) => v,
            Err(e) => {
                return Ok(ToolOutput::Text(
                    serde_json::json!({
                        "error": "invalid_arguments",
                        "reason": format!("Failed to parse arguments JSON: {e}"),
                    })
                    .to_string(),
                ));
            }
        };

        let query_str = args["query"].as_str().unwrap_or("").to_string();
        let max_results = args["max_results"]
            .as_u64()
            .map(|v| v as usize)
            .unwrap_or(crate::web_search::DEFAULT_MAX_RESULTS);
        let freshness_days = args["freshness_days"].as_u64().map(|v| v as u32);
        let tier = match args["tier"].as_str() {
            Some("premium") => crate::web_search::SearchTier::Premium,
            Some("enterprise") => crate::web_search::SearchTier::Enterprise,
            _ => crate::web_search::SearchTier::Free,
        };

        let mut query = crate::web_search::WebSearchQuery::new(query_str)
            .with_max_results(max_results)
            .with_tier(tier);
        if let Some(fd) = freshness_days {
            query = query.with_freshness(fd);
        }

        let entitlement = *self.entitlement.lock_unpoison();
        match self.router.execute_search(&entitlement, query).await {
            Ok(resp) => {
                let json = serde_json::to_string(&resp).unwrap_or_else(|e| {
                    format!(r#"{{"error":"serialization_error","reason":"{e}"}}"#)
                });
                Ok(ToolOutput::Text(json))
            }
            Err(crate::web_search::WebSearchError::TypedUnavailable(reason)) => {
                Ok(ToolOutput::Text(
                    serde_json::json!({
                        "error": "typed_unavailable",
                        "reason": reason,
                    })
                    .to_string(),
                ))
            }
            Err(err) => Ok(ToolOutput::Text(
                serde_json::json!({
                    "error": "search_failed",
                    "reason": err.to_string(),
                })
                .to_string(),
            )),
        }
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("web_search")
    }

    fn tool_spec(&self) -> ToolSpec {
        let def = crate::tool_registry::web_search_definition();
        ToolSpec {
            name: def.name,
            description: def.description,
            parameters_schema: serde_json::from_value(def.parameters_schema).ok(),
        }
    }
}

#[derive(Clone)]
pub struct ImageSearchTool {
    pub router: crate::image_search::ImageSearchEntitlementRouter,
    pub entitlement: Arc<Mutex<crate::web_search::SearchEntitlement>>,
}

#[async_trait]
impl Tool for ImageSearchTool {
    async fn invoke(
        &self,
        _agent_context: &dyn AgentContext,
        tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        let raw_args = tool_call.args().unwrap_or("{}");
        let args: serde_json::Value = match serde_json::from_str(raw_args) {
            Ok(v) => v,
            Err(e) => {
                return Ok(ToolOutput::Text(
                    serde_json::json!({
                        "error": "invalid_arguments",
                        "reason": format!("Failed to parse arguments JSON: {e}"),
                    })
                    .to_string(),
                ));
            }
        };

        let query_str = args["query"].as_str().unwrap_or("").to_string();
        let max_results = args["max_results"]
            .as_u64()
            .map(|v| v as usize)
            .unwrap_or(crate::image_search::DEFAULT_MAX_RESULTS);
        let safe_search = args["safe_search"].as_bool().unwrap_or(true);
        let tier = match args["tier"].as_str() {
            Some("premium") => crate::image_search::SearchTier::Premium,
            Some("enterprise") => crate::image_search::SearchTier::Enterprise,
            _ => crate::image_search::SearchTier::Free,
        };

        let query = crate::image_search::ImageSearchQuery::new(query_str)
            .with_max_results(max_results)
            .with_safe_search(safe_search)
            .with_tier(tier);

        let entitlement = *self.entitlement.lock_unpoison();
        match self.router.execute_search(&entitlement, query).await {
            Ok(resp) => {
                let json = serde_json::to_string(&resp).unwrap_or_else(|e| {
                    format!(r#"{{"error":"serialization_error","reason":"{e}"}}"#)
                });
                Ok(ToolOutput::Text(json))
            }
            Err(crate::image_search::ImageSearchError::TypedUnavailable(reason)) => {
                Ok(ToolOutput::Text(
                    serde_json::json!({
                        "error": "typed_unavailable",
                        "reason": reason,
                    })
                    .to_string(),
                ))
            }
            Err(err) => Ok(ToolOutput::Text(
                serde_json::json!({
                    "error": "search_failed",
                    "reason": err.to_string(),
                })
                .to_string(),
            )),
        }
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("image_search")
    }

    fn tool_spec(&self) -> ToolSpec {
        let def = crate::tool_registry::image_search_definition();
        ToolSpec {
            name: def.name,
            description: def.description,
            parameters_schema: serde_json::from_value(def.parameters_schema).ok(),
        }
    }
}

#[derive(Clone)]
pub struct NotificationWaitTool {
    pub wait_registry: Arc<Mutex<crate::event_wait::WaitRegistry>>,
    pub wake_notify: Arc<tokio::sync::Notify>,
    pub session_id: String,
    pub notification_activation: Arc<Mutex<crate::event_wait::NotificationActivation>>,
}

#[async_trait]
impl Tool for NotificationWaitTool {
    async fn invoke(
        &self,
        _agent_context: &dyn AgentContext,
        tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        let args: serde_json::Value =
            serde_json::from_str(tool_call.args().unwrap_or("{}")).unwrap_or_default();

        let sender = args
            .get("sender")
            .and_then(|v| v.as_str())
            .map(|s| s.to_string());
        let source = args
            .get("source")
            .and_then(|v| v.as_str())
            .map(|s| s.to_string());
        let topic = args
            .get("topic")
            .and_then(|v| v.as_str())
            .map(|s| s.to_string());
        let timeout_ms = args
            .get("timeout_ms")
            .and_then(|v| v.as_u64())
            .unwrap_or(30_000);

        let activation = *self.notification_activation.lock_unpoison();
        if let Err(e) = activation.ensure_granted() {
            return Ok(ToolOutput::Text(
                serde_json::json!({
                    "error": "notification_permission_denied",
                    "reason": e.to_string(),
                })
                .to_string(),
            ));
        }

        let run_id = crate::run_journal::AgentRunId::new(self.session_id.clone());
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_millis() as u64;
        let deadline = now + timeout_ms;

        let mut filter = crate::event_wait::EventFilter::notification(sender);
        if let Some(src) = source {
            filter = filter.with_source(src);
        }
        if let Some(top) = topic {
            filter = filter.with_topic(top);
        }

        let _handle = {
            let mut reg = self.wait_registry.lock_unpoison();
            match reg.suspend(run_id.clone(), filter, deadline) {
                Ok(h) => h,
                Err(e) => {
                    return Ok(ToolOutput::Text(
                        serde_json::json!({
                            "error": "wait_registration_failed",
                            "reason": e.to_string(),
                        })
                        .to_string(),
                    ));
                }
            }
        };

        let start = std::time::Instant::now();
        let timeout_dur = std::time::Duration::from_millis(timeout_ms);

        loop {
            {
                let mut reg = self.wait_registry.lock_unpoison();
                if let Ok(status) = reg.poll(&run_id) {
                    match status {
                        crate::event_wait::WaitStatus::Woken { token, event } => {
                            return Ok(ToolOutput::Text(
                                serde_json::json!({
                                    "status": "woken",
                                    "token": token,
                                    "event": event,
                                })
                                .to_string(),
                            ));
                        }
                        crate::event_wait::WaitStatus::Cancelled { .. } => {
                            return Ok(ToolOutput::Text(
                                serde_json::json!({
                                    "status": "cancelled",
                                })
                                .to_string(),
                            ));
                        }
                        crate::event_wait::WaitStatus::TimedOut { .. } => {
                            return Ok(ToolOutput::Text(
                                serde_json::json!({
                                    "status": "timed_out",
                                })
                                .to_string(),
                            ));
                        }
                        crate::event_wait::WaitStatus::Suspended { .. } => {}
                    }
                }
            }

            let elapsed = start.elapsed();
            if elapsed >= timeout_dur {
                return Ok(ToolOutput::Text(
                    serde_json::json!({
                        "status": "timed_out",
                    })
                    .to_string(),
                ));
            }

            let remaining = timeout_dur.saturating_sub(elapsed);
            tokio::select! {
                _ = self.wake_notify.notified() => {},
                _ = tokio::time::sleep(remaining) => {},
            }
        }
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("notification_wait")
    }

    fn tool_spec(&self) -> ToolSpec {
        let def = crate::tool_registry::notification_wait_definition();
        ToolSpec {
            name: def.name,
            description: def.description,
            parameters_schema: serde_json::from_value(def.parameters_schema).ok(),
        }
    }
}

pub fn classify_chat_error(err_str: &str) -> crate::retry_policy::ErrorClass {
    let lower = err_str.to_lowercase();
    if lower.contains("rate limit") || lower.contains("429") || lower.contains("too many requests")
    {
        crate::retry_policy::ErrorClass::RateLimited
    } else if lower.contains("policy")
        || lower.contains("denied")
        || lower.contains("permission")
        || lower.contains("forbidden")
        || lower.contains("403")
        || lower.contains("unauthorized")
        || lower.contains("401")
    {
        crate::retry_policy::ErrorClass::PolicyDenied
    } else if lower.contains("uncertain") || lower.contains("mutation") {
        crate::retry_policy::ErrorClass::UncertainMutation
    } else if lower.contains("timeout")
        || lower.contains("timed out")
        || lower.contains("connection reset")
        || lower.contains("broken pipe")
        || lower.contains("network")
        || lower.contains("transient")
        || lower.contains("econnrefused")
        || lower.contains("temporary")
    {
        crate::retry_policy::ErrorClass::TransientNetwork
    } else if (lower.contains("context")
        && (lower.contains("too large") || lower.contains("length")))
        || lower.contains("context_length_exceeded")
        || lower.contains("context_too_large")
    {
        crate::retry_policy::ErrorClass::ContextTooLarge
    } else {
        crate::retry_policy::ErrorClass::PermanentFailure
    }
}

#[derive(Clone)]
pub struct RetryChatCompletion {
    pub inner: Arc<dyn ChatCompletion>,
    pub retry_policy: crate::retry_policy::RetryPolicy,
    pub fallback_reasons: Arc<Mutex<Vec<crate::retry_policy::FallbackSwitchReason>>>,
    pub model_name: String,
    pub fallback_model_name: Option<String>,
    pub journal: Option<Arc<Mutex<crate::run_journal::RunJournal>>>,
    pub run_id: Option<crate::run_journal::AgentRunId>,
}

#[async_trait]
impl ChatCompletion for RetryChatCompletion {
    async fn complete(
        &self,
        request: &ChatCompletionRequest,
    ) -> Result<
        swiftide_core::chat_completion::ChatCompletionResponse,
        swiftide_core::chat_completion::errors::LanguageModelError,
    > {
        let mut attempt = 1;
        let mut current_request = request.clone();
        let mut emergency_compacted = false;

        loop {
            match self.inner.complete(&current_request).await {
                Ok(resp) => return Ok(resp),
                Err(err) => {
                    let err_str = err.to_string();
                    let error_class = classify_chat_error(&err_str);

                    if error_class == crate::retry_policy::ErrorClass::ContextTooLarge {
                        if !emergency_compacted {
                            emergency_compacted = true;
                            tracing::warn!(
                                target: "maho_agent::retry",
                                model = %self.model_name,
                                "ContextTooLarge encountered; executing emergency compaction retry"
                            );

                            let session_id = self
                                .run_id
                                .as_ref()
                                .map(|r| r.as_str())
                                .unwrap_or("session-emergency");
                            let compaction_res = crate::context_compaction::emergency_compact(
                                session_id,
                                current_request.messages(),
                            );

                            if let Ok(res) = compaction_res {
                                if res.was_compacted {
                                    if let (Some(journal), Some(run_id)) =
                                        (&self.journal, &self.run_id)
                                    {
                                        let mut j = journal.lock_unpoison();
                                        if !j.runs().contains_key(run_id) {
                                            let _ = j.start_run(run_id.clone(), session_id);
                                        }
                                        let now_ts = std::time::SystemTime::now()
                                            .duration_since(std::time::UNIX_EPOCH)
                                            .unwrap_or_default()
                                            .as_secs();
                                        let _ = j.append_event(
                                            run_id,
                                            "compacting",
                                            serde_json::json!({
                                                "status": "compacting",
                                                "trigger": "emergency_overflow_recovery",
                                                "triggered_at": now_ts,
                                                "session_id": session_id,
                                                "estimated_tokens_before": res.estimated_tokens_before,
                                                "estimated_tokens_after": res.estimated_tokens_after,
                                                "dropped_turns": res.dropped_indices,
                                                "kept_markers": res.checkpoint.as_ref().map(|c| c.preserved_keys.clone()).unwrap_or_default(),
                                            })
                                            .to_string(),
                                        );
                                    }

                                    current_request = ChatCompletionRequest {
                                        messages: res.messages,
                                        tools_spec: request.tools_spec.clone(),
                                    };

                                    attempt += 1;
                                    continue;
                                }
                            }

                            return Err(
                                swiftide_core::chat_completion::errors::LanguageModelError::context_length_exceeded(
                                    anyhow::anyhow!(
                                        "ContextTooLarge: context overflow could not be compacted further: {}",
                                        err_str
                                    ),
                                ),
                            );
                        } else {
                            return Err(
                                swiftide_core::chat_completion::errors::LanguageModelError::context_length_exceeded(
                                    anyhow::anyhow!(
                                        "ContextTooLarge: retry after emergency compaction failed: {}",
                                        err_str
                                    ),
                                ),
                            );
                        }
                    }

                    let mut rng = crate::retry_policy::DeterministicRng::seeded(attempt as u64);
                    let jitter = rng.next_f64();

                    let fallback_target = self
                        .fallback_model_name
                        .as_deref()
                        .unwrap_or("fallback-model");

                    let decision = self.retry_policy.evaluate_with_fallback(
                        error_class,
                        crate::retry_policy::IdempotencyClass::ReadOnly,
                        attempt,
                        jitter,
                        &self.model_name,
                        fallback_target,
                    );

                    match decision {
                        crate::retry_policy::RetryDecision::RetryAfter(delay_ms) => {
                            tracing::warn!(
                                target: "maho_agent::retry",
                                attempt = attempt,
                                delay_ms = delay_ms,
                                error = %err_str,
                                "Retrying chat completion after backoff"
                            );
                            if delay_ms > 0 {
                                tokio::time::sleep(std::time::Duration::from_millis(delay_ms))
                                    .await;
                            }
                            attempt += 1;
                        }
                        crate::retry_policy::RetryDecision::SwitchFallback(reason) => {
                            self.fallback_reasons.lock_unpoison().push(reason);
                            return Err(err);
                        }
                        crate::retry_policy::RetryDecision::RequiresConfirmation => {
                            return Err(swiftide_core::chat_completion::errors::LanguageModelError::permanent(
                                anyhow::anyhow!("NeedsResolution: {}", err_str),
                            ));
                        }
                        crate::retry_policy::RetryDecision::FailFast(_) => {
                            return Err(err);
                        }
                    }
                }
            }
        }
    }

    async fn complete_stream(
        &self,
        request: &ChatCompletionRequest,
    ) -> swiftide_core::chat_completion::ChatCompletionStream {
        self.inner.complete_stream(request).await
    }
}

use crate::{BrowserToolBridge, SecureKeyBundle};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]

pub enum BrowserActionToolsState {
    Disabled,

    Enabled,
}

impl BrowserActionToolsState {
    pub const fn from_enabled(enabled: bool) -> Self {
        if enabled {
            Self::Enabled
        } else {
            Self::Disabled
        }
    }

    pub const fn is_enabled(self) -> bool {
        matches!(self, Self::Enabled)
    }
}

impl Default for BrowserActionToolsState {
    fn default() -> Self {
        Self::Disabled
    }
}

#[derive(Clone)]
struct BrowserToolShim {
    capability_id: String,

    name: String,

    description: String,

    parameters_schema: serde_json::Value,

    sensitivity: ToolSensitivity,

    permission: ToolPermission,

    bridge: Arc<dyn BrowserToolBridge>,

    context: BrowserToolShimContext,
}

#[derive(Clone)]

struct BrowserToolShimContext {
    session_id: String,

    approval_policy: Arc<Mutex<ApprovalPolicy>>,
    fallback_ladder: Option<Arc<Mutex<FallbackLadderState>>>,
    opened_tab_id: Arc<Mutex<Option<i64>>>,
    mail_authorization_state: Arc<Mutex<crate::MailAuthorizationState>>,
}

impl BrowserToolShimContext {
    fn discovery(
        approval_policy: Arc<Mutex<ApprovalPolicy>>,
        mail_authorization_state: Arc<Mutex<crate::MailAuthorizationState>>,
    ) -> Self {
        Self {
            session_id: "browser-tool-discovery".to_string(),

            approval_policy,

            fallback_ladder: Option::None,

            opened_tab_id: Arc::new(Mutex::new(None)),
            mail_authorization_state,
        }
    }

    fn turn(
        session_id: &str,

        approval_policy: Arc<Mutex<ApprovalPolicy>>,

        fallback_ladder: Arc<Mutex<FallbackLadderState>>,
        mail_authorization_state: Arc<Mutex<crate::MailAuthorizationState>>,
    ) -> Self {
        Self {
            session_id: session_id.to_string(),

            approval_policy,

            fallback_ladder: Some(fallback_ladder),

            opened_tab_id: Arc::new(Mutex::new(None)),
            mail_authorization_state,
        }
    }
}

#[async_trait]

impl Tool for BrowserToolShim {
    async fn invoke(
        &self,

        _agent_context: &dyn AgentContext,

        tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        if crate::classify_mail_tool(&self.name).is_some() {
            match crate::authorize_mail_tool(
                &self.name,
                *self.context.mail_authorization_state.lock_unpoison(),
                *self.context.approval_policy.lock_unpoison(),
            ) {
                crate::MailAuthorizationAction::Allow => {}
                crate::MailAuthorizationAction::RequireApproval => {}
                crate::MailAuthorizationAction::Deny(reason) => {
                    return Err(permission_error(reason));
                }
            }
        }

        let args = if self.name == "browser_type" || self.name == "input.type" {
            match serde_json::from_str::<serde_json::Value>(tool_call.args().unwrap_or("{}")) {
                Ok(serde_json::Value::Object(mut m)) => {
                    m.insert("allow_credentials".to_string(), serde_json::json!(true));
                    if is_v2_tool(&self.name) && !m.contains_key("max_bytes") {
                        m.insert("max_bytes".to_string(), serde_json::json!(24_000));
                    }
                    serde_json::Value::Object(m).to_string()
                }
                _ => tool_call.args().unwrap_or("{}").to_string(),
            }
        } else if is_v2_tool(&self.name) {
            match serde_json::from_str::<serde_json::Value>(tool_call.args().unwrap_or("{}")) {
                Ok(serde_json::Value::Object(mut m)) => {
                    if !m.contains_key("max_bytes") {
                        m.insert("max_bytes".to_string(), serde_json::json!(24_000));
                    }
                    serde_json::Value::Object(m).to_string()
                }
                _ => serde_json::json!({ "max_bytes": 24_000 }).to_string(),
            }
        } else {
            tool_call.args().unwrap_or("{}").to_string()
        };

        self.enforce_fallback_ladder(&args)?;

        let name = self.name.clone();
        let capability_id = self.capability_id.clone();

        let bridge = Arc::clone(&self.bridge);

        // Grant exact origin before any URL-bearing action.

        if let Some(ref origin) = Self::extract_exact_origin(&args) {
            let grant_bridge = Arc::clone(&bridge);

            let grant_args = serde_json::json!({ "origin": origin }).to_string();

            Self::execute_bridge(grant_bridge, "browser_grant_exact_origin", &grant_args).await?;
        }

        // Acquire a non-stealing lease before any mutation.

        if Self::needs_tab_lease(&name) {
            let resolved_tab =
                Self::resolve_leaseable_tab_id(&bridge, &args, &self.context.opened_tab_id).await?;

            if let Some(tab_id) = resolved_tab {
                let lease_bridge = Arc::clone(&bridge);

                let lease_args = serde_json::json!({ "tab_id": tab_id }).to_string();

                Self::execute_bridge(lease_bridge, "browser_acquire_lease", &lease_args).await?;
                let args_with_tab = match serde_json::from_str::<serde_json::Value>(&args) {
                    Ok(serde_json::Value::Object(mut m)) => {
                        m.insert("tab_id".to_string(), serde_json::json!(tab_id));
                        serde_json::Value::Object(m).to_string()
                    }
                    _ => serde_json::json!({ "tab_id": tab_id }).to_string(),
                };

                let dispatch_bridge = Arc::clone(&bridge);

                let result = dispatch_bridge
                    .execute_tool(&capability_id, &args_with_tab)
                    .await
                    .map(|execution| execution.output_json);

                return match result {
                    Ok(mut json) => {
                        Self::cap_browser_result(&mut json);
                        self.remember_opened_tab(&json);
                        if Self::is_navigation_tool(&name) {
                            Self::settle_after_navigation(Arc::clone(&bridge)).await;
                        }
                        Ok(ToolOutput::Text(json))
                    }

                    Err(error) if Self::should_open_tab_instead(&name, &error.message) => {
                        let fallback_bridge = Arc::clone(&bridge);

                        let fallback =
                            Self::execute_bridge(fallback_bridge, "browser_tab_new", &args).await?;

                        self.remember_opened_tab(&fallback);

                        Ok(ToolOutput::Text(fallback))
                    }

                    Err(error) => Ok(ToolOutput::Text(Self::error_observation(
                        &self.name, &error,
                    ))),
                };
            }
        }

        let dispatch_bridge = Arc::clone(&bridge);

        let dispatch_args = args.clone();

        let result = dispatch_bridge
            .execute_tool(&capability_id, &dispatch_args)
            .await
            .map(|execution| execution.output_json);

        match result {
            Ok(mut json) => {
                if crate::classify_mail_tool(&self.name).is_some() {
                    if let crate::MailAuthorizationAction::Deny(reason) = crate::authorize_mail_tool(
                        &self.name,
                        *self.context.mail_authorization_state.lock_unpoison(),
                        *self.context.approval_policy.lock_unpoison(),
                    ) {
                        return Err(permission_error(reason));
                    }
                }
                Self::cap_browser_result(&mut json);
                self.remember_opened_tab(&json);
                if Self::is_navigation_tool(&name) {
                    Self::settle_after_navigation(Arc::clone(&bridge)).await;
                }
                Ok(ToolOutput::Text(json))
            }

            Err(error) if Self::should_open_tab_instead(&name, &error.message) => {
                let fallback_bridge = Arc::clone(&bridge);

                let fallback =
                    Self::execute_bridge(fallback_bridge, "browser_tab_new", &args).await?;

                self.remember_opened_tab(&fallback);

                Ok(ToolOutput::Text(fallback))
            }

            Err(error) => Ok(ToolOutput::Text(Self::error_observation(
                &self.name, &error,
            ))),
        }
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed(&self.name)
    }

    fn tool_spec(&self) -> ToolSpec {
        let parameters_schema = match serde_json::from_value(self.parameters_schema.clone()) {
            Ok(schema) => Some(schema),

            Err(error) => {
                tracing::error!(

                    target: "maho_agent::swiftide_backend",

                    tool = %self.name,

                    error = %error,

                    "invalid browser tool schema"

                );

                None
            }
        };

        ToolSpec {
            name: self.name.clone(),

            description: self.description.clone(),

            parameters_schema,
        }
    }
}

impl BrowserToolShim {
    async fn execute_bridge(
        bridge: Arc<dyn BrowserToolBridge>,
        capability_id: &str,
        args_json: &str,
    ) -> Result<String, swiftide_core::chat_completion::errors::ToolError> {
        bridge
            .execute_tool(capability_id, args_json)
            .await
            .map(|execution| execution.output_json)
            .map_err(|error| {
                swiftide_core::chat_completion::errors::ToolError::unknown(anyhow::anyhow!(
                    error.to_string()
                ))
            })
    }

    /// Extracts the exact origin (scheme+host+effective_port) from the
    /// `url` field of a tool argument. Returns None when parsing fails or
    /// no URL field is present.
    fn extract_exact_origin(args: &str) -> Option<String> {
        let url_str = serde_json::from_str::<serde_json::Value>(args)
            .ok()?
            .get("url")?
            .as_str()?
            .to_string();
        let (scheme, rest) = url_str.split_once("://")?;
        let (authority, _path) = rest.split_once('/').unwrap_or((rest, ""));
        let (host, port) = authority.split_once(':').map_or(
            (authority, if scheme == "https" { 443u16 } else { 80u16 }),
            |(h, p)| {
                (
                    h,
                    p.parse()
                        .unwrap_or(if scheme == "https" { 443 } else { 80 }),
                )
            },
        );
        Some(format!("{scheme}://{host}:{port}"))
    }

    /// True for tools that mutate browser/page state and therefore need a
    /// tab lease before dispatch.
    fn needs_tab_lease(tool_name: &str) -> bool {
        matches!(
            tool_name,
            "browser_navigate"
                | "browser_click"
                | "browser_type"
                | "browser_select"
                | "browser_scroll"
                | "browser_key_press"
                | "browser_hover"
                | "input.click"
                | "input.type"
                | "input.select"
                | "input.scroll"
                | "input.key_press"
                | "input.hover"
                | "input.locator_click"
                | "input.locator_type"
                | "locator_click"
                | "locator_type"
                | "browser_locator_click"
                | "browser_locator_type"
        )
    }

    /// Resolves a leaseable tab id: the caller's explicit `tab_id`, the
    /// tab this turn opened, or the browser's active targetable tab.
    async fn resolve_leaseable_tab_id(
        bridge: &Arc<dyn BrowserToolBridge>,
        args: &str,
        remembered: &Arc<Mutex<Option<i64>>>,
    ) -> Result<Option<i64>, swiftide_core::chat_completion::errors::ToolError> {
        if let Some(tab_id) = serde_json::from_str::<serde_json::Value>(args)
            .ok()
            .as_ref()
            .and_then(|v| v.get("tab_id"))
            .and_then(serde_json::Value::as_i64)
            .filter(|id| *id != 0)
        {
            return Ok(Some(tab_id));
        }
        if let Some(tab_id) = *remembered.lock_unpoison() {
            return Ok(Some(tab_id));
        }
        let listing = Self::execute_bridge(Arc::clone(bridge), "browser_tab_list", "{}").await?;
        let tabs = serde_json::from_str::<serde_json::Value>(&listing)
            .ok()
            .and_then(|v| v.get("tabs").cloned())
            .and_then(|v| v.as_array().cloned())
            .unwrap_or_default();
        let pick = |require_active: bool| {
            tabs.iter().find_map(|tab| {
                let targetable = tab
                    .get("targetable")
                    .and_then(serde_json::Value::as_bool)
                    .unwrap_or(false);
                let active = tab
                    .get("is_active")
                    .and_then(serde_json::Value::as_bool)
                    .unwrap_or(false);
                let id = tab.get("id").and_then(serde_json::Value::as_i64)?;
                (targetable && id != 0 && (!require_active || active)).then_some(id)
            })
        };
        let tab_id = pick(true).or_else(|| pick(false));
        if let Some(tab_id) = tab_id {
            *remembered.lock_unpoison() = Some(tab_id);
        }
        Ok(tab_id)
    }

    /// Stores the tab id from a tab-opening result so later mutations can
    /// lease it.
    fn remember_opened_tab(&self, json: &str) {
        if let Some(tab_id) = serde_json::from_str::<serde_json::Value>(json)
            .ok()
            .as_ref()
            .and_then(|v| v.get("tab").or_else(|| v.get("id")).or(Some(v)))
            .and_then(serde_json::Value::as_i64)
            .filter(|id| *id != 0)
        {
            *self.context.opened_tab_id.lock_unpoison() = Some(tab_id);
        }
    }

    fn is_navigation_tool(name: &str) -> bool {
        matches!(name, "browser_navigate" | "browser_tab_new")
    }

    /// Wait for a just-triggered navigation to actually commit and render
    /// before returning, so the model's next read observes the loaded page
    /// instead of a blank pre-load document. Bounded so a slow page cannot hang.
    async fn settle_after_navigation(bridge: Arc<dyn BrowserToolBridge>) {
        for _ in 0..16 {
            let probe =
                Self::execute_bridge(Arc::clone(&bridge), "browser_page_content", "{}").await;
            if let Ok(json) = probe {
                if let Ok(v) = serde_json::from_str::<serde_json::Value>(&json) {
                    let url_ready = v
                        .get("url")
                        .and_then(|u| u.as_str())
                        .map(|s| !s.is_empty())
                        .unwrap_or(false);
                    let text_len = v
                        .get("text")
                        .and_then(|t| t.as_str())
                        .map(str::len)
                        .unwrap_or(0);
                    if url_ready && text_len > 64 {
                        return;
                    }
                }
            }
            tokio::time::sleep(std::time::Duration::from_millis(400)).await;
        }
    }

    /// Render a failed browser tool call as a structured observation the model
    /// can read and self-correct from, instead of aborting the whole run.
    fn error_observation(tool: &str, error: &crate::BrowserToolBridgeError) -> String {
        serde_json::json!({
            "error": {
                "code": error.code,
                "message": error.message,
                "retryable": error.retryable,
            },
            "tool": tool,
            "hint": if error.retryable {
                "The browser transport failed temporarily. Retry the tool call."
            } else {
                "The browser rejected this tool call permanently. Do not retry it unchanged."
            }
        })
        .to_string()
    }

    /// Cap oversized browser results (page content, snapshots) before they enter
    /// the model context, so large pages cannot exhaust the context window.
    fn cap_browser_result(json: &mut String) {
        const MAX_RESULT_BYTES: usize = 24_000;
        if json.len() > MAX_RESULT_BYTES {
            truncate_at_char_boundary(json, MAX_RESULT_BYTES);
            json.push_str(
                "\n...[result truncated to fit context; refine the query or scroll/paginate]",
            );
        }
    }

    /// True when navigation should fall back to `browser_tab_new` because
    /// the browser has no eligible active tab. The CLI transport rewrites the
    /// browser's -32005 as "no eligible target tab", so both phrasings match.
    fn should_open_tab_instead(tool_name: &str, message: &str) -> bool {
        tool_name == "browser_navigate"
            && (message.contains("no eligible active tab")
                || message.contains("no eligible target tab"))
    }

    fn enforce_fallback_ladder(
        &self,

        args: &str,
    ) -> Result<(), swiftide_core::chat_completion::errors::ToolError> {
        let Some(ref fallback_ladder) = self.context.fallback_ladder else {
            return Ok(());
        };

        let request = browser_tool_fallback_request(&self.name, args, self.sensitivity);

        let policy = *self.context.approval_policy.lock_unpoison();

        let mut ladder = fallback_ladder.lock_unpoison();

        match ladder.decide(request) {
            Ok(decision) => {
                audit_fallback_decision(
                    &self.context.session_id,
                    &self.name,
                    self.sensitivity,
                    policy,
                    &decision,
                );

                Ok(())
            }

            Err(error) => {
                audit_fallback_decision(
                    &self.context.session_id,
                    &self.name,
                    self.sensitivity,
                    policy,
                    error.decision(),
                );

                Err(fallback_error(&error))
            }
        }
    }

    fn tool_definition(&self) -> ToolDefinition {
        let sensitive = self.sensitivity == ToolSensitivity::Sensitive;

        ToolDefinition {
            name: self.name.to_string(),

            description: self.description.to_string(),

            parameters_schema: self.parameters_schema.clone(),

            provenance: ToolProvenance::BuiltinBrowser,

            sensitive,

            permission: self.permission,
        }
    }
}

struct BrowserActionToolMetadata {
    description: &'static str,

    parameters_schema: serde_json::Value,
}

fn mail_read_tool_metadata(tool_name: &str) -> Option<BrowserActionToolMetadata> {
    match tool_name {
        "mail_list_accounts" => Some(BrowserActionToolMetadata {
            description: "List mail accounts available to the mail read broker.",

            parameters_schema: serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {},

                "required": []

            }),
        }),

        "mail_list_folders" => Some(BrowserActionToolMetadata {
            description: "List folders for a mail account. Args: account_id (string).",

            parameters_schema: serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "account_id": {"type": "string", "description": "Mail account id."}

                },

                "required": ["account_id"]

            }),
        }),

        "mail_list_emails" => Some(BrowserActionToolMetadata {
            description: "List emails in a folder. Args: account_id (string), folder_id (string), limit (int, optional, default 50), offset (int, optional, default 0).",

            parameters_schema: serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "account_id": {"type": "string", "description": "Mail account id."},

                    "folder_id": {"type": "string", "description": "Mail folder id."},

                    "limit": {"type": "integer", "description": "Maximum emails to return; default 50."},

                    "offset": {"type": "integer", "description": "Pagination offset; default 0."}

                },

                "required": ["account_id", "folder_id"]

            }),
        }),

        "mail_get_email" => Some(BrowserActionToolMetadata {
            description: "Get a single email by id. Args: email_id (string).",

            parameters_schema: serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "email_id": {"type": "string", "description": "Email id."}

                },

                "required": ["email_id"]

            }),
        }),

        "mail_search_emails" => Some(BrowserActionToolMetadata {
            description: "Search emails. Args: query (string), account_id (string, optional), folder_id (string, optional), limit (int, optional, default 50), offset (int, optional, default 0).",

            parameters_schema: serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "query_json": {"type": "string", "description": "JSON search query accepted by the Mail search broker."}

                },

                "required": ["query_json"]

            }),
        }),

        "mail_list_thread" => Some(BrowserActionToolMetadata {
            description: "List a mail thread. Args: account_id (string), message_id (string).",

            parameters_schema: serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "account_id": {"type": "string", "description": "Mail account id."},

                    "message_id": {"type": "string", "description": "RFC message id for the thread."}

                },

                "required": ["account_id", "message_id"]

            }),
        }),

        _ => None,
    }
}

fn mail_write_tool_metadata(tool_name: &str) -> Option<BrowserActionToolMetadata> {
    let attachments = serde_json::json!({
        "type": "array",
        "description": "Optional attachments. Size limits are enforced by the mail provider; oversized attachments are rejected on send.",
        "items": {
            "type": "object",
            "additionalProperties": false,
            "properties": {
                "filename": {"type": "string", "description": "Attachment file name."},
                "mime_type": {"type": "string", "description": "Attachment MIME type, e.g. image/png."},
                "data_base64": {"type": "string", "description": "Attachment bytes, base64-encoded."}
            },
            "required": ["filename", "mime_type", "data_base64"]
        }
    });
    let mut compose_props = serde_json::json!({
        "account_id": {"type": "string", "description": "Sending Mail account id."},
        "to": {"type": "array", "items": {"type": "string"}, "description": "Primary recipients."},
        "subject": {"type": "string", "description": "Email subject."},
        "cc": {"type": "array", "items": {"type": "string"}, "description": "CC recipients."},
        "bcc": {"type": "array", "items": {"type": "string"}, "description": "BCC recipients."},
        "body_text": {"type": "string", "description": "Plain-text body."},
        "body_html": {"type": "string", "description": "HTML body."},
        "in_reply_to": {"type": "string", "description": "RFC message id this replies to."},
        "references": {"type": "string", "description": "RFC references header value."},
        "attachments": attachments
    });
    match tool_name {
        "mail_send" | "mail_save_draft" => {
            let description: &'static str = if tool_name == "mail_send" {
                "Send an email through Maho Mail. Requires explicit user approval; sending honors the agent approval policy. Args: account_id (string), to (string array), subject (string), plus optional cc, bcc, body_text, body_html, in_reply_to, references, attachments."
            } else {
                "Save a new email draft in Maho Mail. Requires explicit user approval. Args: account_id (string), to (string array), subject (string), plus optional cc, bcc, body_text, body_html, in_reply_to, references, attachments."
            };
            Some(BrowserActionToolMetadata {
                description,
                parameters_schema: serde_json::json!({
                    "type": "object",
                    "additionalProperties": false,
                    "properties": compose_props,
                    "required": ["account_id", "to", "subject"]
                }),
            })
        }
        "mail_update_draft" => {
            compose_props["draft_id"] =
                serde_json::json!({"type": "string", "description": "Draft id to update."});
            Some(BrowserActionToolMetadata {
                description: "Update an existing Maho Mail draft by id. Requires explicit user approval. Args: draft_id (string), account_id (string), to (string array), subject (string), plus optional cc, bcc, body_text, body_html, in_reply_to, references, attachments.",
                parameters_schema: serde_json::json!({
                    "type": "object",
                    "additionalProperties": false,
                    "properties": compose_props,
                    "required": ["draft_id", "account_id", "to", "subject"]
                }),
            })
        }
        _ => None,
    }
}

fn tool_sensitivity_from_browser_contract(sensitivity: BrowserToolSensitivity) -> ToolSensitivity {
    match sensitivity {
        BrowserToolSensitivity::Low => ToolSensitivity::ReadOnly,
        BrowserToolSensitivity::Sensitive => ToolSensitivity::Sensitive,
    }
}

fn browser_action_tool_metadata(tool_name: &str) -> BrowserActionToolMetadata {
    match tool_name {
        "browser_navigate" => BrowserActionToolMetadata {
            description: "Navigate the active browser tab, or a supplied tab id, to a URL.",

            parameters_schema: serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "tab_id": {"type": "integer", "description": "Optional tab ID. Defaults to the active tab."},

                    "url": {"type": "string", "description": "URL to navigate to."}

                },

                "required": ["url"]

            }),
        },

        "browser_click" => BrowserActionToolMetadata {
            description: "Click an element by its @ref label from the accessibility snapshot.",

            parameters_schema: serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "ref": {"type": "integer", "description": "Accessibility @ref of the element to click."},

                    "tab_id": {"type": "integer", "description": "Optional tab ID. Defaults to the active tab."}

                },

                "required": ["ref"]

            }),
        },

        "browser_type" => BrowserActionToolMetadata {
            description: "Type text into an element by its @ref label.",

            parameters_schema: serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "ref": {"type": "integer", "description": "Accessibility @ref of the input element."},

                    "text": {"type": "string", "description": "Text to type into the element."},

                    "tab_id": {"type": "integer", "description": "Optional tab ID. Defaults to the active tab."}

                },

                "required": ["ref", "text"]

            }),
        },

        "browser_select" => BrowserActionToolMetadata {
            description: "Select a value on a <select> element by its @ref label.",

            parameters_schema: serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "ref": {"type": "integer", "description": "Accessibility @ref of the select element."},

                    "value": {"type": "string", "description": "Option value to select."},

                    "tab_id": {"type": "integer", "description": "Optional tab ID. Defaults to the active tab."}

                },

                "required": ["ref", "value"]

            }),
        },

        "browser_scroll" => BrowserActionToolMetadata {
            description: "Scroll the main viewport or an element.",

            parameters_schema: serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "direction": {"type": "string", "enum": ["up", "down", "left", "right"], "description": "Scroll direction."},

                    "pixels": {"type": "integer", "minimum": 1, "description": "Pixels to scroll. Defaults to 300 browser-side."},

                    "ref": {"type": "integer", "description": "Optional accessibility @ref to anchor the scroll origin."},

                    "tab_id": {"type": "integer", "description": "Optional tab ID. Defaults to the active tab."}

                },

                "required": ["direction"]

            }),
        },

        "browser_hover" => BrowserActionToolMetadata {
            description: "Hover the mouse over an element by its @ref label.",

            parameters_schema: serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "ref": {"type": "integer", "description": "Accessibility @ref of the element to hover."},

                    "tab_id": {"type": "integer", "description": "Optional tab ID. Defaults to the active tab."}

                },

                "required": ["ref"]

            }),
        },

        "browser_key_press" => BrowserActionToolMetadata {
            description: "Press a keyboard key in the active browser tab, or a supplied tab id.",

            parameters_schema: serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "key": {"type": "string", "description": "Key name such as Enter, Tab, Escape, ArrowUp, F5, Space, or a single character."},

                    "modifiers": {"type": "array", "items": {"type": "string", "enum": ["shift", "ctrl", "alt", "meta"]}, "description": "Modifier keys."},

                    "tab_id": {"type": "integer", "description": "Optional tab ID. Defaults to the active tab."}

                },

                "required": ["key"]

            }),
        },

        _ => BrowserActionToolMetadata {
            description:
                "Invoke a flag-gated browser action tool from the canonical action contract.",

            parameters_schema: serde_json::json!({

                "type": "object",

                "additionalProperties": true,

                "properties": {},

                "required": []

            }),
        },
    }
}

fn browser_action_shim(
    contract: BrowserActionContract<'static>,

    bridge: Arc<dyn BrowserToolBridge>,

    context: BrowserToolShimContext,
) -> BrowserToolShim {
    let metadata = browser_action_tool_metadata(contract.tool_name);

    BrowserToolShim {
        capability_id: contract.tool_name.to_string(),

        name: contract.tool_name.to_string(),

        description: metadata.description.to_string(),

        parameters_schema: metadata.parameters_schema,

        sensitivity: tool_sensitivity_from_browser_contract(contract.sensitivity),

        permission: match contract.kind {
            BrowserToolKind::Read => ToolPermission::AutoApprove,
            BrowserToolKind::Action => ToolPermission::AlwaysAsk,
        },

        bridge,

        context,
    }
}

fn truncate_at_char_boundary(s: &mut String, max_bytes: usize) {
    if s.len() <= max_bytes {
        return;
    }

    let mut end = max_bytes;

    while end > 0 && !s.is_char_boundary(end) {
        end -= 1;
    }

    s.truncate(end);
}

const V2_TOOL_INSTRUCTIONS: &str = "default observation interactive; if a diff is returned, do NOT re-request a full snapshot; do not hunt numeric refs when locator works; full snapshot only for ambiguity/debug escalation; use wait/actionability, never fixed sleeps.";

fn is_v2_locator_action(name: &str) -> bool {
    matches!(
        name,
        "input.locator_click"
            | "input.locator_type"
            | "locator_click"
            | "locator_type"
            | "browser_locator_click"
            | "browser_locator_type"
    )
}

fn is_v2_snapshot(name: &str) -> bool {
    matches!(
        name,
        "page.accessibility_snapshot_v2"
            | "page_accessibility_snapshot_v2"
            | "browser_accessibility_snapshot_v2"
    )
}

fn is_v2_tool(name: &str) -> bool {
    is_v2_locator_action(name) || is_v2_snapshot(name)
}

fn is_v1_browser_primitive(name: &str) -> bool {
    matches!(
        name,
        "browser_click"
            | "browser_type"
            | "browser_select"
            | "browser_scroll"
            | "browser_hover"
            | "browser_key_press"
            | "browser_accessibility_snapshot"
            | "page.accessibility_snapshot"
            | "input.click"
            | "input.type"
            | "input.select"
            | "input.scroll"
            | "input.hover"
            | "input.key_press"
    )
}

fn browser_tool_rank(name: &str, v2_advertised: bool) -> usize {
    if !v2_advertised {
        return 0;
    }
    if is_v2_locator_action(name) {
        0
    } else if is_v2_snapshot(name) {
        1
    } else if !is_v1_browser_primitive(name) {
        2
    } else {
        3
    }
}

fn v2_tool_description(tool_name: &str, original_description: &str) -> String {
    if !is_v2_tool(tool_name) {
        return original_description.to_string();
    }
    if original_description.contains("if a diff is returned, do NOT re-request a full snapshot") {
        return original_description.to_string();
    }
    let base = match tool_name {
        "page.accessibility_snapshot_v2"
        | "page_accessibility_snapshot_v2"
        | "browser_accessibility_snapshot_v2" => {
            "Get a compact, scoped, or differential accessibility snapshot."
        }
        "input.locator_click" | "locator_click" | "browser_locator_click" => {
            "Click an element matched by locator (ref, css, or role+name) with automatic actionability wait and return observation diff."
        }
        "input.locator_type" | "locator_type" | "browser_locator_type" => {
            "Type text into an element matched by locator (ref, css, or role+name) with automatic actionability wait and return observation diff."
        }
        _ => original_description,
    };
    if base.is_empty() || (base == original_description && !original_description.is_empty()) {
        format!("{original_description} {V2_TOOL_INSTRUCTIONS}")
    } else {
        format!("{base} {V2_TOOL_INSTRUCTIONS}")
    }
}

fn browser_tool_shims(
    bridge: Arc<dyn BrowserToolBridge>,
    descriptors: Vec<crate::BrowserToolDescriptor>,
    action_state: BrowserActionToolsState,
    context: BrowserToolShimContext,
) -> Vec<BrowserToolShim> {
    let mut seen_capability_ids = std::collections::HashSet::new();
    let mut seen_names = std::collections::HashSet::new();

    let v2_advertised = descriptors
        .iter()
        .any(|d| is_v2_tool(&d.name) || is_v2_tool(&d.capability_id));

    let mut shims: Vec<BrowserToolShim> = descriptors
        .into_iter()
        .filter_map(|descriptor| {
            let schema_valid = descriptor.input_schema.as_object().is_some_and(|schema| {
                schema.get("type").and_then(serde_json::Value::as_str) == Some("object")
            });
            let identity_valid = !descriptor.capability_id.trim().is_empty()
                && !descriptor.name.trim().is_empty()
                && descriptor.schema_version > 0;
            let unique = seen_capability_ids.insert(descriptor.capability_id.clone())
                && seen_names.insert(descriptor.name.clone());
            let action_allowed = action_state.is_enabled()
                || !matches!(
                    descriptor.name.as_str(),
                    "browser_navigate"
                        | "browser_click"
                        | "browser_type"
                        | "browser_select"
                        | "browser_scroll"
                        | "browser_hover"
                        | "browser_key_press"
                        | "browser_accessibility_snapshot"
                        | "page.accessibility_snapshot"
                        | "input.click"
                        | "input.type"
                        | "input.select"
                        | "input.scroll"
                        | "input.hover"
                        | "input.key_press"
                        | "input.locator_click"
                        | "input.locator_type"
                        | "locator_click"
                        | "locator_type"
                        | "browser_locator_click"
                        | "browser_locator_type"
                        | "page.accessibility_snapshot_v2"
                        | "page_accessibility_snapshot_v2"
                        | "browser_accessibility_snapshot_v2"
                );
            if !schema_valid || !identity_valid || !unique || !action_allowed {
                tracing::warn!(
                    target: "maho_agent::swiftide_backend",
                    capability_id = %descriptor.capability_id,
                    tool = %descriptor.name,
                    schema_valid,
                    identity_valid,
                    unique,
                    action_allowed,
                    "browser tool descriptor rejected"
                );
                return None;
            }
            let description = v2_tool_description(&descriptor.name, &descriptor.description);
            Some(BrowserToolShim {
                capability_id: descriptor.capability_id,
                name: descriptor.name,
                description,
                parameters_schema: descriptor.input_schema,
                sensitivity: if descriptor.policy.sensitive {
                    ToolSensitivity::Sensitive
                } else {
                    ToolSensitivity::ReadOnly
                },
                permission: descriptor.policy.permission,
                bridge: Arc::clone(&bridge),
                context: context.clone(),
            })
        })
        .collect();

    if v2_advertised {
        shims.sort_by_key(|s| browser_tool_rank(&s.name, true));
    }

    shims
}

/// Plan row 11: the registered dispatch surface for composite (batch) page
/// execution. The model emits one `composite_execute` call; the runner
/// executes steps sequentially through `BridgeStepExecutor`, so every step
/// crosses the capability broker individually, and progress streams over the
/// row-1 Status event plumbing. Admission is gated by the session's
/// runtime_config bound `CompositeExecutionConfig` (fail-closed default).
#[derive(Clone)]
pub struct CompositeExecuteTool {
    bridge: Option<Arc<dyn BrowserToolBridge>>,
    config: Arc<Mutex<crate::composite::CompositeExecutionConfig>>,
    on_event: Option<std::sync::Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static>>,
    clock: Arc<dyn crate::event_wait::Clock>,
    turn_start_millis: u64,
}

#[async_trait]
impl Tool for CompositeExecuteTool {
    async fn invoke(
        &self,
        _agent_context: &dyn AgentContext,
        tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        let request: crate::composite::CompositeRequest =
            serde_json::from_str(tool_call.args().unwrap_or("{}")).map_err(|error| {
                swiftide_core::chat_completion::errors::ToolError::unknown(anyhow::anyhow!(
                    "invalid composite_execute arguments: {error}"
                ))
            })?;

        let config = *self.config.lock_unpoison();
        let runner = crate::composite::CompositeRunner::new(config);
        let executor = crate::composite::BridgeStepExecutor::new(self.bridge.clone());

        let on_event = self.on_event.clone();
        let clock = Arc::clone(&self.clock);
        let turn_start_millis = self.turn_start_millis;
        let result = runner
            .execute_batch(&request, &executor, |progress| {
                if let Some(ref callback) = on_event {
                    let elapsed_secs = clock.now_millis().saturating_sub(turn_start_millis) / 1000;
                    let message = serde_json::to_string(&progress).unwrap_or_default();
                    callback(AgentStreamEvent::Status {
                        elapsed_secs,
                        message,
                    });
                }
            })
            .await;

        match result {
            Ok(batch) => {
                let output = serde_json::to_string(&batch).unwrap_or_default();
                Ok(ToolOutput::Text(output))
            }
            Err(error) => Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!(error.to_string()),
            )),
        }
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("composite_execute")
    }

    fn tool_spec(&self) -> ToolSpec {
        let definition = crate::tool_registry::composite_execute_definition();
        ToolSpec {
            name: definition.name,
            description: definition.description,
            parameters_schema: serde_json::from_value(definition.parameters_schema).unwrap_or(None),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]

pub enum LlmProviderKind {
    OpenAi,

    Anthropic,
}

fn normalize_provider(p: &str) -> &str {
    match p {
        "claude" => "anthropic",

        other => other,
    }
}

/// Probe the secure-storage callback for a usable BYOK key and build the

/// matching LLM client.

///

/// `preferred` (from `maho run --provider`) is probed FIRST so an Anthropic

/// selection is never mislabeled as OpenAI. The previous behavior hard-coded

/// the probe order to `openai` first and used the *probe argument* (not the key

/// the callback actually returned) as the provider label, so `--provider

/// anthropic` built an OpenAI client pointed at the OpenAI base URL while

/// carrying an Anthropic key.

pub fn resolve_byok_client<F>(
    preferred: Option<&str>,

    secure_cb: &F,

    resolved_model: &str,
) -> Result<
    (
        Box<dyn swiftide_core::chat_completion::ChatCompletion>,
        LlmProviderKind,
    ),
    AgentError,
>
where
    F: Fn(&str) -> Result<SecureKeyBundle, AgentError> + ?Sized,
{
    // An explicitly selected provider is probed alone: never silently fall
    // back to a different provider's credential (that would send data to a
    // provider the caller did not choose). Fall-through across providers is
    // only allowed when no provider was selected.
    let order: Vec<&str> = match preferred.map(normalize_provider) {
        Some(selected) => vec![selected],

        None => vec!["openai", "anthropic"],
    };

    let mut found: Option<(&str, SecureKeyBundle)> = Option::None;

    let mut last_err: Option<AgentError> = Option::None;

    for provider in order {
        match secure_cb(provider) {
            Ok(bundle) => {
                found = Some((provider, bundle));

                break;
            }

            Err(e) => last_err = Some(e),
        }
    }

    let (provider, bundle) = match found {
        Some(pair) => pair,

        Option::None => {
            return Err(last_err.unwrap_or_else(|| {
                AgentError::Credential(CredentialError::SecureStoreUnavailable)
            }));
        }
    };

    build_byok_client(provider, bundle, resolved_model)
}

fn build_byok_client(
    provider: &str,

    bundle: SecureKeyBundle,

    resolved_model: &str,
) -> Result<
    (
        Box<dyn swiftide_core::chat_completion::ChatCompletion>,
        LlmProviderKind,
    ),
    AgentError,
> {
    let api_key = bundle.key;

    let base_url_override = bundle.base_url;

    let model_override = bundle.model;

    if normalize_provider(provider) == "anthropic" {
        let default_model = model_override.filter(|s| !s.is_empty()).unwrap_or_else(|| {
            if resolved_model.is_empty() {
                "claude-3-5-sonnet".to_string()
            } else {
                resolved_model.to_string()
            }
        });

        let client = async_anthropic::Client::from_api_key(api_key.secret().clone());

        let swiftide_anthropic = swiftide_integrations::anthropic::Anthropic::builder()
            .default_prompt_model(default_model)
            .client(client)
            .build()
            .map_err(|e| {
                AgentError::ExecutionError(format!("Failed to build Anthropic client: {e}"))
            })?;

        Ok((Box::new(swiftide_anthropic), LlmProviderKind::Anthropic))
    } else {
        let api_base = base_url_override
            .filter(|s| !s.is_empty())
            .unwrap_or_else(|| OPENAI_DEFAULT_API_BASE.to_string());

        let default_model = model_override.filter(|s| !s.is_empty()).unwrap_or_else(|| {
            if resolved_model.is_empty() {
                "gpt-4o".to_string()
            } else {
                resolved_model.to_string()
            }
        });

        let secret_key = secrecy::SecretString::new(api_key.as_str().to_string().into_boxed_str());

        let client = crate::maho_reasoning_openai::MahoReasoningOpenAI::new(
            secret_key,
            api_base,
            default_model,
        );

        Ok((Box::new(client), LlmProviderKind::OpenAi))
    }
}

pub struct SwiftideBackend {
    storage: Arc<dyn AgentStorage>,

    permission_callback: Arc<Mutex<Option<PermissionCallback>>>,

    active_sessions: Arc<Mutex<std::collections::HashMap<String, tokio::task::AbortHandle>>>,

    llm_override: Arc<Mutex<Option<Box<dyn swiftide_core::chat_completion::ChatCompletion>>>>,

    secure_storage_callback: Arc<
        Mutex<
            Option<
                Box<dyn Fn(&str) -> Result<SecureKeyBundle, AgentError> + Send + Sync + 'static>,
            >,
        >,
    >,

    browser_tool_bridge: Arc<Mutex<Option<Arc<dyn BrowserToolBridge>>>>,

    workspace_root: std::path::PathBuf,

    artifact_root: Arc<Mutex<Option<std::path::PathBuf>>>,

    allow_insecure_key_storage: bool,

    system_prompt: Arc<Mutex<String>>,

    mcp_client_cache:
        Arc<Mutex<std::collections::HashMap<String, Arc<crate::mcp_client::McpClient>>>>,

    active_space_id: Arc<Mutex<Option<String>>>,

    approval_policy: Arc<Mutex<ApprovalPolicy>>,

    browser_action_tools_state: Arc<Mutex<BrowserActionToolsState>>,

    composite_config: Arc<Mutex<crate::composite::CompositeExecutionConfig>>,

    mail_authorization_state: Arc<Mutex<crate::MailAuthorizationState>>,

    preferred_provider: Arc<Mutex<Option<String>>>,

    allowed_tools: Arc<Mutex<Option<Vec<String>>>>,

    turn_controllers:
        Arc<Mutex<std::collections::HashMap<String, crate::turn_control::SessionTurnController>>>,

    wait_registry: Arc<Mutex<crate::event_wait::WaitRegistry>>,

    wake_notify: Arc<tokio::sync::Notify>,

    notification_activation: Arc<Mutex<crate::event_wait::NotificationActivation>>,

    channel_registry: Arc<Mutex<Option<Arc<tokio::sync::Mutex<crate::channels::ChannelRegistry>>>>>,

    search_entitlement: Arc<Mutex<crate::web_search::SearchEntitlement>>,

    routing_decisions: Arc<Mutex<Vec<crate::model_routing::RoutingDecision>>>,

    retry_fallback_reasons: Arc<Mutex<Vec<crate::retry_policy::FallbackSwitchReason>>>,

    available_models: Arc<Mutex<Vec<String>>>,

    retry_policy: Arc<Mutex<crate::retry_policy::RetryPolicy>>,

    run_journal: Arc<Mutex<crate::run_journal::RunJournal>>,

    interaction_broker: Arc<Mutex<crate::interaction::InteractionBroker>>,

    // Wave 1D (D7): session runtime tier + fs whitelist roots for the kernel
    // tool dispatch gate. None/empty defaults keep the gate inert on every
    // surface that does not explicitly bind a tier.
    runtime_tier: Arc<Mutex<Option<String>>>,

    fs_whitelist_roots: Arc<Mutex<Vec<String>>>,

    clock: Arc<dyn crate::event_wait::Clock>,
}

impl Clone for SwiftideBackend {
    fn clone(&self) -> Self {
        Self {
            storage: Arc::clone(&self.storage),

            permission_callback: Arc::clone(&self.permission_callback),

            active_sessions: Arc::clone(&self.active_sessions),

            llm_override: Arc::clone(&self.llm_override),

            secure_storage_callback: Arc::clone(&self.secure_storage_callback),

            browser_tool_bridge: Arc::clone(&self.browser_tool_bridge),

            workspace_root: self.workspace_root.clone(),

            artifact_root: Arc::clone(&self.artifact_root),

            allow_insecure_key_storage: self.allow_insecure_key_storage,

            system_prompt: Arc::clone(&self.system_prompt),

            mcp_client_cache: Arc::clone(&self.mcp_client_cache),

            active_space_id: Arc::clone(&self.active_space_id),

            approval_policy: Arc::clone(&self.approval_policy),

            browser_action_tools_state: Arc::clone(&self.browser_action_tools_state),

            composite_config: Arc::clone(&self.composite_config),

            mail_authorization_state: Arc::clone(&self.mail_authorization_state),

            preferred_provider: Arc::clone(&self.preferred_provider),

            allowed_tools: Arc::clone(&self.allowed_tools),

            turn_controllers: Arc::clone(&self.turn_controllers),

            wait_registry: Arc::clone(&self.wait_registry),

            wake_notify: Arc::clone(&self.wake_notify),

            notification_activation: Arc::clone(&self.notification_activation),

            channel_registry: Arc::clone(&self.channel_registry),

            search_entitlement: Arc::clone(&self.search_entitlement),

            routing_decisions: Arc::clone(&self.routing_decisions),

            retry_fallback_reasons: Arc::clone(&self.retry_fallback_reasons),

            available_models: Arc::clone(&self.available_models),

            retry_policy: Arc::clone(&self.retry_policy),

            run_journal: Arc::clone(&self.run_journal),

            interaction_broker: Arc::clone(&self.interaction_broker),

            runtime_tier: Arc::clone(&self.runtime_tier),

            fs_whitelist_roots: Arc::clone(&self.fs_whitelist_roots),

            clock: Arc::clone(&self.clock),
        }
    }
}

impl SwiftideBackend {
    pub fn new(
        storage: Arc<dyn AgentStorage>,

        permission_callback: Option<PermissionCallback>,

        workspace_root: std::path::PathBuf,

        allow_insecure_key_storage: bool,
    ) -> Self {
        crate::init_audit_logging();

        Self {
            storage,

            permission_callback: Arc::new(Mutex::new(permission_callback)),

            active_sessions: Arc::new(Mutex::new(std::collections::HashMap::new())),

            llm_override: Arc::new(Mutex::new(None)),

            secure_storage_callback: Arc::new(Mutex::new(None)),

            browser_tool_bridge: Arc::new(Mutex::new(None)),

            workspace_root,

            artifact_root: Arc::new(Mutex::new(None)),

            mcp_client_cache: Arc::new(Mutex::new(std::collections::HashMap::new())),

            allow_insecure_key_storage,

            system_prompt: Arc::new(Mutex::new(String::new())),

            active_space_id: Arc::new(Mutex::new(None)),

            approval_policy: Arc::new(Mutex::new(ApprovalPolicy::default())),

            browser_action_tools_state: Arc::new(Mutex::new(BrowserActionToolsState::default())),

            composite_config: Arc::new(Mutex::new(
                crate::composite::CompositeExecutionConfig::default(),
            )),

            mail_authorization_state: Arc::new(
                Mutex::new(crate::MailAuthorizationState::default()),
            ),

            preferred_provider: Arc::new(Mutex::new(None)),

            allowed_tools: Arc::new(Mutex::new(None)),

            turn_controllers: Arc::new(Mutex::new(std::collections::HashMap::new())),

            wait_registry: Arc::new(Mutex::new(crate::event_wait::WaitRegistry::new())),

            wake_notify: Arc::new(tokio::sync::Notify::new()),

            notification_activation: Arc::new(Mutex::new(
                crate::event_wait::NotificationActivation::granted(),
            )),

            channel_registry: Arc::new(Mutex::new(None)),

            search_entitlement: Arc::new(Mutex::new(crate::web_search::SearchEntitlement::free())),

            routing_decisions: Arc::new(Mutex::new(Vec::new())),

            retry_fallback_reasons: Arc::new(Mutex::new(Vec::new())),

            available_models: Arc::new(Mutex::new(vec![
                "google/gemini-3-flash-lite:free".to_string(),
                "anthropic/claude-3-7-sonnet".to_string(),
                "gpt-4o".to_string(),
                "claude-3-5-sonnet".to_string(),
            ])),

            retry_policy: Arc::new(Mutex::new(crate::retry_policy::RetryPolicy::default())),

            run_journal: Arc::new(Mutex::new(crate::run_journal::RunJournal::new())),

            interaction_broker: Arc::new(Mutex::new(crate::interaction::InteractionBroker::new())),

            runtime_tier: Arc::new(Mutex::new(None)),

            fs_whitelist_roots: Arc::new(Mutex::new(Vec::new())),

            clock: Arc::new(crate::event_wait::SystemClock),
        }
    }

    pub fn with_clock(mut self, clock: Arc<dyn crate::event_wait::Clock>) -> Self {
        self.clock = clock;
        self
    }

    pub fn set_clock(&mut self, clock: Arc<dyn crate::event_wait::Clock>) {
        self.clock = clock;
    }

    pub fn has_screenshot_capability(&self, platform_tools: &[Box<dyn Tool>]) -> bool {
        if platform_tools
            .iter()
            .any(|t| t.name().contains("screenshot"))
        {
            return true;
        }
        if self.browser_tool_bridge.lock_unpoison().is_some() {
            return true;
        }
        false
    }

    pub fn submit_follow_up(
        &self,
        session_id: &str,
        message: &str,
        behavior: crate::turn_control::FollowUpBehavior,
    ) -> Result<crate::turn_control::FollowUpSubmissionResult, crate::turn_control::TurnControlError>
    {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();
        let mut controllers = self.turn_controllers.lock_unpoison();
        let controller = controllers
            .entry(session_id.to_string())
            .or_insert_with(|| crate::turn_control::SessionTurnController::new(session_id));

        let res = controller.submit_follow_up(message, behavior, now)?;
        if behavior == crate::turn_control::FollowUpBehavior::Interrupt {
            let active = self.active_sessions.lock_unpoison();
            if let Some(handle) = active.get(session_id) {
                handle.abort();
            }
        }
        Ok(res)
    }

    pub fn wake_wait_event(
        &self,
        event: &crate::event_wait::WakeEvent,
    ) -> Vec<crate::event_wait::ResumeToken> {
        let tokens = self.wait_registry.lock_unpoison().wake(event);
        self.wake_notify.notify_waiters();
        tokens
    }

    pub fn set_search_entitlement(&self, entitlement: crate::web_search::SearchEntitlement) {
        *self.search_entitlement.lock_unpoison() = entitlement;
    }

    pub fn set_notification_activation(
        &self,
        activation: crate::event_wait::NotificationActivation,
    ) {
        *self.notification_activation.lock_unpoison() = activation;
    }

    pub fn set_channel_registry(
        &self,
        registry: Arc<tokio::sync::Mutex<crate::channels::ChannelRegistry>>,
    ) {
        *self.channel_registry.lock_unpoison() = Some(registry);
    }

    pub async fn handle_channel_inbound(
        &self,
        event: crate::channels::ChannelEvent,
    ) -> Result<Option<crate::turn_control::FollowUpSubmissionResult>, AgentError> {
        let channel_reg = self.channel_registry.lock_unpoison().clone();
        if let Some(registry) = channel_reg {
            let mut reg = registry.lock().await;
            let outcome = reg.process_inbound_event(event);
            if let crate::channels::InboundProcessOutcome::Triggered(trigger) = outcome {
                let session_id = format!("channel-{}", trigger.channel_id.as_str());
                let sub_res = self
                    .submit_follow_up(
                        &session_id,
                        &trigger.message_text,
                        crate::turn_control::FollowUpBehavior::Queue,
                    )
                    .map_err(|e| AgentError::ExecutionError(e.to_string()))?;
                return Ok(Some(sub_res));
            }
        }
        Ok(None)
    }

    pub fn routing_decisions(&self) -> Vec<crate::model_routing::RoutingDecision> {
        self.routing_decisions.lock_unpoison().clone()
    }

    pub fn last_routing_decision(&self) -> Option<crate::model_routing::RoutingDecision> {
        self.routing_decisions.lock_unpoison().last().cloned()
    }

    pub fn set_available_models(&self, models: Vec<String>) {
        *self.available_models.lock_unpoison() = models;
    }

    pub fn retry_fallback_reasons(&self) -> Vec<crate::retry_policy::FallbackSwitchReason> {
        self.retry_fallback_reasons.lock_unpoison().clone()
    }

    pub fn run_journal(&self) -> Arc<Mutex<crate::run_journal::RunJournal>> {
        Arc::clone(&self.run_journal)
    }

    pub fn with_journal(mut self, journal: Arc<Mutex<crate::run_journal::RunJournal>>) -> Self {
        self.run_journal = journal;
        self
    }

    pub fn with_journal_store(self, store: Arc<dyn crate::run_journal::JournalStore>) -> Self {
        {
            let mut j = self.run_journal.lock_unpoison();
            j.set_store(store);
            j.set_persist_on_append(true);
        }
        self
    }

    pub fn with_journal_path(self, path: impl Into<std::path::PathBuf>) -> Self {
        let store = Arc::new(crate::run_journal::FsJsonStore::new(path));
        self.with_journal_store(store)
    }

    pub fn set_journal_store(&self, store: Arc<dyn crate::run_journal::JournalStore>) {
        let mut j = self.run_journal.lock_unpoison();
        j.set_store(store);
        j.set_persist_on_append(true);
    }

    pub fn set_journal_path(&self, path: impl Into<std::path::PathBuf>) {
        let path_buf = path.into();
        let journal = crate::run_journal::RunJournal::from_fs_path_or_new(path_buf);
        let mut j = self.run_journal.lock_unpoison();
        *j = journal;
    }

    pub fn attach_session_journal(
        &self,
        session_id: impl std::fmt::Display,
        storage_path: &std::path::Path,
    ) {
        let path = storage_path
            .join(".maho")
            .join("journals")
            .join(format!("{session_id}.json"));
        self.set_journal_path(path);
    }

    pub fn preferred_provider(&self) -> Arc<Mutex<Option<String>>> {
        Arc::clone(&self.preferred_provider)
    }

    pub fn latest_run_id(&self, session_id: &str) -> Option<crate::run_journal::AgentRunId> {
        Some(crate::run_journal::AgentRunId::new(session_id))
    }

    pub fn interaction_broker(&self) -> Arc<Mutex<crate::interaction::InteractionBroker>> {
        Arc::clone(&self.interaction_broker)
    }

    pub fn resolve_interaction(
        &self,
        req_id: &str,
        answer: crate::interaction::InteractionAnswer,
    ) -> Result<(), crate::interaction::InteractionError> {
        let mut broker = self.interaction_broker.lock_unpoison();
        broker.resolve(
            &crate::interaction::InteractionRequestId::new(req_id),
            answer,
        )
    }

    pub fn timeout_interaction(
        &self,
        req_id: &str,
    ) -> Result<(), crate::interaction::InteractionError> {
        let mut broker = self.interaction_broker.lock_unpoison();
        broker.timeout(&crate::interaction::InteractionRequestId::new(req_id))
    }

    pub fn cancel_interaction(
        &self,
        req_id: &str,
    ) -> Result<(), crate::interaction::InteractionError> {
        let mut broker = self.interaction_broker.lock_unpoison();
        broker.cancel(&crate::interaction::InteractionRequestId::new(req_id))
    }

    pub fn set_retry_policy(&self, policy: crate::retry_policy::RetryPolicy) {
        *self.retry_policy.lock_unpoison() = policy;
    }

    pub fn set_active_space_id(&self, space_id: Option<String>) {
        *self.active_space_id.lock_unpoison() = space_id;
    }

    pub fn set_artifact_root(&self, artifact_root: Option<std::path::PathBuf>) {
        *self.artifact_root.lock_unpoison() = artifact_root;
    }

    /// List a session's persisted artifacts (oldest first) via the agent
    /// storage index. Backs the FFI `maho_agent_list_artifacts`.
    pub async fn list_artifacts(
        &self,
        session_id: &str,
    ) -> Result<Vec<maho_types::artifact::ArtifactInfo>, AgentError> {
        self.storage.list_artifacts(session_id).await
    }

    /// Resolve an artifact id to its root-relative storage path within a
    /// session, or `None` if the id is unknown. Paths never cross the FFI as
    /// opaque ids do; the caller resolves against the configured artifact root.
    pub async fn artifact_storage_rel_path(
        &self,
        session_id: &str,
        artifact_id: &str,
    ) -> Result<Option<String>, AgentError> {
        let artifacts = self.storage.list_artifacts(session_id).await?;
        Ok(artifacts
            .into_iter()
            .find(|a| a.artifact_id == artifact_id)
            .map(|a| a.storage_rel_path))
    }

    pub fn set_allowed_tools(&self, tools: Option<Vec<String>>) {
        *self.allowed_tools.lock_unpoison() = tools;
    }

    pub fn set_approval_policy(&self, policy: String) {
        *self.approval_policy.lock_unpoison() = ApprovalPolicy::from_legacy_pref(&policy);
    }

    pub fn set_browser_action_tools_state(&self, state: BrowserActionToolsState) {
        *self.browser_action_tools_state.lock_unpoison() = state;
    }

    pub fn set_browser_action_tools_enabled(&self, enabled: bool) {
        self.set_browser_action_tools_state(BrowserActionToolsState::from_enabled(enabled));
    }

    /// Bind the composite execution gate to this session's runtime_config
    /// (plan row 11). The default is fail-closed: `enabled: false` refuses
    /// every batch before any executor is touched.
    pub fn set_composite_config(&self, config: crate::composite::CompositeExecutionConfig) {
        *self.composite_config.lock_unpoison() = config;
    }

    /// Read back the composite execution gate bound to this backend
    /// (Wave 3A): the CLI session-open path binds it once from the task
    /// runtime-config payload; `enabled: false` until then.
    pub fn composite_config(&self) -> crate::composite::CompositeExecutionConfig {
        *self.composite_config.lock_unpoison()
    }

    /// Wave 1D (D7): bind the session's runtime permission tier for the
    /// kernel tool dispatch gate. `None` leaves the gate inert; CLI task
    /// sessions bind their `--tier` value here.
    pub fn set_runtime_tier(&self, tier: Option<String>) {
        *self.runtime_tier.lock_unpoison() = tier;
    }

    pub fn runtime_tier(&self) -> Option<String> {
        self.runtime_tier.lock_unpoison().clone()
    }

    /// Wave 1D (D7/R-N2): provision the fs whitelist roots for the tier
    /// gate. Roots must come from the shared provisioning source
    /// (`permission::default_fs_whitelist_roots`), never an ad-hoc list.
    pub fn set_fs_whitelist_roots(&self, roots: Vec<String>) {
        *self.fs_whitelist_roots.lock_unpoison() = roots;
    }

    pub fn fs_whitelist_roots(&self) -> Vec<String> {
        self.fs_whitelist_roots.lock_unpoison().clone()
    }

    pub fn artifact_root(&self) -> Option<std::path::PathBuf> {
        self.artifact_root.lock_unpoison().clone()
    }

    async fn get_managed_auth_token(&self) -> Result<Option<crate::ZeroizedString>, AgentError> {
        let token = self.storage.get_setting("auth:access_token").await?;
        Ok(token.filter(|tok| !tok.as_str().trim().is_empty()))
    }

    pub fn set_mail_authorization_state(&self, state: crate::MailAuthorizationState) {
        *self.mail_authorization_state.lock_unpoison() = state;
    }

    fn browser_action_tools_state(&self) -> BrowserActionToolsState {
        *self.browser_action_tools_state.lock_unpoison()
    }

    pub fn set_preferred_provider(&self, provider: Option<String>) {
        *self.preferred_provider.lock_unpoison() = provider;
    }

    pub async fn run_prompt(&self, prompt: &str) -> Result<String, AgentError> {
        let session_id = format!("run-{}", uuid::Uuid::new_v4());
        self.run_prompt_in_session(&session_id, prompt, None).await
    }

    /// Like [`Self::run_turn`], but resolves to the final assistant text.
    /// Wave 3C (G9): `on_event` receives the live stream (tool-call
    /// started/finished, status ticks, artifacts, terminal proof/reason) so
    /// callers can surface progress; `None` keeps the historical silent
    /// behavior.
    pub async fn run_prompt_in_session(
        &self,
        session_id: &str,
        prompt: &str,
        on_event: Option<std::sync::Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static>>,
    ) -> Result<String, AgentError> {
        let denied_flag = Arc::new(Mutex::new(false));

        let message = MahoChatMessage::user(ChatContent::text(prompt.to_string()));

        let result = self
            .run_turn_internal(session_id, message, denied_flag, None, on_event)
            .await?;

        match result.content {
            ChatContent::Text(text) => Ok(text),

            _ => Err(AgentError::ExecutionError(
                "No assistant response generated".to_string(),
            )),
        }
    }

    async fn find_active_workspace(&self) -> Option<maho_types::ai::AiWorkspace> {
        let space_id = self.active_space_id.lock_unpoison().clone();

        let root_str = self.workspace_root.to_string_lossy().to_string();

        let workspaces = self.storage.list_workspaces().await.ok()?;

        if let Some(ref sid) = space_id {
            if let Some(ws) = workspaces.iter().find(|w| w.space_id.as_ref() == Some(sid)) {
                return Some(ws.clone());
            }
        }

        workspaces
            .into_iter()
            .find(|w| w.workspace_root.as_ref() == Some(&root_str))
    }

    pub fn set_llm_override(&self, llm: Box<dyn swiftide_core::chat_completion::ChatCompletion>) {
        *self.llm_override.lock_unpoison() = Some(llm);
    }

    fn build_gated_tool(
        &self,

        tool: Arc<dyn Tool>,

        sensitive: bool,

        denied_flag: Arc<Mutex<bool>>,

        session_id: &str,
    ) -> Box<dyn Tool> {
        let tool_name = tool.name().to_string();

        let resolution = ToolSensitivityResolution::for_tool_name(&tool_name, sensitive);

        let sensitivity = resolution.effective;

        Box::new(PermissionGatedTool {
            inner_tool: tool,

            sensitivity,

            sensitivity_downgrade: resolution.downgrade,

            permission_callback: Arc::clone(&self.permission_callback),

            denied_flag,

            session_id: session_id.to_string(),

            approval_policy: Arc::clone(&self.approval_policy),

            runtime_tier: Arc::clone(&self.runtime_tier),

            fs_whitelist_roots: Arc::clone(&self.fs_whitelist_roots),

            workspace_root: Arc::new(self.workspace_root.clone()),

            artifact_root: Arc::clone(&self.artifact_root),
        }) as Box<dyn Tool>
    }

    // Gate a tool using an explicit effective sensitivity, WITHOUT re-resolving
    // it from the shared browser-action contract. Consequence classification in
    // PermissionGatedTool decides whether an otherwise ordinary browser action
    // may proceed without a prompt. The browser remains authoritative for
    // target, exact-origin, lease, gesture, Vault, and Mail boundaries.
    fn build_gated_tool_with_sensitivity(
        &self,
        tool: Arc<dyn Tool>,
        sensitivity: ToolSensitivity,
        denied_flag: Arc<Mutex<bool>>,
        session_id: &str,
    ) -> Box<dyn Tool> {
        Box::new(PermissionGatedTool {
            inner_tool: tool,
            sensitivity,
            sensitivity_downgrade: Option::None,
            permission_callback: Arc::clone(&self.permission_callback),
            denied_flag,
            session_id: session_id.to_string(),
            approval_policy: Arc::clone(&self.approval_policy),
            runtime_tier: Arc::clone(&self.runtime_tier),
            fs_whitelist_roots: Arc::clone(&self.fs_whitelist_roots),
            workspace_root: Arc::new(self.workspace_root.clone()),
            artifact_root: Arc::clone(&self.artifact_root),
        }) as Box<dyn Tool>
    }

    /// Resolve the user-facing final answer for a completed agent turn.
    ///
    /// Shared by the tool-enabled loop (`run_turn_internal`, used by the CLI and
    /// the in-browser AI panel via the FFI leased bridge) and the no-tools loop
    /// (`run_turn_internal_simple`, used by deep research). Centralizing it here
    /// guarantees the synthesis / anti-raw-tool-output behavior cannot silently
    /// diverge between surfaces: pick the stop/assistant answer, synthesize when
    /// the loop ended on unfinished intent, and synthesize (with a non-error
    /// tool-output fallback) when no assistant text was produced at all.
    async fn finalize_turn_answer(
        &self,
        session_id: &str,
        llm: &dyn ChatCompletion,
        result_history: &[SwiftideChatMessage],
    ) -> Result<MahoChatMessage, AgentError> {
        let stop_answer = result_history
            .iter()
            .rev()
            .find_map(|msg| {
                if let SwiftideChatMessage::ToolOutput(_, out) = msg {
                    match out {
                        ToolOutput::Stop(Some(v)) | ToolOutput::AgentFailed(Some(v)) => {
                            Some(match v {
                                serde_json::Value::String(s) => s.clone(),
                                other => other.to_string(),
                            })
                        }
                        _ => None,
                    }
                } else {
                    None
                }
            })
            .filter(|s| !s.trim().is_empty());
        let last_assistant_text = stop_answer.or_else(|| {
            result_history.iter().rev().find_map(|msg| {
                if let SwiftideChatMessage::Assistant(text, _) = msg {
                    text.clone().filter(|t| !t.trim().is_empty())
                } else {
                    None
                }
            })
        });

        if let Some(text) = last_assistant_text {
            let final_text = if looks_like_unfinished_intent(&text) {
                synthesize_final_answer(llm, result_history)
                    .await
                    .unwrap_or(text)
            } else {
                text
            };
            self.storage
                .save_message(session_id, "assistant", &final_text)
                .await?;
            Ok(MahoChatMessage::assistant(ChatContent::text(final_text)))
        } else {
            let history_summary: Vec<&str> = result_history
                .iter()
                .map(|m| match m {
                    SwiftideChatMessage::System(_) => "system",
                    SwiftideChatMessage::User(_) => "user",
                    SwiftideChatMessage::Assistant(_, _) => "assistant",
                    SwiftideChatMessage::ToolOutput(_, _) => "tool_output",
                    SwiftideChatMessage::Summary(_) => "summary",
                })
                .collect();
            tracing::error!(
                target = "maho_agent::run_turn",
                "Swiftide agent run completed but no assistant message was found in context. History sequence: {:?}",
                history_summary
            );
            let synthesized = synthesize_final_answer(llm, result_history).await;
            let fallback = synthesized.unwrap_or_else(|| {
                result_history
                    .iter()
                    .rev()
                    .find_map(|msg| {
                        if let SwiftideChatMessage::ToolOutput(_, ToolOutput::Text(s)) = msg {
                            let t = s.trim();
                            if t.is_empty() || t.starts_with("{\"error\"") {
                                None
                            } else {
                                Some(s.clone())
                            }
                        } else {
                            None
                        }
                    })
                    .unwrap_or_default()
            });
            self.storage
                .save_message(session_id, "assistant", &fallback)
                .await?;
            Ok(MahoChatMessage::assistant(ChatContent::text(fallback)))
        }
    }

    async fn resolve_llm_client(
        &self,
        llm_override_opt: Option<Box<dyn swiftide_core::chat_completion::ChatCompletion>>,
        selected_model_name: &str,
    ) -> Result<Box<dyn swiftide_core::chat_completion::ChatCompletion>, AgentError> {
        if let Some(llm) = llm_override_opt {
            return Ok(llm);
        }

        let preferred = self.preferred_provider.lock_unpoison().clone();
        let is_managed = preferred.as_deref() == Some("managed");
        let has_explicit_byok = preferred.is_some() && !is_managed;

        if is_managed {
            if let Some(access_token) = self.get_managed_auth_token().await? {
                let api_base = crate::proxy_url();
                let model_name = selected_model_name.to_string();
                let secret_key =
                    secrecy::SecretString::new(access_token.as_str().to_string().into_boxed_str());
                let client = crate::maho_reasoning_openai::MahoReasoningOpenAI::new(
                    secret_key, api_base, model_name,
                );
                return Ok(Box::new(client));
            } else {
                return Err(AgentError::Credential(
                    CredentialError::ManagedAuthUnavailable,
                ));
            }
        }

        if has_explicit_byok {
            let secure_client = {
                let guard = self.secure_storage_callback.lock_unpoison();
                guard.as_ref().map(|cb| {
                    resolve_byok_client(preferred.as_deref(), cb.as_ref(), selected_model_name)
                })
            };

            if let Some(res) = secure_client {
                let (client, _kind) = res?;
                return Ok(client);
            }

            if !self.allow_insecure_key_storage {
                return Err(AgentError::Credential(
                    CredentialError::SecureStoreUnavailable,
                ));
            }

            let pref_str = preferred.as_deref().unwrap_or("openai");
            let (provider, bundle) = if let Some(key) = self.storage.get_byok_key(pref_str).await? {
                (pref_str, SecureKeyBundle::from_key(key))
            } else {
                return Err(AgentError::ExecutionError(format!(
                    "No BYOK API key configured for provider '{pref_str}'"
                )));
            };

            let (client, _kind) = build_byok_client(provider, bundle, selected_model_name)?;
            return Ok(client);
        }

        // Neither managed nor explicit BYOK specified:
        // Try managed first if available, otherwise fall back to secure storage probing.
        if let Some(access_token) = self.get_managed_auth_token().await? {
            let api_base = crate::proxy_url();
            let model_name = selected_model_name.to_string();
            let secret_key =
                secrecy::SecretString::new(access_token.as_str().to_string().into_boxed_str());
            let client = crate::maho_reasoning_openai::MahoReasoningOpenAI::new(
                secret_key, api_base, model_name,
            );
            return Ok(Box::new(client));
        }

        let secure_client = {
            let guard = self.secure_storage_callback.lock_unpoison();
            guard
                .as_ref()
                .and_then(|cb| resolve_byok_client(None, cb.as_ref(), selected_model_name).ok())
        };

        if let Some((client, _kind)) = secure_client {
            Ok(client)
        } else {
            if !self.allow_insecure_key_storage {
                return Err(AgentError::Credential(
                    CredentialError::SecureStoreUnavailable,
                ));
            }

            let (provider, bundle) = if let Some(key) = self.storage.get_byok_key("openai").await? {
                ("openai", SecureKeyBundle::from_key(key))
            } else if let Some(key) = self.storage.get_byok_key("anthropic").await? {
                ("anthropic", SecureKeyBundle::from_key(key))
            } else {
                return Err(AgentError::ExecutionError(
                    "No BYOK API key configured".to_string(),
                ));
            };

            let (client, _kind) = build_byok_client(provider, bundle, selected_model_name)?;
            Ok(client)
        }
    }

    async fn run_turn_internal(
        &self,

        session_id: &str,

        message: MahoChatMessage,

        denied_flag: Arc<Mutex<bool>>,

        on_token: Option<Arc<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>>,

        on_event: Option<std::sync::Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static>>,
    ) -> Result<MahoChatMessage, AgentError> {
        let content_str = match &message.content {
            ChatContent::Text(t) => t.clone(),

            ChatContent::Image { .. } => {
                return Err(AgentError::ExecutionError(
                    "Multimodal image content is not supported yet".to_string(),
                ));
            }

            ChatContent::Mixed(_) => {
                return Err(AgentError::ExecutionError(
                    "Multimodal mixed content is not supported yet".to_string(),
                ));
            }
        };

        // Save user message to storage

        self.storage
            .save_message(session_id, &message.role, &content_str)
            .await?;

        // Initialize run and record step_started event in journal
        let run_id = crate::run_journal::AgentRunId::new(session_id);
        {
            let mut j = self.run_journal.lock_unpoison();
            if !j.runs().contains_key(&run_id) {
                let _ = j.start_run(run_id.clone(), session_id);
            }
            let now_ts = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs();
            let _ = j.append_event(
                &run_id,
                "step_started",
                serde_json::json!({
                    "role": message.role,
                    "timestamp": now_ts,
                })
                .to_string(),
            );
        }

        // Fetch active workspace and profile to resolve configuration

        let active_workspace = self.find_active_workspace().await;

        let root_str = self.workspace_root.to_string_lossy().to_string();

        let workspace_is_real = active_workspace.is_some();

        let ws = active_workspace.unwrap_or_else(|| maho_types::ai::AiWorkspace {
            id: "default".to_string(),

            name: "Default Workspace".to_string(),

            profile_id: Some("default".to_string()),

            space_id: None,

            workspace_root: Some(root_str),

            created_at: String::new(),

            updated_at: String::new(),
        });

        let mut profile = None;

        let mut profile_deleted_notice = false;

        if let Some(ref pid) = ws.profile_id {
            if let Ok(Some(p)) = self.storage.get_profile(pid).await {
                profile = Some(p);
            } else if workspace_is_real {
                profile_deleted_notice = true;
            }
        }

        if profile.is_none() {
            if let Ok(Some(seeded)) = self.storage.get_profile("blank").await {
                profile = Some(seeded);
            }
        }

        let profile = profile.unwrap_or_else(|| maho_types::ai::AiProfile {
            id: "default".to_string(),

            name: "Default Profile".to_string(),

            system_prompt: String::new(),

            preferred_model: None,

            tools: vec![],

            mcp_servers: vec![],

            is_default: true,

            created_at: String::new(),

            updated_at: String::new(),
        });

        let resolved = maho_core::config_resolver::resolve_config(None, &profile, &ws);

        let l1_briefing_content = match self
            .storage
            .get_memory_block(maho_core::memory::L1_BRIEFING_LABEL)
            .await
        {
            Ok(Some(b)) if !b.trim().is_empty() => Some(b),
            _ => None,
        };

        let memory_enabled = !profile
            .tools
            .iter()
            .any(|t| t == "no_memory" || t == "disable_memory");
        let active_l1_briefing = if memory_enabled {
            l1_briefing_content.as_deref()
        } else {
            None
        };

        let initial_composed_prompt = crate::system_prompt::compose_system_prompt_with_briefing(
            &resolved.system_prompt,
            &[],
            active_l1_briefing,
        );

        let _ = self
            .storage
            .set_system_prompt_snapshot_if_absent(session_id, &initial_composed_prompt)
            .await;

        if profile_deleted_notice {
            let notice = "System notice: The active agent profile was deleted. Falling back to the default Blank profile.";

            let _ = self
                .storage
                .save_message(session_id, "system", notice)
                .await;
        }

        // Model routing resolution
        let available = self.available_models.lock_unpoison().clone();
        let category = crate::model_routing::TaskCategory::Chat;
        let preference = profile.preferred_model.clone();

        let routing_decision =
            crate::model_routing::RoutingPolicy::resolve(category, preference.clone(), &available);

        let (selected_model_name, fallback_model_name) = match routing_decision {
            Ok(decision) => {
                self.routing_decisions
                    .lock_unpoison()
                    .push(decision.clone());
                if decision.is_fallback {
                    tracing::info!(
                        target: "maho_agent::audit",
                        session_id = %session_id,
                        fallback_reason = %decision.reason,
                        "Model routing fallback: {}",
                        decision.reason
                    );
                    if let Some(ref cb) = on_event {
                        cb(AgentStreamEvent::Thinking(format!(
                            "[Model Routing] {}",
                            decision.reason
                        )));
                    }
                }
                (
                    decision.selected_model.clone(),
                    decision.fallback_model.clone(),
                )
            }
            Err(e) => {
                tracing::warn!(
                    target: "maho_agent::routing",
                    error = %e,
                    "Model routing resolution failed; using profile/default model"
                );
                (
                    profile
                        .preferred_model
                        .clone()
                        .unwrap_or_else(|| crate::DEFAULT_MANAGED_MODEL.to_string()),
                    None,
                )
            }
        };

        // Determine LLM client

        let llm_override_opt = {
            let lock = self.llm_override.lock_unpoison();

            lock.as_ref().map(|llm| dyn_clone::clone_box(&**llm))
        };

        let raw_llm = self
            .resolve_llm_client(llm_override_opt, &selected_model_name)
            .await?;

        let retry_policy = self.retry_policy.lock_unpoison().clone();
        let llm: Box<dyn swiftide_core::chat_completion::ChatCompletion> =
            Box::new(RetryChatCompletion {
                inner: Arc::from(raw_llm),
                retry_policy,
                fallback_reasons: Arc::clone(&self.retry_fallback_reasons),
                model_name: selected_model_name.clone(),
                fallback_model_name,
                journal: Some(Arc::clone(&self.run_journal)),
                run_id: Some(crate::run_journal::AgentRunId::new(session_id)),
            });

        // Get history

        let history = self.storage.get_history(session_id).await?;

        let mut swiftide_history = Vec::new();

        // Retrieve system prompt and prepend it.

        // Priority: explicit programmatic override (empty in production) >

        // per-conversation snapshot (frozen at first turn so mid-conversation

        // profile switches do NOT rewrite an ongoing chat, #13) > current profile.

        let mut memories_str = String::new();

        if memory_enabled {
            if let Ok(memories) = self.storage.fts_relevant_memories(&content_str, 10).await {
                if !memories.is_empty() {
                    memories_str.push_str("\n\nRetrieved Memories:\n");

                    for (_, content, _) in memories {
                        memories_str.push_str(&format!("- {}\n", content));
                    }
                }
            }
        }

        let system_prompt_val = {
            let sp = self.system_prompt.lock_unpoison().clone();

            let base = if !sp.is_empty() {
                sp
            } else {
                match self.storage.get_system_prompt_snapshot(session_id).await {
                    Ok(Some(snap)) => snap,

                    _ => initial_composed_prompt.clone(),
                }
            };

            if base.is_empty() && memories_str.is_empty() {
                String::new()
            } else {
                format!("{}{}", base, memories_str)
            }
        };

        if !system_prompt_val.is_empty() {
            swiftide_history.push(SwiftideChatMessage::new_system(system_prompt_val));
        }

        for msg in history {
            let text_content = match &msg.content {
                ChatContent::Text(t) => t.clone(),

                ChatContent::Image { .. } => {
                    return Err(AgentError::ExecutionError(
                        "Multimodal image content is not supported in history".to_string(),
                    ));
                }

                ChatContent::Mixed(_) => {
                    return Err(AgentError::ExecutionError(
                        "Multimodal mixed content is not supported in history".to_string(),
                    ));
                }
            };

            let swiftide_msg = match msg.role.as_str() {
                "system" => SwiftideChatMessage::new_system(text_content),

                "user" => SwiftideChatMessage::new_user(text_content),

                "assistant" => SwiftideChatMessage::new_assistant(Some(text_content), None),

                _ => SwiftideChatMessage::new_user(text_content),
            };

            swiftide_history.push(swiftide_msg);
        }

        // Construct platform-gated tools

        let mut platform_tools = Vec::new();

        #[cfg(any(target_os = "ios", target_os = "android"))]
        {
            platform_tools.push(self.build_gated_tool(
                Arc::new(CameraTool),
                false,
                denied_flag.clone(),
                session_id,
            ));

            platform_tools.push(self.build_gated_tool(
                Arc::new(PhotoLibraryTool),
                false,
                denied_flag.clone(),
                session_id,
            ));

            platform_tools.push(self.build_gated_tool(
                Arc::new(ClipboardTool),
                false,
                denied_flag.clone(),
                session_id,
            ));

            platform_tools.push(self.build_gated_tool(
                Arc::new(FsWriteTool {
                    artifact_root: self.artifact_root.lock_unpoison().clone(),
                }),
                false,
                denied_flag.clone(),
                session_id,
            ));
        }

        #[cfg(not(any(target_os = "ios", target_os = "android")))]
        {
            platform_tools.push(self.build_gated_tool(
                Arc::new(FsReadTool {
                    workspace_root: self.workspace_root.clone(),
                }),
                false,
                denied_flag.clone(),
                session_id,
            ));

            platform_tools.push(self.build_gated_tool(
                Arc::new(FsWriteTool {
                    artifact_root: self.artifact_root.lock_unpoison().clone(),
                }),
                false,
                denied_flag.clone(),
                session_id,
            ));

            platform_tools.push(self.build_gated_tool(
                Arc::new(ShellExecTool {
                    workspace_root: self.workspace_root.clone(),
                }),
                true,
                denied_flag.clone(),
                session_id,
            ));

            // Dynamic loading of MCP and CLI tools.

            // NOTE: SwiftideBackend is the Source of Truth (SoT) for discovering,

            // connecting to, and executing MCP servers and CLI tools during an agent session.

            // It resolves active workspaces matching the current root, executes tool specs,

            // and manages connection states dynamically.

            let active_workspace = self.find_active_workspace().await;

            let workspace_id = active_workspace.as_ref().map(|ws| ws.id.clone());

            let mut mcp_servers = Vec::new();

            let mut cli_tools = Vec::new();

            if let Some(ws_id) = &workspace_id {
                if let Ok(servers) = self.storage.list_mcp_servers(ws_id).await {
                    tracing::debug!(

                        target: "maho_agent::swiftide_backend",

                        workspace_id = %ws_id,

                        server_count = servers.len(),

                        "list_mcp_servers loaded"

                    );

                    mcp_servers = servers;
                }

                if let Ok(tools) = self.storage.list_cli_tools(ws_id).await {
                    cli_tools = tools;
                }
            }

            let keychain_lookup = |keychain_id: &Option<String>| -> Option<String> {
                if let Some(ref kid) = keychain_id {
                    let secure_cb = self.secure_storage_callback.lock_unpoison();

                    if let Some(ref cb) = *secure_cb {
                        let provider = format!("mcp:{kid}");

                        if let Ok(bundle) = cb(&provider) {
                            return Some(bundle.key.as_str().to_string());
                        }
                    }
                }

                None
            };

            // Purge MCP client cache entries for deleted servers.
            {
                let mut cache = self.mcp_client_cache.lock_unpoison();
                cache.retain(|k, _| {
                    if let Some((id, _)) = k.split_once("::") {
                        mcp_servers.iter().any(|s| s.id == id)
                    } else {
                        false
                    }
                });
            }

            for server in mcp_servers {
                if !should_attempt_mcp_connect(&server) {
                    continue;
                }

                if !profile.mcp_servers.is_empty() && !profile.mcp_servers.contains(&server.id) {
                    continue;
                }

                let auth_token = keychain_lookup(&server.auth_keychain_id);

                let cache_key = format!("{}::{}", server.id, server.updated_at);

                let cached = {
                    let cache = self.mcp_client_cache.lock_unpoison();

                    cache.get(&cache_key).cloned()
                };

                let client_arc = match cached {
                    Some(c) if c.is_alive() => Some(c),

                    Some(dead) => {
                        dead.emit_unavailable();

                        self.mcp_client_cache.lock_unpoison().remove(&cache_key);

                        Option::None
                    }

                    Option::None => Option::None,
                };

                let client_arc = if let Some(cached) = client_arc {
                    cached
                } else {
                    match McpClient::connect(&server, auth_token).await {
                        Ok(client) => {
                            let arc = Arc::new(client);

                            let mut cache = self.mcp_client_cache.lock_unpoison();

                            cache.retain(|k, _| !k.starts_with(&format!("{}::", server.id)));

                            cache.insert(cache_key, Arc::clone(&arc));

                            arc
                        }

                        Err(e) => {
                            tracing::warn!(

                                target: "maho_agent::swiftide_backend",

                                server = %server.name,

                                error = %e,

                                "McpClient::connect failed"

                            );

                            if cfg!(debug_assertions) {
                                let dbg_msg = format!(
                                    "[swiftide_backend] MCP server '{}' offline: {}",
                                    server.name, e
                                );

                                let _ = std::fs::OpenOptions::new()
                                    .create(true)
                                    .append(true)
                                    .open("/tmp/maho-agent-live.log")
                                    .and_then(|mut f| {
                                        use std::io::Write;

                                        writeln!(f, "{}", dbg_msg)
                                    });
                            }

                            if let Some(ref list) = server.trusted_tools {
                                for tool_name in list {
                                    let shim = OfflineMcpToolShim {
                                        name: format!("mcp:{}/{}", server.name, tool_name),

                                        server_name: server.name.clone(),
                                    };

                                    platform_tools.push(self.build_gated_tool(
                                        Arc::new(shim),
                                        false,
                                        denied_flag.clone(),
                                        session_id,
                                    ));
                                }
                            }

                            continue;
                        }
                    }
                };

                match client_arc.list_tools().await {
                    Ok(tools) => {
                        for tool in tools {
                            // Per-tool sensitivity: a dogfood (AutoApprove) tool

                            // auto-runs only if READ-ONLY; sensitive browser actions

                            // stay gated. Explicit server trust is preserved. See

                            // mcp_tool_is_sensitive (FIX-9a #3).

                            let raw_name = tool
                                .name
                                .split_once('/')
                                .map(|(_, name)| name)
                                .unwrap_or(&tool.name);

                            let sensitive = mcp_tool_is_sensitive(
                                tool.sensitive,
                                raw_name,
                                &tool.name,
                                server.trusted,
                                server.trusted_tools.as_ref(),
                            );

                            let shim = McpToolShim {
                                name: llm_safe_tool_name(&tool.name),

                                dispatch_name: tool.name.clone(),

                                description: tool.description.clone(),

                                parameters_schema: tool.parameters_schema.clone(),

                                client: Arc::clone(&client_arc),
                            };

                            platform_tools.push(self.build_gated_tool(
                                Arc::new(shim),
                                sensitive,
                                denied_flag.clone(),
                                session_id,
                            ));
                        }
                    }

                    Err(e) => {
                        tracing::warn!(

                            target: "maho_agent::swiftide_backend",

                            server = %server.name,

                            error = %e,

                            "McpClient::list_tools failed"

                        );
                    }
                }
            }

            for tool in cli_tools {
                let executor =
                    CliToolExecutor::new(tool.clone(), Some(self.workspace_root.clone()));

                let shim = CliToolShim {
                    name: format!("sh:{}", tool.name),

                    executor: Arc::new(executor),
                };

                platform_tools.push(self.build_gated_tool(
                    Arc::new(shim),
                    tool.sensitive,
                    denied_flag.clone(),
                    session_id,
                ));
            }
        }

        let browser_bridge_opt = self.browser_tool_bridge.lock_unpoison().clone();

        let dbg_log = |msg: &str| {
            // #8: debug-only diagnostic log. Never write tool args (may contain

            // page content) to a world-readable fixed path in release builds.

            if !cfg!(debug_assertions) {
                return;
            }

            let _ = std::fs::OpenOptions::new()
                .create(true)
                .append(true)
                .open("/tmp/maho-agent-live.log")
                .and_then(|mut f| {
                    use std::io::Write;

                    writeln!(f, "{}", msg)
                });
        };

        if let Some(bridge) = browser_bridge_opt {
            let descriptors = match bridge.list_tool_descriptors().await {
                Ok(descriptors) => descriptors,
                Err(error) => {
                    tracing::warn!(target: "maho_agent::swiftide_backend", %error, "browser tool discovery unavailable; browser tools disabled");
                    Vec::new()
                }
            };
            let fallback_ladder = Arc::new(Mutex::new(FallbackLadderState::new(session_id)));

            let shim_context = BrowserToolShimContext::turn(
                session_id,
                Arc::clone(&self.approval_policy),
                fallback_ladder,
                Arc::clone(&self.mail_authorization_state),
            );

            let shims = browser_tool_shims(
                bridge,
                descriptors,
                self.browser_action_tools_state(),
                shim_context,
            );

            let names: Vec<_> = shims.iter().map(|s| s.name.clone()).collect();

            dbg_log(&format!(
                "[swiftide_backend] Registering {} browser tools: {:?}",
                shims.len(),
                names
            ));

            for shim in shims {
                let sensitivity = shim.sensitivity;

                platform_tools.push(self.build_gated_tool_with_sensitivity(
                    Arc::new(shim),
                    sensitivity,
                    denied_flag.clone(),
                    session_id,
                ));
            }
        } else {
            dbg_log("[swiftide_backend] No browser_tool_bridge set");
        }

        // Composite (batch) execution — plan row 11. Registered like every
        // other platform tool; admission is gated by the session
        // runtime_config at dispatch time (fail-closed default).
        platform_tools.push(self.build_gated_tool(
            Arc::new(CompositeExecuteTool {
                bridge: self.browser_tool_bridge.lock_unpoison().clone(),
                config: Arc::clone(&self.composite_config),
                on_event: on_event.clone(),
                clock: Arc::clone(&self.clock),
                turn_start_millis: self.clock.now_millis(),
            }),
            false,
            denied_flag.clone(),
            session_id,
        ));

        // profile.tools is a per-profile allowlist keyed by tool name (empty =

        // all tools allowed), mirroring maho_core get_effective_tools. Enforce

        // it at exec time so a profile's restrictions actually gate the tools

        // the agent can call, not just the settings UI listing.

        if !profile.tools.is_empty() {
            platform_tools.retain(|t| profile.tools.contains(&t.name().to_string()));
        }

        {
            let filter_lock = self.allowed_tools.lock_unpoison();

            if let Some(ref allowed) = *filter_lock {
                platform_tools.retain(|t| {
                    let name_cow = t.name();

                    let name_str = name_cow.as_ref();

                    let raw_name = name_str
                        .split_once('/')
                        .map(|(_, name)| name)
                        .unwrap_or(name_str);

                    allowed
                        .iter()
                        .any(|a| a.as_str() == name_str || a.as_str() == raw_name)
                });
            }
        }

        let final_names: Vec<_> = platform_tools
            .iter()
            .map(|t| t.name().to_string())
            .collect();

        dbg_log(&format!(
            "[swiftide_backend] Final platform_tools count: {} names: {:?}",
            platform_tools.len(),
            final_names
        ));

        // Load DefaultContext with history

        let mut context = swiftide_agents::DefaultContext::default();

        context
            .with_existing_messages(swiftide_history)
            .await
            .map_err(|e| AgentError::ExecutionError(e.to_string()))?;

        let has_screenshot = self.has_screenshot_capability(&platform_tools);

        // Instantiate Agent Builder

        let mut builder = swiftide_agents::Agent::builder();

        builder.llm(&llm).context(context).tools(platform_tools);

        let clock = Arc::clone(&self.clock);
        let turn_start_millis = clock.now_millis();
        let status_ticks_emitted = Arc::new(std::sync::atomic::AtomicU64::new(0));

        let cb_before = on_event.clone();
        let clock_before = Arc::clone(&clock);
        let status_ticks_before = Arc::clone(&status_ticks_emitted);
        let journal_before = Arc::clone(&self.run_journal);
        let session_id_before = session_id.to_string();

        builder.before_tool(move |_agent, tool_call| {
            let name = tool_call.name().to_string();

            if is_internal_control_tool(&name) {
                return Box::pin(async move { Ok(()) });
            }

            let id = tool_call.id().to_string();
            let args = crate::permission::sanitize_tool_arguments(tool_call.args().unwrap_or("{}"));

            let run_id = crate::run_journal::AgentRunId::new(&session_id_before);
            {
                let mut j = journal_before.lock_unpoison();
                if !j.runs().contains_key(&run_id) {
                    let _ = j.start_run(run_id.clone(), &session_id_before);
                }
                let now_ts = clock_before.now_millis() / 1000;
                let _ = j.append_event(
                    &run_id,
                    &name,
                    serde_json::json!({
                        "tool_call_id": id,
                        "args": args,
                        "timestamp": now_ts,
                    })
                    .to_string(),
                );
            }

            if let Some(ref cb) = cb_before {
                let now_millis = clock_before.now_millis();
                let elapsed_secs = now_millis.saturating_sub(turn_start_millis) / 1000;
                let target_ticks = elapsed_secs / 30;
                let mut current_ticks =
                    status_ticks_before.load(std::sync::atomic::Ordering::SeqCst);
                while current_ticks < target_ticks {
                    if status_ticks_before
                        .compare_exchange(
                            current_ticks,
                            current_ticks + 1,
                            std::sync::atomic::Ordering::SeqCst,
                            std::sync::atomic::Ordering::SeqCst,
                        )
                        .is_ok()
                    {
                        current_ticks += 1;
                        let secs = current_ticks * 30;
                        cb(AgentStreamEvent::Status {
                            elapsed_secs: secs,
                            message: format!("Turn in progress: {secs}s elapsed"),
                        });
                    } else {
                        current_ticks =
                            status_ticks_before.load(std::sync::atomic::Ordering::SeqCst);
                    }
                }

                cb(AgentStreamEvent::ToolCall { id, name, args });
            }

            Box::pin(async move { Ok(()) })
        });

        let cb_after = on_event.clone();
        let artifact_session_id = session_id.to_string();
        let artifact_storage = std::sync::Arc::clone(&self.storage);
        let journal_after = Arc::clone(&self.run_journal);

        builder.after_tool(move |_agent, tool_call, result| {
            let name = tool_call.name().to_string();

            if is_internal_control_tool(&name) {
                return Box::pin(async move { Ok(()) });
            }

            let id = tool_call.id().to_string();

            let (result_str, succeeded) = match result {
                Ok(output) => (format!("{}", output), true),
                Err(err) => (format!("{}", err), false),
            };

            let run_id = crate::run_journal::AgentRunId::new(&artifact_session_id);
            {
                let mut j = journal_after.lock_unpoison();
                let now_ts = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_secs();
                let _ = j.append_event(
                    &run_id,
                    "step_progress",
                    serde_json::json!({
                        "tool_call_id": id,
                        "tool_name": name,
                        "succeeded": succeeded,
                        "result": result_str,
                        "timestamp": now_ts,
                    })
                    .to_string(),
                );
            }

            let (events, artifact_to_persist) =
                build_after_tool_events(&artifact_session_id, id, name, succeeded, &result_str);
            if let Some(ref cb) = cb_after {
                for ev in events {
                    cb(ev);
                }
            }
            let storage = std::sync::Arc::clone(&artifact_storage);
            Box::pin(async move {
                if let Some(artifact) = artifact_to_persist {
                    if let Err(err) = storage.save_artifact(&artifact).await {
                        dbg_log(&format!(
                            "[swiftide_backend] failed to persist artifact index: {err}"
                        ));
                    }
                }
                Ok(())
            })
        });

        if on_token.is_some() || on_event.is_some() {
            let on_token_cb = on_token.clone();

            let on_event_cb = on_event.clone();

            builder.on_stream(move |_agent, response| {
                let on_token_cb = on_token_cb.clone();

                let on_event_cb = on_event_cb.clone();

                if let Some(ref delta) = response.delta {
                    if let Some(ref chunk) = delta.message_chunk {
                        if let Some(ref cb) = on_token_cb {
                            cb(chunk);
                        }

                        if let Some(ref cb) = on_event_cb {
                            cb(AgentStreamEvent::Token(chunk.clone()));
                        }
                    }

                    if let Some(ref reasoning) = delta.reasoning_chunk {
                        if let Some(ref cb) = on_event_cb {
                            cb(AgentStreamEvent::Thinking(reasoning.clone()));
                        }
                    }
                }

                Box::pin(async move { Ok(()) })
            });
        }

        let mut agent = builder
            .build()
            .map_err(|e| AgentError::ExecutionError(e.to_string()))?;

        // Turn safety-net timeout (600s). Per-tool timeouts (MCP 60s, browser

        // 30s, CLI/shell configurable) and the permission dialog's own 30s bound

        // govern real waits; this outer cap only catches a genuinely runaway

        // agent loop and does NOT count human permission-response time against

        // the turn.

        tokio::time::timeout(
            std::time::Duration::from_secs(600),
            agent.query(content_str.clone()),
        )
        .await
        .map_err(|_| AgentError::ExecutionError("Agent query timed out".to_string()))?
        .map_err(|e| AgentError::ExecutionError(e.to_string()))?;

        // A denied tool returns a ToolError to the model (see PermissionGatedTool),

        // which lets the model produce a normal response instead of aborting the

        // whole turn. We no longer discard the turn on denial.

        // Retrieve the last message from the context

        let result_history = agent
            .context()
            .history()
            .await
            .map_err(|e| AgentError::ExecutionError(e.to_string()))?;

        let res = self
            .finalize_turn_answer(session_id, llm.as_ref(), &result_history)
            .await;

        if let Some(ref cb) = on_event {
            if has_screenshot {
                cb(AgentStreamEvent::ProofOrReason {
                    proof_locator: Some("browser_screenshot_element".to_string()),
                    reason: None,
                });
            } else {
                cb(AgentStreamEvent::ProofOrReason {
                    proof_locator: None,
                    reason: Some(
                        "Screenshot/proof capability is not available in current environment"
                            .to_string(),
                    ),
                });
            }
        }

        // Record terminal status and durable checkpoint in run journal
        let run_id = crate::run_journal::AgentRunId::new(session_id);
        {
            let mut j = self.run_journal.lock_unpoison();
            let now_ts = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs();
            let (status_str, kind) = match &res {
                Ok(_) => (
                    "completed",
                    crate::run_journal::RunCheckpointKind::StepProgress,
                ),
                Err(_) => ("failed", crate::run_journal::RunCheckpointKind::Failed),
            };
            let _ = j.append_event(
                &run_id,
                "status",
                serde_json::json!({
                    "status": status_str,
                    "timestamp": now_ts,
                })
                .to_string(),
            );
            let last_seq = j
                .get_entries(&run_id)
                .map(|e| e.last().map(|x| x.event_seq).unwrap_or(0))
                .unwrap_or(0);
            let _ = j.record_checkpoint(crate::run_journal::RunCheckpoint {
                run_id: run_id.clone(),
                session_id: session_id.to_string(),
                last_event_seq: last_seq,
                kind,
                classification: crate::run_journal::RestartClassification::Recoverable,
                state_payload: None,
                timestamp: now_ts,
            });
        }

        res
    }

    pub async fn discover_tools(&self) -> Vec<ToolDefinition> {
        let mut platform_tools = crate::tool_registry::get_platform_tools();

        let active_workspace = self.find_active_workspace().await;

        let mut active_profile = None;

        if let Some(ws) = active_workspace.as_ref() {
            if let Some(profile_id) = ws.profile_id.as_ref() {
                if let Ok(Some(profile)) = self.storage.get_profile(profile_id).await {
                    active_profile = Some(profile);
                }
            }

            if active_profile.is_none() {
                if let Ok(Some(profile)) = self.storage.get_profile("blank").await {
                    active_profile = Some(profile);
                }
            }
        }

        let workspace_id = active_workspace.as_ref().map(|ws| ws.id.clone());

        let mut mcp_servers = Vec::new();

        let mut cli_tools = Vec::new();

        if let Some(ws_id) = &workspace_id {
            if let Ok(servers) = self.storage.list_mcp_servers(ws_id).await {
                mcp_servers = servers;
            }

            if let Ok(tools) = self.storage.list_cli_tools(ws_id).await {
                cli_tools = tools;
            }
        }

        let keychain_lookup = |keychain_id: &Option<String>| -> Option<String> {
            if let Some(ref kid) = keychain_id {
                let secure_cb = self.secure_storage_callback.lock_unpoison();

                if let Some(ref cb) = *secure_cb {
                    let provider = format!("mcp:{kid}");

                    if let Ok(bundle) = cb(&provider) {
                        return Some(bundle.key.as_str().to_string());
                    }
                }
            }

            None
        };

        // Purge MCP client cache entries for deleted servers.
        {
            let mut cache = self.mcp_client_cache.lock_unpoison();
            cache.retain(|k, _| {
                if let Some((id, _)) = k.split_once("::") {
                    mcp_servers.iter().any(|s| s.id == id)
                } else {
                    false
                }
            });
        }

        for server in mcp_servers {
            if !server.trusted {
                continue;
            }

            let auth_token = keychain_lookup(&server.auth_keychain_id);

            let cache_key = format!("{}::{}", server.id, server.updated_at);

            let cached = {
                let cache = self.mcp_client_cache.lock_unpoison();

                cache.get(&cache_key).cloned()
            };

            let client_arc = match cached {
                Some(c) if c.is_alive() => Some(c),

                Some(dead) => {
                    dead.emit_unavailable();

                    self.mcp_client_cache.lock_unpoison().remove(&cache_key);

                    Option::None
                }

                Option::None => Option::None,
            };

            let client_arc = if let Some(cached) = client_arc {
                cached
            } else {
                match McpClient::connect(&server, auth_token).await {
                    Ok(client) => {
                        let arc = Arc::new(client);

                        let mut cache = self.mcp_client_cache.lock_unpoison();

                        cache.retain(|k, _| !k.starts_with(&format!("{}::", server.id)));

                        cache.insert(cache_key, Arc::clone(&arc));

                        arc
                    }

                    Err(_) => {
                        if let Some(ref list) = server.trusted_tools {
                            for tool_name in list {
                                platform_tools.push(ToolDefinition {
                                    name: format!("mcp:{}/{}", server.name, tool_name),

                                    description: format!(
                                        "Unavailable: MCP server '{}' is offline.",
                                        server.name
                                    ),

                                    parameters_schema: serde_json::json!({

                                        "type": "object",

                                        "properties": {}

                                    }),

                                    provenance: ToolProvenance::External(server.name.clone()),

                                    sensitive: false,

                                    permission: ToolPermission::AutoApprove,
                                });
                            }
                        }

                        continue;
                    }
                }
            };

            if let Ok(tools) = client_arc.list_tools().await {
                for tool in tools {
                    let raw_name = tool
                        .name
                        .split_once('/')
                        .map(|(_, name)| name)
                        .unwrap_or(&tool.name);

                    let per_tool_trusted = server.trusted
                        && match &server.trusted_tools {
                            Option::None => true,

                            Some(list) => list.iter().any(|n| n == raw_name || n == &tool.name),
                        };

                    let sensitive = !per_tool_trusted;

                    platform_tools.push(ToolDefinition {
                        name: tool.name.clone(),

                        description: tool.description.clone(),

                        parameters_schema: tool.parameters_schema.clone(),

                        provenance: ToolProvenance::External(server.name.clone()),

                        sensitive,

                        permission: if sensitive {
                            ToolPermission::AlwaysAsk
                        } else {
                            ToolPermission::AutoApprove
                        },
                    });
                }
            }
        }

        for tool in cli_tools {
            platform_tools.push(ToolDefinition {
                name: format!("sh:{}", tool.name),

                description: tool.description.clone(),

                parameters_schema: tool.parameters_schema.clone(),

                provenance: ToolProvenance::AgentLocal,

                sensitive: tool.sensitive,

                permission: if tool.sensitive {
                    ToolPermission::AlwaysAsk
                } else {
                    ToolPermission::AutoApprove
                },
            });
        }

        let browser_bridge = self.browser_tool_bridge.lock_unpoison().clone();
        if let Some(bridge) = browser_bridge {
            let descriptors = match bridge.list_tool_descriptors().await {
                Ok(descriptors) => descriptors,
                Err(error) => {
                    tracing::warn!(target: "maho_agent::swiftide_backend", %error, "browser tool discovery unavailable; browser tools disabled");
                    Vec::new()
                }
            };
            let shim_context = BrowserToolShimContext::discovery(
                Arc::clone(&self.approval_policy),
                Arc::clone(&self.mail_authorization_state),
            );

            for shim in browser_tool_shims(
                bridge,
                descriptors,
                self.browser_action_tools_state(),
                shim_context,
            ) {
                platform_tools.push(shim.tool_definition());
            }
        }

        if let Some(ref profile) = active_profile {
            if !profile.tools.is_empty() {
                platform_tools.retain(|tool| profile.tools.contains(&tool.name));
            }
        }

        {
            let filter_lock = self.allowed_tools.lock_unpoison();

            if let Some(ref allowed) = *filter_lock {
                platform_tools.retain(|t| {
                    let raw_name = t
                        .name
                        .split_once('/')
                        .map(|(_, name)| name)
                        .unwrap_or(&t.name);

                    allowed.iter().any(|a| a == &t.name || a == raw_name)
                });
            }
        }

        platform_tools
    }

    async fn run_turn_internal_simple(
        &self,

        session_id: &str,

        message: MahoChatMessage,

        on_token: Option<Arc<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>>,

        on_event: Option<std::sync::Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static>>,
    ) -> Result<MahoChatMessage, AgentError> {
        let content_str = match &message.content {
            ChatContent::Text(t) => t.clone(),

            ChatContent::Image { .. } => {
                return Err(AgentError::ExecutionError(
                    "Multimodal image content is not supported yet".to_string(),
                ));
            }

            ChatContent::Mixed(_) => {
                return Err(AgentError::ExecutionError(
                    "Multimodal mixed content is not supported yet".to_string(),
                ));
            }
        };

        // Save user message to storage

        self.storage
            .save_message(session_id, &message.role, &content_str)
            .await?;

        // Initialize run and record step_started event in journal
        let run_id = crate::run_journal::AgentRunId::new(session_id);
        {
            let mut j = self.run_journal.lock_unpoison();
            if !j.runs().contains_key(&run_id) {
                let _ = j.start_run(run_id.clone(), session_id);
            }
            let now_ts = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs();
            let _ = j.append_event(
                &run_id,
                "step_started",
                serde_json::json!({
                    "role": message.role,
                    "timestamp": now_ts,
                })
                .to_string(),
            );
        }

        // Fetch active workspace and profile to resolve configuration

        let active_workspace = self.find_active_workspace().await;

        let root_str = self.workspace_root.to_string_lossy().to_string();

        let ws = active_workspace.unwrap_or_else(|| maho_types::ai::AiWorkspace {
            id: "default".to_string(),

            name: "Default Workspace".to_string(),

            profile_id: Some("default".to_string()),

            space_id: None,

            workspace_root: Some(root_str),

            created_at: String::new(),

            updated_at: String::new(),
        });

        let mut profile = None;

        if let Some(ref pid) = ws.profile_id {
            if let Ok(Some(p)) = self.storage.get_profile(pid).await {
                profile = Some(p);
            }
        }

        if profile.is_none() {
            if let Ok(Some(seeded)) = self.storage.get_profile("blank").await {
                profile = Some(seeded);
            }
        }

        let profile = profile.unwrap_or_else(|| maho_types::ai::AiProfile {
            id: "default".to_string(),

            name: "Default Profile".to_string(),

            system_prompt: String::new(),

            preferred_model: None,

            tools: vec![],

            mcp_servers: vec![],

            is_default: true,

            created_at: String::new(),

            updated_at: String::new(),
        });

        let resolved = maho_core::config_resolver::resolve_config(None, &profile, &ws);

        let l1_briefing_content = match self
            .storage
            .get_memory_block(maho_core::memory::L1_BRIEFING_LABEL)
            .await
        {
            Ok(Some(b)) if !b.trim().is_empty() => Some(b),
            _ => None,
        };

        let memory_enabled = !profile
            .tools
            .iter()
            .any(|t| t == "no_memory" || t == "disable_memory");
        let active_l1_briefing = if memory_enabled {
            l1_briefing_content.as_deref()
        } else {
            None
        };

        let initial_composed_prompt = crate::system_prompt::compose_system_prompt_with_briefing(
            &resolved.system_prompt,
            &[],
            active_l1_briefing,
        );

        let _ = self
            .storage
            .set_system_prompt_snapshot_if_absent(session_id, &initial_composed_prompt)
            .await;

        // Determine LLM client

        let llm_override_opt = {
            let lock = self.llm_override.lock_unpoison();

            lock.as_ref().map(|llm| dyn_clone::clone_box(&**llm))
        };

        let llm = self
            .resolve_llm_client(llm_override_opt, &resolved.model)
            .await?;

        // Get history

        let history = self.storage.get_history(session_id).await?;

        let mut swiftide_history = Vec::new();

        // Retrieve system prompt and prepend it.

        // Priority: explicit programmatic override (empty in production) >

        // per-conversation snapshot (frozen at conversational first turn) > current profile.

        let mut memories_str = String::new();

        if let Ok(memories) = self.storage.fts_relevant_memories(&content_str, 10).await {
            if !memories.is_empty() {
                memories_str.push_str("\n\nRetrieved Memories:\n");

                for (_, content, _) in memories {
                    memories_str.push_str(&format!("- {}\n", content));
                }
            }
        }

        let system_prompt_val = {
            let sp = self.system_prompt.lock_unpoison().clone();

            let base = if !sp.is_empty() {
                sp
            } else {
                match self.storage.get_system_prompt_snapshot(session_id).await {
                    Ok(Some(snap)) => snap,

                    _ => initial_composed_prompt.clone(),
                }
            };

            if base.is_empty() && memories_str.is_empty() {
                String::new()
            } else {
                format!("{}{}", base, memories_str)
            }
        };

        if !system_prompt_val.is_empty() {
            swiftide_history.push(SwiftideChatMessage::new_system(system_prompt_val));
        }

        for msg in history {
            let text_content = match &msg.content {
                ChatContent::Text(t) => t.clone(),

                ChatContent::Image { .. } => {
                    return Err(AgentError::ExecutionError(
                        "Multimodal image content is not supported in history".to_string(),
                    ));
                }

                ChatContent::Mixed(_) => {
                    return Err(AgentError::ExecutionError(
                        "Multimodal mixed content is not supported in history".to_string(),
                    ));
                }
            };

            let swiftide_msg = match msg.role.as_str() {
                "system" => SwiftideChatMessage::new_system(text_content),

                "user" => SwiftideChatMessage::new_user(text_content),

                "assistant" => SwiftideChatMessage::new_assistant(Some(text_content), None),

                _ => SwiftideChatMessage::new_user(text_content),
            };

            swiftide_history.push(swiftide_msg);
        }

        // Load DefaultContext with history

        let mut context = swiftide_agents::DefaultContext::default();

        context
            .with_existing_messages(swiftide_history)
            .await
            .map_err(|e| AgentError::ExecutionError(e.to_string()))?;

        // Instantiate Agent Builder

        let mut builder = swiftide_agents::Agent::builder();

        builder.llm(&llm).context(context);

        if on_token.is_some() || on_event.is_some() {
            let on_token_cb = on_token.clone();

            let on_event_cb = on_event.clone();

            builder.on_stream(move |_agent, response| {
                let on_token_cb = on_token_cb.clone();

                let on_event_cb = on_event_cb.clone();

                if let Some(ref delta) = response.delta {
                    if let Some(ref chunk) = delta.message_chunk {
                        if let Some(ref cb) = on_token_cb {
                            cb(chunk);
                        }

                        if let Some(ref cb) = on_event_cb {
                            cb(AgentStreamEvent::Token(chunk.clone()));
                        }
                    }

                    if let Some(ref reasoning) = delta.reasoning_chunk {
                        if let Some(ref cb) = on_event_cb {
                            cb(AgentStreamEvent::Thinking(reasoning.clone()));
                        }
                    }
                }

                Box::pin(async move { Ok(()) })
            });
        }

        let mut agent = builder
            .build()
            .map_err(|e| AgentError::ExecutionError(e.to_string()))?;

        // Simple path has no tools; 600s safety-net only (see run_turn_internal).

        tokio::time::timeout(
            std::time::Duration::from_secs(600),
            agent.query(content_str.clone()),
        )
        .await
        .map_err(|_| AgentError::ExecutionError("Agent query timed out".to_string()))?
        .map_err(|e| AgentError::ExecutionError(e.to_string()))?;

        // Retrieve the last message from the context

        let result_history = agent
            .context()
            .history()
            .await
            .map_err(|e| AgentError::ExecutionError(e.to_string()))?;

        let res = self
            .finalize_turn_answer(session_id, llm.as_ref(), &result_history)
            .await;

        if let Some(ref cb) = on_event {
            cb(AgentStreamEvent::ProofOrReason {
                proof_locator: None,
                reason: Some(
                    "Screenshot/proof capability is not available in current environment"
                        .to_string(),
                ),
            });
        }

        // Record terminal status and durable checkpoint in run journal
        let run_id = crate::run_journal::AgentRunId::new(session_id);
        {
            let mut j = self.run_journal.lock_unpoison();
            let now_ts = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs();
            let (status_str, kind) = match &res {
                Ok(_) => (
                    "completed",
                    crate::run_journal::RunCheckpointKind::StepProgress,
                ),
                Err(_) => ("failed", crate::run_journal::RunCheckpointKind::Failed),
            };
            let _ = j.append_event(
                &run_id,
                "status",
                serde_json::json!({
                    "status": status_str,
                    "timestamp": now_ts,
                })
                .to_string(),
            );
            let last_seq = j
                .get_entries(&run_id)
                .map(|e| e.last().map(|x| x.event_seq).unwrap_or(0))
                .unwrap_or(0);
            let _ = j.record_checkpoint(crate::run_journal::RunCheckpoint {
                run_id: run_id.clone(),
                session_id: session_id.to_string(),
                last_event_seq: last_seq,
                kind,
                classification: crate::run_journal::RestartClassification::Recoverable,
                state_payload: None,
                timestamp: now_ts,
            });
        }

        res
    }
}

struct SessionTurnGuard {
    active_sessions: Arc<Mutex<std::collections::HashMap<String, tokio::task::AbortHandle>>>,
    session_id: String,
    abort_handle: tokio::task::AbortHandle,
    defused: bool,
}

impl SessionTurnGuard {
    fn defuse(mut self) {
        self.defused = true;
    }
}

impl Drop for SessionTurnGuard {
    fn drop(&mut self) {
        if !self.defused {
            self.abort_handle.abort();
            self.active_sessions
                .lock_unpoison()
                .remove(&self.session_id);
        }
    }
}

#[async_trait]

impl AgentRuntime for SwiftideBackend {
    fn attach_session_journal(&self, session_id: &str, storage_path: &std::path::Path) {
        SwiftideBackend::attach_session_journal(self, session_id, storage_path);
    }

    fn set_active_space_id(&self, space_id: Option<String>) {
        SwiftideBackend::set_active_space_id(self, space_id);
    }

    async fn run_turn(
        &self,

        session_id: &str,

        message: MahoChatMessage,

        on_token: Option<Box<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>>,

        on_event: Option<std::sync::Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static>>,
    ) -> Result<MahoChatMessage, AgentError> {
        let start_time = std::time::Instant::now();

        tracing::info!(

            target = "maho_agent::audit",

            session_id = %session_id,

            event = "session_start",

        );

        let self_clone = self.clone();

        let session_id_str = session_id.to_string();

        let denied_flag = Arc::new(Mutex::new(false));

        let denied_flag_clone = denied_flag.clone();

        let on_token_shared: Option<Arc<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>> =
            on_token.map(Arc::from);

        // Turn controller start turn
        {
            let mut controllers = self.turn_controllers.lock_unpoison();
            let controller = controllers
                .entry(session_id_str.clone())
                .or_insert_with(|| {
                    crate::turn_control::SessionTurnController::new(&session_id_str)
                });
            if controller.is_active() {
                return Err(AgentError::SessionBusy(session_id.to_string()));
            }
            let _ = controller.start_turn("turn-main");
        }

        // Session admission fence: reject a concurrent turn for the same
        // session instead of overwriting the in-flight turn's abort handle.
        // The reservation is held until this call's own post-join cleanup, so a
        // cancelled-but-not-yet-joined turn keeps the session reserved.
        let join_handle = {
            let mut sessions = self.active_sessions.lock_unpoison();
            if sessions.contains_key(session_id) {
                return Err(AgentError::SessionBusy(session_id.to_string()));
            }
            let on_token_shared_clone = on_token_shared.clone();
            let join_handle = tokio::spawn(async move {
                let initial_res = self_clone
                    .run_turn_internal(
                        &session_id_str,
                        message,
                        denied_flag_clone.clone(),
                        on_token_shared_clone.clone(),
                        on_event.clone(),
                    )
                    .await;

                // Process any queued follow-up turns FIFO
                let mut last_res = initial_res;
                loop {
                    let next_item = {
                        let mut controllers = self_clone.turn_controllers.lock_unpoison();
                        if let Some(ctrl) = controllers.get_mut(&session_id_str) {
                            ctrl.complete_current_turn()
                        } else {
                            None
                        }
                    };

                    match next_item {
                        Some(queued) => {
                            {
                                let mut controllers = self_clone.turn_controllers.lock_unpoison();
                                if let Some(ctrl) = controllers.get_mut(&session_id_str) {
                                    let _ = ctrl.start_turn(&queued.id);
                                }
                            }
                            let queued_msg =
                                MahoChatMessage::user(ChatContent::text(queued.message));
                            last_res = self_clone
                                .run_turn_internal(
                                    &session_id_str,
                                    queued_msg,
                                    denied_flag_clone.clone(),
                                    on_token_shared_clone.clone(),
                                    on_event.clone(),
                                )
                                .await;
                        }
                        None => break,
                    }
                }
                last_res
            });
            sessions.insert(session_id.to_string(), join_handle.abort_handle());
            join_handle
        };

        let guard = SessionTurnGuard {
            active_sessions: Arc::clone(&self.active_sessions),
            session_id: session_id.to_string(),
            abort_handle: join_handle.abort_handle(),
            defused: false,
        };

        let res = join_handle.await;

        guard.defuse();
        self.active_sessions.lock_unpoison().remove(session_id);
        {
            let mut controllers = self.turn_controllers.lock_unpoison();
            if let Some(ctrl) = controllers.get_mut(session_id) {
                ctrl.complete_current_turn();
            }
        }

        let elapsed = start_time.elapsed();

        tracing::info!(

            target = "maho_agent::audit",

            session_id = %session_id,

            event = "session_end",

            duration_ms = elapsed.as_millis(),

        );

        match res {
            Ok(result) => result,

            Err(e) => {
                if e.is_cancelled() {
                    Err(AgentError::Cancelled)
                } else {
                    Err(AgentError::ExecutionError(e.to_string()))
                }
            }
        }
    }

    async fn run_turn_simple(
        &self,

        session_id: &str,

        message: MahoChatMessage,

        on_token: Option<Box<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>>,

        on_event: Option<std::sync::Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static>>,
    ) -> Result<MahoChatMessage, AgentError> {
        let start_time = std::time::Instant::now();

        tracing::info!(

            target = "maho_agent::audit",

            session_id = %session_id,

            event = "session_start_simple",

        );

        let self_clone = self.clone();

        let session_id_str = session_id.to_string();

        let on_token_shared: Option<Arc<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>> =
            on_token.map(Arc::from);

        // Session admission fence: reject a concurrent turn for the same
        // session instead of overwriting the in-flight turn's abort handle.
        // The reservation is held until this call's own post-join cleanup, so a
        // cancelled-but-not-yet-joined turn keeps the session reserved.
        let join_handle = {
            let mut sessions = self.active_sessions.lock_unpoison();
            if sessions.contains_key(session_id) {
                return Err(AgentError::SessionBusy(session_id.to_string()));
            }
            let join_handle = tokio::spawn(async move {
                self_clone
                    .run_turn_internal_simple(&session_id_str, message, on_token_shared, on_event)
                    .await
            });
            sessions.insert(session_id.to_string(), join_handle.abort_handle());
            join_handle
        };

        let guard = SessionTurnGuard {
            active_sessions: Arc::clone(&self.active_sessions),
            session_id: session_id.to_string(),
            abort_handle: join_handle.abort_handle(),
            defused: false,
        };

        let res = join_handle.await;

        guard.defuse();
        self.active_sessions.lock_unpoison().remove(session_id);

        let elapsed = start_time.elapsed();

        tracing::info!(

            target = "maho_agent::audit",

            session_id = %session_id,

            event = "session_end_simple",

            duration_ms = elapsed.as_millis(),

        );

        match res {
            Ok(result) => result,

            Err(e) => {
                if e.is_cancelled() {
                    Err(AgentError::Cancelled)
                } else {
                    Err(AgentError::ExecutionError(e.to_string()))
                }
            }
        }
    }

    async fn cancel(&self, session_id: &str) -> Result<(), AgentError> {
        // Abort the in-flight turn without releasing its admission reservation:
        // the reservation is cleared by that turn's own post-join cleanup, so a
        // cancelled-but-not-yet-joined session stays fenced and cancel remains
        // idempotent while the turn is still registered.
        let abort_handle = self
            .active_sessions
            .lock_unpoison()
            .get(session_id)
            .cloned();

        if let Some(abort_handle) = abort_handle {
            abort_handle.abort();

            let policy = *self.approval_policy.lock_unpoison();

            emit_audit_record(&ApprovalAuditRecord::permission_cancel(session_id, policy));

            Ok(())
        } else {
            Err(AgentError::SessionNotFound(session_id.to_string()))
        }
    }

    async fn list_tools(&self) -> Result<Vec<ToolDefinition>, AgentError> {
        Ok(self.discover_tools().await)
    }

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
    ) -> Result<(), AgentError> {
        let mut lock = self.permission_callback.lock_unpoison();

        *lock = Some(callback);

        Ok(())
    }

    fn set_secure_storage_callback(
        &self,

        callback: Box<dyn Fn(&str) -> Result<SecureKeyBundle, AgentError> + Send + Sync + 'static>,
    ) -> Result<(), AgentError> {
        let mut lock = self.secure_storage_callback.lock_unpoison();

        *lock = Some(callback);

        Ok(())
    }

    fn set_browser_tool_bridge(
        &self,

        bridge: Arc<dyn BrowserToolBridge>,
    ) -> Result<(), AgentError> {
        let mut lock = self.browser_tool_bridge.lock_unpoison();

        *lock = Some(bridge);

        Ok(())
    }

    fn set_browser_action_tools_state(&self, state: BrowserActionToolsState) {
        SwiftideBackend::set_browser_action_tools_state(self, state);
    }

    fn set_composite_config(&self, config: crate::composite::CompositeExecutionConfig) {
        SwiftideBackend::set_composite_config(self, config);
    }

    fn set_mail_authorization_state(&self, state: crate::MailAuthorizationState) {
        SwiftideBackend::set_mail_authorization_state(self, state);
    }

    fn set_system_prompt(&self, prompt: &str) {
        *self.system_prompt.lock_unpoison() = prompt.to_string();
    }

    fn set_allowed_tools(&self, tools: Option<Vec<String>>) {
        *self.allowed_tools.lock_unpoison() = tools;
    }

    fn set_approval_policy(&self, policy: String) {
        SwiftideBackend::set_approval_policy(self, policy);
    }

    fn set_runtime_tier(&self, tier: Option<String>) {
        SwiftideBackend::set_runtime_tier(self, tier);
    }

    fn runtime_tier(&self) -> Option<String> {
        SwiftideBackend::runtime_tier(self)
    }

    fn set_fs_whitelist_roots(&self, roots: Vec<String>) {
        SwiftideBackend::set_fs_whitelist_roots(self, roots);
    }

    fn fs_whitelist_roots(&self) -> Vec<String> {
        SwiftideBackend::fs_whitelist_roots(self)
    }

    fn set_preferred_provider(&self, provider: Option<String>) {
        SwiftideBackend::set_preferred_provider(self, provider);
    }

    fn set_artifact_root(&self, artifact_root: Option<std::path::PathBuf>) {
        SwiftideBackend::set_artifact_root(self, artifact_root);
    }

    async fn list_artifacts(
        &self,
        session_id: &str,
    ) -> Result<Vec<maho_types::artifact::ArtifactInfo>, AgentError> {
        SwiftideBackend::list_artifacts(self, session_id).await
    }

    async fn artifact_storage_rel_path(
        &self,
        session_id: &str,
        artifact_id: &str,
    ) -> Result<Option<String>, AgentError> {
        SwiftideBackend::artifact_storage_rel_path(self, session_id, artifact_id).await
    }

    fn resolve_interaction(
        &self,
        req_id: &str,
        answer: crate::interaction::InteractionAnswer,
    ) -> Result<(), crate::interaction::InteractionError> {
        SwiftideBackend::resolve_interaction(self, req_id, answer)
    }

    fn timeout_interaction(
        &self,
        req_id: &str,
    ) -> Result<(), crate::interaction::InteractionError> {
        SwiftideBackend::timeout_interaction(self, req_id)
    }

    fn cancel_interaction(&self, req_id: &str) -> Result<(), crate::interaction::InteractionError> {
        SwiftideBackend::cancel_interaction(self, req_id)
    }

    fn interaction_broker(
        &self,
    ) -> std::sync::Arc<std::sync::Mutex<crate::interaction::InteractionBroker>> {
        SwiftideBackend::interaction_broker(self)
    }

    fn run_journal(&self) -> std::sync::Arc<std::sync::Mutex<crate::run_journal::RunJournal>> {
        SwiftideBackend::run_journal(self)
    }

    fn latest_run_id(&self, session_id: &str) -> Option<crate::run_journal::AgentRunId> {
        SwiftideBackend::latest_run_id(self, session_id)
    }
}

use crate::cli_tool_executor::CliToolExecutor;

use crate::mcp_client::McpClient;

#[derive(Clone)]

struct McpToolShim {
    name: String,

    dispatch_name: String,

    description: String,

    parameters_schema: serde_json::Value,

    client: Arc<McpClient>,
}

fn llm_safe_tool_name(name: &str) -> String {
    name.chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || c == '_' {
                c
            } else {
                '_'
            }
        })
        .collect()
}

#[async_trait]

impl swiftide_core::Tool for McpToolShim {
    async fn invoke(
        &self,

        _agent_context: &dyn swiftide_core::AgentContext,

        tool_call: &swiftide_core::chat_completion::ToolCall,
    ) -> anyhow::Result<
        swiftide_core::chat_completion::ToolOutput,
        swiftide_core::chat_completion::errors::ToolError,
    > {
        let args_str = tool_call.args().unwrap_or("{}");

        let args_json: serde_json::Value = serde_json::from_str(args_str)
            .map_err(|e| swiftide_core::chat_completion::errors::ToolError::unknown(e))?;

        let result = self
            .client
            .call_tool(&self.dispatch_name, args_json)
            .await
            .map_err(|e| swiftide_core::chat_completion::errors::ToolError::unknown(e))?;

        Ok(swiftide_core::chat_completion::ToolOutput::Text(
            result.to_string(),
        ))
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed(&self.name)
    }

    fn tool_spec(&self) -> swiftide_core::chat_completion::ToolSpec {
        swiftide_core::chat_completion::ToolSpec {
            name: self.name.clone(),

            description: self.description.clone(),

            parameters_schema: Some(
                serde_json::from_value(self.parameters_schema.clone()).unwrap(),
            ),
        }
    }
}

#[derive(Clone)]

struct OfflineMcpToolShim {
    name: String,

    server_name: String,
}

#[async_trait]

impl swiftide_core::Tool for OfflineMcpToolShim {
    async fn invoke(
        &self,

        _agent_context: &dyn swiftide_core::AgentContext,

        _tool_call: &swiftide_core::chat_completion::ToolCall,
    ) -> anyhow::Result<
        swiftide_core::chat_completion::ToolOutput,
        swiftide_core::chat_completion::errors::ToolError,
    > {
        Err(swiftide_core::chat_completion::errors::ToolError::unknown(
            anyhow::anyhow!(
                "ToolUnavailable: MCP server '{}' is offline, please reconnect.",
                self.server_name
            ),
        ))
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed(&self.name)
    }

    fn tool_spec(&self) -> swiftide_core::chat_completion::ToolSpec {
        swiftide_core::chat_completion::ToolSpec {
            name: self.name.clone(),

            description: format!("Unavailable: MCP server '{}' is offline.", self.server_name),

            parameters_schema: Some(
                serde_json::from_value(serde_json::json!({

                    "type": "object",

                    "properties": {}

                }))
                .unwrap(),
            ),
        }
    }
}

#[derive(Clone)]

struct CliToolShim {
    name: String,

    executor: Arc<CliToolExecutor>,
}

#[async_trait]

impl swiftide_core::Tool for CliToolShim {
    async fn invoke(
        &self,

        _agent_context: &dyn swiftide_core::AgentContext,

        tool_call: &swiftide_core::chat_completion::ToolCall,
    ) -> anyhow::Result<
        swiftide_core::chat_completion::ToolOutput,
        swiftide_core::chat_completion::errors::ToolError,
    > {
        let args_str = tool_call.args().unwrap_or("{}");

        let args_json: serde_json::Value = serde_json::from_str(args_str)
            .map_err(|e| swiftide_core::chat_completion::errors::ToolError::unknown(e))?;

        let result = self
            .executor
            .execute(args_json)
            .await
            .map_err(|e| swiftide_core::chat_completion::errors::ToolError::unknown(e))?;

        Ok(swiftide_core::chat_completion::ToolOutput::Text(
            result.to_string(),
        ))
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed(&self.name)
    }

    fn tool_spec(&self) -> swiftide_core::chat_completion::ToolSpec {
        let tool_config = &self.executor.tool;

        swiftide_core::chat_completion::ToolSpec {
            name: self.name.clone(),

            description: tool_config.description.clone(),

            parameters_schema: Some(
                serde_json::from_value(tool_config.parameters_schema.clone()).unwrap(),
            ),
        }
    }
}

#[cfg(test)]
#[cfg(not(any(target_os = "ios", target_os = "android")))]

mod tests {

    use super::*;

    use std::sync::atomic::{AtomicUsize, Ordering};

    use std::task::Poll;

    fn test_mcp_server(
        transport: maho_types::ai::McpTransport,

        trusted: bool,

        socket_path: Option<String>,
    ) -> maho_types::ai::AiMcpServer {
        maho_types::ai::AiMcpServer {
            id: "srv".to_string(),

            workspace_id: "ws".to_string(),

            name: "browser-dogfood".to_string(),

            transport,

            command: None,

            url: None,

            auth_keychain_id: None,

            trusted,

            trusted_tools: None,

            timeout_ms: 1000,

            output_cap_bytes: 10240,

            socket_path,

            created_at: String::new(),

            updated_at: String::new(),
        }
    }

    #[derive(Clone)]

    struct CountingTool {
        name: &'static str,

        invocations: Arc<AtomicUsize>,

        output: &'static str,
    }

    #[async_trait]

    impl Tool for CountingTool {
        async fn invoke(
            &self,

            _agent_context: &dyn AgentContext,

            _tool_call: &ToolCall,
        ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
            self.invocations.fetch_add(1, Ordering::SeqCst);

            Ok(ToolOutput::Text(self.output.to_string()))
        }

        fn name(&self) -> Cow<'_, str> {
            Cow::Borrowed(self.name)
        }

        fn tool_spec(&self) -> ToolSpec {
            ToolSpec {
                name: self.name.to_string(),

                description: "test tool".to_string(),

                parameters_schema: Some(
                    serde_json::from_value(serde_json::json!({

                        "type": "object",

                        "properties": {}

                    }))
                    .expect("test schema is valid"),
                ),
            }
        }
    }

    fn test_tool_call(name: &str, args: String) -> ToolCall {
        ToolCall::builder()
            .id("call-test".to_string())
            .name(name.to_string())
            .args(args)
            .build()
            .expect("test tool call builds")
    }

    fn gated_counting_tool(
        name: &'static str,

        sensitivity: ToolSensitivity,

        policy: ApprovalPolicy,

        permission_callback: Option<PermissionCallback>,

        invocations: Arc<AtomicUsize>,
    ) -> PermissionGatedTool {
        PermissionGatedTool {
            inner_tool: Arc::new(CountingTool {
                name,

                invocations,

                output: r#"{"ok":true}"#,
            }),

            sensitivity,

            sensitivity_downgrade: Option::None,

            permission_callback: Arc::new(Mutex::new(permission_callback)),

            denied_flag: Arc::new(Mutex::new(false)),

            session_id: "session-permission-test".to_string(),

            approval_policy: Arc::new(Mutex::new(policy)),

            runtime_tier: Arc::new(Mutex::new(None)),

            fs_whitelist_roots: Arc::new(Mutex::new(Vec::new())),

            workspace_root: Arc::new(std::path::PathBuf::from("/")),

            artifact_root: Arc::new(Mutex::new(Option::None)),
        }
    }

    /// Wave 1D: gated tool with a bound runtime tier + whitelist roots, for
    /// driving the dispatch tier gate without a full backend.
    fn gated_counting_tool_with_tier(
        name: &'static str,
        sensitivity: ToolSensitivity,
        policy: ApprovalPolicy,
        permission_callback: Option<PermissionCallback>,
        invocations: Arc<AtomicUsize>,
        runtime_tier: Option<&str>,
        whitelist_roots: Vec<String>,
    ) -> PermissionGatedTool {
        let tool = gated_counting_tool(name, sensitivity, policy, permission_callback, invocations);
        *tool.runtime_tier.lock_unpoison() = runtime_tier.map(str::to_string);
        *tool.fs_whitelist_roots.lock_unpoison() = whitelist_roots;
        tool
    }

    #[tokio::test]
    async fn review_deny_all_cannot_be_overridden_by_tier_prompt() {
        let calls = Arc::new(AtomicUsize::new(0));
        let tool = gated_counting_tool_with_tier(
            "fs_read", ToolSensitivity::ReadOnly, ApprovalPolicy::DenyAll,
            Some(Box::new(|_| Box::pin(async { PermissionDecision::Allow }))),
            calls.clone(), Some("guard"), vec!["/ws/public".into()],
        );
        let result = tool.invoke(&swiftide_agents::DefaultContext::default(),
            &test_tool_call("fs_read", r#"{"path":"/ws/private.txt"}"#.into())).await;
        assert!(result.is_err(), "DenyAll must not allow execution after a tier prompt");
        assert_eq!(calls.load(Ordering::SeqCst), 0);
    }

    #[tokio::test]
    async fn review_read_only_denies_configured_cli_dispatch() {
        let dir = tempfile::tempdir().unwrap();
        let mut tool = gated_counting_tool_with_tier(
            "sh:touch", ToolSensitivity::ReadOnly, ApprovalPolicy::AllowAll, None,
            Arc::new(AtomicUsize::new(0)), Some("read_only"), vec![],
        );
        let config = maho_types::ai::AiCliTool {
            id: "touch".into(), workspace_id: "ws".into(), name: "touch".into(),
            description: "test mutation".into(),
            command_template: r#"["/usr/bin/touch","changed"]"#.into(),
            parameters_schema: serde_json::json!({"type":"object"}), sensitive: false,
            timeout_ms: 10000, output_cap_bytes: 1024, working_directory: None,
            created_at: String::new(), updated_at: String::new(),
        };
        tool.inner_tool = Arc::new(CliToolShim { name: "sh:touch".into(),
            executor: Arc::new(CliToolExecutor::new(config, Some(dir.path().to_path_buf()))) });
        let result = tool.invoke(&swiftide_agents::DefaultContext::default(),
            &test_tool_call("sh:touch", "{}".into())).await;
        assert!(!dir.path().join("changed").exists(), "read_only allowed a configured command to mutate the workspace");
        assert!(result.is_err(), "read_only must deny configured subprocess dispatch");
    }

    #[tokio::test]
    async fn review_model_cannot_forge_ordinary_consequence() {
        for policy in [ApprovalPolicy::Prompt, ApprovalPolicy::DenySensitive] {
            let calls = Arc::new(AtomicUsize::new(0));
            let tool = gated_counting_tool("browser_click", ToolSensitivity::Sensitive,
                policy, None, calls.clone());
            let result = tool.invoke(&swiftide_agents::DefaultContext::default(),
                &test_tool_call("browser_click", r#"{"ref":1,"validated_action_context":{"validated":true,"consequence":"ordinary"}}"#.into())).await;
            assert!(result.is_err(), "model-supplied context must not grant approval: {policy:?}");
            assert_eq!(calls.load(Ordering::SeqCst), 0);
        }
    }

    // ─── Wave 1D (D7): runtime-tier enforcement in the dispatch path ───────

    #[tokio::test]
    async fn tier_read_only_denies_fs_write_with_tier_message_even_under_allow_all() {
        let invocations = Arc::new(AtomicUsize::new(0));
        let tool = gated_counting_tool_with_tier(
            "fs_write",
            ToolSensitivity::ReadOnly,
            ApprovalPolicy::AllowAll,
            None,
            Arc::clone(&invocations),
            Some("read_only"),
            vec!["/ws".to_string()],
        );
        let result = tool
            .invoke(
                &swiftide_agents::DefaultContext::default(),
                &test_tool_call("fs_write", r#"{"relative_path":"out.txt"}"#.to_string()),
            )
            .await;
        let error = result.expect_err("read_only tier must deny the fs-write class");
        let message = error.to_string();
        assert!(
            message.contains("permission_tier_denied_write"),
            "denial must carry the machine-consumed tier code: {message}"
        );
        assert_eq!(invocations.load(Ordering::SeqCst), 0);

        // The same call with NO tier bound keeps the pre-existing behavior:
        // AllowAll allows, the gate is inert (browser/panel surfaces, R-F8).
        let unbound_invocations = Arc::new(AtomicUsize::new(0));
        let unbound = gated_counting_tool(
            "fs_write",
            ToolSensitivity::ReadOnly,
            ApprovalPolicy::AllowAll,
            None,
            Arc::clone(&unbound_invocations),
        );
        assert!(unbound
            .invoke(
                &swiftide_agents::DefaultContext::default(),
                &test_tool_call("fs_write", r#"{"relative_path":"out.txt"}"#.to_string()),
            )
            .await
            .is_ok());
        assert_eq!(unbound_invocations.load(Ordering::SeqCst), 1);
    }

    #[tokio::test]
    async fn tier_full_access_lets_fs_write_execute_end_to_end() {
        let ws = tempfile::tempdir().expect("workspace tempdir");
        let artifacts = ws.path().join(".maho").join("artifacts");
        tokio::fs::create_dir_all(&artifacts)
            .await
            .expect("artifact dir");
        let session_id = "session-tier-full-access";
        let _ = crate::approval_audit::take_test_audit_records_for_session(session_id);
        let tool = PermissionGatedTool {
            inner_tool: Arc::new(FsWriteTool {
                artifact_root: Some(artifacts.clone()),
            }),
            sensitivity: ToolSensitivity::ReadOnly,
            sensitivity_downgrade: Option::None,
            permission_callback: Arc::new(Mutex::new(None)),
            denied_flag: Arc::new(Mutex::new(false)),
            session_id: session_id.to_string(),
            approval_policy: Arc::new(Mutex::new(ApprovalPolicy::AllowAll)),
            runtime_tier: Arc::new(Mutex::new(Some("full_access".to_string()))),
            fs_whitelist_roots: Arc::new(Mutex::new(
                crate::permission::default_fs_whitelist_roots(ws.path()),
            )),
            workspace_root: Arc::new(ws.path().to_path_buf()),
            artifact_root: Arc::new(Mutex::new(Some(artifacts.clone()))),
        };
        let result = tool
            .invoke(
                &swiftide_agents::DefaultContext::default(),
                &test_tool_call(
                    "fs_write",
                    r#"{"relative_path":"out.txt","content":"hello"}"#.to_string(),
                ),
            )
            .await;
        assert!(result.is_ok(), "full_access tier must let fs_write execute");
        assert!(
            artifacts.join("out.txt").exists(),
            "artifact must be written"
        );
    }

    #[tokio::test]
    async fn tier_guard_shell_exec_reaches_the_tty_prompt_callback() {
        // guard + shell_exec on a TTY-equivalent session: the tier gate is
        // inert for the not-file-scoped call and the Prompt policy routes it
        // to the permission callback — the same contract the CLI's TTY
        // y/n prompt implements.
        let invocations = Arc::new(AtomicUsize::new(0));
        let approvals = Arc::new(AtomicUsize::new(0));
        let approval_count = Arc::clone(&approvals);
        let allow_callback: PermissionCallback = Box::new(move |request| {
            assert_eq!(request.tool_name, "shell_exec");
            approval_count.fetch_add(1, Ordering::SeqCst);
            Box::pin(async { PermissionDecision::Allow })
        });
        let tool = gated_counting_tool_with_tier(
            "shell_exec",
            ToolSensitivity::Sensitive,
            ApprovalPolicy::Prompt,
            Some(allow_callback),
            Arc::clone(&invocations),
            Some("guard"),
            vec!["/ws".to_string()],
        );
        let result = tool
            .invoke(
                &swiftide_agents::DefaultContext::default(),
                &test_tool_call("shell_exec", r#"{"command":"ls"}"#.to_string()),
            )
            .await;
        assert!(result.is_ok(), "allowed prompt must execute the call");
        assert_eq!(
            approvals.load(Ordering::SeqCst),
            1,
            "call must prompt first"
        );
        assert_eq!(invocations.load(Ordering::SeqCst), 1);

        // A no answer denies and poisons the turn (prior-denial semantics).
        let denied_invocations = Arc::new(AtomicUsize::new(0));
        let deny_callback: PermissionCallback =
            Box::new(|_request| Box::pin(async { PermissionDecision::Deny }));
        let denied_tool = gated_counting_tool_with_tier(
            "shell_exec",
            ToolSensitivity::Sensitive,
            ApprovalPolicy::Prompt,
            Some(deny_callback),
            Arc::clone(&denied_invocations),
            Some("guard"),
            vec!["/ws".to_string()],
        );
        assert!(denied_tool
            .invoke(
                &swiftide_agents::DefaultContext::default(),
                &test_tool_call("shell_exec", r#"{"command":"ls"}"#.to_string()),
            )
            .await
            .is_err());
        assert_eq!(denied_invocations.load(Ordering::SeqCst), 0);
    }

    #[tokio::test]
    async fn tier_guard_ask_gate_outside_whitelist_overrides_policy_auto_allow() {
        // guard + fs_read outside the whitelist: RequireApproval must reach
        // the callback path even under AllowAll. Headless (no callback) →
        // fail-closed MissingCallback deny; a live prompt decides.
        let invocations = Arc::new(AtomicUsize::new(0));
        let headless = gated_counting_tool_with_tier(
            "fs_read",
            ToolSensitivity::ReadOnly,
            ApprovalPolicy::AllowAll,
            None,
            Arc::clone(&invocations),
            Some("guard"),
            vec!["/ws".to_string()],
        );
        let call = test_tool_call("fs_read", r#"{"path":"/etc/passwd"}"#.to_string());
        let error = headless
            .invoke(&swiftide_agents::DefaultContext::default(), &call)
            .await
            .expect_err("guard ask-gate must not be bypassed by AllowAll headless");
        assert!(
            error
                .to_string()
                .contains("approval decision denied the tool"),
            "headless guard ask-gate must fail closed: {error}"
        );
        assert_eq!(invocations.load(Ordering::SeqCst), 0);

        let prompt_invocations = Arc::new(AtomicUsize::new(0));
        let approvals = Arc::new(AtomicUsize::new(0));
        let approval_count = Arc::clone(&approvals);
        let callback: PermissionCallback = Box::new(move |_request| {
            approval_count.fetch_add(1, Ordering::SeqCst);
            Box::pin(async { PermissionDecision::Allow })
        });
        let prompted = gated_counting_tool_with_tier(
            "fs_read",
            ToolSensitivity::ReadOnly,
            ApprovalPolicy::AllowAll,
            Some(callback),
            Arc::clone(&prompt_invocations),
            Some("guard"),
            vec!["/ws".to_string()],
        );
        assert!(prompted
            .invoke(&swiftide_agents::DefaultContext::default(), &call)
            .await
            .is_ok());
        assert_eq!(
            approvals.load(Ordering::SeqCst),
            1,
            "outside-whitelist must prompt"
        );
        assert_eq!(prompt_invocations.load(Ordering::SeqCst), 1);
    }

    #[tokio::test]
    async fn tier_read_only_denies_out_of_whitelist_fs_read_with_tier_code() {
        let invocations = Arc::new(AtomicUsize::new(0));
        let tool = gated_counting_tool_with_tier(
            "fs_read",
            ToolSensitivity::ReadOnly,
            ApprovalPolicy::AllowAll,
            None,
            Arc::clone(&invocations),
            Some("read_only"),
            vec!["/ws".to_string()],
        );
        let error = tool
            .invoke(
                &swiftide_agents::DefaultContext::default(),
                &test_tool_call("fs_read", r#"{"path":"/etc/passwd"}"#.to_string()),
            )
            .await
            .expect_err("read_only must deny out-of-whitelist reads");
        assert!(
            error.to_string().contains("permission_tier_denied_read"),
            "denial must carry the tier read code"
        );
        assert_eq!(invocations.load(Ordering::SeqCst), 0);

        // In-whitelist reads stay allowed (policy auto-allow).
        let inside_invocations = Arc::new(AtomicUsize::new(0));
        let inside = gated_counting_tool_with_tier(
            "fs_read",
            ToolSensitivity::ReadOnly,
            ApprovalPolicy::AllowAll,
            None,
            Arc::clone(&inside_invocations),
            Some("read_only"),
            vec!["/ws".to_string()],
        );
        assert!(inside
            .invoke(
                &swiftide_agents::DefaultContext::default(),
                &test_tool_call("fs_read", r#"{"path":"/ws/notes.md"}"#.to_string()),
            )
            .await
            .is_ok());
        assert_eq!(inside_invocations.load(Ordering::SeqCst), 1);
    }

    #[derive(Default)]

    struct RecordingBrowserBridge {
        invocations: AtomicUsize,

        calls: Mutex<Vec<(String, String)>>,
    }

    #[async_trait]
    impl BrowserToolBridge for RecordingBrowserBridge {
        async fn execute_tool(
            &self,
            capability_id: &str,
            args_json: &str,
        ) -> Result<crate::BrowserToolExecution, crate::BrowserToolBridgeError> {
            self.invocations.fetch_add(1, Ordering::SeqCst);

            self.calls
                .lock_unpoison()
                .push((capability_id.to_string(), args_json.to_string()));

            Ok(crate::BrowserToolExecution {
                output_json: r#"{"ok":true}"#.to_string(),
                receipt: crate::BrowserToolExecutionReceipt {
                    capability_id: capability_id.to_string(),
                    execution_id: "test-execution".to_string(),
                    metadata: serde_json::json!({}),
                },
            })
        }
    }

    #[test]
    fn browser_tool_error_observation_preserves_permanence_and_retryability() {
        let denied = crate::BrowserToolBridgeError::new(
            "policy_denied",
            "Browser action denied by approval policy",
            false,
        );
        let transport = crate::BrowserToolBridgeError::new(
            "transport_unavailable",
            "Browser callback transport is unavailable",
            true,
        );

        let denied_observation: serde_json::Value = serde_json::from_str(
            &BrowserToolShim::error_observation("browser_click", &denied),
        )
        .expect("denial observation is JSON");
        let transport_observation: serde_json::Value = serde_json::from_str(
            &BrowserToolShim::error_observation("browser_click", &transport),
        )
        .expect("transport observation is JSON");

        assert_eq!(denied_observation["error"]["code"], "policy_denied");
        assert_eq!(denied_observation["error"]["retryable"], false);
        assert_eq!(
            transport_observation["error"]["code"],
            "transport_unavailable"
        );
        assert_eq!(transport_observation["error"]["retryable"], true);
        assert_ne!(denied_observation["hint"], transport_observation["hint"]);
    }

    #[test]

    fn browser_tool_shims_expose_mail_read_tools_with_expected_discovery_contracts() {
        let bridge = Arc::new(RecordingBrowserBridge::default());

        let approval_policy = Arc::new(Mutex::new(ApprovalPolicy::Prompt));

        let context = BrowserToolShimContext::discovery(
            approval_policy,
            Arc::new(Mutex::new(crate::MailAuthorizationState {
                feature_enabled: true,
                helper_ready: true,
                helper_starting: false,
                read_allowed: true,
            })),
        );

        let descriptors = [
            "mail_list_accounts",
            "mail_list_folders",
            "mail_list_emails",
            "mail_get_email",
            "mail_search_emails",
            "mail_list_thread",
        ]
        .into_iter()
        .filter_map(|name| {
            mail_read_tool_metadata(name).map(|metadata| crate::BrowserToolDescriptor {
                capability_id: name.to_string(),
                name: name.to_string(),
                description: metadata.description.to_string(),
                input_schema: metadata.parameters_schema,
                schema_version: 1,
                policy: crate::BrowserToolPolicy {
                    sensitive: false,
                    permission: ToolPermission::AutoApprove,
                },
            })
        })
        .collect();
        let shims = browser_tool_shims(
            Arc::clone(&bridge) as Arc<dyn BrowserToolBridge>,
            descriptors,
            BrowserActionToolsState::Disabled,
            context,
        );

        let names = shims
            .iter()
            .map(|shim| shim.name.as_str())
            .collect::<Vec<_>>();

        for expected in [
            "mail_list_accounts",
            "mail_list_folders",
            "mail_list_emails",
            "mail_get_email",
            "mail_search_emails",
            "mail_list_thread",
        ] {
            assert!(names.contains(&expected), "missing mail shim {expected}");
        }

        let by_name = |name: &str| {
            shims
                .iter()
                .find(|shim| shim.name == name)
                .unwrap_or_else(|| panic!("missing shim {name}"))
        };

        let accounts = by_name("mail_list_accounts");

        assert_eq!(accounts.sensitivity, ToolSensitivity::ReadOnly);

        assert_eq!(
            accounts.tool_definition().provenance,
            ToolProvenance::BuiltinBrowser
        );

        assert!(!accounts.tool_definition().sensitive);

        assert_eq!(
            accounts.tool_definition().permission,
            ToolPermission::AutoApprove
        );

        assert_eq!(
            accounts.parameters_schema,
            serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {},

                "required": []

            })
        );

        let folders = by_name("mail_list_folders");

        assert_eq!(
            folders.parameters_schema,
            serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "account_id": {"type": "string", "description": "Mail account id."}

                },

                "required": ["account_id"]

            })
        );

        let emails = by_name("mail_list_emails");

        assert_eq!(
            emails.parameters_schema,
            serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "account_id": {"type": "string", "description": "Mail account id."},

                    "folder_id": {"type": "string", "description": "Mail folder id."},

                    "limit": {"type": "integer", "description": "Maximum emails to return; default 50."},

                    "offset": {"type": "integer", "description": "Pagination offset; default 0."}

                },

                "required": ["account_id", "folder_id"]

            })
        );

        let email = by_name("mail_get_email");

        assert_eq!(
            email.parameters_schema,
            serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "email_id": {"type": "string", "description": "Email id."}

                },

                "required": ["email_id"]

            })
        );

        let search = by_name("mail_search_emails");

        assert_eq!(
            search.parameters_schema,
            serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "query_json": {"type": "string", "description": "JSON search query accepted by the Mail search broker."}

                },

                "required": ["query_json"]

            })
        );

        let thread = by_name("mail_list_thread");

        assert_eq!(
            thread.parameters_schema,
            serde_json::json!({

                "type": "object",

                "additionalProperties": false,

                "properties": {

                    "account_id": {"type": "string", "description": "Mail account id."},

                    "message_id": {"type": "string", "description": "RFC message id for the thread."}

                },

                "required": ["account_id", "message_id"]

            })
        );
    }

    #[tokio::test]

    async fn mail_read_browser_tool_shim_forwards_name_and_arguments_to_bridge() {
        let bridge = Arc::new(RecordingBrowserBridge::default());

        let shim = BrowserToolShim {
            capability_id: "mail_search_emails".to_string(),

            name: "mail_search_emails".to_string(),

            description: "Search emails.".to_string(),

            parameters_schema: mail_read_tool_metadata("mail_search_emails")
                .expect("mail metadata exists")
                .parameters_schema,

            sensitivity: ToolSensitivity::ReadOnly,

            permission: ToolPermission::AutoApprove,

            bridge: Arc::clone(&bridge) as Arc<dyn BrowserToolBridge>,

            context: BrowserToolShimContext::turn(
                "session-mail-bridge-forward",
                Arc::new(Mutex::new(ApprovalPolicy::Prompt)),
                Arc::new(Mutex::new(FallbackLadderState::new(
                    "session-mail-bridge-forward",
                ))),
                Arc::new(Mutex::new(crate::MailAuthorizationState {
                    feature_enabled: true,
                    helper_ready: true,
                    helper_starting: false,
                    read_allowed: true,
                })),
            ),
        };

        let tool_call = test_tool_call(
            "mail_search_emails",
            r#"{"query_json":"{\"query\":\"receipt\",\"limit\":7}"}"#.to_string(),
        );

        let context = swiftide_agents::DefaultContext::default();

        let result = shim.invoke(&context, &tool_call).await;

        assert!(result.is_ok(), "mail read shim should forward to bridge");

        assert_eq!(bridge.invocations.load(Ordering::SeqCst), 1);

        let calls = bridge.calls.lock_unpoison().clone();

        assert_eq!(calls.len(), 1);

        assert_eq!(calls[0].0, "mail_search_emails");

        assert_eq!(
            calls[0].1,
            r#"{"query_json":"{\"query\":\"receipt\",\"limit\":7}"}"#
        );
    }

    struct RevokingMailBridge {
        state: Arc<Mutex<crate::MailAuthorizationState>>,
    }

    #[async_trait]
    impl BrowserToolBridge for RevokingMailBridge {
        async fn execute_tool(
            &self,
            capability_id: &str,
            _args_json: &str,
        ) -> Result<crate::BrowserToolExecution, crate::BrowserToolBridgeError> {
            self.state.lock_unpoison().read_allowed = false;
            Ok(crate::BrowserToolExecution {
                output_json: r#"{"subject":"must-not-leak"}"#.to_string(),
                receipt: crate::BrowserToolExecutionReceipt {
                    capability_id: capability_id.to_string(),
                    execution_id: "revoked-read".to_string(),
                    metadata: serde_json::json!({}),
                },
            })
        }
    }

    #[tokio::test]
    async fn mail_read_revocation_during_dispatch_discards_backend_result() {
        let state = Arc::new(Mutex::new(crate::MailAuthorizationState {
            feature_enabled: true,
            helper_ready: true,
            helper_starting: false,
            read_allowed: true,
        }));
        let shim = BrowserToolShim {
            capability_id: "mail_get_email".to_string(),
            name: "mail_get_email".to_string(),
            description: "Get email".to_string(),
            parameters_schema: serde_json::json!({"type":"object"}),
            sensitivity: ToolSensitivity::ReadOnly,
            permission: ToolPermission::AutoApprove,
            bridge: Arc::new(RevokingMailBridge {
                state: Arc::clone(&state),
            }),
            context: BrowserToolShimContext::turn(
                "mail-revocation",
                Arc::new(Mutex::new(ApprovalPolicy::AllowAll)),
                Arc::new(Mutex::new(FallbackLadderState::new("mail-revocation"))),
                state,
            ),
        };
        let result = shim
            .invoke(
                &swiftide_agents::DefaultContext::default(),
                &test_tool_call("mail_get_email", r#"{"email_id":"e1"}"#.to_string()),
            )
            .await;
        let error = result.expect_err("revoked read must be denied").to_string();
        assert!(error.contains("mail_read_consent_required"));
        assert!(!error.contains("must-not-leak"));
    }

    #[tokio::test]
    async fn mail_write_global_allow_still_emits_typed_approval_request() {
        let invocations = Arc::new(AtomicUsize::new(0));
        let approvals = Arc::new(AtomicUsize::new(0));
        let approval_count = Arc::clone(&approvals);
        let callback: PermissionCallback = Box::new(move |request| {
            assert_eq!(request.tool_name, "mail_send");
            assert_eq!(request.sensitivity, ToolSensitivity::Sensitive);
            assert_eq!(request.arguments, r#""[REDACTED]""#);
            approval_count.fetch_add(1, Ordering::SeqCst);
            Box::pin(async { PermissionDecision::Allow })
        });
        let tool = gated_counting_tool(
            "mail_send",
            ToolSensitivity::Sensitive,
            ApprovalPolicy::AllowAll,
            Some(callback),
            Arc::clone(&invocations),
        );
        let result = tool
            .invoke(
                &swiftide_agents::DefaultContext::default(),
                &test_tool_call(
                    "mail_send",
                    r#"{"body_text":"private message"}"#.to_string(),
                ),
            )
            .await;
        assert!(result.is_ok());
        assert_eq!(approvals.load(Ordering::SeqCst), 1);
        assert_eq!(invocations.load(Ordering::SeqCst), 1);
    }

    #[tokio::test]

    async fn browser_action_shims_audit_records_action_sensitivity() {
        let session_id = "session-browser-action-audit";

        let _ = crate::approval_audit::take_test_audit_records_for_session(session_id);

        let contract = canonical_browser_action_contracts()
            .expect("canonical browser action contract parses")
            .into_iter()
            .find(|contract| contract.tool_name == "browser_scroll")
            .expect("browser_scroll action contract exists");

        let bridge = Arc::new(RecordingBrowserBridge::default());

        let callback: PermissionCallback =
            Box::new(move |_request| Box::pin(async move { PermissionDecision::Allow }));

        let shim_context = BrowserToolShimContext::turn(
            session_id,
            Arc::new(Mutex::new(ApprovalPolicy::Prompt)),
            Arc::new(Mutex::new(FallbackLadderState::new(session_id))),
            Arc::new(Mutex::new(crate::MailAuthorizationState::default())),
        );

        let shim = browser_action_shim(
            contract,
            Arc::clone(&bridge) as Arc<dyn BrowserToolBridge>,
            shim_context,
        );

        let tool = PermissionGatedTool {
            inner_tool: Arc::new(shim),

            sensitivity: ToolSensitivity::Sensitive,

            sensitivity_downgrade: Option::None,

            permission_callback: Arc::new(Mutex::new(Some(callback))),

            denied_flag: Arc::new(Mutex::new(false)),

            session_id: session_id.to_string(),

            approval_policy: Arc::new(Mutex::new(ApprovalPolicy::Prompt)),

            runtime_tier: Arc::new(Mutex::new(None)),

            fs_whitelist_roots: Arc::new(Mutex::new(Vec::new())),

            workspace_root: Arc::new(std::path::PathBuf::from("/")),

            artifact_root: Arc::new(Mutex::new(Option::None)),
        };

        let tool_call = test_tool_call(
            "browser_scroll",
            r#"{"direction":"down","pixels":200}"#.to_string(),
        );

        let context = swiftide_agents::DefaultContext::default();

        let result = tool.invoke(&context, &tool_call).await;

        assert!(result.is_ok(), "allowed browser action should execute");

        // browser_scroll is a mutation: the shim first resolves a leaseable tab
        // (browser_tab_list) then dispatches the scroll. With this bare mock no
        // targetable tab is found, so no lease is acquired but dispatch still
        // proceeds -- two bridge invocations: tab-list resolution + scroll.
        assert_eq!(bridge.invocations.load(Ordering::SeqCst), 2);

        let records = crate::approval_audit::take_test_audit_records_for_session(session_id);

        let sensitive_records = records
            .iter()
            .filter(|record| {
                matches!(
                    record.kind,
                    ApprovalAuditEventKind::PermissionRequest
                        | ApprovalAuditEventKind::PermissionAllow
                        | ApprovalAuditEventKind::ToolExecutionResult
                )
            })
            .collect::<Vec<_>>();

        assert_eq!(sensitive_records.len(), 3);

        for record in sensitive_records {
            assert_eq!(record.tool_name, "browser_scroll");

            assert_eq!(record.sensitivity, ToolSensitivity::Sensitive);
        }
    }

    #[tokio::test]

    async fn typed_approval_sensitive_request_carries_sanitized_args_and_metadata() {
        let invocations = Arc::new(AtomicUsize::new(0));

        let captured = Arc::new(Mutex::new(None::<PermissionRequest>));

        let captured_request = Arc::clone(&captured);

        let callback: PermissionCallback = Box::new(move |request| {
            let captured_request = Arc::clone(&captured_request);

            Box::pin(async move {
                *captured_request.lock_unpoison() = Some(request);

                PermissionDecision::Allow
            })
        });

        let tool = gated_counting_tool(
            "browser_type",
            ToolSensitivity::Sensitive,
            ApprovalPolicy::Prompt,
            Some(callback),
            Arc::clone(&invocations),
        );

        let sentinel = "S3NTINEL-maho-vault-9F4C";

        let tool_call = test_tool_call(
            "browser_type",
            format!(r#"{{"text":"hello","password":"{sentinel}"}}"#),
        );

        let context = swiftide_agents::DefaultContext::default();

        let result = tool.invoke(&context, &tool_call).await;

        assert!(
            result.is_ok(),
            "allowed sensitive tool should execute: {result:?}"
        );

        assert_eq!(invocations.load(Ordering::SeqCst), 1);

        let request = captured
            .lock_unpoison()
            .clone()
            .expect("permission callback captures request");

        assert_eq!(request.tool_name, "browser_type");

        assert_eq!(request.sensitivity, ToolSensitivity::Sensitive);

        assert!(!request.arguments.contains(sentinel));

        assert!(request.arguments.contains("[REDACTED]"));
    }

    #[tokio::test]

    async fn permission_missing_callback_denies_sensitive_tool_fail_closed() {
        let invocations = Arc::new(AtomicUsize::new(0));

        let tool = gated_counting_tool(
            "browser_click",
            ToolSensitivity::Sensitive,
            ApprovalPolicy::Prompt,
            None,
            Arc::clone(&invocations),
        );

        let tool_call = test_tool_call("browser_click", r#"{"ref":1}"#.to_string());

        let context = swiftide_agents::DefaultContext::default();

        let result = tool.invoke(&context, &tool_call).await;

        assert!(
            result.is_err(),
            "missing callback must deny sensitive tools"
        );

        assert_eq!(invocations.load(Ordering::SeqCst), 0);

        assert!(*tool.denied_flag.lock_unpoison());
    }

    #[tokio::test]

    async fn permission_cancel_audit_emits_when_invoke_future_is_dropped() {
        let session_id = "session-permission-test";

        let _ = crate::approval_audit::take_test_audit_records_for_session(session_id);

        let invocations = Arc::new(AtomicUsize::new(0));

        let callback: PermissionCallback =
            Box::new(move |_request| Box::pin(std::future::pending::<PermissionDecision>()));

        let tool = gated_counting_tool(
            "browser_type",
            ToolSensitivity::Sensitive,
            ApprovalPolicy::Prompt,
            Some(callback),
            Arc::clone(&invocations),
        );

        let sentinel = "S3NTINEL-maho-vault-9F4C";

        let tool_call = test_tool_call(
            "browser_type",
            format!(r#"{{"text":"hello","password":"{sentinel}"}}"#),
        );

        let context = swiftide_agents::DefaultContext::default();

        let mut future = tool.invoke(&context, &tool_call);

        assert!(matches!(futures::poll!(future.as_mut()), Poll::Pending));

        drop(future);

        let records = crate::approval_audit::take_test_audit_records_for_session(session_id);

        let cancel = records
            .iter()
            .find(|record| {
                record.kind == ApprovalAuditEventKind::PermissionCancel
                    && record.session_id == session_id
                    && record.tool_name == "browser_type"
            })
            .expect("dropping an in-flight permission-gated invoke future emits PermissionCancel");

        assert_eq!(cancel.sensitivity, ToolSensitivity::Sensitive);

        assert_eq!(cancel.policy, ApprovalPolicy::Prompt);

        assert_eq!(cancel.decision, Some(PermissionDecision::Deny));

        let metadata = cancel
            .decision_metadata
            .expect("cancel audit carries runtime metadata");

        assert_eq!(metadata.source, ApprovalDecisionSource::Runtime);

        assert_eq!(metadata.reason, ApprovalDecisionReason::SessionCancel);

        assert_eq!(invocations.load(Ordering::SeqCst), 0);

        let json = serde_json::to_string(cancel).expect("cancel audit record serializes");

        assert!(!json.contains(sentinel));
    }

    #[tokio::test]

    async fn permission_downgrade_audit_emits_on_invocation_before_decision() {
        let session_id = "session-downgrade-invoke";

        let _ = crate::approval_audit::take_test_audit_records_for_session(session_id);

        let invocations = Arc::new(AtomicUsize::new(0));

        let tool = PermissionGatedTool {
            inner_tool: Arc::new(CountingTool {
                name: "browser_accessibility_snapshot",

                invocations: Arc::clone(&invocations),

                output: r#"{"snapshot":true}"#,
            }),

            sensitivity: ToolSensitivity::ReadOnly,

            sensitivity_downgrade: Some(ToolSensitivityDowngrade {
                from: ToolSensitivity::Sensitive,

                to: ToolSensitivity::ReadOnly,
            }),

            permission_callback: Arc::new(Mutex::new(None)),

            denied_flag: Arc::new(Mutex::new(false)),

            runtime_tier: Arc::new(Mutex::new(None)),

            fs_whitelist_roots: Arc::new(Mutex::new(Vec::new())),

            workspace_root: Arc::new(std::path::PathBuf::from("/")),

            artifact_root: Arc::new(Mutex::new(Option::None)),
            session_id: session_id.to_string(),

            approval_policy: Arc::new(Mutex::new(ApprovalPolicy::Prompt)),
        };

        let sentinel = "S3NTINEL-maho-vault-9F4C";

        let tool_call = test_tool_call(
            "browser_accessibility_snapshot",
            format!(r#"{{"password":"{sentinel}"}}"#),
        );

        let context = swiftide_agents::DefaultContext::default();

        let result = tool.invoke(&context, &tool_call).await;

        assert!(result.is_ok(), "read-only downgraded tool should execute");

        assert_eq!(invocations.load(Ordering::SeqCst), 1);

        let records = crate::approval_audit::take_test_audit_records_for_session(session_id);

        let downgrade_index = records
            .iter()
            .position(|record| record.kind == ApprovalAuditEventKind::PermissionDowngrade)
            .expect("downgrade audit record is emitted");

        let request_index = records
            .iter()
            .position(|record| record.kind == ApprovalAuditEventKind::PermissionRequest)
            .expect("permission request audit record is emitted");

        let allow_index = records
            .iter()
            .position(|record| record.kind == ApprovalAuditEventKind::PermissionAllow)
            .expect("permission allow audit record is emitted");

        assert!(downgrade_index < request_index);

        assert!(downgrade_index < allow_index);

        let downgrade = &records[downgrade_index];

        assert_eq!(downgrade.tool_name, "browser_accessibility_snapshot");

        assert_eq!(downgrade.session_id, "session-downgrade-invoke");

        assert_eq!(downgrade.sensitivity, ToolSensitivity::ReadOnly);

        assert_eq!(
            downgrade.attempted_sensitivity,
            Some(ToolSensitivity::Sensitive)
        );

        assert_eq!(
            downgrade.selected_sensitivity,
            Some(ToolSensitivity::ReadOnly)
        );

        assert_eq!(downgrade.decision, Option::None);

        let json = serde_json::to_string(&records).expect("audit records serialize");

        assert!(!json.contains(sentinel));
    }

    #[tokio::test]

    async fn permission_read_only_auto_runs_only_when_policy_allows() {
        let context = swiftide_agents::DefaultContext::default();

        let allowed_invocations = Arc::new(AtomicUsize::new(0));

        let allowed_tool = gated_counting_tool(
            "fs_read",
            ToolSensitivity::ReadOnly,
            ApprovalPolicy::Prompt,
            None,
            Arc::clone(&allowed_invocations),
        );

        let tool_call = test_tool_call("fs_read", r#"{"path":"Cargo.toml"}"#.to_string());

        let allowed = allowed_tool.invoke(&context, &tool_call).await;

        assert!(
            allowed.is_ok(),
            "read-only tools auto-run under prompt policy"
        );

        assert_eq!(allowed_invocations.load(Ordering::SeqCst), 1);

        let denied_invocations = Arc::new(AtomicUsize::new(0));

        let denied_tool = gated_counting_tool(
            "fs_read",
            ToolSensitivity::ReadOnly,
            ApprovalPolicy::DenyAll,
            None,
            Arc::clone(&denied_invocations),
        );

        let denied = denied_tool.invoke(&context, &tool_call).await;

        assert!(denied.is_err(), "deny-all blocks even read-only tools");

        assert_eq!(denied_invocations.load(Ordering::SeqCst), 0);
    }

    #[test]

    fn navigate_fallback_matches_both_no_eligible_tab_phrasings() {
        assert!(BrowserToolShim::should_open_tab_instead(
            "browser_navigate",
            "rpc error -32005: no eligible active tab"
        ));

        assert!(BrowserToolShim::should_open_tab_instead(
            "browser_navigate",
            "no eligible target tab for `browser_navigate`"
        ));

        assert!(!BrowserToolShim::should_open_tab_instead(
            "browser_click",
            "no eligible target tab for `browser_click`"
        ));

        assert!(!BrowserToolShim::should_open_tab_instead(
            "browser_navigate",
            "lease conflict"
        ));
    }

    #[test]

    fn internal_control_tools_are_filtered_from_ui_events() {
        assert!(is_internal_control_tool("stop"));

        assert!(is_internal_control_tool("task_failed"));

        assert!(!is_internal_control_tool("clipboard"));

        assert!(!is_internal_control_tool("camera"));

        assert!(!is_internal_control_tool("photo_library"));

        assert!(!is_internal_control_tool("Stop"));
    }

    #[test]

    fn default_browser_uds_is_connectable_without_trusted_flag() {
        let socket_path = maho_browser_mcp::paths::default_socket_path()
            .to_string_lossy()
            .to_string();

        let server = test_mcp_server(maho_types::ai::McpTransport::Uds, false, Some(socket_path));

        assert!(
            should_attempt_mcp_connect(&server),
            "the browser dogfood UDS socket must bypass the external-server trust gate"
        );
    }

    #[test]

    fn arbitrary_untrusted_uds_remains_blocked_before_connect() {
        let server = test_mcp_server(
            maho_types::ai::McpTransport::Uds,
            false,
            Some("/tmp/not-the-maho-browser.sock".to_string()),
        );

        assert!(!should_attempt_mcp_connect(&server));
    }

    #[test]

    fn untrusted_stdio_remains_blocked_before_connect() {
        let server = test_mcp_server(maho_types::ai::McpTransport::Stdio, false, None);

        assert!(!should_attempt_mcp_connect(&server));
    }

    #[test]

    fn dogfood_readonly_tool_auto_runs() {
        assert!(
            !mcp_tool_is_sensitive(
                false,
                "browser_tab_list",
                "mcp:browser/browser_tab_list",
                false,
                None,
            ),
            "a read-only dogfood tool must be non-sensitive (auto-run)"
        );
    }

    #[test]

    fn dogfood_sensitive_action_stays_gated() {
        for name in [
            "browser_click",
            "browser_type",
            "browser_navigate",
            "browser_tab_new",
        ] {
            assert!(
                mcp_tool_is_sensitive(true, name, &format!("mcp:browser/{name}"), false, None,),
                "{name}: a sensitive dogfood action must stay sensitive so the callback gates it"
            );
        }
    }

    #[test]

    fn explicit_server_trust_still_trusts_any_tool() {
        assert!(
            !mcp_tool_is_sensitive(
                true,
                "browser_click",
                "mcp:browser/browser_click",
                true,
                None,
            ),
            "an explicitly trusted server (trusted_tools=None) trusts all its tools"
        );

        assert!(
            !mcp_tool_is_sensitive(
                true,
                "custom_tool",
                "mcp:ext/custom_tool",
                true,
                Some(&vec!["custom_tool".to_string()]),
            ),
            "a tool explicitly listed in trusted_tools is trusted"
        );

        assert!(
            mcp_tool_is_sensitive(
                true,
                "other_tool",
                "mcp:ext/other_tool",
                true,
                Some(&vec!["custom_tool".to_string()]),
            ),
            "a tool NOT in trusted_tools remains sensitive"
        );
    }

    #[test]

    fn non_dogfood_sensitive_tool_is_gated() {
        assert!(
            mcp_tool_is_sensitive(
                true,
                "browser_tab_list",
                "mcp:ext/browser_tab_list",
                false,
                None,
            ),
            "an untrusted (non-dogfood) server's tools stay sensitive even if read-only-named"
        );
    }

    #[tokio::test]

    async fn test_fsread_size_cap_rejection() {
        let temp_dir = tempfile::tempdir().unwrap();

        let big_path = temp_dir.path().join("big.txt");

        let big_content = vec![b'X'; (FS_READ_MAX_BYTES as usize) + 1];

        tokio::fs::write(&big_path, &big_content).await.unwrap();

        let tool = FsReadTool {
            workspace_root: temp_dir.path().to_path_buf(),
        };

        let tool_call = ToolCall::builder()
            .id("test-1".to_string())
            .name("fs_read".to_string())
            .args(format!("{{\"path\": \"{}\"}}", big_path.display()))
            .build()
            .unwrap();

        let context = swiftide_agents::DefaultContext::default();

        let result = tool.invoke(&context, &tool_call).await;

        let err = result.expect_err("fs_read should reject files larger than FS_READ_MAX_BYTES");

        let msg = err.to_string();

        assert!(
            msg.contains("File too large"),
            "Error should mention size cap; got: {msg}"
        );
    }

    #[tokio::test]

    async fn test_fsread_within_size_cap_succeeds() {
        let temp_dir = tempfile::tempdir().unwrap();

        let small_path = temp_dir.path().join("small.txt");

        tokio::fs::write(&small_path, b"hello world").await.unwrap();

        let tool = FsReadTool {
            workspace_root: temp_dir.path().to_path_buf(),
        };

        let tool_call = ToolCall::builder()
            .id("test-2".to_string())
            .name("fs_read".to_string())
            .args(format!("{{\"path\": \"{}\"}}", small_path.display()))
            .build()
            .unwrap();

        let context = swiftide_agents::DefaultContext::default();

        let result = tool.invoke(&context, &tool_call).await;

        let output = result.expect("fs_read should succeed for files under the cap");

        let text = match output {
            ToolOutput::Text(t) => t,

            other => panic!("Expected text output; got {other:?}"),
        };

        assert_eq!(text, "hello world");
    }

    fn fs_write_call(relative_path: &str, content: &str, mime_type: Option<&str>) -> ToolCall {
        let mut args = serde_json::json!({
            "relative_path": relative_path,
            "content": content,
        });
        if let Some(mime_type) = mime_type {
            args["mime_type"] = serde_json::Value::String(mime_type.to_string());
        }
        ToolCall::builder()
            .id("fs-write-test".to_string())
            .name("fs_write".to_string())
            .args(args.to_string())
            .build()
            .unwrap()
    }

    async fn invoke_fs_write(
        tool: &FsWriteTool,
        relative_path: &str,
        content: &str,
        mime_type: Option<&str>,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        let context = swiftide_agents::DefaultContext::default();
        tool.invoke(&context, &fs_write_call(relative_path, content, mime_type))
            .await
    }

    #[tokio::test]
    async fn fs_write_rejects_path_traversal_without_writing_outside_root() {
        let container = tempfile::tempdir().unwrap();
        let root = container.path().join("artifacts");
        tokio::fs::create_dir(&root).await.unwrap();
        let tool = FsWriteTool {
            artifact_root: Some(root),
        };

        let result = invoke_fs_write(&tool, "../evil.txt", "blocked", None).await;

        assert!(result.is_err());
        assert!(!container.path().join("evil.txt").exists());
    }

    #[tokio::test]
    async fn fs_write_rejects_absolute_empty_and_unconfigured_paths() {
        let root = tempfile::tempdir().unwrap();
        let configured = FsWriteTool {
            artifact_root: Some(root.path().to_path_buf()),
        };
        let absolute = root.path().join("absolute.txt");

        assert!(
            invoke_fs_write(&configured, absolute.to_string_lossy().as_ref(), "x", None)
                .await
                .is_err()
        );
        assert!(invoke_fs_write(&configured, "", "x", None).await.is_err());
        assert!(invoke_fs_write(
            &FsWriteTool {
                artifact_root: None,
            },
            "file.txt",
            "x",
            None,
        )
        .await
        .is_err());
        assert!(!absolute.exists());
    }

    #[cfg(unix)]
    #[tokio::test]
    async fn fs_write_rejects_symlink_parent_escape() {
        use std::os::unix::fs::symlink;

        let container = tempfile::tempdir().unwrap();
        let root = container.path().join("artifacts");
        let outside = container.path().join("outside");
        tokio::fs::create_dir(&root).await.unwrap();
        tokio::fs::create_dir(&outside).await.unwrap();
        symlink(&outside, root.join("escape")).unwrap();
        let tool = FsWriteTool {
            artifact_root: Some(root),
        };

        let result = invoke_fs_write(&tool, "escape/evil.txt", "blocked", None).await;

        assert!(result.is_err());
        assert!(!outside.join("evil.txt").exists());
    }

    #[tokio::test]
    async fn fs_write_writes_unicode_path_and_returns_structured_output() {
        let root = tempfile::tempdir().unwrap();
        let tool = FsWriteTool {
            artifact_root: Some(root.path().to_path_buf()),
        };

        let output = invoke_fs_write(&tool, "보고서.txt", "hello", Some("text/plain"))
            .await
            .expect("fs_write should accept a contained Unicode filename");
        let text = match output {
            ToolOutput::Text(text) => text,
            other => panic!("expected text output, got {other:?}"),
        };
        let value: serde_json::Value = serde_json::from_str(&text).unwrap();

        assert_eq!(value["relative_path"], "보고서.txt");
        assert_eq!(value["mime_type"], "text/plain");
        assert_eq!(value["size_bytes"], 5);
        assert_eq!(value["display_name"], "보고서.txt");
        assert_eq!(
            tokio::fs::read(root.path().join("보고서.txt"))
                .await
                .unwrap(),
            b"hello"
        );
    }

    #[test]
    fn build_after_tool_events_orders_artifact_before_tool_result() {
        let raw = serde_json::json!({
            "relative_path": "report.txt",
            "mime_type": "text/plain",
            "size_bytes": 5u64,
            "display_name": "report.txt",
        })
        .to_string();
        let (events, artifact) = build_after_tool_events(
            "session-1",
            "tc-1".to_string(),
            "fs_write".to_string(),
            true,
            &raw,
        );
        assert_eq!(events.len(), 2, "expected ArtifactCreated + ToolResult");
        match &events[0] {
            AgentStreamEvent::ArtifactCreated { artifact } => {
                assert_eq!(artifact.session_id, "session-1");
                assert_eq!(artifact.display_name, "report.txt");
                assert_eq!(artifact.mime_type, "text/plain");
                assert_eq!(artifact.size_bytes, 5);
                assert_eq!(artifact.storage_rel_path, "report.txt");
                assert!(!artifact.artifact_id.is_empty());
            }
            other => panic!("expected ArtifactCreated first, got {other:?}"),
        }
        match &events[1] {
            AgentStreamEvent::ToolResult { name, .. } => assert_eq!(name, "fs_write"),
            other => panic!("expected ToolResult second, got {other:?}"),
        }
        assert_eq!(
            artifact.expect("artifact to persist").storage_rel_path,
            "report.txt"
        );
    }

    #[test]
    fn build_after_tool_events_skips_malformed_fs_write() {
        let (events, artifact) = build_after_tool_events(
            "session-1",
            "tc-2".to_string(),
            "fs_write".to_string(),
            true,
            "not-json{{",
        );
        assert_eq!(events.len(), 1, "malformed output emits only ToolResult");
        assert!(matches!(events[0], AgentStreamEvent::ToolResult { .. }));
        assert!(artifact.is_none());
    }

    #[test]
    fn build_after_tool_events_ignores_non_fs_write_and_failures() {
        let ok_json = serde_json::json!({
            "relative_path": "x",
            "mime_type": "text/plain",
            "size_bytes": 1u64,
            "display_name": "x",
        })
        .to_string();
        let (events, artifact) = build_after_tool_events(
            "s",
            "tc".to_string(),
            "http_get".to_string(),
            true,
            &ok_json,
        );
        assert_eq!(events.len(), 1);
        assert!(artifact.is_none());
        let (events, artifact) = build_after_tool_events(
            "s",
            "tc".to_string(),
            "fs_write".to_string(),
            false,
            "\"boom\"",
        );
        assert_eq!(events.len(), 1);
        assert!(artifact.is_none());
        match &events[0] {
            AgentStreamEvent::ToolResult { result, .. } => assert!(result.contains("boom")),
            other => panic!("expected ToolResult, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn backend_lists_and_resolves_artifacts_by_id() {
        use std::sync::Arc;
        let sqlite = maho_storage::sqlite::SqliteStorage::open_in_memory_with_key("todo4-key")
            .expect("in-memory sqlite");
        let storage = Arc::new(crate::MutexAgentStorage(Arc::new(std::sync::Mutex::new(
            sqlite,
        ))));
        let a1 = maho_types::artifact::ArtifactInfo {
            artifact_id: "art-1".to_string(),
            session_id: "sess-A".to_string(),
            display_name: "a.txt".to_string(),
            mime_type: "text/plain".to_string(),
            size_bytes: 3,
            storage_rel_path: "sess-A/a.txt".to_string(),
            created_at_ms: 1,
            kind: None,
        };
        let a2 = maho_types::artifact::ArtifactInfo {
            artifact_id: "art-2".to_string(),
            storage_rel_path: "sess-A/b.txt".to_string(),
            created_at_ms: 2,
            ..a1.clone()
        };
        {
            use crate::AgentStorage;
            storage.save_artifact(&a1).await.unwrap();
            storage.save_artifact(&a2).await.unwrap();
        }
        let backend = SwiftideBackend::new(
            storage,
            None,
            std::path::PathBuf::from("/tmp/todo4-ws"),
            true,
        );
        let listed = backend.list_artifacts("sess-A").await.unwrap();
        assert_eq!(listed.len(), 2);
        assert_eq!(listed[0].artifact_id, "art-1");
        assert_eq!(
            backend
                .artifact_storage_rel_path("sess-A", "art-2")
                .await
                .unwrap(),
            Some("sess-A/b.txt".to_string())
        );
        // Unknown id resolves to None, not an error.
        assert_eq!(
            backend
                .artifact_storage_rel_path("sess-A", "nope")
                .await
                .unwrap(),
            None
        );
        // Unknown session lists empty.
        assert!(backend.list_artifacts("other").await.unwrap().is_empty());
    }

    #[tokio::test]
    async fn fs_write_rejects_oversize_without_partial_file() {
        let root = tempfile::tempdir().unwrap();
        let tool = FsWriteTool {
            artifact_root: Some(root.path().to_path_buf()),
        };
        let oversized = "x".repeat((FS_WRITE_MAX_BYTES as usize) + 1);

        let result = invoke_fs_write(&tool, "large.txt", &oversized, None).await;

        assert!(result.is_err());
        assert!(!root.path().join("large.txt").exists());
        let entries = std::fs::read_dir(root.path()).unwrap().collect::<Vec<_>>();
        assert!(entries.is_empty(), "failed write must not leave temp files");
    }

    #[tokio::test]
    async fn fs_write_success_through_gate_emits_audit_record() {
        let session_id = "session-fs-write-audit";
        let _ = crate::approval_audit::take_test_audit_records_for_session(session_id);
        let root = tempfile::tempdir().unwrap();
        let tool = PermissionGatedTool {
            inner_tool: Arc::new(FsWriteTool {
                artifact_root: Some(root.path().to_path_buf()),
            }),
            sensitivity: ToolSensitivity::ReadOnly,
            sensitivity_downgrade: Option::None,
            permission_callback: Arc::new(Mutex::new(None)),
            denied_flag: Arc::new(Mutex::new(false)),
            session_id: session_id.to_string(),
            approval_policy: Arc::new(Mutex::new(ApprovalPolicy::Prompt)),
            runtime_tier: Arc::new(Mutex::new(None)),
            fs_whitelist_roots: Arc::new(Mutex::new(Vec::new())),
            workspace_root: Arc::new(std::path::PathBuf::from("/")),
            artifact_root: Arc::new(Mutex::new(Option::None)),
        };
        let context = swiftide_agents::DefaultContext::default();

        let result = tool
            .invoke(
                &context,
                &fs_write_call("audited.txt", "written", Some("text/plain")),
            )
            .await;

        assert!(result.is_ok());
        let records = crate::approval_audit::take_test_audit_records_for_session(session_id);
        assert!(records.iter().any(|record| {
            record.tool_name == "fs_write"
                && record.kind == ApprovalAuditEventKind::ToolExecutionResult
        }));
    }

    #[tokio::test]
    async fn fs_write_atomic_rename_failure_removes_temp_file() {
        let root = tempfile::tempdir().unwrap();
        tokio::fs::create_dir(root.path().join("occupied"))
            .await
            .unwrap();
        let tool = FsWriteTool {
            artifact_root: Some(root.path().to_path_buf()),
        };

        let result = invoke_fs_write(&tool, "occupied", "cannot replace directory", None).await;

        assert!(result.is_err());
        assert!(root.path().join("occupied").is_dir());
        let names = std::fs::read_dir(root.path())
            .unwrap()
            .map(|entry| entry.unwrap().file_name())
            .collect::<Vec<_>>();
        assert_eq!(names, vec![std::ffi::OsString::from("occupied")]);
    }

    #[test]

    fn test_truncate_at_char_boundary() {
        // Under the limit: unchanged.

        let mut s = "hi".to_string();

        truncate_at_char_boundary(&mut s, 100);

        assert_eq!(s, "hi");

        // "日本語" = 3 chars x 3 bytes = 9 bytes. A 4-byte cut lands mid-"本";

        // must back off to the 3-byte boundary after "日" (never panic).

        let mut s = "日本語".to_string();

        truncate_at_char_boundary(&mut s, 4);

        assert_eq!(s, "日");

        assert!(s.len() <= 4);

        // Exact boundary is preserved.

        let mut s = "日本語".to_string();

        truncate_at_char_boundary(&mut s, 3);

        assert_eq!(s, "日");

        // 4-byte emoji: "a😀b" = 1 + 4 + 1 bytes; a 3-byte cut lands mid-emoji.

        let mut s = "a😀b".to_string();

        truncate_at_char_boundary(&mut s, 3);

        assert_eq!(s, "a");
    }

    #[tokio::test]
    async fn test_follow_up_active_turn_queues_and_executes_fifo_once() {
        let sqlite = maho_storage::sqlite::SqliteStorage::open_in_memory_with_key("queue-test-key")
            .expect("in-memory sqlite");
        let storage = Arc::new(crate::MutexAgentStorage(Arc::new(std::sync::Mutex::new(
            sqlite,
        ))));

        let backend = SwiftideBackend::new(
            storage.clone(),
            None,
            std::path::PathBuf::from("/tmp/queue-ws"),
            true,
        );

        let session_id = "sess-queue-fifo-1";

        // Pre-enqueue follow-up messages into session turn controller
        {
            let mut controllers = backend.turn_controllers.lock_unpoison();
            let ctrl = controllers
                .entry(session_id.to_string())
                .or_insert_with(|| crate::turn_control::SessionTurnController::new(session_id));
            let _ = ctrl.submit_follow_up(
                "follow-up msg 1",
                crate::turn_control::FollowUpBehavior::Queue,
                100,
            );
            let _ = ctrl.submit_follow_up(
                "follow-up msg 2",
                crate::turn_control::FollowUpBehavior::Queue,
                200,
            );
        }

        let executed_messages = Arc::new(Mutex::new(Vec::new()));
        let exec_clone = Arc::clone(&executed_messages);

        #[derive(Clone)]
        struct EchoLlm {
            executed: Arc<Mutex<Vec<String>>>,
        }
        #[async_trait]
        impl ChatCompletion for EchoLlm {
            async fn complete(
                &self,
                request: &ChatCompletionRequest,
            ) -> Result<
                swiftide_core::chat_completion::ChatCompletionResponse,
                swiftide_core::chat_completion::errors::LanguageModelError,
            > {
                let user_msg = request
                    .messages()
                    .iter()
                    .rev()
                    .find_map(|m| {
                        if let SwiftideChatMessage::User(t) = m {
                            Some(t.clone())
                        } else {
                            None
                        }
                    })
                    .unwrap_or_else(|| "default".to_string());
                self.executed.lock_unpoison().push(user_msg.clone());
                Ok(swiftide_core::chat_completion::ChatCompletionResponse {
                    message: Some(format!("echo: {user_msg}")),
                    ..Default::default()
                })
            }
            async fn complete_stream(
                &self,
                _request: &ChatCompletionRequest,
            ) -> swiftide_core::chat_completion::ChatCompletionStream {
                Box::pin(futures::stream::empty())
            }
        }

        backend.set_llm_override(Box::new(EchoLlm {
            executed: exec_clone,
        }));

        let initial_msg = MahoChatMessage::user(ChatContent::text("initial turn msg"));
        let res = backend
            .run_turn(session_id, initial_msg, None, None)
            .await
            .unwrap();

        assert_eq!(res.role, "assistant");
        let content = match res.content {
            ChatContent::Text(t) => t,
            _ => panic!("expected text"),
        };
        assert_eq!(content, "echo: follow-up msg 2");

        // Verify turns executed in FIFO order exactly once
        let executed = executed_messages.lock_unpoison().clone();
        assert_eq!(
            executed,
            vec![
                "initial turn msg".to_string(),
                "follow-up msg 1".to_string(),
                "follow-up msg 2".to_string(),
            ]
        );
    }

    #[tokio::test]
    async fn test_transient_model_error_retries_and_policy_denied_fails_fast() {
        use std::sync::atomic::AtomicUsize;

        #[derive(Clone)]
        struct MockCounterLlm {
            call_count: Arc<AtomicUsize>,
            mode: Arc<Mutex<&'static str>>,
        }

        #[async_trait]
        impl ChatCompletion for MockCounterLlm {
            async fn complete(
                &self,
                _request: &ChatCompletionRequest,
            ) -> Result<
                swiftide_core::chat_completion::ChatCompletionResponse,
                swiftide_core::chat_completion::errors::LanguageModelError,
            > {
                let count = self.call_count.fetch_add(1, Ordering::SeqCst);
                let mode = *self.mode.lock_unpoison();
                match mode {
                    "transient_then_succeed" => {
                        if count < 2 {
                            Err(swiftide_core::chat_completion::errors::LanguageModelError::permanent(
                                anyhow::anyhow!("connection reset by peer: transient network error"),
                            ))
                        } else {
                            Ok(swiftide_core::chat_completion::ChatCompletionResponse {
                                message: Some("success after retries".to_string()),
                                ..Default::default()
                            })
                        }
                    }
                    "policy_denied" => Err(
                        swiftide_core::chat_completion::errors::LanguageModelError::permanent(
                            anyhow::anyhow!("permission denied by security policy: forbidden"),
                        ),
                    ),
                    "uncertain_mutation" => Err(
                        swiftide_core::chat_completion::errors::LanguageModelError::permanent(
                            anyhow::anyhow!("uncertain mutation unverified"),
                        ),
                    ),
                    _ => Ok(swiftide_core::chat_completion::ChatCompletionResponse::default()),
                }
            }
            async fn complete_stream(
                &self,
                _request: &ChatCompletionRequest,
            ) -> swiftide_core::chat_completion::ChatCompletionStream {
                Box::pin(futures::stream::empty())
            }
        }

        let count = Arc::new(AtomicUsize::new(0));
        let mode = Arc::new(Mutex::new("transient_then_succeed"));
        let mock_llm = Arc::new(MockCounterLlm {
            call_count: Arc::clone(&count),
            mode: Arc::clone(&mode),
        });

        let retry_policy = crate::retry_policy::RetryPolicy {
            max_attempts: 3,
            backoff: crate::retry_policy::BoundedBackoffConfig {
                initial_backoff_ms: 1,
                max_backoff_ms: 5,
                backoff_factor: 1.0,
                jitter_ratio: 0.0,
            },
            retryable_classes: vec![
                crate::retry_policy::ErrorClass::TransientNetwork,
                crate::retry_policy::ErrorClass::RateLimited,
            ],
            allow_uncertain_mutation_retry: false,
        };

        let fallback_reasons = Arc::new(Mutex::new(Vec::new()));
        let retry_llm = RetryChatCompletion {
            inner: mock_llm.clone(),
            retry_policy,
            fallback_reasons: Arc::clone(&fallback_reasons),
            model_name: "primary-model".to_string(),
            fallback_model_name: Some("fallback-model".to_string()),
            journal: None,
            run_id: None,
        };

        let req = ChatCompletionRequest::from(vec![SwiftideChatMessage::new_user("hello")]);

        // (1) Transient errors retry and then succeed on attempt 3
        let res = retry_llm.complete(&req).await;
        assert!(res.is_ok());
        assert_eq!(res.unwrap().message.unwrap(), "success after retries");
        assert_eq!(count.load(Ordering::SeqCst), 3);

        // (2) PolicyDenied fails immediately on attempt 1 without retrying
        count.store(0, Ordering::SeqCst);
        *mode.lock_unpoison() = "policy_denied";
        let res_denied = retry_llm.complete(&req).await;
        assert!(res_denied.is_err());
        assert_eq!(count.load(Ordering::SeqCst), 1);

        // (3) UncertainMutation fails as NeedsResolution without retrying
        count.store(0, Ordering::SeqCst);
        *mode.lock_unpoison() = "uncertain_mutation";
        let res_mut = retry_llm.complete(&req).await;
        assert!(res_mut.is_err());
        assert!(res_mut.unwrap_err().to_string().contains("NeedsResolution"));
        assert_eq!(count.load(Ordering::SeqCst), 1);
    }

    #[tokio::test]
    async fn test_web_search_unentitled_premium_surfaces_typed_unavailable() {
        let tool = WebSearchTool {
            router: crate::web_search::WebSearchEntitlementRouter::default(),
            entitlement: Arc::new(Mutex::new(crate::web_search::SearchEntitlement::free())),
        };

        let context = swiftide_agents::DefaultContext::default();
        let call = test_tool_call(
            "web_search",
            serde_json::json!({
                "query": "quantum computing",
                "tier": "premium",
            })
            .to_string(),
        );

        let res = tool.invoke(&context, &call).await.unwrap();
        match res {
            ToolOutput::Text(t) => {
                let json: serde_json::Value = serde_json::from_str(&t).expect("valid json output");
                assert_eq!(json["error"], "typed_unavailable");
                assert!(json["reason"]
                    .as_str()
                    .unwrap()
                    .contains("requires Max tier"));
            }
            other => panic!("expected ToolOutput::Text, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn test_model_routing_fallback_reason_recorded() {
        let sqlite =
            maho_storage::sqlite::SqliteStorage::open_in_memory_with_key("routing-test-key")
                .expect("in-memory sqlite");
        let storage = Arc::new(crate::MutexAgentStorage(Arc::new(std::sync::Mutex::new(
            sqlite,
        ))));

        let backend = SwiftideBackend::new(
            storage,
            None,
            std::path::PathBuf::from("/tmp/routing-ws"),
            true,
        );

        // Category default for Chat is google/gemini-3-flash-lite:free.
        // Provide only fallback model anthropic/claude-3-7-sonnet in available_models.
        backend.set_available_models(vec!["anthropic/claude-3-7-sonnet".to_string()]);

        #[derive(Clone)]
        struct SimpleEchoLlm;
        #[async_trait]
        impl ChatCompletion for SimpleEchoLlm {
            async fn complete(
                &self,
                _request: &ChatCompletionRequest,
            ) -> Result<
                swiftide_core::chat_completion::ChatCompletionResponse,
                swiftide_core::chat_completion::errors::LanguageModelError,
            > {
                Ok(swiftide_core::chat_completion::ChatCompletionResponse {
                    message: Some("hello".to_string()),
                    ..Default::default()
                })
            }
            async fn complete_stream(
                &self,
                _request: &ChatCompletionRequest,
            ) -> swiftide_core::chat_completion::ChatCompletionStream {
                Box::pin(futures::stream::empty())
            }
        }

        backend.set_llm_override(Box::new(SimpleEchoLlm));

        let msg = MahoChatMessage::user(ChatContent::text("what is the weather"));
        let _ = backend
            .run_turn("sess-routing-test", msg, None, None)
            .await
            .unwrap();

        let decisions = backend.routing_decisions();
        assert!(!decisions.is_empty(), "routing decision must be recorded");
        let last = decisions.last().unwrap();
        assert!(last.is_fallback, "decision should be marked as fallback");
        assert!(
            !last.reason.is_empty(),
            "fallback reason string must be recorded"
        );
        assert_eq!(last.selected_model, "anthropic/claude-3-7-sonnet");
    }

    #[tokio::test]
    async fn test_notification_wait_suspends_and_wake_resumes_same_turn() {
        let registry = Arc::new(Mutex::new(crate::event_wait::WaitRegistry::new()));
        let wake_notify = Arc::new(tokio::sync::Notify::new());
        let activation = Arc::new(Mutex::new(
            crate::event_wait::NotificationActivation::granted(),
        ));

        let session_id = "sess-wait-wake-1".to_string();
        let tool = NotificationWaitTool {
            wait_registry: Arc::clone(&registry),
            wake_notify: Arc::clone(&wake_notify),
            session_id: session_id.clone(),
            notification_activation: activation,
        };

        let context = swiftide_agents::DefaultContext::default();
        let call = test_tool_call(
            "notification_wait",
            serde_json::json!({
                "sender": "service-worker",
                "timeout_ms": 10000,
            })
            .to_string(),
        );

        let invoke_task = tokio::spawn(async move { tool.invoke(&context, &call).await });

        // Yield execution to allow the tool to register in WaitRegistry and suspend
        tokio::task::yield_now().await;

        let run_id = crate::run_journal::AgentRunId::new(&session_id);
        {
            let mut reg = registry.lock_unpoison();
            let status = reg.poll(&run_id).expect("poll run status");
            assert!(
                status.is_suspended(),
                "Run must be in suspended state in WaitRegistry"
            );
        }

        // Wake the suspended run with matching WakeEvent
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_millis() as u64;

        let wake_event = crate::event_wait::WakeEvent::for_run(
            run_id.clone(),
            crate::event_wait::EventKind::Notification,
            now,
        )
        .with_sender("service-worker")
        .with_payload(serde_json::json!({ "result": "notification payload delivered" }));

        let tokens = registry.lock_unpoison().wake(&wake_event);
        assert_eq!(tokens.len(), 1, "Exactly one run should be woken");
        wake_notify.notify_waiters();

        // Await the suspended invoke task and assert resumed output
        let res = invoke_task.await.unwrap().unwrap();
        match res {
            ToolOutput::Text(t) => {
                let json: serde_json::Value = serde_json::from_str(&t).expect("valid json output");
                assert_eq!(json["status"], "woken");
                assert_eq!(
                    json["event"]["payload"]["result"],
                    "notification payload delivered"
                );
            }
            other => panic!("expected ToolOutput::Text, got {other:?}"),
        }
    }

    // ==========================================
    // Gap Tests: (A) Binary fs_write
    // ==========================================

    #[tokio::test]
    async fn test_fs_write_roundtrip_binary_bytes_and_kind_deduction() {
        use base64::Engine;
        let root = tempfile::tempdir().unwrap();
        let tool = FsWriteTool {
            artifact_root: Some(root.path().to_path_buf()),
        };

        // Binary payload: 8 bytes with non-UTF-8 values
        let binary_data: Vec<u8> = vec![0x89, b'P', b'N', b'G', 0x0D, 0x0A, 0x1A, 0x0A];
        let b64_str = base64::engine::general_purpose::STANDARD.encode(&binary_data);

        // 1. Write binary xlsx artifact
        let context = swiftide_agents::DefaultContext::default();
        let call_xlsx = ToolCall::builder()
            .id("call-xlsx-bin".to_string())
            .name("fs_write".to_string())
            .args(
                serde_json::json!({
                    "relative_path": "reports/data.xlsx",
                    "content_b64": b64_str,
                    "mime_type": "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"
                })
                .to_string(),
            )
            .build()
            .unwrap();

        let output_xlsx = tool
            .invoke(&context, &call_xlsx)
            .await
            .expect("binary fs_write xlsx");
        let text = match output_xlsx {
            ToolOutput::Text(t) => t,
            other => panic!("expected ToolOutput::Text, got {other:?}"),
        };
        let parsed: serde_json::Value = serde_json::from_str(&text).unwrap();
        assert_eq!(parsed["size_bytes"], 8);
        assert_eq!(parsed["kind"], "xlsx");
        assert_eq!(parsed["display_name"], "data.xlsx");

        // Verify written bytes on disk
        let written_bytes = tokio::fs::read(root.path().join("reports/data.xlsx"))
            .await
            .expect("read written file");
        assert_eq!(written_bytes, binary_data);

        // 2. Verify build_after_tool_events produces ArtifactCreated with kind "xlsx"
        let (events, artifact_to_persist) = build_after_tool_events(
            "sess-1",
            "call-xlsx-bin".to_string(),
            "fs_write".to_string(),
            true,
            &text,
        );
        assert_eq!(events.len(), 2);
        match &events[0] {
            AgentStreamEvent::ArtifactCreated { artifact } => {
                assert_eq!(artifact.display_name, "data.xlsx");
                assert_eq!(artifact.size_bytes, 8);
                assert_eq!(artifact.kind.as_deref(), Some("xlsx"));
            }
            other => panic!("expected ArtifactCreated, got {other:?}"),
        }
        assert_eq!(artifact_to_persist.unwrap().kind.as_deref(), Some("xlsx"));

        // 3. Write binary pdf artifact
        let pdf_data = b"%PDF-1.4 test binary data";
        let pdf_b64 = base64::engine::general_purpose::STANDARD.encode(pdf_data);
        let call_pdf = ToolCall::builder()
            .id("call-pdf-bin".to_string())
            .name("fs_write".to_string())
            .args(
                serde_json::json!({
                    "relative_path": "doc.pdf",
                    "content_b64": pdf_b64,
                    "mime_type": "application/pdf"
                })
                .to_string(),
            )
            .build()
            .unwrap();

        let output_pdf = tool
            .invoke(&context, &call_pdf)
            .await
            .expect("binary fs_write pdf");
        let text_pdf = match output_pdf {
            ToolOutput::Text(t) => t,
            _other => panic!("expected text"),
        };
        let (pdf_events, _) = build_after_tool_events(
            "sess-1",
            "call-pdf-bin".to_string(),
            "fs_write".to_string(),
            true,
            &text_pdf,
        );
        match &pdf_events[0] {
            AgentStreamEvent::ArtifactCreated { artifact } => {
                assert_eq!(artifact.kind.as_deref(), Some("pdf"));
            }
            other => panic!("expected ArtifactCreated, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn test_fs_write_both_content_and_b64_returns_typed_error() {
        let root = tempfile::tempdir().unwrap();
        let tool = FsWriteTool {
            artifact_root: Some(root.path().to_path_buf()),
        };
        let context = swiftide_agents::DefaultContext::default();
        let call = ToolCall::builder()
            .id("call-err-1".to_string())
            .name("fs_write".to_string())
            .args(
                serde_json::json!({
                    "relative_path": "test.txt",
                    "content": "text content",
                    "content_b64": "dGV4dCBjb250ZW50"
                })
                .to_string(),
            )
            .build()
            .unwrap();

        let err = tool
            .invoke(&context, &call)
            .await
            .expect_err("must error when both set");
        let msg = err.to_string();
        assert!(msg.contains("cannot provide both 'content' and 'content_b64'"));
    }

    #[tokio::test]
    async fn test_fs_write_empty_content_and_b64_returns_typed_error() {
        let root = tempfile::tempdir().unwrap();
        let tool = FsWriteTool {
            artifact_root: Some(root.path().to_path_buf()),
        };
        let context = swiftide_agents::DefaultContext::default();
        let call = ToolCall::builder()
            .id("call-err-2".to_string())
            .name("fs_write".to_string())
            .args(
                serde_json::json!({
                    "relative_path": "test.txt"
                })
                .to_string(),
            )
            .build()
            .unwrap();

        let err = tool
            .invoke(&context, &call)
            .await
            .expect_err("must error when neither set");
        let msg = err.to_string();
        assert!(msg.contains("must provide either 'content' or 'content_b64'"));
    }

    #[tokio::test]
    async fn test_fs_write_invalid_base64_returns_typed_error() {
        let root = tempfile::tempdir().unwrap();
        let tool = FsWriteTool {
            artifact_root: Some(root.path().to_path_buf()),
        };
        let context = swiftide_agents::DefaultContext::default();
        let call = ToolCall::builder()
            .id("call-err-3".to_string())
            .name("fs_write".to_string())
            .args(
                serde_json::json!({
                    "relative_path": "test.bin",
                    "content_b64": "@@@invalid-base64!!!"
                })
                .to_string(),
            )
            .build()
            .unwrap();

        let err = tool
            .invoke(&context, &call)
            .await
            .expect_err("must error on invalid base64");
        let msg = err.to_string();
        assert!(msg.contains("Invalid base64 payload"));
    }

    // ==========================================
    // Gap Tests: (B) 30s Status / Proof Emitter
    // ==========================================

    #[tokio::test]
    async fn test_runtime_status_emitter_65s_turn_emits_status_and_proof() {
        use std::sync::atomic::AtomicUsize;
        let sqlite =
            maho_storage::sqlite::SqliteStorage::open_in_memory_with_key("status-test-key")
                .expect("sqlite");
        let storage = Arc::new(crate::MutexAgentStorage(Arc::new(std::sync::Mutex::new(
            sqlite,
        ))));
        let clock = Arc::new(crate::event_wait::MockClock::new(1000));
        let root = tempfile::tempdir().unwrap();

        let backend = SwiftideBackend::new(storage, None, root.path().to_path_buf(), true)
            .with_clock(clock.clone());
        backend.set_artifact_root(Some(root.path().to_path_buf()));

        #[derive(Clone)]
        struct MultiStepMockLlm {
            step: Arc<AtomicUsize>,
            clock: Arc<crate::event_wait::MockClock>,
        }

        #[async_trait]
        impl ChatCompletion for MultiStepMockLlm {
            async fn complete(
                &self,
                _request: &ChatCompletionRequest,
            ) -> Result<
                swiftide_core::chat_completion::ChatCompletionResponse,
                swiftide_core::chat_completion::errors::LanguageModelError,
            > {
                let current_step = self.step.fetch_add(1, Ordering::SeqCst);
                if current_step == 0 {
                    // Tool call 1 after 35s simulated wall progress
                    self.clock.advance(35_000);
                    Ok(swiftide_core::chat_completion::ChatCompletionResponse {
                        message: None,
                        tool_calls: Some(vec![ToolCall::builder()
                            .id("step-tool-1".to_string())
                            .name("fs_write".to_string())
                            .args(
                                serde_json::json!({ "relative_path": "a.txt", "content": "step1" })
                                    .to_string(),
                            )
                            .build()
                            .unwrap()]),
                        ..Default::default()
                    })
                } else if current_step == 1 {
                    // Tool call 2 after another 30s (total elapsed: 65s)
                    self.clock.advance(30_000);
                    Ok(swiftide_core::chat_completion::ChatCompletionResponse {
                        message: None,
                        tool_calls: Some(vec![ToolCall::builder()
                            .id("step-tool-2".to_string())
                            .name("fs_write".to_string())
                            .args(
                                serde_json::json!({ "relative_path": "b.txt", "content": "step2" })
                                    .to_string(),
                            )
                            .build()
                            .unwrap()]),
                        ..Default::default()
                    })
                } else {
                    // Completion
                    Ok(swiftide_core::chat_completion::ChatCompletionResponse {
                        message: Some("Completed multi-step task successfully".to_string()),
                        tool_calls: None,
                        ..Default::default()
                    })
                }
            }
        }

        let mock_llm = MultiStepMockLlm {
            step: Arc::new(AtomicUsize::new(0)),
            clock: clock.clone(),
        };
        backend.set_llm_override(Box::new(mock_llm));

        let events_collected = Arc::new(Mutex::new(Vec::new()));
        let ev_clone = events_collected.clone();
        let on_event = Arc::new(move |ev: AgentStreamEvent| {
            ev_clone.lock_unpoison().push(ev);
        });

        let user_msg = MahoChatMessage::user(ChatContent::text("run 65s multi-step task"));
        let res = backend
            .run_turn("session-status-65s", user_msg, None, Some(on_event))
            .await
            .unwrap();

        assert_eq!(res.role, "assistant");

        let events = events_collected.lock_unpoison().clone();
        println!("[DEBUG test_runtime_status_emitter] events: {:?}", events);

        // Filter status events
        let status_events: Vec<&AgentStreamEvent> = events
            .iter()
            .filter(|e| matches!(e, AgentStreamEvent::Status { .. }))
            .collect();

        // Must emit >= 2 status events (for 30s and 60s ticks)
        assert!(
            status_events.len() >= 2,
            "Expected >= 2 status events for 65s turn, got {}",
            status_events.len()
        );

        match status_events[0] {
            AgentStreamEvent::Status {
                elapsed_secs,
                message,
            } => {
                assert_eq!(*elapsed_secs, 30);
                assert!(message.contains("30s"));
            }
            _ => panic!("expected Status event"),
        }

        match status_events[1] {
            AgentStreamEvent::Status {
                elapsed_secs,
                message,
            } => {
                assert_eq!(*elapsed_secs, 60);
                assert!(message.contains("60s"));
            }
            _ => panic!("expected Status event"),
        }

        // Terminal event must be ProofOrReason
        let last_event = events.last().expect("must have events");
        match last_event {
            AgentStreamEvent::ProofOrReason {
                proof_locator,
                reason,
            } => {
                assert!(proof_locator.is_some() || reason.is_some());
            }
            other => panic!("expected terminal ProofOrReason event, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn test_runtime_status_emitter_fast_turn_emits_no_status_and_terminal_proof() {
        let sqlite = maho_storage::sqlite::SqliteStorage::open_in_memory_with_key("fast-test-key")
            .expect("sqlite");
        let storage = Arc::new(crate::MutexAgentStorage(Arc::new(std::sync::Mutex::new(
            sqlite,
        ))));
        let clock = Arc::new(crate::event_wait::MockClock::new(1000));

        let backend = SwiftideBackend::new(
            storage,
            None,
            std::path::PathBuf::from("/tmp/maho-test-fast"),
            true,
        )
        .with_clock(clock.clone());

        #[derive(Clone)]
        struct FastMockLlm {
            clock: Arc<crate::event_wait::MockClock>,
        }

        #[async_trait]
        impl ChatCompletion for FastMockLlm {
            async fn complete(
                &self,
                _request: &ChatCompletionRequest,
            ) -> Result<
                swiftide_core::chat_completion::ChatCompletionResponse,
                swiftide_core::chat_completion::errors::LanguageModelError,
            > {
                // Fast turn: advance 10s only (< 30s)
                self.clock.advance(10_000);
                Ok(swiftide_core::chat_completion::ChatCompletionResponse {
                    message: Some("Fast answer".to_string()),
                    tool_calls: None,
                    ..Default::default()
                })
            }

            async fn complete_stream(
                &self,
                request: &ChatCompletionRequest,
            ) -> swiftide_core::chat_completion::ChatCompletionStream {
                Box::pin(futures::stream::iter(vec![self.complete(request).await]))
            }
        }

        backend.set_llm_override(Box::new(FastMockLlm {
            clock: clock.clone(),
        }));

        let events_collected = Arc::new(Mutex::new(Vec::new()));
        let ev_clone = events_collected.clone();
        let on_event = Arc::new(move |ev: AgentStreamEvent| {
            ev_clone.lock_unpoison().push(ev);
        });

        let user_msg = MahoChatMessage::user(ChatContent::text("fast query"));
        let _ = backend
            .run_turn("session-fast-turn", user_msg, None, Some(on_event))
            .await
            .unwrap();

        let events = events_collected.lock_unpoison().clone();

        // 0 status events
        let status_count = events
            .iter()
            .filter(|e| matches!(e, AgentStreamEvent::Status { .. }))
            .count();
        assert_eq!(
            status_count, 0,
            "Fast turn (<30s) must emit 0 status events"
        );

        // Terminal event must be ProofOrReason
        let last_event = events.last().expect("must have events");
        match last_event {
            AgentStreamEvent::ProofOrReason { .. } => {}
            other => panic!("expected terminal ProofOrReason event, got {other:?}"),
        }
    }

    /// Wave 3C (G9): `run_prompt_in_session` forwards the live event stream
    /// to the supplied sink — a tool call surfaces as ToolCall followed by a
    /// successful ToolResult — while still returning the final assistant
    /// text (the CLI's stderr progress rides exactly this sink).
    #[tokio::test]
    async fn test_run_prompt_in_session_streams_tool_events_to_sink() {
        let sqlite =
            maho_storage::sqlite::SqliteStorage::open_in_memory_with_key("prompt-stream-key")
                .expect("sqlite");
        let storage = Arc::new(crate::MutexAgentStorage(Arc::new(std::sync::Mutex::new(
            sqlite,
        ))));
        let root = tempfile::tempdir().unwrap();

        let backend = SwiftideBackend::new(storage, None, root.path().to_path_buf(), true);
        backend.set_artifact_root(Some(root.path().to_path_buf()));
        // AllowAll so the gated fs_write executes deterministically without
        // a permission callback (mirrors `maho agent task --approve-all`).
        backend.set_approval_policy("allow_all".to_string());

        #[derive(Clone)]
        struct ToolThenAnswerLlm {
            step: Arc<AtomicUsize>,
        }

        #[async_trait]
        impl ChatCompletion for ToolThenAnswerLlm {
            async fn complete(
                &self,
                _request: &ChatCompletionRequest,
            ) -> Result<
                swiftide_core::chat_completion::ChatCompletionResponse,
                swiftide_core::chat_completion::errors::LanguageModelError,
            > {
                let step = self.step.fetch_add(1, Ordering::SeqCst);
                if step == 0 {
                    Ok(swiftide_core::chat_completion::ChatCompletionResponse {
                        message: None,
                        tool_calls: Some(vec![ToolCall::builder()
                            .id("stream-tool-1".to_string())
                            .name("fs_write".to_string())
                            .args(
                                serde_json::json!({
                                    "relative_path": "stream.txt",
                                    "content": "hello"
                                })
                                .to_string(),
                            )
                            .build()
                            .unwrap()]),
                        ..Default::default()
                    })
                } else {
                    Ok(swiftide_core::chat_completion::ChatCompletionResponse {
                        message: Some("Wrote the file".to_string()),
                        tool_calls: None,
                        ..Default::default()
                    })
                }
            }
        }

        backend.set_llm_override(Box::new(ToolThenAnswerLlm {
            step: Arc::new(AtomicUsize::new(0)),
        }));

        let events_collected = Arc::new(Mutex::new(Vec::new()));
        let ev_clone = events_collected.clone();
        let on_event = Arc::new(move |ev: AgentStreamEvent| {
            ev_clone.lock_unpoison().push(ev);
        });

        let answer = backend
            .run_prompt_in_session(
                "session-prompt-stream",
                "write the file then say done",
                Some(on_event),
            )
            .await
            .unwrap();
        assert_eq!(answer, "Wrote the file");

        let events = events_collected.lock_unpoison().clone();
        let tool_call_idx = events
            .iter()
            .position(|e| {
                matches!(e, AgentStreamEvent::ToolCall { name, .. } if name.as_str() == "fs_write")
            })
            .expect("sink must receive the fs_write ToolCall");
        let tool_result_idx = events
            .iter()
            .position(|e| {
                matches!(e, AgentStreamEvent::ToolResult { name, .. } if name.as_str() == "fs_write")
            })
            .expect("sink must receive the fs_write ToolResult");
        assert!(
            tool_call_idx < tool_result_idx,
            "ToolCall must precede ToolResult: {events:?}"
        );
        match &events[tool_result_idx] {
            AgentStreamEvent::ToolResult { succeeded, .. } => {
                assert!(*succeeded, "fs_write must report success: {events:?}");
            }
            other => panic!("expected ToolResult, got {other:?}"),
        }
    }

    // ==========================================
    // Gap Tests: (C) L1 Briefing Injection
    // ==========================================

    #[tokio::test]
    async fn test_l1_briefing_injection_in_session_and_memory_disabled_profile() {
        use maho_storage::sqlite::SqliteStorage;
        let sqlite = SqliteStorage::open_in_memory_with_key("l1-briefing-test-key").unwrap();
        // Insert L1 briefing into sqlite storage
        sqlite
            .upsert_memory_block(
                maho_core::memory::L1_BRIEFING_LABEL,
                "User preferences: Rust async, macOS terminal Ghostty, prefers concise replies.",
                "hot",
                20,
            )
            .unwrap();

        let storage = Arc::new(crate::MutexAgentStorage(Arc::new(std::sync::Mutex::new(
            sqlite,
        ))));

        let backend = SwiftideBackend::new(
            storage.clone(),
            None,
            std::path::PathBuf::from("/tmp/maho-test-l1"),
            true,
        );

        #[derive(Clone)]
        struct CapturePromptLlm {
            captured_prompts: Arc<Mutex<Vec<String>>>,
        }

        #[async_trait]
        impl ChatCompletion for CapturePromptLlm {
            async fn complete(
                &self,
                request: &ChatCompletionRequest,
            ) -> Result<
                swiftide_core::chat_completion::ChatCompletionResponse,
                swiftide_core::chat_completion::errors::LanguageModelError,
            > {
                for msg in request.messages() {
                    if let SwiftideChatMessage::System(prompt) = msg {
                        self.captured_prompts.lock_unpoison().push(prompt.clone());
                    }
                }
                Ok(swiftide_core::chat_completion::ChatCompletionResponse {
                    message: Some("Acknowledged prompt".to_string()),
                    tool_calls: None,
                    ..Default::default()
                })
            }
        }

        // 1. Run turn with briefing present
        let captured = Arc::new(Mutex::new(Vec::new()));
        backend.set_llm_override(Box::new(CapturePromptLlm {
            captured_prompts: captured.clone(),
        }));

        let user_msg = MahoChatMessage::user(ChatContent::text("hello with memory"));
        let _ = backend
            .run_turn("session-briefing-1", user_msg, None, None)
            .await
            .unwrap();

        let all_prompts = captured.lock_unpoison().clone();
        let prompt = all_prompts.join("\n\n");
        assert!(prompt.contains("[MAHO RUNTIME SYSTEM CONTRACT - IMMUTABLE]"));
        assert!(prompt.contains("[MEMORY L1 BRIEFING]"));
        assert!(prompt.contains("User preferences: Rust async, macOS terminal Ghostty"));

        let baseline_idx = prompt
            .find("[MAHO RUNTIME SYSTEM CONTRACT - IMMUTABLE]")
            .unwrap();
        let briefing_idx = prompt.find("[MEMORY L1 BRIEFING]").unwrap();
        assert!(
            baseline_idx < briefing_idx,
            "L1 briefing must appear after baseline contract"
        );

        // 2. Run turn with memory-disabled profile
        // Create a profile that has "no_memory" tool gate or blank without memory
        let disabled_profile = maho_types::ai::AiProfile {
            id: "disabled-profile".to_string(),
            name: "No Memory Profile".to_string(),
            system_prompt: "Standard custom prompt".to_string(),
            preferred_model: None,
            tools: vec!["no_memory".to_string()],
            mcp_servers: vec![],
            is_default: false,
            created_at: String::new(),
            updated_at: String::new(),
        };
        {
            let raw_storage = storage.0.lock().unwrap();
            raw_storage.create_ai_profile(&disabled_profile).unwrap();
            let ws = maho_types::ai::AiWorkspace {
                id: "ws-disabled".to_string(),
                name: "Disabled Workspace".to_string(),
                profile_id: Some("disabled-profile".to_string()),
                space_id: None,
                workspace_root: Some("/tmp/maho-test-l1".to_string()),
                created_at: String::new(),
                updated_at: String::new(),
            };
            raw_storage.create_workspace(&ws).unwrap();
        }

        let captured_disabled = Arc::new(Mutex::new(Vec::new()));
        backend.set_llm_override(Box::new(CapturePromptLlm {
            captured_prompts: captured_disabled.clone(),
        }));

        let user_msg2 = MahoChatMessage::user(ChatContent::text("hello without memory"));
        let _ = backend
            .run_turn("session-briefing-disabled", user_msg2, None, None)
            .await
            .unwrap();

        let all_disabled = captured_disabled.lock_unpoison().clone();
        let prompt_disabled = all_disabled.join("\n\n");
        assert!(prompt_disabled.contains("[MAHO RUNTIME SYSTEM CONTRACT - IMMUTABLE]"));
        assert!(
            !prompt_disabled.contains("[MEMORY L1 BRIEFING]"),
            "Memory-disabled profile must omit L1 briefing block"
        );
        assert!(!prompt_disabled.contains("macOS terminal Ghostty"));
    }

    // ==========================================
    // RunJournal Storage Attachment & Persistence
    // ==========================================

    #[tokio::test]
    async fn test_run_journal_fs_store_attachment_and_disk_write_through() {
        use maho_storage::sqlite::SqliteStorage;
        let temp_dir = tempfile::tempdir().unwrap();
        let journal_path = temp_dir.path().join("session_journal.json");

        let sqlite = SqliteStorage::open_in_memory_with_key("journal-persist-key").unwrap();
        let storage = Arc::new(crate::MutexAgentStorage(Arc::new(std::sync::Mutex::new(
            sqlite,
        ))));

        let backend = SwiftideBackend::new(storage, None, temp_dir.path().to_path_buf(), true)
            .with_journal_path(&journal_path);

        #[derive(Clone)]
        struct SimpleMockLlm;
        #[async_trait]
        impl ChatCompletion for SimpleMockLlm {
            async fn complete(
                &self,
                _request: &ChatCompletionRequest,
            ) -> Result<
                swiftide_core::chat_completion::ChatCompletionResponse,
                swiftide_core::chat_completion::errors::LanguageModelError,
            > {
                Ok(swiftide_core::chat_completion::ChatCompletionResponse {
                    message: Some("Persistence verified".to_string()),
                    tool_calls: None,
                    ..Default::default()
                })
            }
        }

        backend.set_llm_override(Box::new(SimpleMockLlm));

        let session_id = "session-disk-persist-test";
        let user_msg = MahoChatMessage::user(ChatContent::text("test persist to disk"));
        let res = backend
            .run_turn(session_id, user_msg, None, None)
            .await
            .unwrap();

        assert_eq!(res.role, "assistant");

        // Verify journal file exists on disk
        assert!(
            journal_path.exists(),
            "Journal file must exist on disk after turn"
        );

        // Verify disk contents via RunJournal loaded from store
        let reloaded = crate::run_journal::RunJournal::from_fs_path_or_new(&journal_path);
        let run_id = crate::run_journal::AgentRunId::new(session_id);
        let entries = reloaded
            .get_entries(&run_id)
            .expect("Run entries must exist in reloaded journal");

        assert!(
            entries.iter().any(|e| e.event_name == "step_started"),
            "Must contain step_started event"
        );
        assert!(
            entries.iter().any(|e| e.event_name == "status"),
            "Must contain status event"
        );

        let checkpoints = reloaded
            .get_checkpoints(&run_id)
            .expect("Checkpoints must exist");
        assert!(!checkpoints.is_empty(), "Must have checkpoints");

        // Test mutating journal state writes through to disk immediately
        {
            let journal_arc = backend.run_journal();
            let mut j = journal_arc.lock_unpoison();
            j.append_event(&run_id, "custom_audit_op", r#"{"verified":true}"#)
                .unwrap();
        }

        let reloaded_after_mutation =
            crate::run_journal::RunJournal::from_fs_path_or_new(&journal_path);
        let updated_entries = reloaded_after_mutation.get_entries(&run_id).unwrap();
        assert!(
            updated_entries
                .iter()
                .any(|e| e.event_name == "custom_audit_op"),
            "Mutation must be on disk"
        );
    }

    #[tokio::test]
    async fn test_run_journal_attach_session_journal_default_path() {
        use maho_storage::sqlite::SqliteStorage;
        let temp_dir = tempfile::tempdir().unwrap();

        let sqlite = SqliteStorage::open_in_memory_with_key("journal-session-key").unwrap();
        let storage = Arc::new(crate::MutexAgentStorage(Arc::new(std::sync::Mutex::new(
            sqlite,
        ))));

        let backend = SwiftideBackend::new(storage, None, temp_dir.path().to_path_buf(), true);

        let session_id = "sess-default-store-1";
        backend.attach_session_journal(session_id, &temp_dir.path());

        #[derive(Clone)]
        struct SimpleMockLlm;
        #[async_trait]
        impl ChatCompletion for SimpleMockLlm {
            async fn complete(
                &self,
                _request: &ChatCompletionRequest,
            ) -> Result<
                swiftide_core::chat_completion::ChatCompletionResponse,
                swiftide_core::chat_completion::errors::LanguageModelError,
            > {
                Ok(swiftide_core::chat_completion::ChatCompletionResponse {
                    message: Some("Default path persistence verified".to_string()),
                    tool_calls: None,
                    ..Default::default()
                })
            }
        }

        backend.set_llm_override(Box::new(SimpleMockLlm));

        let user_msg = MahoChatMessage::user(ChatContent::text("default journal path test"));
        let _ = backend
            .run_turn(session_id, user_msg, None, None)
            .await
            .unwrap();

        let expected_path = temp_dir
            .path()
            .join(".maho")
            .join("journals")
            .join(format!("{session_id}.json"));
        assert!(
            expected_path.exists(),
            "Default session journal file must exist at {expected_path:?}"
        );

        let reloaded = crate::run_journal::RunJournal::from_fs_path_or_new(&expected_path);
        let run_id = crate::run_journal::AgentRunId::new(session_id);
        let entries = reloaded
            .get_entries(&run_id)
            .expect("Run entries must exist in default path");
        assert!(entries.iter().any(|e| e.event_name == "step_started"));
    }
}
