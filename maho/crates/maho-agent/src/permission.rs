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

pub const CREDENTIAL_TYPING_APPROVAL_REQUIRED: &str = "credential_typing_approval_required";
pub const CREDENTIAL_TYPING_DENIED: &str = "credential_typing_denied";

// Fixed denial reason codes emitted by the runtime permission-tier and
// final-confirmation gates (rows 3/5 wire the enforcement; this row only
// defines the codes). These are machine-consumed public contract strings, so
// the exact snake_case spelling is load-bearing and must stay in lockstep with
// any C++ mirror when that gate is added.
pub const FINAL_CONFIRM_REQUIRED: &str = "final_confirm_required";
pub const PERMISSION_TIER_DENIED_WRITE: &str = "permission_tier_denied_write";
pub const PERMISSION_TIER_DENIED_READ: &str = "permission_tier_denied_read";
pub const PERMISSION_TIER_ROUTINE_EXCEEDS_SESSION: &str = "permission_tier_routine_exceeds_session";

// Redacts the argument payload attached to a runtime-tier or final-confirmation
// denial. The raw arguments routinely carry file paths and form contents that
// must never be echoed back on a denial, so — exactly like the mail-tool
// argument path — the whole blob collapses to a fixed marker and fails closed
// on malformed input.
pub fn redact_denial_arguments(_raw: &str) -> String {
    // JSON string literal, byte-identical to json!("[REDACTED]").to_string()
    // without pulling in the macro's internal unwrap.
    "\"[REDACTED]\"".to_string()
}

// ---- Runtime permission-tier policy mirror (plan row 3) -------------------
//
// Rust-side mirror of the C++ MahoCapabilityBroker::Evaluate tier gate. The
// broker is the security boundary; this mirror exists so the internal-agent
// decorator can fail before a capability ever crosses into the browser with
// the exact same tier policy. The two MUST stay in lockstep:
//
//   read_only    -> writes denied, out-of-whitelist reads denied
//   guard        -> free inside whitelist roots, ask-gate outside
//   full_access  -> allowed (structural sandbox/origin/lease still apply)

/// Canonical session permission tiers, ordered weakest to strongest. The
/// derived Ord is load-bearing: routine admission takes
/// min(session, routine) by this ordering.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum RuntimeTier {
    ReadOnly,
    Guard,
    FullAccess,
}

impl RuntimeTier {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::ReadOnly => "read_only",
            Self::Guard => "guard",
            Self::FullAccess => "full_access",
        }
    }
}

/// Outcome of the tier gate for one file-scoped request. `Allow` means the
/// tier adds no restriction of its own; descriptor-level approval, lease,
/// origin, and presence obligations are orthogonal and still apply.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TierFileDecision {
    Allow,
    RequireApproval,
    Deny(&'static str),
}

/// Parses a raw runtime-config tier string into its canonical form, failing
/// closed to [`RuntimeTier::Guard`] for empty or unknown input. Must stay
/// byte-compatible with the C++ `ParseRuntimeConfigTier` in
/// maho_capability_broker.cc and with the maho-ffi `normalize_tier`.
pub fn parse_runtime_tier(raw: &str) -> RuntimeTier {
    match raw {
        "read_only" => RuntimeTier::ReadOnly,
        "guard" => RuntimeTier::Guard,
        "full_access" => RuntimeTier::FullAccess,
        // Fail closed to the session default tier.
        _ => RuntimeTier::Guard,
    }
}

/// Component-boundary whitelist containment for the runtime-tier file gate.
/// Roots are absolute directory prefixes (account dirs, Downloads, Documents,
/// session dirs) supplied by the session. Fails closed: an empty path, a
/// relative path, an empty root list, or a sibling-prefix collision
/// (`/a/bc` vs root `/a/b`) is outside the whitelist.
pub fn fs_path_inside_whitelist(path: &str, whitelist_roots: &[&str]) -> bool {
    if path.is_empty() || !path.starts_with('/') {
        return false;
    }
    whitelist_roots.iter().any(|root| {
        let root = root.trim_end_matches('/');
        if root.is_empty() || !root.starts_with('/') {
            return false;
        }
        path == root || path.starts_with(&format!("{root}/"))
    })
}

