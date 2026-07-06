mod auth;
mod browser;
mod byok;
mod chat;
mod credits;
pub mod config;

use anyhow::{Context, Result, bail};
use clap::{Args, Parser, Subcommand};

use config::CliConfig;

const DEFAULT_PROXY_URL: &str = "https://proxy.maho.co/v1";
const DEFAULT_RELAY_URL: &str = "https://relay.maho.co";

#[derive(Parser)]
#[command(name = "maho", version, about = "Maho CLI — browser control and AI from your terminal")]
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

    /// Run as stdio MCP server
    Mcp,

    /// Show version info
    Version,

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

// ─── Subcommand enums ─────────────────────────────────────────────────────────

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

// ─── Stub exit helper ─────────────────────────────────────────────────────────

fn stub_not_implemented(task: &str) -> Result<()> {
    eprintln!("not implemented yet — {task}");
    std::process::exit(2);
}

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
        Commands::History { command } => cmd_history(command, socket_path.as_deref(), json_output).await,

        // ─── Bookmarks group ──────────────────────────────────────────────────
        Commands::Bookmarks { command } => cmd_bookmarks(command, socket_path.as_deref(), json_output).await,

        // ─── Page group (stubs — Task 3.2) ────────────────────────────────
        Commands::Page { .. } => stub_not_implemented("Task 3.2"),

        // ─── Headless ─────────────────────────────────────────────────────
        Commands::Headless(args) => cmd_headless(args, socket_path.as_deref(), json_output).await,

        // ─── Browser group (stub — Task 3.4) ──────────────────────────────
        Commands::Browser { command } => match command {
            BrowserCommands::Repl => cmd_repl(socket_path.as_deref(), json_output).await,
        },

        // ─── MCP server (stub — Task 5.x) ────────────────────────────────
        Commands::Mcp => stub_not_implemented("Task 5.x"),

        // ─── Version ──────────────────────────────────────────────────────
        Commands::Version => {
            println!("maho {}", env!("CARGO_PKG_VERSION"));
            Ok(())
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
        "Last purchased: {}",
        format_unix_ts(bal.last_purchase_at)
    );
    println!("Last used:      {}", format_unix_ts(bal.last_consumption_at));
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
            let result = browser::send_tool_call(&client, "browser_tab_list", serde_json::json!({})).await?;
            let tabs = result.get("tabs").and_then(|t| t.as_array());
            let active = tabs
                .and_then(|arr| arr.iter().find(|t| t.get("is_active") == Some(&serde_json::Value::Bool(true))));

            match active {
                Some(tab) => {
                    if json_output {
                        println!("{}", serde_json::to_string_pretty(tab)?);
                    } else {
                        println!("id: {}", tab.get("id").and_then(|v| v.as_i64()).unwrap_or(0));
                        println!("title: {}", tab.get("title").and_then(|v| v.as_str()).unwrap_or(""));
                        println!("url: {}", tab.get("url").and_then(|v| v.as_str()).unwrap_or(""));
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
            let result = browser::send_tool_call(&client, "browser_tab_list", serde_json::json!({})).await?;

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
                    let active = tab.get("is_active").and_then(|v| v.as_bool()).unwrap_or(false);
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
            ).await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!("id: {}", result.get("id").and_then(|v| v.as_i64()).unwrap_or(0));
                println!("title: {}", result.get("title").and_then(|v| v.as_str()).unwrap_or(""));
                println!("url: {}", result.get("url").and_then(|v| v.as_str()).unwrap_or(""));
                println!("active: {}", result.get("is_active").and_then(|v| v.as_bool()).unwrap_or(false));
            }
            Ok(())
        }
        TabCommands::Close { tab_id } => {
            let client = browser::connect(socket_path).await?;
            let result = browser::send_tool_call(
                &client,
                "browser_tab_close",
                serde_json::json!({ "tab_id": tab_id.parse::<i64>().unwrap_or(0) }),
            ).await?;

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
                let tab_url = tab.get("url").and_then(|v| v.as_str()).unwrap_or("about:blank");
                println!("New tab {id}: {tab_url}");
            }
            Ok(())
        }
        TabCommands::Navigate { tab_id, url } => {
            let client = browser::connect(socket_path).await?;
            let result = browser::send_tool_call(
                &client,
                "browser_navigate",
                serde_json::json!({ "tab_id": tab_id.parse::<i64>().unwrap_or(0), "url": url }),
            ).await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!("Navigated tab {} to {}", tab_id, url);
            }
            Ok(())
        }
        TabCommands::Read { format, selection, tab } => {
            let client = browser::connect(socket_path).await?;
            let tool = if selection { "browser_page_text" } else { "browser_page_content" };
            let mut args = serde_json::json!({ "format": format });
            if let Some(t) = &tab {
                args["tab_id"] = serde_json::Value::Number(
                    serde_json::Number::from(t.parse::<i64>().unwrap_or(0)),
                );
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
            let client = browser::connect(socket_path).await?;
            let mut input = String::new();
            std::io::Read::read_to_string(&mut std::io::stdin(), &mut input)?;
            let mut args = serde_json::json!({ "content": input });
            if let Some(t) = &title {
                args["title"] = serde_json::Value::String(t.clone());
            }
            if let Some(t) = &tab {
                args["tab_id"] = serde_json::Value::Number(
                    serde_json::Number::from(t.parse::<i64>().unwrap_or(0)),
                );
            }
            let result = browser::send_tool_call(&client, "browser_tab_annotate", args).await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!("Annotation saved.");
            }
            Ok(())
        }
        TabCommands::Write { format, tab } => {
            let client = browser::connect(socket_path).await?;
            let mut input = String::new();
            std::io::Read::read_to_string(&mut std::io::stdin(), &mut input)?;
            let mut args = serde_json::json!({ "content": input, "format": format });
            if let Some(t) = &tab {
                args["tab_id"] = serde_json::Value::Number(
                    serde_json::Number::from(t.parse::<i64>().unwrap_or(0)),
                );
            }
            let result = browser::send_tool_call(&client, "browser_tab_write", args).await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!("Content written to tab.");
            }
            Ok(())
        }
        TabCommands::LeaseAcquire { ttl, tab } => {
            let client = browser::connect(socket_path).await?;
            let mut args = serde_json::json!({ "ttl": ttl });
            if let Some(t) = &tab {
                args["tab_id"] = serde_json::Value::Number(
                    serde_json::Number::from(t.parse::<i64>().unwrap_or(0)),
                );
            }
            let result = browser::send_tool_call(&client, "browser_tab_lease_acquire", args).await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                let lease_id = result.get("lease_id").and_then(|v| v.as_str()).unwrap_or("unknown");
                println!("Lease acquired: {lease_id}");
            }
            Ok(())
        }
        TabCommands::LeaseRelease { tab } => {
            let client = browser::connect(socket_path).await?;
            let mut args = serde_json::json!({});
            if let Some(t) = &tab {
                args["tab_id"] = serde_json::Value::Number(
                    serde_json::Number::from(t.parse::<i64>().unwrap_or(0)),
                );
            }
            let result = browser::send_tool_call(&client, "browser_tab_lease_release", args).await?;

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
                args["tab_id"] = serde_json::Value::Number(
                    serde_json::Number::from(t.parse::<i64>().unwrap_or(0)),
                );
            }
            let result = browser::send_tool_call(&client, "browser_tab_lease_heartbeat", args).await?;

            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!("Heartbeat sent.");
            }
            Ok(())
        }
    }
}

