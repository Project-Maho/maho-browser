use crate::approval::{
    ApprovalDecisionMetadata, ApprovalDecisionReason, ApprovalDecisionSource, ApprovalPolicy,
    ToolSensitivity,
};
use crate::fallback_ladder::{
    FallbackDecision, FallbackDecisionReason, FallbackSafetyState, FallbackTier,
};
use crate::permission::PermissionDecision;

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ApprovalAuditEventKind {
    PermissionRequest,
    PermissionAllow,
    PermissionDeny,
    PermissionTimeout,
    PermissionCancel,
    PermissionDefaultFailClosed,
    PermissionDowngrade,
    FallbackDecision,
    FallbackDowngrade,
    FallbackEscalation,
    ToolExecutionResult,
}

impl ApprovalAuditEventKind {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::PermissionRequest => "permission_request",
            Self::PermissionAllow => "permission_allow",
            Self::PermissionDeny => "permission_deny",
            Self::PermissionTimeout => "permission_timeout",
            Self::PermissionCancel => "permission_cancel",
            Self::PermissionDefaultFailClosed => "permission_default_fail_closed",
            Self::PermissionDowngrade => "permission_downgrade",
            Self::FallbackDecision => "fallback_decision",
            Self::FallbackDowngrade => "fallback_downgrade",
            Self::FallbackEscalation => "fallback_escalation",
            Self::ToolExecutionResult => "tool_execution_result",
        }
    }
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
pub struct FallbackAuditFields {
    pub attempted_tier: FallbackTier,
    pub selected_tier: FallbackTier,
    pub reason: FallbackDecisionReason,
    pub safety_state: FallbackSafetyState,
    pub approval_required: bool,
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
pub struct ApprovalAuditRecord {
    pub kind: ApprovalAuditEventKind,
    pub session_id: String,
    pub tool_name: String,
    pub sensitivity: ToolSensitivity,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub attempted_sensitivity: Option<ToolSensitivity>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub selected_sensitivity: Option<ToolSensitivity>,
    pub policy: ApprovalPolicy,
    pub decision: Option<PermissionDecision>,
    pub decision_metadata: Option<ApprovalDecisionMetadata>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub fallback: Option<FallbackAuditFields>,
    pub sanitized_arguments: Option<String>,
    pub sanitized_result: Option<String>,
}

impl ApprovalAuditRecord {
    pub fn permission_request(
        session_id: &str,
        tool_name: &str,
        sensitivity: ToolSensitivity,
        policy: ApprovalPolicy,
        sanitized_arguments: &str,
    ) -> Self {
        Self {
            kind: ApprovalAuditEventKind::PermissionRequest,
            session_id: session_id.to_string(),
            tool_name: tool_name.to_string(),
            sensitivity,
            attempted_sensitivity: Option::None,
            selected_sensitivity: Option::None,
            policy,
            decision: Option::None,
            decision_metadata: Option::None,
            fallback: Option::None,
            sanitized_arguments: Some(sanitized_arguments.to_string()),
            sanitized_result: Option::None,
        }
    }

    pub fn decision(
        kind: ApprovalAuditEventKind,
        session_id: &str,
        tool_name: &str,
        sensitivity: ToolSensitivity,
        metadata: ApprovalDecisionMetadata,
    ) -> Self {
        Self {
            kind,
            session_id: session_id.to_string(),
            tool_name: tool_name.to_string(),
            sensitivity,
            attempted_sensitivity: Option::None,
            selected_sensitivity: Option::None,
            policy: metadata.policy,
            decision: Some(metadata.decision),
            decision_metadata: Some(metadata),
            fallback: Option::None,
            sanitized_arguments: Option::None,
            sanitized_result: Option::None,
        }
    }

