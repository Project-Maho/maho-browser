mod auth;
mod browser;
pub mod config;
mod credits;
mod doctor;
mod platform;

use maho_cli::browser_pipe;

use anyhow::{bail, Context, Result};
use clap::{Args, Parser, Subcommand};
use maho_agent::{AgentStreamEvent, SwiftideBackend};
use maho_cli::vault::{self, RecoveryUnlockResult};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use config::CliConfig;

const DEFAULT_RELAY_URL: &str = "https://relay.maho.co";

#[derive(Parser)]
#[command(
    name = "maho",
    version,
    about = "Maho CLI — browser control from your terminal"
)]
pub struct Cli {
    /// Output structured JSON for all commands
    #[arg(long, global = true)]
    pub json: bool,

    /// Override UDS socket path (for testing)
    #[arg(long, global = true)]
    pub socket_path: Option<String>,

    /// Override the relay URL
    #[arg(long, global = true, default_value = DEFAULT_RELAY_URL)]
    pub relay_url: String,

    #[command(subcommand)]
    pub command: Commands,
}

#[derive(Subcommand)]
pub enum Commands {
    /// Authentication & account management
    Auth {
        #[command(subcommand)]
        command: AuthCommands,
    },

    /// Credit balance & purchases
    Billing {
        #[command(subcommand)]
        command: BillingCommands,
    },

    /// Active tab operations
    Tab {
        #[command(subcommand)]
        command: TabCommands,
    },

    /// Browsing history queries
    History {
        #[command(subcommand)]
        command: HistoryCommands,
    },

    /// Bookmark operations
    Bookmarks {
        #[command(subcommand)]
        command: BookmarkCommands,
    },

    /// Mail operations
    Mail {
        #[command(subcommand)]
        command: MailCommands,
    },

    /// Page content extraction (typed extractors)
    Page {
        #[command(subcommand)]
        command: PageCommands,
    },

    /// Headless extraction (single-URL or batch)
    Headless(HeadlessArgs),

    /// Browser control utilities
    Browser {
        /// Override the browser MCP RPC timeout in seconds.
        ///
        /// Without this flag, cheap RPCs use 15 seconds while accessibility
        /// snapshots and ref-consuming input actions use 60 seconds.
        #[arg(long, global = true, value_name = "SECS")]
        timeout: Option<u64>,

        #[command(subcommand)]
        command: BrowserCommands,
    },

    /// Manage MCP servers or run as stdio MCP server
    Mcp {
        #[command(subcommand)]
        command: Option<McpCommands>,
    },

    /// Manage and run routines
    Routine {
        #[command(subcommand)]
        command: RoutineCommands,
    },

    /// Run agent tasks against the shared workspace store
    Agent {
        #[command(subcommand)]
        command: AgentCommands,
    },

    #[command(about = "Vault recovery operations")]
    Vault {
        #[command(subcommand)]
        command: VaultCommands,
    },

    Doctor {
        #[arg(long)]
        fix: bool,
    },

    /// Show version info
    Version,

    /// Tool execution and inspection
    Tool {
        #[command(subcommand)]
        command: ToolCommands,
    },

    /// Platform contacts operations
    Contacts {
        #[command(subcommand)]
        command: ContactsCommands,
    },

    /// Platform iMessage operations
    Imessage {
        #[command(subcommand)]
        command: ImessageCommands,
    },

    /// Platform disk status
    Disk {
        #[command(subcommand)]
        command: DiskCommands,
    },

    /// Platform desktop input (keyboard/mouse) — requires --approve
    Desktop {
        #[command(subcommand)]
        command: DesktopCommands,
    },

    // ─── Deprecation shims (hidden) ───────────────────────────────────────
    /// [deprecated] Use `maho auth login`
    #[command(hide = true)]
    Login {
        /// Sign up instead of logging in
        #[arg(long)]
        signup: bool,

        /// Email (non-interactive; skips prompt)
        #[arg(long)]
        email: Option<String>,

        /// Password (non-interactive; skips prompt)
        #[arg(long)]
        password: Option<String>,
    },

    /// [deprecated] Use `maho auth signup`
    #[command(hide = true)]
    Signup {
        /// Email (non-interactive; skips prompt)
        #[arg(long)]
        email: Option<String>,

        /// Password (non-interactive; skips prompt)
        #[arg(long)]
        password: Option<String>,
    },

    /// [deprecated] Use `maho auth logout`
    #[command(hide = true)]
    Logout,

    /// [deprecated] Use `maho auth whoami`
    #[command(hide = true)]
    Whoami,

    /// [deprecated] Use `maho auth config-show`
    #[command(hide = true)]
    Config {
        #[command(subcommand)]
        action: ConfigAction,
    },

    /// [deprecated] Use `maho billing balance`
    #[command(hide = true)]
    Balance,

    /// [deprecated] Use `maho billing buy-credits`
    #[command(hide = true, name = "buy-credits")]
    BuyCredits {
        /// Suggested USD amount (10, 50, or 100); choose the final amount in checkout
        #[arg(long, value_parser = ["10", "50", "100"])]
        amount: String,
    },
}

#[derive(Subcommand)]
pub enum ToolCommands {
    /// List browser capabilities and catalog diagnostics
    List,

    /// Describe one browser capability by canonical ID
    Describe {
        /// Canonical capability ID
        id: String,
    },

    /// Run one browser capability by canonical ID
    #[command(alias = "call")]
    Run {
        /// Canonical capability ID
        id: String,

        /// Arguments in JSON format
        #[arg(long = "args")]
        args: Option<String>,
    },
}

#[derive(Subcommand)]
pub enum McpCommands {
    /// Add a new MCP server configuration
    Add {
        /// Name of the MCP server
        name: String,

        /// Command to run the MCP server (for stdio transport)
        #[arg(long)]
        command: Option<String>,

        /// URL of the MCP server (for HTTP transport)
        #[arg(long)]
        url: Option<String>,

        /// Socket path of the MCP server (for UDS transport)
        #[arg(long, name = "socket-path")]
        mcp_socket_path: Option<String>,

        /// Mark the MCP server as trusted (auto-approves non-sensitive tools)
        #[arg(long)]
        trusted: bool,

        /// Workspace ID or name override
        #[arg(long)]
        workspace: Option<String>,
    },

    /// List all configured MCP servers
    List {
        /// Workspace ID or name override
        #[arg(long)]
        workspace: Option<String>,
    },

    /// Remove an MCP server configuration by name
    Remove {
        /// Name of the MCP server to remove
        name: String,

        /// Workspace ID or name override
        #[arg(long)]
        workspace: Option<String>,
    },
}

#[derive(Subcommand)]
pub enum RoutineCommands {
    /// List all available built-in and user-defined routines
    List {
        /// Workspace ID or name override
        #[arg(long)]
        workspace: Option<String>,
    },

    /// Run a routine by ID.
    ///
    /// Routines execute inside the running browser under its own provider,
    /// model, and permission settings, so there is deliberately no
    /// --provider/--model/--workspace here. Use `maho agent task` for a
    /// CLI-side session you can point at a specific model or provider.
    Run {
        /// The ID of the routine to run
        name: String,
    },
}

/// Agent session subcommands (plan row 13 re-introduction of a CLI agent
/// entry; sessions ride the same shared-workspace store transport the
/// removed `maho run` path used — no new transport).
#[derive(Subcommand, Debug, Clone)]
pub enum AgentCommands {
    /// Run one agent task in a session bound to the shared workspace store
    Task {
        /// Task goal for the agent to execute
        goal: String,

        /// Permission tier carried in the session runtime_config
        /// (read-only | guard | full-access)
        #[arg(long, value_enum, default_value_t = TierArg::Guard)]
        tier: TierArg,

        /// Require confirmation before externally visible or irreversible
        /// actions (default); whichever of --final-confirm/--no-final-confirm
        /// appears last wins
        #[arg(long, overrides_with = "no_final_confirm", default_value_t = true)]
        final_confirm: bool,

        /// Opposite of --final-confirm: leave the final-confirmation gate inert
        #[arg(long, overrides_with = "final_confirm")]
        no_final_confirm: bool,

        /// Run the session in proactive mode (max-effort autonomous
        /// instruction block appended to the system prompt); whichever of
        /// --proactive/--no-proactive appears last wins
        #[arg(long, overrides_with = "no_proactive")]
        proactive: bool,

        /// Opposite of --proactive: keep the proactive instruction block off
        #[arg(long, overrides_with = "proactive")]
        no_proactive: bool,

        /// Resume an existing session by ID instead of starting a fresh
        /// task-<uuid> session: stored conversation turns and title are
        /// reused. Fails closed when the session does not exist.
        #[arg(long, value_name = "SESSION_ID")]
        resume: Option<String>,

        /// Enable composite (batch) page execution for this session: the
        /// model may emit one composite_execute call carrying an ordered
        /// batch of page operations. Fail-closed default — without the flag
        /// the composite gate refuses every batch before any executor runs.
        #[arg(long)]
        composite: bool,

        /// Auto-approve every tool call (AllowAll) for automation/headless
        /// runs, skipping the interactive approval prompt. Without it, a TTY
        /// session prompts y/n per sensitive tool call and a headless session
        /// fails closed (sensitive calls denied).
        #[arg(long)]
        approve_all: bool,

        /// Target workspace ID or name (defaults to the first workspace in
        /// the shared registry, bootstrapping the default when empty)
        #[arg(long)]
        workspace: Option<String>,

        /// Model this session routes to, overriding the workspace profile's
        /// preferred model (e.g. anthropic/claude-3-7-sonnet). The named
        /// model is used exactly: routing performs no silent substitution.
        #[arg(long, short = 'm', value_name = "MODEL")]
        model: Option<String>,

        /// LLM provider whose stored key this session uses (e.g. anthropic,
        /// openai). A selected provider is probed alone — the session never
        /// falls back to another provider's credential, so your data only
        /// reaches the provider you named.
        #[arg(long, short = 'p', value_name = "PROVIDER")]
        provider: Option<String>,

        /// Absolute directory the agent may read and write (repeatable).
        /// Replaces the implicit workspace-root whitelist; every path
        /// outside the set is gated by the tier as usual.
        #[arg(long = "dir", value_name = "PATH")]
        dirs: Vec<PathBuf>,

        /// Stream the full agent event stream (tokens, thinking, artifacts,
        /// proof) to stderr in addition to the default tool-progress view.
        /// Ignored with --json, which stays final-only.
        #[arg(long)]
        stream: bool,
    },

    /// List agent sessions that `maho agent task --resume <SESSION_ID>` can
    /// rebind to, most recently updated first
    Sessions {
        /// Maximum number of sessions to list
        #[arg(long, default_value_t = 20)]
        limit: usize,

        /// Include browser-side conversations too, not just the `task-`
        /// sessions minted by `maho agent task`
        #[arg(long)]
        all: bool,
    },
}

/// Permission tier for the `maho agent task` runtime_config. Unknown values
/// are rejected at parse time (arg_enum); the canonical snake_case spelling
/// mirrors the broker's `ParseRuntimeConfigTier` and the maho-ffi
/// `normalize_tier` contract (fail-closed normalization stays session-side).
#[derive(Copy, Clone, Debug, PartialEq, Eq, clap::ValueEnum)]
pub enum TierArg {
    /// File-scoped reads only; writes denied (canonical: read_only)
    ReadOnly,
    /// Routine actions run, consequential actions gated (session default)
    Guard,
    /// Full access; the tier adds no restriction of its own
    FullAccess,
}

impl TierArg {
    pub fn canonical(self) -> &'static str {
        match self {
            TierArg::ReadOnly => "read_only",
            TierArg::Guard => "guard",
            TierArg::FullAccess => "full_access",
        }
    }
}

/// Session runtime_config payload carried by `maho agent task` (plan row 13).
/// Shape and defaults mirror the row-1 session runtime config
/// (`AgentRuntimeConfig` in maho-ffi, broker `CapabilityRequestContext`):
/// tier "guard", final_confirm true, proactive_mode false, composite
/// disabled. `composite_enabled` rides the same payload but is bound
/// load-once at session open (Wave 3A) — it gates only the composite batch
/// runner, never the per-call tier gate.
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct AgentTaskRuntimeConfig {
    pub permission_tier: String,
    pub final_confirm: bool,
    pub proactive_mode: bool,
    /// Wave 3A (G8): composite (batch) page execution. Fail-closed default
    /// — without `--composite` the runner refuses every batch.
    pub composite_enabled: bool,
    /// Per-session model override (`-m`). `None` leaves routing on the
    /// workspace profile's `preferred_model`.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub model: Option<String>,
    /// Per-session provider override (`-p`), normalized to the lowercase
    /// key the backend's credential resolution uses. `None` leaves the
    /// unselected-provider fall-through in force.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub provider: Option<String>,
}

impl Default for AgentTaskRuntimeConfig {
    fn default() -> Self {
        Self {
            permission_tier: "guard".to_string(),
            final_confirm: true,
            proactive_mode: false,
            composite_enabled: false,
            model: None,
            provider: None,
        }
    }
}

impl AgentTaskRuntimeConfig {
    /// Builds the payload from parsed CLI parts. `--final-confirm` and
    /// `--no-final-confirm` (likewise `--proactive`/`--no-proactive`) are a
    /// mutually-overriding flag pair (POSIX last-one-wins via `overrides_with`;
    /// an overridden flag reverts to its default), so the raw pair collapses
    /// to one effective value per gate here. `composite` is a plain opt-in
    /// flag (no opposite): it fails closed when absent.
    #[allow(clippy::too_many_arguments)]
    pub fn from_parts(
        tier: TierArg,
        final_confirm: bool,
        no_final_confirm: bool,
        proactive: bool,
        no_proactive: bool,
        composite: bool,
        model: Option<String>,
        provider: Option<String>,
    ) -> Self {
        Self {
            permission_tier: tier.canonical().to_string(),
            final_confirm: final_confirm && !no_final_confirm,
            proactive_mode: proactive && !no_proactive,
            composite_enabled: composite,
            model: normalize_routing_override(model, false),
            provider: normalize_routing_override(provider, true),
        }
    }
}

/// Trims a routing override and drops it when nothing is left, so a blank
/// `-m ''` never reaches the backend as an empty preference (which routing
/// would treat as "no preference" anyway, silently). Providers are
/// additionally lowercased to match the backend's provider keying.
fn normalize_routing_override(value: Option<String>, lowercase: bool) -> Option<String> {
    let trimmed = value?.trim().to_string();
    if trimmed.is_empty() {
        return None;
    }
    Some(if lowercase {
        trimmed.to_ascii_lowercase()
    } else {
        trimmed
    })
}

#[derive(Subcommand)]
pub enum VaultCommands {
    #[command(about = "Unlock a Vault with recovery material entered through the terminal prompt")]
    Recover,
}

#[derive(Subcommand)]
pub enum AuthCommands {
    /// Log in to Maho
    Login {
        /// Sign up instead of logging in
        #[arg(long)]
        signup: bool,

        /// Email (non-interactive; skips prompt)
        #[arg(long)]
        email: Option<String>,

        /// Password (non-interactive; skips prompt)
        #[arg(long)]
        password: Option<String>,
    },
    /// Sign up for a new account
    Signup {
        /// Email (non-interactive; skips prompt)
        #[arg(long)]
        email: Option<String>,

        /// Password (non-interactive; skips prompt)
        #[arg(long)]
        password: Option<String>,
    },
    /// Log out and wipe credentials
    Logout,
    /// Show current user info
    Whoami,
    /// Show current config (tokens redacted)
    #[command(name = "config-show")]
    ConfigShow,
}

#[derive(Subcommand)]
pub enum BillingCommands {
    /// Show PAYG credit balance
    Balance,
    /// Open the pay-what-you-want credit checkout
    #[command(name = "buy-credits")]
    BuyCredits {
        /// Suggested USD amount (10, 50, or 100); choose the final amount in checkout
        #[arg(long, value_parser = ["10", "50", "100"])]
        amount: String,
    },
}

#[derive(Subcommand)]
pub enum TabCommands {
    /// Show info about the active tab
    Info,
    /// List open tabs
    List {
        #[arg(long)]
        space: Option<String>,
    },
    /// Get info for a specific tab
    Get {
        /// Tab ID
        tab_id: String,
    },
    /// Close a tab
    Close {
        /// Tab ID
        tab_id: String,
    },
    /// Open a new tab
    New {
        #[arg(long)]
        url: Option<String>,
        #[arg(long)]
        space: Option<String>,
    },
    /// Navigate a tab to a URL
    Navigate {
        /// Tab ID
        tab_id: String,
        #[arg(long)]
        url: String,
    },
    /// Read tab content
    Read {
        #[arg(long, default_value = "markdown")]
        format: String,
        #[arg(long)]
        selection: bool,
        #[arg(long)]
        tab: Option<String>,
    },
    /// Annotate a tab (reads stdin)
    Annotate {
        #[arg(long)]
        title: Option<String>,
        #[arg(long)]
        tab: Option<String>,
    },
    /// Write content to a tab (reads stdin)
    Write {
        #[arg(long, default_value = "markdown")]
        format: String,
        #[arg(long)]
        tab: Option<String>,
    },
    /// Acquire an exclusive lease on a tab
    #[command(name = "lease-acquire")]
    LeaseAcquire {
        /// Lease TTL (e.g. "30s", "5m")
        #[arg(long, default_value = "60s")]
        ttl: String,
        /// Tab ID (default: active tab)
        #[arg(long)]
        tab: Option<String>,
    },
    /// Release a held lease
    #[command(name = "lease-release")]
    LeaseRelease {
        #[arg(long)]
        tab: Option<String>,
    },
    /// Send heartbeat to extend a lease
    #[command(name = "lease-heartbeat")]
    LeaseHeartbeat {
        #[arg(long)]
        tab: Option<String>,
    },
}

#[derive(Subcommand)]
pub enum HistoryCommands {
    /// List history entries
    List {
        #[arg(long)]
        since: Option<String>,
        #[arg(long)]
        domain: bool,
        #[arg(long)]
        space: Option<String>,
        #[arg(long)]
        limit: Option<u32>,
    },
    /// Search history with filters
    Search {
        #[arg(long)]
        url_substring: Option<String>,
        #[arg(long)]
        title_substring: Option<String>,
        #[arg(long)]
        referrer_domain: Option<String>,
        #[arg(long)]
        space: Option<String>,
        #[arg(long)]
        since: Option<String>,
        #[arg(long, default_value = "human")]
        format: String,
    },
    /// Export history to NDJSON
    Export {
        #[arg(long)]
        out: Option<String>,
    },
}

#[derive(Subcommand)]
pub enum BookmarkCommands {
    /// List bookmarks
    List {
        #[arg(long)]
        folder: Option<String>,
        #[arg(long)]
        space: Option<String>,
    },
    /// Search bookmarks
    Search {
        /// Search query
        query: String,
        #[arg(long)]
        limit: Option<u32>,
    },
    /// Create a bookmark
    Create {
        #[arg(long)]
        url: String,
        #[arg(long)]
        title: String,
        #[arg(long)]
        folder: Option<String>,
    },
}

#[derive(Subcommand)]
pub enum MailCommands {
    /// List configured mail accounts
    Accounts,
    /// List folders for a mail account
    Folders {
        /// Mail account ID
        #[arg(long)]
        account_id: String,
    },
    /// List emails in a folder
    List {
        /// Mail account ID
        #[arg(long)]
        account_id: String,
        /// Mail folder ID
        #[arg(long)]
        folder_id: String,
        /// Maximum number of emails to return
        #[arg(long)]
        limit: Option<i64>,
        /// Number of emails to skip
        #[arg(long)]
        offset: Option<i64>,
    },
    /// Get one email by ID
    Get {
        /// Email ID
        email_id: String,
    },
    /// Search emails
    Search {
        /// Search query
        query: String,
        /// Optional mail account ID filter
        #[arg(long)]
        account_id: Option<String>,
        /// Optional mail folder ID filter
        #[arg(long)]
        folder_id: Option<String>,
        /// Maximum number of emails to return
        #[arg(long)]
        limit: Option<i64>,
        /// Number of emails to skip
        #[arg(long)]
        offset: Option<i64>,
    },
    /// List all emails in a thread
    Thread {
        /// Mail account ID containing the message
        #[arg(long)]
        account_id: String,
        /// Message ID whose thread to list
        #[arg(long)]
        message_id: String,
    },
    /// Extract a one-time password from recent mail
    Otp {
        /// Optional mail account ID filter
        #[arg(long)]
        account_id: Option<String>,
        /// Optional mail folder ID filter
        #[arg(long)]
        folder_id: Option<String>,
        /// Optional search query to narrow candidate messages
        #[arg(long)]
        query: Option<String>,
        /// Only consider mail newer than this many seconds
        #[arg(long)]
        max_age_seconds: Option<i64>,
    },
}

