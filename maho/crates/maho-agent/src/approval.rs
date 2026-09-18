use crate::browser_action_contract::{canonical_browser_action_contracts, BrowserToolSensitivity};
use crate::permission::PermissionDecision;

pub use crate::approval_audit::{emit_audit_record, ApprovalAuditEventKind, ApprovalAuditRecord};

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ApprovalPolicy {
    Prompt,
    AllowAll,
    AllowMcp,
    DenySensitive,
    DenyAll,
}

impl Default for ApprovalPolicy {
    fn default() -> Self {
        Self::Prompt
    }
}

impl ApprovalPolicy {
    pub fn from_legacy_pref(policy: &str) -> Self {
        match policy.trim().to_ascii_lowercase().as_str() {
            "prompt" | "ask" | "default" => Self::Prompt,
            "allow" | "allow-all" | "allow_all" | "yolo" => Self::AllowAll,
            "allow-mcp" | "allow_mcp" => Self::AllowMcp,
            "deny-sensitive" | "deny_sensitive" => Self::DenySensitive,
            "deny" | "deny-all" | "deny_all" => Self::DenyAll,
            _ => Self::DenyAll,
        }
    }

    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Prompt => "prompt",
            Self::AllowAll => "allow_all",
            Self::AllowMcp => "allow_mcp",
            Self::DenySensitive => "deny_sensitive",
            Self::DenyAll => "deny_all",
        }
    }

    pub fn immediate_decision(
        self,
        tool_name: &str,
        sensitivity: ToolSensitivity,
    ) -> Option<ApprovalDecisionMetadata> {
        match (self, sensitivity) {
            (Self::DenyAll, _) => Some(ApprovalDecisionMetadata::new(
                PermissionDecision::Deny,
                self,
                ApprovalDecisionSource::Policy,
                ApprovalDecisionReason::PolicyDenyAll,
            )),
            (Self::AllowAll, _) => Some(ApprovalDecisionMetadata::new(
                PermissionDecision::Allow,
                self,
                ApprovalDecisionSource::Policy,
                ApprovalDecisionReason::PolicyAllowAll,
            )),
            (Self::DenySensitive, ToolSensitivity::Sensitive) => {
                Some(ApprovalDecisionMetadata::new(
                    PermissionDecision::Deny,
                    self,
                    ApprovalDecisionSource::Policy,
                    ApprovalDecisionReason::PolicyDenySensitive,
                ))
            }
            (Self::AllowMcp, ToolSensitivity::Sensitive) if is_mcp_tool_name(tool_name) => {
                Some(ApprovalDecisionMetadata::new(
                    PermissionDecision::Allow,
                    self,
                    ApprovalDecisionSource::Policy,
                    ApprovalDecisionReason::PolicyAllowMcp,
                ))
            }
            (Self::AllowMcp, ToolSensitivity::Sensitive) => Some(ApprovalDecisionMetadata::new(
                PermissionDecision::Deny,
                self,
                ApprovalDecisionSource::Policy,
                ApprovalDecisionReason::PolicyDenySensitive,
            )),
            (_, ToolSensitivity::ReadOnly) => Some(ApprovalDecisionMetadata::new(
                PermissionDecision::Allow,
                self,
                ApprovalDecisionSource::Policy,
                ApprovalDecisionReason::PolicyReadOnlyAutoAllow,
            )),
            (Self::Prompt, ToolSensitivity::Sensitive) => None,
        }
    }
}

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ToolSensitivity {
    ReadOnly,
    Sensitive,
}

impl ToolSensitivity {
    pub fn from_sensitive_flag(sensitive: bool) -> Self {
        if sensitive {
            Self::Sensitive
        } else {
            Self::ReadOnly
        }
    }

    pub fn for_tool_name(tool_name: &str, fallback_sensitive: bool) -> Self {
        ToolSensitivityResolution::for_tool_name(tool_name, fallback_sensitive).effective
    }