/// Tier gate for one file-scoped request (the Rust mirror of the broker's
/// file/write branch). `path` is the local filesystem path the capability
/// reads or writes; an empty path means the request is not file-scoped and
/// the gate is inert. `is_write` mirrors the descriptor mutability class.
pub fn authorize_tier_fs_request(
    raw_tier: &str,
    is_write: bool,
    path: &str,
    whitelist_roots: &[&str],
) -> TierFileDecision {
    // Not a file-scoped request: the tier file gate is inert and structural
    // descriptor obligations elsewhere remain authoritative.
    if path.is_empty() {
        return TierFileDecision::Allow;
    }
    match parse_runtime_tier(raw_tier) {
        // Full access removes only the tier ask, never the structural
        // sandbox/origin/lease checks (enforced by the broker regardless).
        RuntimeTier::FullAccess => TierFileDecision::Allow,
        RuntimeTier::ReadOnly => {
            if is_write {
                TierFileDecision::Deny(PERMISSION_TIER_DENIED_WRITE)
            } else if fs_path_inside_whitelist(path, whitelist_roots) {
                TierFileDecision::Allow
            } else {
                TierFileDecision::Deny(PERMISSION_TIER_DENIED_READ)
            }
        }
        RuntimeTier::Guard => {
            if fs_path_inside_whitelist(path, whitelist_roots) {
                TierFileDecision::Allow
            } else {
                TierFileDecision::RequireApproval
            }
        }
    }
}

/// Routine admission clamp: a routine can never run with a tier above the
/// session's rank. Returns min(session, routine) by [`RuntimeTier`] ordering;
/// raw strings are parsed fail-closed like every other tier input.
pub fn clamp_routine_tier(session_raw: &str, routine_raw: &str) -> RuntimeTier {
    let session = parse_runtime_tier(session_raw);
    let routine = parse_runtime_tier(routine_raw);
    if routine > session {
        session
    } else {
        routine
    }
}

// ---- Kernel-tool tier gate (Wave 1D, plan D7) ------------------------------
//
// CLI-side application of the runtime-tier mirror to the kernel (non-browser)
// tool dispatch. A CLI task session has no capability broker (the broker
// gates browser tools inside the browser process — disjoint by surface, plan
// R-F8), so the session tier is enforced directly in the dispatch path with
// the same policy table as the broker's file/write branch.

/// Kernel (non-browser) tools whose whole capability is write-class: they can
/// mutate local system state outside any single file scope (fs_write emits
/// artifacts, shell_exec runs arbitrary commands), so a read_only tier denies
/// the class outright instead of relying on per-file scoping.
pub const TIER_WRITE_CLASS_KERNEL_TOOLS: &[&str] = &["fs_write", "shell_exec"];

/// Outcome of the tier gate for one kernel tool call at the dispatch
/// boundary. `Allow` means the tier adds no restriction of its own and the
/// approval policy stays authoritative; `RequireApproval` forces the call
/// onto the interactive prompt path even when the policy would auto-allow;
/// `Deny` refuses the call with the machine-consumed tier reason code.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum KernelTierDecision {
    Allow,
    RequireApproval,
    Deny(&'static str),
}

/// Tier gate for one kernel tool call — the dispatch-boundary composition of
/// the write-class rule and the file-scoped [`authorize_tier_fs_request`].
/// `is_write`/`fs_path` carry the file-scoped part of the request (see
/// [`kernel_fs_request`]); an empty `fs_path` means the call is not
/// file-scoped and only the write-class rule applies. Unknown or empty tier
/// input fails closed to guard via [`parse_runtime_tier`], like every other
/// tier entry point.
pub fn authorize_kernel_tool_request(
    raw_tier: &str,
    tool_name: &str,
    is_write: bool,
    fs_path: &str,
    whitelist_roots: &[&str],
) -> KernelTierDecision {
    if parse_runtime_tier(raw_tier) == RuntimeTier::ReadOnly
        && TIER_WRITE_CLASS_KERNEL_TOOLS.contains(&tool_name)
    {
        return KernelTierDecision::Deny(PERMISSION_TIER_DENIED_WRITE);
    }
    match authorize_tier_fs_request(raw_tier, is_write, fs_path, whitelist_roots) {
        TierFileDecision::Allow => KernelTierDecision::Allow,
        TierFileDecision::RequireApproval => KernelTierDecision::RequireApproval,
        TierFileDecision::Deny(reason) => KernelTierDecision::Deny(reason),
    }
}