#[derive(Subcommand)]
pub enum PageCommands {
    /// Get readable content (markdown)
    Content {
        #[arg(long)]
        tab: Option<String>,
    },
    /// Get plaintext body
    Text {
        #[arg(long)]
        tab: Option<String>,
    },
    /// Search page content for matching excerpts
    Search {
        #[arg(long)]
        query: String,
        #[arg(long)]
        max_results: Option<u32>,
        #[arg(long)]
        tab: Option<String>,
    },
    /// Extract structured page context (typed)
    Context {
        #[arg(long)]
        tab: Option<String>,
    },
    /// Take a screenshot
    Screenshot {
        #[arg(long)]
        tab: Option<String>,
        #[arg(long)]
        out: Option<String>,
        #[arg(long)]
        full_page: bool,
    },
    /// Query a CSS selector, return matching elements
    #[command(name = "query-selector")]
    QuerySelector {
        /// CSS selector
        selector: String,
        #[arg(long)]
        tab: Option<String>,
    },
    /// Get text content of elements matching a selector
    #[command(name = "get-text")]
    GetText {
        /// CSS selector
        selector: String,
        #[arg(long)]
        tab: Option<String>,
    },
    /// Get an attribute value from elements matching a selector
    #[command(name = "get-attribute")]
    GetAttribute {
        /// CSS selector
        selector: String,
        /// Attribute name
        #[arg(long)]
        attr: String,
        #[arg(long)]
        tab: Option<String>,
    },
    /// Wait for a selector to appear in the page
    #[command(name = "wait-for-selector")]
    WaitForSelector {
        /// CSS selector
        selector: String,
        /// Timeout in milliseconds
        #[arg(long, default_value = "5000")]
        timeout: u64,
        #[arg(long)]
        tab: Option<String>,
    },
}

#[derive(Args)]
pub struct HeadlessArgs {
    /// Single URL to fetch
    #[arg(long, conflicts_with = "batch")]
    pub url: Option<String>,

    /// Batch file of URLs (one per line)
    #[arg(long, conflicts_with = "url")]
    pub batch: Option<String>,

    /// Named fields to extract (comma-separated)
    #[arg(long)]
    pub extract: String,

    /// Output format
    #[arg(long, default_value = "text")]
    pub format: String,

    /// Wait condition (e.g. "selector:#main", "timeout:5s")
    #[arg(long)]
    pub wait: Option<String>,

    /// Output file (batch mode)
    #[arg(long)]
    pub out: Option<String>,

    /// Concurrency for batch mode
    #[arg(long, default_value = "2")]
    pub concurrency: u32,

    /// Skip URLs already in this file (resume)
    #[arg(long)]
    pub skip_completed: Option<String>,

    /// Auto-launch Maho.app if browser is not running
    #[arg(long)]
    pub launch: bool,

    /// Keep browser running after CLI exits (only with --launch)
    #[arg(long)]
    pub keep_alive: bool,
}

#[derive(Subcommand)]
pub enum BrowserCommands {
    /// Interactive browser REPL
    Repl,

    /// Machine-readable persistent automation pipe over stdin/stdout NDJSON
    Pipe,
}

#[derive(Subcommand, Clone)]
pub enum ConfigAction {
    /// Show current config
    Show,
}

#[derive(Subcommand, Debug, Clone)]
pub enum ContactsCommands {
    /// Search contacts by query
    Search {
        /// Contact search query
        query: String,
    },
    /// Resolve contact handles
    Resolve {
        /// Contact handles
        handles: Vec<String>,
    },
}

#[derive(Subcommand, Debug, Clone)]
pub enum ImessageCommands {
    /// List iMessage chats
    Chats {
        /// Path to chat.db SQLite database
        #[arg(long)]
        db_path: Option<String>,
    },
    /// Retrieve message history for a chat
    History {
        /// Chat GUID
        chat_guid: String,
        /// Maximum number of messages to return
        #[arg(long, default_value = "50")]
        limit: u32,
        /// Path to chat.db SQLite database
        #[arg(long)]
        db_path: Option<String>,
    },
    /// Search messages by query or sender
    Search {
        /// Search query text
        query: Option<String>,
        /// Filter by sender handle
        #[arg(long)]
        sender: Option<String>,
        /// Path to chat.db SQLite database
        #[arg(long)]
        db_path: Option<String>,
    },
    /// Send an iMessage
    Send {
        /// Recipient handle
        #[arg(long)]
        to: String,
        /// Message text
        #[arg(long)]
        text: String,
        /// Chat GUID
        #[arg(long)]
        chat_guid: Option<String>,
        /// Dry run: compose without sending
        #[arg(long)]
        dry_run: bool,
        /// Approve send
        #[arg(long)]
        approve: bool,
        /// Path to chat.db SQLite database
        #[arg(long)]
        db_path: Option<String>,
    },
}

#[derive(Subcommand, Debug, Clone)]
pub enum DiskCommands {
    /// Check Full Disk Access status
    Status,
}

#[derive(Subcommand, Debug, Clone)]
pub enum DesktopCommands {
    /// Report Accessibility (TCC) grant status without prompting
    Status,

    /// Capture the screen to an image file
    Capture {
        /// Optional path to save the captured image
        #[arg(long)]
        output: Option<std::path::PathBuf>,
    },

    /// Perform OCR on a captured screen or image
    Ocr {
        /// Optional bounding box as "x,y,w,h"
        #[arg(long)]
        region: Option<String>,
        /// Output all recognized text blocks instead of filtered summary
        #[arg(long)]
        full: bool,
        /// Optional path to an image file to OCR
        #[arg(long)]
        output: Option<std::path::PathBuf>,
    },

    /// Inspect accessibility elements of an application
    Elements {
        /// Target application: "frontmost" or numeric PID
        #[arg(long)]
        app: Option<String>,
        /// Filter by element role (e.g. AXButton, AXTextField)
        #[arg(long)]
        role: Option<String>,
        /// Optional path to write elements output
        #[arg(long)]
        output: Option<std::path::PathBuf>,
    },

    /// Find text on screen or in an image and return coordinates
    FindText {
        /// Text to search for
        #[arg(long)]
        text: String,
        /// Optional image path to search in (defaults to live screen)
        #[arg(long)]
        image: Option<std::path::PathBuf>,
    },

    /// Move the mouse cursor to absolute coordinates
    Move {
        #[arg(long)]
        x: i32,
        #[arg(long)]
        y: i32,
        #[arg(long)]
        approve: bool,
        #[arg(long)]
        dry_run: bool,
    },

    /// Click a mouse button
    Click {
        #[arg(long, default_value = "left")]
        button: String,
        #[arg(long)]
        approve: bool,
        #[arg(long)]
        dry_run: bool,
    },

    /// Scroll by (dx, dy)
    Scroll {
        #[arg(long, default_value = "0")]
        dx: i32,
        #[arg(long, default_value = "0")]
        dy: i32,
        #[arg(long)]
        approve: bool,
        #[arg(long)]
        dry_run: bool,
    },

    /// Type text into the focused application
    Type {
        #[arg(long)]
        text: String,
        #[arg(long)]
        approve: bool,
        #[arg(long)]
        dry_run: bool,
    },

    /// Press a hotkey combo, e.g. --keys cmd+shift+t (last key is tapped)
    Hotkey {
        #[arg(long)]
        keys: String,
        #[arg(long)]
        approve: bool,
        #[arg(long)]
        dry_run: bool,
    },
}

// ─── Deprecation helper ───────────────────────────────────────────────────────

fn deprecation_warning(old: &str, new: &str) {
    eprintln!(
        "[deprecated] `maho {old}` is now `maho {new}`. This alias will be removed in v0.6.0."
    );
}

// ─── Main ─────────────────────────────────────────────────────────────────────

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .with_target(false)
        .init();

    let cli = Cli::parse();
    let socket_path = cli.socket_path;
    let json_output = cli.json;
    let relay_url = cli.relay_url;
    let command = cli.command;

    match command {
        // ─── Auth group ───────────────────────────────────────────────────
        Commands::Auth { command } => match command {
            AuthCommands::Login {
                signup,
                email,
                password,
            } => cmd_login(&relay_url, signup, email, password).await,
            AuthCommands::Signup { email, password } => {
                cmd_login(&relay_url, true, email, password).await
            }
            AuthCommands::Logout => cmd_logout(),
            AuthCommands::Whoami => cmd_whoami(&relay_url).await,
            AuthCommands::ConfigShow => cmd_config_show(),
        },

        // ─── Billing group ────────────────────────────────────────────────
        Commands::Billing { command } => match command {
            BillingCommands::Balance => cmd_balance(&relay_url).await,
            BillingCommands::BuyCredits { amount } => cmd_buy_credits(&relay_url, &amount).await,
        },

        // ─── Tab group ──────────────────────────────────────────────────────
        Commands::Tab { command } => cmd_tab(command, socket_path.as_deref(), json_output).await,

        // ─── History group ────────────────────────────────────────────────────
        Commands::History { command } => {
            cmd_history(command, socket_path.as_deref(), json_output).await
        }

        // ─── Bookmarks group ──────────────────────────────────────────────────
        Commands::Bookmarks { command } => {
            cmd_bookmarks(command, socket_path.as_deref(), json_output).await
        }

        // ─── Mail group ───────────────────────────────────────────────────────
        Commands::Mail { command } => cmd_mail(command, socket_path.as_deref(), json_output).await,

        // ─── Page group ───────────────────────────────────────────────────
        Commands::Page { command } => cmd_page(command, socket_path.as_deref(), json_output).await,

        // ─── Headless ─────────────────────────────────────────────────────
        Commands::Headless(args) => cmd_headless(args, socket_path.as_deref(), json_output).await,

        // ─── Browser group ─────────────────────────────────────
        Commands::Browser { command, timeout } => {
            browser::set_timeout_override(timeout)?;
            match command {
                BrowserCommands::Repl => cmd_repl(socket_path.as_deref(), json_output).await,
                BrowserCommands::Pipe => browser_pipe::run_pipe(socket_path.as_deref()).await,
            }
        }

        // ─── MCP server ───────────────────────────────────────────────────
        Commands::Mcp { command } => cmd_mcp(command, json_output).await,

        // ─── Routine group ────────────────────────────────────────────────
        Commands::Routine { command } => match command {
            RoutineCommands::List { workspace: _ } => {
                cmd_routine_list(socket_path.as_deref(), json_output).await
            }
            RoutineCommands::Run { name } => {
                cmd_routine_run(&name, socket_path.as_deref(), json_output).await
            }
        },

        // ─── Agent group ──────────────────────────────────────────────
        Commands::Agent { command } => match command {
            AgentCommands::Task {
                goal,
                tier,
                final_confirm,
                no_final_confirm,
                proactive,
                no_proactive,
                composite,
                resume,
                approve_all,
                workspace,
                model,
                provider,
                dirs,
                stream,
            } => {
                let config = AgentTaskRuntimeConfig::from_parts(
                    tier,
                    final_confirm,
                    no_final_confirm,
                    proactive,
                    no_proactive,
                    composite,
                    model,
                    provider,
                );
                cmd_agent_task(
                    &goal,
                    &config,
                    AgentTaskSessionInputs {
                        workspace: workspace.as_deref(),
                        approve_all,
                        resume: resume.as_deref(),
                        dirs: &dirs,
                    },
                    json_output,
                    stream,
                )
                .await
            }
            AgentCommands::Sessions { limit, all } => {
                cmd_agent_sessions(limit, all, json_output).await
            }
        },

        Commands::Vault { command } => match command {
            VaultCommands::Recover => match vault::recover_from_terminal()? {
                RecoveryUnlockResult::Unlocked => {
                    println!("Vault recovered.");
                    Ok(())
                }
                RecoveryUnlockResult::InvalidRecoveryMaterial => {
                    bail!("Vault recovery material was not accepted.")
                }
                RecoveryUnlockResult::Failed => {
                    bail!("Vault recovery failed.")
                }
            },
        },

        Commands::Doctor { fix } => cmd_doctor(fix, socket_path.as_deref(), json_output).await,

        // ─── Version ──────────────────────────────────────────────────────
        Commands::Version => {
            println!("maho {}", env!("CARGO_PKG_VERSION"));
            Ok(())
        }

        Commands::Tool { command } => cmd_tool(command, socket_path.as_deref(), json_output).await,

        Commands::Contacts { command } => platform::cmd_contacts(command).await,
        Commands::Imessage { command } => platform::cmd_imessage(command).await,
        Commands::Disk { command } => platform::cmd_disk(command).await,
        Commands::Desktop { command } => platform::cmd_desktop(command).await,

        // ─── Deprecation shims ────────────────────────────────────────────
        Commands::Login {
            signup,
            email,
            password,
        } => {
            deprecation_warning("login", "auth login");
            cmd_login(&relay_url, signup, email, password).await
        }
        Commands::Signup { email, password } => {
            deprecation_warning("signup", "auth signup");
            cmd_login(&relay_url, true, email, password).await
        }
        Commands::Logout => {
            deprecation_warning("logout", "auth logout");
            cmd_logout()
        }
        Commands::Whoami => {
            deprecation_warning("whoami", "auth whoami");
            cmd_whoami(&relay_url).await
        }
        Commands::Config { action } => {
            deprecation_warning("config show", "auth config-show");
            match action {
                ConfigAction::Show => cmd_config_show(),
            }
        }
        Commands::Balance => {
            deprecation_warning("balance", "billing balance");
            cmd_balance(&relay_url).await
        }
        Commands::BuyCredits { amount } => {
            deprecation_warning("buy-credits", "billing buy-credits");
            cmd_buy_credits(&relay_url, &amount).await
        }
    }
}

// ─── Command handlers (preserved from original) ──────────────────────────────

async fn cmd_login(
    relay_url: &str,
    signup: bool,
    email_flag: Option<String>,
    password_flag: Option<String>,
) -> Result<()> {
    let email = match email_flag {
        Some(e) => e,
        None => {
            print!("Email: ");
            std::io::Write::flush(&mut std::io::stdout())?;
            let mut buf = String::new();
            std::io::stdin().read_line(&mut buf)?;
            buf.trim().to_string()
        }
    };

    let password = match password_flag {
        Some(p) => p,
        None => rpassword::prompt_password("Password: ")?,
    };

    let resp = if signup {
        println!("Signing up...");
        auth::signup(relay_url, &email, &password).await?
    } else {
        println!("Logging in...");
        auth::login(relay_url, &email, &password).await?
    };

    let cfg = CliConfig {
        access_token: Some(resp.access_token),
        refresh_token: Some(resp.refresh_token),
        email: Some(email.clone()),
        tier: None,
    };
    cfg.save()?;

    println!("Logged in as {}", email);
    Ok(())
}

fn cmd_logout() -> Result<()> {
    CliConfig::wipe()?;
    println!("Logged out. Credentials removed.");
    Ok(())
}

async fn cmd_whoami(relay_url: &str) -> Result<()> {
    let cfg = CliConfig::load()?;
    let me = auth::whoami(relay_url, &cfg).await?;
    println!("Email: {}", me.email().unwrap_or(""));
    println!("Tier:  {}", me.tier.as_deref().unwrap_or("free"));
    Ok(())
}

fn cmd_config_show() -> Result<()> {
    let cfg = CliConfig::load()?;
    println!("{}", cfg.redacted_display());
    Ok(())
}

async fn cmd_balance(relay_url: &str) -> Result<()> {
    let cfg = CliConfig::load()?;
    let token = cfg.access_token.as_deref().ok_or_else(|| {
        eprintln!("Not logged in. Run `maho auth login` first.");
        anyhow::anyhow!("not logged in")
    })?;

    let bal = credits::fetch_balance(relay_url, token).await?;

    println!("PAYG Credit Balance: ${:.2} USD", bal.balance_usd);
    println!(
        "Lifetime purchased:  ${:.2} USD",
        bal.lifetime_purchased_usd
    );
    Ok(())
}

async fn cmd_buy_credits(relay_url: &str, amount: &str) -> Result<()> {
    let cfg = CliConfig::load()?;
    let _token = cfg.access_token.as_deref().ok_or_else(|| {
        eprintln!("Not logged in. Run `maho auth login` first.");
        anyhow::anyhow!("not logged in")
    })?;

    let me = auth::whoami(relay_url, &cfg).await?;
    let user_id = me.user_id().ok_or_else(|| {
        anyhow::anyhow!("authenticated relay account missing numeric user_id in /auth/me")
    })?;

    let url = credits::checkout_url_for_pack(amount, user_id)?;
    println!("Opening LemonSqueezy pay-what-you-want checkout (suggested amount: ${amount}; choose the final amount in checkout)...");
    println!("Checkout URL: {url}");
    match credits::open_checkout_url(&url) {
        Ok(credits::OpenOutcome::OpenedInMahoBrowser) => {
            println!("Opened in Maho Browser.");
        }
        Ok(credits::OpenOutcome::OpenedInSystemBrowser) => {
            println!("Opened in your default browser.");
        }
        Ok(credits::OpenOutcome::SkippedForTest) => {}
        Err(e) => {
            eprintln!("Could not open browser automatically: {e}");
            eprintln!("Please open the URL above manually.");
        }
    }
    println!(
        "Once your purchase completes, run `maho billing balance` to verify your new balance."
    );
    Ok(())
}

// ─── Utility functions ────────────────────────────────────────────────────────

// ─── Tab command handlers ─────────────────────────────────────────────────────

/// Browser tool responses are wrapped in a capability-receipt envelope
/// (`{"receipt": .., "result": {..}}`). Reach through it so callers can read the
/// payload fields directly; a bare payload is returned unchanged.
fn tool_payload(value: &serde_json::Value) -> &serde_json::Value {
    value.get("result").unwrap_or(value)
}

async fn cmd_tab(command: TabCommands, socket_path: Option<&str>, json_output: bool) -> Result<()> {
    match command {
        TabCommands::Info => {
            let client = browser::connect(socket_path).await?;
            let result =
                browser::send_tool_call(&client, "browser_tab_list", serde_json::json!({})).await?;
            let tabs = tool_payload(&result).get("tabs").and_then(|t| t.as_array());
            let active = tabs.and_then(|arr| {
                arr.iter()
                    .find(|t| t.get("is_active") == Some(&serde_json::Value::Bool(true)))
            });

            match active {
                Some(tab) => {
                    if json_output {
                        println!("{}", serde_json::to_string_pretty(tab)?);
                    } else {
                        println!(
                            "id: {}",
                            tab.get("id").and_then(|v| v.as_i64()).unwrap_or(0)
                        );
                        println!(
                            "title: {}",
                            tab.get("title").and_then(|v| v.as_str()).unwrap_or("")
                        );
                        println!(
                            "url: {}",
                            tab.get("url").and_then(|v| v.as_str()).unwrap_or("")
                        );
                        println!("active: true");
                    }
                }
                None => {
                    eprintln!("No active tab found.");
                    std::process::exit(1);
                }
            }
            Ok(())
        }
        TabCommands::List { .. } => {
            let client = browser::connect(socket_path).await?;
            let result =
                browser::send_tool_call(&client, "browser_tab_list", serde_json::json!({})).await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
                return Ok(());
            }

            let tabs = tool_payload(&result).get("tabs").and_then(|t| t.as_array());
            if let Some(tabs) = tabs {
                println!("{:<4} {:<40} {:<60} {}", "ID", "TITLE", "URL", "ACTIVE");
                for tab in tabs {
                    let id = tab.get("id").and_then(|v| v.as_i64()).unwrap_or(0);
                    let title = tab.get("title").and_then(|v| v.as_str()).unwrap_or("");
                    let url = tab.get("url").and_then(|v| v.as_str()).unwrap_or("");
                    let active = tab
                        .get("is_active")
                        .and_then(|v| v.as_bool())
                        .unwrap_or(false);
                    println!(
                        "{:<4} {:<40} {:<60} {}",
                        id,
                        browser::truncate(title, 40),
                        browser::truncate(url, 60),
                        if active { "*" } else { "" }
                    );
                }
            }
            Ok(())
        }
        TabCommands::Get { tab_id } => {
            let client = browser::connect(socket_path).await?;
            let result = browser::send_tool_call(
                &client,
                "browser_tab_get",
                serde_json::json!({ "tab_id": tab_id.parse::<i64>().unwrap_or(0) }),
            )
            .await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!(
                    "id: {}",
                    result.get("id").and_then(|v| v.as_i64()).unwrap_or(0)
                );
                println!(
                    "title: {}",
                    result.get("title").and_then(|v| v.as_str()).unwrap_or("")
                );
                println!(
                    "url: {}",
                    result.get("url").and_then(|v| v.as_str()).unwrap_or("")
                );
                println!(
                    "active: {}",
                    result
                        .get("is_active")
                        .and_then(|v| v.as_bool())
                        .unwrap_or(false)
                );
            }
            Ok(())
        }
        TabCommands::Close { tab_id } => {
            let client = browser::connect(socket_path).await?;
            let result = browser::send_tool_call(
                &client,
                "browser_tab_close",
                serde_json::json!({ "tab_id": tab_id.parse::<i64>().unwrap_or(0) }),
            )
            .await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!("Tab {} closed.", tab_id);
            }
            Ok(())
        }
        TabCommands::New { url, space } => {
            let client = browser::connect(socket_path).await?;
            let mut args = serde_json::json!({});
            if let Some(u) = &url {
                args["url"] = serde_json::Value::String(u.clone());
            }
            if let Some(s) = &space {
                args["space"] = serde_json::Value::String(s.clone());
            }
            let result = browser::send_tool_call(&client, "browser_tab_new", args).await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                let tab = result.get("tab").unwrap_or(&result);
                let id = tab.get("id").and_then(|v| v.as_i64()).unwrap_or(0);
                let tab_url = tab
                    .get("url")
                    .and_then(|v| v.as_str())
                    .unwrap_or("about:blank");
                println!("New tab {id}: {tab_url}");
            }
            Ok(())
        }
        TabCommands::Navigate { tab_id, url } => {
            let client = browser::connect(socket_path).await?;
            let tab_id_i64 = tab_id.parse::<i64>().unwrap_or(0);
            let _ = browser::send_tool_call(
                &client,
                "browser_acquire_lease",
                serde_json::json!({ "tab_id": tab_id_i64, "ttl_seconds": 60 }),
            )
            .await;
            let result = browser::send_tool_call(
                &client,
                "browser_navigate",
                serde_json::json!({ "tab_id": tab_id_i64, "url": url }),
            )
            .await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!("Navigated tab {} to {}", tab_id, url);
            }
            Ok(())
        }
        TabCommands::Read {
            format,
            selection,
            tab,
        } => {
            let client = browser::connect(socket_path).await?;
            let tool = if selection {
                "browser_page_text"
            } else {
                "browser_page_content"
            };
            let mut args = serde_json::json!({ "format": format });
            if let Some(t) = &tab {
                args["tab_id"] = serde_json::Value::Number(serde_json::Number::from(
                    t.parse::<i64>().unwrap_or(0),
                ));
            }
            if selection {
                args["selection_only"] = serde_json::Value::Bool(true);
            }
            let result = browser::send_tool_call(&client, tool, args).await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                let text = result.get("text").and_then(|v| v.as_str()).unwrap_or("");
                print!("{text}");
            }
            Ok(())
        }
        TabCommands::Annotate { title, tab } => {
            let _ = (title, tab);
            eprintln!(
                "Command 'tab annotate' is deprecated and not supported by the browser server."
            );
            std::process::exit(2);
        }
        TabCommands::Write { format, tab } => {
            let _ = (format, tab);
            eprintln!("Command 'tab write' is deprecated and not supported by the browser server.");
            std::process::exit(2);
        }
        TabCommands::LeaseAcquire { ttl, tab } => {
            let client = browser::connect(socket_path).await?;
            let seconds = parse_ttl_to_seconds(&ttl);
            let mut args = serde_json::json!({ "ttl_seconds": seconds });
            if let Some(t) = &tab {
                args["tab_id"] = serde_json::Value::Number(serde_json::Number::from(
                    t.parse::<i64>().unwrap_or(0),
                ));
            }
            let result = browser::send_control_call(&client, "browser_acquire_lease", args).await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                let lease_id = result
                    .get("session_id")
                    .and_then(|v| v.as_str())
                    .unwrap_or("unknown");
                println!("Lease acquired: {lease_id}");
            }
            Ok(())
        }
        TabCommands::LeaseRelease { tab } => {
            let client = browser::connect(socket_path).await?;
            let mut args = serde_json::json!({});
            if let Some(t) = &tab {
                args["tab_id"] = serde_json::Value::Number(serde_json::Number::from(
                    t.parse::<i64>().unwrap_or(0),
                ));
            }
            let result = browser::send_control_call(&client, "browser_release_lease", args).await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!("Lease released.");
            }
            Ok(())
        }
        TabCommands::LeaseHeartbeat { tab } => {
            let client = browser::connect(socket_path).await?;
            let mut args = serde_json::json!({});
            if let Some(t) = &tab {
                args["tab_id"] = serde_json::Value::Number(serde_json::Number::from(
                    t.parse::<i64>().unwrap_or(0),
                ));
            }
            let result =
                browser::send_control_call(&client, "browser_heartbeat_lease", args).await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!("Heartbeat sent.");
            }
            Ok(())
        }
    }
}

