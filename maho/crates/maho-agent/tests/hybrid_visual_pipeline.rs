// Copyright 2026 The Maho Authors. All rights reserved.

//! Hybrid visual pipeline integration tests.
//!
//! Verifies:
//! - Image-dependent deterministic fixture provider sees a redacted PNG and
//!   returns a randomized fixture position without any locator oracle.
//! - Strict typed native action parser over multimodal prompt_messages completions.
//! - Security invariants: denied consent, provider error, stale image, and
//!   invalid actions NEVER dispatch.

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

use maho_agent::fallback_ladder::ScreenshotCuaApproval;
use maho_agent::visual_cua::{
    parse_native_action, ClickMotionProfile, CompletionRequest, DeterministicFixtureProvider,
    NativeAction, NativeActionDispatcher, NativeActionRequest, PointF, RectF, VisualCuaError,
    VisualCuaPipeline, VisualCuaProvider, VisualFrame,
};
use serde_json::json;

/// Recording dispatcher that tracks calls and can simulate errors.
#[derive(Default)]
struct RecordingDispatcher {
    calls: Arc<Mutex<Vec<NativeActionRequest>>>,
    dispatch_count: Arc<AtomicUsize>,
    fail_with: Arc<Mutex<Option<String>>>,
}

impl RecordingDispatcher {
    fn new() -> Self {
        Self::default()
    }

    fn calls(&self) -> Vec<NativeActionRequest> {
        self.calls.lock().unwrap().clone()
    }

    fn count(&self) -> usize {
        self.dispatch_count.load(Ordering::SeqCst)
    }

    fn set_failure(&self, err: &str) {
        *self.fail_with.lock().unwrap() = Some(err.to_string());
    }
}

#[async_trait::async_trait]
impl NativeActionDispatcher for RecordingDispatcher {
    async fn dispatch_native_action(
        &self,
        request: &NativeActionRequest,
    ) -> Result<serde_json::Value, VisualCuaError> {
        if let Some(err) = self.fail_with.lock().unwrap().as_ref() {
            return Err(VisualCuaError::DispatchError(err.clone()));
        }
        self.calls.lock().unwrap().push(request.clone());
        self.dispatch_count.fetch_add(1, Ordering::SeqCst);
        Ok(json!({
            "dispatched": true,
            "verified": true,
            "frame_token": request.frame_token,
            "lease_epoch": request.lease_epoch
        }))
    }
}

/// Helper to construct a synthetic 1x1 PNG for testing.
fn create_test_png(variant: u8) -> Vec<u8> {
    let mut data = vec![
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, // PNG magic
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, // IHDR chunk
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15,
        0xC4, 0x89,
    ];
    // Vary byte payload
    data.push(variant);
    data.extend([
        0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, // IDAT chunk
        0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00, 0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, // IEND chunk
        0xAE, 0x42, 0x60, 0x82,
    ]);
    data
}

fn create_valid_frame(token: &str, variant: u8) -> VisualFrame {
    VisualFrame {
        frame_token: token.to_string(),
        tab_id: 42,
        lease_epoch: 10,
        document_epoch: 1,
        viewport_width: 1280.0,
        viewport_height: 800.0,
        redacted_png: create_test_png(variant),
        is_stale: false,
    }
}