    pub fn execution_result(
        session_id: &str,
        tool_name: &str,
        sensitivity: ToolSensitivity,
        policy: ApprovalPolicy,
        sanitized_result: &str,
    ) -> Self {
        Self {
            kind: ApprovalAuditEventKind::ToolExecutionResult,
            session_id: session_id.to_string(),
            tool_name: tool_name.to_string(),
            sensitivity,
            attempted_sensitivity: Option::None,
            selected_sensitivity: Option::None,
            policy,
            decision: Some(PermissionDecision::Allow),
            decision_metadata: Option::None,
            fallback: Option::None,
            sanitized_arguments: Option::None,
            sanitized_result: Some(sanitized_result.to_string()),
        }
    }

    pub fn cancellation(
        session_id: &str,
        tool_name: &str,
        sensitivity: ToolSensitivity,
        policy: ApprovalPolicy,
    ) -> Self {
        let metadata = ApprovalDecisionMetadata::new(
            PermissionDecision::Deny,
            policy,
            ApprovalDecisionSource::Runtime,
            ApprovalDecisionReason::SessionCancel,
        );
        Self {
            kind: ApprovalAuditEventKind::PermissionCancel,
            session_id: session_id.to_string(),
            tool_name: tool_name.to_string(),
            sensitivity,
            attempted_sensitivity: Option::None,
            selected_sensitivity: Option::None,
            policy,
            decision: Some(metadata.decision),
            decision_metadata: Some(metadata),
            fallback: Option::None,
            sanitized_arguments: Option::None,
            sanitized_result: Option::None,
        }
    }

    pub fn permission_cancel(session_id: &str, policy: ApprovalPolicy) -> Self {
        Self::cancellation(session_id, "session", ToolSensitivity::Sensitive, policy)
    }

    pub fn sensitivity_downgrade(
        session_id: &str,
        tool_name: &str,
        attempted_sensitivity: ToolSensitivity,
        selected_sensitivity: ToolSensitivity,
        policy: ApprovalPolicy,
    ) -> Self {
        Self {
            kind: ApprovalAuditEventKind::PermissionDowngrade,
            session_id: session_id.to_string(),
            tool_name: tool_name.to_string(),
            sensitivity: selected_sensitivity,
            attempted_sensitivity: Some(attempted_sensitivity),
            selected_sensitivity: Some(selected_sensitivity),
            policy,
            decision: Option::None,
            decision_metadata: Option::None,
            fallback: Option::None,
            sanitized_arguments: Option::None,
            sanitized_result: Option::None,
        }
    }

    pub fn fallback_decision(
        session_id: &str,
        tool_name: &str,
        sensitivity: ToolSensitivity,
        policy: ApprovalPolicy,
        decision: &FallbackDecision,
    ) -> Self {
        let kind = match decision.safety_state {
            FallbackSafetyState::Safe => ApprovalAuditEventKind::FallbackDecision,
            FallbackSafetyState::Downgraded | FallbackSafetyState::Blocked => {
                ApprovalAuditEventKind::FallbackDowngrade
            }
            FallbackSafetyState::EscalationApprovalRequired
            | FallbackSafetyState::EscalationApproved
            | FallbackSafetyState::EscalationDenied => ApprovalAuditEventKind::FallbackEscalation,
        };
        Self {
            kind,
            session_id: session_id.to_string(),
            tool_name: tool_name.to_string(),
            sensitivity,
            attempted_sensitivity: Option::None,
            selected_sensitivity: Option::None,
            policy,
            decision: Option::None,
            decision_metadata: Option::None,
            fallback: Some(FallbackAuditFields {
                attempted_tier: decision.attempted_tier,
                selected_tier: decision.selected_tier,
                reason: decision.reason,
                safety_state: decision.safety_state,
                approval_required: decision.approval_required,
            }),
            sanitized_arguments: Option::None,
            sanitized_result: Option::None,
        }
    }
}

#[cfg(test)]
static TEST_AUDIT_RECORDS: std::sync::Mutex<Vec<ApprovalAuditRecord>> =
    std::sync::Mutex::new(Vec::new());

#[cfg(test)]
fn test_audit_records() -> std::sync::MutexGuard<'static, Vec<ApprovalAuditRecord>> {
    TEST_AUDIT_RECORDS
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
}