fn parse_ttl_to_seconds(ttl: &str) -> i64 {
    if let Some(s) = ttl.strip_suffix('s') {
        s.parse::<i64>().unwrap_or(60)
    } else if let Some(s) = ttl.strip_suffix('m') {
        s.parse::<i64>().unwrap_or(1) * 60
    } else {
        ttl.parse::<i64>().unwrap_or(60)
    }
}

// ─── History command handlers ─────────────────────────────────────────────────

async fn cmd_history(
    command: HistoryCommands,
    socket_path: Option<&str>,
    json_output: bool,
) -> Result<()> {
    match command {
        HistoryCommands::List {
            since,
            domain,
            limit,
            ..
        } => {
            let client = browser::connect(socket_path).await?;

            let since_seconds = match &since {
                Some(s) => Some(browser::parse_duration_to_seconds(s)?),
                None => None,
            };

            let domain_filter: Option<String> = if domain {
                None // group-by-domain mode, no filter
            } else {
                None
            };

            let args = serde_json::json!({
                "query": null,
                "since_seconds": since_seconds,
                "domain": domain_filter,
                "max_results": limit.unwrap_or(50),
            });

            let result = browser::send_tool_call(&client, "browser_history_search", args).await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
                return Ok(());
            }

            let entries = result.get("entries").and_then(|e| e.as_array());
            if let Some(entries) = entries {
                if domain {
                    // Group by domain
                    let mut grouped: std::collections::BTreeMap<String, Vec<&serde_json::Value>> =
                        std::collections::BTreeMap::new();
                    for entry in entries {
                        let url = entry.get("url").and_then(|v| v.as_str()).unwrap_or("");
                        let d = browser::extract_domain(url).to_string();
                        grouped.entry(d).or_default().push(entry);
                    }
                    for (d, items) in &grouped {
                        println!("## {d}");
                        for entry in items {
                            let ts = entry
                                .get("visited_at")
                                .and_then(|v| {
                                    if let Some(s) = v.as_str() {
                                        Some(s.to_string())
                                    } else {
                                        v.as_f64().filter(|f| *f > 0.0).map(|f| f.to_string())
                                    }
                                })
                                .unwrap_or_default();
                            let title = entry.get("title").and_then(|v| v.as_str()).unwrap_or("");
                            let url = entry.get("url").and_then(|v| v.as_str()).unwrap_or("");
                            println!("  {ts} {title}");
                            println!("  {url}");
                        }
                    }
                } else {
                    for entry in entries {
                        let ts = entry
                            .get("visited_at")
                            .and_then(|v| {
                                if let Some(s) = v.as_str() {
                                    Some(s.to_string())
                                } else {
                                    v.as_f64().filter(|f| *f > 0.0).map(|f| f.to_string())
                                }
                            })
                            .unwrap_or_default();
                        let title = entry.get("title").and_then(|v| v.as_str()).unwrap_or("");
                        let url = entry.get("url").and_then(|v| v.as_str()).unwrap_or("");
                        println!("{ts}  {title}");
                        println!("  {url}");
                    }
                }
            }
            Ok(())
        }
        HistoryCommands::Search {
            url_substring,
            title_substring,
            referrer_domain,
            since,
            ..
        } => {
            if referrer_domain.is_some() {
                eprintln!("Error: --referrer-domain option is not supported.");
                std::process::exit(2);
            }
            let client = browser::connect(socket_path).await?;

            let query = url_substring
                .as_deref()
                .or(title_substring.as_deref())
                .unwrap_or("");

            let since_seconds = match &since {
                Some(s) => Some(browser::parse_duration_to_seconds(s)?),
                None => None,
            };

            let args = serde_json::json!({
                "query": query,
                "since_seconds": since_seconds,
            });

            let result = browser::send_tool_call(&client, "browser_history_search", args).await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
                return Ok(());
            }

            let entries = result.get("entries").and_then(|e| e.as_array());
            if let Some(entries) = entries {
                for entry in entries {
                    let ts = entry
                        .get("visited_at")
                        .and_then(|v| {
                            if let Some(s) = v.as_str() {
                                Some(s.to_string())
                            } else {
                                v.as_f64().filter(|f| *f > 0.0).map(|f| f.to_string())
                            }
                        })
                        .unwrap_or_default();
                    let title = entry.get("title").and_then(|v| v.as_str()).unwrap_or("");
                    let url = entry.get("url").and_then(|v| v.as_str()).unwrap_or("");
                    println!("{ts}  {title}");
                    println!("  {url}");
                }
            }
            Ok(())
        }
        HistoryCommands::Export { out } => {
            use std::io::Write;
            let client = browser::connect(socket_path).await?;
            let args = serde_json::json!({ "query": null, "max_results": 100000 });
            let result = browser::send_tool_call(&client, "browser_history_search", args).await?;
            let entries = result
                .get("entries")
                .and_then(|e| e.as_array())
                .cloned()
                .unwrap_or_default();

            let mut writer: Box<dyn Write> = match &out {
                Some(p) => Box::new(std::io::BufWriter::new(
                    std::fs::File::create(p)
                        .with_context(|| format!("failed to create output file: {p}"))?,
                )),
                None => Box::new(std::io::stdout()),
            };
            for entry in &entries {
                writeln!(writer, "{}", serde_json::to_string(entry)?)?;
            }
            writer.flush()?;
            Ok(())
        }
    }
}

// ─── Bookmarks command handlers ───────────────────────────────────────────────

async fn cmd_bookmarks(
    command: BookmarkCommands,
    socket_path: Option<&str>,
    json_output: bool,
) -> Result<()> {
    match command {
        BookmarkCommands::List { .. } => {
            let client = browser::connect(socket_path).await?;
            let result = browser::send_tool_call(
                &client,
                "browser_bookmarks_search",
                serde_json::json!({ "query": "" }),
            )
            .await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
                return Ok(());
            }

            let bookmarks = result.get("bookmarks").and_then(|b| b.as_array());
            if let Some(bookmarks) = bookmarks {
                for bm in bookmarks {
                    let title = bm.get("title").and_then(|v| v.as_str()).unwrap_or("");
                    let url = bm.get("url").and_then(|v| v.as_str()).unwrap_or("");
                    println!("{title}");
                    println!("  {url}");
                }
            }
            Ok(())
        }
        BookmarkCommands::Search { query, .. } => {
            let client = browser::connect(socket_path).await?;
            let result = browser::send_tool_call(
                &client,
                "browser_bookmarks_search",
                serde_json::json!({ "query": query }),
            )
            .await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
                return Ok(());
            }

            let bookmarks = result.get("bookmarks").and_then(|b| b.as_array());
            if let Some(bookmarks) = bookmarks {
                for bm in bookmarks {
                    let title = bm.get("title").and_then(|v| v.as_str()).unwrap_or("");
                    let url = bm.get("url").and_then(|v| v.as_str()).unwrap_or("");
                    println!("{title}");
                    println!("  {url}");
                }
            }
            Ok(())
        }
        BookmarkCommands::Create { url, title, folder } => {
            let client = browser::connect(socket_path).await?;
            let result = browser::send_tool_call(
                &client,
                "browser_bookmark_create",
                serde_json::json!({ "title": title, "url": url, "folder": folder }),
            )
            .await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!("Bookmark created: {title}");
                println!("  {url}");
            }
            Ok(())
        }
    }
}

// ─── Mail command handlers ────────────────────────────────────────────────────

fn mail_tool_request(command: MailCommands) -> (&'static str, serde_json::Value) {
    match command {
        MailCommands::Accounts => ("mail_list_accounts", serde_json::json!({})),
        MailCommands::Folders { account_id } => (
            "mail_list_folders",
            serde_json::json!({ "account_id": account_id }),
        ),
        MailCommands::List {
            account_id,
            folder_id,
            limit,
            offset,
        } => {
            let mut args = serde_json::json!({
                "account_id": account_id,
                "folder_id": folder_id,
            });
            if let Some(limit) = limit {
                args["limit"] = limit.into();
            }
            if let Some(offset) = offset {
                args["offset"] = offset.into();
            }
            ("mail_list_emails", args)
        }
        MailCommands::Get { email_id } => (
            "mail_get_email",
            serde_json::json!({ "email_id": email_id }),
        ),
        MailCommands::Search {
            query,
            account_id,
            folder_id,
            limit,
            offset,
        } => {
            let mut args = serde_json::json!({ "query": query });
            if let Some(account_id) = account_id {
                args["account_id"] = account_id.into();
            }
            if let Some(folder_id) = folder_id {
                args["folder_id"] = folder_id.into();
            }
            if let Some(limit) = limit {
                args["limit"] = limit.into();
            }
            if let Some(offset) = offset {
                args["offset"] = offset.into();
            }
            ("mail_search_emails", args)
        }
        MailCommands::Thread {
            account_id,
            message_id,
        } => (
            "mail_list_thread",
            serde_json::json!({ "account_id": account_id, "message_id": message_id }),
        ),
        MailCommands::Otp {
            account_id,
            folder_id,
            query,
            max_age_seconds,
        } => (
            "mail_extract_otp",
            serde_json::json!({
                "account_id": account_id,
                "folder_id": folder_id,
                "query": query,
                "max_age_seconds": max_age_seconds,
            }),
        ),
    }
}

async fn cmd_mail(
    command: MailCommands,
    socket_path: Option<&str>,
    json_output: bool,
) -> Result<()> {
    let client = browser::connect(socket_path).await?;
    let (tool, args) = mail_tool_request(command);
    let result = browser::send_tool_call(&client, tool, args).await?;
    print_tool_result(&result, json_output)
}

fn print_tool_result(result: &serde_json::Value, json_output: bool) -> Result<()> {
    if json_output {
        println!("{}", serde_json::to_string_pretty(result)?);
    } else {
        println!("{}", result);
    }
    Ok(())
}

// ─── Page command handlers ────────────────────────────────────────────────────

fn apply_tab_id(args: &mut serde_json::Value, tab: &Option<String>) {
    if let Some(t) = tab {
        args["tab_id"] =
            serde_json::Value::Number(serde_json::Number::from(t.parse::<i64>().unwrap_or(0)));
    }
}

async fn page_query_selector_ref(
    client: &maho_browser_mcp::client::BrowserClient,
    selector: &str,
    tab: &Option<String>,
) -> Result<String> {
    let mut args = serde_json::json!({ "selector": selector });
    apply_tab_id(&mut args, tab);
    let result = browser::send_tool_call(client, "page_query_selector", args).await?;
    let ref_id = result.get("ref_id").and_then(|v| v.as_str()).unwrap_or("");
    if ref_id.is_empty() {
        bail!("no element found for selector: {selector}");
    }
    Ok(ref_id.to_string())
}