/// File-scoped part of a kernel tool call: `(is_write, absolute fs_path)`,
/// resolved lexically — no filesystem access, the tier gate must stay pure;
/// canonicalization and symlink containment stay owned by the tools
/// themselves. fs_read reads its `path` argument (workspace-relative input
/// resolved against `workspace_root`); fs_write writes inside the artifact
/// root (its `relative_path` resolved against `artifact_root`); shell_exec is
/// not file-scoped and rides the write-class rule alone.
pub fn kernel_fs_request(
    tool_name: &str,
    raw_args: &str,
    workspace_root: &std::path::Path,
    artifact_root: Option<&std::path::Path>,
) -> (bool, String) {
    let args: serde_json::Value = serde_json::from_str(raw_args).unwrap_or(serde_json::Value::Null);
    match tool_name {
        "fs_read" => {
            let raw = args.get("path").and_then(|v| v.as_str()).unwrap_or("");
            let path = std::path::Path::new(raw);
            let resolved = if path.is_absolute() {
                path.to_path_buf()
            } else {
                workspace_root.join(path)
            };
            (false, resolved.to_string_lossy().into_owned())
        }
        "fs_write" => {
            let raw = args
                .get("relative_path")
                .and_then(|v| v.as_str())
                .unwrap_or("");
            // Without a configured artifact root the tool itself fails; the
            // gate still gets the workspace-relative resolution so a read_only
            // tier denies the write class before any tool-level error leaks.
            let base = artifact_root.unwrap_or(workspace_root);
            (true, base.join(raw).to_string_lossy().into_owned())
        }
        _ => (false, String::new()),
    }
}

/// Single provisioning source for the runtime-tier fs whitelist roots
/// (plan R-N2): the workspace defaults. The CLI tier wiring (Wave 1D) and the
/// Wave 2A C++ req_ctx stamping MUST both derive their root lists from this
/// function (the C++ side via its same-input mirror) — independent root
/// lists drift apart and reintroduce the cross-surface divergence the
/// permission.rs mirror exists to prevent.
///
/// The workspace default is the workspace root itself, returned as an
/// absolute lexical prefix. A relative or empty anchor provisions an EMPTY
/// list, which [`fs_path_inside_whitelist`] fails closed against — the gate
/// then ask-gates (guard) or denies (read_only) every file-scoped request.
pub fn default_fs_whitelist_roots(workspace_root: &std::path::Path) -> Vec<String> {
    let trimmed = workspace_root
        .to_string_lossy()
        .trim_end_matches('/')
        .to_string();
    if trimmed.is_empty() || !trimmed.starts_with('/') {
        return Vec::new();
    }
    vec![trimmed]
}

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
        CredentialTypingAuthorizationAction::RequireApproval(CREDENTIAL_TYPING_APPROVAL_REQUIRED)
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

/// Outcome of the final-confirmation gate (plan row 5) for one request.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FinalConfirmDecision {
    /// Not gated: the request is either outside the consequence scope or
    /// resolves to a free class (read/navigate/draft/fill-before-submit).
    Allow,
    /// Externally visible or irreversible: a user confirmation must be
    /// obtained before commit. The payload is the fixed public reason code.
    RequireApproval(&'static str),
}