/// 1. Image-dependent deterministic fixture provider loop:
/// - Sees a redacted PNG
/// - Returns a randomized fixture position
/// - Uses NO locator oracle (no DOM selectors or @refs)
/// - Deterministic: identical PNG bytes yield identical randomized positions across iterations
/// - Distinct PNG bytes yield different positions
/// - Dispatches valid native actions to the dispatcher
#[tokio::test]
async fn test_image_dependent_deterministic_provider_loop() {
    let provider = Arc::new(DeterministicFixtureProvider::new());
    let dispatcher = Arc::new(RecordingDispatcher::new());
    let pipeline = VisualCuaPipeline::new(
        provider,
        dispatcher.clone() as Arc<dyn NativeActionDispatcher>,
    );

    let frame_alpha = create_valid_frame("token-alpha", 1);
    let frame_beta = create_valid_frame("token-beta", 2);

    // Iteration 1: frame_alpha
    let result1 = pipeline
        .execute_step(&frame_alpha, true, "click the primary button")
        .await
        .expect("step 1 succeeds");
    assert_eq!(result1["dispatched"], true);

    // Iteration 2: frame_alpha again -> must yield identical coordinates
    let result2 = pipeline
        .execute_step(&frame_alpha, true, "click the primary button")
        .await
        .expect("step 2 succeeds");
    assert_eq!(result2["dispatched"], true);

    // Iteration 3: frame_beta with different image bytes -> must yield different coordinates
    let result3 = pipeline
        .execute_step(&frame_beta, true, "click the primary button")
        .await
        .expect("step 3 succeeds");
    assert_eq!(result3["dispatched"], true);

    let calls = dispatcher.calls();
    assert_eq!(calls.len(), 3);

    // Verify first and second calls (same image bytes) are identical
    let (c1_pt, c1_rect) = match &calls[0].action {
        NativeAction::Click {
            click_point_css,
            target_rect_css,
            ..
        } => (*click_point_css, *target_rect_css),
        _ => panic!("expected Click action"),
    };
    let (c2_pt, c2_rect) = match &calls[1].action {
        NativeAction::Click {
            click_point_css,
            target_rect_css,
            ..
        } => (*click_point_css, *target_rect_css),
        _ => panic!("expected Click action"),
    };
    assert_eq!(
        c1_pt, c2_pt,
        "identical image bytes must yield deterministic position"
    );
    assert_eq!(
        c1_rect, c2_rect,
        "identical image bytes must yield deterministic target rect"
    );

    // Invariant: target rect contains click point
    assert!(
        c1_rect.contains(&c1_pt),
        "target_rect must strictly contain click_point"
    );

    // Verify third call (different image bytes) is different
    let (c3_pt, c3_rect) = match &calls[2].action {
        NativeAction::Click {
            click_point_css,
            target_rect_css,
            ..
        } => (*click_point_css, *target_rect_css),
        _ => panic!("expected Click action"),
    };
    assert_ne!(
        c1_pt, c3_pt,
        "different image bytes must yield different position"
    );
    assert!(
        c3_rect.contains(&c3_pt),
        "target_rect must contain click_point for frame beta"
    );

    // Verify frame metadata forwarding
    assert_eq!(calls[0].frame_token, "token-alpha");
    assert_eq!(calls[0].tab_id, 42);
    assert_eq!(calls[0].lease_epoch, 10);
    assert_eq!(calls[0].document_epoch, 1);
    assert_eq!(calls[2].frame_token, "token-beta");
}

/// 2. Denied consent NEVER dispatches.
#[tokio::test]
async fn test_denied_consent_never_dispatches() {
    let provider = Arc::new(DeterministicFixtureProvider::new());
    let dispatcher = Arc::new(RecordingDispatcher::new());
    let pipeline = VisualCuaPipeline::new(
        provider,
        dispatcher.clone() as Arc<dyn NativeActionDispatcher>,
    );

    let frame = create_valid_frame("token-1", 1);

    // Direct boolean consent = false
    let err = pipeline
        .execute_step(&frame, false, "click button")
        .await
        .expect_err("denied consent must fail");
    assert_eq!(err, VisualCuaError::ConsentDenied);
    assert_eq!(dispatcher.count(), 0, "denied consent must never dispatch");

    // Typed ScreenshotCuaApproval::Denied
    let err2 = pipeline
        .execute_step_with_approval(&frame, ScreenshotCuaApproval::Denied, "click button")
        .await
        .expect_err("Denied approval must fail");
    assert_eq!(err2, VisualCuaError::ConsentDenied);
    assert_eq!(dispatcher.count(), 0, "Denied approval must never dispatch");

    // Typed ScreenshotCuaApproval::NotRequested
    let err3 = pipeline
        .execute_step_with_approval(&frame, ScreenshotCuaApproval::NotRequested, "click button")
        .await
        .expect_err("NotRequested approval must fail");
    assert_eq!(err3, VisualCuaError::ConsentDenied);
    assert_eq!(
        dispatcher.count(),
        0,
        "NotRequested approval must never dispatch"
    );
}