async fn cmd_page(
    command: PageCommands,
    socket_path: Option<&str>,
    json_output: bool,
) -> Result<()> {
    match &command {
        PageCommands::Search { max_results, .. } => {
            if max_results.is_some() {
                eprintln!("Error: --max-results option is not supported.");
                std::process::exit(2);
            }
        }
        PageCommands::Screenshot { full_page, .. } => {
            if *full_page {
                eprintln!("Error: --full-page option is not supported.");
                std::process::exit(2);
            }
        }
        _ => {}
    }

    let client = browser::connect(socket_path).await?;
    match command {
        PageCommands::Content { tab } => {
            let mut args = serde_json::json!({});
            apply_tab_id(&mut args, &tab);
            let result = browser::send_tool_call(&client, "browser_page_content", args).await?;
            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                print!(
                    "{}",
                    result.get("text").and_then(|v| v.as_str()).unwrap_or("")
                );
            }
            Ok(())
        }
        PageCommands::Text { tab } => {
            let mut args = serde_json::json!({});
            apply_tab_id(&mut args, &tab);
            let result = browser::send_tool_call(&client, "browser_page_text", args).await?;
            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                print!(
                    "{}",
                    result.get("text").and_then(|v| v.as_str()).unwrap_or("")
                );
            }
            Ok(())
        }
        PageCommands::Search {
            query,
            max_results,
            tab,
        } => {
            if max_results.is_some() {
                eprintln!("Error: --max-results option is not supported.");
                std::process::exit(2);
            }
            let mut args = serde_json::json!({ "query": query });
            apply_tab_id(&mut args, &tab);
            let result = browser::send_tool_call(&client, "browser_search_in_page", args).await?;
            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                let matches = result
                    .get("match_count")
                    .and_then(|v| v.as_i64())
                    .unwrap_or(0);
                let active = result
                    .get("active_match_index")
                    .and_then(|v| v.as_i64())
                    .unwrap_or(0);
                println!("matches: {matches}");
                println!("active: {active}");
            }
            Ok(())
        }
        PageCommands::Context { tab } => {
            let mut args = serde_json::json!({});
            apply_tab_id(&mut args, &tab);
            let result = browser::send_tool_call(&client, "browser_page_context", args).await?;
            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!(
                    "url: {}",
                    result.get("url").and_then(|v| v.as_str()).unwrap_or("")
                );
                println!(
                    "title: {}",
                    result.get("title").and_then(|v| v.as_str()).unwrap_or("")
                );
                if let Some(content) = result.get("content").and_then(|v| v.as_str()) {
                    println!("{content}");
                }
            }
            Ok(())
        }
        PageCommands::Screenshot {
            tab,
            out,
            full_page,
        } => {
            if full_page {
                eprintln!("Error: --full-page option is not supported.");
                std::process::exit(2);
            }
            let mut args = serde_json::json!({});
            apply_tab_id(&mut args, &tab);
            let result = browser::send_tool_call(&client, "browser_screenshot_full", args).await?;
            let payload = result.get("result").unwrap_or(&result);
            let data = payload.get("data").and_then(|v| v.as_str()).unwrap_or("");
            if data.is_empty() {
                bail!("browser returned no screenshot data");
            }
            use base64::Engine;
            let bytes = base64::engine::general_purpose::STANDARD
                .decode(data)
                .map_err(|e| anyhow::anyhow!("failed to decode screenshot base64: {e}"))?;
            let path = match out {
                Some(p) => p,
                None => {
                    let ts = std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .unwrap_or_default()
                        .as_secs();
                    format!("/tmp/maho-screenshot-{ts}.png")
                }
            };
            std::fs::write(&path, &bytes)
                .with_context(|| format!("failed to write screenshot: {path}"))?;
            println!("{path}");
            Ok(())
        }
        PageCommands::QuerySelector { selector, tab } => {
            let mut args = serde_json::json!({ "selector": selector });
            apply_tab_id(&mut args, &tab);
            let result = browser::send_tool_call(&client, "page_query_selector", args).await?;
            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                let ref_id = result.get("ref_id").and_then(|v| v.as_str()).unwrap_or("");
                let tag = result.get("tag").and_then(|v| v.as_str()).unwrap_or("");
                println!("ref:{ref_id} tag:{tag}");
            }
            Ok(())
        }
        PageCommands::GetText { selector, tab } => {
            let ref_id = page_query_selector_ref(&client, &selector, &tab).await?;
            let result = browser::send_tool_call(
                &client,
                "page_get_text",
                serde_json::json!({ "ref_id": ref_id }),
            )
            .await?;
            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!(
                    "{}",
                    result.get("text").and_then(|v| v.as_str()).unwrap_or("")
                );
            }
            Ok(())
        }
        PageCommands::GetAttribute {
            selector,
            attr,
            tab,
        } => {
            let ref_id = page_query_selector_ref(&client, &selector, &tab).await?;
            let result = browser::send_tool_call(
                &client,
                "page_get_attribute",
                serde_json::json!({ "ref_id": ref_id, "attribute": attr }),
            )
            .await?;
            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                let value = result
                    .get("value")
                    .and_then(|v| v.as_str())
                    .or_else(|| result.get("attribute").and_then(|v| v.as_str()))
                    .unwrap_or("");
                println!("{value}");
            }
            Ok(())
        }
        PageCommands::WaitForSelector {
            selector,
            timeout,
            tab,
        } => {
            let mut args = serde_json::json!({ "selector": selector, "timeout_ms": timeout });
            apply_tab_id(&mut args, &tab);
            let result = browser::send_tool_call(&client, "page_wait_for_selector", args).await?;
            let found = result
                .get("found")
                .and_then(|v| v.as_bool())
                .unwrap_or(false);
            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!("found: {found}");
            }
            if !found {
                std::process::exit(1);
            }
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{
        format_agent_stream_event, mail_tool_request, AgentCommands, AgentStreamEvent,
        AgentTaskOutput, AgentTaskRuntimeConfig, Cli, Commands, MailCommands, TierArg,
    };

    /// Plan row 13: valid `maho agent task` args parse the GOAL positional and
    /// map the runtime-config flags onto the session runtime_config payload
    /// with the product defaults (tier=guard, final_confirm=true, proactive=false).
    #[test]
    fn agent_task_parses_goal_with_default_runtime_config() {
        use clap::Parser as _;
        let parsed = Cli::try_parse_from(["maho", "agent", "task", "Summarize open tabs"])
            .expect("valid agent task args must parse");
        let Commands::Agent { command } = parsed.command else {
            panic!("expected Commands::Agent variant");
        };
        let AgentCommands::Task {
            goal,
            tier,
            final_confirm,
            no_final_confirm,
            proactive,
            no_proactive,
            composite,
            ..
        } = command
        else {
            panic!("expected AgentCommands::Task");
        };
        assert_eq!(goal, "Summarize open tabs");
        assert_eq!(tier, TierArg::Guard);
        let config = AgentTaskRuntimeConfig::from_parts(
            tier,
            final_confirm,
            no_final_confirm,
            proactive,
            no_proactive,
            composite,
            None,
            None,
        );
        assert_eq!(config, AgentTaskRuntimeConfig::default());
        assert_eq!(config.permission_tier, "guard");
        assert!(config.final_confirm);
        assert!(!config.proactive_mode);
        // Wave 3A: composite fails closed — absent --composite stays disabled.
        assert!(!config.composite_enabled);
    }

    /// Plan row 13: the three runtime-config flags override the defaults, and
    /// the kebab-case tier values normalize to the canonical snake_case
    /// spellings shared with the broker / maho-ffi `normalize_tier`.
    #[test]
    fn agent_task_flags_override_runtime_config_defaults() {
        use clap::Parser as _;
        let parsed = Cli::try_parse_from([
            "maho",
            "agent",
            "task",
            "do it",
            "--tier",
            "read-only",
            "--no-final-confirm",
            "--proactive",
        ])
        .expect("overridden agent task args must parse");
        let Commands::Agent {
            command:
                AgentCommands::Task {
                    tier,
                    final_confirm,
                    no_final_confirm,
                    proactive,
                    no_proactive,
                    composite,
                    ..
                },
        } = parsed.command
        else {
            panic!("expected Commands::Agent(Task)");
        };
        let config = AgentTaskRuntimeConfig::from_parts(
            tier,
            final_confirm,
            no_final_confirm,
            proactive,
            no_proactive,
            composite,
            None,
            None,
        );
        assert_eq!(config.permission_tier, "read_only");
        assert!(!config.final_confirm);
        assert!(config.proactive_mode);
        assert!(
            !config.composite_enabled,
            "composite stays fail-closed unless --composite is passed"
        );

        let parsed = Cli::try_parse_from([
            "maho",
            "agent",
            "task",
            "do it",
            "--tier",
            "full-access",
            "--final-confirm",
        ])
        .expect("full-access + explicit --final-confirm must parse");
        let Commands::Agent {
            command:
                AgentCommands::Task {
                    tier,
                    final_confirm,
                    no_final_confirm,
                    proactive,
                    no_proactive,
                    composite,
                    ..
                },
        } = parsed.command
        else {
            panic!("expected Commands::Agent(Task)");
        };
        let config = AgentTaskRuntimeConfig::from_parts(
            tier,
            final_confirm,
            no_final_confirm,
            proactive,
            no_proactive,
            composite,
            None,
            None,
        );
        assert_eq!(config.permission_tier, "full_access");
        assert!(config.final_confirm);
        assert!(!config.proactive_mode);
        assert!(!config.composite_enabled);
    }

    /// Wave 3A (G8): `--composite` opts the session into composite (batch)
    /// page execution; without the flag the gate stays disabled (fail-closed
    /// default — an absent flag must never arm the batch runner).
    #[test]
    fn agent_task_parses_composite_flag() {
        use clap::Parser as _;
        let parsed =
            Cli::try_parse_from(["maho", "agent", "task", "batch the page ops", "--composite"])
                .expect("--composite must parse");
        let Commands::Agent {
            command: AgentCommands::Task { composite, .. },
        } = parsed.command
        else {
            panic!("expected Commands::Agent(Task)");
        };
        assert!(composite, "--composite must arm the composite gate");
    }

    /// Wave 3C (G9): `--stream` opts the run into the full stderr event
    /// stream; the default run stays progress-only (no flag).
    #[test]
    fn agent_task_stream_flag_parses() {
        use clap::Parser as _;
        let parsed = Cli::try_parse_from(["maho", "agent", "task", "watch the tools"])
            .expect("default agent task args must parse");
        let Commands::Agent {
            command: AgentCommands::Task { stream, .. },
        } = parsed.command
        else {
            panic!("expected Commands::Agent(Task)");
        };
        assert!(!stream, "stream must default to progress-only (off)");

        let parsed = Cli::try_parse_from(["maho", "agent", "task", "watch the tools", "--stream"])
            .expect("--stream must parse");
        let Commands::Agent {
            command: AgentCommands::Task { stream, .. },
        } = parsed.command
        else {
            panic!("expected Commands::Agent(Task)");
        };
        assert!(stream, "--stream must opt into the full event stream");
    }

    /// Wave 3C (G9) fixture: an artifact event payload for stream tests.
    fn stream_artifact_fixture() -> maho_types::artifact::ArtifactInfo {
        maho_types::artifact::ArtifactInfo {
            artifact_id: "00000000-0000-0000-0000-000000000001".to_string(),
            session_id: "task-1".to_string(),
            display_name: "report.txt".to_string(),
            mime_type: "text/plain".to_string(),
            size_bytes: 12,
            storage_rel_path: "report.txt".to_string(),
            created_at_ms: 0,
            kind: None,
        }
    }

    /// Wave 3C (G9): the default (progress-only) stderr view renders
    /// tool-call started/finished and status ticks, and suppresses the
    /// full-stream events (thinking, artifacts, proof/reason, tokens).
    #[test]
    fn agent_stream_progress_view_renders_tools_and_status_only() {
        let tool_call = AgentStreamEvent::ToolCall {
            id: "t1".to_string(),
            name: "fs_write".to_string(),
            // Arguments must never surface on the stderr stream.
            args: "{\"content\":\"secret\"}".to_string(),
        };
        assert_eq!(
            format_agent_stream_event(&tool_call, false).as_deref(),
            Some("[tool] fs_write started")
        );

        let ok = AgentStreamEvent::ToolResult {
            id: "t1".to_string(),
            name: "fs_write".to_string(),
            result: "{}".to_string(),
            succeeded: true,
        };
        assert_eq!(
            format_agent_stream_event(&ok, false).as_deref(),
            Some("[tool] fs_write finished (ok)")
        );

        let failed = AgentStreamEvent::ToolResult {
            id: "t2".to_string(),
            name: "web_navigate".to_string(),
            result: "boom".to_string(),
            succeeded: false,
        };
        assert_eq!(
            format_agent_stream_event(&failed, false).as_deref(),
            Some("[tool] web_navigate finished (failed)")
        );

        let status = AgentStreamEvent::Status {
            elapsed_secs: 30,
            message: "Turn in progress: 30s elapsed".to_string(),
        };
        assert_eq!(
            format_agent_stream_event(&status, false).as_deref(),
            Some("[status +30s] Turn in progress: 30s elapsed")
        );

        for event in [
            AgentStreamEvent::Token("hi ".to_string()),
            AgentStreamEvent::Thinking("reasoning chunk".to_string()),
            AgentStreamEvent::ArtifactCreated {
                artifact: stream_artifact_fixture(),
            },
            AgentStreamEvent::ProofOrReason {
                proof_locator: Some("browser_screenshot_element".to_string()),
                reason: None,
            },
        ] {
            assert!(
                format_agent_stream_event(&event, false).is_none(),
                "progress-only view must suppress {event:?}"
            );
        }
    }

    /// Wave 3C (G9): the --stream view adds thinking, artifacts, and the
    /// terminal proof/reason lines; tokens stay raw-streamed by the sink and
    /// never format to a line.
    #[test]
    fn agent_stream_full_view_adds_thinking_artifacts_and_proof() {
        assert_eq!(
            format_agent_stream_event(
                &AgentStreamEvent::Thinking("reasoning chunk".to_string()),
                true
            )
            .as_deref(),
            Some("[thinking] reasoning chunk")
        );

        let artifact_event = AgentStreamEvent::ArtifactCreated {
            artifact: stream_artifact_fixture(),
        };
        assert_eq!(
            format_agent_stream_event(&artifact_event, true).as_deref(),
            Some("[artifact] 00000000-0000-0000-0000-000000000001 (report.txt)")
        );

        assert_eq!(
            format_agent_stream_event(
                &AgentStreamEvent::ProofOrReason {
                    proof_locator: Some("browser_screenshot_element".to_string()),
                    reason: None,
                },
                true
            )
            .as_deref(),
            Some("[proof] browser_screenshot_element")
        );

        assert_eq!(
            format_agent_stream_event(
                &AgentStreamEvent::ProofOrReason {
                    proof_locator: None,
                    reason: Some("all done".to_string()),
                },
                true
            )
            .as_deref(),
            Some("[reason] all done")
        );

        assert!(
            format_agent_stream_event(&AgentStreamEvent::Token("hi".to_string()), true).is_none(),
            "tokens stream raw through the sink, never through the formatter"
        );
    }

    /// SC9: `--json` output shape is pinned to exactly session_id,
    /// runtime_config, response — the streaming work must not leak stream
    /// fields into the final-only JSON projection.
    #[test]
    fn agent_task_json_output_shape_stays_final_only() {
        let config = AgentTaskRuntimeConfig::default();
        let out = AgentTaskOutput {
            session_id: "task-abc",
            runtime_config: &config,
            response: "final answer",
        };
        let value = serde_json::to_value(&out).expect("AgentTaskOutput must serialize");
        let object = value
            .as_object()
            .expect("projection must serialize to a JSON object");
        let mut keys: Vec<_> = object.keys().map(String::as_str).collect();
        keys.sort_unstable();
        assert_eq!(keys, vec!["response", "runtime_config", "session_id"]);
        assert_eq!(object["session_id"], "task-abc");
        assert_eq!(object["response"], "final answer");
        assert_eq!(
            object["runtime_config"],
            serde_json::json!({
                "permission_tier": "guard",
                "final_confirm": true,
                "proactive_mode": false,
                "composite_enabled": false,
            })
        );
    }

    /// Plan row 13: an unknown tier value must be rejected by clap (arg_enum),
    /// not silently normalized — normalization is the broker/FFI concern.
    #[test]
    fn agent_task_rejects_unknown_tier_value() {
        use clap::Parser as _;
        let error = Cli::try_parse_from(["maho", "agent", "task", "do it", "--tier", "bogus"])
            .err()
            .expect("unknown tier value must fail parsing");
        assert_eq!(error.kind(), clap::error::ErrorKind::InvalidValue);
        assert!(error.to_string().contains("bogus"));
    }

    /// Plan row 13: `maho agent task --help` renders a usage line listing the
    /// runtime-config flags.
    #[test]
    fn agent_task_help_renders_usage_line() {
        use clap::Parser as _;
        let error = Cli::try_parse_from(["maho", "agent", "task", "--help"])
            .err()
            .expect("--help exits through DisplayHelp");
        assert_eq!(error.kind(), clap::error::ErrorKind::DisplayHelp);
        let help = error.to_string();
        assert!(
            help.contains("Usage:"),
            "help must render a usage line\n{help}"
        );
        assert!(
            help.contains("agent task"),
            "usage must name the subcommand\n{help}"
        );
        assert!(help.contains("--tier"), "help must list --tier\n{help}");
    }

    /// Plan row 13: dry session creation through the shared-workspace store
    /// path — no LLM call, no browser; the session must construct and report
    /// its platform tool surface, and the runtime_config payload round-trips.
    #[tokio::test]
    async fn agent_task_creates_session_dry_with_runtime_config() {
        let dir = tempfile::tempdir().expect("temp dir");
        let db_path = dir.path().join("maho.db");
        seed_fixed_storage_key(dir.path());
        let config = AgentTaskRuntimeConfig {
            permission_tier: "read_only".to_string(),
            final_confirm: false,
            proactive_mode: true,
            ..AgentTaskRuntimeConfig::default()
        };
        let backend = super::build_agent_task_session(
            &db_path,
            None,
            &config,
            false,
            super::TaskSessionTarget::New {
                title: super::agent_task_title("do the thing"),
            },
            &[],
        )
        .await
        .expect("dry agent task session must be created via the workspace store path")
        .1;
        let tools = maho_agent::AgentRuntime::list_tools(&backend)
            .await
            .expect("session must expose its platform tool surface");
        assert!(!tools.is_empty(), "platform tool surface must be non-empty");

        // The proactive runtime flag gates the row-9 instruction block through
        // the same composition the session build applies.
        let prompt = super::agent_task_system_prompt(&config);
        assert!(prompt.contains(maho_agent::system_prompt::PROACTIVITY_INSTRUCTION_SENTINEL));
        let quiet = super::agent_task_system_prompt(&AgentTaskRuntimeConfig::default());
        assert!(!quiet.contains(maho_agent::system_prompt::PROACTIVITY_INSTRUCTION_SENTINEL));
    }

    /// Wave 1D (D7): the session tier and the workspace-derived fs whitelist
    /// roots are bound into the kernel dispatch gate, and the artifact root
    /// defaults under the workspace so fs_write's fs_path resolves inside the
    /// whitelist. Roots must come from the shared provisioning function
    /// (R-N2), never an ad-hoc list.
    #[tokio::test]
    async fn agent_task_session_binds_tier_and_workspace_fs_roots() {
        let dir = tempfile::tempdir().expect("temp dir");
        let db_path = dir.path().join("maho.db");
        seed_fixed_storage_key(dir.path());
        let config = AgentTaskRuntimeConfig {
            permission_tier: "read_only".to_string(),
            ..AgentTaskRuntimeConfig::default()
        };
        let backend = super::build_agent_task_session(
            &db_path,
            None,
            &config,
            false,
            super::TaskSessionTarget::New {
                title: "tier wiring".to_string(),
            },
            &[],
        )
        .await
        .expect("dry agent task session must be created")
        .1;

        // The --tier value rides into the dispatch gate.
        assert_eq!(backend.runtime_tier().as_deref(), Some("read_only"));

        // Whitelist roots are exactly the shared provisioning function's
        // output for the session workspace (workspace_root None here → the
        // current-directory fallback, the same value both sides compute).
        let workspace_root =
            std::env::current_dir().expect("test process must have a working directory");
        assert_eq!(
            backend.fs_whitelist_roots(),
            maho_agent::permission::default_fs_whitelist_roots(&workspace_root)
        );

        // The default artifact root sits under the workspace, so a tier-gated
        // fs_write resolves its fs_path inside the whitelist.
        assert_eq!(
            backend.artifact_root(),
            Some(workspace_root.join(".maho").join("artifacts"))
        );
    }

    /// Wave 3A (G8): session open binds the composite gate ONCE from the
    /// runtime-config payload. `--composite` arms the batch runner with the
    /// shared step cap; the default (no flag) stays fail-closed — the bound
    /// gate refuses every batch before any executor is touched.
    #[tokio::test]
    async fn agent_task_session_binds_composite_gate_fail_closed() {
        let dir = tempfile::tempdir().expect("temp dir");
        let db_path = dir.path().join("maho.db");
        seed_fixed_storage_key(dir.path());

        // Default posture: no --composite → the gate binds disabled.
        let (_, backend) = super::build_agent_task_session(
            &db_path,
            None,
            &AgentTaskRuntimeConfig::default(),
            false,
            super::TaskSessionTarget::New {
                title: "composite default".to_string(),
            },
            &[],
        )
        .await
        .expect("default dry session must be created");
        let bound = backend.composite_config();
        assert!(
            !bound.enabled,
            "fail-closed: no flag must leave the gate disabled"
        );
        assert_eq!(
            bound.max_steps,
            maho_agent::composite::DEFAULT_MAX_STEPS,
            "the shared step cap rides the default config"
        );
        drop(backend);

        // --composite posture: the gate binds enabled, still bounded by the
        // shared step cap.
        let config = AgentTaskRuntimeConfig {
            composite_enabled: true,
            ..AgentTaskRuntimeConfig::default()
        };
        let (_, backend) = super::build_agent_task_session(
            &db_path,
            None,
            &config,
            false,
            super::TaskSessionTarget::New {
                title: "composite armed".to_string(),
            },
            &[],
        )
        .await
        .expect("composite dry session must be created");
        let bound = backend.composite_config();
        assert!(
            bound.enabled,
            "--composite must arm the gate at session open"
        );
        assert_eq!(bound.max_steps, maho_agent::composite::DEFAULT_MAX_STEPS);
    }

    /// Wave 1C (G10): the title is the goal's first ~80 chars — long goals
    /// are cut at a char boundary, short goals pass through untouched, and
    /// multi-byte goals never panic mid-codepoint.
    #[test]
    fn agent_task_title_truncates_long_goal_to_eighty_chars() {
        let goal = "x".repeat(200);
        let title = super::agent_task_title(&goal);
        assert_eq!(title.chars().count(), 80);
        assert!(goal.starts_with(&title));

        let multibyte = "\u{1f52d}".repeat(100);
        let title = super::agent_task_title(&multibyte);
        assert_eq!(title.chars().count(), 80);
        assert!(title.chars().all(|c| c == '\u{1f52d}'));
    }

    /// Wave 1C (G10): short goals become the title verbatim; embedded
    /// newlines/tabs collapse so the panel history row stays single-line.
    #[test]
    fn agent_task_title_keeps_short_goal_and_collapses_whitespace() {
        assert_eq!(
            super::agent_task_title("Fix the login bug"),
            "Fix the login bug"
        );
        assert_eq!(
            super::agent_task_title("  multi\nline \t goal  "),
            "multi line goal"
        );
    }

    /// Wave 1C (G10): a fresh task session's conversation row is titled from
    /// the goal up front, so the row is titled even before the first turn
    /// persists (pre-existing untitled rows are NOT migrated — no backfill).
    #[tokio::test]
    async fn agent_task_new_session_is_titled_from_goal() {
        let dir = tempfile::tempdir().expect("temp dir");
        let db_path = dir.path().join("maho.db");
        seed_fixed_storage_key(dir.path());
        let goal = "Summarize the quarterly compliance report and draft follow-ups";
        let (session_id, backend) = super::build_agent_task_session(
            &db_path,
            None,
            &AgentTaskRuntimeConfig::default(),
            false,
            super::TaskSessionTarget::New {
                title: super::agent_task_title(goal),
            },
            &[],
        )
        .await
        .expect("new task session must be created");
        assert!(
            session_id.starts_with("task-"),
            "fresh session id must be task-<uuid>, got {session_id}"
        );
        drop(backend);

        let storage = maho_cli::workspace::open_storage(&db_path).expect("reopen store");
        let conversations = storage
            .list_conversations(maho_types::chat::ConversationListState::All, 100)
            .expect("list conversations");
        let created = conversations
            .iter()
            .find(|c| c.id == session_id)
            .expect("titled session row must exist");
        assert_eq!(created.title.as_deref(), Some(goal));
    }

    /// Wave 1C (G10): `--resume <session-id>` rebinds to the existing
    /// conversation — same id, stored turns and title reused, and an unknown
    /// id fails closed instead of silently forking a fresh session.
    #[tokio::test]
    async fn agent_task_resume_reuses_existing_session() {
        let dir = tempfile::tempdir().expect("temp dir");
        let db_path = dir.path().join("maho.db");
        seed_fixed_storage_key(dir.path());

        // Seed a prior run: a titled session with one stored turn, exactly
        // what a previous `maho agent task` leaves behind.
        let (session_id, backend) = super::build_agent_task_session(
            &db_path,
            None,
            &AgentTaskRuntimeConfig::default(),
            false,
            super::TaskSessionTarget::New {
                title: super::agent_task_title("first run goal"),
            },
            &[],
        )
        .await
        .expect("initial session must be created");
        drop(backend);
        {
            let storage = maho_cli::workspace::open_storage(&db_path).expect("open store");
            storage
                .insert_conversation_turn("turn-1", &session_id, "user", "first run goal", None)
                .expect("seed prior turn");
        }

        // Resume must resolve to the SAME session, keeping its turns and
        // title (no re-titling on resume).
        let (resumed_id, backend) = super::build_agent_task_session(
            &db_path,
            None,
            &AgentTaskRuntimeConfig::default(),
            false,
            super::TaskSessionTarget::Resume {
                session_id: session_id.clone(),
            },
            &[],
        )
        .await
        .expect("resume must rebind to the existing session");
        assert_eq!(resumed_id, session_id, "resume must reuse the session id");
        drop(backend);

        let storage = maho_cli::workspace::open_storage(&db_path).expect("reopen store");
        let turns = storage
            .load_conversation_session(&resumed_id)
            .expect("load resumed session turns");
        assert_eq!(turns.len(), 1, "resumed session must keep prior turns");
        assert_eq!(turns[0].1, "first run goal");
        let conversations = storage
            .list_conversations(maho_types::chat::ConversationListState::All, 100)
            .expect("list conversations");
        let resumed = conversations
            .iter()
            .find(|c| c.id == resumed_id)
            .expect("resumed session row must exist");
        assert_eq!(resumed.title.as_deref(), Some("first run goal"));

        // Unknown resume id: fail closed, never fork a fresh session.
        let error = super::build_agent_task_session(
            &db_path,
            None,
            &AgentTaskRuntimeConfig::default(),
            false,
            super::TaskSessionTarget::Resume {
                session_id: "task-00000000-0000-0000-0000-000000000000".to_string(),
            },
            &[],
        )
        .await
        .err()
        .expect("resume of an unknown session must fail closed");
        assert!(
            error.to_string().contains("not found"),
            "error must name the missing session: {error}"
        );
    }

    /// Wave 1C (G10): `--resume <SESSION_ID>` parses onto the task command;
    /// without the flag it stays None.
    #[test]
    fn agent_task_parses_resume_flag() {
        use clap::Parser as _;
        let parsed = Cli::try_parse_from([
            "maho",
            "agent",
            "task",
            "continue where we left off",
            "--resume",
            "task-abc",
        ])
        .expect("--resume must parse");
        let Commands::Agent {
            command: AgentCommands::Task { resume, .. },
        } = parsed.command
        else {
            panic!("expected Commands::Agent(Task)");
        };
        assert_eq!(resume.as_deref(), Some("task-abc"));

        let parsed = Cli::try_parse_from(["maho", "agent", "task", "fresh start"])
            .expect("plain task args must parse");
        let Commands::Agent {
            command: AgentCommands::Task { resume, .. },
        } = parsed.command
        else {
            panic!("expected Commands::Agent(Task)");
        };
        assert_eq!(resume, None);
    }

    /// Pre-seed a FIXED storage key file for a test profile.
    ///
    /// `workspace::open_storage` derives the SQLCipher key from
    /// `<db dir>/maho_storage.key`, generating a RANDOM key file when absent
    /// and injecting it into a process-global slot before every open. When
    /// storage-opening tests run in parallel threads, one thread's key set
    /// can land between another thread's set and open, so a DB gets opened
    /// under the wrong key ("file is not a database" flakes). Seeding the
    /// same fixed key content in every storage-using test makes every
    /// derivation — and therefore the global slot — identical, so
    /// interleavings are harmless.
    fn seed_fixed_storage_key(dir: &std::path::Path) {
        std::fs::write(
            dir.join("maho_storage.key"),
            b"maho-cli-agent-task-test-fixed-key-01",
        )
        .expect("seed fixed storage key file");
    }

    /// clap must give registered platform subcommands precedence over the
    /// agent-first prompt fallback (`maho <prompt>`). Regression: these used to
    /// be swallowed as the prompt, launching the agent instead of dispatching.
    #[test]
    fn platform_subcommands_take_precedence_over_prompt_positional() {
        use clap::Parser as _;
        for args in [
            vec!["maho", "disk", "status"],
            vec!["maho", "contacts", "search", "Indo"],
            vec!["maho", "imessage", "chats"],
            vec!["maho", "desktop", "status"],
            vec!["maho", "desktop", "capture"],
            vec!["maho", "desktop", "ocr"],
            vec!["maho", "desktop", "elements"],
            vec!["maho", "desktop", "find-text", "--text", "hello"],
        ] {
            let parsed = Cli::try_parse_from(&args);
            let command = parsed.expect("must parse").command;
            assert!(
                matches!(
                    command,
                    Commands::Disk { .. }
                        | Commands::Contacts { .. }
                        | Commands::Imessage { .. }
                        | Commands::Desktop { .. }
                ),
                "args {args:?} must match a platform subcommand"
            );
        }
    }

    #[test]
    fn mail_default_argv_omits_optional_wire_arguments() {
        use clap::Parser as _;
        for (argv, expected_tool, expected_args) in [
            (
                vec![
                    "maho",
                    "mail",
                    "list",
                    "--account-id",
                    "acct",
                    "--folder-id",
                    "inbox",
                ],
                "mail_list_emails",
                serde_json::json!({"account_id": "acct", "folder_id": "inbox"}),
            ),
            (
                vec!["maho", "mail", "search", "receipt"],
                "mail_search_emails",
                serde_json::json!({"query": "receipt"}),
            ),
        ] {
            let Commands::Mail { command } = Cli::try_parse_from(argv).expect("Mail argv").command
            else {
                panic!("expected Mail command");
            };
            let (tool, args) = mail_tool_request(command);
            let wire = serde_json::to_string(&args).expect("request JSON");
            let parsed: serde_json::Value = serde_json::from_str(&wire).expect("one request parse");
            assert_eq!(tool, expected_tool);
            assert_eq!(parsed, expected_args);
        }
    }

    #[test]
    fn mail_thread_command_sends_account_id_and_message_id() {
        let (tool, args) = mail_tool_request(MailCommands::Thread {
            account_id: "acc1".to_string(),
            message_id: "msg1".to_string(),
        });

        assert_eq!(tool, "mail_list_thread");
        assert_eq!(args["account_id"], "acc1");
        assert_eq!(args["message_id"], "msg1");
        assert!(
            args.get("thread_id").is_none(),
            "must not send the stale thread_id key the browser MCP rejects"
        );
    }

    // The three `unsupported_routine_run_flags` tests that lived here pinned
    // a runtime rejection helper for flags that were registered but could
    // never work. The flags are now unregistered, so clap rejects them at
    // parse time (same exit code) and the helper no longer exists; the
    // replacement coverage is
    // `routine_run_rejects_unimplementable_flags_at_parse_time` below.

    #[test]
    fn mail_search_command_routes_to_mail_search_emails() {
        let (tool, args) = mail_tool_request(MailCommands::Search {
            query: "receipt".to_string(),
            account_id: Some("acc1".to_string()),
            folder_id: Some("fold1".to_string()),
            limit: Some(10),
            offset: Some(5),
        });

        assert_eq!(tool, "mail_search_emails");
        assert_eq!(args["query"], "receipt");
        assert_eq!(args["account_id"], "acc1");
        assert_eq!(args["folder_id"], "fold1");
        assert_eq!(args["limit"], 10);
        assert_eq!(args["offset"], 5);
    }

    #[test]
    fn maho_app_bundle_path_resolves_to_executable() {
        let temp = tempfile::tempdir().expect("temporary app directory must exist");
        let app = temp.path().join("Maho.app");
        let executable = app.join("Contents/MacOS/Maho");
        std::fs::create_dir_all(
            executable
                .parent()
                .expect("bundle executable must have a parent"),
        )
        .expect("bundle executable directory must exist");
        std::fs::write(&executable, b"fixture").expect("bundle executable fixture must exist");

        assert_eq!(crate::resolve_maho_binary_path(&app), Some(executable));
    }

    #[test]
    fn direct_maho_executable_path_is_preserved() {
        let temp = tempfile::tempdir().expect("temporary binary directory must exist");
        let executable = temp.path().join("Maho");
        std::fs::write(&executable, b"fixture").expect("binary fixture must exist");

        assert_eq!(
            crate::resolve_maho_binary_path(&executable),
            Some(executable.clone())
        );
    }

    // ─── P0-1: per-session model / provider override ──────────────────────

    /// `-m/--model` and `-p/--provider` carry a per-session routing override
    /// onto the task session (backend `set_available_models` /
    /// `set_preferred_provider`). Both spellings must parse.
    #[test]
    fn agent_task_parses_model_and_provider_overrides() {
        use clap::Parser as _;
        for argv in [
            [
                "maho",
                "agent",
                "task",
                "do it",
                "--model",
                "anthropic/claude-3-7-sonnet",
                "--provider",
                "anthropic",
            ],
            [
                "maho",
                "agent",
                "task",
                "do it",
                "-m",
                "anthropic/claude-3-7-sonnet",
                "-p",
                "anthropic",
            ],
        ] {
            let parsed = Cli::try_parse_from(argv).expect("model/provider overrides must parse");
            let Commands::Agent {
                command:
                    AgentCommands::Task {
                        model, provider, ..
                    },
            } = parsed.command
            else {
                panic!("expected Commands::Agent(Task)");
            };
            assert_eq!(model.as_deref(), Some("anthropic/claude-3-7-sonnet"));
            assert_eq!(provider.as_deref(), Some("anthropic"));
        }
    }

    /// Absent flags leave routing untouched: the stored profile's
    /// `preferred_model` and the unselected-provider fall-through stay in
    /// force, so the payload carries `None` rather than a fabricated default.
    #[test]
    fn agent_task_model_and_provider_default_to_none() {
        use clap::Parser as _;
        let parsed = Cli::try_parse_from(["maho", "agent", "task", "do it"])
            .expect("bare agent task must parse");
        let Commands::Agent {
            command: AgentCommands::Task {
                model, provider, ..
            },
        } = parsed.command
        else {
            panic!("expected Commands::Agent(Task)");
        };
        assert!(model.is_none());
        assert!(provider.is_none());
    }

    /// The overrides ride the runtime_config payload so `--json` round-trips
    /// the model/provider actually bound to the session.
    #[test]
    fn runtime_config_carries_model_and_provider_overrides() {
        let config = AgentTaskRuntimeConfig::from_parts(
            TierArg::Guard,
            true,
            false,
            false,
            false,
            false,
            Some("anthropic/claude-3-7-sonnet".to_string()),
            Some("Anthropic ".to_string()),
        );
        assert_eq!(config.model.as_deref(), Some("anthropic/claude-3-7-sonnet"));
        // Provider is normalized (trimmed + lowercased) so `-p Anthropic`
        // matches the backend's `normalize_provider` keying.
        assert_eq!(config.provider.as_deref(), Some("anthropic"));

        let bare = AgentTaskRuntimeConfig::from_parts(
            TierArg::Guard,
            true,
            false,
            false,
            false,
            false,
            None,
            None,
        );
        assert_eq!(bare, AgentTaskRuntimeConfig::default());
        assert!(bare.model.is_none());
        assert!(bare.provider.is_none());
    }

    /// An empty/whitespace-only override is a user error, not a silent
    /// no-op: it must not reach the backend as an empty preference.
    #[test]
    fn blank_model_and_provider_overrides_are_dropped() {
        let config = AgentTaskRuntimeConfig::from_parts(
            TierArg::Guard,
            true,
            false,
            false,
            false,
            false,
            Some("   ".to_string()),
            Some("".to_string()),
        );
        assert!(config.model.is_none());
        assert!(config.provider.is_none());
    }

    // ─── P0-2: session listing ────────────────────────────────────────────

    /// `maho agent sessions` makes `--resume <SESSION_ID>` discoverable.
    #[test]
    fn agent_sessions_subcommand_parses_with_defaults() {
        use clap::Parser as _;
        let parsed =
            Cli::try_parse_from(["maho", "agent", "sessions"]).expect("agent sessions must parse");
        let Commands::Agent {
            command: AgentCommands::Sessions { limit, all },
        } = parsed.command
        else {
            panic!("expected Commands::Agent(Sessions)");
        };
        assert_eq!(limit, 20);
        assert!(!all, "listing defaults to CLI task sessions only");

        let parsed = Cli::try_parse_from(["maho", "agent", "sessions", "--limit", "5", "--all"])
            .expect("agent sessions flags must parse");
        let Commands::Agent {
            command: AgentCommands::Sessions { limit, all },
        } = parsed.command
        else {
            panic!("expected Commands::Agent(Sessions)");
        };
        assert_eq!(limit, 5);
        assert!(all);
    }

    /// Default listing shows exactly the sessions `agent task` mints (the
    /// `task-` prefix), so every row is a valid `--resume` argument; `--all`
    /// widens to browser-side conversations too.
    #[test]
    fn agent_sessions_filters_to_resumable_task_sessions() {
        let ids = ["task-abc", "chat-7", "task-def"];
        let resumable: Vec<&str> = ids
            .iter()
            .copied()
            .filter(|id| crate::is_cli_task_session(id))
            .collect();
        assert_eq!(resumable, vec!["task-abc", "task-def"]);
    }

    // ─── P1-5: fs whitelist roots ─────────────────────────────────────────

    /// `--dir` overrides the implicit single workspace root the agent would
    /// otherwise whitelist, and repeats to build a multi-root whitelist.
    #[test]
    fn agent_task_parses_repeated_dir_overrides() {
        use clap::Parser as _;
        let parsed = Cli::try_parse_from([
            "maho", "agent", "task", "refactor", "--dir", "/tmp/a", "--dir", "/tmp/b",
        ])
        .expect("repeated --dir must parse");
        let Commands::Agent {
            command: AgentCommands::Task { dirs, .. },
        } = parsed.command
        else {
            panic!("expected Commands::Agent(Task)");
        };
        assert_eq!(
            dirs,
            vec![
                std::path::PathBuf::from("/tmp/a"),
                std::path::PathBuf::from("/tmp/b")
            ]
        );
    }

    /// Explicit `--dir` roots replace the derived workspace root; with none
    /// passed the derived root is used unchanged. Roots that
    /// `default_fs_whitelist_roots` would silently drop (relative paths)
    /// fail loudly instead of handing the agent an empty, unexplained
    /// whitelist.
    #[test]
    fn fs_whitelist_roots_honor_explicit_dirs() {
        let workspace_root = std::path::Path::new("/tmp/workspace");

        let derived = crate::resolve_fs_whitelist_roots(workspace_root, &[])
            .expect("derived root must resolve");
        assert_eq!(derived, vec!["/tmp/workspace".to_string()]);

        let overridden = crate::resolve_fs_whitelist_roots(
            workspace_root,
            &[
                std::path::PathBuf::from("/tmp/a"),
                std::path::PathBuf::from("/tmp/b/"),
            ],
        )
        .expect("explicit roots must resolve");
        assert_eq!(overridden, vec!["/tmp/a".to_string(), "/tmp/b".to_string()]);

        let err = crate::resolve_fs_whitelist_roots(
            workspace_root,
            &[std::path::PathBuf::from("relative/dir")],
        )
        .expect_err("a relative --dir must fail closed with an explanation");
        assert!(
            err.to_string().contains("relative/dir"),
            "error must name the rejected root, got: {err}"
        );
    }

    // ─── P0-3 / P1-1 / P1-2 / P1-4: removed no-op surface ─────────────────

    /// P0-3: `--isolated` never had a consumer — every handler dropped it.
    /// A hard parse error beats silently ignoring a security-adjacent flag.
    #[test]
    fn isolated_global_flag_is_rejected() {
        use clap::Parser as _;
        assert!(
            Cli::try_parse_from(["maho", "--isolated", "version"]).is_err(),
            "--isolated must not parse: nothing consumes it"
        );
    }

    /// P1-1: `history import` had a hard-exit stub handler; it must no
    /// longer be advertised as a peer of `list`/`search`/`export`.
    #[test]
    fn history_import_is_not_registered() {
        use clap::Parser as _;
        assert!(
            Cli::try_parse_from(["maho", "history", "import", "./h.ndjson"]).is_err(),
            "history import must not parse: the browser exposes no import tool"
        );
        assert!(
            Cli::try_parse_from(["maho", "history", "export"]).is_ok(),
            "the remaining history verbs must keep working"
        );
    }

    /// P1-2: `browser pipe --jsonl` was parsed then discarded — NDJSON
    /// framing is unconditional, so the flag promised a switch that did not
    /// exist.
    #[test]
    fn browser_pipe_jsonl_flag_is_rejected() {
        use clap::Parser as _;
        assert!(
            Cli::try_parse_from(["maho", "browser", "pipe", "--jsonl"]).is_err(),
            "--jsonl must not parse: NDJSON framing is always on"
        );
        assert!(
            Cli::try_parse_from(["maho", "browser", "pipe"]).is_ok(),
            "browser pipe itself must keep working"
        );
    }

    /// P1-4: `routine run --provider/--model/--workspace` were registered
    /// then rejected with exit 2. Clap's own unknown-argument error keeps
    /// the exit code while removing them from help.
    #[test]
    fn routine_run_rejects_unimplementable_flags_at_parse_time() {
        use clap::Parser as _;
        for flag in ["--provider", "--model", "--workspace"] {
            assert!(
                Cli::try_parse_from(["maho", "routine", "run", "r1", flag, "x"]).is_err(),
                "routine run {flag} must not parse: routines execute browser-side"
            );
        }
        assert!(
            Cli::try_parse_from(["maho", "routine", "run", "r1"]).is_ok(),
            "routine run itself must keep working"
        );
    }

    // ─── P1-3: remediation advice must name real commands ─────────────────

    /// Every `maho <sub>` spelling quoted in *runtime user-facing text* must
    /// resolve to a visible command. The three offenders named `maho repl`,
    /// `maho login`, and `maho balance` — one nonexistent, two hidden
    /// deprecated aliases the CLI simultaneously warns it is removing — so
    /// copy-pasting the CLI's own advice failed.
    ///
    /// Scans only lines that carry a string literal, since comments
    /// legitimately discuss removed commands (e.g. "the removed `maho run`
    /// path") as history rather than as advice.
    #[test]
    fn quoted_remediation_commands_resolve_to_visible_commands() {
        use clap::CommandFactory as _;
        let source = include_str!("main.rs");
        let command = Cli::command();
        let visible: Vec<String> = command
            .get_subcommands()
            .filter(|sub| !sub.is_hide_set())
            .map(|sub| sub.get_name().to_string())
            .collect();

        let mut offenders = Vec::new();
        for line in source.lines() {
            let trimmed = line.trim_start();
            // Comments and doc comments are not user-facing output.
            if trimmed.starts_with("//") || !line.contains('"') {
                continue;
            }
            for (index, _) in line.match_indices("`maho ") {
                let rest = &line[index + "`maho ".len()..];
                let Some(end) = rest.find('`') else { continue };
                let quoted = &rest[..end];
                // Only single-word invocations can be checked mechanically;
                // multi-word ones name a group + subcommand already covered by
                // the group's own registration.
                let Some(word) = quoted.split_whitespace().next() else {
                    continue;
                };
                if quoted.split_whitespace().count() != 1 {
                    continue;
                }
                // Only concrete command names are checkable: skip flags, format
                // placeholders (`{old}`), and doc metasyntax (`<cmd>`).
                let is_concrete_name = word
                    .chars()
                    .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-')
                    && !word.starts_with('-');
                if !is_concrete_name {
                    continue;
                }
                if !visible.iter().any(|name| name == word) {
                    offenders.push(word.to_string());
                }
            }
        }
        offenders.sort();
        offenders.dedup();
        assert!(
            offenders.is_empty(),
            "user-facing text quotes `maho <cmd>` spellings that are hidden or nonexistent: {offenders:?}"
        );
    }
}

