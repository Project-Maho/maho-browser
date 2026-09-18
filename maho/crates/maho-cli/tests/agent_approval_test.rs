//! Wave 1B (gaps G1/G9-part/G3-CLI): CLI approval surface tests.
//!
//! Pins the three postures that replace the removed hard-coded
//! `set_approval_policy("allow")` in `maho agent task`:
//!
//! - TTY stdin          → `Prompt` + interactive y/n permission callback that
//!   renders the sanitized request (secret-free) before reading the answer
//! - no TTY             → `DenySensitive` (fail closed, sensitive denied)
//! - `--approve-all`    → `AllowAll` (automation escape hatch)
//! - non-sensitive tools stay policy-auto-allowed under every posture
//!
//! The policy→decision gate machinery lives in maho-agent's
//! `PermissionGatedTool`; here we pin the contract the CLI posture selection
//! relies on (`immediate_decision` outcomes) plus the actual new behavior:
//! posture resolution, prompt rendering, answer mapping, and backend wiring.

use std::sync::{Arc, Mutex};

use maho_agent::permission::RuntimeTier;
use maho_agent::{
    ApprovalDecisionReason, ApprovalPolicy, PermissionCallback, PermissionDecision,
    PermissionRequest, ToolSensitivity,
};

use maho_cli::approval::{
    apply_cli_approval, decide_from_answer, render_permission_prompt, resolve_cli_approval_policy,
    tty_prompt_callback,
};

const SENSITIVE_TOOL: &str = "browser_fill_credential";
const READ_ONLY_TOOL: &str = "browser_snapshot";

