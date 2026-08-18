mod auth;
mod browser;
mod byok;
mod chat;
pub mod config;
mod credits;

use anyhow::{bail, Context, Result};
use clap::{Args, Parser, Subcommand};
use maho_agent::{
    AgentError, AgentRuntime, AgentStorage, MutexAgentStorage, PermissionDecision, SecureKeyBundle,
    SwiftideBackend, ZeroizedString,
};
use maho_cli::vault::{self, RecoveryUnlockResult};
use maho_types::chat::ChatMessage;
use std::path::PathBuf;
use std::sync::{Arc, Mutex, Once};

use config::CliConfig;

const DEFAULT_PROXY_URL: &str = "https://proxy.maho.co/v1";
const DEFAULT_RELAY_URL: &str = "https://relay.maho.co";

#[derive(Parser)]
#[command(
    name = "maho",
    version,
    about = "Maho CLI — browser control and AI from your terminal"
)]
pub struct Cli {
    /// Output structured JSON for all commands
    #[arg(long, global = true)]
    pub json: bool,

    /// Use an isolated browser profile
    #[arg(long, global = true)]
    pub isolated: bool,

    /// Override UDS socket path (for testing)
    #[arg(long, global = true)]
    pub socket_path: Option<String>,

    /// Override the billing proxy URL
    #[arg(long, global = true, default_value = DEFAULT_PROXY_URL)]
    pub proxy_url: String,

    /// Override the relay URL
    #[arg(long, global = true, default_value = DEFAULT_RELAY_URL)]
    pub relay_url: String,

    #[command(subcommand)]
    pub command: Commands,
}

#[derive(Subcommand)]
pub enum AgentSubcommands {
    /// Run an autonomous AI agent session. Tool calls are auto-approved.
    Run {
        /// Prompt or command string
        prompt: Option<String>,

        /// Path to routine JSON or routine name
        #[arg(long)]
        routine: Option<String>,

        /// LLM provider
        #[arg(long)]
        provider: Option<String>,

        /// LLM model name override
        #[arg(long)]
        model: Option<String>,

        /// Deprecated no-op. Tool calls are auto-approved by default.
        #[arg(long, hide = true)]
        yolo: bool,

        /// Deprecated no-op. Tool calls are auto-approved by default.
        #[arg(long, name = "allow-tools", hide = true)]
        allow_tools: bool,

        /// Target workspace ID
        #[arg(long)]
        workspace: Option<String>,

        /// Continue an existing persisted session
        #[arg(long)]
        session: Option<String>,
    },
}

#[derive(Subcommand)]
pub enum Commands {
    /// Authentication & account management
    Auth {
        #[command(subcommand)]
        command: AuthCommands,
    },

