//! Wave 3D (SC8, seed method N1) — cross-surface runtime-config parity.
//!
//! Contract under test: the SAME seeded session runtime-config
//! (tier / final_confirm / proactive) must produce IDENTICAL kernel-gate
//! verdicts regardless of which surface's session context the gate runs
//! under. Per N1 the proof seeds an identical config ROW on each surface —
//! a CLI-shaped session (`task-<uuid>`, the `maho agent task` namespace) and
//! a panel-shaped session (bare FFI/conversation id) — NOT one session
//! observed from both (the panel/CLI session-id namespaces differ until the
//! D6 unification).
//!
//! Each surface gets its own in-memory store (surfaces never share a store
//! in production), the identical row is written under each surface's id, and
//! the loaded values drive the exact dispatch composition the CLI tier
//! wiring uses (Wave 1D): `kernel_fs_request` → `authorize_kernel_tool_request`
//! over whitelist roots provisioned by the single shared source
//! `default_fs_whitelist_roots` (plan R-N2), plus the final-confirm gate
//! `authorize_final_confirm`. The tier→policy dimension is asserted through
//! the shared composition the dispatch applies on top of any approval policy
//! (tier Deny overrides everything; RequireApproval forces the prompt path;
//! Allow leaves the policy authoritative) — the maho-agent-side equivalent
//! of the CLI's tier-capped posture mapping.

use maho_agent::permission::{
    authorize_final_confirm, authorize_kernel_tool_request, authorize_tier_fs_request,
    default_fs_whitelist_roots, kernel_fs_request, parse_runtime_tier, FinalConfirmDecision,
    KernelTierDecision, TierFileDecision, FINAL_CONFIRM_REQUIRED, PERMISSION_TIER_DENIED_READ,
    PERMISSION_TIER_DENIED_WRITE,
};
use maho_agent::{ApprovalDecisionReason, ApprovalPolicy, PermissionDecision, ToolSensitivity};
use maho_storage::sqlite::SqliteStorage;

// ─── Surface-shaped seeded sessions (N1) ────────────────────────────────────

/// The two agent surfaces. Only the session-id namespace (and therefore the
/// store row key) differs; the runtime-config schema and the gate below are
/// the shared kernel both surfaces dispatch through.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Surface {
    /// `maho agent task` sessions: `task-<uuid>` ids (main.rs namespace).
    Cli,
    /// Panel agent sessions: bare FFI/conversation ids from the host.
    Panel,
}

impl Surface {
    fn session_id(&self) -> String {
        let uuid = uuid::Uuid::new_v4().to_string();
        match self {
            Self::Cli => format!("task-{uuid}"),
            Self::Panel => uuid,
        }
    }

    fn label(&self) -> &'static str {
        match self {
            Self::Cli => "cli",
            Self::Panel => "panel",
        }
    }
}

/// One seeded runtime-config triple, identical values on both surfaces.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct SeededConfig {
    raw_tier: &'static str,
    final_confirm: bool,
    proactive: bool,
}

/// The scenarios SC8 must hold for: the three canonical tiers, exercising
/// the final_confirm gate on and off. (The guard scenario is byte-identical
/// to the plumbing defaults; the full_access one mirrors the FFI setter
/// shape `set_runtime_config(full_access, false, true)`.)
const SCENARIOS: &[SeededConfig] = &[
    SeededConfig {
        raw_tier: "read_only",
        final_confirm: true,
        proactive: true,
    },
    SeededConfig {
        raw_tier: "guard",
        final_confirm: true,
        proactive: false,
    },
    SeededConfig {
        raw_tier: "full_access",
        final_confirm: false,
        proactive: true,
    },
];

