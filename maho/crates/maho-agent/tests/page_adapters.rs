use maho_agent::approval::ToolSensitivity;
use maho_agent::fallback_ladder::{
    FallbackDecisionReason, FallbackLadderState, FallbackSafetyState, FallbackSelectionRequest,
    FallbackTier, ScreenshotCuaApproval,
};
use maho_agent::page_adapters::PageAdapterRegistry;

#[test]
fn page_adapters_exact_origin_lookup_and_insertion_order_determinism() {
    let mut registry = PageAdapterRegistry::new();

    // Adapter 1 registered first
    registry.register_simple(
        "google-docs-v1",
        vec!["https://docs.google.com".to_string()],
        vec!["get_selection".to_string(), "insert_text".to_string()],
    );

    // Adapter 2 registered second for same origin with overlapping + new op
    registry.register_simple(
        "google-docs-v2",
        vec!["https://docs.google.com".to_string()],
        vec!["get_selection".to_string(), "delete_range".to_string()],
    );

    // Insertion order determinism: first registered adapter matching (origin, op) wins
    let adapter1 = registry
        .lookup("https://docs.google.com", "get_selection")
        .expect("should match adapter 1");
    assert_eq!(adapter1.id(), "google-docs-v1");

    // Operation supported only by adapter 2 resolves to adapter 2
    let adapter2 = registry
        .lookup("https://docs.google.com", "delete_range")
        .expect("should match adapter 2");
    assert_eq!(adapter2.id(), "google-docs-v2");

    // Lookup on full document URL resolves to exact origin
    let adapter_from_url = registry
        .lookup("https://docs.google.com/document/d/123/edit", "insert_text")
        .expect("should match by origin extracted from url");
    assert_eq!(adapter_from_url.id(), "google-docs-v1");
}

#[test]
fn page_adapters_evil_origin_and_different_origin_probes_not_matched() {
    let mut registry = PageAdapterRegistry::new();
    registry.register_simple(
        "google-docs-official",
        vec!["https://docs.google.com".to_string()],
        vec!["get_selection".to_string(), "read_document".to_string()],
    );

    // Evil origin subdomain attack: https://docs.google.com.evil.io must NOT match
    assert!(
        registry
            .lookup("https://docs.google.com.evil.io", "get_selection")
            .is_none(),
        "evil.io suffix probe must fail closed"
    );
    assert!(
        registry
            .lookup(
                "https://docs.google.com.evil.io/document/d/1",
                "get_selection"
            )
            .is_none(),
        "evil.io path probe must fail closed"
    );

    // Evil target parameter probe
    assert!(
        registry
            .lookup(
                "https://evil.io/?target=https://docs.google.com",
                "get_selection"
            )
            .is_none(),
        "query param evil origin must fail closed"
    );

    // Different subdomain
    assert!(
        registry
            .lookup("https://sheets.google.com", "get_selection")
            .is_none(),
        "different subdomain must not match"
    );

    // Different scheme (http vs https)
    assert!(
        registry
            .lookup("http://docs.google.com", "get_selection")
            .is_none(),
        "http scheme must not match https origin"
    );

    // Different port
    assert!(
        registry
            .lookup("https://docs.google.com:8443", "get_selection")
            .is_none(),
        "different port must not match standard 443 origin"
    );

    // Exact matching origin matches
    assert!(
        registry
            .lookup("https://docs.google.com", "get_selection")
            .is_some(),
        "exact origin must match"
    );
}

#[test]
fn page_adapters_unsupported_op_falls_through_to_next_tier() {
    let mut registry = PageAdapterRegistry::new();
    registry.register_simple(
        "docs-adapter",
        vec!["https://docs.google.com".to_string()],
        vec!["get_selection".to_string()],
    );

    // Unsupported op returns None from registry
    assert!(
        registry
            .lookup("https://docs.google.com", "unsupported_op")
            .is_none(),
        "unsupported op must return None"
    );

    // Fallback ladder state integration: unsupported op falls through to DomTier
    let mut ladder = FallbackLadderState::new("session-page-adapter-ladder");
    let decision = ladder
        .decide_with_adapter_registry(
            "browser_click",
            "https://docs.google.com",
            "unsupported_op",
            &registry,
        )
        .expect("unsupported op falls through to Dom tier");

    assert_eq!(decision.selected_tier, FallbackTier::DomRefLocator);
    assert_eq!(decision.safety_state, FallbackSafetyState::Downgraded);

    // Supported op selects ExactOriginPageAdapter tier
    let adapter_decision = ladder
        .decide_with_adapter_registry(
            "browser_type",
            "https://docs.google.com",
            "get_selection",
            &registry,
        )
        .expect("supported op selects exact origin page adapter tier");

    assert_eq!(
        adapter_decision.selected_tier,
        FallbackTier::ExactOriginPageAdapter
    );
}

#[test]
fn page_adapters_ladder_gains_adapter_tier_before_dom_tier_order() {
    // Verify tier ordering enum definition: AdapterTier precedes DomTier
    assert!(
        FallbackTier::TypedDomainApi < FallbackTier::ExactOriginPageAdapter,
        "TypedDomainApi must precede ExactOriginPageAdapter"
    );
    assert!(
        FallbackTier::ExactOriginPageAdapter < FallbackTier::DomRefLocator,
        "ExactOriginPageAdapter (AdapterTier) must precede DomRefLocator (DomTier)"
    );
    assert!(
        FallbackTier::DomRefLocator < FallbackTier::AccessibilitySnapshot,
        "DomRefLocator must precede AccessibilitySnapshot"
    );
    assert!(
        FallbackTier::AccessibilitySnapshot < FallbackTier::ScreenshotCua,
        "AccessibilitySnapshot must precede ScreenshotCua"
    );

    // Also verify aliases / as_str representation
    assert_eq!(
        FallbackTier::ExactOriginPageAdapter.as_str(),
        "exact_origin_page_adapter"
    );
    assert_eq!(
        FallbackTier::AdapterTier,
        FallbackTier::ExactOriginPageAdapter
    );
    assert_eq!(FallbackTier::DomTier, FallbackTier::DomRefLocator);

    // Test Ladder State transitions:
    let mut ladder = FallbackLadderState::new("session-adapter-order");

    // Requesting ExactOriginPageAdapter directly when TypedDomainApi is unavailable
    let req = FallbackSelectionRequest {
        tool_name: "browser_type",
        requested_tier: FallbackTier::ExactOriginPageAdapter,
        sensitivity: ToolSensitivity::Sensitive,
        screenshot_cua_approval: ScreenshotCuaApproval::NotRequested,
    };
    let decision = ladder.decide(req).expect("adapter tier selection succeeds");
    assert_eq!(decision.attempted_tier, FallbackTier::TypedDomainApi);
    assert_eq!(decision.selected_tier, FallbackTier::ExactOriginPageAdapter);

    // Once Adapter tier succeeds for this action class, downgrading to DomRefLocator or lower is blocked
    let downgrade_req = FallbackSelectionRequest {
        tool_name: "browser_type",
        requested_tier: FallbackTier::DomRefLocator,
        sensitivity: ToolSensitivity::Sensitive,
        screenshot_cua_approval: ScreenshotCuaApproval::NotRequested,
    };
    let err = ladder
        .decide(downgrade_req)
        .expect_err("downgrading from successful page adapter to DOM is unsafe");
    assert_eq!(
        err.decision().reason,
        FallbackDecisionReason::PageAdapterAlreadySucceeded
    );
    assert_eq!(err.decision().safety_state, FallbackSafetyState::Blocked);
}
