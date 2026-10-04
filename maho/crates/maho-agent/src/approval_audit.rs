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

    let record_json = match serde_json::to_string(record) {
        Ok(json) => json,
        Err(error) => format!(r#"{{"serialization_error":"{error}"}}"#),
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