#[cfg(test)]
pub(crate) fn take_test_audit_records_for_session(session_id: &str) -> Vec<ApprovalAuditRecord> {
    let mut records = test_audit_records();
    let mut matched = Vec::new();
    let mut retained = Vec::new();
    for record in std::mem::take(&mut *records) {
        if record.session_id == session_id {
            matched.push(record);
        } else {
            retained.push(record);
        }
    }
    *records = retained;
    matched
}

pub fn emit_audit_record(record: &ApprovalAuditRecord) {
    #[cfg(test)]
    test_audit_records().push(record.clone());

    journal_consequential_action(record);

    let record_json = match serde_json::to_string(record) {
        Ok(json) => json,
        Err(error) => serialization_error_json(&error),
    };
    let decision = record
        .decision
        .map(PermissionDecision::as_str)
        .unwrap_or("none");
    let decision_source = record
        .decision_metadata
        .map(|metadata| metadata.source.as_str())
        .unwrap_or("none");
    let decision_reason = record
        .decision_metadata
        .map(|metadata| metadata.reason.as_str())
        .unwrap_or("none");
    let fallback_attempted_tier = record
        .fallback
        .as_ref()
        .map(|fields| fields.attempted_tier.as_str())
        .unwrap_or("none");
    let fallback_selected_tier = record
        .fallback
        .as_ref()
        .map(|fields| fields.selected_tier.as_str())
        .unwrap_or("none");
    let fallback_reason = record
        .fallback
        .as_ref()
        .map(|fields| fields.reason.as_str())
        .unwrap_or("none");
    let fallback_safety_state = record
        .fallback
        .as_ref()
        .map(|fields| fields.safety_state.as_str())
        .unwrap_or("none");
    tracing::info!(
        target = "maho_agent::audit",
        event = %record.kind.as_str(),
        kind = %record.kind.as_str(),
        tool = %record.tool_name,
        tool_name = %record.tool_name,
        sensitivity = %record.sensitivity.as_str(),
        session_id = %record.session_id,
        policy = %record.policy.as_str(),
        decision = %decision,
        decision_source = %decision_source,
        decision_reason = %decision_reason,
        fallback_attempted_tier = %fallback_attempted_tier,
        fallback_selected_tier = %fallback_selected_tier,
        fallback_reason = %fallback_reason,
        fallback_safety_state = %fallback_safety_state,
        record = %record_json,
    );
}

/// Fallback audit payload when a record fails to serialize. Built with
/// serde_json so an error message containing quotes or control characters
/// can never break out of the JSON string and forge audit fields.
fn serialization_error_json(error: &impl std::fmt::Display) -> String {
    serde_json::json!({ "serialization_error": error.to_string() }).to_string()
}

// ---------------------------------------------------------------------------
// Persistent agent action journal (after-the-fact review, never a gate).
// ---------------------------------------------------------------------------

/// One consequential action handed to the journal sink.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AgentActionJournalRecord {
    pub ts: i64,
    pub session_id: String,
    /// `cli` | `mcp` | `panel`.
    pub actor: &'static str,
    /// See `maho_storage::agent_action_journal::AGENT_ACTION_KINDS`.
    pub action: &'static str,
    pub origin: String,
    pub summary: String,
    pub outcome: String,
}

/// Destination for journal records. Implementations must be cheap and must
/// never block the agent on user input; an `Err` is logged and dropped.
pub trait AgentActionJournalSink: Send + Sync {
    fn record(&self, record: &AgentActionJournalRecord) -> Result<(), String>;
}

static JOURNAL_SINK: std::sync::RwLock<Option<std::sync::Arc<dyn AgentActionJournalSink>>> =
    std::sync::RwLock::new(None);

/// Install (or clear with `None`) the process-wide journal sink. Hosts must
/// not install a sink for incognito / off-the-record profiles.
pub fn set_agent_action_journal_sink(sink: Option<std::sync::Arc<dyn AgentActionJournalSink>>) {
    *JOURNAL_SINK
        .write()
        .unwrap_or_else(std::sync::PoisonError::into_inner) = sink;
}