    /// Send a chat message
    Chat {
        /// The message to send
        message: String,

        /// Use BYOK (bring your own key) mode: provider name
        #[arg(long, requires = "api_key")]
        byok: Option<String>,

        /// API key for BYOK mode
        #[arg(long, requires = "byok")]
        api_key: Option<String>,
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

    #[command(about = "Vault recovery operations")]
    Vault {
        #[command(subcommand)]
        command: VaultCommands,
    },

    /// Show version info
    Version,

    /// Run an autonomous AI agent session. Tool calls are auto-approved.
    Run {
        /// The prompt to execute
        prompt: String,

        /// LLM provider (e.g. anthropic, gemini, claude)
        #[arg(long)]
        provider: Option<String>,

        /// LLM model name override
        #[arg(long)]
        model: Option<String>,

        /// Deprecated no-op. Tool calls are auto-approved by default.
        #[arg(long, hide = true)]
        yolo: bool,

        /// Deprecated no-op. Tool calls are auto-approved by default.
        #[arg(long, name = "allow-tools", hide = true)]
        allow_tools: bool,

        /// Workspace ID to target
        #[arg(long)]
        workspace: Option<String>,

        /// Continue an existing persisted session
        #[arg(long)]
        session: Option<String>,
    },

    /// Run an autonomous AI agent session or routine. Tool calls are auto-approved.
    Agent {
        #[command(subcommand)]
        subcommand: Option<AgentSubcommands>,

        /// Prompt to execute
        #[arg(short, long)]
        prompt: Option<String>,

        /// Path to routine JSON or routine name to execute
        #[arg(long)]
        routine: Option<String>,

        /// LLM provider (e.g. anthropic, gemini, claude)
        #[arg(long)]
        provider: Option<String>,

        /// LLM model name override
        #[arg(long)]
        model: Option<String>,

        /// Deprecated no-op. Tool calls are auto-approved by default.
        #[arg(long, hide = true)]
        yolo: bool,

        /// Deprecated no-op. Tool calls are auto-approved by default.
        #[arg(long, name = "allow-tools", hide = true)]
        allow_tools: bool,

        /// Workspace ID to target
        #[arg(long)]
        workspace: Option<String>,

        /// Continue an existing persisted session
        #[arg(long)]
        session: Option<String>,
    },

    /// Tool execution and inspection
    Tool {
        #[command(subcommand)]
        command: ToolCommands,
    },

    /// Continue and inspect agent sessions in shared storage
    Session {
        #[command(subcommand)]
        command: SessionCommands,
    },

    /// List and export browser-owned session artifacts
    Artifact {
        #[command(subcommand)]
        command: ArtifactCommands,
    },

    /// Bounded deep-research mode (Max/PAYG). Runs a capped multi-step research
    /// loop and prints a cited summary. BYOK-only from the CLI.
    Research {
        /// The research question
        query: String,

        /// LLM provider (e.g. anthropic, gemini, openai); required for BYOK
        #[arg(long)]
        provider: Option<String>,

        /// LLM model name override
        #[arg(long)]
        model: Option<String>,

        /// Maximum research steps (hard-capped at 20)
        #[arg(long, default_value_t = 6)]
        max_steps: u32,

        /// Maximum estimated spend for the run, in USD
        #[arg(long, default_value_t = 0.50)]
        budget_usd: f64,

        /// Maximum wall-clock time for the run, in seconds
        #[arg(long, default_value_t = 120)]
        timeout_secs: u64,

        /// Workspace ID to target
        #[arg(long)]
        workspace: Option<String>,
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
        /// Amount in USD (10, 50, or 100)
        #[arg(long, value_parser = ["10", "50", "100"])]
        amount: String,
    },
}

#[derive(Subcommand)]
pub enum SessionCommands {
    /// List sessions in the shared desktop-agent store
    List,
    /// Resume an existing session by loading its persisted history
    Resume {
        /// Opaque session ID
        session_id: String,
    },
    /// Show a session's persisted message history
    History {
        /// Opaque session ID
        session_id: String,
    },
}

#[derive(Subcommand)]
pub enum ArtifactCommands {
    /// List artifacts, optionally scoped to one session
    List {
        #[arg(long)]
        session: Option<String>,
    },
    /// Export an artifact to a validated destination
    Export {
        /// Opaque artifact ID
        artifact_id: String,
        /// Destination file path
        #[arg(long)]
        out: PathBuf,
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

    /// Run a routine by ID
    Run {
        /// The ID of the routine to run
        name: String,

        /// LLM provider (e.g. anthropic, gemini, claude)
        #[arg(long)]
        provider: Option<String>,

        /// LLM model name override
        #[arg(long)]
        model: Option<String>,

        /// Deprecated no-op. Tool calls are auto-approved by default.
        #[arg(long, hide = true)]
        yolo: bool,

        /// Deprecated no-op. Tool calls are auto-approved by default.
        #[arg(long, name = "allow-tools", hide = true)]
        allow_tools: bool,

        /// Workspace ID or name override
        #[arg(long)]
        workspace: Option<String>,
    },
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
    /// Open checkout for a credit pack
    #[command(name = "buy-credits")]
    BuyCredits {
        /// Amount in USD (10, 50, or 100)
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
    /// Import history from NDJSON
    Import {
        /// Path to NDJSON file
        path: String,
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
}

#[derive(Subcommand, Clone)]
pub enum ConfigAction {
    /// Show current config
    Show,
}

// ─── Deprecation helper ───────────────────────────────────────────────────────

fn deprecation_warning(old: &str, new: &str) {
    eprintln!(
        "[deprecated] `maho {old}` is now `maho {new}`. This alias will be removed in v0.6.0."
    );
}

static APPROVAL_FLAG_DEPRECATION: Once = Once::new();

fn warn_deprecated_approval_flags(yolo: bool, allow_tools: bool) {
    if yolo || allow_tools {
        APPROVAL_FLAG_DEPRECATION.call_once(|| {
            eprintln!(
                "[deprecated] --yolo and --allow-tools are no-ops; tool calls are auto-approved by default."
            );
        });
    }
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
    let proxy_url = cli.proxy_url;
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

        // ─── Chat (top-level) ─────────────────────────────────────────────
        Commands::Chat {
            message,
            byok,
            api_key,
        } => {
            if let (Some(provider), Some(key)) = (byok, api_key) {
                byok::send_byok(&provider, &key, &message).await
            } else {
                let cfg = CliConfig::load()?;
                chat::send_chat(&proxy_url, &cfg, &message).await
            }
        }

        // ─── Billing group ────────────────────────────────────────────────
        Commands::Billing { command } => match command {
            BillingCommands::Balance => cmd_balance(&relay_url).await,
            BillingCommands::BuyCredits { amount } => cmd_buy_credits(&amount),
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

        // ─── Browser group (stub — Task 3.4) ──────────────────────────────
        Commands::Browser { command } => match command {
            BrowserCommands::Repl => cmd_repl(socket_path.as_deref(), json_output).await,
        },

        // ─── MCP server ───────────────────────────────────────────────────
        Commands::Mcp { command } => cmd_mcp(command, json_output).await,

        // ─── Routine group ────────────────────────────────────────────────
        Commands::Routine { command } => match command {
            RoutineCommands::List { workspace: _ } => {
                cmd_routine_list(socket_path.as_deref(), json_output).await
            }
            RoutineCommands::Run {
                name,
                provider,
                model,
                yolo,
                allow_tools,
                workspace,
            } => {
                warn_deprecated_approval_flags(yolo, allow_tools);
                let unsupported = unsupported_routine_run_flags(
                    provider.is_some(),
                    model.is_some(),
                    workspace.is_some(),
                );
                if !unsupported.is_empty() {
                    eprintln!(
                        "Error: {} not supported by `maho routine run`. Routines execute inside \
                         the running browser using its own provider, model, and permission \
                         settings, so these flags cannot be applied to a browser-side routine. \
                         For a CLI-driven agent session that honors them, use `maho run <prompt>`.",
                        unsupported.join(", ")
                    );
                    std::process::exit(2);
                }
                cmd_routine_run(&name, socket_path.as_deref(), json_output).await
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

        // ─── Version ──────────────────────────────────────────────────────
        Commands::Version => {
            println!("maho {}", env!("CARGO_PKG_VERSION"));
            Ok(())
        }

        Commands::Run {
            prompt,
            provider,
            model,
            yolo,
            allow_tools,
            workspace,
            session,
        } => {
            warn_deprecated_approval_flags(yolo, allow_tools);
            cmd_run(
                &prompt,
                provider.as_deref(),
                model.as_deref(),
                workspace.as_deref(),
                socket_path.as_deref(),
                session.as_deref(),
            )
            .await
        }

        Commands::Agent {
            subcommand,
            prompt,
            routine,
            provider,
            model,
            yolo,
            allow_tools,
            workspace,
            session,
        } => {
            let (
                final_prompt,
                final_routine,
                final_provider,
                final_model,
                final_yolo,
                final_allow_tools,
                final_workspace,
                final_session,
            ) = match subcommand {
                Some(AgentSubcommands::Run {
                    prompt: sub_prompt,
                    routine: sub_routine,
                    provider: sub_provider,
                    model: sub_model,
                    yolo: sub_yolo,
                    allow_tools: sub_allow_tools,
                    workspace: sub_workspace,
                    session: sub_session,
                }) => (
                    sub_prompt.or(prompt),
                    sub_routine.or(routine),
                    sub_provider.or(provider),
                    sub_model.or(model),
                    sub_yolo || yolo,
                    sub_allow_tools || allow_tools,
                    sub_workspace.or(workspace),
                    sub_session.or(session),
                ),
                None => (
                    prompt,
                    routine,
                    provider,
                    model,
                    yolo,
                    allow_tools,
                    workspace,
                    session,
                ),
            };

            warn_deprecated_approval_flags(final_yolo, final_allow_tools);
            if let Some(r_path) = final_routine {
                let unsupported = unsupported_routine_run_flags(
                    final_provider.is_some(),
                    final_model.is_some(),
                    final_workspace.is_some(),
                );
                if !unsupported.is_empty() {
                    eprintln!(
                        "Error: {} not supported by `maho routine run`. Routines execute inside \
                         the running browser using its own provider, model, and permission \
                         settings, so these flags cannot be applied to a browser-side routine. \
                         For a CLI-driven agent session that honors them, use `maho run <prompt>`.",
                        unsupported.join(", ")
                    );
                    std::process::exit(2);
                }
                cmd_routine_run(&r_path, socket_path.as_deref(), json_output).await
            } else if let Some(p) = final_prompt {
                cmd_run(
                    &p,
                    final_provider.as_deref(),
                    final_model.as_deref(),
                    final_workspace.as_deref(),
                    socket_path.as_deref(),
                    final_session.as_deref(),
                )
                .await
            } else {
                eprintln!("Error: Either a prompt or --routine flag is required for maho agent");
                std::process::exit(1);
            }
        }

        Commands::Tool { command } => cmd_tool(command, socket_path.as_deref(), json_output).await,

        Commands::Session { command } => {
            cmd_session(command, socket_path.as_deref(), json_output).await
        }

        Commands::Artifact { command } => {
            cmd_artifact(command, socket_path.as_deref(), json_output).await
        }

        Commands::Research {
            query,
            provider,
            model,
            max_steps,
            budget_usd,
            timeout_secs,
            workspace,
        } => {
            cmd_research(
                &query,
                provider.as_deref(),
                model.as_deref(),
                max_steps,
                budget_usd,
                timeout_secs,
                workspace.as_deref(),
                socket_path.as_deref(),
                json_output,
            )
            .await
        }

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
            cmd_buy_credits(&amount)
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
    println!("Email: {}", me.email);
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
        eprintln!("Not logged in. Run `maho login` first.");
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

fn cmd_buy_credits(amount: &str) -> Result<()> {
    let url = credits::checkout_url_for_pack(amount);
    println!("Opening LemonSqueezy checkout for ${amount} pack...");
    println!("Checkout URL: {url}");
    match credits::open_checkout_url(url) {
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
    println!("Once your purchase completes, run `maho balance` to verify your new balance.");
    Ok(())
}

// ─── Utility functions ────────────────────────────────────────────────────────

// ─── Tab command handlers ─────────────────────────────────────────────────────

async fn cmd_tab(command: TabCommands, socket_path: Option<&str>, json_output: bool) -> Result<()> {
    match command {
        TabCommands::Info => {
            let client = browser::connect(socket_path).await?;
            let result =
                browser::send_tool_call(&client, "browser_tab_list", serde_json::json!({})).await?;
            let tabs = result.get("tabs").and_then(|t| t.as_array());
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

            let tabs = result.get("tabs").and_then(|t| t.as_array());
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
            let result = browser::send_tool_call(&client, "browser_acquire_lease", args).await?;

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
            let result = browser::send_tool_call(&client, "browser_release_lease", args).await?;

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
            let result = browser::send_tool_call(&client, "browser_heartbeat_lease", args).await?;

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
        HistoryCommands::Import { path } => {
            let _ = path;
            eprintln!(
                "`maho history import` is not supported: the browser MCP server exposes no \
                 `browser_history_import` tool (history access is read-only via \
                 `browser_history_search`). Importing history would require a new server-side \
                 tool and is out of scope for the CLI."
            );
            std::process::exit(2);
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
        } => (
            "mail_list_emails",
            serde_json::json!({
                "account_id": account_id,
                "folder_id": folder_id,
                "limit": limit,
                "offset": offset,
            }),
        ),
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
        } => (
            "mail_search_emails",
            serde_json::json!({
                "query": query,
                "account_id": account_id,
                "folder_id": folder_id,
                "limit": limit,
                "offset": offset,
            }),
        ),
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
            let data = result.get("data").and_then(|v| v.as_str()).unwrap_or("");
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

struct CliAgentStorage {
    inner: MutexAgentStorage,
    bypass_auth: bool,
}

#[async_trait::async_trait]
impl AgentStorage for CliAgentStorage {
    async fn get_byok_key(&self, provider: &str) -> Result<Option<ZeroizedString>, AgentError> {
        self.inner.get_byok_key(provider).await
    }

    async fn get_setting(&self, key: &str) -> Result<Option<ZeroizedString>, AgentError> {
        if key == "auth:access_token" && self.bypass_auth {
            return Ok(None);
        }
        self.inner.get_setting(key).await
    }

    async fn save_message(
        &self,
        session_id: &str,
        role: &str,
        content: &str,
    ) -> Result<(), AgentError> {
        self.inner.save_message(session_id, role, content).await
    }

    async fn get_history(&self, session_id: &str) -> Result<Vec<ChatMessage>, AgentError> {
        self.inner.get_history(session_id).await
    }

    async fn list_mcp_servers(
        &self,
        workspace_id: &str,
    ) -> Result<Vec<maho_types::ai::AiMcpServer>, AgentError> {
        self.inner.list_mcp_servers(workspace_id).await
    }

    async fn list_cli_tools(
        &self,
        workspace_id: &str,
    ) -> Result<Vec<maho_types::ai::AiCliTool>, AgentError> {
        self.inner.list_cli_tools(workspace_id).await
    }

    async fn get_mcp_server(
        &self,
        id: &str,
    ) -> Result<Option<maho_types::ai::AiMcpServer>, AgentError> {
        self.inner.get_mcp_server(id).await
    }

    async fn get_cli_tool(
        &self,
        id: &str,
    ) -> Result<Option<maho_types::ai::AiCliTool>, AgentError> {
        self.inner.get_cli_tool(id).await
    }

    async fn list_workspaces(&self) -> Result<Vec<maho_types::ai::AiWorkspace>, AgentError> {
        self.inner.list_workspaces().await
    }

    async fn get_profile(&self, id: &str) -> Result<Option<maho_types::ai::AiProfile>, AgentError> {
        self.inner.get_profile(id).await
    }

    async fn set_system_prompt_snapshot_if_absent(
        &self,
        conversation_id: &str,
        system_prompt: &str,
    ) -> Result<(), AgentError> {
        self.inner
            .set_system_prompt_snapshot_if_absent(conversation_id, system_prompt)
            .await
    }

    async fn get_system_prompt_snapshot(
        &self,
        conversation_id: &str,
    ) -> Result<Option<String>, AgentError> {
        self.inner.get_system_prompt_snapshot(conversation_id).await
    }
}

/// Bridges the agent to the running browser's authoritative MCP capability
/// discovery and execution endpoints. Browser tool identities are never
/// reconstructed or aliased in the CLI.
struct CliBrowserToolBridge {
    socket_path: Option<String>,
    /// One reused browser connection for the whole run.
    ///
    /// The MCP server scopes tab leases and the domain allowlist to the
    /// connection, so reconnecting per call threw away the lease the previous call
    /// had just acquired. Holding the client here keeps that state alive.
    client: Arc<tokio::sync::Mutex<Option<Arc<maho_browser_mcp::client::BrowserClient>>>>,
}

impl CliBrowserToolBridge {
    fn new(socket_path: Option<String>) -> Self {
        Self {
            socket_path,
            client: Arc::new(tokio::sync::Mutex::new(Option::None)),
        }
    }
}

fn browser_session_transport_failed(message: &str) -> bool {
    message.contains("socket connect failed")
        || message.contains("io error")
        || message.contains("response handler closed")
        || message.contains("connection reset")
        || message.contains("broken pipe")
}

fn normalize_provider_name(p: &str) -> &str {
    if p == "claude" {
        "anthropic"
    } else {
        p
    }
}

fn normalize_base_url_env_value(value: Option<String>) -> Option<String> {
    value
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
}

fn select_byok_base_url(
    maho_byok_base_url: Option<String>,
    openai_base_url: Option<String>,
) -> Option<String> {
    normalize_base_url_env_value(maho_byok_base_url)
        .or_else(|| normalize_base_url_env_value(openai_base_url))
}

fn byok_base_url_from_env() -> Option<String> {
    select_byok_base_url(
        std::env::var("MAHO_BYOK_BASE_URL").ok(),
        std::env::var("OPENAI_BASE_URL").ok(),
    )
}

fn unsupported_routine_run_flags(
    provider: bool,
    model: bool,
    workspace: bool,
) -> Vec<&'static str> {
    let mut flags = Vec::new();
    if provider {
        flags.push("--provider");
    }
    if model {
        flags.push("--model");
    }
    if workspace {
        flags.push("--workspace");
    }
    flags
}

#[cfg(test)]
mod tests {
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Arc;

    use maho_agent::{
        BrowserToolBridge, BrowserToolDescriptor, BrowserToolExecution, BrowserToolExecutionReceipt,
    };
    use serde_json::{json, Value};
    use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
    use tokio::net::UnixListener;

    #[test]
    fn browser_bridge_reuses_one_connection_for_lease_continuity() {
        let runtime = tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)
            .enable_all()
            .build()
            .expect("test runtime must build");
        let temp = tempfile::tempdir().expect("temporary socket directory must exist");
        let socket_path = temp.path().join("browser.sock");
        let listener = {
            let _runtime_guard = runtime.enter();
            UnixListener::bind(&socket_path).expect("test socket must bind")
        };
        let accepts = Arc::new(AtomicUsize::new(0));
        let accepts_for_server = Arc::clone(&accepts);
        let server = runtime.spawn(async move {
            let (stream, _) = listener.accept().await.expect("bridge must connect");
            accepts_for_server.fetch_add(1, Ordering::SeqCst);
            let (reader, mut writer) = stream.into_split();
            let mut reader = BufReader::new(reader);
            for index in 0..3 {
                let mut line = String::new();
                reader
                    .read_line(&mut line)
                    .await
                    .expect("request must be readable");
                let request: Value = serde_json::from_str(&line).expect("request must be JSON");
                let result = if index == 0 {
                    json!({
                        "protocolVersion": "2025-03-26",
                        "serverInfo": {"name": "maho-browser", "version": "1"},
                        "capabilities": {"tools": {}},
                        "sessionInfo": {
                            "id": "mock-session-id",
                            "displayLabel": "Maho CLI",
                            "controllerKind": "maho-cli"
                        }
                    })
                } else {
                    json!({"content": [{"type": "text", "text": "{\"ok\":true}"}]})
                };
                let response = json!({
                    "jsonrpc": "2.0",
                    "id": request["id"],
                    "result": result
                });
                writer
                    .write_all(format!("{response}\n").as_bytes())
                    .await
                    .expect("response must be writable");
            }
        });
        let bridge =
            crate::CliBrowserToolBridge::new(Some(socket_path.to_string_lossy().into_owned()));

        let first = runtime
            .block_on(bridge.execute_tool("browser_tab_list", "{}"))
            .expect("first execution succeeds");
        let second = runtime
            .block_on(bridge.execute_tool("browser_tab_list", "{}"))
            .expect("second execution succeeds");
        assert_eq!(first.output_json, "{\"ok\":true}");
        assert_eq!(second.output_json, "{\"ok\":true}");
        runtime.block_on(server).expect("test server must finish");
        assert_eq!(accepts.load(Ordering::SeqCst), 1);
    }

    use super::{
        mail_tool_request, select_byok_base_url, unsupported_routine_run_flags, MailCommands,
    };

    #[test]
    fn browser_discovery_decodes_only_typed_descriptors() {
        let descriptor = BrowserToolDescriptor {
            capability_id: "tab.list".to_string(),
            name: "browser_tab_list".to_string(),
            description: "List tabs".to_string(),
            input_schema: serde_json::json!({"type":"object","properties":{}}),
            schema_version: 1,
            policy: maho_agent::BrowserToolPolicy {
                sensitive: false,
                permission: maho_types::tool::ToolPermission::AutoApprove,
            },
        };
        let wire = serde_json::json!({"tools": [descriptor.clone()]});
        assert_eq!(
            crate::decode_browser_descriptors(wire).expect("typed discovery decodes"),
            vec![descriptor]
        );
        assert!(crate::decode_browser_descriptors(serde_json::json!({
            "tools": [{"name":"guessed_tool","description":"missing typed policy"}]
        }))
        .is_err());
    }

    #[test]
    fn typed_execution_decoder_preserves_receipt_or_builds_boundary_receipt() {
        let typed = BrowserToolExecution {
            output_json: "{\"ok\":true}".to_string(),
            receipt: BrowserToolExecutionReceipt {
                capability_id: "tab.list".to_string(),
                execution_id: "browser-receipt".to_string(),
                metadata: serde_json::json!({}),
            },
        };
        let decoded = crate::decode_browser_execution(
            "tab.list",
            serde_json::to_value(&typed).expect("execution serializes"),
        )
        .expect("typed execution decodes");
        assert_eq!(decoded, typed);

        let wrapped = crate::decode_browser_execution("tab.list", serde_json::json!({"tabs": []}))
            .expect("legacy result envelope is wrapped at the transport boundary");
        assert_eq!(wrapped.receipt.capability_id, "tab.list");
        assert_eq!(wrapped.output_json, "{\"tabs\":[]}");
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

    #[test]
    fn routine_run_reports_every_unsupported_flag() {
        let flags = unsupported_routine_run_flags(true, true, true);
        assert_eq!(flags, vec!["--provider", "--model", "--workspace"]);
    }

    #[test]
    fn routine_run_allows_no_flags() {
        assert!(unsupported_routine_run_flags(false, false, false).is_empty());
    }

    #[test]
    fn routine_run_reports_only_the_flags_set() {
        assert_eq!(
            unsupported_routine_run_flags(true, false, true),
            vec!["--provider", "--workspace"]
        );
    }

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
    fn byok_base_url_prefers_maho_specific_env() {
        let selected = select_byok_base_url(
            Some(" http://127.0.0.1:18317/v1 ".to_string()),
            Some("https://proxy.example/v1".to_string()),
        );

        assert_eq!(selected.as_deref(), Some("http://127.0.0.1:18317/v1"));
    }

    #[test]
    fn byok_base_url_falls_back_to_openai_env() {
        let selected = select_byok_base_url(None, Some("http://localhost:9999/v1".to_string()));

        assert_eq!(selected.as_deref(), Some("http://localhost:9999/v1"));
    }

    #[test]
    fn byok_base_url_ignores_empty_values() {
        let selected = select_byok_base_url(
            Some("   ".to_string()),
            Some(" http://localhost:9999/v1 ".to_string()),
        );

        assert_eq!(selected.as_deref(), Some("http://localhost:9999/v1"));
    }

    #[test]
    fn browser_session_invalidation_distinguishes_transport_from_rpc_errors() {
        assert!(crate::browser_session_transport_failed(
            "tools/call browser_navigate failed: io error: broken pipe"
        ));
        assert!(!crate::browser_session_transport_failed(
            "tools/call browser_navigate failed: request timed out after 15s"
        ));
        assert!(!crate::browser_session_transport_failed(
            "tools/call browser_navigate failed: rpc error -32007: active tab lease required"
        ));
        assert!(!crate::browser_session_transport_failed(
            "tools/call browser_navigate failed: rpc error -32000: domain_approval_required"
        ));
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
}

fn decode_browser_descriptors(
    result: serde_json::Value,
) -> Result<Vec<maho_agent::BrowserToolDescriptor>> {
    let tools = result
        .get("tools")
        .cloned()
        .ok_or_else(|| anyhow::anyhow!("browser tools/list response is missing typed tools"))?;
    serde_json::from_value(tools).map_err(|error| {
        anyhow::anyhow!("browser tools/list returned invalid descriptors: {error}")
    })
}

fn decode_browser_execution(
    capability_id: &str,
    result: serde_json::Value,
) -> Result<maho_agent::BrowserToolExecution> {
    if let Ok(execution) =
        serde_json::from_value::<maho_agent::BrowserToolExecution>(result.clone())
    {
        if execution.receipt.capability_id != capability_id {
            bail!("browser execution receipt capability does not match the requested capability");
        }
        return Ok(execution);
    }
    Ok(maho_agent::BrowserToolExecution {
        output_json: serde_json::to_string(&result)?,
        receipt: maho_agent::BrowserToolExecutionReceipt {
            capability_id: capability_id.to_string(),
            execution_id: uuid::Uuid::new_v4().to_string(),
            metadata: serde_json::json!({"source":"maho-cli-browser-transport"}),
        },
    })
}

#[async_trait::async_trait]
impl maho_agent::BrowserToolBridge for CliBrowserToolBridge {
    async fn list_tool_descriptors(
        &self,
    ) -> std::result::Result<
        Vec<maho_agent::BrowserToolDescriptor>,
        maho_agent::BrowserToolBridgeError,
    > {
        let socket_path = self.socket_path.clone();
        let mut client = self.client.lock().await;
        if client.is_none() {
            let connected = browser::connect(socket_path.as_deref())
                .await
                .map_err(|error| {
                    maho_agent::BrowserToolBridgeError::new(
                        "discovery_unavailable",
                        error.to_string(),
                        true,
                    )
                })?;
            *client = Some(Arc::new(connected));
        }
        let connected = client.as_ref().ok_or_else(|| {
            maho_agent::BrowserToolBridgeError::new(
                "discovery_unavailable",
                "browser client initialization produced no client",
                true,
            )
        })?;
        let descriptors = connected
            .call("tools/list", serde_json::json!({}))
            .await
            .map_err(|error| {
                maho_agent::BrowserToolBridgeError::new(
                    "discovery_unavailable",
                    error.to_string(),
                    true,
                )
            })?;
        decode_browser_descriptors(descriptors).map_err(|error| {
            maho_agent::BrowserToolBridgeError::new(
                "discovery_unavailable",
                error.to_string(),
                true,
            )
        })
    }

    async fn execute_tool(
        &self,
        capability_id: &str,
        args_json: &str,
    ) -> std::result::Result<maho_agent::BrowserToolExecution, maho_agent::BrowserToolBridgeError>
    {
        let capability_id = capability_id.to_string();
        let socket_path = self.socket_path.clone();
        let args = serde_json::from_str(args_json).map_err(|error| {
            maho_agent::BrowserToolBridgeError::new("invalid_arguments", error.to_string(), false)
        })?;
        let mut client = self.client.lock().await;
        if client.is_none() {
            let connected = browser::connect(socket_path.as_deref())
                .await
                .map_err(|error| {
                    maho_agent::BrowserToolBridgeError::new(
                        "execution_unavailable",
                        error.to_string(),
                        true,
                    )
                })?;
            *client = Some(Arc::new(connected));
        }
        let result = {
            let connected = client.as_ref().ok_or_else(|| {
                maho_agent::BrowserToolBridgeError::new(
                    "execution_unavailable",
                    "browser client initialization produced no client",
                    true,
                )
            })?;
            browser::send_stateful_tool_call(connected, &capability_id, args).await
        };
        match result {
            Ok(result) => decode_browser_execution(&capability_id, result).map_err(|error| {
                maho_agent::BrowserToolBridgeError::new(
                    "execution_failed",
                    error.to_string(),
                    false,
                )
            }),
            Err(error) => {
                let message = error.to_string();
                if browser_session_transport_failed(&message)
                    || browser::is_transport_ambiguous(&error)
                {
                    *client = None;
                }
                Err(maho_agent::BrowserToolBridgeError::new(
                    "execution_failed",
                    message,
                    false,
                ))
            }
        }
    }
}

async fn build_cli_backend(
    provider: Option<&str>,
    model: Option<&str>,
    workspace: Option<&str>,
    socket_path: Option<&str>,
) -> Result<SwiftideBackend> {
    let db_path = maho_cli::workspace::default_db_path();

    // Resolve target workspace
    let resolved_ws_id = maho_cli::workspace::resolve_workspace_id(&db_path, workspace).await?;

    let storage = maho_cli::workspace::open_storage(&db_path)
        .map_err(|e| anyhow::anyhow!("Failed to open database: {}", e))?;

    let workspaces = storage
        .list_workspaces()
        .map_err(|e| anyhow::anyhow!("Failed to list workspaces: {}", e))?;

    let ws = workspaces
        .into_iter()
        .find(|w| w.id == resolved_ws_id)
        .ok_or_else(|| anyhow::anyhow!("Resolved workspace not found in database"))?;

    let workspace_root = ws
        .workspace_root
        .as_ref()
        .map(PathBuf::from)
        .unwrap_or_else(|| std::env::current_dir().unwrap_or_else(|_| PathBuf::from(".")));

    let mutex_storage = MutexAgentStorage(Arc::new(Mutex::new(storage)));
    let bypass_auth = provider.is_some() || model.is_some();
    let cli_storage = Arc::new(CliAgentStorage {
        inner: mutex_storage,
        bypass_auth,
    });

    let backend = SwiftideBackend::new(
        cli_storage,
        None,
        workspace_root,
        true, // allow insecure key storage for CLI BYOK convenience
    );

    if let Some(space_id) = &ws.space_id {
        backend.set_active_space_id(Some(space_id.clone()));
    }

    // Route `--provider`/`--model` to the correct BYOK client + base URL.
    backend.set_preferred_provider(provider.map(str::to_string));

    // CLI agent sessions are non-interactive and always use AllowAll.
    // Consequential tools and typed approval flows are auto-approved as well;
    // the browser capability sandbox still enforces its independent boundaries.
    backend.set_approval_policy("allow".to_string());

    // Register the running browser as a tool bridge so the agent can drive
    // page/tab tools over the MCP socket during the loop.
    let browser_bridge: Arc<dyn maho_agent::BrowserToolBridge> =
        Arc::new(CliBrowserToolBridge::new(socket_path.map(String::from)));
    backend
        .set_browser_tool_bridge(browser_bridge)
        .map_err(|e| anyhow::anyhow!("Failed to set browser tool bridge: {:?}", e))?;

    // Browser action tools (click / type / navigate / scroll) are disabled by
    // default in the library so embedders must opt in. The CLI is an explicit
    // browser-automation surface, so it always exposes them and its AllowAll
    // approval policy auto-approves their sensitive action contracts. The
    // browser capability sandbox remains the independent enforcement boundary.
    backend.set_browser_action_tools_enabled(true);

    // Without a system prompt the model has no answer contract, so it narrates
    // that it found the information and stops without ever reporting it. State
    // the reporting requirement explicitly.
    if let Some(prompt) = std::env::var("MAHO_AGENT_SYSTEM_PROMPT")
        .ok()
        .filter(|p| !p.is_empty())
    {
        maho_agent::AgentRuntime::set_system_prompt(&backend, &prompt);
    } else {
        maho_agent::AgentRuntime::set_system_prompt(
            &backend,
            "You are Maho's browser agent, driving a real browser through tools.\n\
             Use the browser tools to inspect and act on live pages; never answer \
             a question about page content from memory.\n\
             Your final message must contain the actual answer: quote the exact \
             text, value, or result you observed. Saying that you found or \
             verified something without stating it is a failed response.\n\
             If a task cannot be completed, say precisely what blocked it.",
        );
    }

    // Auto-approve all browser tools and actions by default in maho run (Aside-parity)
    backend
        .set_permission_callback(Box::new(move |_req| {
            Box::pin(async move { PermissionDecision::Allow })
        }))
        .map_err(|e| anyhow::anyhow!("Failed to set permission callback: {:?}", e))?;

    // Resolve BYOK keys and set secure storage callback if provider is requested
    let openai_env = std::env::var("OPENAI_API_KEY").ok();
    let anthropic_env = std::env::var("ANTHROPIC_API_KEY").ok();

    // We open a separate storage handle to read DB keys
    let db_storage = maho_cli::workspace::open_storage(&db_path)?;
    let openai_db = db_storage.get_setting("byok:openai").ok().flatten();
    let anthropic_db = db_storage.get_setting("byok:anthropic").ok().flatten();

    let resolved_openai = openai_env.or(openai_db);
    let resolved_anthropic = anthropic_env.or(anthropic_db);

    // BYOK base-URL override: when set, target an OpenAI-compatible endpoint
    // other than api.openai.com (e.g. a local proxy). `MAHO_BYOK_BASE_URL` is
    // Maho-specific and wins; `OPENAI_BASE_URL` is the conventional OpenAI SDK
    // var and is the fallback. Unset => None => provider default base URL, so
    // existing behavior is unchanged. Only the OpenAI-compatible client consumes
    // base_url (see build_byok_client in swiftide_backend.rs); the Anthropic
    // client builder does not honor it, so we only attach it to openai bundles.
    let byok_base_url = byok_base_url_from_env();

    let provider_override = provider.map(String::from);
    let model_override = model.map(String::from);

    backend
        .set_secure_storage_callback(Box::new(move |provider_req| {
            // Honor the probe argument so the backend can label the resolved key
            // correctly. When an explicit --provider override is set, only the
            // matching probe returns a key; every other probe returns Err. This
            // prevents an Anthropic key from being handed back for an "openai"
            // probe (which previously built an OpenAI client at the OpenAI base URL).
            let normalize = normalize_provider_name;
            let requested = normalize(provider_req);
            if let Some(ref ovr) = provider_override {
                if normalize(ovr) != requested {
                    return Err(AgentError::ExecutionError(format!(
                        "Provider {provider_req} not selected (override is {ovr})"
                    )));
                }
            }
            let key_str = if requested == "openai" {
                resolved_openai.clone()
            } else if requested == "anthropic" {
                resolved_anthropic.clone()
            } else {
                None
            }
            .ok_or_else(|| {
                AgentError::ExecutionError(format!(
                    "No API key found for provider {}",
                    provider_req
                ))
            })?;

            Ok(SecureKeyBundle {
                key: ZeroizedString::new(key_str),
                // Only the OpenAI-compatible client honors base_url; attach the
                // override there so BYOK can target a local proxy instead of
                // api.openai.com. Anthropic ignores it (see build_byok_client).
                base_url: if requested == "openai" {
                    byok_base_url.clone()
                } else {
                    None
                },
                model: model_override.clone(),
            })
        }))
        .map_err(|e| anyhow::anyhow!("Failed to set secure storage callback: {:?}", e))?;

    Ok(backend)
}

async fn cmd_run(
    prompt: &str,
    provider: Option<&str>,
    model: Option<&str>,
    workspace: Option<&str>,
    socket_path: Option<&str>,
    session_id: Option<&str>,
) -> Result<()> {
    let backend = build_cli_backend(provider, model, workspace, socket_path).await?;

    // Run the prompt
    println!("Running autonomous agent session...");
    let response = if let Some(session_id) = session_id {
        backend.run_prompt_in_session(session_id, prompt).await
    } else {
        backend.run_prompt(prompt).await
    }
    .map_err(|e| anyhow::anyhow!("Agent loop failed: {:?}", e))?;

    println!("\n--- Agent Response ---\n{}", response);
    Ok(())
}

/// Bounded deep-research mode (Max/PAYG feature). Drives the existing agent
/// loop under strict step/cost/time caps and emits a cited summary. This CLI
/// surface is BYOK (`--provider`/`--model`): the user brings their own key and
/// cost, so it is treated as entitled. Managed (Maho-billed) deep research runs
/// on the desktop AI surface; the frontier-model gate still applies there.
#[allow(clippy::too_many_arguments)]
async fn cmd_research(
    query: &str,
    provider: Option<&str>,
    model: Option<&str>,
    max_steps: u32,
    budget_usd: f64,
    timeout_secs: u64,
    workspace: Option<&str>,
    socket_path: Option<&str>,
    json_output: bool,
) -> Result<()> {
    if provider.is_none() {
        bail!(
            "`maho research` is BYOK-only: pass --provider (and optionally --model). \
             Managed deep research runs on the desktop AI surface."
        );
    }

    let backend = build_cli_backend(provider, model, workspace, socket_path).await?;
    let runtime: Arc<dyn maho_agent::AgentRuntime> = Arc::new(backend);
    let session_id = format!("research-{}", uuid::Uuid::new_v4());
    let stepper = maho_agent::AgentResearchStepper::new(runtime, session_id, 0.02);

    // BYOK path: entitlement is the user's own key, and managed=false skips the
    // frontier gate (that gate protects Maho-billed managed routing only).
    let policy = maho_agent::ResearchPolicy {
        entitlement: maho_agent::ResearchEntitlement {
            tier: maho_types::account::UserTier::Max,
            payg_credits_usd: 0.0,
        },
        model: model.unwrap_or("byok").to_string(),
        managed: false,
    };
    let budget = maho_agent::ResearchBudget {
        max_steps,
        max_wall_clock: std::time::Duration::from_secs(timeout_secs),
        max_cost_usd: budget_usd,
    };

    let report = maho_agent::run_deep_research(query, &policy, budget, &stepper)
        .await
        .map_err(|e| anyhow::anyhow!("Deep research failed: {}", e))?;

    if json_output {
        let citations: Vec<serde_json::Value> = report
            .citations
            .iter()
            .map(|c| serde_json::json!({ "url": c.url, "note": c.note }))
            .collect();
        let out = serde_json::json!({
            "summary": report.summary,
            "citations": citations,
            "evidence": report.evidence,
            "steps_taken": report.steps_taken,
            "estimated_cost_usd": report.estimated_cost_usd,
            "stop_reason": format!("{:?}", report.stop_reason),
            "model": report.model,
        });
        println!("{}", serde_json::to_string_pretty(&out)?);
    } else {
        println!("\n--- Deep Research Summary ---\n{}", report.summary);
        if !report.citations.is_empty() {
            println!("\n--- Sources ---");
            for c in &report.citations {
                println!("- {}", c.url);
            }
        }
        println!(
            "\n[steps={} est_cost=${:.4} stop={:?} model={}]",
            report.steps_taken, report.estimated_cost_usd, report.stop_reason, report.model
        );
    }
    Ok(())
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
            let tool = find_tool_by_id(&result, &id)?;
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
                     Run it inside a persistent session instead: `maho repl` holds the session \
                     open so the state can be released."
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
            let descriptor = find_tool_by_id(&discovery, &id)?;
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

fn require_non_empty(value: &str, field: &str) -> Result<()> {
    if value.trim().is_empty() {
        bail!("{field} cannot be empty");
    }
    Ok(())
}

fn print_control_error(error: &maho_cli::session_artifact::CliControlError, json_output: bool) {
    if json_output {
        let body = serde_json::json!({
            "ok": false,
            "error": {
                "status": error.status(),
                "message": error.to_string(),
                "data": error.data(),
            }
        });
        eprintln!(
            "{}",
            serde_json::to_string_pretty(&body).unwrap_or_else(|_| error.to_string())
        );
    } else {
        eprintln!("Error [{}]: {}", error.status(), error);
    }
}

async fn connect_control_browser(
    socket_path: Option<&str>,
) -> std::result::Result<
    maho_browser_mcp::client::BrowserClient,
    maho_cli::session_artifact::CliControlError,
> {
    maho_cli::session_artifact::connect(socket_path).await
}

async fn run_control_operation(
    client: &maho_browser_mcp::client::BrowserClient,
    operation: maho_cli::session_artifact::ControlOperation,
    arguments: serde_json::Value,
    json_output: bool,
) -> Result<serde_json::Value> {
    match maho_cli::session_artifact::call(client, operation, arguments).await {
        Ok(value) => Ok(value),
        Err(error) => {
            print_control_error(&error, json_output);
            std::process::exit(1);
        }
    }
}

async fn cmd_session(
    command: SessionCommands,
    _socket_path: Option<&str>,
    json_output: bool,
) -> Result<()> {
    let db_path = maho_cli::workspace::default_db_path();
    let storage = maho_cli::workspace::open_storage(&db_path)
        .map_err(|error| anyhow::anyhow!("Failed to open shared agent storage: {error}"))?;
    let result = match command {
        SessionCommands::List => maho_cli::session_artifact::list_sessions(&storage),
        SessionCommands::Resume { session_id } => {
            require_non_empty(&session_id, "session ID")?;
            maho_cli::session_artifact::resume_session(&storage, &session_id)
                .map_err(anyhow::Error::new)?
        }
        SessionCommands::History { session_id } => {
            require_non_empty(&session_id, "session ID")?;
            maho_cli::session_artifact::session_history(&storage, &session_id)
                .map_err(anyhow::Error::new)?
        }
    };
    print_session_result(&result, json_output)
}

fn print_session_result(result: &serde_json::Value, json_output: bool) -> Result<()> {
    if json_output {
        println!("{}", serde_json::to_string_pretty(result)?);
        return Ok(());
    }
    if let Some(sessions) = result.get("sessions").and_then(serde_json::Value::as_array) {
        if sessions.is_empty() {
            println!("No sessions found.");
        } else {
            println!("{:<24} {:<24} {}", "SESSION", "UPDATED", "TITLE");
            for session in sessions {
                println!(
                    "{:<24} {:<24} {}",
                    session.get("id").and_then(|v| v.as_str()).unwrap_or(""),
                    session
                        .get("updatedAt")
                        .and_then(|v| v.as_str())
                        .unwrap_or(""),
                    session.get("title").and_then(|v| v.as_str()).unwrap_or("")
                );
            }
        }
    } else {
        println!("{}", serde_json::to_string_pretty(result)?);
    }
    Ok(())
}

async fn cmd_artifact(
    command: ArtifactCommands,
    socket_path: Option<&str>,
    json_output: bool,
) -> Result<()> {
    use maho_cli::session_artifact::{validate_export_destination, ControlOperation};

    let (operation, arguments) = match command {
        ArtifactCommands::List { session } => {
            if let Some(session) = session.as_deref() {
                require_non_empty(session, "session ID")?;
            }
            (
                ControlOperation::ArtifactList,
                serde_json::json!({"session_id": session}),
            )
        }
        ArtifactCommands::Export { artifact_id, out } => {
            require_non_empty(&artifact_id, "artifact ID")?;
            let destination = validate_export_destination(&out).map_err(anyhow::Error::new)?;
            (
                ControlOperation::ArtifactExport,
                serde_json::json!({
                    "artifact_id": artifact_id,
                    "destination": destination.to_string_lossy(),
                }),
            )
        }
    };

    let client = match connect_control_browser(socket_path).await {
        Ok(client) => client,
        Err(error) => {
            print_control_error(&error, json_output);
            std::process::exit(1);
        }
    };
    let result = run_control_operation(&client, operation, arguments, json_output).await?;
    print_artifact_result(operation, &result, json_output)
}

fn print_artifact_result(
    operation: maho_cli::session_artifact::ControlOperation,
    result: &serde_json::Value,
    json_output: bool,
) -> Result<()> {
    if json_output {
        println!("{}", serde_json::to_string_pretty(result)?);
        return Ok(());
    }
    match operation {
        maho_cli::session_artifact::ControlOperation::ArtifactList => {
            let artifacts = result
                .get("artifacts")
                .and_then(serde_json::Value::as_array)
                .cloned()
                .unwrap_or_default();
            if artifacts.is_empty() {
                println!("No artifacts found.");
            } else {
                println!(
                    "{:<24} {:<12} {:<10} {}",
                    "ARTIFACT", "SESSION", "BYTES", "NAME"
                );
                for artifact in artifacts {
                    println!(
                        "{:<24} {:<12} {:<10} {}",
                        artifact
                            .get("artifact_id")
                            .and_then(|v| v.as_str())
                            .unwrap_or(""),
                        artifact
                            .get("session_id")
                            .and_then(|v| v.as_str())
                            .unwrap_or(""),
                        artifact
                            .get("size_bytes")
                            .and_then(|v| v.as_u64())
                            .unwrap_or(0),
                        artifact
                            .get("display_name")
                            .and_then(|v| v.as_str())
                            .unwrap_or("")
                    );
                }
            }
        }
        _ => println!("{}", serde_json::to_string_pretty(result)?),
    }
    Ok(())
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
                    Err(e) => serde_json::json!({ "url": url, "ok": false, "error": e.to_string() }),
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
  click <ref>|@<name>   Click by ref, or by name (re-resolved live)
  type <ref> <text>     Type text into element (also: type @<name>=<text>)
  select <ref> <value>  Choose an option by value (also: select @<name>=<value>)
  refs [filter]         List interactive elements: <ref> <role> <name>[=<value>]
  key <name> [mods]     Press a key (e.g. Enter, ArrowDown; mods: shift,ctrl,alt,meta)
  lease [tab_id]        Acquire active-tab lease (auto-acquired before mutations)
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
/// pass the active-tab lease gate. Best-effort.
async fn ensure_active_lease(client: &maho_browser_mcp::client::BrowserClient) -> Result<()> {
    let result = browser::send_tool_call(client, "browser_tab_list", serde_json::json!({})).await?;
    if let Some(tabs) = result.get("tabs").and_then(|t| t.as_array()) {
        if let Some(active) = tabs
            .iter()
            .find(|t| t.get("is_active") == Some(&serde_json::Value::Bool(true)))
        {
            if let Some(id) = active.get("id").and_then(|v| v.as_i64()) {
                let _ = browser::send_tool_call(
                    client,
                    "browser_acquire_lease",
                    serde_json::json!({ "tab_id": id, "ttl_seconds": 300 }),
                )
                .await;
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
            if let Some(tabs) = result.get("tabs").and_then(|t| t.as_array()) {
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
            let tabs = result.get("tabs").and_then(|t| t.as_array());
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
            let result =
                browser::send_tool_call(client, "browser_screenshot_full", serde_json::json!({}))
                    .await?;
            let data = result.get("data").and_then(|v| v.as_str()).unwrap_or("");
            if data.is_empty() {
                println!("No screenshot data received.");
                return Ok(());
            }
            use base64::Engine;
            let bytes = base64::engine::general_purpose::STANDARD.decode(data)?;
            let ts = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs();
            let path = format!("/tmp/maho-repl-{ts}.png");
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
                        serde_json::json!({ "ref": n, "text": text }),
                    )
                    .await?;
                    println!("ok");
                }
                _ => println!("Usage: type <ref> <text> | type @<name>=<text>"),
            }
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
                    let active = result
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
            browser::send_tool_call(
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