fn validate_tool_discovery(result: &serde_json::Value) -> Result<()> {
    let diagnostics = result
        .get("catalogDiagnostics")
        .ok_or_else(|| anyhow::anyhow!("browser discovery omitted catalog diagnostics"))?;
    for field in [
        "catalogVersion",
        "schemaVersion",
        "resultVersion",
        "canonicalCount",
    ] {
        if diagnostics
            .get(field)
            .and_then(serde_json::Value::as_u64)
            .unwrap_or(0)
            == 0
        {
            bail!("browser catalog diagnostics have invalid {field}");
        }
    }
    let tools = result
        .get("tools")
        .and_then(serde_json::Value::as_array)
        .ok_or_else(|| anyhow::anyhow!("browser discovery omitted tools"))?;
    let expected = diagnostics
        .pointer("/surfaces/publicMcp/count")
        .and_then(serde_json::Value::as_u64)
        .ok_or_else(|| anyhow::anyhow!("browser diagnostics omitted public MCP count"))?;
    if expected != tools.len() as u64 {
        bail!("browser catalog diagnostics do not match tools/list");
    }
    Ok(())
}

fn find_tool_by_id<'a>(result: &'a serde_json::Value, id: &str) -> Result<&'a serde_json::Value> {
    validate_tool_discovery(result)?;
    result["tools"]
        .as_array()
        .and_then(|tools| {
            tools.iter().find(|tool| {
                tool.get("capabilityId").and_then(serde_json::Value::as_str) == Some(id)
            })
        })
        .ok_or_else(|| anyhow::anyhow!("unknown or unavailable capability '{id}'"))
}

/// C4-DEFECT-1: the global `--json` flag promises structured output for all
/// commands, but the capability-resolution failures below returned a plain
/// `Err` that anyhow rendered as a human sentence on stderr, leaving stdout
/// empty. A machine consumer reading stdout got nothing parseable and no
/// stable reason code.
///
/// Under `--json` this emits the structured object on stdout and exits with
/// the caller's code; otherwise it returns the error unchanged so the human
/// path (sentence on stderr, empty stdout) is byte-identical to before.
///
/// `reason` is a stable machine token, deliberately distinct from the
/// `error.status` key used by the pre-existing runtime-unavailable payload
/// on stderr — that contract is pinned by tests and is left untouched.
fn emit_json_error_and_exit(
    json_output: bool,
    reason: &str,
    error: anyhow::Error,
) -> anyhow::Error {
    if !json_output {
        return error;
    }
    let payload = serde_json::json!({
        "ok": false,
        "error": {
            "reason": reason,
            "message": error.to_string(),
        }
    });
    match serde_json::to_string_pretty(&payload) {
        Ok(rendered) => println!("{rendered}"),
        // Serializing a two-string object cannot realistically fail, but if
        // it ever did, falling back to the human path beats exiting silent.
        Err(_) => return error,
    }
    std::process::exit(1);
}

fn packaged_tool_catalog() -> Result<serde_json::Value> {
    let catalog: serde_json::Value =
        serde_json::from_str(include_str!("tool_catalog_snapshot.json"))
            .context("packaged tool catalog snapshot is invalid")?;
    validate_tool_discovery(&catalog)?;
    Ok(catalog)
}

async fn discover_tool_catalog(
    socket_path: Option<&str>,
) -> Result<(serde_json::Value, &'static str, bool)> {
    let client = match browser::connect(socket_path).await {
        Ok(client) => client,
        Err(_) => return Ok((packaged_tool_catalog()?, "packaged_snapshot", false)),
    };
    let discovery = client.call("tools/list", serde_json::json!({})).await?;
    validate_tool_discovery(&discovery)?;
    Ok((discovery, "live_browser", true))
}

async fn cmd_doctor(
    fix: bool,
    socket_path: Option<&str>,
    json_output: bool,
) -> Result<()> {
    doctor::run(fix, socket_path, json_output).await
}