    pub const fn as_str(self) -> &'static str {
        match self {
            Self::ReadOnly => "read_only",
            Self::Sensitive => "sensitive",
        }
    }

    const fn from_browser_contract(sensitivity: BrowserToolSensitivity) -> Self {
        match sensitivity {
            BrowserToolSensitivity::Low => Self::ReadOnly,
            BrowserToolSensitivity::Sensitive => Self::Sensitive,
        }
    }
}

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
pub struct ToolSensitivityDowngrade {
    pub from: ToolSensitivity,
    pub to: ToolSensitivity,
}

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
pub struct ToolSensitivityResolution {
    pub effective: ToolSensitivity,
    pub downgrade: Option<ToolSensitivityDowngrade>,
}

impl ToolSensitivityResolution {
    pub fn for_tool_name(tool_name: &str, fallback_sensitive: bool) -> Self {
        let fallback = ToolSensitivity::from_sensitive_flag(fallback_sensitive);
        let raw_name = raw_tool_name(tool_name);
        let canonical = match canonical_browser_action_contracts() {
            Ok(contracts) => contracts
                .iter()
                .find(|contract| contract.tool_name == raw_name)
                .map(|contract| ToolSensitivity::from_browser_contract(contract.sensitivity)),
            Err(_) => Option::None,
        };
        let effective = canonical.unwrap_or(fallback);
        let downgrade = match (fallback, effective, canonical) {
            (ToolSensitivity::Sensitive, ToolSensitivity::ReadOnly, Some(_)) => {
                Some(ToolSensitivityDowngrade {
                    from: fallback,
                    to: effective,
                })
            }
            _ => Option::None,
        };

        Self {
            effective,
            downgrade,
        }
    }
}

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ApprovalDecisionSource {
    Policy,
    Callback,
    MissingCallback,
    Timeout,
    PriorDeny,
    Runtime,
}

impl ApprovalDecisionSource {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Policy => "policy",
            Self::Callback => "callback",
            Self::MissingCallback => "missing_callback",
            Self::Timeout => "timeout",
            Self::PriorDeny => "prior_deny",
            Self::Runtime => "runtime",
        }
    }
}

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ApprovalDecisionReason {
    PolicyAllowAll,
    PolicyAllowMcp,
    PolicyReadOnlyAutoAllow,
    PolicyDenyAll,
    PolicyDenySensitive,
    UserAllow,
    UserDeny,
    MissingCallback,
    CallbackTimeout,
    PriorDeny,
    SessionCancel,
    /// Runtime-tier gate refusal (Wave 1D); the machine-consumed tier code
    /// rides the denial message, the audit reason only marks the class.
    TierDeny,
}

impl ApprovalDecisionReason {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::PolicyAllowAll => "policy_allow_all",
            Self::PolicyAllowMcp => "policy_allow_mcp",
            Self::PolicyReadOnlyAutoAllow => "policy_read_only_auto_allow",
            Self::PolicyDenyAll => "policy_deny_all",
            Self::PolicyDenySensitive => "policy_deny_sensitive",
            Self::UserAllow => "user_allow",
            Self::UserDeny => "user_deny",
            Self::MissingCallback => "missing_callback",
            Self::CallbackTimeout => "callback_timeout",
            Self::PriorDeny => "prior_deny",
            Self::SessionCancel => "session_cancel",
            Self::TierDeny => "tier_deny",
        }
    }
}

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
pub struct ApprovalDecisionMetadata {
    pub decision: PermissionDecision,
    pub policy: ApprovalPolicy,
    pub source: ApprovalDecisionSource,
    pub reason: ApprovalDecisionReason,
}

impl ApprovalDecisionMetadata {
    pub const fn new(
        decision: PermissionDecision,
        policy: ApprovalPolicy,
        source: ApprovalDecisionSource,
        reason: ApprovalDecisionReason,
    ) -> Self {
        Self {
            decision,
            policy,
            source,
            reason,
        }
    }
}

fn raw_tool_name(tool_name: &str) -> &str {
    match tool_name.rsplit_once('/') {
        Some((_, raw_name)) => raw_name,
        Option::None => tool_name,
    }
}

fn is_mcp_tool_name(tool_name: &str) -> bool {
    tool_name.starts_with("mcp:") || tool_name.starts_with("mcp_")
}