/// Inputs for classifying one tool execution into a journal action kind.
///
/// Mirrors `maho::ai::ClassifyJournalAction` in
/// `maho-chromium/browser/ai/maho_control_activity_service.cc`; keep the two
/// tables in sync with the real tool catalog
/// (`maho_browser_capability_catalog.def`).
#[derive(Clone, Copy, Debug, Default)]
pub struct JournalActionInput<'a> {
    pub tool_name: &'a str,
    /// Tool arguments as JSON (`None` / unparsable = no argument signals).
    pub arguments: Option<&'a serde_json::Value>,
    /// Browser-minted consequence for this action, e.g. `"submit"` for a
    /// click classified as a form submission. Never model-supplied.
    pub action_consequence: Option<&'a str>,
    /// Committed origin of the page before the action, when known.
    pub current_origin: Option<&'a str>,
}

/// Reserved journal kinds with no producing tool in the current catalog.
/// They stay in the storage enum so a future checkout / mail-delete tool can
/// journal without a schema change, but nothing maps to them today.
pub const RESERVED_JOURNAL_ACTIONS: &[&str] = &["purchase", "mail_delete"];

/// Normalize a URL to `scheme://host[:port]` (lowercased), or `None` when it
/// has no authority (e.g. `about:blank`, relative URLs).
pub fn origin_of(url: &str) -> Option<String> {
    let (scheme, rest) = url.split_once("://")?;
    if scheme.is_empty()
        || !scheme
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || "+-.".contains(c))
    {
        return None;
    }
    let authority = rest.split(['/', '?', '#']).next().unwrap_or("");
    let host_port = authority.rsplit('@').next().unwrap_or("");
    if host_port.is_empty() {
        return None;
    }
    let scheme = scheme.to_ascii_lowercase();
    let mut host_port = host_port.to_ascii_lowercase();
    let default_port = match scheme.as_str() {
        "http" => Some(":80"),
        "https" => Some(":443"),
        _ => None,
    };
    if let Some(port) = default_port {
        if let Some(stripped) = host_port.strip_suffix(port) {
            host_port = stripped.to_string();
        }
    }
    Some(format!("{scheme}://{host_port}"))
}

fn arg_bool(args: Option<&serde_json::Value>, key: &str) -> bool {
    args.and_then(|a| a.get(key))
        .and_then(serde_json::Value::as_bool)
        .unwrap_or(false)
}

fn arg_str<'a>(args: Option<&'a serde_json::Value>, key: &str) -> Option<&'a str> {
    args.and_then(|a| a.get(key))
        .and_then(serde_json::Value::as_str)
}

fn is_cross_origin(target_url: Option<&str>, current_origin: Option<&str>) -> bool {
    match (target_url.and_then(origin_of), current_origin) {
        (Some(target), Some(current)) => {
            let current = origin_of(current).unwrap_or_else(|| current.to_ascii_lowercase());
            target != current
        }
        _ => false,
    }
}