async fn cmd_tool(
    command: ToolCommands,
    socket_path: Option<&str>,
    json_output: bool,
) -> Result<()> {
    match command {
        ToolCommands::List => {
            let (result, source, runtime_available) = discover_tool_catalog(socket_path).await?;
            if json_output {
                let output = serde_json::json!({
                    "source": source,
                    "runtimeAvailable": runtime_available,
                    "catalogDiagnostics": result["catalogDiagnostics"].clone(),
                    "tools": result["tools"].clone(),
                });
                println!("{}", serde_json::to_string_pretty(&output)?);
            } else if let Some(tools) = result.get("tools").and_then(|t| t.as_array()) {
                for tool in tools {
                    let name = tool.get("name").and_then(|v| v.as_str()).unwrap_or("");
                    let desc = tool
                        .get("description")
                        .and_then(|v| v.as_str())
                        .unwrap_or("");
                    let id = tool
                        .get("capabilityId")
                        .and_then(|v| v.as_str())
                        .unwrap_or("");
                    println!("{} [{}]: {}", id, name, desc);
                }
            }
            Ok(())
        }
        ToolCommands::Describe { id } => {
            let (result, source, runtime_available) = discover_tool_catalog(socket_path).await?;
            let tool = find_tool_by_id(&result, &id)
                .map_err(|e| emit_json_error_and_exit(json_output, "unknown_capability", e))?;
            if json_output {
                let output = serde_json::json!({
                    "source": source,
                    "runtimeAvailable": runtime_available,
                    "catalogDiagnostics": result["catalogDiagnostics"].clone(),
                    "tool": tool,
                });
                println!("{}", serde_json::to_string_pretty(&output)?);
            } else {
                println!("{}", serde_json::to_string_pretty(tool)?);
            }
            Ok(())
        }
        ToolCommands::Run { id, args } => {
            // Session-binding tools acquire browser-side state (a lease, an
            // in-flight network capture) that must be released by the same
            // session that created it. A one-shot `maho tool call` exits right
            // after the call, orphaning that state — a leaked lease blocks other
            // clients, an in-flight capture never stops. Reject BEFORE connecting
            // or executing anything, regardless of whether the browser is up.
            const STATEFUL_CAPABILITY_IDS: &[&str] = &[
                "capture.network_start",
                "capture.network_stop",
                "capture.network_get_har",
                "lease.acquire",
                "lease.heartbeat",
                "lease.release",
            ];
            if maho_cli::STATEFUL_TOOLS.iter().any(|&tool| tool == id)
                || STATEFUL_CAPABILITY_IDS.contains(&id.as_str())
            {
                eprintln!(
                    "Error: '{id}' is a session-binding tool and cannot be run as a one-shot \
                     `maho tool call` — its browser-side state (lease / network capture) would be \
                     orphaned when the command exits.\n\
                     Run it inside a persistent session instead: `maho browser repl` holds the \
                     session open so the state can be released."
                );
                std::process::exit(2);
            }

            let client = match browser::connect(socket_path).await {
                Ok(c) => c,
                Err(e) => {
                    if json_output {
                        let error = serde_json::json!({
                            "ok": false,
                            "error": {
                                "status": "runtime_unavailable",
                                "message": format!(
                                    "Failed to connect to browser to run capability '{}': {}",
                                    id, e
                                ),
                                "data": {
                                    "capabilityId": id,
                                    "runtimeAvailable": false,
                                }
                            }
                        });
                        eprintln!("{}", serde_json::to_string_pretty(&error)?);
                        std::process::exit(1);
                    }
                    bail!(
                        "Failed to connect to browser to run capability '{}': {}",
                        id,
                        e
                    );
                }
            };

            let discovery = client.call("tools/list", serde_json::json!({})).await?;
            let descriptor = find_tool_by_id(&discovery, &id)
                .map_err(|e| emit_json_error_and_exit(json_output, "unknown_capability", e))?;
            let name = descriptor
                .get("name")
                .and_then(serde_json::Value::as_str)
                .ok_or_else(|| anyhow::anyhow!("capability '{id}' has no executable tool name"))?;

            let args_val = if let Some(j) = args {
                serde_json::from_str(&j).with_context(|| "Failed to parse arguments as JSON")?
            } else {
                serde_json::json!({})
            };

            let result = browser::send_tool_call(&client, &name, args_val).await?;
            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!("{}", result);
            }
            Ok(())
        }
    }
}

// ─── MCP server entrypoint ──────────────────────────────────────────────────

fn cmd_mcp_server() -> Result<()> {
    let exe = std::env::current_exe().context("could not resolve current executable path")?;
    let dir = exe
        .parent()
        .ok_or_else(|| anyhow::anyhow!("could not resolve executable directory"))?;
    let binary = dir.join("maho-browser-mcp");
    if !binary.exists() {
        bail!(
            "maho-browser-mcp binary not found next to `maho` (looked in {}). \
             Reinstall Maho or build the maho-browser-mcp binary.",
            dir.display()
        );
    }

    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        let err = std::process::Command::new(&binary).exec();
        Err(anyhow::anyhow!("failed to exec maho-browser-mcp: {err}"))
    }

    #[cfg(not(unix))]
    {
        let status = std::process::Command::new(&binary)
            .status()
            .with_context(|| format!("failed to spawn {}", binary.display()))?;
        std::process::exit(status.code().unwrap_or(1));
    }
}

async fn cmd_mcp(command: Option<McpCommands>, json_output: bool) -> Result<()> {
    match command {
        None => cmd_mcp_server(),
        Some(cmd) => match cmd {
            McpCommands::Add {
                name,
                command,
                url,
                mcp_socket_path,
                trusted,
                workspace,
            } => {
                cmd_mcp_add(
                    &name,
                    command.as_deref(),
                    url.as_deref(),
                    mcp_socket_path.as_deref(),
                    trusted,
                    workspace.as_deref(),
                    json_output,
                )
                .await
            }
            McpCommands::List { workspace } => {
                cmd_mcp_list(workspace.as_deref(), json_output).await
            }
            McpCommands::Remove { name, workspace } => {
                cmd_mcp_remove(&name, workspace.as_deref(), json_output).await
            }
        },
    }
}

async fn cmd_mcp_add(
    name: &str,
    command: Option<&str>,
    url: Option<&str>,
    mcp_socket_path: Option<&str>,
    trusted: bool,
    workspace: Option<&str>,
    json_output: bool,
) -> Result<()> {
    let db_path = maho_cli::workspace::default_db_path();
    let resolved_ws_id = maho_cli::workspace::resolve_workspace_id(&db_path, workspace).await?;

    let storage = maho_cli::workspace::open_storage(&db_path)
        .map_err(|e| anyhow::anyhow!("Failed to open database: {}", e))?;

    let transport = if mcp_socket_path.is_some() {
        maho_types::ai::McpTransport::Uds
    } else if url.is_some() {
        maho_types::ai::McpTransport::Http
    } else {
        maho_types::ai::McpTransport::Stdio
    };

    let server = maho_types::ai::AiMcpServer {
        id: uuid::Uuid::new_v4().to_string(),
        workspace_id: resolved_ws_id.clone(),
        name: name.to_string(),
        transport,
        command: command.map(String::from),
        url: url.map(String::from),
        auth_keychain_id: None,
        trusted,
        trusted_tools: None,
        timeout_ms: 30000,
        output_cap_bytes: 10240,
        socket_path: mcp_socket_path.map(String::from),
        created_at: String::new(),
        updated_at: String::new(),
    };

    storage
        .create_mcp_server(&server)
        .map_err(|e| anyhow::anyhow!("Failed to create MCP server: {:?}", e))?;

    // No core-update emit here: the CLI process never registers a core-update
    // callback (`set_core_update_callback`), so `emit_core_update` is a no-op in
    // this process. Cross-process propagation to a running browser happens via
    // the shared SQLite MCP registry (written above) picked up on the browser's
    // next `McpClient::connect`, which emits `ToolAvailabilityChanged` in that
    // process — not via a CLI emit.

    if json_output {
        println!("{}", serde_json::to_string_pretty(&server)?);
    } else {
        println!("Added MCP server '{}' successfully.", name);
    }
    Ok(())
}

async fn cmd_mcp_list(workspace: Option<&str>, json_output: bool) -> Result<()> {
    let db_path = maho_cli::workspace::default_db_path();
    let resolved_ws_id = maho_cli::workspace::resolve_workspace_id(&db_path, workspace).await?;

    let storage = maho_cli::workspace::open_storage(&db_path)
        .map_err(|e| anyhow::anyhow!("Failed to open database: {}", e))?;

    let servers = storage
        .list_mcp_servers(&resolved_ws_id)
        .map_err(|e| anyhow::anyhow!("Failed to list MCP servers: {:?}", e))?;

    if json_output {
        println!("{}", serde_json::to_string_pretty(&servers)?);
    } else {
        println!("Configured MCP servers for workspace '{}':", resolved_ws_id);
        for server in servers {
            let transport_str = match server.transport {
                maho_types::ai::McpTransport::Stdio => "stdio",
                maho_types::ai::McpTransport::Http => "http",
                maho_types::ai::McpTransport::Uds => "uds",
            };
            println!("- {} (transport: {})", server.name, transport_str);
        }
    }
    Ok(())
}

async fn cmd_mcp_remove(name: &str, workspace: Option<&str>, json_output: bool) -> Result<()> {
    let db_path = maho_cli::workspace::default_db_path();
    let resolved_ws_id = maho_cli::workspace::resolve_workspace_id(&db_path, workspace).await?;

    let storage = maho_cli::workspace::open_storage(&db_path)
        .map_err(|e| anyhow::anyhow!("Failed to open database: {}", e))?;

    storage
        .delete_mcp_server(&resolved_ws_id, name)
        .map_err(|e| anyhow::anyhow!("Failed to delete MCP server: {:?}", e))?;

    // No core-update emit here (same reasoning as cmd_mcp_add): the CLI never
    // registers a core-update callback, so the emit would be a dead no-op. The
    // running browser observes the removal via the shared SQLite registry on its
    // next `McpClient::connect`.

    if json_output {
        let res = serde_json::json!({ "status": "success", "removed": name });
        println!("{}", serde_json::to_string_pretty(&res)?);
    } else {
        println!("Removed MCP server '{}' successfully.", name);
    }
    Ok(())
}

/// Plan row 13: `maho agent task` — run one agent task from the terminal.
/// The session is created through the existing shared-workspace store path
/// (`workspace::default_db_path` → `resolve_workspace_id` → `open_storage` →
/// `MutexAgentStorage` → `SwiftideBackend`), the same transport the removed
/// `maho run` path used. tier/final_confirm enforcement stays at the
/// capability broker in the browser process (plan rows 3/5); the CLI carries
/// the validated runtime_config payload and applies the row-9 proactive
/// prompt gate.
/// The session-provisioning inputs for one `maho agent task` run: which
/// workspace to bind, the approval posture, an optional `--resume` target,
/// and the explicit `--dir` whitelist roots. Grouped so the run entry point
/// keeps a readable signature as session inputs grow.
struct AgentTaskSessionInputs<'a> {
    workspace: Option<&'a str>,
    approve_all: bool,
    resume: Option<&'a str>,
    dirs: &'a [PathBuf],
}

async fn cmd_agent_task(
    goal: &str,
    config: &AgentTaskRuntimeConfig,
    session: AgentTaskSessionInputs<'_>,
    json_output: bool,
    stream: bool,
) -> Result<()> {
    let AgentTaskSessionInputs {
        workspace,
        approve_all,
        resume,
        dirs,
    } = session;
    let db_path = maho_cli::workspace::default_db_path();
    // Wave 1C (G10): a fresh run binds a new `task-<uuid>` conversation titled
    // from the goal; `--resume <id>` rebinds to the existing session instead
    // (stored turns and title reused, fail-closed on unknown ids).
    let target = match resume {
        Some(session_id) => TaskSessionTarget::Resume {
            session_id: session_id.to_string(),
        },
        None => TaskSessionTarget::New {
            title: agent_task_title(goal),
        },
    };
    let (session_id, backend) =
        build_agent_task_session(&db_path, workspace, config, approve_all, target, dirs).await?;

    if !json_output {
        let tier = &config.permission_tier;
        let final_confirm = config.final_confirm;
        let proactive_mode = config.proactive_mode;
        let composite = config.composite_enabled;
        println!(
            "Running agent task (tier: {tier}, final_confirm: {final_confirm}, proactive: {proactive_mode}, composite: {composite})..."
        );
        // The session id is the only handle `--resume` accepts, and
        // `maho agent sessions` is the only other way to recover it — print
        // it up front so a scrolled-away run stays resumable. The whitelist
        // roots are the write boundary the user just handed the agent, so
        // they are stated before the first turn rather than discovered
        // through a denial.
        println!("  session: {session_id}");
        let roots = backend.fs_whitelist_roots();
        if roots.is_empty() {
            println!("  writable dirs: (none — file operations fail closed)");
        } else {
            println!("  writable dirs: {}", roots.join(", "));
        }
        if let Some(model) = &config.model {
            println!("  model: {model}");
        }
        if let Some(provider) = &config.provider {
            println!("  provider: {provider}");
        }
    }

    // Wave 3C (G9): non-JSON runs surface live progress on stderr —
    // tool-call started/finished plus status ticks by default, the full
    // event stream with --stream. --json stays final-only (SC9): no stderr
    // stream, output shape unchanged.
    let on_event = if json_output {
        None
    } else {
        Some(agent_task_event_sink(stream))
    };

    let response = backend
        .run_prompt_in_session(&session_id, goal, on_event)
        .await
        .map_err(|e| anyhow::anyhow!("Agent task failed: {e:?}"))?;

    if json_output {
        let out = AgentTaskOutput {
            session_id: &session_id,
            runtime_config: config,
            response: &response,
        };
        println!("{}", serde_json::to_string_pretty(&out)?);
    } else {
        println!("\n--- Agent Task Response ---\n{response}");
    }
    Ok(())
}

/// True for the session ids `maho agent task` mints, which are the ids
/// `--resume` is meant to take. Browser-side conversations live in the same
/// table, so the default listing filters on this prefix instead of dumping
/// every chat the browser ever opened.
fn is_cli_task_session(session_id: &str) -> bool {
    session_id.starts_with("task-")
}

/// JSON projection for `maho agent sessions --json`.
#[derive(serde::Serialize)]
struct AgentSessionOutput {
    session_id: String,
    title: Option<String>,
    updated_at: String,
}

/// P0-2: lists the sessions `maho agent task --resume <SESSION_ID>` accepts,
/// read from the same shared workspace store `--resume` resolves against
/// (`workspace::default_db_path` → `open_storage` → `list_conversations`),
/// so a listed id is always a resolvable one.
async fn cmd_agent_sessions(limit: usize, all: bool, json_output: bool) -> Result<()> {
    let db_path = maho_cli::workspace::default_db_path();
    let sessions = tokio::task::spawn_blocking(move || -> Result<Vec<AgentSessionOutput>> {
        let storage = maho_cli::workspace::open_storage(&db_path)
            .map_err(|e| anyhow::anyhow!("Failed to open database: {e}"))?;
        // Over-read before filtering so the `task-` filter cannot starve the
        // requested limit when browser chats dominate the recent rows.
        let scan_limit = if all {
            limit
        } else {
            limit.saturating_mul(10).max(limit)
        };
        let conversations = storage
            .list_conversations(maho_types::chat::ConversationListState::Active, scan_limit)
            .map_err(|e| anyhow::anyhow!("Failed to list sessions: {e}"))?;
        Ok(conversations
            .into_iter()
            .filter(|c| all || is_cli_task_session(&c.id))
            .take(limit)
            .map(|c| AgentSessionOutput {
                session_id: c.id,
                title: c.title,
                updated_at: c.updated_at,
            })
            .collect())
    })
    .await??;

    if json_output {
        println!("{}", serde_json::to_string_pretty(&sessions)?);
        return Ok(());
    }

    if sessions.is_empty() {
        println!("No agent sessions yet. `maho agent task \"<goal>\"` starts one.");
        return Ok(());
    }

    for session in &sessions {
        let title = session.title.as_deref().unwrap_or("(untitled)");
        println!("{}  {}  {}", session.session_id, session.updated_at, title);
    }
    println!("\nResume one with: maho agent task \"<goal>\" --resume <SESSION_ID>");
    Ok(())
}

/// Wave 1C (G10): how a task run binds to the store — either a fresh
/// `task-<uuid>` conversation titled from the goal, or an explicit
/// `--resume <session-id>` rebind to the existing conversation.
#[derive(Debug, Clone)]
enum TaskSessionTarget {
    New { title: String },
    Resume { session_id: String },
}

/// Wave 1C (G10): a fresh task session's conversation title — the goal's
/// leading whitespace collapsed, cut to the first 80 chars at a char
/// boundary (never panics on multi-byte goals).
fn agent_task_title(goal: &str) -> String {
    const MAX_AGENT_TASK_TITLE_CHARS: usize = 80;
    goal.split_whitespace()
        .collect::<Vec<_>>()
        .join(" ")
        .chars()
        .take(MAX_AGENT_TASK_TITLE_CHARS)
        .collect()
}

/// Builds the agent task session over the shared workspace store (plan row
/// 13; mirrors the removed `maho run` backend construction, minus the
/// deleted BYOK/browser-bridge surface). Dry-callable: no LLM turn and no
/// browser connection happen here, so tests create real sessions. Returns
/// the bound session id alongside the backend: `New` targets mint a titled
/// `task-<uuid>` conversation row up front (so the title exists even before
/// the first turn persists), `Resume` targets verify the session exists and
/// fail closed otherwise.
async fn build_agent_task_session(
    db_path: &std::path::Path,
    workspace: Option<&str>,
    config: &AgentTaskRuntimeConfig,
    approve_all: bool,
    target: TaskSessionTarget,
    dirs: &[PathBuf],
) -> Result<(String, SwiftideBackend)> {
    let resolved_ws_id = maho_cli::workspace::resolve_workspace_id(db_path, workspace).await?;

    let storage = maho_cli::workspace::open_storage(db_path)
        .map_err(|e| anyhow::anyhow!("Failed to open database: {e}"))?;

    // Resolve the session binding while the raw store handle is still in
    // hand (the backend wraps it opaquely from here on).
    let session_id = match target {
        TaskSessionTarget::New { title } => {
            let id = format!("task-{}", uuid::Uuid::new_v4());
            storage
                .ensure_conversation_with_title(&id, &title)
                .map_err(|e| anyhow::anyhow!("Failed to title new task session: {e}"))?;
            id
        }
        TaskSessionTarget::Resume { session_id } => {
            let exists = storage
                .conversation_exists(&session_id)
                .map_err(|e| anyhow::anyhow!("Failed to look up session {session_id}: {e}"))?;
            if !exists {
                anyhow::bail!("Session not found: {session_id}");
            }
            session_id
        }
    };

    let workspaces = storage
        .list_workspaces()
        .map_err(|e| anyhow::anyhow!("Failed to list workspaces: {e}"))?;

    let ws = workspaces
        .into_iter()
        .find(|w| w.id == resolved_ws_id)
        .ok_or_else(|| anyhow::anyhow!("Resolved workspace not found in database"))?;

    let workspace_root = ws
        .workspace_root
        .as_ref()
        .map(PathBuf::from)
        .unwrap_or_else(|| std::env::current_dir().unwrap_or_else(|_| PathBuf::from(".")));

    let backend = SwiftideBackend::new(
        Arc::new(maho_agent::MutexAgentStorage(Arc::new(Mutex::new(storage)))),
        None,
        workspace_root.clone(),
        true, // allow insecure key storage for CLI convenience (pre-existing CLI posture)
    );

    if let Some(space_id) = &ws.space_id {
        backend.set_active_space_id(Some(space_id.clone()));
    }

    // Wave 1B + 1D: approval posture is explicit, never hard-coded, and the
    // session tier caps it (read_only → DenySensitive even with --approve-all;
    // full_access → AllowAll-with-audit; guard → TTY prompt / headless
    // fail-closed / --approve-all escape hatch). The same tier is bound into
    // the kernel dispatch gate below, so the fs-write/shell-exec class and
    // the whitelist ask-gate are enforced regardless of the posture.
    maho_cli::approval::apply_cli_approval(
        &backend,
        approve_all,
        maho_cli::approval::stdin_is_tty(),
        &config.permission_tier,
    );

    // Wave 1D (D7): bind the runtime tier + fs whitelist roots for the kernel
    // tool dispatch gate, and default the artifact root under the workspace
    // so fs_write's fs_path resolves inside the whitelist. Roots come from
    // the shared provisioning function in maho-agent (R-N2) — never an
    // ad-hoc list — so the Wave 2A panel stamping derives identical roots.
    // `--dir` replaces the derived root with an explicit set, still routed
    // through the same provisioning function per root.
    backend.set_runtime_tier(Some(config.permission_tier.clone()));
    backend.set_fs_whitelist_roots(resolve_fs_whitelist_roots(&workspace_root, dirs)?);

    // Per-session routing overrides (`-p` / `-m`). A selected provider is
    // probed alone by the backend's credential resolution, so the session
    // never reaches a provider the caller did not name. Constraining the
    // availability list to the named model makes routing select exactly it
    // (no silent substitution to a category default); leaving it untouched
    // preserves the stored profile's preferred_model and fallback ladder.
    if config.provider.is_some() {
        backend.set_preferred_provider(config.provider.clone());
    }
    if let Some(model) = &config.model {
        backend.set_available_models(vec![model.clone()]);
    }

    // Wave 3A (G8): the composite (batch) gate is bound ONCE here at session
    // open from the runtime-config payload — load-once applies to COMPOSITE
    // ONLY, never to the per-call tier gate (R-D5). Fail-closed default:
    // without `--composite` the runner refuses every batch before any
    // executor is touched.
    backend.set_composite_config(maho_agent::composite::CompositeExecutionConfig {
        enabled: config.composite_enabled,
        ..maho_agent::composite::CompositeExecutionConfig::default()
    });
    if backend.artifact_root().is_none() {
        backend.set_artifact_root(Some(workspace_root.join(".maho").join("artifacts")));
    }

    maho_agent::AgentRuntime::set_system_prompt(&backend, &agent_task_system_prompt(config));

    Ok((session_id, backend))
}