/// Seeds `config` as a persisted runtime-config row for a fresh session on
/// `surface`, then loads it back exactly as that surface's session-open path
/// does. Both surfaces re-validate the tier on load (the panel via the
/// maho-ffi `normalize_tier` mirror, the CLI by feeding the raw string into
/// the gate); the shared interpreter both mirrors are specified to stay
/// byte-compatible with is `parse_runtime_tier`, so the canonical tier is
/// resolved through it here. Returns the loaded config the gate consumes.
fn seed_and_load(surface: Surface, config: SeededConfig) -> (String, SeededConfig) {
    // Each surface owns its store; the row is keyed by the surface's own
    // session-id namespace (N1: namespaces differ, the CONFIG must not).
    let store = SqliteStorage::open_in_memory_with_key(&format!("parity-key-{}", surface.label()))
        .expect("in-memory surface store opens");
    let session_id = surface.session_id();

    store
        .save_agent_runtime_config(
            &session_id,
            config.raw_tier,
            config.final_confirm,
            config.proactive,
        )
        .expect("seed row persists");

    let row = store
        .load_agent_runtime_config(&session_id)
        .expect("load succeeds")
        .unwrap_or_else(|| panic!("seeded row must exist for {} session", surface.label()));

    // Session-open load semantics: the tier is re-normalized fail-closed so a
    // stale or hand-edited row cannot escalate above the canonical tiers.
    let loaded = SeededConfig {
        raw_tier: parse_runtime_tier(&row.permission_tier).as_str(),
        final_confirm: row.final_confirm,
        proactive: row.proactive_mode,
    };
    (session_id, loaded)
}

// ─── The request battery and the dispatch-shaped gate ──────────────────────

/// One kernel tool call presented to the gate identically on both surfaces.
struct KernelRequest {
    label: &'static str,
    tool: &'static str,
    raw_args: &'static str,
    /// Descriptor sensitivity class of the registered kernel tool (mirrors
    /// the backend registration: fs tools are read-only class, shell_exec is
    /// sensitive).
    sensitivity: ToolSensitivity,
}

const BATTERY: &[KernelRequest] = &[
    // fs_write INSIDE the whitelist: relative_path resolves against the
    // artifact root (workspace/.maho/artifacts), which sits under the
    // workspace whitelist root.
    KernelRequest {
        label: "fs_write inside whitelist",
        tool: "fs_write",
        raw_args: r#"{"relative_path":"reports/q3.md","content":"quarterly numbers"}"#,
        sensitivity: ToolSensitivity::ReadOnly,
    },
    // fs_write OUTSIDE the whitelist: an absolute relative_path resolves
    // lexically to a path outside the workspace root.
    KernelRequest {
        label: "fs_write outside whitelist",
        tool: "fs_write",
        raw_args: r#"{"relative_path":"/etc/maho-parity-escape/probe.txt","content":"x"}"#,
        sensitivity: ToolSensitivity::ReadOnly,
    },
    // shell_exec is write-class but not file-scoped: the class rule alone
    // decides under read_only; the tier is inert under guard/full_access.
    KernelRequest {
        label: "shell_exec",
        tool: "shell_exec",
        raw_args: r#"{"command":"ls -1"}"#,
        sensitivity: ToolSensitivity::Sensitive,
    },
    // Non-sensitive read inside the whitelist.
    KernelRequest {
        label: "fs_read inside whitelist",
        tool: "fs_read",
        raw_args: r#"{"path":"notes.md"}"#,
        sensitivity: ToolSensitivity::ReadOnly,
    },
    // Read OUTSIDE the whitelist: pins the read-branch deny code.
    KernelRequest {
        label: "fs_read outside whitelist",
        tool: "fs_read",
        raw_args: r#"{"path":"/etc/passwd"}"#,
        sensitivity: ToolSensitivity::ReadOnly,
    },
];