/// Classify a tool execution into a consequential journal action kind, or
/// `None` when it is not journaled (reads, scrolls, snapshots, ...).
pub fn classify_journal_action(input: &JournalActionInput<'_>) -> Option<&'static str> {
    let args = input.arguments;
    match input.tool_name {
        "mail_send" | "mail_queue_email" => Some("mail_send"),
        "vault_fill_credential" | "vault_fill_totp" => Some("credential_typing"),
        "browser_file_upload_select" => Some("file_upload"),
        "browser_type" | "browser_locator_type" => {
            if arg_bool(args, "allow_credentials") {
                Some("credential_typing")
            } else if arg_bool(args, "submit") {
                Some("form_submit")
            } else {
                None
            }
        }
        "browser_click" | "browser_locator_click" | "browser_visual_click" => {
            (input.action_consequence == Some("submit")).then_some("form_submit")
        }
        "browser_act_and_observe" => match arg_str(args, "action") {
            Some("type") if arg_bool(args, "submit") => Some("form_submit"),
            Some("click") if input.action_consequence == Some("submit") => Some("form_submit"),
            Some("navigate") if is_cross_origin(arg_str(args, "url"), input.current_origin) => {
                Some("cross_origin_navigation")
            }
            _ => None,
        },
        "browser_navigate" => is_cross_origin(arg_str(args, "url"), input.current_origin)
            .then_some("cross_origin_navigation"),
        _ => None,
    }
}

/// Name-only classification (no argument signals). Kept for callers that
/// only have the tool name.
pub fn consequential_action_for_tool(tool_name: &str) -> Option<&'static str> {
    classify_journal_action(&JournalActionInput {
        tool_name,
        ..Default::default()
    })
}

/// Infer the actor from the session id convention (`mcp:` / `cli:` prefixes);
/// everything else originates in the AI panel.
pub fn journal_actor_for_session(session_id: &str) -> &'static str {
    if session_id.starts_with("mcp:") || session_id.starts_with("mcp-") {
        "mcp"
    } else if session_id.starts_with("cli:") || session_id.starts_with("cli-") {
        "cli"
    } else {
        "panel"
    }
}

/// Persist consequential tool executions. Best-effort: a missing sink or a
/// sink failure is logged and the agent action proceeds unchanged. Returns
/// whether a record was written (for tests).
fn journal_consequential_action(record: &ApprovalAuditRecord) -> bool {
    if record.kind != ApprovalAuditEventKind::ToolExecutionResult {
        return false;
    }
    let arguments = record
        .sanitized_arguments
        .as_deref()
        .and_then(|raw| serde_json::from_str::<serde_json::Value>(raw).ok());
    let Some(action) = classify_journal_action(&JournalActionInput {
        tool_name: &record.tool_name,
        arguments: arguments.as_ref(),
        action_consequence: None,
        current_origin: None,
    }) else {
        return false;
    };
    let sink = JOURNAL_SINK
        .read()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
        .clone();
    let Some(sink) = sink else {
        return false;
    };
    let journal = AgentActionJournalRecord {
        ts: chrono::Utc::now().timestamp(),
        session_id: record.session_id.clone(),
        actor: journal_actor_for_session(&record.session_id),
        action,
        origin: String::new(),
        summary: record.tool_name.clone(),
        outcome: "completed".to_string(),
    };
    match sink.record(&journal) {
        Ok(()) => true,
        Err(error) => {
            tracing::warn!(
                target = "maho_agent::audit",
                event = "agent_action_journal_write_failed",
                action = %action,
                error = %error,
                "journal write failed; agent action continues"
            );
            false
        }
    }
}

/// SQLite-backed sink writing to the profile database's
/// `agent_action_journal` table. Opens the database per write so it never
/// holds a lock across agent turns.
pub struct SqliteAgentActionJournalSink {
    db_path: String,
}

impl SqliteAgentActionJournalSink {
    pub fn new(db_path: impl Into<String>) -> Self {
        Self {
            db_path: db_path.into(),
        }
    }
}

impl AgentActionJournalSink for SqliteAgentActionJournalSink {
    fn record(&self, record: &AgentActionJournalRecord) -> Result<(), String> {
        let storage =
            maho_storage::sqlite::SqliteStorage::open(&self.db_path).map_err(|e| e.to_string())?;
        storage
            .record_agent_action(&maho_storage::agent_action_journal::NewAgentAction {
                ts: record.ts,
                session_id: &record.session_id,
                actor: record.actor,
                action: record.action,
                origin: &record.origin,
                summary: &record.summary,
                outcome: &record.outcome,
            })
            .map(|_| ())
            .map_err(|e| e.to_string())
    }
}

#[cfg(test)]
mod journal_tests {
    use super::*;
    use crate::approval::{ApprovalPolicy, ToolSensitivity};
    use std::sync::{Arc, Mutex};

