//! Wave 1B (gap G1/G9-part/G3-CLI): the CLI approval surface for
//! `maho agent task`.
//!
//! Replaces the pre-existing hard-coded `set_approval_policy("allow")` +
//! `permission_callback: None` construction with three explicit postures:
//!
//! - `--approve-all`            → [`ApprovalPolicy::AllowAll`] — the documented
//!   automation escape hatch (no prompts, everything allowed).
//! - stdin is a TTY             → [`ApprovalPolicy::Prompt`] plus a
//!   permission callback that renders the sanitized request on **stderr** and
//!   reads a y/n answer from stdin.
//! - no TTY (piped/redirected)  → [`ApprovalPolicy::DenySensitive`] — fail
//!   closed: sensitive tool calls are denied by policy, non-sensitive calls
//!   auto-allow exactly as before.
//!
//! The decision machinery (policy evaluation, callback dispatch, and the
//! 30s callback decision timeout that auto-denies an unanswered prompt)
//! stays in maho-agent's `PermissionGatedTool`; this module only selects the
//! posture and provides the interactive callback. The `PermissionCallback`
//! signature answers [`PermissionDecision`] (Allow/Deny only), so richer
//! aside-panel interactions (option buttons, free text) have no channel here —
//! y/n is the complete mapping.

use std::io::IsTerminal;
use std::sync::Arc;

use maho_agent::approval::ApprovalPolicy;
use maho_agent::permission::parse_runtime_tier;
use maho_agent::{
    AgentRuntime, PermissionCallback, PermissionDecision, PermissionRequest,
};

/// Reads one answer line. Injectable so tests can drive the prompt without a
/// real terminal.
pub type PromptSource = dyn Fn() -> std::io::Result<String> + Send + Sync;

/// Receives one rendered prompt line. Injectable for the same reason; the
/// production sink writes to stderr.
pub type PromptSink = dyn Fn(&str) + Send + Sync;

/// True when stdin is an interactive terminal. Piped or redirected stdin is
/// headless (R-W1B).
pub fn stdin_is_tty() -> bool {
    std::io::stdin().is_terminal()
}

/// Wave 1D (D7): maps the session tier, the parsed `--approve-all` flag, and
/// the stdin TTY probe onto the effective approval policy. The tier CAPS the
/// posture — it can only tighten, never loosen:
///
/// - `read_only`    → [`ApprovalPolicy::DenySensitive`] under every posture
///   (`--approve-all` cannot authorize consequential actions the tier
///   forbids; the fs-write/shell-exec class is additionally denied by the
///   dispatch tier gate in maho-agent with the tier reason code).
/// - `full_access`  → [`ApprovalPolicy::AllowAll`] with audit — the tier adds
///   no restriction of its own, so a full-access task session may run
///   headless without `--approve-all`; every decision is still audited by
///   the permission gate.
/// - `guard`        → the Wave 1B posture (`--approve-all` → AllowAll, TTY →
///   Prompt, headless → DenySensitive); the whitelist ask-gate is enforced
///   by the dispatch tier gate regardless of the posture.
pub fn resolve_cli_approval_policy(
    approve_all: bool,
    stdin_is_tty: bool,
    runtime_tier: &str,
) -> ApprovalPolicy {
    match parse_runtime_tier(runtime_tier) {
        maho_agent::permission::RuntimeTier::ReadOnly => ApprovalPolicy::DenySensitive,
        maho_agent::permission::RuntimeTier::FullAccess => ApprovalPolicy::AllowAll,
        maho_agent::permission::RuntimeTier::Guard => {
            if approve_all {
                ApprovalPolicy::AllowAll
            } else if stdin_is_tty {
                ApprovalPolicy::Prompt
            } else {
                ApprovalPolicy::DenySensitive
            }
        }
    }
}

/// Maps a raw terminal answer onto the permission decision. Fail closed:
/// only `y`/`yes` (any casing, surrounding whitespace tolerated) allows;
/// `n`, empty input, EOF, and unrecognized answers all deny.
pub fn decide_from_answer(answer: &str) -> PermissionDecision {
    match answer.trim().to_ascii_lowercase().as_str() {
        "y" | "yes" => PermissionDecision::Allow,
        _ => PermissionDecision::Deny,
    }
}

/// Display cap for the sanitized arguments blob so a huge payload cannot
/// flood the terminal.
const MAX_RENDERED_ARGUMENTS: usize = 800;

/// Renders the interactive prompt for one permission request. The request
/// arguments are already sanitized by `PermissionRequest::new` (recursive
/// key redaction, fail-closed marker for non-JSON); this only caps the
/// display length. Written to stderr by the callback, never stdout, so
/// `--json` output stays clean.
pub fn render_permission_prompt(request: &PermissionRequest) -> String {
    let arguments = truncate_for_display(&request.arguments, MAX_RENDERED_ARGUMENTS);
    format!(
        "Approval required: {} ({})\n  Arguments: {}\n  Allow this tool call? [y/N] ",
        request.tool_name,
        request.sensitivity.as_str(),
        arguments
    )
}

