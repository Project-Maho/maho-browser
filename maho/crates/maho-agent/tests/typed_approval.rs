use maho_agent::{
    ApprovalAuditEventKind, ApprovalAuditRecord, ApprovalDecisionReason, ApprovalDecisionSource,
    ApprovalPolicy, PermissionDecision, ToolSensitivity, ToolSensitivityDowngrade,
    ToolSensitivityResolution,
};
use std::io;
use std::sync::{Arc, Mutex};

const SENTINEL: &str = "S3NTINEL-maho-vault-9F4C";

#[derive(Clone)]
struct BufferWriter {
    bytes: Arc<Mutex<Vec<u8>>>,
}

struct BufferHandle {
    bytes: Arc<Mutex<Vec<u8>>>,
}

impl<'a> tracing_subscriber::fmt::MakeWriter<'a> for BufferWriter {
    type Writer = BufferHandle;

    fn make_writer(&'a self) -> Self::Writer {
        BufferHandle {
            bytes: Arc::clone(&self.bytes),
        }
    }
}

impl io::Write for BufferHandle {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.bytes
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .extend_from_slice(buf);
        Ok(buf.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

#[test]
fn typed_approval_legacy_policy_parser_fails_closed() {
    assert_eq!(
        ApprovalPolicy::from_legacy_pref("allow"),
        ApprovalPolicy::AllowAll
    );
    assert_eq!(
        ApprovalPolicy::from_legacy_pref("prompt"),
        ApprovalPolicy::Prompt
    );
    assert_eq!(
        ApprovalPolicy::from_legacy_pref("deny-sensitive"),
        ApprovalPolicy::DenySensitive
    );
    assert_eq!(
        ApprovalPolicy::from_legacy_pref("allow-mcp"),
        ApprovalPolicy::AllowMcp
    );
    assert_eq!(
        ApprovalPolicy::from_legacy_pref("surprise-new-policy"),
        ApprovalPolicy::DenyAll
    );
}

#[test]
fn typed_approval_policy_canonical_strings_roundtrip() {
    for policy in [
        ApprovalPolicy::Prompt,
        ApprovalPolicy::AllowAll,
        ApprovalPolicy::AllowMcp,
        ApprovalPolicy::DenySensitive,
        ApprovalPolicy::DenyAll,
    ] {
        assert_eq!(ApprovalPolicy::from_legacy_pref(policy.as_str()), policy);
    }
}

#[test]
fn typed_approval_sensitive_policy_decision_metadata_is_structured() {
    let metadata = ApprovalPolicy::DenySensitive
        .immediate_decision("browser_click", ToolSensitivity::Sensitive)
        .expect("deny-sensitive returns an immediate sensitive-tool decision");

    assert_eq!(metadata.decision, PermissionDecision::Deny);
    assert_eq!(metadata.policy, ApprovalPolicy::DenySensitive);
    assert_eq!(metadata.source, ApprovalDecisionSource::Policy);
    assert_eq!(metadata.reason, ApprovalDecisionReason::PolicyDenySensitive);
}

#[test]
fn typed_approval_audit_record_serializes_only_sanitized_payload() {
    let record = ApprovalAuditRecord::permission_request(
        "session-typed",
        "browser_type",
        ToolSensitivity::Sensitive,
        ApprovalPolicy::Prompt,
        &maho_agent::sanitize_tool_arguments(&format!(
            r#"{{"text":"hello","password":"{SENTINEL}"}}"#
        )),
    );

    let json = serde_json::to_string(&record).expect("audit record serializes");
    assert!(json.contains("permission_request"));
    assert!(json.contains("browser_type"));
    assert!(json.contains("sensitive"));
    assert!(json.contains("session-typed"));
    assert!(!json.contains(SENTINEL));
}

#[test]
fn typed_approval_downgrade_audit_record_carries_attempted_and_selected_sensitivity() {
    let record = ApprovalAuditRecord::sensitivity_downgrade(
        "session-downgrade",
        "browser_tab_list",
        ToolSensitivity::Sensitive,
        ToolSensitivity::ReadOnly,
        ApprovalPolicy::Prompt,
    );

    assert_eq!(record.kind, ApprovalAuditEventKind::PermissionDowngrade);
    assert_eq!(record.session_id, "session-downgrade");
    assert_eq!(record.tool_name, "browser_tab_list");
    assert_eq!(record.sensitivity, ToolSensitivity::ReadOnly);
    assert_eq!(
        record.attempted_sensitivity,
        Some(ToolSensitivity::Sensitive)
    );
    assert_eq!(record.selected_sensitivity, Some(ToolSensitivity::ReadOnly));
    assert_eq!(record.decision, None);

    let json = serde_json::to_string(&record).expect("downgrade audit record serializes");
    assert!(json.contains("permission_downgrade"));
    assert!(json.contains("attempted_sensitivity"));
    assert!(json.contains("selected_sensitivity"));
    assert!(!json.contains(SENTINEL));
}

#[test]
fn typed_approval_downgrade_audit_emission_uses_maho_agent_audit_target() {
    let bytes = Arc::new(Mutex::new(Vec::new()));
    let subscriber = tracing_subscriber::fmt()
        .json()
        .with_ansi(false)
        .with_writer(BufferWriter {
            bytes: Arc::clone(&bytes),
        })
        .with_max_level(tracing::Level::INFO)
        .finish();
    let record = ApprovalAuditRecord::sensitivity_downgrade(
        "session-downgrade-target",
        "browser_tab_list",
        ToolSensitivity::Sensitive,
        ToolSensitivity::ReadOnly,
        ApprovalPolicy::Prompt,
    );

    tracing::subscriber::with_default(subscriber, || {
        maho_agent::approval::emit_audit_record(&record);
    });

    let output = String::from_utf8(
        bytes
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .clone(),
    )
    .expect("captured audit event is UTF-8");
    assert!(
        output.contains(r#""target":"maho_agent::audit""#),
        "downgrade audit must be emitted on the canonical audit target; got {output}"
    );
    assert!(output.contains("permission_downgrade"));
}

#[test]
fn typed_approval_cancel_audit_record_serializes_session_marker_and_decision() {
    let record = ApprovalAuditRecord::permission_cancel("session-cancel", ApprovalPolicy::Prompt);

    assert_eq!(record.kind, ApprovalAuditEventKind::PermissionCancel);
    assert_eq!(record.session_id, "session-cancel");
    assert_eq!(record.tool_name, "session");
    assert_eq!(record.sensitivity, ToolSensitivity::Sensitive);
    assert_eq!(record.decision, Some(PermissionDecision::Deny));

    let json = serde_json::to_string(&record).expect("cancel audit record serializes");
    assert!(json.contains("permission_cancel"));
    assert!(json.contains("session-cancel"));
    assert!(json.contains("\"tool_name\":\"session\""));
    assert!(json.contains("deny"));
    assert!(json.contains("session_cancel"));
    assert!(!json.contains(SENTINEL));
}

#[test]
fn permission_canonical_read_only_contract_records_sensitive_fallback_downgrade() {
    let resolution =
        ToolSensitivityResolution::for_tool_name("browser_accessibility_snapshot", true);

    assert_eq!(resolution.effective, ToolSensitivity::ReadOnly);
    assert_eq!(
        resolution.downgrade,
        Some(ToolSensitivityDowngrade {
            from: ToolSensitivity::Sensitive,
            to: ToolSensitivity::ReadOnly,
        })
    );
}