    // The sink is process-global; serialize the tests that install one.
    static SINK_TEST_LOCK: Mutex<()> = Mutex::new(());

    struct FailingSink;
    impl AgentActionJournalSink for FailingSink {
        fn record(&self, _: &AgentActionJournalRecord) -> Result<(), String> {
            Err("disk full".into())
        }
    }

    struct CollectingSink(Mutex<Vec<AgentActionJournalRecord>>);
    impl AgentActionJournalSink for CollectingSink {
        fn record(&self, record: &AgentActionJournalRecord) -> Result<(), String> {
            self.0.lock().unwrap().push(record.clone());
            Ok(())
        }
    }

    fn exec(session: &str, tool: &str) -> ApprovalAuditRecord {
        ApprovalAuditRecord::execution_result(
            session,
            tool,
            ToolSensitivity::Sensitive,
            ApprovalPolicy::Prompt,
            "ok",
        )
    }

    #[test]
    fn journal_records_consequential_tools_only() {
        let _guard = SINK_TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let sink = Arc::new(CollectingSink(Mutex::new(Vec::new())));
        set_agent_action_journal_sink(Some(sink.clone()));
        emit_audit_record(&exec("mcp:journal-1", "mail_send"));
        emit_audit_record(&exec("journal-1", "browser_scroll"));
        emit_audit_record(&exec("cli:journal-1", "vault_fill_credential"));
        set_agent_action_journal_sink(None);
        let _ = take_test_audit_records_for_session("mcp:journal-1");
        let _ = take_test_audit_records_for_session("journal-1");
        let _ = take_test_audit_records_for_session("cli:journal-1");

        let got = sink.0.lock().unwrap().clone();
        assert_eq!(got.len(), 2);
        assert_eq!((got[0].actor, got[0].action), ("mcp", "mail_send"));
        assert_eq!((got[1].actor, got[1].action), ("cli", "credential_typing"));
    }