fn truncate_for_display(raw: &str, cap: usize) -> String {
    if raw.len() <= cap {
        return raw.to_string();
    }
    let mut cut = cap;
    while !raw.is_char_boundary(cut) {
        cut -= 1;
    }
    format!("[truncated, {} chars total] {}", raw.len(), &raw[..cut])
}

/// Builds the interactive TTY permission callback: render the sanitized
/// request, read one answer line, map it fail-closed. Any read error (EOF
/// included) denies.
pub fn tty_prompt_callback(
    read_line: Arc<PromptSource>,
    write_line: Arc<PromptSink>,
) -> PermissionCallback {
    Box::new(move |request: PermissionRequest| {
        write_line(&render_permission_prompt(&request));
        let answer = read_line().unwrap_or_default();
        let decision = decide_from_answer(&answer);
        Box::pin(async move { decision })
    })
}

fn terminal_read_line() -> std::io::Result<String> {
    let mut answer = String::new();
    std::io::stdin().read_line(&mut answer)?;
    Ok(answer)
}

fn terminal_write_line(line: &str) {
    use std::io::Write as _;
    let mut stderr = std::io::stderr().lock();
    let _ = stderr.write_all(line.as_bytes());
    let _ = stderr.write_all(b"\n");
    let _ = stderr.flush();
}

/// Wires one CLI approval posture into an agent task session: sets the
/// tier-capped policy and, for the interactive posture, installs the TTY
/// prompt callback. Returns the effective policy for callers that report it.
pub fn apply_cli_approval(
    backend: &dyn AgentRuntime,
    approve_all: bool,
    stdin_is_tty: bool,
    runtime_tier: &str,
) -> ApprovalPolicy {
    let policy = resolve_cli_approval_policy(approve_all, stdin_is_tty, runtime_tier);
    backend.set_approval_policy(policy.as_str().to_string());
    if policy == ApprovalPolicy::Prompt {
        // Infallible for the in-process backend; a failure would silently
        // downgrade the interactive posture to missing-callback denies.
        AgentRuntime::set_permission_callback(
            backend,
            tty_prompt_callback(Arc::new(terminal_read_line), Arc::new(terminal_write_line)),
        )
        .expect("TTY prompt permission callback must install");
    }
    policy
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn resolve_maps_the_three_postures() {
        assert_eq!(
            resolve_cli_approval_policy(true, true, "guard"),
            ApprovalPolicy::AllowAll
        );
        assert_eq!(
            resolve_cli_approval_policy(true, false, "guard"),
            ApprovalPolicy::AllowAll
        );
        assert_eq!(
            resolve_cli_approval_policy(false, true, "guard"),
            ApprovalPolicy::Prompt
        );
        assert_eq!(
            resolve_cli_approval_policy(false, false, "guard"),
            ApprovalPolicy::DenySensitive
        );
    }

    #[test]
    fn tier_caps_the_posture_per_d7_mapping() {
        // read_only: DenySensitive under every posture — --approve-all and a
        // TTY cannot authorize what the tier forbids.
        assert_eq!(
            resolve_cli_approval_policy(true, true, "read_only"),
            ApprovalPolicy::DenySensitive
        );
        assert_eq!(
            resolve_cli_approval_policy(false, true, "read_only"),
            ApprovalPolicy::DenySensitive
        );
        assert_eq!(
            resolve_cli_approval_policy(false, false, "read_only"),
            ApprovalPolicy::DenySensitive
        );
        // full_access: AllowAll with audit, even headless without --approve-all.
        assert_eq!(
            resolve_cli_approval_policy(false, false, "full_access"),
            ApprovalPolicy::AllowAll
        );
        assert_eq!(
            resolve_cli_approval_policy(true, true, "full_access"),
            ApprovalPolicy::AllowAll
        );
        // Unknown tier strings fail closed to guard, never to a weaker posture.
        assert_eq!(
            resolve_cli_approval_policy(false, false, "bogus"),
            ApprovalPolicy::DenySensitive
        );
    }

    #[test]
    fn decide_from_answer_fails_closed() {
        assert_eq!(decide_from_answer("y\n"), PermissionDecision::Allow);
        assert_eq!(decide_from_answer("  Yes "), PermissionDecision::Allow);
        assert_eq!(decide_from_answer("n"), PermissionDecision::Deny);
        assert_eq!(decide_from_answer(""), PermissionDecision::Deny);
        assert_eq!(decide_from_answer("maybe"), PermissionDecision::Deny);
    }
}