fn sensitive_request() -> PermissionRequest {
    PermissionRequest::new(SENSITIVE_TOOL, r#"{"ref": 3}"#, ToolSensitivity::Sensitive)
}

/// The prompt callback decides synchronously before returning its future, so
/// awaiting it is deterministic — no runtime timing involved.
async fn decision_of(
    callback: PermissionCallback,
    request: PermissionRequest,
) -> PermissionDecision {
    callback(request).await
}

/// Builds a callback over in-memory I/O; returns (callback, captured prompts).
fn scripted_callback(answer: &str) -> (PermissionCallback, Arc<Mutex<Vec<String>>>) {
    let writes: Arc<Mutex<Vec<String>>> = Arc::new(Mutex::new(Vec::new()));
    let sink_writes = Arc::clone(&writes);
    let owned = answer.to_string();
    let callback = tty_prompt_callback(
        Arc::new(move || Ok(owned.clone())),
        Arc::new(move |line: &str| sink_writes.lock().unwrap().push(line.to_string())),
    );
    (callback, writes)
}

// ─── Scenario 1: sensitive + TTY → interactive prompt ──────────────────────

#[test]
fn sensitive_with_tty_resolves_prompt_posture() {
    assert_eq!(
        resolve_cli_approval_policy(false, true, "guard"),
        ApprovalPolicy::Prompt,
        "TTY stdin must select the interactive prompt posture under guard"
    );
    // The prompt posture must defer sensitive calls to the callback (None =
    // no immediate policy verdict), which is exactly where the TTY prompt fires.
    assert!(ApprovalPolicy::Prompt
        .immediate_decision(SENSITIVE_TOOL, ToolSensitivity::Sensitive)
        .is_none());
}

#[tokio::test]
async fn tty_prompt_callback_allows_on_yes_and_renders_sanitized_request() {
    let (callback, writes) = scripted_callback("y\n");
    // Secret-carrying payload: PermissionRequest::new sanitizes at the trust
    // boundary; the rendered prompt must carry the redaction, never the secret.
    let request = PermissionRequest::new(
        "browser_type",
        r#"{"ref": 1, "text": "hunter2-secret"}"#,
        ToolSensitivity::Sensitive,
    );
    assert_eq!(
        decision_of(callback, request).await,
        PermissionDecision::Allow
    );

    let rendered = writes.lock().unwrap().join("\n");
    assert!(
        rendered.contains("browser_type"),
        "prompt must name the tool\n{rendered}"
    );
    assert!(
        rendered.contains("sensitive"),
        "prompt must carry the sensitivity class\n{rendered}"
    );
    assert!(
        !rendered.contains("hunter2-secret"),
        "prompt must never echo the raw secret\n{rendered}"
    );
    assert!(
        rendered.contains("[REDACTED]"),
        "prompt must show the sanitized payload\n{rendered}"
    );
}

#[tokio::test]
async fn tty_prompt_callback_denies_on_no_and_eof() {
    let (deny_callback, _) = scripted_callback("n\n");
    assert_eq!(
        decision_of(deny_callback, sensitive_request()).await,
        PermissionDecision::Deny
    );

    // Read failure / EOF: the source errors, the callback fails closed.
    let writes: Arc<Mutex<Vec<String>>> = Arc::new(Mutex::new(Vec::new()));
    let sink_writes = Arc::clone(&writes);
    let eof_callback = tty_prompt_callback(
        Arc::new(|| Err(std::io::Error::other("stdin closed"))),
        Arc::new(move |line: &str| sink_writes.lock().unwrap().push(line.to_string())),
    );
    assert_eq!(
        decision_of(eof_callback, sensitive_request()).await,
        PermissionDecision::Deny
    );
    assert!(
        !writes.lock().unwrap().is_empty(),
        "the prompt itself must still render before the read fails"
    );
}

// ─── Scenario 2: sensitive + no TTY → DenySensitive ────────────────────────

#[test]
fn sensitive_without_tty_fails_closed_to_deny_sensitive() {
    assert_eq!(
        resolve_cli_approval_policy(false, false, "guard"),
        ApprovalPolicy::DenySensitive,
        "headless stdin must fail closed to DenySensitive"
    );
    let verdict = ApprovalPolicy::DenySensitive
        .immediate_decision(SENSITIVE_TOOL, ToolSensitivity::Sensitive)
        .expect("sensitive call under DenySensitive must be settled by policy");
    assert_eq!(verdict.decision, PermissionDecision::Deny);
    assert_eq!(verdict.reason, ApprovalDecisionReason::PolicyDenySensitive);
    // A policy-settled deny never consults (or waits on) a callback.
}

#[test]
fn answer_mapping_fails_closed_on_everything_but_yes() {
    assert_eq!(decide_from_answer("y\n"), PermissionDecision::Allow);
    assert_eq!(decide_from_answer("YES"), PermissionDecision::Allow);
    assert_eq!(decide_from_answer("  y  "), PermissionDecision::Allow);
    assert_eq!(decide_from_answer("n"), PermissionDecision::Deny);
    assert_eq!(decide_from_answer("N"), PermissionDecision::Deny);
    assert_eq!(decide_from_answer(""), PermissionDecision::Deny);
    assert_eq!(decide_from_answer("yes please"), PermissionDecision::Deny);
    assert_eq!(decide_from_answer("0"), PermissionDecision::Deny);
}

// ─── Scenario 3: --approve-all → AllowAll ──────────────────────────────────

#[test]
fn approve_all_maps_to_allow_all_under_any_tty_state() {
    assert_eq!(
        resolve_cli_approval_policy(true, true, "guard"),
        ApprovalPolicy::AllowAll
    );
    assert_eq!(
        resolve_cli_approval_policy(true, false, "guard"),
        ApprovalPolicy::AllowAll
    );
    let verdict = ApprovalPolicy::AllowAll
        .immediate_decision(SENSITIVE_TOOL, ToolSensitivity::Sensitive)
        .expect("sensitive call under AllowAll must be settled by policy");
    assert_eq!(verdict.decision, PermissionDecision::Allow);
    assert_eq!(verdict.reason, ApprovalDecisionReason::PolicyAllowAll);
}

// ─── Scenario 4: non-sensitive tools unaffected ────────────────────────────

#[test]
fn non_sensitive_tools_stay_auto_allowed_under_every_posture() {
    let postures = [
        resolve_cli_approval_policy(false, true, "guard"), // TTY → Prompt
        resolve_cli_approval_policy(false, false, "guard"), // headless → DenySensitive
        resolve_cli_approval_policy(true, false, "guard"), // --approve-all → AllowAll
    ];
    for policy in postures {
        let verdict = policy
            .immediate_decision(READ_ONLY_TOOL, ToolSensitivity::ReadOnly)
            .unwrap_or_else(|| panic!("{policy:?} must settle read-only calls by policy"));
        assert_eq!(
            verdict.decision,
            PermissionDecision::Allow,
            "{policy:?} must not gate non-sensitive calls"
        );
        // AllowAll settles read-only calls through its blanket-allow arm;
        // Prompt and DenySensitive fall through to the read-only arm. Either
        // way the verdict is Allow — never a prompt, never a deny.
        let expected_reason = if policy == ApprovalPolicy::AllowAll {
            ApprovalDecisionReason::PolicyAllowAll
        } else {
            ApprovalDecisionReason::PolicyReadOnlyAutoAllow
        };
        assert_eq!(verdict.reason, expected_reason);
    }
}

// ─── Prompt rendering ──────────────────────────────────────────────────────

#[test]
fn rendered_prompt_caps_oversized_payloads() {
    // A JSON string literal passes sanitization unchanged (redaction is
    // key-based), so this exercises the display cap, not the sanitizer.
    let blob = format!("\"{}\"", "x".repeat(4_000));
    let request = PermissionRequest::new(SENSITIVE_TOOL, &blob, ToolSensitivity::Sensitive);
    let rendered = render_permission_prompt(&request);
    assert!(
        rendered.len() < blob.len(),
        "oversized payload must be capped"
    );
    assert!(rendered.contains("[truncated"));
}

// ─── Backend wiring ────────────────────────────────────────────────────────

fn dry_backend(dir: &tempfile::TempDir) -> Arc<dyn maho_agent::AgentRuntime> {
    let db_path = dir.path().join("maho.db");
    let storage =
        maho_cli::workspace::open_storage(&db_path).expect("temp profile storage must open");
    maho_agent::omo::factory::create_agent_runtime(
        Arc::new(maho_agent::MutexAgentStorage(Arc::new(Mutex::new(storage)))),
        None,
        dir.path().to_path_buf(),
        true,
    )
}

/// The exact calls `build_agent_task_session` makes must succeed against a
/// real session backend for all three postures — the policy string must
/// round-trip through the legacy-pref parser and the TTY posture must accept
/// the prompt callback installation.
#[test]
fn apply_cli_approval_wires_every_posture_into_a_real_backend() {
    let dir = tempfile::tempdir().expect("temp dir");
    let backend = dry_backend(&dir);

    assert_eq!(
        apply_cli_approval(backend.as_ref(), true, false, "guard"),
        ApprovalPolicy::AllowAll
    );
    assert_eq!(
        apply_cli_approval(backend.as_ref(), false, false, "guard"),
        ApprovalPolicy::DenySensitive
    );
    assert_eq!(
        apply_cli_approval(backend.as_ref(), false, true, "guard"),
        ApprovalPolicy::Prompt
    );
}

// ─── Wave 1D (D7): tier→policy capping ──────────────────────────────────────

#[test]
fn read_only_tier_caps_every_posture_and_parses_to_the_canonical_tier() {
    // --approve-all and a TTY cannot authorize what the tier forbids.
    assert_eq!(
        resolve_cli_approval_policy(true, true, "read_only"),
        ApprovalPolicy::DenySensitive
    );
    assert_eq!(
        resolve_cli_approval_policy(false, false, "read_only"),
        ApprovalPolicy::DenySensitive
    );
    // The raw --tier value round-trips through the shared parser, so the
    // mapping cannot drift from the dispatch gate's tier interpretation.
    assert_eq!(parse_tier("read_only"), RuntimeTier::ReadOnly);
}

#[test]
fn full_access_tier_resolves_allow_all_even_headless() {
    // AllowAll-with-audit (D7): full access adds no restriction of its own,
    // so an automation run needs no --approve-all under this tier.
    assert_eq!(
        resolve_cli_approval_policy(false, false, "full_access"),
        ApprovalPolicy::AllowAll
    );
    assert_eq!(parse_tier("full_access"), RuntimeTier::FullAccess);
}

fn parse_tier(raw: &str) -> RuntimeTier {
    maho_agent::permission::parse_runtime_tier(raw)
}