/// The fs whitelist roots bound into the kernel dispatch gate. With no
/// `--dir` the session keeps the historical single workspace-derived root;
/// with `--dir` the explicit set replaces it, each root run through the same
/// shared provisioning function (R-N2) so the CLI never hand-rolls a root
/// list. `default_fs_whitelist_roots` drops a relative anchor silently
/// (yielding a fail-closed empty whitelist with no explanation), so an
/// explicit root it refuses is reported here instead of leaving the user to
/// wonder why every file operation is denied.
fn resolve_fs_whitelist_roots(
    workspace_root: &std::path::Path,
    dirs: &[PathBuf],
) -> Result<Vec<String>> {
    if dirs.is_empty() {
        return Ok(maho_agent::permission::default_fs_whitelist_roots(
            workspace_root,
        ));
    }

    let mut roots = Vec::with_capacity(dirs.len());
    for dir in dirs {
        let resolved = maho_agent::permission::default_fs_whitelist_roots(dir);
        let Some(root) = resolved.into_iter().next() else {
            bail!(
                "--dir {} is not an absolute path, so it cannot be whitelisted \
                 (a relative root would leave the agent with an empty, \
                 fail-closed whitelist). Pass an absolute directory.",
                dir.display()
            );
        };
        if !roots.contains(&root) {
            roots.push(root);
        }
    }
    Ok(roots)
}

/// System prompt for a task session: the row-9 proactive instruction block
/// gated on `proactive_mode`. With the flag off the composition is byte-
/// identical to the plain (empty-base) composition — nothing appended.
fn agent_task_system_prompt(config: &AgentTaskRuntimeConfig) -> String {
    maho_agent::system_prompt::compose_system_prompt_proactive("", &[], None, config.proactive_mode)
}

/// JSON projection for `maho agent task --json`. A typed struct (not the
/// `json!` macro) keeps the workspace disallowed-`unwrap` lint span out of
/// this file's new surface.
#[derive(serde::Serialize)]
struct AgentTaskOutput<'a> {
    session_id: &'a str,
    runtime_config: &'a AgentTaskRuntimeConfig,
    response: &'a str,
}

/// Wave 3C (G9): one stderr line per non-token stream event for
/// `maho agent task`. The default (progress-only) view renders tool-call
/// started/finished and status ticks; `--stream` (full) adds thinking,
/// artifacts, and the terminal proof/reason. Tokens are raw-streamed by the
/// sink itself, so they never format to a line here. Tool args and results
/// are intentionally not rendered on stderr (the panel sanitizes them; the
/// CLI stream carries tool names and outcomes only).
fn format_agent_stream_event(event: &AgentStreamEvent, full: bool) -> Option<String> {
    match event {
        AgentStreamEvent::Token(_) => None,
        AgentStreamEvent::Thinking(text) => full.then(|| format!("[thinking] {text}")),
        AgentStreamEvent::ToolCall { name, .. } => Some(format!("[tool] {name} started")),
        AgentStreamEvent::ToolResult {
            name, succeeded, ..
        } => Some(format!(
            "[tool] {name} finished ({})",
            if *succeeded { "ok" } else { "failed" }
        )),
        AgentStreamEvent::ArtifactCreated { artifact } => full.then(|| {
            format!(
                "[artifact] {} ({})",
                artifact.artifact_id, artifact.storage_rel_path
            )
        }),
        AgentStreamEvent::Status {
            elapsed_secs,
            message,
        } => Some(format!("[status +{elapsed_secs}s] {message}")),
        AgentStreamEvent::ProofOrReason {
            proof_locator,
            reason,
        } => full.then(|| {
            if let Some(locator) = proof_locator {
                format!("[proof] {locator}")
            } else {
                format!("[reason] {}", reason.as_deref().unwrap_or(""))
            }
        }),
    }
}

/// Wave 3C (G9): the stderr event sink for a non-JSON `maho agent task`
/// run. In `--stream` mode tokens stream raw (no newline); every other
/// event renders through `format_agent_stream_event`.
fn agent_task_event_sink(full: bool) -> Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static> {
    Arc::new(move |event: AgentStreamEvent| match &event {
        AgentStreamEvent::Token(token) if full => eprint!("{token}"),
        other => {
            if let Some(line) = format_agent_stream_event(other, full) {
                eprintln!("{line}");
            }
        }
    })
}

async fn cmd_routine_list(socket_path: Option<&str>, json_output: bool) -> Result<()> {
    // Route through MCP against the RUNNING browser so the CLI reflects the same
    // routine set (built-in recipes + WebUI-created custom routines) that the
    // live browser profile exposes — NOT a private in-process SQLite DB.
    let client = browser::connect(socket_path).await?;
    let result =
        browser::send_tool_call(&client, "browser_routines_list", serde_json::json!({})).await?;

    if json_output {
        println!("{}", serde_json::to_string_pretty(&result)?);
    } else {
        // The browser returns `{ "routines": [ { id, name, description, ... } ] }`.
        // Fall back to a bare array for robustness.
        let routines = result
            .get("routines")
            .and_then(|r| r.as_array())
            .cloned()
            .or_else(|| result.as_array().cloned())
            .unwrap_or_default();

        println!("Available routines:");
        for r in routines {
            let id = r.get("id").and_then(|v| v.as_str()).unwrap_or("");
            let name = r.get("name").and_then(|v| v.as_str()).unwrap_or("");
            let desc = r.get("description").and_then(|v| v.as_str()).unwrap_or("");
            println!("- {} ({}): {}", id, name, desc);
        }
    }
    Ok(())
}

async fn cmd_routine_run(name: &str, socket_path: Option<&str>, json_output: bool) -> Result<()> {
    // If name points to a local file, validate JSON payload format
    let p = std::path::Path::new(name);
    if p.exists() || name.ends_with(".json") {
        if let Ok(content) = std::fs::read_to_string(p) {
            if serde_json::from_str::<serde_json::Value>(&content).is_err() {
                if json_output {
                    println!(
                        "{}",
                        serde_json::json!({
                            "error": {
                                "kind": "invalid_routine_json",
                                "message": format!("Invalid JSON format in routine file '{}'", name)
                            }
                        })
                    );
                } else {
                    eprintln!("Error: Invalid routine JSON format in file '{}'", name);
                }
                std::process::exit(1);
            }
        } else if !p.exists() {
            if json_output {
                println!(
                    "{}",
                    serde_json::json!({
                        "error": {
                            "kind": "routine_file_not_found",
                            "message": format!("Routine file not found: '{}'", name)
                        }
                    })
                );
            } else {
                eprintln!("Error: Routine file not found: '{}'", name);
            }
            std::process::exit(1);
        }
    }

    let client = browser::connect(socket_path).await?;

    println!("Running routine '{}'...", name);
    match browser::send_routine_run(&client, name).await {
        Ok(result) => {
            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                let id = result.get("id").and_then(|v| v.as_str()).unwrap_or(name);
                let content = result.get("content").and_then(|v| v.as_str()).unwrap_or("");
                println!("\n--- Routine Result ({}) ---\n{}", id, content);
            }
            Ok(())
        }
        Err(browser::RoutineCallError::TierLocked { .. }) => {
            eprintln!("Error: Routines are available on Max tier only");
            std::process::exit(2);
        }
        Err(browser::RoutineCallError::Other(error)) => {
            eprintln!("Error: {error}");
            std::process::exit(1);
        }
    }
}

// ─── Headless command handler ─────────────────────────────────────────────────

fn resolve_maho_binary_path(path: &std::path::Path) -> Option<std::path::PathBuf> {
    if !path.exists() {
        return None;
    }
    if path.extension().is_some_and(|extension| extension == "app") {
        let executable_name = path.file_stem()?;
        let executable = path.join("Contents/MacOS").join(executable_name);
        return executable.exists().then_some(executable);
    }
    Some(path.to_path_buf())
}

fn find_maho_binary() -> Option<std::path::PathBuf> {
    if let Ok(p) = std::env::var("MAHO_APP_PATH") {
        let path = std::path::PathBuf::from(&p);
        return resolve_maho_binary_path(&path);
    }

    let candidates = [
        "/Applications/Maho.app/Contents/MacOS/Maho",
        "chromium/src/out/Default/Maho.app/Contents/MacOS/Maho",
    ];
    for c in &candidates {
        let path = std::path::PathBuf::from(c);
        if path.exists() {
            return Some(path);
        }
    }
    None
}

struct SpawnedBrowser {
    child: std::process::Child,
}

impl SpawnedBrowser {
    fn spawn(binary: &std::path::Path) -> Result<Self> {
        let child = std::process::Command::new(binary)
            .args([
                "--headless=new",
                "--no-first-run",
                "--no-default-browser-check",
            ])
            .stdin(std::process::Stdio::null())
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .spawn()
            .with_context(|| format!("failed to spawn browser: {}", binary.display()))?;
        Ok(Self { child })
    }

    fn terminate(&mut self) {
        #[cfg(unix)]
        {
            use nix::sys::signal::{kill, Signal};
            use nix::unistd::Pid;

            let pid = Pid::from_raw(self.child.id() as i32);
            let _ = kill(pid, Signal::SIGTERM);

            for _ in 0..50 {
                std::thread::sleep(std::time::Duration::from_millis(100));
                if let Ok(Some(_)) = self.child.try_wait() {
                    return;
                }
            }
            let _ = kill(pid, Signal::SIGKILL);
            let _ = self.child.wait();
        }
        #[cfg(not(unix))]
        {
            // Windows has no SIGTERM; request process termination via the OS
            // (TerminateProcess) and reap the child to avoid a zombie handle.
            let _ = self.child.kill();
            let _ = self.child.wait();
        }
    }
}

async fn wait_for_socket(
    socket_path: Option<&str>,
    timeout_ms: u64,
) -> Result<maho_browser_mcp::client::BrowserClient> {
    let effective_timeout = std::env::var("MAHO_SOCKET_TIMEOUT_MS")
        .ok()
        .and_then(|v| v.parse::<u64>().ok())
        .unwrap_or(timeout_ms);
    let polls = effective_timeout / 200;
    for _ in 0..polls {
        tokio::time::sleep(std::time::Duration::from_millis(200)).await;
        if let Ok(client) = browser::connect(socket_path).await {
            return Ok(client);
        }
    }
    bail!(
        "browser socket did not appear within {}s",
        effective_timeout / 1000
    )
}

async fn cmd_headless(
    args: HeadlessArgs,
    socket_path: Option<&str>,
    _json_output: bool,
) -> Result<()> {
    if args.wait.is_some() {
        eprintln!("Error: --wait option is not supported.");
        std::process::exit(2);
    }
    if args.batch.is_some() {
        return cmd_headless_batch(args, socket_path).await;
    }

    let url = match &args.url {
        Some(u) => u.clone(),
        None => {
            eprintln!("Error: --url or --batch is required");
            std::process::exit(1);
        }
    };

    let mut spawned: Option<SpawnedBrowser> = None;

    let client = match browser::connect(socket_path).await {
        Ok(c) => c,
        Err(_) if args.launch => {
            let binary = find_maho_binary().ok_or_else(|| {
                anyhow::anyhow!(
                    "Cannot find Maho.app binary. Set MAHO_APP_PATH env var or install Maho.app to /Applications."
                )
            })?;
            eprintln!("Launching browser: {}", binary.display());
            spawned = Some(SpawnedBrowser::spawn(&binary)?);

            tokio::time::sleep(std::time::Duration::from_millis(100)).await;
            if let Ok(Some(status)) = spawned.as_mut().unwrap().child.try_wait() {
                bail!("browser binary exited immediately with status: {status}");
            }

            wait_for_socket(socket_path, 15000).await?
        }
        Err(e) => {
            eprintln!("Browser not running. Use --launch to auto-start Maho.app.");
            return Err(e);
        }
    };

    let result = run_headless_extraction(&client, &url, &args).await;

    if let Some(ref mut browser) = spawned {
        if !args.keep_alive {
            browser.terminate();
        }
    }

    result
}

async fn open_tab_and_wait(
    client: &maho_browser_mcp::client::BrowserClient,
    url: &str,
) -> Result<i64> {
    let new_tab_result =
        browser::send_tool_call(client, "browser_tab_new", serde_json::json!({ "url": url }))
            .await?;

    let tab_id = new_tab_result
        .get("tab")
        .and_then(|t| t.get("id"))
        .and_then(|id| id.as_i64())
        .unwrap_or(0);

    let mut loaded = false;
    for _ in 0..20 {
        tokio::time::sleep(std::time::Duration::from_millis(500)).await;
        let tab_info = browser::send_tool_call(
            client,
            "browser_tab_get",
            serde_json::json!({ "tab_id": tab_id }),
        )
        .await?;
        let status = tab_info
            .get("status")
            .and_then(|s| s.as_str())
            .unwrap_or("complete");
        if status != "loading" {
            loaded = true;
            break;
        }
    }

    if !loaded {
        eprintln!("Warning: page did not finish loading within 10s");
    }

    Ok(tab_id)
}

async fn run_headless_extraction(
    client: &maho_browser_mcp::client::BrowserClient,
    url: &str,
    args: &HeadlessArgs,
) -> Result<()> {
    let tab_id = open_tab_and_wait(client, url).await?;

    let extractors: Vec<&str> = args.extract.split(',').map(|s| s.trim()).collect();
    for extractor in &extractors {
        match *extractor {
            "title" => {
                let info = browser::send_tool_call(
                    client,
                    "browser_tab_get",
                    serde_json::json!({ "tab_id": tab_id }),
                )
                .await?;
                let title = info.get("title").and_then(|v| v.as_str()).unwrap_or("");
                println!("{title}");
            }
            "text" => {
                let result = browser::send_tool_call(
                    client,
                    "browser_page_text",
                    serde_json::json!({ "tab_id": tab_id }),
                )
                .await?;
                let text = result.get("text").and_then(|v| v.as_str()).unwrap_or("");
                println!("{text}");
            }
            "links" => {
                let result = browser::send_tool_call(
                    client,
                    "browser_page_context",
                    serde_json::json!({ "tab_id": tab_id }),
                )
                .await?;
                if let Some(links) = result.get("links").and_then(|l| l.as_array()) {
                    for link in links {
                        if let Some(href) = link.as_str() {
                            println!("{href}");
                        } else if let Some(href) = link.get("href").and_then(|h| h.as_str()) {
                            println!("{href}");
                        }
                    }
                }
            }
            "meta" => {
                let result = browser::send_tool_call(
                    client,
                    "browser_page_context",
                    serde_json::json!({ "tab_id": tab_id }),
                )
                .await?;
                if let Some(desc) = result.get("meta_description").and_then(|v| v.as_str()) {
                    println!("description: {desc}");
                }
                if let Some(lang) = result.get("language").and_then(|v| v.as_str()) {
                    println!("language: {lang}");
                }
                if let Some(title) = result.get("title").and_then(|v| v.as_str()) {
                    println!("title: {title}");
                }
            }
            "html" => {
                let result = browser::send_tool_call(
                    client,
                    "browser_page_content",
                    serde_json::json!({ "tab_id": tab_id }),
                )
                .await?;
                let text = result.get("text").and_then(|v| v.as_str()).unwrap_or("");
                println!("{text}");
            }
            other => {
                eprintln!("Unknown extractor: {other}. Valid: title, text, links, meta, html");
                std::process::exit(1);
            }
        }
    }

    let _ = browser::send_tool_call(
        client,
        "browser_tab_close",
        serde_json::json!({ "tab_id": tab_id }),
    )
    .await;

    Ok(())
}

// ─── Batch headless handler ───────────────────────────────────────────────────

async fn run_batch_url(
    socket_path: Option<&str>,
    url: &str,
    extract: &str,
) -> Result<serde_json::Map<String, serde_json::Value>> {
    let client = browser::connect(socket_path).await?;
    let tab_id = open_tab_and_wait(&client, url).await?;

    let mut fields = serde_json::Map::new();
    let extractors: Vec<&str> = extract
        .split(',')
        .map(|s| s.trim())
        .filter(|s| !s.is_empty())
        .collect();

    for extractor in &extractors {
        match *extractor {
            "title" => {
                let info = browser::send_tool_call(
                    &client,
                    "browser_tab_get",
                    serde_json::json!({ "tab_id": tab_id }),
                )
                .await?;
                let title = info
                    .get("title")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                fields.insert("title".to_string(), serde_json::Value::String(title));
            }
            "text" => {
                let result = browser::send_tool_call(
                    &client,
                    "browser_page_text",
                    serde_json::json!({ "tab_id": tab_id }),
                )
                .await?;
                let text = result
                    .get("text")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                fields.insert("text".to_string(), serde_json::Value::String(text));
            }
            "links" => {
                let result = browser::send_tool_call(
                    &client,
                    "browser_page_context",
                    serde_json::json!({ "tab_id": tab_id }),
                )
                .await?;
                let links = result
                    .get("links")
                    .cloned()
                    .unwrap_or_else(|| serde_json::Value::Array(vec![]));
                fields.insert("links".to_string(), links);
            }
            "meta" => {
                let result = browser::send_tool_call(
                    &client,
                    "browser_page_context",
                    serde_json::json!({ "tab_id": tab_id }),
                )
                .await?;
                let mut meta = serde_json::Map::new();
                if let Some(d) = result.get("meta_description") {
                    meta.insert("description".to_string(), d.clone());
                }
                if let Some(l) = result.get("language") {
                    meta.insert("language".to_string(), l.clone());
                }
                if let Some(t) = result.get("title") {
                    meta.insert("title".to_string(), t.clone());
                }
                fields.insert("meta".to_string(), serde_json::Value::Object(meta));
            }
            "html" => {
                let result = browser::send_tool_call(
                    &client,
                    "browser_page_content",
                    serde_json::json!({ "tab_id": tab_id }),
                )
                .await?;
                let text = result
                    .get("text")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                fields.insert("html".to_string(), serde_json::Value::String(text));
            }
            other => {
                let _ = browser::send_tool_call(
                    &client,
                    "browser_tab_close",
                    serde_json::json!({ "tab_id": tab_id }),
                )
                .await;
                bail!("unknown extractor: {other}. Valid: title, text, links, meta, html");
            }
        }
    }

    let _ = browser::send_tool_call(
        &client,
        "browser_tab_close",
        serde_json::json!({ "tab_id": tab_id }),
    )
    .await;

    Ok(fields)
}

fn load_skip_urls(path: &str) -> std::collections::HashSet<String> {
    let mut done = std::collections::HashSet::new();
    if let Ok(content) = std::fs::read_to_string(path) {
        for line in content.lines() {
            let l = line.trim();
            if l.is_empty() {
                continue;
            }
            if let Ok(v) = serde_json::from_str::<serde_json::Value>(l) {
                if let Some(u) = v.get("url").and_then(|x| x.as_str()) {
                    done.insert(u.to_string());
                    continue;
                }
            }
            done.insert(l.to_string());
        }
    }
    done
}

async fn cmd_headless_batch(args: HeadlessArgs, socket_path: Option<&str>) -> Result<()> {
    use std::io::Write;

    let batch_path = args
        .batch
        .as_ref()
        .ok_or_else(|| anyhow::anyhow!("batch mode requires --batch FILE"))?;
    let content = std::fs::read_to_string(batch_path)
        .with_context(|| format!("failed to read batch file: {batch_path}"))?;

    let mut seen = std::collections::HashSet::new();
    let mut urls: Vec<String> = Vec::new();
    for line in content.lines() {
        let u = line.trim();
        if u.is_empty() {
            continue;
        }
        if seen.insert(u.to_string()) {
            urls.push(u.to_string());
        }
    }

    if let Some(skip) = &args.skip_completed {
        let done = load_skip_urls(skip);
        urls.retain(|u| !done.contains(u));
    }

    let writer: std::sync::Arc<std::sync::Mutex<Box<dyn Write + Send>>> = match &args.out {
        Some(p) => std::sync::Arc::new(std::sync::Mutex::new(Box::new(std::io::BufWriter::new(
            std::fs::File::create(p)
                .with_context(|| format!("failed to create output file: {p}"))?,
        )))),
        None => std::sync::Arc::new(std::sync::Mutex::new(Box::new(std::io::stdout()))),
    };

    let concurrency = (args.concurrency.max(1)) as usize;
    let extract = std::sync::Arc::new(args.extract.clone());
    let socket_owned: Option<String> = socket_path.map(|s| s.to_string());

    for chunk in urls.chunks(concurrency) {
        let mut handles = Vec::with_capacity(chunk.len());
        for url in chunk {
            let writer = std::sync::Arc::clone(&writer);
            let extract = std::sync::Arc::clone(&extract);
            let socket_owned = socket_owned.clone();
            let url = url.clone();
            handles.push(tokio::spawn(async move {
                let row = match run_batch_url(socket_owned.as_deref(), &url, &extract).await {
                    Ok(mut fields) => {
                        fields.insert("url".to_string(), serde_json::Value::String(url.clone()));
                        fields.insert("ok".to_string(), serde_json::Value::Bool(true));
                        serde_json::Value::Object(fields)
                    }
                    Err(e) => {
                        serde_json::json!({ "url": url, "ok": false, "error": e.to_string() })
                    }
                };
                if let Ok(line) = serde_json::to_string(&row) {
                    if let Ok(mut w) = writer.lock() {
                        let _ = writeln!(w, "{line}");
                        let _ = w.flush();
                    }
                }
            }));
        }

        for h in handles {
            let _ = h.await;
        }
    }

    Ok(())
}