/// 3. Provider error NEVER dispatches.
#[tokio::test]
async fn test_provider_error_never_dispatches() {
    struct FailingProvider;
    #[async_trait::async_trait]
    impl VisualCuaProvider for FailingProvider {
        async fn complete(&self, _request: &CompletionRequest) -> Result<String, VisualCuaError> {
            Err(VisualCuaError::ProviderError("model API 500 error".into()))
        }
    }

    let provider = Arc::new(FailingProvider);
    let dispatcher = Arc::new(RecordingDispatcher::new());
    let pipeline = VisualCuaPipeline::new(
        provider,
        dispatcher.clone() as Arc<dyn NativeActionDispatcher>,
    );

    let frame = create_valid_frame("token-1", 1);
    let err = pipeline
        .execute_step(&frame, true, "click button")
        .await
        .expect_err("provider error must propagate");
    assert!(matches!(err, VisualCuaError::ProviderError(_)));
    assert_eq!(dispatcher.count(), 0, "provider error must never dispatch");
}

/// 4. Stale image / frame NEVER dispatches.
#[tokio::test]
async fn test_stale_image_never_dispatches() {
    let provider = Arc::new(DeterministicFixtureProvider::new());
    let dispatcher = Arc::new(RecordingDispatcher::new());
    let pipeline = VisualCuaPipeline::new(
        provider,
        dispatcher.clone() as Arc<dyn NativeActionDispatcher>,
    );

    // Case a: is_stale = true
    let mut frame_stale = create_valid_frame("token-1", 1);
    frame_stale.is_stale = true;
    let err1 = pipeline
        .execute_step(&frame_stale, true, "click")
        .await
        .expect_err("stale frame must fail");
    assert_eq!(err1, VisualCuaError::StaleFrame);
    assert_eq!(dispatcher.count(), 0);

    // Case b: empty frame_token
    let mut frame_no_token = create_valid_frame("", 1);
    frame_no_token.frame_token = "   ".to_string();
    let err2 = pipeline
        .execute_step(&frame_no_token, true, "click")
        .await
        .expect_err("empty token must fail");
    assert_eq!(err2, VisualCuaError::StaleFrame);
    assert_eq!(dispatcher.count(), 0);

    // Case c: zero lease_epoch
    let mut frame_no_lease = create_valid_frame("token-1", 1);
    frame_no_lease.lease_epoch = 0;
    let err3 = pipeline
        .execute_step(&frame_no_lease, true, "click")
        .await
        .expect_err("zero lease epoch must fail");
    assert_eq!(err3, VisualCuaError::StaleFrame);
    assert_eq!(dispatcher.count(), 0);

    // Case d: zero document_epoch
    let mut frame_no_doc = create_valid_frame("token-1", 1);
    frame_no_doc.document_epoch = 0;
    let err4 = pipeline
        .execute_step(&frame_no_doc, true, "click")
        .await
        .expect_err("zero document epoch must fail");
    assert_eq!(err4, VisualCuaError::StaleFrame);
    assert_eq!(dispatcher.count(), 0);

    // Case e: empty png bytes
    let mut frame_no_png = create_valid_frame("token-1", 1);
    frame_no_png.redacted_png = Vec::new();
    let err5 = pipeline
        .execute_step(&frame_no_png, true, "click")
        .await
        .expect_err("empty png must fail");
    assert_eq!(err5, VisualCuaError::StaleFrame);
    assert_eq!(dispatcher.count(), 0);
}