// ─── History command handlers ─────────────────────────────────────────────────

async fn cmd_history(command: HistoryCommands, socket_path: Option<&str>, json_output: bool) -> Result<()> {
    match command {
        HistoryCommands::List { since, domain, limit, .. } => {
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
                "limit": limit.unwrap_or(50),
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
                            let ts = entry.get("visited_at").and_then(|v| v.as_str()).unwrap_or("");
                            let title = entry.get("title").and_then(|v| v.as_str()).unwrap_or("");
                            let url = entry.get("url").and_then(|v| v.as_str()).unwrap_or("");
                            println!("  {ts} {title}");
                            println!("  {url}");
                        }
                    }
                } else {
                    for entry in entries {
                        let ts = entry.get("visited_at").and_then(|v| v.as_str()).unwrap_or("");
                        let title = entry.get("title").and_then(|v| v.as_str()).unwrap_or("");
                        let url = entry.get("url").and_then(|v| v.as_str()).unwrap_or("");
                        println!("{ts}  {title}");
                        println!("  {url}");
                    }
                }
            }
            Ok(())
        }
        HistoryCommands::Search { url_substring, title_substring, since, .. } => {
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
                    let ts = entry.get("visited_at").and_then(|v| v.as_str()).unwrap_or("");
                    let title = entry.get("title").and_then(|v| v.as_str()).unwrap_or("");
                    let url = entry.get("url").and_then(|v| v.as_str()).unwrap_or("");
                    println!("{ts}  {title}");
                    println!("  {url}");
                }
            }
            Ok(())
        }
        _ => stub_not_implemented("Task 3.2 — history export/import"),
    }
}

