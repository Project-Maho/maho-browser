use crate::browser_action_contract::{canonical_browser_action_contracts, BrowserToolSensitivity};
use crate::permission::PermissionDecision;
use maho_types::tool::ToolProvenance;

pub use crate::approval_audit::{emit_audit_record, ApprovalAuditEventKind, ApprovalAuditRecord};

#[derive(Clone, Copy, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ApprovalPolicy {
    #[default]
    Prompt,
    AllowAll,
    AllowMcp,
    DenySensitive,
    DenyAll,
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
        _tool_name: &str,
        sensitivity: ToolSensitivity,
    ) -> Option<ApprovalDecisionMetadata> {
        // Provenance unknown: AllowMcp must never trust a name prefix (a
        // builtin or model-chosen name can start with `mcp_`), so it fails
        // closed to the sensitive-deny branch.
        self.decide(sensitivity, false)
    }

    /// Provenance-aware variant: `AllowMcp` auto-allows a sensitive tool only
    /// when the registry recorded it as coming from an external MCP server.
    pub fn immediate_decision_with_provenance(
        self,
        sensitivity: ToolSensitivity,
        provenance: &ToolProvenance,
    ) -> Option<ApprovalDecisionMetadata> {
        self.decide(sensitivity, matches!(provenance, ToolProvenance::External(_)))
    }

    fn decide(
        self,
        sensitivity: ToolSensitivity,
        is_external_mcp: bool,
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
            (Self::AllowMcp, ToolSensitivity::Sensitive) if is_external_mcp => {
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
        let (raw_name, first_party) = raw_tool_name(tool_name);
        let canonical_sensitivity = match canonical_browser_action_contracts() {
            Ok(contracts) => contracts
                .iter()
                .find(|contract| contract.tool_name == raw_name)
                .map(|contract| ToolSensitivity::from_browser_contract(contract.sensitivity)),
            Err(_) => Option::None,
        };
        // Only bare (first-party builtin) names may resolve to a canonical
        // contract. A namespaced foreign tool whose leaf impersonates a
        // canonical name is forced Sensitive instead of inheriting it.
        let canonical = if first_party {
            canonical_sensitivity
        } else {
            canonical_sensitivity.map(|_| ToolSensitivity::Sensitive)
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

/// `(leaf, first_party)`: builtin tools are registered bare; any namespaced
/// (`server/tool`, `mcp:...`, `sh:...`) name is foreign.
fn raw_tool_name(tool_name: &str) -> (&str, bool) {
    match tool_name.rsplit_once('/') {
        Some((_, raw_name)) => (raw_name, false),
        Option::None => (tool_name, !tool_name.contains(':')),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn allow_mcp_decides_on_provenance_not_name_prefix() {
        // A name prefix is not provenance: without it AllowMcp fails closed.
        for name in ["mcp_exfiltrate", "mcp:srv/tool"] {
            let decision = ApprovalPolicy::AllowMcp
                .immediate_decision(name, ToolSensitivity::Sensitive)
                .expect("AllowMcp decides sensitive tools immediately");
            assert_eq!(decision.decision, PermissionDecision::Deny, "{name}");
        }
        let external = ApprovalPolicy::AllowMcp
            .immediate_decision_with_provenance(
                ToolSensitivity::Sensitive,
                &ToolProvenance::External("srv".into()),
            )
            .unwrap();
        assert_eq!(external.decision, PermissionDecision::Allow);
        assert_eq!(external.reason, ApprovalDecisionReason::PolicyAllowMcp);
        for provenance in [ToolProvenance::BuiltinBrowser, ToolProvenance::AgentLocal] {
            let decision = ApprovalPolicy::AllowMcp
                .immediate_decision_with_provenance(ToolSensitivity::Sensitive, &provenance)
                .unwrap();
            assert_eq!(decision.decision, PermissionDecision::Deny, "{provenance:?}");
        }
    }

    #[test]
    fn namespaced_tool_cannot_inherit_canonical_low_sensitivity() {
        for name in ["evil/browser_accessibility_snapshot", "mcp:evil/browser_observe"] {
            let resolution = ToolSensitivityResolution::for_tool_name(name, false);
            assert_eq!(resolution.effective, ToolSensitivity::Sensitive, "{name}");
            assert_eq!(resolution.downgrade, None, "{name}");
        }
        assert_eq!(
            ToolSensitivityResolution::for_tool_name("browser_accessibility_snapshot", true)
                .effective,
            ToolSensitivity::ReadOnly
        );
        // Non-colliding foreign tools keep their declared sensitivity.
        assert_eq!(
            ToolSensitivityResolution::for_tool_name("mcp:srv/lookup", false).effective,
            ToolSensitivity::ReadOnly
        );
    }
}
