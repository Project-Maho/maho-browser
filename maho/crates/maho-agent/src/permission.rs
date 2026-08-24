use std::future::Future;
use std::pin::Pin;

use crate::approval::{ApprovalPolicy, ToolSensitivity};
use crate::browser_action_contract::{
    canonical_browser_action_contracts, BrowserActionContract, BrowserToolKind,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MailToolClass {
    Read,
    WriteAccount,
}

pub const MAIL_READ_TOOLS: &[&str] = &[
    "mail_list_accounts",
    "mail_list_folders",
    "mail_list_emails",
    "mail_get_email",
    "mail_search_emails",
    "mail_list_thread",
    "mail_extract_otp",
];

pub const MAIL_WRITE_ACCOUNT_TOOLS: &[&str] = &[
    "mail_save_draft",
    "mail_update_draft",
    "mail_send",
    "mail_queue_email",
    "mail_flag",
    "mail_test_connection",
    "mail_add_account",
    "mail_delete_account",
    "mail_reconnect_account",
    "mail_start_oauth",
    "mail_complete_oauth",
    "mail_import_migration_archive",
];

pub const CREDENTIAL_TYPING_APPROVAL_REQUIRED: &str =
    "credential_typing_approval_required";
pub const CREDENTIAL_TYPING_DENIED: &str = "credential_typing_denied";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CredentialTypingAuthorizationAction {
    Allow,
    RequireApproval(&'static str),
}

pub const fn authorize_credential_typing(
    allow_credentials_requested: bool,
) -> CredentialTypingAuthorizationAction {
    if !allow_credentials_requested {
        CredentialTypingAuthorizationAction::Allow
    } else {
        CredentialTypingAuthorizationAction::RequireApproval(
            CREDENTIAL_TYPING_APPROVAL_REQUIRED,
        )
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct MailAuthorizationState {
    pub feature_enabled: bool,
    pub helper_ready: bool,
    pub helper_starting: bool,
    pub read_allowed: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MailAuthorizationAction {
    Allow,
    RequireApproval,
    Deny(&'static str),
}

pub fn classify_mail_tool(tool_name: &str) -> Option<MailToolClass> {
    MAIL_READ_TOOLS
        .contains(&tool_name)
        .then_some(MailToolClass::Read)
        .or_else(|| {
            MAIL_WRITE_ACCOUNT_TOOLS
                .contains(&tool_name)
                .then_some(MailToolClass::WriteAccount)
        })
}

pub fn authorize_mail_tool(
    tool_name: &str,
    state: MailAuthorizationState,
    policy: crate::ApprovalPolicy,
) -> MailAuthorizationAction {
    let Some(tool_class) = classify_mail_tool(tool_name) else {
        return MailAuthorizationAction::Deny("mail_tool_unknown");
    };
    if !state.feature_enabled {
        return MailAuthorizationAction::Deny("mail_feature_disabled");
    }
    if !state.helper_ready {
        return MailAuthorizationAction::Deny(if state.helper_starting {
            "mail_helper_starting"
        } else {
            "mail_helper_unavailable"
        });
    }
    if policy == crate::ApprovalPolicy::DenyAll {
        return MailAuthorizationAction::Deny("mail_global_policy_denied");
    }
    match tool_class {
        MailToolClass::Read if !state.read_allowed => {
            MailAuthorizationAction::Deny("mail_read_consent_required")
        }
        MailToolClass::Read => MailAuthorizationAction::Allow,
        MailToolClass::WriteAccount => MailAuthorizationAction::RequireApproval,
    }
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct PermissionRequest {
    pub tool_name: String,
    pub arguments: String,
    pub sensitivity: ToolSensitivity,
}

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum PermissionDecision {
    Allow,
    Deny,
}

impl PermissionDecision {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Allow => "allow",
            Self::Deny => "deny",
        }
    }
}

impl PermissionRequest {
    pub fn new(tool_name: impl Into<String>, raw_args: &str, sensitivity: ToolSensitivity) -> Self {
        let tool_name = tool_name.into();
        Self {
            arguments: sanitize_tool_arguments_for_tool(&tool_name, raw_args),
            tool_name,
            sensitivity,
        }
    }
}

// Sanitizes raw tool-call arguments at the agent trust boundary. Valid JSON is
// recursively redacted by key; anything that does not parse as JSON fails
// closed to a fixed marker so a malformed blob can never smuggle a secret
// through verbatim. This is the single chokepoint for permission payloads and
// stream tool-call args.
pub fn sanitize_tool_arguments(raw: &str) -> String {
    sanitize_json_payload(raw, "\"[REDACTED_INVALID_ARGUMENTS]\"")
}

pub fn sanitize_tool_arguments_for_tool(tool_name: &str, raw: &str) -> String {
    let mut value = match serde_json::from_str::<serde_json::Value>(raw) {
        Ok(value) => value,
        Err(_) => return "\"[REDACTED_INVALID_ARGUMENTS]\"".to_string(),
    };
    redact_value(&mut value);
    if classify_mail_tool(tool_name).is_some() {
        return serde_json::json!("[REDACTED]").to_string();
    }
    if tool_name == "browser_type" {
        if let Some(text) = value.get_mut("text") {
            *text = serde_json::json!("[REDACTED]");
        }
    }
    value.to_string()
}

// Sanitizes a tool result before it is emitted on the stream / persisted to
// history. Same boundary as tool-call args: JSON is recursively key-redacted
// (nested objects and arrays), and non-JSON / malformed output fails closed so
// a secret can never ride out through a broken result blob.
pub fn sanitize_tool_result(raw: &str) -> String {
    sanitize_json_payload(raw, "\"[REDACTED_INVALID_RESULT]\"")
}

fn sanitize_json_payload(raw: &str, invalid_marker: &str) -> String {
    match serde_json::from_str::<serde_json::Value>(raw) {
        Ok(mut val) => {
            redact_value(&mut val);
            val.to_string()
        }
        Err(_) => invalid_marker.to_string(),
    }
}

fn key_is_sensitive(key_lower: &str) -> bool {
    const SENSITIVE_FRAGMENTS: [&str; 9] = [
        "password",
        "passcode",
        "secret",
        "totp",
        "recovery",
        "otp",
        "one-time",
        "onetime",
        "credential",
    ];
    SENSITIVE_FRAGMENTS
        .iter()
        .any(|fragment| key_lower.contains(fragment))
}

fn redact_value(val: &mut serde_json::Value) {
    match val {
        serde_json::Value::Object(map) => {
            for (k, v) in map.iter_mut() {
                if key_is_sensitive(&k.to_lowercase()) {
                    *v = serde_json::json!("[REDACTED]");
                } else {
                    redact_value(v);
                }
            }
        }
        serde_json::Value::Array(arr) => {
            for item in arr.iter_mut() {
                redact_value(item);
            }
        }
        _ => {}
    }
}

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ActionConsequence {
    Ordinary,
    Submit,
    PurchaseOrTransfer,
    AccountSecurity,
    CredentialFill,
    FileTransfer,
    Destructive,
    NewOrigin,
    Unknown,
}

impl ActionConsequence {
    pub const fn requires_approval(self) -> bool {
        !matches!(self, Self::Ordinary)
    }

    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Ordinary => "ordinary",
            Self::Submit => "submit",
            Self::PurchaseOrTransfer => "purchase_or_transfer",
            Self::AccountSecurity => "account_security",
            Self::CredentialFill => "credential_fill",
            Self::FileTransfer => "file_transfer",
            Self::Destructive => "destructive",
            Self::NewOrigin => "new_origin",
            Self::Unknown => "unknown",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ConsequenceApprovalAction {
    NotApplicable,
    Allow,
    RequireApproval,
    Deny,
}

fn canonical_action_contract(tool_name: &str) -> Option<BrowserActionContract<'static>> {
    canonical_browser_action_contracts()
        .ok()?
        .into_iter()
        .find(|contract| contract.tool_name == tool_name)
}

fn declared_validated_consequence(arguments: &serde_json::Value) -> Option<ActionConsequence> {
    let context = arguments.get("validated_action_context")?.as_object()?;
    if context
        .get("validated")
        .and_then(serde_json::Value::as_bool)
        != Some(true)
    {
        return None;
    }
    match context.get("consequence")?.as_str()? {
        "ordinary" => Some(ActionConsequence::Ordinary),
        "submit" => Some(ActionConsequence::Submit),
        "purchase" | "transfer" | "purchase_or_transfer" => {
            Some(ActionConsequence::PurchaseOrTransfer)
        }
        "account_security" => Some(ActionConsequence::AccountSecurity),
        "credential_fill" => Some(ActionConsequence::CredentialFill),
        "upload" | "download" | "share" | "file_transfer" => Some(ActionConsequence::FileTransfer),
        "destructive" => Some(ActionConsequence::Destructive),
        "new_origin" => Some(ActionConsequence::NewOrigin),
        "unknown" => Some(ActionConsequence::Unknown),
        _ => None,
    }
}

pub fn classify_action_consequence(tool_name: &str, raw_args: &str) -> ActionConsequence {
    let raw_name = tool_name
        .rsplit_once('/')
        .map_or(tool_name, |(_, name)| name);
    let Some(contract) = canonical_action_contract(raw_name) else {
        return match raw_name {
            "vault_request_credential_use" | "vault_fill_credential" | "vault_fill_totp" => {
                ActionConsequence::CredentialFill
            }
            name if name.contains("upload")
                || name.contains("download")
                || name.contains("share") =>
            {
                ActionConsequence::FileTransfer
            }
            _ => ActionConsequence::Unknown,
        };
    };
    if contract.kind != BrowserToolKind::Action {
        return ActionConsequence::Ordinary;
    }

    let arguments = match serde_json::from_str::<serde_json::Value>(raw_args) {
        Ok(value) => value,
        Err(_) => return ActionConsequence::Unknown,
    };
    if let Some(consequence) = declared_validated_consequence(&arguments) {
        return consequence;
    }

    match raw_name {
        // These actions do not commit page state by themselves. Existing tab,
        // origin, lease, and gesture checks still execute in the browser.
        "browser_scroll" | "browser_hover" => ActionConsequence::Ordinary,
        // Destination-origin equality is browser-owned. Without a validated
        // context, navigation is conservatively treated as an escalation.
        "browser_navigate" => ActionConsequence::NewOrigin,
        // Ref-based targets need browser validation before their consequence is
        // known. Missing context must never turn a click/type/select/key into a
        // blanket auto-approval.
        "browser_click" | "browser_type" | "browser_select" | "browser_key_press" => {
            ActionConsequence::Unknown
        }
        _ => ActionConsequence::Unknown,
    }
}

pub fn authorize_action_consequence(
    tool_name: &str,
    raw_args: &str,
    policy: ApprovalPolicy,
) -> ConsequenceApprovalAction {
    let raw_name = tool_name
        .rsplit_once('/')
        .map_or(tool_name, |(_, name)| name);
    let applies = canonical_action_contract(raw_name)
        .is_some_and(|contract| contract.kind == BrowserToolKind::Action)
        || matches!(
            raw_name,
            "vault_request_credential_use" | "vault_fill_credential" | "vault_fill_totp"
        )
        || raw_name.contains("upload")
        || raw_name.contains("download")
        || raw_name.contains("share");
    if !applies {
        return ConsequenceApprovalAction::NotApplicable;
    }
    let consequence = classify_action_consequence(tool_name, raw_args);
    match policy {
        ApprovalPolicy::DenyAll => ConsequenceApprovalAction::Deny,
        ApprovalPolicy::DenySensitive if consequence.requires_approval() => {
            ConsequenceApprovalAction::Deny
        }
        ApprovalPolicy::Prompt | ApprovalPolicy::AllowAll | ApprovalPolicy::AllowMcp
            if consequence.requires_approval() =>
        {
            ConsequenceApprovalAction::RequireApproval
        }
        _ => ConsequenceApprovalAction::Allow,
    }
}

pub type PermissionCallback = Box<
    dyn Fn(PermissionRequest) -> Pin<Box<dyn Future<Output = PermissionDecision> + Send>>
        + Send
        + Sync
        + 'static,
>;

#[cfg(test)]
mod tests {
    use super::*;

    const SENTINEL: &str = "S3NTINEL-maho-vault-9F4C";

    #[test]
    fn credential_typing_authorization_contract_requires_explicit_approval() {
        assert_eq!(
            CREDENTIAL_TYPING_APPROVAL_REQUIRED,
            "credential_typing_approval_required"
        );
        assert_eq!(CREDENTIAL_TYPING_DENIED, "credential_typing_denied");
        assert_eq!(
            authorize_credential_typing(false),
            CredentialTypingAuthorizationAction::Allow
        );
        assert_eq!(
            authorize_credential_typing(true),
            CredentialTypingAuthorizationAction::RequireApproval(
                CREDENTIAL_TYPING_APPROVAL_REQUIRED
            )
        );
    }

    #[test]
    fn test_permission_request_redacts_credentials() {
        let req = PermissionRequest::new(
            "browser_type",
            r#"{"ref": 1, "password": "super_secret_123"}"#,
            ToolSensitivity::Sensitive,
        );
        assert!(!req.arguments.contains("super_secret_123"));
        assert!(req.arguments.contains("[REDACTED]"));
    }

    #[test]
    fn browser_type_text_is_redacted() {
        let req = PermissionRequest::new(
            "browser_type",
            r#"{"ref": 2, "text": "hello world"}"#,
            ToolSensitivity::Sensitive,
        );
        assert!(!req.arguments.contains("hello world"));
        assert!(req.arguments.contains("[REDACTED]"));
    }

    #[test]
    fn credential_class_keys_are_redacted() {
        let raw = format!(
            r#"{{"password":"{s}","current-password":"{s}","one-time-code":"{s}","otp":"{s}","recovery_code":"{s}","totp":"{s}"}}"#,
            s = SENTINEL
        );
        let req = PermissionRequest::new("browser_type", &raw, ToolSensitivity::Sensitive);
        assert!(
            !req.arguments.contains(SENTINEL),
            "sentinel leaked through credential-key redaction: {}",
            req.arguments
        );
    }

    #[test]
    fn nested_credential_values_are_redacted() {
        let raw = format!(
            r#"{{"outer":{{"list":[{{"password":"{s}"}}]}}}}"#,
            s = SENTINEL
        );
        let req = PermissionRequest::new("browser_type", &raw, ToolSensitivity::Sensitive);
        assert!(!req.arguments.contains(SENTINEL));
    }

    #[test]
    fn malformed_non_json_arguments_fail_closed() {
        let raw = format!("not-json password={SENTINEL}");
        let req = PermissionRequest::new("browser_type", &raw, ToolSensitivity::Sensitive);
        assert!(
            !req.arguments.contains(SENTINEL),
            "sentinel leaked through non-JSON fail-open path: {}",
            req.arguments
        );
    }

    #[test]
    fn sanitize_helper_fails_closed_on_invalid_json() {
        let out = sanitize_tool_arguments(&format!("{{broken json {SENTINEL}"));
        assert!(!out.contains(SENTINEL));
        assert!(out.contains("[REDACTED_INVALID_ARGUMENTS]"));
    }

    #[test]
    fn tool_result_redacts_nested_credential_values() {
        let raw = format!(
            r#"{{"items":[{{"label":"pw","password":"{s}"}}],"totp":"{s}"}}"#,
            s = SENTINEL
        );
        let out = sanitize_tool_result(&raw);
        assert!(
            !out.contains(SENTINEL),
            "sentinel leaked through tool-result redaction: {out}"
        );
        assert!(out.contains("[REDACTED]"));
    }

    #[test]
    fn tool_result_fails_closed_on_malformed() {
        let out = sanitize_tool_result(&format!("Tool call failed: password={SENTINEL}"));
        assert!(
            !out.contains(SENTINEL),
            "sentinel leaked through malformed tool-result: {out}"
        );
        assert!(out.contains("[REDACTED_INVALID_RESULT]"));
    }

    #[test]
    fn ordinary_json_result_passes_through() {
        let out = sanitize_tool_result(r#"{"clicked":true,"ref":3}"#);
        assert!(out.contains("clicked"));
        assert!(out.contains("true"));
        assert!(!out.contains("[REDACTED"));
    }

    #[test]
    fn mail_authorization_table_covers_every_canonical_tool_and_gate_state() {
        let states = [
            (
                MailAuthorizationState::default(),
                MailAuthorizationAction::Deny("mail_feature_disabled"),
            ),
            (
                MailAuthorizationState {
                    feature_enabled: true,
                    helper_starting: true,
                    ..MailAuthorizationState::default()
                },
                MailAuthorizationAction::Deny("mail_helper_starting"),
            ),
            (
                MailAuthorizationState {
                    feature_enabled: true,
                    ..MailAuthorizationState::default()
                },
                MailAuthorizationAction::Deny("mail_helper_unavailable"),
            ),
        ];
        for tool in MAIL_READ_TOOLS.iter().chain(MAIL_WRITE_ACCOUNT_TOOLS) {
            for (state, expected) in states {
                assert_eq!(
                    authorize_mail_tool(tool, state, crate::ApprovalPolicy::Prompt),
                    expected,
                    "{tool}"
                );
            }
        }
        for tool in MAIL_READ_TOOLS {
            assert_eq!(classify_mail_tool(tool), Some(MailToolClass::Read));
            assert_eq!(
                authorize_mail_tool(
                    tool,
                    MailAuthorizationState {
                        feature_enabled: true,
                        helper_ready: true,
                        helper_starting: false,
                        read_allowed: false,
                    },
                    crate::ApprovalPolicy::AllowAll,
                ),
                MailAuthorizationAction::Deny("mail_read_consent_required")
            );
        }
        for tool in MAIL_WRITE_ACCOUNT_TOOLS {
            assert_eq!(classify_mail_tool(tool), Some(MailToolClass::WriteAccount));
            assert_eq!(
                authorize_mail_tool(
                    tool,
                    MailAuthorizationState {
                        feature_enabled: true,
                        helper_ready: true,
                        helper_starting: false,
                        read_allowed: false,
                    },
                    crate::ApprovalPolicy::AllowAll,
                ),
                MailAuthorizationAction::RequireApproval
            );
        }
        assert_eq!(classify_mail_tool("mail_future_tool"), None);
    }

    #[test]
    fn consequence_policy_is_canonical_context_aware_and_fail_closed() {
        let cases = [
            (
                "browser_scroll",
                r#"{"direction":"down"}"#,
                ActionConsequence::Ordinary,
            ),
            ("browser_hover", r#"{"ref":1}"#, ActionConsequence::Ordinary),
            (
                "browser_navigate",
                r#"{"url":"https://other.example"}"#,
                ActionConsequence::NewOrigin,
            ),
            ("browser_click", r#"{"ref":2}"#, ActionConsequence::Unknown),
            (
                "browser_type",
                r#"{"ref":3,"text":"hello"}"#,
                ActionConsequence::Unknown,
            ),
            (
                "browser_click",
                r#"{"ref":4,"validated_action_context":{"validated":true,"consequence":"submit"}}"#,
                ActionConsequence::Submit,
            ),
            (
                "browser_click",
                r#"{"ref":5,"validated_action_context":{"validated":true,"consequence":"destructive"}}"#,
                ActionConsequence::Destructive,
            ),
            (
                "browser_type",
                r#"{"ref":6,"validated_action_context":{"validated":true,"consequence":"credential_fill"}}"#,
                ActionConsequence::CredentialFill,
            ),
            ("browser_click", "not-json", ActionConsequence::Unknown),
        ];
        for (tool, args, expected) in cases {
            assert_eq!(classify_action_consequence(tool, args), expected, "{tool}");
        }
    }

    #[test]
    fn yolo_only_auto_allows_ordinary_actions_and_never_weakens_consequences() {
        let ordinary = r#"{"direction":"down"}"#;
        let consequential =
            r#"{"ref":2,"validated_action_context":{"validated":true,"consequence":"submit"}}"#;
        assert_eq!(
            authorize_action_consequence("browser_scroll", ordinary, ApprovalPolicy::AllowAll),
            ConsequenceApprovalAction::Allow
        );
        assert_eq!(
            authorize_action_consequence("browser_click", consequential, ApprovalPolicy::AllowAll),
            ConsequenceApprovalAction::RequireApproval
        );
        assert_eq!(
            authorize_action_consequence("browser_click", r#"{"ref":2}"#, ApprovalPolicy::AllowAll),
            ConsequenceApprovalAction::RequireApproval
        );
        assert_eq!(
            authorize_action_consequence("browser_scroll", ordinary, ApprovalPolicy::DenyAll),
            ConsequenceApprovalAction::Deny
        );
    }

    #[test]
    fn denied_mail_arguments_are_redacted_with_stable_reason_codes() {
        let sentinel = "mail-secret-sentinel";
        let request = PermissionRequest::new(
            "mail_complete_oauth",
            &format!(r#"{{"code":"{sentinel}","password":"{sentinel}"}}"#),
            ToolSensitivity::Sensitive,
        );
        assert!(!request.arguments.contains(sentinel));
        assert_eq!(
            authorize_mail_tool(
                "mail_get_email",
                MailAuthorizationState {
                    feature_enabled: true,
                    helper_ready: true,
                    helper_starting: false,
                    read_allowed: false,
                },
                crate::ApprovalPolicy::AllowAll,
            ),
            MailAuthorizationAction::Deny("mail_read_consent_required")
        );
    }
}
