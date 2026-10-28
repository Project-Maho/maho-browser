//! Opt-in, browser-only episodic memory.
//!
//! An episode is one page visit the user chose to remember. It is stored as a
//! regular memory fact (`source = "browse"`, categories `["episode", origin]`)
//! so hybrid retrieval finds it without a second index. Nothing outside the
//! browser is captured, and recording is off until the user enables it.

use maho_storage::sqlite::{MemoryInsertParams, SqliteStorage};
use maho_types::settings::EpisodicMemorySettings;
use sha2::{Digest, Sha256};

use crate::error::CoreError;

pub const EPISODE_SOURCE: &str = "browse";
const MAX_EPISODE_CHARS: usize = 2_000;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct EpisodeInput<'a> {
    pub url: &'a str,
    pub title: &'a str,
    pub summary: &'a str,
    pub is_private_context: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum EpisodeDecision {
    Record { origin: String },
    Skip(SkipReason),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SkipReason {
    Disabled,
    Paused,
    PrivateContext,
    NotWebPage,
    ExcludedDomain,
    SensitiveDomain,
    Empty,
}

pub fn canonical_origin(url: &str) -> Option<String> {
    let parsed = url::Url::parse(url).ok()?;
    if !matches!(parsed.scheme(), "http" | "https") {
        return None;
    }
    let host = parsed.host_str()?.to_ascii_lowercase();
    Some(match parsed.port() {
        Some(port) => format!("{}://{host}:{port}", parsed.scheme()),
        None => format!("{}://{host}", parsed.scheme()),
    })
}

fn registrable_domain(host: &str) -> String {
    psl::domain_str(host).unwrap_or(host).to_ascii_lowercase()
}

const SENSITIVE_HOST_LABELS: &[&str] = &[
    "bank",
    "banking",
    "pay",
    "payment",
    "payments",
    "wallet",
    "checkout",
    "billing",
    "invoice",
    "tax",
    "brokerage",
    "trading",
    "login",
    "signin",
    "auth",
    "accounts",
    "account",
    "sso",
    "id",
    "identity",
    "password",
    "vault",
    "mail",
    "webmail",
];

const SENSITIVE_DOMAINS: &[&str] = &[
    "paypal.com",
    "stripe.com",
    "venmo.com",
    "wise.com",
    "revolut.com",
    "coinbase.com",
    "binance.com",
    "kraken.com",
    "robinhood.com",
    "schwab.com",
    "fidelity.com",
    "vanguard.com",
    "chase.com",
    "wellsfargo.com",
    "bankofamerica.com",
    "citi.com",
    "hsbc.com",
    "barclays.co.uk",
    "kbstar.com",
    "shinhan.com",
    "wooribank.com",
    "hanabank.com",
    "ibk.co.kr",
    "nonghyup.com",
    "kakaobank.com",
    "toss.im",
    "tossbank.com",
    "upbit.com",
    "bithumb.com",
    "hometax.go.kr",
    "1password.com",
    "bitwarden.com",
    "lastpass.com",
    "dashlane.com",
    "okta.com",
    "accounts.google.com",
    "login.microsoftonline.com",
    "appleid.apple.com",
];

/// Credential, payment and finance sites are never recorded.
pub fn is_sensitive_host(host: &str) -> bool {
    let host = host.to_ascii_lowercase();
    let domain = registrable_domain(&host);
    if SENSITIVE_DOMAINS
        .iter()
        .any(|d| host == *d || host.ends_with(&format!(".{d}")) || domain == *d)
    {
        return true;
    }
    host.split('.')
        .flat_map(|label| label.split('-'))
        .any(|part| {
            SENSITIVE_HOST_LABELS.contains(&part)
                || part.ends_with("bank")
                || part.ends_with("banking")
        })
}

pub fn is_excluded_host(host: &str, excluded: &[String]) -> bool {
    let host = host.to_ascii_lowercase();
    excluded.iter().any(|entry| {
        let entry = entry.trim().trim_start_matches("*.").to_ascii_lowercase();
        !entry.is_empty() && (host == entry || host.ends_with(&format!(".{entry}")))
    })
}

/// Every gate, in order. Only `Record` lets an episode reach storage.
pub fn decide(settings: &EpisodicMemorySettings, input: &EpisodeInput<'_>) -> EpisodeDecision {
    if !settings.enabled {
        return EpisodeDecision::Skip(SkipReason::Disabled);
    }
    if settings.paused {
        return EpisodeDecision::Skip(SkipReason::Paused);
    }
    if input.is_private_context {
        return EpisodeDecision::Skip(SkipReason::PrivateContext);
    }
    let Some(origin) = canonical_origin(input.url) else {
        return EpisodeDecision::Skip(SkipReason::NotWebPage);
    };
    let host = url::Url::parse(input.url)
        .ok()
        .and_then(|u| u.host_str().map(str::to_string))
        .unwrap_or_default();
    if is_excluded_host(&host, &settings.excluded_domains) {
        return EpisodeDecision::Skip(SkipReason::ExcludedDomain);
    }
    if is_sensitive_host(&host) {
        return EpisodeDecision::Skip(SkipReason::SensitiveDomain);
    }
    if input.title.trim().is_empty() && input.summary.trim().is_empty() {
        return EpisodeDecision::Skip(SkipReason::Empty);
    }
    EpisodeDecision::Record { origin }
}

fn episode_text(input: &EpisodeInput<'_>) -> String {
    let text = match (input.title.trim(), input.summary.trim()) {
        (title, "") => title.to_string(),
        ("", summary) => summary.to_string(),
        (title, summary) => format!("{title}: {summary}"),
    };
    text.chars()
        .filter(|c| !c.is_control())
        .take(MAX_EPISODE_CHARS)
        .collect()
}

pub fn record_episode(
    storage: &SqliteStorage,
    settings: &EpisodicMemorySettings,
    input: &EpisodeInput<'_>,
) -> Result<Result<String, SkipReason>, CoreError> {
    let origin = match decide(settings, input) {
        EpisodeDecision::Record { origin } => origin,
        EpisodeDecision::Skip(reason) => return Ok(Err(reason)),
    };
    let text = episode_text(input);
    let id = format!(
        "episode_{:x}",
        Sha256::digest(format!("{}\n{text}", input.url).as_bytes())
    );
    let categories = serde_json::json!(["episode", origin]).to_string();
    let metadata = serde_json::json!({ "origin": origin, "url": input.url }).to_string();
    storage
        .insert_memory(MemoryInsertParams {
            id: &id,
            fact: &text,
            source: EPISODE_SOURCE,
            session_id: None,
            categories: &categories,
            importance: 0.3,
            metadata: Some(&metadata),
        })
        .map_err(|e| CoreError::Storage(e.to_string()))?;
    Ok(Ok(id))
}

pub fn forget_origin(storage: &SqliteStorage, site: &str) -> Result<usize, CoreError> {
    let origin = canonical_origin(site)
        .ok_or_else(|| CoreError::Parse(format!("not an http(s) origin: {site}")))?;
    storage
        .delete_episode_facts_for_origin(&origin)
        .map_err(|e| CoreError::Storage(e.to_string()))
}