// ─── Bookmarks command handlers ───────────────────────────────────────────────

async fn cmd_bookmarks(command: BookmarkCommands, socket_path: Option<&str>, json_output: bool) -> Result<()> {
    match command {
        BookmarkCommands::List { .. } => {
            let client = browser::connect(socket_path).await?;
            let result = browser::send_tool_call(
                &client,
                "browser_bookmarks_search",
                serde_json::json!({ "query": "" }),
            ).await?;

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
            ).await?;

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
            ).await?;

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

// ─── Headless command handler ─────────────────────────────────────────────────

fn find_maho_binary() -> Option<std::path::PathBuf> {
    if let Ok(p) = std::env::var("MAHO_APP_PATH") {
        let path = std::path::PathBuf::from(&p);
        if path.exists() {
            return Some(path);
        }
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
            .args(["--headless=new", "--no-first-run", "--no-default-browser-check"])
            .stdin(std::process::Stdio::null())
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .spawn()
            .with_context(|| format!("failed to spawn browser: {}", binary.display()))?;
        Ok(Self { child })
    }

    fn terminate(&mut self) {
        use nix::sys::signal::{Signal, kill};
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

async fn wait_for_socket(socket_path: Option<&str>, timeout_ms: u64) -> Result<maho_browser_mcp::client::BrowserClient> {
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
    bail!("browser socket did not appear within {}s", effective_timeout / 1000)
}

async fn cmd_headless(args: HeadlessArgs, socket_path: Option<&str>, _json_output: bool) -> Result<()> {
    let url = match &args.url {
        Some(u) => u.clone(),
        None => {
            if args.batch.is_some() {
                return stub_not_implemented("Task 4.3 — batch headless");
            }
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

async fn run_headless_extraction(
    client: &maho_browser_mcp::client::BrowserClient,
    url: &str,
    args: &HeadlessArgs,
) -> Result<()> {
    let new_tab_result = browser::send_tool_call(
        client,
        "browser_tab_new",
        serde_json::json!({ "url": url }),
    ).await?;

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
        ).await?;
        let status = tab_info.get("status").and_then(|s| s.as_str()).unwrap_or("complete");
        if status != "loading" {
            loaded = true;
            break;
        }
    }

    if !loaded {
        eprintln!("Warning: page did not finish loading within 10s");
    }

    let extractors: Vec<&str> = args.extract.split(',').map(|s| s.trim()).collect();
    for extractor in &extractors {
        match *extractor {
            "title" => {
                let info = browser::send_tool_call(
                    client,
                    "browser_tab_get",
                    serde_json::json!({ "tab_id": tab_id }),
                ).await?;
                let title = info.get("title").and_then(|v| v.as_str()).unwrap_or("");
                println!("{title}");
            }
            "text" => {
                let result = browser::send_tool_call(
                    client,
                    "browser_page_text",
                    serde_json::json!({ "tab_id": tab_id }),
                ).await?;
                let text = result.get("text").and_then(|v| v.as_str()).unwrap_or("");
                println!("{text}");
            }
            "links" => {
                let result = browser::send_tool_call(
                    client,
                    "browser_page_context",
                    serde_json::json!({ "tab_id": tab_id }),
                ).await?;
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
                ).await?;
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
                ).await?;
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
    ).await;

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

async fn handle_repl_line(client: &maho_browser_mcp::client::BrowserClient, line: &str) -> Result<()> {
    let parts: Vec<&str> = line.splitn(3, ' ').collect();
    let cmd = parts[0];

    match cmd {
        "help" => {
            print!("{REPL_HELP}");
        }
        "tabs" => {
            let result = browser::send_tool_call(client, "browser_tab_list", serde_json::json!({})).await?;
            if let Some(tabs) = result.get("tabs").and_then(|t| t.as_array()) {
                println!("{:<4} {:<40} {:<60} {}", "ID", "TITLE", "URL", "ACTIVE");
                for tab in tabs {
                    let id = tab.get("id").and_then(|v| v.as_i64()).unwrap_or(0);
                    let title = tab.get("title").and_then(|v| v.as_str()).unwrap_or("");
                    let url = tab.get("url").and_then(|v| v.as_str()).unwrap_or("");
                    let active = tab.get("is_active").and_then(|v| v.as_bool()).unwrap_or(false);
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
            let result = browser::send_tool_call(client, "browser_tab_list", serde_json::json!({})).await?;
            let tabs = result.get("tabs").and_then(|t| t.as_array());
            let active = tabs
                .and_then(|arr| arr.iter().find(|t| t.get("is_active") == Some(&serde_json::Value::Bool(true))));
            if let Some(tab) = active {
                println!("id: {}", tab.get("id").and_then(|v| v.as_i64()).unwrap_or(0));
                println!("title: {}", tab.get("title").and_then(|v| v.as_str()).unwrap_or(""));
                println!("url: {}", tab.get("url").and_then(|v| v.as_str()).unwrap_or(""));
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
            browser::send_tool_call(client, "browser_navigate", serde_json::json!({ "url": *url })).await?;
            println!("ok");
        }
        "text" => {
            let result = browser::send_tool_call(client, "browser_page_text", serde_json::json!({})).await?;
            let text = result.get("text").and_then(|v| v.as_str()).unwrap_or("");
            println!("{}", browser::truncate(text, 2000));
        }
        "screenshot" => {
            let result = browser::send_tool_call(client, "browser_screenshot_full", serde_json::json!({})).await?;
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
            let result = browser::send_tool_call(client, "browser_accessibility_snapshot", serde_json::json!({})).await?;
            println!("{}", serde_json::to_string_pretty(&result)?);
        }
        "click" => {
            let ref_str = parts.get(1).unwrap_or(&"");
            let ref_num: i64 = ref_str.parse().unwrap_or(-1);
            if ref_num < 0 {
                println!("Usage: click <ref>");
                return Ok(());
            }
            browser::send_tool_call(client, "browser_click", serde_json::json!({ "ref": ref_num })).await?;
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
            browser::send_tool_call(client, "browser_type", serde_json::json!({ "ref": ref_num, "text": *text })).await?;
            println!("ok");
        }
        other => {
            println!("unknown command: {other}");
            println!("Type 'help' for available commands.");
        }
    }
    Ok(())
}