/// Final-confirmation gate for one capability request (plan row 5; the Rust
/// mirror of the broker's consequence gate). Under `final_confirm=true` a
/// request that is externally visible or irreversible must obtain a user
/// approval before it commits, covering BOTH classification paths:
///
///   (a) browser-action consequences resolved by
///       [`classify_action_consequence`] — every class except Ordinary and
///       NewOrigin gates, and a missing `validated_action_context` resolves
///       to Unknown, which fails closed to gated;
///   (b) mail write/account mutations (`mail_send`, `mail_queue_email`, ...)
///       and credential typing (`allow_credentials` requests), which never
///       reach the browser-action classifier and would silently bypass (a).
///
/// Read/navigate/draft/fill-before-submit stay free. `final_confirm=false`
/// leaves today's behavior untouched: the gate is inert and never grants —
/// mail writes keep their unconditional typed approval in
/// [`authorize_mail_tool`], and every structural obligation elsewhere still
/// applies. The approval ask itself routes through the existing approval
/// handshake; this function only decides whether one is owed.
pub fn authorize_final_confirm(
    tool_name: &str,
    raw_args: &str,
    final_confirm: bool,
) -> FinalConfirmDecision {
    if !final_confirm {
        return FinalConfirmDecision::Allow;
    }
    let raw_name = tool_name
        .rsplit_once('/')
        .map_or(tool_name, |(_, name)| name);
    // Path (b): mail tools are invisible to the browser-action classifier;
    // their write/account-mutation class is the externally visible one here.
    if let Some(MailToolClass::WriteAccount) = classify_mail_tool(raw_name) {
        return FinalConfirmDecision::RequireApproval(FINAL_CONFIRM_REQUIRED);
    }
    // Path (b): credential typing. Same contract as
    // authorize_credential_typing — an allow_credentials request must never
    // be its own evidence of approval.
    if matches!(raw_name, "browser_type" | "input.type") {
        let requests_credentials = serde_json::from_str::<serde_json::Value>(raw_args)
            .ok()
            .and_then(|value| {
                value
                    .get("allow_credentials")
                    .and_then(serde_json::Value::as_bool)
            })
            .unwrap_or(false);
        if authorize_credential_typing(requests_credentials)
            != CredentialTypingAuthorizationAction::Allow
        {
            return FinalConfirmDecision::RequireApproval(FINAL_CONFIRM_REQUIRED);
        }
    }
    // Path (a): same request scope as authorize_action_consequence — tools
    // outside it carry no consequence class and stay free.
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
        return FinalConfirmDecision::Allow;
    }
    match classify_action_consequence(tool_name, raw_args) {
        // Ordinary passes; NewOrigin (navigation) is reversible and stays
        // free per the row contract. Unknown — including the missing
        // validated-context case — fails closed to gated.
        ActionConsequence::Ordinary | ActionConsequence::NewOrigin => FinalConfirmDecision::Allow,
        _ => FinalConfirmDecision::RequireApproval(FINAL_CONFIRM_REQUIRED),
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
    fn runtime_tier_denial_codes_are_stable_public_contract_strings() {
        // Machine-consumed public contract strings. Exact snake_case spelling is
        // load-bearing; mirror any change on the C++ side when it is wired.
        assert_eq!(FINAL_CONFIRM_REQUIRED, "final_confirm_required");
        assert_eq!(PERMISSION_TIER_DENIED_WRITE, "permission_tier_denied_write");
        assert_eq!(PERMISSION_TIER_DENIED_READ, "permission_tier_denied_read");
        assert_eq!(
            PERMISSION_TIER_ROUTINE_EXCEEDS_SESSION,
            "permission_tier_routine_exceeds_session"
        );
    }

    #[test]
    fn tier_denial_payload_redacts_file_path_and_form_contents() {
        let path = "/Users/victim/Documents/tax-return-2025.pdf";
        let form = "4111 1111 1111 1111";
        let payload = redact_denial_arguments(&format!(
            r#"{{"path":"{path}","form":{{"card_number":"{form}"}}}}"#
        ));
        assert!(!payload.contains(path), "raw path leaked: {payload}");
        assert!(!payload.contains(form), "form content leaked: {payload}");
        assert!(payload.contains("[REDACTED]"), "missing marker: {payload}");
    }

    #[test]
    fn tier_denial_payload_fails_closed_on_non_json_arguments() {
        let path = "/private/etc/master.passwd";
        let payload = redact_denial_arguments(&format!("not-json path={path}"));
        assert!(!payload.contains(path), "raw path leaked: {payload}");
        assert!(payload.contains("[REDACTED]"), "missing marker: {payload}");
    }

    #[test]
    fn runtime_tier_read_only_denies_writes_and_out_of_whitelist_reads() {
        let roots = ["/Users/u/Downloads", "/Users/u/MahoSessions/s-1"];
        // Writes are denied everywhere, even inside the whitelist, and an
        // approval token can never rescue a tier denial.
        assert_eq!(
            authorize_tier_fs_request("read_only", true, "/Users/u/Downloads/a.pdf", &roots),
            TierFileDecision::Deny(PERMISSION_TIER_DENIED_WRITE)
        );
        assert_eq!(
            authorize_tier_fs_request("read_only", true, "/etc/passwd", &roots),
            TierFileDecision::Deny(PERMISSION_TIER_DENIED_WRITE)
        );
        // Out-of-whitelist reads are denied...
        assert_eq!(
            authorize_tier_fs_request("read_only", false, "/etc/passwd", &roots),
            TierFileDecision::Deny(PERMISSION_TIER_DENIED_READ)
        );
        // ...while in-whitelist reads pass the tier gate.
        assert_eq!(
            authorize_tier_fs_request("read_only", false, "/Users/u/Downloads/a.pdf", &roots),
            TierFileDecision::Allow
        );
    }

    #[test]
    fn runtime_tier_guard_is_free_inside_whitelist_and_ask_gates_outside() {
        let roots = ["/Users/u/Downloads", "/Users/u/Documents"];
        // Inside the whitelist the tier gate adds nothing.
        assert_eq!(
            authorize_tier_fs_request("guard", false, "/Users/u/Downloads/report.pdf", &roots),
            TierFileDecision::Allow
        );
        assert_eq!(
            authorize_tier_fs_request("guard", true, "/Users/u/Documents/notes.txt", &roots),
            TierFileDecision::Allow
        );
        // Outside the whitelist every file operation ask-gates.
        assert_eq!(
            authorize_tier_fs_request("guard", true, "/etc/passwd", &roots),
            TierFileDecision::RequireApproval
        );
        assert_eq!(
            authorize_tier_fs_request("guard", false, "/Users/u/other/x", &roots),
            TierFileDecision::RequireApproval
        );
        // An empty path is not file-scoped: the gate is inert (mirrors the
        // C++ fs_path contract; structural descriptor gates stay in charge).
        assert_eq!(
            authorize_tier_fs_request("guard", true, "", &roots),
            TierFileDecision::Allow
        );
    }

    #[test]
    fn kernel_write_class_tools_denied_under_read_only_regardless_of_path() {
        // fs_write and shell_exec are write-class: read_only denies the whole
        // class even with an empty (not-file-scoped) path, and even inside
        // the whitelist.
        for tool in TIER_WRITE_CLASS_KERNEL_TOOLS {
            assert_eq!(
                authorize_kernel_tool_request("read_only", tool, true, "", &[]),
                KernelTierDecision::Deny(PERMISSION_TIER_DENIED_WRITE),
                "{tool} must be class-denied under read_only"
            );
            assert_eq!(
                authorize_kernel_tool_request(
                    "read_only",
                    tool,
                    true,
                    "/ws/artifacts/a.txt",
                    &["/ws"]
                ),
                KernelTierDecision::Deny(PERMISSION_TIER_DENIED_WRITE),
                "{tool} must be class-denied under read_only even in-whitelist"
            );
        }
        // The class list is exactly the write-class kernel pair — browser
        // tools stay broker-gated (plan R-F8) and must never appear here.
        assert_eq!(TIER_WRITE_CLASS_KERNEL_TOOLS, &["fs_write", "shell_exec"]);
    }

    #[test]
    fn kernel_tier_gate_mirrors_broker_file_branch_for_non_class_tools() {
        let roots = ["/ws"];
        // fs_read under read_only: in-whitelist allow, outside deny-with-code.
        assert_eq!(
            authorize_kernel_tool_request("read_only", "fs_read", false, "/ws/notes.md", &roots),
            KernelTierDecision::Allow
        );
        assert_eq!(
            authorize_kernel_tool_request("read_only", "fs_read", false, "/etc/passwd", &roots),
            KernelTierDecision::Deny(PERMISSION_TIER_DENIED_READ)
        );
        // guard ask-gates outside the whitelist, is inert inside.
        assert_eq!(
            authorize_kernel_tool_request("guard", "fs_read", false, "/etc/passwd", &roots),
            KernelTierDecision::RequireApproval
        );
        assert_eq!(
            authorize_kernel_tool_request("guard", "fs_read", false, "/ws/notes.md", &roots),
            KernelTierDecision::Allow
        );
        // full_access adds no restriction; non-fs tools are inert (empty path).
        assert_eq!(
            authorize_kernel_tool_request("full_access", "fs_read", false, "/etc/passwd", &roots),
            KernelTierDecision::Allow
        );
        assert_eq!(
            authorize_kernel_tool_request("read_only", "web_search", false, "", &[]),
            KernelTierDecision::Allow
        );
        // Unknown tier strings fail closed to guard.
        assert_eq!(
            authorize_kernel_tool_request("bogus", "fs_read", false, "/etc/passwd", &roots),
            KernelTierDecision::RequireApproval
        );
    }

    #[test]
    fn kernel_fs_request_resolves_each_tool_shape_lexically() {
        let ws = std::path::Path::new("/ws");
        let artifact = std::path::Path::new("/ws/.maho/artifacts");

        // fs_read: absolute arg passes through; relative resolves against ws.
        assert_eq!(
            kernel_fs_request("fs_read", r#"{"path":"/etc/passwd"}"#, ws, None),
            (false, "/etc/passwd".to_string())
        );
        assert_eq!(
            kernel_fs_request("fs_read", r#"{"path":"src/lib.rs"}"#, ws, None),
            (false, "/ws/src/lib.rs".to_string())
        );

        // fs_write: relative_path resolves against the artifact root when
        // configured, workspace otherwise (is_write=true either way).
        assert_eq!(
            kernel_fs_request(
                "fs_write",
                r#"{"relative_path":"out.txt"}"#,
                ws,
                Some(artifact)
            ),
            (true, "/ws/.maho/artifacts/out.txt".to_string())
        );
        assert_eq!(
            kernel_fs_request("fs_write", r#"{"relative_path":"out.txt"}"#, ws, None),
            (true, "/ws/out.txt".to_string())
        );

        // shell_exec and non-kernel tools are not file-scoped.
        assert_eq!(
            kernel_fs_request("shell_exec", r#"{"command":"ls"}"#, ws, None),
            (false, String::new())
        );
        assert_eq!(
            kernel_fs_request("web_search", r#"{"query":"x"}"#, ws, None),
            (false, String::new())
        );
        // Malformed args degrade to empty lookups, never panic (the empty
        // relative path resolves against the workspace root).
        let (is_write, resolved) = kernel_fs_request("fs_read", "not-json", ws, None);
        assert!(!is_write);
        assert!(
            resolved.starts_with("/ws"),
            "unexpected resolution: {resolved}"
        );
    }

    #[test]
    fn default_fs_whitelist_roots_provision_workspace_anchor_fail_closed() {
        assert_eq!(
            default_fs_whitelist_roots(std::path::Path::new("/home/u/project")),
            vec!["/home/u/project".to_string()]
        );
        // Trailing slash is trimmed so prefix containment cannot be split.
        assert_eq!(
            default_fs_whitelist_roots(std::path::Path::new("/home/u/project/")),
            vec!["/home/u/project".to_string()]
        );
        // Relative or empty anchors provision an EMPTY list: fs_path_inside_
        // whitelist fails closed against an empty list.
        assert!(default_fs_whitelist_roots(std::path::Path::new(".")).is_empty());
        assert!(default_fs_whitelist_roots(std::path::Path::new("")).is_empty());
    }

    #[test]
    fn routine_tier_clamps_to_session_rank_at_admission() {
        assert_eq!(
            clamp_routine_tier("read_only", "full_access"),
            RuntimeTier::ReadOnly
        );
        assert_eq!(
            clamp_routine_tier("guard", "full_access"),
            RuntimeTier::Guard
        );
        assert_eq!(
            clamp_routine_tier("guard", "read_only"),
            RuntimeTier::ReadOnly
        );
        assert_eq!(
            clamp_routine_tier("full_access", "full_access"),
            RuntimeTier::FullAccess
        );
        // Unknown inputs fail closed to guard on both sides.
        assert_eq!(
            clamp_routine_tier("full_access", "root"),
            RuntimeTier::Guard
        );
        assert_eq!(clamp_routine_tier("bogus", "guard"), RuntimeTier::Guard);
    }

    #[test]
    fn runtime_tier_strings_reflect_broker_gate_contract() {
        // Reflection contract owned by this row: the exact tier strings row 1
        // stores on the session runtime_config (and the C++
        // ParseRuntimeConfigTier accepts) must round-trip and produce the
        // DESIGN table outcome for the same file-scoped request.
        for (raw, expected) in [
            ("read_only", RuntimeTier::ReadOnly),
            ("guard", RuntimeTier::Guard),
            ("full_access", RuntimeTier::FullAccess),
        ] {
            assert_eq!(parse_runtime_tier(raw), expected);
            assert_eq!(parse_runtime_tier(raw).as_str(), raw);
        }
        // Unknown / empty fail closed to guard, mirroring the broker.
        assert_eq!(parse_runtime_tier(""), RuntimeTier::Guard);
        assert_eq!(parse_runtime_tier("GUARD"), RuntimeTier::Guard);
        // Full-access removes only the tier ask, never the structural gates:
        // the mirror grants the tier dimension on the same request the C++
        // broker permits, and the ask-gate dimension matches guard exactly.
        let roots = ["/Users/u/Downloads"];
        assert_eq!(
            authorize_tier_fs_request("full_access", true, "/etc/passwd", &roots),
            TierFileDecision::Allow
        );
        assert_eq!(
            authorize_tier_fs_request("guard", true, "/etc/passwd", &roots),
            TierFileDecision::RequireApproval
        );
    }

    #[test]
    fn final_confirm_gate_gates_submit_classified_actions_under_default_flag() {
        // A browser-minted validated context classifying the click as a final
        // form submit is externally visible: gated under final_confirm=true
        // (the session default).
        assert_eq!(
            authorize_final_confirm(
                "browser_click",
                r#"{"ref":7,"validated_action_context":{"validated":true,"consequence":"submit"}}"#,
                true,
            ),
            FinalConfirmDecision::RequireApproval(FINAL_CONFIRM_REQUIRED)
        );
    }

    #[test]
    fn final_confirm_gate_keeps_read_and_out_of_scope_actions_free() {
        // Read-class browser actions stay free under the default flag...
        assert_eq!(
            authorize_final_confirm("browser_scroll", r#"{"direction":"down"}"#, true),
            FinalConfirmDecision::Allow
        );
        assert_eq!(
            authorize_final_confirm("browser_hover", r#"{"ref":1}"#, true),
            FinalConfirmDecision::Allow
        );
        // ...as do tools outside the consequence scope entirely.
        assert_eq!(
            authorize_final_confirm("web_search", r#"{"query":"maho"}"#, true),
            FinalConfirmDecision::Allow
        );
    }

    #[test]
    fn final_confirm_gate_gates_mail_send_and_credential_typing() {
        // Mail writes are NOT covered by the browser-action classification
        // path; missing them here would be a silent bypass of the gate.
        for tool in ["mail_send", "mail_queue_email", "mail_add_account"] {
            assert_eq!(
                authorize_final_confirm(tool, r#"{"to":"a@example.com"}"#, true),
                FinalConfirmDecision::RequireApproval(FINAL_CONFIRM_REQUIRED),
                "{tool}"
            );
        }
        // Credential typing rides the same gate...
        assert_eq!(
            authorize_final_confirm(
                "browser_type",
                r#"{"ref":8,"allow_credentials":true}"#,
                true,
            ),
            FinalConfirmDecision::RequireApproval(FINAL_CONFIRM_REQUIRED)
        );
        // ...while mail reads and validated fill-before-submit stay free.
        assert_eq!(
            authorize_final_confirm("mail_get_email", r#"{"email_id":"1"}"#, true),
            FinalConfirmDecision::Allow
        );
        assert_eq!(
            authorize_final_confirm(
                "browser_type",
                r#"{"ref":8,"text":"hi","validated_action_context":{"validated":true,"consequence":"ordinary"}}"#,
                true,
            ),
            FinalConfirmDecision::Allow
        );
    }

    #[test]
    fn final_confirm_gate_fails_closed_on_missing_context_and_is_inert_when_disabled() {
        // A missing validated_action_context on an action tool resolves to
        // Unknown — the gate must fail closed instead of auto-passing.
        assert_eq!(
            classify_action_consequence("browser_click", r#"{"ref":9}"#),
            ActionConsequence::Unknown
        );
        assert_eq!(
            authorize_final_confirm("browser_click", r#"{"ref":9}"#, true),
            FinalConfirmDecision::RequireApproval(FINAL_CONFIRM_REQUIRED)
        );
        // final_confirm=false preserves today's behavior exactly: the gate
        // is inert for every class and adds no new gating.
        for (tool, args) in [
            ("browser_click", r#"{"ref":9}"#),
            (
                "browser_click",
                r#"{"ref":9,"validated_action_context":{"validated":true,"consequence":"submit"}}"#,
            ),
            ("mail_send", r#"{"to":"a@example.com"}"#),
            ("browser_type", r#"{"ref":8,"allow_credentials":true}"#),
            ("browser_scroll", r#"{"direction":"down"}"#),
        ] {
            assert_eq!(
                authorize_final_confirm(tool, args, false),
                FinalConfirmDecision::Allow,
                "{tool}"
            );
        }
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
