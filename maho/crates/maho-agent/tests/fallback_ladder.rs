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
    assert_eq!(
        downgrade.decision().safety_state,
        FallbackSafetyState::Blocked
    );
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
    assert_eq!(
        decision.reason,
        FallbackDecisionReason::TypedDomainUnavailable
    );
    assert_eq!(decision.safety_state, FallbackSafetyState::Downgraded);
}

#[test]
fn fallback_ladder_accessibility_snapshot_selected_after_page_ref_read() {
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
        .expect("accessibility snapshot is selected on its own merits");

    assert_eq!(decision.attempted_tier, FallbackTier::DomRefLocator);
    assert_eq!(decision.selected_tier, FallbackTier::AccessibilitySnapshot);
    // A page-read DOM/ref lookup belongs to a different action class than the
    // accessibility snapshot, so it does not turn the snapshot into a
    // ref-missing downgrade: the snapshot supplies @refs for click/type/select
    // and is selected on its own merits.
    assert_eq!(
        decision.reason,
        FallbackDecisionReason::AccessibilitySelected
    );
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
        &format!(r#"{{"fallback_tier":"screenshot_cua","text":"{sentinel}"}}"#),
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

#[test]
fn fallback_ladder_page_adapter_tier_precedes_dom_tier() {
    let mut ladder = FallbackLadderState::new("session-fallback-adapter");

    // ExactOriginPageAdapter tier is selected when requested
    let decision = ladder
        .decide(request(
            "browser_type",
            FallbackTier::ExactOriginPageAdapter,
            ScreenshotCuaApproval::NotRequested,
        ))
        .expect("adapter tier decision succeeds");

    assert_eq!(decision.attempted_tier, FallbackTier::TypedDomainApi);
    assert_eq!(decision.selected_tier, FallbackTier::ExactOriginPageAdapter);
    assert_eq!(
        decision.reason,
        FallbackDecisionReason::TypedDomainUnavailable
    );
    assert_eq!(decision.safety_state, FallbackSafetyState::Downgraded);

    // Later downgrade to DOM tier is blocked
    let err = ladder
        .decide(request(
            "browser_type",
            FallbackTier::DomRefLocator,
            ScreenshotCuaApproval::NotRequested,
        ))
        .expect_err("unsafe downgrade from page adapter to dom is blocked");

    assert_eq!(
        err.decision().reason,
        FallbackDecisionReason::PageAdapterAlreadySucceeded
    );
    assert_eq!(err.decision().safety_state, FallbackSafetyState::Blocked);
}

#[test]
fn fallback_ladder_v2_locator_failure_routes_down_ladder_to_v1_then_screenshot() {
    let mut ladder = FallbackLadderState::new("session-v2-fallback");

    // Tier 1: V2 Locator attempt
    let v2_decision = ladder
        .decide(request(
            "input.locator_click",
            FallbackTier::DomRefLocator,
            ScreenshotCuaApproval::NotRequested,
        ))
        .expect("V2 locator selection succeeds");

    assert_eq!(v2_decision.attempted_tier, FallbackTier::TypedDomainApi);
    assert_eq!(v2_decision.selected_tier, FallbackTier::DomRefLocator);
    assert_eq!(
        v2_decision.reason,
        FallbackDecisionReason::TypedDomainUnavailable
    );
    assert_eq!(v2_decision.safety_state, FallbackSafetyState::Downgraded);

    // Tier 2: V2 Locator fails -> V1 accessibility snapshot/ref flow fallback
    let v1_decision = ladder
        .decide(request(
            "browser_click",
            FallbackTier::AccessibilitySnapshot,
            ScreenshotCuaApproval::NotRequested,
        ))
        .expect("V1 ref flow fallback succeeds after V2 locator failure");

    assert_eq!(v1_decision.attempted_tier, FallbackTier::DomRefLocator);
    assert_eq!(
        v1_decision.selected_tier,
        FallbackTier::AccessibilitySnapshot
    );
    assert_eq!(v1_decision.reason, FallbackDecisionReason::DomRefMissing);
    assert_eq!(v1_decision.safety_state, FallbackSafetyState::Downgraded);

    // Tier 3: V1 ref fails -> Screenshot / CUA escalation
    // Without approval: blocked with escalation approval required
    let cua_err = ladder
        .decide(request(
            "browser_click",
            FallbackTier::ScreenshotCua,
            ScreenshotCuaApproval::NotRequested,
        ))
        .expect_err("screenshot/CUA escalation requires approval");

    assert!(matches!(
        cua_err,
        FallbackLadderError::ScreenshotCuaApprovalRequired(_)
    ));
    assert_eq!(
        cua_err.decision().safety_state,
        FallbackSafetyState::EscalationApprovalRequired
    );
    assert_eq!(
        cua_err.decision().selected_tier,
        FallbackTier::ScreenshotCua
    );

    // With approval: succeeds
    let cua_approved = ladder
        .decide(request(
            "browser_click",
            FallbackTier::ScreenshotCua,
            ScreenshotCuaApproval::Approved,
        ))
        .expect("approved screenshot/CUA escalation succeeds");

    assert_eq!(
        cua_approved.attempted_tier,
        FallbackTier::AccessibilitySnapshot
    );
    assert_eq!(cua_approved.selected_tier, FallbackTier::ScreenshotCua);
    assert_eq!(
        cua_approved.reason,
        FallbackDecisionReason::ScreenshotCuaApproved
    );
    assert_eq!(
        cua_approved.safety_state,
        FallbackSafetyState::EscalationApproved
    );
}

#[test]
fn fallback_ladder_browser_tool_request_infers_v2_tiers() {
    let v2_locator = browser_tool_fallback_request(
        "input.locator_click",
        r#"{"locator":{"css":"button"}}"#,
        ToolSensitivity::Sensitive,
    );
    assert_eq!(v2_locator.requested_tier, FallbackTier::DomRefLocator);

    let v2_type = browser_tool_fallback_request(
        "input.locator_type",
        r#"{"locator":{"role":"textbox","name":"Search"}}"#,
        ToolSensitivity::Sensitive,
    );
    assert_eq!(v2_type.requested_tier, FallbackTier::DomRefLocator);

    let v2_snapshot = browser_tool_fallback_request(
        "page.accessibility_snapshot_v2",
        r#"{"mode":"interactive"}"#,
        ToolSensitivity::ReadOnly,
    );
    assert_eq!(
        v2_snapshot.requested_tier,
        FallbackTier::AccessibilitySnapshot
    );

    let v2_cua_escalation = browser_tool_fallback_request(
        "input.locator_click",
        r#"{"locator":{"css":"button"},"screenshot_cua":true}"#,
        ToolSensitivity::Sensitive,
    );
    assert_eq!(
        v2_cua_escalation.requested_tier,
        FallbackTier::ScreenshotCua
    );
}

#[test]
fn fallback_ladder_model_supplied_screenshot_cua_approved_is_never_trusted_as_consent() {
    let mut ladder = FallbackLadderState::new("session-untrusted-model-consent");

    // The model injects screenshot_cua_approved: true in tool arguments.
    // The ladder must NOT treat this as consent/approval!
    let untrusted_request = browser_tool_fallback_request(
        "browser_click",
        r#"{"screenshot_cua_approved":true,"screenshot_cua":true}"#,
        ToolSensitivity::Sensitive,
    );
    assert_eq!(
        untrusted_request.screenshot_cua_approval,
        ScreenshotCuaApproval::NotRequested,
        "model-supplied screenshot_cua_approved MUST NOT be trusted as consent"
    );

    let error = ladder
        .decide(untrusted_request)
        .expect_err("model cannot approve its own escalation");
    assert!(matches!(
        error,
        FallbackLadderError::ScreenshotCuaApprovalRequired(_)
    ));
    assert_eq!(
        error.decision().safety_state,
        FallbackSafetyState::EscalationApprovalRequired
    );
}

#[test]
fn fallback_ladder_active_sentinel_request_triggers_screenshot_cua() {
    let sentinel_req = browser_tool_fallback_request(
        "browser_click",
        r#"{"sentinel":true}"#,
        ToolSensitivity::Sensitive,
    );
    assert_eq!(
        sentinel_req.requested_tier,
        FallbackTier::ScreenshotCua,
        "active sentinel request triggers ScreenshotCua"
    );

    let fallback_tier_req = browser_tool_fallback_request(
        "input.locator_click",
        r#"{"fallback_tier":"screenshot_cua"}"#,
        ToolSensitivity::Sensitive,
    );
    assert_eq!(
        fallback_tier_req.requested_tier,
        FallbackTier::ScreenshotCua,
        "fallback_tier screenshot_cua triggers ScreenshotCua"
    );
}

#[test]
fn fallback_ladder_native_aliases_and_subactions_classified() {
    let req_key = browser_tool_fallback_request(
        "input.key_press",
        r#"{"key":"Enter"}"#,
        ToolSensitivity::Sensitive,
    );
    assert_eq!(req_key.requested_tier, FallbackTier::TypedDomainApi);

    let req_locator_click = browser_tool_fallback_request(
        "input_locator_click",
        r##"{"locator":{"css":"#btn"}}"##,
        ToolSensitivity::Sensitive,
    );
    assert_eq!(
        req_locator_click.requested_tier,
        FallbackTier::DomRefLocator
    );

    let req_locator_type = browser_tool_fallback_request(
        "input_locator_type",
        r##"{"locator":{"css":"#input"},"text":"hello"}"##,
        ToolSensitivity::Sensitive,
    );
    assert_eq!(req_locator_type.requested_tier, FallbackTier::DomRefLocator);

    let req_act_and_observe = browser_tool_fallback_request(
        "browser_act_and_observe",
        r#"{"action":{"kind":"click"}}"#,
        ToolSensitivity::Sensitive,
    );
    assert_eq!(
        req_act_and_observe.requested_tier,
        FallbackTier::DomRefLocator
    );

    let req_act_and_observe_alias = browser_tool_fallback_request(
        "browser.act_and_observe",
        r#"{"action":{"kind":"click"}}"#,
        ToolSensitivity::Sensitive,
    );
    assert_eq!(
        req_act_and_observe_alias.requested_tier,
        FallbackTier::DomRefLocator
    );

    let req_select = browser_tool_fallback_request(
        "input.select",
        r#"{"ref":1,"value":"option1"}"#,
        ToolSensitivity::Sensitive,
    );
    assert_eq!(
        req_select.requested_tier,
        FallbackTier::AccessibilitySnapshot
    );

    let req_hover =
        browser_tool_fallback_request("input.hover", r#"{"ref":1}"#, ToolSensitivity::Sensitive);
    assert_eq!(
        req_hover.requested_tier,
        FallbackTier::AccessibilitySnapshot
    );
}