    fn classify(
        tool: &str,
        args: serde_json::Value,
        consequence: Option<&str>,
        current: Option<&str>,
    ) -> Option<&'static str> {
        classify_journal_action(&JournalActionInput {
            tool_name: tool,
            arguments: Some(&args),
            action_consequence: consequence,
            current_origin: current,
        })
    }

    #[test]
    fn journal_classifier_matches_real_catalog_tools() {
        use serde_json::json;
        assert_eq!(
            classify("mail_send", json!({}), None, None),
            Some("mail_send")
        );
        assert_eq!(
            classify("mail_queue_email", json!({}), None, None),
            Some("mail_send")
        );
        assert_eq!(
            classify("vault_fill_credential", json!({}), None, None),
            Some("credential_typing")
        );
        assert_eq!(
            classify("vault_fill_totp", json!({}), None, None),
            Some("credential_typing")
        );
        assert_eq!(
            classify(
                "browser_type",
                json!({"allow_credentials": true, "submit": true}),
                None,
                None
            ),
            Some("credential_typing")
        );
        assert_eq!(
            classify("browser_type", json!({"submit": true}), None, None),
            Some("form_submit")
        );
        assert_eq!(
            classify("browser_type", json!({"text": "hi"}), None, None),
            None
        );
        assert_eq!(
            classify(
                "browser_act_and_observe",
                json!({"action": "type", "submit": true}),
                None,
                None
            ),
            Some("form_submit")
        );
        assert_eq!(
            classify("browser_click", json!({}), Some("submit"), None),
            Some("form_submit")
        );
        assert_eq!(
            classify("browser_click", json!({}), Some("ordinary"), None),
            None
        );
        assert_eq!(
            classify(
                "browser_navigate",
                json!({"url": "https://other.example/path"}),
                None,
                Some("https://a.example")
            ),
            Some("cross_origin_navigation")
        );
        assert_eq!(
            classify(
                "browser_navigate",
                json!({"url": "https://a.example:443/next"}),
                None,
                Some("https://a.example")
            ),
            None,
            "same origin is not journaled"
        );
        assert_eq!(
            classify(
                "browser_navigate",
                json!({"url": "https://b.example"}),
                None,
                None
            ),
            None,
            "unknown current origin is not guessed"
        );
        assert_eq!(
            classify(
                "browser_act_and_observe",
                json!({"action": "navigate", "url": "http://b.example"}),
                None,
                Some("https://b.example")
            ),
            Some("cross_origin_navigation")
        );
        assert_eq!(
            classify("browser_file_upload_select", json!({}), None, None),
            Some("file_upload")
        );
        assert_eq!(classify("browser_scroll", json!({}), None, None), None);
        // Phantom names that do not exist in the catalog are no longer mapped.
        for phantom in [
            "browser_form_submit",
            "browser_submit",
            "browser_purchase",
            "browser_checkout",
            "browser_navigate_cross_origin",
            "browser_file_upload",
            "mail_delete",
            "mail_delete_email",
            "mail_trash",
        ] {
            assert_eq!(classify(phantom, json!({}), None, None), None, "{phantom}");
        }
        // Reserved kinds remain valid storage values.
        for reserved in RESERVED_JOURNAL_ACTIONS {
            assert!(maho_storage::agent_action_journal::AGENT_ACTION_KINDS.contains(reserved));
        }
    }

    #[test]
    fn journal_records_browser_type_submit_from_audit_arguments() {
        let _guard = SINK_TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let sink = Arc::new(CollectingSink(Mutex::new(Vec::new())));
        set_agent_action_journal_sink(Some(sink.clone()));
        let mut record = exec("journal-args", "browser_type");
        record.sanitized_arguments = Some(r#"{"text":"q","submit":true}"#.to_string());
        emit_audit_record(&record);
        set_agent_action_journal_sink(None);
        let _ = take_test_audit_records_for_session("journal-args");
        let got = sink.0.lock().unwrap().clone();
        assert_eq!(got.len(), 1);
        assert_eq!(got[0].action, "form_submit");
    }

    #[test]
    fn journal_write_failure_is_logged_and_non_blocking() {
        let _guard = SINK_TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        set_agent_action_journal_sink(Some(Arc::new(FailingSink)));
        let record = exec("journal-fail", "mail_send");
        // Must return normally (no panic, no wait) even though the sink fails.
        assert!(!journal_consequential_action(&record));
        emit_audit_record(&record);
        set_agent_action_journal_sink(None);
        let emitted = take_test_audit_records_for_session("journal-fail");
        assert_eq!(emitted.len(), 1, "audit emission still completes");
    }

    #[test]
    fn journal_sqlite_sink_round_trips() {
        let _guard = SINK_TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("journal.db");
        let path = path.to_str().unwrap().to_string();
        crate::install_test_sqlcipher_key();
        let sink = SqliteAgentActionJournalSink::new(path.clone());
        sink.record(&AgentActionJournalRecord {
            ts: 1,
            session_id: "s".into(),
            actor: "panel",
            action: "file_upload",
            origin: "https://a.example".into(),
            summary: "browser_file_upload_select".into(),
            outcome: "completed".into(),
        })
        .unwrap();
        let storage = maho_storage::sqlite::SqliteStorage::open(&path).unwrap();
        let rows = storage.list_agent_actions(&Default::default(), 10).unwrap();
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].action, "file_upload");
    }
}

#[cfg(test)]
mod serialization_fallback_tests {
    use super::serialization_error_json;

    #[test]
    fn serialization_error_fallback_escapes_injected_json() {
        let hostile = r#"x","decision":"allow","y":"
"#;
        let json = serialization_error_json(&hostile);
        let parsed: serde_json::Value =
            serde_json::from_str(&json).expect("fallback must stay valid JSON");
        let object = parsed.as_object().expect("fallback is a JSON object");
        assert_eq!(object.len(), 1, "injected fields leaked: {json}");
        assert_eq!(object["serialization_error"], hostile);
    }
}
