use maho_agent::{
    browser_tool_fallback_request, ApprovalAuditRecord, ApprovalPolicy, FallbackDecisionReason,
    FallbackLadderError, FallbackLadderState, FallbackSafetyState, FallbackSelectionRequest,
    FallbackTier, PermissionDecision, ScreenshotCuaApproval, ToolSensitivity,
};

fn request(
    tool_name: &str,
    requested_tier: FallbackTier,
    screenshot_cua_approval: ScreenshotCuaApproval,
) -> FallbackSelectionRequest<'_> {
    FallbackSelectionRequest {
        tool_name,
        requested_tier,
        sensitivity: ToolSensitivity::Sensitive,
        screenshot_cua_approval,
    }
}

#[test]
fn fallback_ladder_typed_success_does_not_downgrade() {
    let mut ladder = FallbackLadderState::new("session-fallback-typed");

    let typed = ladder
        .decide(request(
            "browser_navigate",
            FallbackTier::TypedDomainApi,
            ScreenshotCuaApproval::NotRequested,
        ))
        .expect("typed browser API decision is allowed");
    assert_eq!(typed.selected_tier, FallbackTier::TypedDomainApi);
    assert_eq!(typed.safety_state, FallbackSafetyState::Safe);

    let downgrade = ladder
        .decide(request(
            "browser_navigate",
            FallbackTier::DomRefLocator,
            ScreenshotCuaApproval::NotRequested,
        ))
        .expect_err("typed success must block later lower-tier downgrade");
    assert_eq!(
        downgrade.decision().reason,
        FallbackDecisionReason::TypedDomainAlreadySucceeded
    );
    assert_eq!(downgrade.decision().safety_state, FallbackSafetyState::Blocked);
}

#[test]
fn fallback_ladder_dom_ref_follows_typed_miss() {
    let mut ladder = FallbackLadderState::new("session-fallback-dom");

    let decision = ladder
        .decide(request(
            "page_query_selector",
            FallbackTier::DomRefLocator,
            ScreenshotCuaApproval::NotRequested,
        ))
        .expect("DOM/ref tier is selected after missing typed/domain API");

    assert_eq!(decision.attempted_tier, FallbackTier::TypedDomainApi);
    assert_eq!(decision.selected_tier, FallbackTier::DomRefLocator);
    assert_eq!(decision.reason, FallbackDecisionReason::TypedDomainUnavailable);
    assert_eq!(decision.safety_state, FallbackSafetyState::Downgraded);
}

#[test]
fn fallback_ladder_accessibility_follows_missing_ref() {
    let mut ladder = FallbackLadderState::new("session-fallback-accessibility");
    let _ = ladder
        .decide(request(
            "page_query_selector",
            FallbackTier::DomRefLocator,
            ScreenshotCuaApproval::NotRequested,
        ))
        .expect("DOM/ref attempt is recorded");

    let decision = ladder
        .decide(request(
            "browser_accessibility_snapshot",
            FallbackTier::AccessibilitySnapshot,
            ScreenshotCuaApproval::NotRequested,
        ))
        .expect("accessibility snapshot follows missing DOM/ref");

    assert_eq!(decision.attempted_tier, FallbackTier::DomRefLocator);
    assert_eq!(decision.selected_tier, FallbackTier::AccessibilitySnapshot);
    assert_eq!(decision.reason, FallbackDecisionReason::DomRefMissing);
    assert_eq!(decision.safety_state, FallbackSafetyState::Downgraded);
}

#[test]
fn fallback_ladder_screenshot_cua_requires_explicit_approval() {
    let mut ladder = FallbackLadderState::new("session-fallback-cua");

    let error = ladder
        .decide(request(
            "browser_click",
            FallbackTier::ScreenshotCua,
            ScreenshotCuaApproval::NotRequested,
        ))
        .expect_err("screenshot/CUA cannot run without explicit approval");

    assert!(matches!(
        error,
        FallbackLadderError::ScreenshotCuaApprovalRequired(_)
    ));
    assert_eq!(
        error.decision().safety_state,
        FallbackSafetyState::EscalationApprovalRequired
    );
    assert!(error.decision().approval_required);
}

#[test]
fn fallback_ladder_denial_prevents_bridge_dispatch() {
    let mut ladder = FallbackLadderState::new("session-fallback-denied");
    let mut dispatched = false;

    let decision = ladder.decide(request(
        "browser_click",
        FallbackTier::ScreenshotCua,
        ScreenshotCuaApproval::Denied,
    ));
    if decision.is_ok() {
        dispatched = true;
    }

    let error = decision.expect_err("denied screenshot/CUA escalation is blocked");
    assert!(matches!(error, FallbackLadderError::ScreenshotCuaDenied(_)));
    assert_eq!(
        error.decision().safety_state,
        FallbackSafetyState::EscalationDenied
    );
    assert!(!dispatched, "denial must prevent bridge/dispatch");
}

#[test]
fn fallback_ladder_audit_records_reason_without_sentinels() {
    let sentinel = "S3NTINEL-maho-vault-9F4C";
    let mut ladder = FallbackLadderState::new("session-fallback-audit");
    let request = browser_tool_fallback_request(
        "browser_click",
        &format!(
            r#"{{"fallback_tier":"screenshot_cua","text":"{sentinel}"}}"#
        ),
        ToolSensitivity::Sensitive,
    );

    let error = ladder
        .decide(request)
        .expect_err("unapproved screenshot/CUA emits escalation decision");
    let record = ApprovalAuditRecord::fallback_decision(
        "session-fallback-audit",
        "browser_click",
        ToolSensitivity::Sensitive,
        ApprovalPolicy::Prompt,
        error.decision(),
    );
    let json = serde_json::to_string(&record).expect("fallback audit serializes");

    assert!(json.contains("screenshot_cua_approval_required"));
    assert!(json.contains("escalation_approval_required"));
    assert!(!json.contains(sentinel));
    assert_eq!(record.decision, Option::<PermissionDecision>::None);
}