/// Surface-independent kernel-gate verdict. `Deny` carries the
/// machine-consumed reason code (the `'static str` codes are the public
/// contract strings). Final-confirm verdicts compare through the public
/// `FinalConfirmDecision` directly — it already derives PartialEq.
#[derive(Debug, PartialEq, Eq)]
enum Verdict {
    Allow,
    RequireApproval,
    Deny(&'static str),
}

/// The effective dispatch outcome: the tier gate composed with the session's
/// approval policy — exactly the composition the backend dispatch applies
/// (Deny refuses; RequireApproval forces the callback path; Allow falls
/// through to the policy, whose settled verdicts carry their reason).
#[derive(Clone, Debug, PartialEq, Eq)]
enum Outcome {
    Allowed(&'static str),
    Prompted,
    Denied(&'static str),
}

fn compose_outcome(
    decision: KernelTierDecision,
    policy: ApprovalPolicy,
    request: &KernelRequest,
) -> Outcome {
    match decision {
        KernelTierDecision::Deny(reason) => Outcome::Denied(reason),
        KernelTierDecision::RequireApproval => Outcome::Prompted,
        KernelTierDecision::Allow => {
            match policy.immediate_decision(request.tool, request.sensitivity) {
                Some(meta) => match meta.decision {
                    PermissionDecision::Allow => Outcome::Allowed(meta.reason.as_str()),
                    PermissionDecision::Deny => Outcome::Denied(meta.reason.as_str()),
                },
                // The policy defers this call to the permission callback.
                None => Outcome::Prompted,
            }
        }
    }
}

fn file_verdict(verdict: TierFileDecision) -> Verdict {
    match verdict {
        TierFileDecision::Allow => Verdict::Allow,
        TierFileDecision::RequireApproval => Verdict::RequireApproval,
        TierFileDecision::Deny(reason) => Verdict::Deny(reason),
    }
}

fn kernel_verdict(verdict: KernelTierDecision) -> Verdict {
    match verdict {
        KernelTierDecision::Allow => Verdict::Allow,
        KernelTierDecision::RequireApproval => Verdict::RequireApproval,
        KernelTierDecision::Deny(reason) => Verdict::Deny(reason),
    }
}

/// A full gate pass for one surface's seeded session: per request, the
/// kernel-gate verdict and the final-confirm gate verdict. This mirrors the
/// production dispatch shape: live tier + whitelist roots from the shared
/// provisioning source, fs args resolved lexically via `kernel_fs_request`,
/// artifact root defaulted under the workspace.
#[derive(Debug, PartialEq, Eq)]
struct GatePass {
    kernel: Verdict,
    final_confirm: FinalConfirmDecision,
}

fn run_gate(surface: Surface, config: SeededConfig) -> (String, Vec<GatePass>) {
    let (session_id, loaded) = seed_and_load(surface, config);

    // Each surface runs against its own workspace; absolute roots differ,
    // verdicts must not. Roots come from the single provisioning function
    // (R-N2) and the artifact root from the shared workspace default layout
    // — the same values the CLI wiring and the Wave 2A panel stamping derive.
    let workspace = tempfile::tempdir().expect("surface workspace tempdir");
    let workspace_root = workspace.path().to_path_buf();
    let roots = default_fs_whitelist_roots(&workspace_root);
    let root_refs: Vec<&str> = roots.iter().map(String::as_str).collect();
    let artifact_root = workspace_root.join(".maho").join("artifacts");

    let passes = BATTERY
        .iter()
        .map(|request| {
            let (is_write, fs_path) = kernel_fs_request(
                request.tool,
                request.raw_args,
                &workspace_root,
                Some(&artifact_root),
            );
            let kernel = kernel_verdict(authorize_kernel_tool_request(
                loaded.raw_tier,
                request.tool,
                is_write,
                &fs_path,
                &root_refs,
            ));
            let final_confirm =
                authorize_final_confirm(request.tool, request.raw_args, loaded.final_confirm);
            GatePass {
                kernel,
                final_confirm,
            }
        })
        .collect();
    (session_id, passes)
}

// ─── 1. Seed evidence: identical rows load identically on both surfaces ────

#[test]
fn seeded_rows_load_identically_on_both_surfaces() {
    for config in SCENARIOS {
        for surface in [Surface::Cli, Surface::Panel] {
            let (session_id, loaded) = seed_and_load(surface, *config);
            // The namespace actually diverged (N1 premise)...
            match surface {
                Surface::Cli => assert!(
                    session_id.starts_with("task-"),
                    "CLI session ids are task-<uuid>: {session_id}"
                ),
                Surface::Panel => assert!(
                    !session_id.starts_with("task-"),
                    "panel session ids live in the FFI namespace: {session_id}"
                ),
            }
            // ...but the loaded config must not. proactive round-trips for
            // the prompt-composition consumers (D8); it is not a gate input.
            assert_eq!(
                loaded.raw_tier,
                config.raw_tier,
                "{}: tier round-trips",
                surface.label()
            );
            assert_eq!(
                parse_runtime_tier(loaded.raw_tier),
                parse_runtime_tier(config.raw_tier),
                "{}: canonical tier identical",
                surface.label()
            );
            assert_eq!(
                loaded.final_confirm,
                config.final_confirm,
                "{}: final_confirm round-trips",
                surface.label()
            );
            assert_eq!(
                loaded.proactive,
                config.proactive,
                "{}: proactive round-trips",
                surface.label()
            );
        }
    }
}

// ─── 2. Core SC8: same seeded config → identical gate verdicts ─────────────

#[test]
fn same_seeded_config_yields_identical_kernel_verdicts_on_both_surfaces() {
    for config in SCENARIOS {
        let (cli_id, cli_passes) = run_gate(Surface::Cli, *config);
        let (panel_id, panel_passes) = run_gate(Surface::Panel, *config);
        assert_ne!(
            cli_id, panel_id,
            "the sessions are distinct across surfaces"
        );
        assert_eq!(
            cli_passes, panel_passes,
            "seeded tier={:?} must gate identically for cli session {cli_id} and panel session {panel_id}",
            config.raw_tier
        );
    }
}

#[test]
fn read_only_seed_denies_the_write_class_and_outside_reads_on_both_surfaces() {
    for surface in [Surface::Cli, Surface::Panel] {
        let (_, passes) = run_gate(surface, SCENARIOS[0]);
        let by_label = |label: &str| -> &GatePass {
            let index = BATTERY
                .iter()
                .position(|r| r.label == label)
                .expect("battery label exists");
            &passes[index]
        };

        // Write class (fs_write + shell_exec): denied with the tier message
        // everywhere — inside the whitelist too, an approval token can never
        // rescue a tier denial.
        for label in [
            "fs_write inside whitelist",
            "fs_write outside whitelist",
            "shell_exec",
        ] {
            assert_eq!(
                by_label(label).kernel,
                Verdict::Deny(PERMISSION_TIER_DENIED_WRITE),
                "{:?} {label}: write class denied with the tier message",
                surface
            );
        }
        // Reads: in-whitelist allowed, outside denied with the read code.
        assert_eq!(by_label("fs_read inside whitelist").kernel, Verdict::Allow);
        assert_eq!(
            by_label("fs_read outside whitelist").kernel,
            Verdict::Deny(PERMISSION_TIER_DENIED_READ)
        );
    }
}

#[test]
fn guard_seed_ask_gates_outside_whitelist_and_is_inert_inside_on_both_surfaces() {
    for surface in [Surface::Cli, Surface::Panel] {
        let (_, passes) = run_gate(surface, SCENARIOS[1]);
        let by_label = |label: &str| -> &GatePass {
            let index = BATTERY
                .iter()
                .position(|r| r.label == label)
                .expect("battery label exists");
            &passes[index]
        };

        // Inside the whitelist the tier gate adds nothing (guard is the
        // default tier: the workspace is the session's free zone).
        assert_eq!(by_label("fs_write inside whitelist").kernel, Verdict::Allow);
        assert_eq!(by_label("fs_read inside whitelist").kernel, Verdict::Allow);
        // Not file-scoped: the tier is inert, the approval policy stays
        // authoritative for shell_exec.
        assert_eq!(by_label("shell_exec").kernel, Verdict::Allow);
        // Outside the whitelist every file operation ask-gates.
        assert_eq!(
            by_label("fs_write outside whitelist").kernel,
            Verdict::RequireApproval
        );
        assert_eq!(
            by_label("fs_read outside whitelist").kernel,
            Verdict::RequireApproval
        );
    }
}

#[test]
fn full_access_seed_adds_no_tier_restriction_on_both_surfaces() {
    for surface in [Surface::Cli, Surface::Panel] {
        let (_, passes) = run_gate(surface, SCENARIOS[2]);
        for (request, pass) in BATTERY.iter().zip(passes.iter()) {
            assert_eq!(
                pass.kernel,
                Verdict::Allow,
                "{:?} {}: full_access removes only the tier ask",
                surface,
                request.label
            );
        }
    }
}

// ─── 3. The file-branch mirror agrees with the dispatch gate ───────────────

#[test]
fn authorize_tier_fs_request_matches_the_kernel_gate_on_file_scoped_requests() {
    // The task names both entry points; on every file-scoped request the
    // standalone file branch (`authorize_tier_fs_request`) and the dispatch
    // composition (`authorize_kernel_tool_request`) must agree — on BOTH
    // surfaces, from both surfaces' seeded rows.
    for config in SCENARIOS {
        for surface in [Surface::Cli, Surface::Panel] {
            let (_, loaded) = seed_and_load(surface, *config);
            let workspace = tempfile::tempdir().expect("workspace tempdir");
            let workspace_root = workspace.path().to_path_buf();
            let roots = default_fs_whitelist_roots(&workspace_root);
            let root_refs: Vec<&str> = roots.iter().map(String::as_str).collect();
            let artifact_root = workspace_root.join(".maho").join("artifacts");

            for request in BATTERY {
                let (is_write, fs_path) = kernel_fs_request(
                    request.tool,
                    request.raw_args,
                    &workspace_root,
                    Some(&artifact_root),
                );
                if fs_path.is_empty() {
                    // Not file-scoped (shell_exec): the file branch is inert
                    // by contract; only the kernel class rule can deny.
                    continue;
                }
                assert_eq!(
                    file_verdict(authorize_tier_fs_request(
                        loaded.raw_tier,
                        is_write,
                        &fs_path,
                        &root_refs
                    )),
                    kernel_verdict(authorize_kernel_tool_request(
                        loaded.raw_tier,
                        request.tool,
                        is_write,
                        &fs_path,
                        &root_refs,
                    )),
                    "{:?} seeded {:?}: file branch and kernel gate agree on {}",
                    surface,
                    config.raw_tier,
                    request.label
                );
            }
        }
    }
}

// ─── 4. Tier→policy mapping: the cap composes identically on both sides ────

#[test]
fn tier_cap_composes_identically_with_every_approval_policy_on_both_surfaces() {
    // The CLI resolves its posture per tier (read_only→DenySensitive,
    // guard→prompt/headless-deny, full_access→AllowAll-with-audit); the
    // panel's broker applies the same policy table. The maho-agent-side
    // equivalent — the composition any policy meets at the dispatch gate —
    // must be identical for the same seeded tier on both surfaces, for EVERY
    // policy (a surface could otherwise rescue a call its peer refuses).
    let policies = [
        ApprovalPolicy::AllowAll,
        ApprovalPolicy::Prompt,
        ApprovalPolicy::DenySensitive,
    ];

    for config in SCENARIOS {
        let mut cli_outcomes = Vec::new();
        let mut panel_outcomes = Vec::new();
        for surface in [Surface::Cli, Surface::Panel] {
            let (_, loaded) = seed_and_load(surface, *config);
            let workspace = tempfile::tempdir().expect("workspace tempdir");
            let workspace_root = workspace.path().to_path_buf();
            let roots = default_fs_whitelist_roots(&workspace_root);
            let root_refs: Vec<&str> = roots.iter().map(String::as_str).collect();
            let artifact_root = workspace_root.join(".maho").join("artifacts");

            let mut outcomes = Vec::new();
            for policy in policies {
                for request in BATTERY {
                    let (is_write, fs_path) = kernel_fs_request(
                        request.tool,
                        request.raw_args,
                        &workspace_root,
                        Some(&artifact_root),
                    );
                    let decision = authorize_kernel_tool_request(
                        loaded.raw_tier,
                        request.tool,
                        is_write,
                        &fs_path,
                        &root_refs,
                    );
                    outcomes.push((
                        policy,
                        request.label,
                        compose_outcome(decision, policy, request),
                    ));
                }
            }
            match surface {
                Surface::Cli => cli_outcomes = outcomes,
                Surface::Panel => panel_outcomes = outcomes,
            }
        }
        assert_eq!(
            cli_outcomes, panel_outcomes,
            "seeded tier={:?}: the tier×policy outcome table must be identical on both surfaces",
            config.raw_tier
        );
    }
}

#[test]
fn tier_cap_beats_every_policy_in_both_directions_with_pinned_outcomes() {
    // Spot-pins against a tautological both-sides-equal: the specific cap
    // semantics the tier→policy mappings on both surfaces rely on.
    let workspace = tempfile::tempdir().expect("workspace tempdir");
    let workspace_root = workspace.path().to_path_buf();
    let roots = default_fs_whitelist_roots(&workspace_root);
    let root_refs: Vec<&str> = roots.iter().map(String::as_str).collect();
    let artifact_root = workspace_root.join(".maho").join("artifacts");
    let fs_write_out = &BATTERY[1];
    let fs_read_in = &BATTERY[3];
    let shell_exec = &BATTERY[2];

    let gate = |tier: &str, request: &KernelRequest| {
        let (is_write, fs_path) = kernel_fs_request(
            request.tool,
            request.raw_args,
            &workspace_root,
            Some(&artifact_root),
        );
        authorize_kernel_tool_request(tier, request.tool, is_write, &fs_path, &root_refs)
    };

    // read_only: even --approve-all (AllowAll) cannot authorize the write
    // class — this is what makes the CLI cap the posture to DenySensitive
    // and what the broker enforces on the panel.
    for policy in [
        ApprovalPolicy::AllowAll,
        ApprovalPolicy::Prompt,
        ApprovalPolicy::DenySensitive,
    ] {
        let outcome = compose_outcome(gate("read_only", fs_write_out), policy, fs_write_out);
        assert_eq!(
            outcome,
            Outcome::Denied(PERMISSION_TIER_DENIED_WRITE),
            "read_only write-class deny must survive {policy:?}"
        );
    }
    // ...while the in-whitelist read settles by policy under every posture.
    // AllowAll settles it through its blanket-allow arm; Prompt/DenySensitive
    // through the read-only arm — same Allow, different reason (the same
    // distinction agent_approval_test pins for the postures).
    for (policy, expected_reason) in [
        (
            ApprovalPolicy::AllowAll,
            ApprovalDecisionReason::PolicyAllowAll.as_str(),
        ),
        (
            ApprovalPolicy::Prompt,
            ApprovalDecisionReason::PolicyReadOnlyAutoAllow.as_str(),
        ),
        (
            ApprovalPolicy::DenySensitive,
            ApprovalDecisionReason::PolicyReadOnlyAutoAllow.as_str(),
        ),
    ] {
        let outcome = compose_outcome(gate("read_only", fs_read_in), policy, fs_read_in);
        assert_eq!(
            outcome,
            Outcome::Allowed(expected_reason),
            "read_only in-whitelist read auto-allows under {policy:?}"
        );
    }

    // guard: the ask-gate forces the prompt path EVEN under AllowAll (a
    // policy can never silently auto-allow outside the whitelist), and the
    // policy stays authoritative inside it.
    assert_eq!(
        compose_outcome(
            gate("guard", fs_write_out),
            ApprovalPolicy::AllowAll,
            fs_write_out
        ),
        Outcome::Prompted,
        "guard ask-gate overrides AllowAll outside the whitelist"
    );
    assert_eq!(
        compose_outcome(
            gate("guard", fs_read_in),
            ApprovalPolicy::AllowAll,
            fs_read_in
        ),
        Outcome::Allowed(ApprovalDecisionReason::PolicyAllowAll.as_str()),
    );
    // guard + shell_exec: tier-inert, so the posture alone decides — prompt
    // defers to the callback (TTY), DenySensitive refuses (headless).
    assert_eq!(
        compose_outcome(
            gate("guard", shell_exec),
            ApprovalPolicy::Prompt,
            shell_exec
        ),
        Outcome::Prompted,
    );
    assert_eq!(
        compose_outcome(
            gate("guard", shell_exec),
            ApprovalPolicy::DenySensitive,
            shell_exec
        ),
        Outcome::Denied(ApprovalDecisionReason::PolicyDenySensitive.as_str()),
    );

    // full_access: the tier adds no restriction — the policy is fully
    // authoritative, identical to a tierless session.
    assert_eq!(
        compose_outcome(
            gate("full_access", fs_write_out),
            ApprovalPolicy::AllowAll,
            fs_write_out
        ),
        Outcome::Allowed(ApprovalDecisionReason::PolicyAllowAll.as_str()),
    );
    assert_eq!(
        compose_outcome(
            gate("full_access", shell_exec),
            ApprovalPolicy::DenySensitive,
            shell_exec
        ),
        Outcome::Denied(ApprovalDecisionReason::PolicyDenySensitive.as_str()),
    );
}

// ─── 5. final_confirm dimension: same seeded flag → same gate verdicts ─────

#[test]
fn seeded_final_confirm_gates_identically_on_both_surfaces() {
    // Probes outside the kernel battery: the final-confirm gate's two
    // classification paths (browser-action consequences + mail writes) plus
    // a kernel tool that must stay free. Same seeded flag → same verdicts.
    let probes: &[(&str, &str)] = &[
        (
            "browser_click",
            r#"{"ref":7,"validated_action_context":{"validated":true,"consequence":"submit"}}"#,
        ),
        ("mail_send", r#"{"to":"a@example.com"}"#),
        ("fs_read", r#"{"path":"notes.md"}"#),
        ("web_search", r#"{"query":"maho parity"}"#),
    ];

    for config in SCENARIOS {
        let mut per_surface = Vec::new();
        for surface in [Surface::Cli, Surface::Panel] {
            let (_, loaded) = seed_and_load(surface, *config);
            let verdicts: Vec<(&str, FinalConfirmDecision)> = probes
                .iter()
                .map(|(tool, args)| {
                    (
                        *tool,
                        authorize_final_confirm(tool, args, loaded.final_confirm),
                    )
                })
                .collect();
            per_surface.push((surface.label(), verdicts));
        }
        assert_eq!(
            per_surface[0].1, per_surface[1].1,
            "seeded final_confirm={} must gate identically on both surfaces",
            config.final_confirm
        );

        // Pinned semantics (not merely cross-surface equality): with the
        // flag ON, externally visible/irreversible requests gate with the
        // public reason code; with it OFF the gate is inert. Kernel tools
        // and read-class actions stay free either way.
        let expect_gated = config.final_confirm;
        let click = per_surface[0].1[0].1;
        let mail = per_surface[0].1[1].1;
        let fs_read = per_surface[0].1[2].1;
        let web_search = per_surface[0].1[3].1;
        if expect_gated {
            assert_eq!(
                click,
                FinalConfirmDecision::RequireApproval(FINAL_CONFIRM_REQUIRED)
            );
            assert_eq!(
                mail,
                FinalConfirmDecision::RequireApproval(FINAL_CONFIRM_REQUIRED)
            );
        } else {
            assert_eq!(click, FinalConfirmDecision::Allow);
            assert_eq!(mail, FinalConfirmDecision::Allow);
        }
        assert_eq!(
            fs_read,
            FinalConfirmDecision::Allow,
            "kernel reads stay outside the consequence scope"
        );
        assert_eq!(
            web_search,
            FinalConfirmDecision::Allow,
            "non-action tools stay outside the consequence scope"
        );
    }
}