/// 5. Strict typed native action parser over multimodal completions.
#[tokio::test]
async fn test_strict_typed_native_action_parser() {
    // Valid click JSON
    let click_json = r#"{
        "kind": "click",
        "click_point_css": [150.0, 200.0],
        "target_rect_css": [100.0, 150.0, 100.0, 100.0],
        "motion_profile": "smooth"
    }"#;
    let action = parse_native_action(click_json).expect("parse click");
    assert_eq!(
        action,
        NativeAction::Click {
            click_point_css: PointF::new(150.0, 200.0),
            target_rect_css: RectF::new(100.0, 150.0, 100.0, 100.0),
            motion_profile: ClickMotionProfile::Smooth,
        }
    );

    // Valid type in markdown code fence
    let type_fenced = "```json\n{\"action\": {\"kind\": \"type\", \"text\": \"hello world\"}}\n```";
    let action_type = parse_native_action(type_fenced).expect("parse type");
    assert_eq!(
        action_type,
        NativeAction::Type {
            text: "hello world".into()
        }
    );

    // Valid key action
    let key_json = r#"{"kind": "key", "key": "Enter", "modifiers": ["Control"]}"#;
    let action_key = parse_native_action(key_json).expect("parse key");
    assert_eq!(
        action_key,
        NativeAction::Key {
            key: "Enter".into(),
            modifiers: vec!["Control".into()],
        }
    );

    // Invalid: out-of-bounds click point (target rect does not contain click point)
    let bad_click = NativeActionRequest {
        action: NativeAction::Click {
            click_point_css: PointF::new(500.0, 500.0),
            target_rect_css: RectF::new(0.0, 0.0, 50.0, 50.0),
            motion_profile: ClickMotionProfile::Direct,
        },
        frame_token: "frame-1".into(),
        tab_id: 1,
        lease_epoch: 1,
        document_epoch: 1,
        user_modifiers_active: false,
    };
    let err = bad_click
        .validate()
        .expect_err("out of bounds click must fail validation");
    assert!(matches!(err, VisualCuaError::InvalidAction(_)));

    // Invalid: zero target rect dimensions
    let zero_rect = NativeActionRequest {
        action: NativeAction::Click {
            click_point_css: PointF::new(10.0, 10.0),
            target_rect_css: RectF::new(10.0, 10.0, 0.0, 10.0),
            motion_profile: ClickMotionProfile::Direct,
        },
        frame_token: "frame-1".into(),
        tab_id: 1,
        lease_epoch: 1,
        document_epoch: 1,
        user_modifiers_active: false,
    };
    assert!(zero_rect.validate().is_err());

    // Invalid: empty type text
    let empty_type = NativeActionRequest {
        action: NativeAction::Type { text: "".into() },
        frame_token: "frame-1".into(),
        tab_id: 1,
        lease_epoch: 1,
        document_epoch: 1,
        user_modifiers_active: false,
    };
    assert!(empty_type.validate().is_err());

    // Invalid: empty key
    let empty_key = NativeActionRequest {
        action: NativeAction::Key {
            key: "".into(),
            modifiers: vec![],
        },
        frame_token: "frame-1".into(),
        tab_id: 1,
        lease_epoch: 1,
        document_epoch: 1,
        user_modifiers_active: false,
    };
    assert!(empty_key.validate().is_err());

    // Invalid: generic arbitrary tool op
    let bad_op = r#"{"kind": "arbitrary_tool", "command": "rm -rf /"}"#;
    assert!(parse_native_action(bad_op).is_err());
}

/// 6. Dispatch failure propagates.
#[tokio::test]
async fn test_dispatcher_failure_propagates() {
    let provider = Arc::new(DeterministicFixtureProvider::new());
    let dispatcher = Arc::new(RecordingDispatcher::new());
    dispatcher.set_failure("OS SendInput failed");
    let pipeline = VisualCuaPipeline::new(
        provider,
        dispatcher.clone() as Arc<dyn NativeActionDispatcher>,
    );

    let frame = create_valid_frame("token-1", 1);
    let err = pipeline
        .execute_step(&frame, true, "click button")
        .await
        .expect_err("dispatch failure must propagate");
    assert!(matches!(err, VisualCuaError::DispatchError(_)));
}
