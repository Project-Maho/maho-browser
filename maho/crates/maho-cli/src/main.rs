mod auth;
mod byok;
mod chat;
mod credits;
pub mod config;

use anyhow::Result;
use clap::{Parser, Subcommand};

use config::CliConfig;

const DEFAULT_PROXY_URL: &str = "https://proxy.maho.co/v1";
const DEFAULT_RELAY_URL: &str = "https://relay.maho.co";

#[derive(Parser)]
#[command(name = "maho", version, about = "Maho CLI — chat with AI from your terminal")]
struct Cli {
    /// Override the billing proxy URL
    #[arg(long, global = true, default_value = DEFAULT_PROXY_URL)]
    proxy_url: String,

    /// Override the relay URL
    #[arg(long, global = true, default_value = DEFAULT_RELAY_URL)]
    relay_url: String,

    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Log in to Maho (or sign up if no account)
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
    /// Sign up for a new Maho account
    Signup {
        /// Email (non-interactive; skips prompt)
        #[arg(long)]
        email: Option<String>,

        /// Password (non-interactive; skips prompt)
        #[arg(long)]
        password: Option<String>,
    },
    /// Log out and wipe stored credentials
    Logout,
    /// Show current user info
    Whoami,
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
    /// Manage CLI configuration
    Config {
        #[command(subcommand)]
        action: ConfigAction,
    },
    /// Show current PAYG credit balance
    Balance,
    /// Open LemonSqueezy checkout in browser to buy a credit pack
    BuyCredits {
        /// Amount in USD (10, 50, or 100)
        #[arg(long, value_parser = ["10", "50", "100"])]
        amount: String,
    },
}

#[derive(Subcommand)]
enum ConfigAction {
    /// Show current config (tokens redacted)
    Show,
}

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::from_default_env(),
        )
        .with_target(false)
        .init();

    let cli = Cli::parse();

    match cli.command {
        Commands::Login {
            signup,
            email,
            password,
        } => cmd_login(&cli.relay_url, signup, email, password).await,
        Commands::Signup { email, password } => cmd_login(&cli.relay_url, true, email, password).await,
        Commands::Logout => cmd_logout(),
        Commands::Whoami => cmd_whoami(&cli.relay_url).await,
        Commands::Chat {
            message,
            byok,
            api_key,
        } => {
            if let (Some(provider), Some(key)) = (byok, api_key) {
                byok::send_byok(&provider, &key, &message).await
            } else {
                let cfg = CliConfig::load()?;
                chat::send_chat(&cli.proxy_url, &cfg, &message).await
            }
        }
        Commands::Config { action } => match action {
            ConfigAction::Show => cmd_config_show(),
        },
        Commands::Balance => cmd_balance(&cli.relay_url).await,
        Commands::BuyCredits { amount } => cmd_buy_credits(&amount),
    }
}

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