// ─── REPL command handler ─────────────────────────────────────────────────────

const REPL_HELP: &str = "\
Commands:
  tabs                  List open tabs
  tab info              Show active tab info
  navigate <url>        Navigate active tab to URL
  text                  Get page text (truncated)
  screenshot            Save full-page screenshot to /tmp
  snapshot              Accessibility snapshot (JSON)
  observe [mode] [sel]  Compact accessibility snapshot (mode: interactive|compact|full)
  wait selector <css>   Wait for CSS selector to appear
  wait url <pattern>    Wait for active tab URL to match pattern
  click <ref>|@<name>   Click by ref, or by name (re-resolved live)
  type <ref> <text>     Type text into element (also: type @<name>=<text>)
  upload <path>         Select a local file for the active pending chooser
  select <ref> <value>  Choose an option by value (also: select @<name>=<value>)
  refs [filter]         List interactive elements: <ref> <role> <name>[=<value>]
  key <name> [mods]     Press a key (e.g. Enter, ArrowDown; mods: shift,ctrl,alt,meta)
  lease [tab_id]        Acquire active-tab lease (auto-acquired before mutations)
  grant <origin>        Grant exact origin to this session's sandbox
  help                  Show this help
  .quit / .exit         Exit REPL
";

async fn cmd_repl(socket_path: Option<&str>, _json_output: bool) -> Result<()> {
    let client = browser::connect_repl(socket_path).await?;

    let history_dir = dirs_path();
    std::fs::create_dir_all(&history_dir).ok();
    let history_file = history_dir.join("repl_history");

    let config = rustyline::config::Config::builder()
        .auto_add_history(true)
        .build();
    let mut rl = rustyline::DefaultEditor::with_config(config)?;
    let _ = rl.load_history(&history_file);

    loop {
        let line = match rl.readline("maho> ") {
            Ok(l) => l,
            Err(rustyline::error::ReadlineError::Interrupted) => continue,
            Err(rustyline::error::ReadlineError::Eof) => break,
            Err(e) => {
                eprintln!("readline error: {e}");
                break;
            }
        };

        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }
        if trimmed == ".quit" || trimmed == ".exit" {
            break;
        }

        if let Err(e) = handle_repl_line(&client, trimmed).await {
            eprintln!("error: {e}");
        }
    }

    let _ = rl.save_history(&history_file);
    Ok(())
}

fn dirs_path() -> std::path::PathBuf {
    std::env::var("HOME")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|_| std::path::PathBuf::from("."))
        .join(".maho")
}

/// Acquire an (auto-stealing) lease on the active tab so subsequent mutations
/// pass the active-tab lease gate. Stateful transport failures are propagated
/// because their outcome may be ambiguous and must never be replayed.
async fn ensure_active_lease(client: &maho_browser_mcp::client::BrowserClient) -> Result<()> {
    let result = browser::send_tool_call(client, "browser_tab_list", serde_json::json!({})).await?;
    if let Some(tabs) = tool_payload(&result).get("tabs").and_then(|t| t.as_array()) {
        if let Some(active) = tabs
            .iter()
            .find(|t| t.get("is_active") == Some(&serde_json::Value::Bool(true)))
        {
            if let Some(id) = active.get("id").and_then(|v| v.as_i64()) {
                browser::send_control_call(
                    client,
                    "browser_acquire_lease",
                    serde_json::json!({ "tab_id": id, "ttl_seconds": 300 }),
                )
                .await?;
            }
        }
    }
    Ok(())
}

/// Dispatch a mutating tool call, auto-acquiring the active-tab lease and
/// retrying once when the browser reports the active-tab lease is required
/// (e.g. after lease expiry). Acquisition auto-steals, so no user action is
/// needed and "lease required" never surfaces during a session.
async fn mutate(
    client: &maho_browser_mcp::client::BrowserClient,
    tool: &str,
    args: serde_json::Value,
) -> Result<serde_json::Value> {
    match browser::send_tool_call(client, tool, args.clone()).await {
        Ok(v) => Ok(v),
        Err(e) if format!("{e}").contains("lease required") => {
            ensure_active_lease(client).await?;
            browser::send_tool_call(client, tool, args).await
        }
        Err(e) => Err(e),
    }
}

/// Dispatch a mutating tool call without reconnect-and-replay transport,
/// while still auto-acquiring the active-tab lease on pre-dispatch lease rejection.
async fn mutate_no_replay(
    client: &maho_browser_mcp::client::BrowserClient,
    tool: &str,
    args: serde_json::Value,
) -> Result<serde_json::Value> {
    match browser::send_tool_call_no_replay(client, tool, args.clone()).await {
        Ok(v) => Ok(v),
        Err(e) if format!("{e}").contains("lease required") => {
            ensure_active_lease(client).await?;
            browser::send_tool_call_no_replay(client, tool, args).await
        }
        Err(e) => Err(e),
    }
}

/// Resolve an element's current @ref by its accessible name against a FRESH
/// accessibility snapshot, so name-addressed mutations never act on a stale
/// ref after the DOM renumbers (e.g. a dialog opens). Exact case-insensitive
/// name match wins; otherwise the first substring match. Only nodes carrying
/// an integer `ref` (interactive elements) are considered.
async fn resolve_ref_by_name(
    client: &maho_browser_mcp::client::BrowserClient,
    name: &str,
) -> Result<Option<i64>> {
    let needle = name.trim().to_lowercase();
    if needle.is_empty() {
        return Ok(None);
    }
    let snapshot = browser::send_tool_call(
        client,
        "browser_accessibility_snapshot",
        serde_json::json!({}),
    )
    .await?;
    let mut exact: Option<i64> = None;
    let mut substring: Option<i64> = None;
    let mut stack = vec![&snapshot];
    while let Some(node) = stack.pop() {
        match node {
            serde_json::Value::Object(map) => {
                if let (Some(r), Some(nm)) = (
                    map.get("ref").and_then(|v| v.as_i64()),
                    map.get("name").and_then(|v| v.as_str()),
                ) {
                    let hay = nm.to_lowercase();
                    if hay == needle {
                        exact.get_or_insert(r);
                    } else if hay.contains(&needle) {
                        substring.get_or_insert(r);
                    }
                }
                for v in map.values() {
                    stack.push(v);
                }
            }
            serde_json::Value::Array(arr) => {
                for v in arr {
                    stack.push(v);
                }
            }
            _ => {}
        }
    }
    Ok(exact.or(substring))
}

async fn handle_repl_line(
    client: &maho_browser_mcp::client::BrowserClient,
    line: &str,
) -> Result<()> {
    let parts: Vec<&str> = line.splitn(3, ' ').collect();
    let cmd = parts[0];

    match cmd {
        "help" => {
            print!("{REPL_HELP}");
        }
        "tabs" => {
            let result =
                browser::send_tool_call(client, "browser_tab_list", serde_json::json!({})).await?;
            if let Some(tabs) = tool_payload(&result).get("tabs").and_then(|t| t.as_array()) {
                println!("{:<4} {:<40} {:<60} {}", "ID", "TITLE", "URL", "ACTIVE");
                for tab in tabs {
                    let id = tab.get("id").and_then(|v| v.as_i64()).unwrap_or(0);
                    let title = tab.get("title").and_then(|v| v.as_str()).unwrap_or("");
                    let url = tab.get("url").and_then(|v| v.as_str()).unwrap_or("");
                    let active = tab
                        .get("is_active")
                        .and_then(|v| v.as_bool())
                        .unwrap_or(false);
                    println!(
                        "{:<4} {:<40} {:<60} {}",
                        id,
                        browser::truncate(title, 40),
                        browser::truncate(url, 60),
                        if active { "*" } else { "" }
                    );
                }
            }
        }
        "tab" if parts.get(1) == Some(&"info") => {
            let result =
                browser::send_tool_call(client, "browser_tab_list", serde_json::json!({})).await?;
            let tabs = tool_payload(&result).get("tabs").and_then(|t| t.as_array());
            let active = tabs.and_then(|arr| {
                arr.iter()
                    .find(|t| t.get("is_active") == Some(&serde_json::Value::Bool(true)))
            });
            if let Some(tab) = active {
                println!(
                    "id: {}",
                    tab.get("id").and_then(|v| v.as_i64()).unwrap_or(0)
                );
                println!(
                    "title: {}",
                    tab.get("title").and_then(|v| v.as_str()).unwrap_or("")
                );
                println!(
                    "url: {}",
                    tab.get("url").and_then(|v| v.as_str()).unwrap_or("")
                );
                println!("active: true");
            } else {
                println!("No active tab.");
            }
        }
        "navigate" => {
            let url = parts.get(1).unwrap_or(&"");
            if url.is_empty() {
                println!("Usage: navigate <url>");
                return Ok(());
            }
            mutate(
                client,
                "browser_navigate",
                serde_json::json!({ "url": *url }),
            )
            .await?;
            println!("ok");
        }
        "text" => {
            let result =
                browser::send_tool_call(client, "browser_page_text", serde_json::json!({})).await?;
            let text = result.get("text").and_then(|v| v.as_str()).unwrap_or("");
            println!("{}", browser::truncate(text, 2000));
        }
        "screenshot" => {
            let rest = line[cmd.len()..].trim();
            let result =
                browser::send_tool_call(client, "browser_screenshot_full", serde_json::json!({}))
                    .await?;
            let payload = tool_payload(&result);
            let data = payload.get("data").and_then(|v| v.as_str()).unwrap_or("");
            if data.is_empty() {
                println!("No screenshot data received.");
                return Ok(());
            }
            use base64::Engine;
            let bytes = base64::engine::general_purpose::STANDARD.decode(data)?;
            let path = if !rest.is_empty() {
                rest.to_string()
            } else {
                let ts = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_secs();
                format!("/tmp/maho-repl-{ts}.png")
            };
            if let Some(parent) = std::path::Path::new(&path).parent() {
                let _ = std::fs::create_dir_all(parent);
            }
            std::fs::write(&path, &bytes)?;
            println!("{path}");
        }
        "snapshot" => {
            let result = browser::send_tool_call(
                client,
                "browser_accessibility_snapshot",
                serde_json::json!({}),
            )
            .await?;
            println!("{}", serde_json::to_string_pretty(&result)?);
        }
        "observe" => {
            let rest = line[cmd.len()..].trim();
            let (first_word, rem) = match rest.split_once(char::is_whitespace) {
                Some((w, r)) => (w, r.trim()),
                None => (rest, ""),
            };
            let (mode, selector) = match first_word {
                "interactive" | "compact" | "full" => {
                    let sel = if rem.is_empty() { None } else { Some(rem) };
                    (first_word, sel)
                }
                "" => ("compact", None),
                _ => ("compact", Some(rest)),
            };

            let mut args = serde_json::json!({
                "mode": mode,
            });
            if let Some(sel) = selector {
                args["scope"] = serde_json::json!({ "selector": sel });
            }

            let result = client
                .call_capability("page.accessibility_snapshot_v2", args)
                .await?;
            let projected = browser::project_cli_result(result);
            if let Some(tree) = projected.get("tree").and_then(|v| v.as_str()) {
                if tree.trim().is_empty() {
                    println!("(empty tree)");
                } else {
                    print!("{tree}");
                    if !tree.ends_with('\n') {
                        println!();
                    }
                }
            } else {
                println!("{}", serde_json::to_string_pretty(&projected)?);
            }
        }
        "wait" => {
            let rest = line[cmd.len()..].trim();
            let (subcmd, target) = match rest.split_once(char::is_whitespace) {
                Some((sc, tg)) => (sc, tg.trim()),
                None => (rest, ""),
            };
            match subcmd {
                "selector" => {
                    if target.is_empty() {
                        println!("Usage: wait selector <css>");
                        return Ok(());
                    }
                    let result = browser::send_tool_call(
                        client,
                        "page_wait_for_selector",
                        serde_json::json!({ "selector": target, "timeout_ms": 10000 }),
                    )
                    .await?;
                    let found = result
                        .get("found")
                        .and_then(|v| v.as_bool())
                        .unwrap_or(false);
                    if found {
                        println!("ok");
                    } else {
                        println!("not found (timeout)");
                    }
                }
                "url" => {
                    if target.is_empty() {
                        println!("Usage: wait url <pattern>");
                        return Ok(());
                    }
                    let start = std::time::Instant::now();
                    let timeout = std::time::Duration::from_millis(10000);
                    let mut matched = false;
                    while start.elapsed() < timeout {
                        let tab_list = browser::send_tool_call(
                            client,
                            "browser_tab_list",
                            serde_json::json!({}),
                        )
                        .await?;
                        if let Some(tabs) = tool_payload(&tab_list)
                            .get("tabs")
                            .and_then(|t| t.as_array())
                        {
                            if let Some(active) = tabs.iter().find(|t| {
                                t.get("is_active") == Some(&serde_json::Value::Bool(true))
                            }) {
                                if let Some(url) = active.get("url").and_then(|v| v.as_str()) {
                                    if browser_pipe::matches_url_pattern(url, target) {
                                        matched = true;
                                        break;
                                    }
                                }
                            }
                        }
                        tokio::time::sleep(std::time::Duration::from_millis(100)).await;
                    }
                    if matched {
                        println!("ok");
                    } else {
                        println!("timeout");
                    }
                }
                _ => {
                    println!("Usage: wait selector <css> | wait url <pattern>");
                }
            }
        }
        "refs" => {
            let filter = line[cmd.len()..].trim().to_lowercase();
            let snapshot = browser::send_tool_call(
                client,
                "browser_accessibility_snapshot",
                serde_json::json!({}),
            )
            .await?;
            let mut rows: Vec<(i64, String)> = Vec::new();
            let mut stack = vec![&snapshot];
            while let Some(node) = stack.pop() {
                match node {
                    serde_json::Value::Object(map) => {
                        if let Some(r) = map.get("ref").and_then(|v| v.as_i64()) {
                            let role = map.get("role").and_then(|v| v.as_str()).unwrap_or("");
                            let name = map.get("name").and_then(|v| v.as_str()).unwrap_or("");
                            let value = map.get("value").and_then(|v| v.as_str());
                            let hay = format!("{role} {name}").to_lowercase();
                            if filter.is_empty() || hay.contains(&filter) {
                                let text = match value {
                                    Some(v) if !v.is_empty() => {
                                        format!("{r}\t{role}\t{name}={v}")
                                    }
                                    _ => format!("{r}\t{role}\t{name}"),
                                };
                                rows.push((r, text));
                            }
                        }
                        for v in map.values() {
                            stack.push(v);
                        }
                    }
                    serde_json::Value::Array(arr) => {
                        for v in arr {
                            stack.push(v);
                        }
                    }
                    _ => {}
                }
            }
            rows.sort_by_key(|(r, _)| *r);
            for (_, line) in &rows {
                println!("{line}");
            }
            if rows.is_empty() {
                println!("(no interactive elements)");
            }
        }
        "click" => {
            let rest = line[cmd.len()..].trim_start();
            let target: Option<i64> = if let Some(name) = rest.strip_prefix('@') {
                match resolve_ref_by_name(client, name).await? {
                    Some(r) => Some(r),
                    None => {
                        println!("no element matching: {}", name.trim());
                        return Ok(());
                    }
                }
            } else {
                match rest
                    .split_whitespace()
                    .next()
                    .and_then(|s| s.parse::<i64>().ok())
                {
                    Some(n) if n >= 0 => Some(n),
                    _ => {
                        println!("Usage: click <ref> | click @<name>");
                        return Ok(());
                    }
                }
            };
            if let Some(n) = target {
                mutate(client, "browser_click", serde_json::json!({ "ref": n })).await?;
                println!("ok");
            }
        }
        "type" => {
            let rest = line[cmd.len()..].trim_start();
            let (target, text): (Option<i64>, String) = if let Some(sel) = rest.strip_prefix('@') {
                match sel.split_once('=') {
                    Some((name, val)) => match resolve_ref_by_name(client, name).await? {
                        Some(r) => (Some(r), val.to_string()),
                        None => {
                            println!("no element matching: {}", name.trim());
                            return Ok(());
                        }
                    },
                    None => {
                        println!("Usage: type @<name>=<text>");
                        return Ok(());
                    }
                }
            } else {
                let ref_num = parts
                    .get(1)
                    .and_then(|s| s.parse::<i64>().ok())
                    .filter(|n| *n >= 0);
                (ref_num, parts.get(2).unwrap_or(&"").to_string())
            };
            match target {
                Some(n) if !text.is_empty() => {
                    mutate(
                        client,
                        "browser_type",
                        serde_json::json!({ "ref": n, "text": text, "allow_credentials": true }),
                    )
                    .await?;
                    println!("ok");
                }
                _ => println!("Usage: type <ref> <text> | type @<name>=<text>"),
            }
        }
        "upload" => {
            let path = line[cmd.len()..].trim_start();
            if path.is_empty() {
                println!("Usage: upload <absolute-path>");
                return Ok(());
            }
            mutate_no_replay(
                client,
                "browser_file_upload_select",
                serde_json::json!({ "path": path }),
            )
            .await?;
            println!("ok");
        }
        "key" => {
            let key_name = parts.get(1).map(|s| s.trim()).unwrap_or("");
            if key_name.is_empty() {
                println!("Usage: key <name> [modifiers]");
                return Ok(());
            }
            let mut args = serde_json::json!({ "key": key_name });
            if let Some(mods) = parts.get(2).map(|s| s.trim()).filter(|s| !s.is_empty()) {
                let list: Vec<String> = mods
                    .split(',')
                    .map(|m| m.trim().to_string())
                    .filter(|m| !m.is_empty())
                    .collect();
                args["modifiers"] = serde_json::Value::Array(
                    list.into_iter().map(serde_json::Value::String).collect(),
                );
            }
            mutate(client, "browser_key_press", args).await?;
            println!("ok");
        }
        "select" => {
            let rest = line[cmd.len()..].trim_start();
            let (target, value): (Option<i64>, String) = if let Some(sel) = rest.strip_prefix('@') {
                match sel.split_once('=') {
                    Some((name, val)) => match resolve_ref_by_name(client, name).await? {
                        Some(r) => (Some(r), val.to_string()),
                        None => {
                            println!("no element matching: {}", name.trim());
                            return Ok(());
                        }
                    },
                    None => {
                        println!("Usage: select @<name>=<value>");
                        return Ok(());
                    }
                }
            } else {
                let ref_num = parts
                    .get(1)
                    .and_then(|s| s.parse::<i64>().ok())
                    .filter(|n| *n >= 0);
                (ref_num, parts.get(2).unwrap_or(&"").to_string())
            };
            match target {
                Some(n) if !value.is_empty() => {
                    mutate(
                        client,
                        "browser_select",
                        serde_json::json!({ "ref": n, "value": value }),
                    )
                    .await?;
                    println!("ok");
                }
                _ => println!("Usage: select <ref> <value> | select @<name>=<value>"),
            }
        }
        "grant" => {
            // Grant an exact origin to THIS session's capability sandbox
            // (browser_grant_exact_origin). The grant lives on this REPL
            // session's connection, so re-issue it here after reconnecting
            // before dispatching actions on that origin.
            match parts.get(1).map(|s| s.trim()).filter(|s| !s.is_empty()) {
                Some(origin) => {
                    browser::send_tool_call(
                        client,
                        "browser_grant_exact_origin",
                        serde_json::json!({ "origin": origin }),
                    )
                    .await?;
                    println!("Origin granted: {origin}");
                }
                None => {
                    println!("Usage: grant <origin>");
                }
            }
        }
        "lease" => {
            // Resolve the target tab: an explicit `lease <tab_id>`, otherwise the
            // active tab. Acquisition auto-steals any existing lease, so no user
            // confirmation is needed; the lease is held on this REPL session's
            // connection and gates subsequent click/type mutations.
            let tab_id = match parts.get(1).map(|s| s.trim()).filter(|s| !s.is_empty()) {
                Some(explicit) => match explicit.parse::<i64>() {
                    Ok(id) => id,
                    Err(_) => {
                        println!("Usage: lease [tab_id]");
                        return Ok(());
                    }
                },
                None => {
                    let result =
                        browser::send_tool_call(client, "browser_tab_list", serde_json::json!({}))
                            .await?;
                    let active = tool_payload(&result)
                        .get("tabs")
                        .and_then(|t| t.as_array())
                        .and_then(|arr| {
                            arr.iter().find(|t| {
                                t.get("is_active") == Some(&serde_json::Value::Bool(true))
                            })
                        })
                        .and_then(|t| t.get("id"))
                        .and_then(|v| v.as_i64());
                    match active {
                        Some(id) => id,
                        None => {
                            println!("No active tab to lease.");
                            return Ok(());
                        }
                    }
                }
            };
            browser::send_control_call(
                client,
                "browser_acquire_lease",
                serde_json::json!({ "tab_id": tab_id, "ttl_seconds": 300 }),
            )
            .await?;
            println!("Lease acquired.");
        }
        other => {
            println!("unknown command: {other}");
            println!("Type 'help' for available commands.");
        }
    }
    Ok(())
}
