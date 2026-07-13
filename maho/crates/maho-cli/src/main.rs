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
use maho_storage::sqlite::SqliteStorage;
use maho_types::chat::ChatMessage;
use std::path::PathBuf;
use std::sync::Arc;
use std::sync::Mutex;

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

    /// Show version info
    Version,

    /// Run an autonomous AI agent session
    Run {
        /// The prompt to execute
        prompt: String,

        /// LLM provider (e.g. anthropic, gemini, claude)
        #[arg(long)]
        provider: Option<String>,

        /// LLM model name override
        #[arg(long)]
        model: Option<String>,

        /// Bypass all tool gating (approve all tools automatically)
        #[arg(long)]
        yolo: bool,

        /// Allow external MCP tools to run (opt-in)
        #[arg(long, name = "allow-tools")]
        allow_tools: bool,

        /// Workspace ID to target
        #[arg(long)]
        workspace: Option<String>,
    },

    /// Tool execution and inspection
    Tool {
        #[command(subcommand)]
        command: ToolCommands,
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
pub enum ToolCommands {
    /// List all available tools from the browser MCP server
    List,

    /// Call a tool by name with JSON arguments
    Call {
        /// Tool name to call
        name: String,

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

        /// Auto-approve all tool calls without confirmation prompt
        #[arg(long)]
        yolo: bool,

        /// Allow all MCP/extension tools
        #[arg(long, name = "allow-tools")]
        allow_tools: bool,

        /// Workspace ID or name override
        #[arg(long)]
        workspace: Option<String>,
    },
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
                provider: _,
                model: _,
                yolo: _,
                allow_tools: _,
                workspace: _,
            } => cmd_routine_run(&name, socket_path.as_deref(), json_output).await,
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
        } => {
            cmd_run(
                &prompt,
                provider.as_deref(),
                model.as_deref(),
                yolo,
                allow_tools,
                workspace.as_deref(),
                socket_path.as_deref(),
            )
            .await
        }

        Commands::Tool { command } => cmd_tool(command, socket_path.as_deref(), json_output).await,

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
    println!("Last purchased: {}", format_unix_ts(bal.last_purchase_at));
    println!(
        "Last used:      {}",
        format_unix_ts(bal.last_consumption_at)
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

fn format_unix_ts(ts: Option<i64>) -> String {
    match ts {
        None => "never".to_string(),
        Some(secs) => {
            let h = (secs / 3600) % 24;
            let m = (secs / 60) % 60;
            let days_since_epoch = secs / 86400;
            format_date_from_epoch_days(days_since_epoch, h as u8, m as u8)
        }
    }
}

fn format_date_from_epoch_days(days: i64, hour: u8, min: u8) -> String {
    let mut y = 1970i32;
    let mut d = days;
    loop {
        let dy = if is_leap(y) { 366 } else { 365 };
        if d < dy {
            break;
        }
        d -= dy;
        y += 1;
    }
    let months = if is_leap(y) {
        [31i64, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    } else {
        [31i64, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    };
    let mut month = 1u8;
    for &mlen in &months {
        if d < mlen {
            break;
        }
        d -= mlen;
        month += 1;
    }
    let day = d + 1;
    format!("{y}-{month:02}-{day:02} {hour:02}:{min:02} UTC")
}

fn is_leap(y: i32) -> bool {
    (y % 4 == 0 && y % 100 != 0) || y % 400 == 0
}

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
            let prior = if let Some(ref u) = url {
                Some(ensure_domain_allowed(&client, u).await?)
            } else {
                None
            };
            let mut args = serde_json::json!({});
            if let Some(u) = &url {
                args["url"] = serde_json::Value::String(u.clone());
            }
            if let Some(s) = &space {
                args["space"] = serde_json::Value::String(s.clone());
            }
            let result = browser::send_tool_call(&client, "browser_tab_new", args).await;

            if let Some(Some(prior_list)) = prior {
                let _ = browser::send_tool_call(
                    &client,
                    "browser_set_allowed_domains",
                    serde_json::json!({ "domains": prior_list }),
                )
                .await;
            }

            let result = result?;

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
            let prior = ensure_domain_allowed(&client, &url).await?;
            let result = browser::send_tool_call(
                &client,
                "browser_navigate",
                serde_json::json!({ "tab_id": tab_id.parse::<i64>().unwrap_or(0), "url": url }),
            )
            .await;

            if let Some(prior_list) = prior {
                let _ = browser::send_tool_call(
                    &client,
                    "browser_set_allowed_domains",
                    serde_json::json!({ "domains": prior_list }),
                )
                .await;
            }

            let result = result?;

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

fn get_host_from_url(url: &str) -> Option<&str> {
    let s = url
        .strip_prefix("http://")
        .or_else(|| url.strip_prefix("https://"))?;
    let host_part = s.split('/').next()?;
    let host_only = host_part.split(':').next()?;
    Some(host_only)
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

async fn ensure_domain_allowed(
    client: &maho_browser_mcp::client::BrowserClient,
    url_str: &str,
) -> Result<Option<Vec<String>>> {
    if let Some(host) = get_host_from_url(url_str) {
        let host_string = host.to_string();
        let wildcard = format!("*.{host}");

        // 1. Get prior allowed domains
        let get_resp =
            browser::send_tool_call(client, "browser_get_allowed_domains", serde_json::json!({}))
                .await;

        let prior_domains = match get_resp {
            Ok(val) => {
                if let Some(domains_val) = val.get("domains") {
                    if let Some(arr) = domains_val.as_array() {
                        let list: Vec<String> = arr
                            .iter()
                            .filter_map(|v| v.as_str().map(|s| s.to_string()))
                            .collect();
                        Some(list)
                    } else {
                        None
                    }
                } else {
                    None
                }
            }
            Err(_) => None,
        };

        // 2. Append new domains additively
        let mut new_domains = prior_domains.clone().unwrap_or_default();
        if !new_domains.contains(&host_string) {
            new_domains.push(host_string);
        }
        if !new_domains.contains(&wildcard) {
            new_domains.push(wildcard);
        }

        // 3. Set allowed domains
        browser::send_tool_call(
            client,
            "browser_set_allowed_domains",
            serde_json::json!({ "domains": new_domains }),
        )
        .await?;

        return Ok(prior_domains);
    }
    Ok(None)
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

/// Bridges the agent's browser tool shims to the running browser's MCP server
/// over the UDS socket. The agent-facing shim names differ from the browser's
/// `browser_*` MCP tool names, so they are mapped here before dispatch.
struct CliBrowserToolBridge {
    socket_path: Option<String>,
}

fn map_browser_tool_name(name: &str) -> &str {
    match name {
        "list_browser_tabs" => "browser_tab_list",
        "read_current_page" => "browser_page_content",
        "get_selected_text" => "browser_page_text",
        "get_active_tab" => "browser_tab_get",
        "search_in_page" => "browser_search_in_page",
        "extract_structured_page_context" => "browser_page_context",
        other => other,
    }
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

#[cfg(test)]
mod tests {
    use super::{map_browser_tool_name, select_byok_base_url};

    #[test]
    fn browser_tool_mapping_routes_tab_list_to_mcp_tool_name() {
        assert_eq!(
            map_browser_tool_name("list_browser_tabs"),
            "browser_tab_list"
        );
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
}

impl maho_agent::BrowserToolBridge for CliBrowserToolBridge {
    fn invoke(&self, tool_name: &str, args_json: &str) -> Result<String, String> {
        let mapped = map_browser_tool_name(tool_name).to_string();
        let socket_path = self.socket_path.clone();
        let args: serde_json::Value =
            serde_json::from_str(args_json).unwrap_or_else(|_| serde_json::json!({}));
        let rt = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .map_err(|e| format!("failed to build runtime for browser tool: {e}"))?;
        rt.block_on(async move {
            let client = browser::connect(socket_path.as_deref())
                .await
                .map_err(|e| e.to_string())?;
            let result = browser::send_tool_call(&client, &mapped, args)
                .await
                .map_err(|e| e.to_string())?;
            serde_json::to_string(&result).map_err(|e| e.to_string())
        })
    }
}

async fn cmd_run(
    prompt: &str,
    provider: Option<&str>,
    model: Option<&str>,
    yolo: bool,
    allow_tools: bool,
    workspace: Option<&str>,
    socket_path: Option<&str>,
) -> Result<()> {
    let db_path = maho_cli::workspace::default_db_path();

    // Resolve target workspace
    let resolved_ws_id = maho_cli::workspace::resolve_workspace_id(&db_path, workspace).await?;

    let storage = SqliteStorage::open(&db_path.to_string_lossy())
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
    backend.set_preferred_provider(provider.map(String::from));

    // Non-interactive approval policy, injected before any tool call:
    //   --yolo         => allow-all
    //   --allow-tools  => allow mcp: prefixed tools (deny other sensitive)
    //   default        => deny sensitive tools (non-sensitive auto-run)
    let approval_policy = if yolo {
        "allow"
    } else if allow_tools {
        "allow-mcp"
    } else {
        "deny-sensitive"
    };
    backend.set_approval_policy(approval_policy.to_string());

    // Register the running browser as a tool bridge so the agent can drive
    // page/tab tools over the MCP socket during the loop.
    let browser_bridge: Arc<dyn maho_agent::BrowserToolBridge> = Arc::new(CliBrowserToolBridge {
        socket_path: socket_path.map(String::from),
    });
    backend
        .set_browser_tool_bridge(browser_bridge)
        .map_err(|e| anyhow::anyhow!("Failed to set browser tool bridge: {:?}", e))?;

    // Set permission callback based on yolo and allow_tools flags
    backend
        .set_permission_callback(Box::new(move |req| {
            let is_mcp = req.tool_name.starts_with("mcp:");
            let decision = if yolo {
                PermissionDecision::Allow
            } else if is_mcp && allow_tools {
                PermissionDecision::Allow
            } else {
                PermissionDecision::Deny
            };
            Box::pin(async move { decision })
        }))
        .map_err(|e| anyhow::anyhow!("Failed to set permission callback: {:?}", e))?;

    // Resolve BYOK keys and set secure storage callback if provider is requested
    let openai_env = std::env::var("OPENAI_API_KEY").ok();
    let anthropic_env = std::env::var("ANTHROPIC_API_KEY").ok();

    // We open a separate storage handle to read DB keys
    let db_storage = SqliteStorage::open(&db_path.to_string_lossy())?;
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

    // Run the prompt
    println!("Running autonomous agent session...");
    let response = backend
        .run_prompt(prompt)
        .await
        .map_err(|e| anyhow::anyhow!("Agent loop failed: {:?}", e))?;

    println!("\n--- Agent Response ---\n{}", response);
    Ok(())
}

async fn cmd_tool(
    command: ToolCommands,
    socket_path: Option<&str>,
    json_output: bool,
) -> Result<()> {
    match command {
        ToolCommands::List => {
            let client = match browser::connect(socket_path).await {
                Ok(c) => c,
                Err(e) => {
                    bail!("Could not connect to browser to list tools: {}", e);
                }
            };

            let result = client.call("tools/list", serde_json::json!({})).await?;
            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                if let Some(tools) = result.get("tools").and_then(|t| t.as_array()) {
                    for tool in tools {
                        let name = tool.get("name").and_then(|v| v.as_str()).unwrap_or("");
                        let desc = tool
                            .get("description")
                            .and_then(|v| v.as_str())
                            .unwrap_or("");
                        println!("{}: {}", name, desc);
                    }
                } else {
                    println!("No tools found.");
                }
            }
            Ok(())
        }
        ToolCommands::Call { name, args } => {
            // Session-binding tools acquire browser-side state (a lease, an
            // in-flight network capture) that must be released by the same
            // session that created it. A one-shot `maho tool call` exits right
            // after the call, orphaning that state — a leaked lease blocks other
            // clients, an in-flight capture never stops. Reject BEFORE connecting
            // or executing anything, regardless of whether the browser is up.
            if maho_cli::STATEFUL_TOOLS.iter().any(|&t| t == name) {
                eprintln!(
                    "Error: '{name}' is a session-binding tool and cannot be run as a one-shot \
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
                    bail!(
                        "Failed to connect to browser to call tool '{}': {}",
                        name,
                        e
                    );
                }
            };

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

    let storage = SqliteStorage::open(&db_path.to_string_lossy())
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

    let storage = SqliteStorage::open(&db_path.to_string_lossy())
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

    let storage = SqliteStorage::open(&db_path.to_string_lossy())
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
    // Route the run through MCP against the RUNNING browser's maho_core routines
    // engine (the SAME engine the WebUI drives), rather than opening a private
    // SqliteStorage + constructing a second local engine. The browser owns tier
    // gating and execution; a tier-locked routine comes back as a structured
    // JSON-RPC error that we map to the clean CLI tier-lock error + exit code 2.
    let client = browser::connect(socket_path).await?;

    println!("Running routine '{}'...", name);
    let args = serde_json::json!({ "id": name });
    match browser::send_tool_call(&client, "browser_routines_run", args).await {
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
        Err(e) => {
            // Full anyhow chain (includes the browser's RPC error message).
            let msg = format!("{e:#}");
            // Preserve the tier-lock CLEAN error: Max-tier gate → exit code 2.
            if msg.contains("Max tier") {
                eprintln!("Error: Routines are available on Max tier only");
                std::process::exit(2);
            }
            // Any other browser-side failure (not found, execution error) →
            // clean, non-zero, non-tier-gate exit.
            eprintln!("Error: {msg}");
            std::process::exit(1);
        }
    }
}

// ─── Headless command handler ─────────────────────────────────────────────────

fn find_maho_binary() -> Option<std::path::PathBuf> {
    if let Ok(p) = std::env::var("MAHO_APP_PATH") {
        let path = std::path::PathBuf::from(&p);
        if path.exists() {
            return Some(path);
        }
        // Stale override: fall through to defaults rather than hard-failing.
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
    let prior = ensure_domain_allowed(client, url).await?;
    let new_tab_result =
        browser::send_tool_call(client, "browser_tab_new", serde_json::json!({ "url": url })).await;

    if let Some(prior_list) = prior {
        let _ = browser::send_tool_call(
            client,
            "browser_set_allowed_domains",
            serde_json::json!({ "domains": prior_list }),
        )
        .await;
    }

    let new_tab_result = new_tab_result?;

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
    let sem = std::sync::Arc::new(tokio::sync::Semaphore::new(concurrency));
    let extract = std::sync::Arc::new(args.extract.clone());
    let socket_owned: Option<String> = socket_path.map(|s| s.to_string());

    let mut handles = Vec::new();
    for url in urls {
        let sem = std::sync::Arc::clone(&sem);
        let writer = std::sync::Arc::clone(&writer);
        let extract = std::sync::Arc::clone(&extract);
        let socket_owned = socket_owned.clone();
        handles.push(tokio::spawn(async move {
            let _permit = sem.acquire().await.expect("semaphore closed unexpectedly");
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
  click <ref>           Click element by ref number
  type <ref> <text>     Type text into element
  help                  Show this help
  .quit / .exit         Exit REPL
";

async fn cmd_repl(socket_path: Option<&str>, _json_output: bool) -> Result<()> {
    let client = browser::connect(socket_path).await?;

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
            browser::send_tool_call(
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
        "click" => {
            let ref_str = parts.get(1).unwrap_or(&"");
            let ref_num: i64 = ref_str.parse().unwrap_or(-1);
            if ref_num < 0 {
                println!("Usage: click <ref>");
                return Ok(());
            }
            browser::send_tool_call(
                client,
                "browser_click",
                serde_json::json!({ "ref": ref_num }),
            )
            .await?;
            println!("ok");
        }
        "type" => {
            let ref_str = parts.get(1).unwrap_or(&"");
            let text = parts.get(2).unwrap_or(&"");
            let ref_num: i64 = ref_str.parse().unwrap_or(-1);
            if ref_num < 0 || text.is_empty() {
                println!("Usage: type <ref> <text>");
                return Ok(());
            }
            browser::send_tool_call(
                client,
                "browser_type",
                serde_json::json!({ "ref": ref_num, "text": *text }),
            )
            .await?;
            println!("ok");
        }
        other => {
            println!("unknown command: {other}");
            println!("Type 'help' for available commands.");
        }
    }
    Ok(())
}
