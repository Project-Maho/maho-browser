#[path = "cli_entry.rs"]
mod cli_entry;

use anyhow::{bail, Result};
use reqwest::Client;
use serde::{Deserialize, Serialize};

use crate::config::CliConfig;

#[derive(Serialize)]
struct AuthRequest {
    email: String,
    password: String,
}

#[derive(Deserialize)]
pub struct AuthResponse {
    pub access_token: String,
    pub refresh_token: String,
}

#[derive(Deserialize)]
pub struct PublicUser {
    pub id: i32,
    pub email: String,
}

#[derive(Deserialize)]
pub struct MeResponse {
    #[serde(default)]
    pub account: Option<PublicUser>,
    #[serde(default)]
    pub email: Option<String>,
    #[serde(default)]
    pub tier: Option<String>,
}

impl MeResponse {
    pub fn user_id(&self) -> Option<i32> {
        self.account.as_ref().map(|account| account.id)
    }

    pub fn email(&self) -> Option<&str> {
        self.email
            .as_deref()
            .or_else(|| self.account.as_ref().map(|a| a.email.as_str()))
    }
}

#[derive(Deserialize)]
struct ErrorBody {
    #[serde(default)]
    message: Option<String>,
    #[serde(default)]
    error: Option<String>,
}

pub async fn login(relay_url: &str, email: &str, password: &str) -> Result<AuthResponse> {
    let client = Client::new();
    let url = format!("{}/auth/login", relay_url);
    let resp = client
        .post(&url)
        .json(&AuthRequest {
            email: email.to_string(),
            password: password.to_string(),
        })
        .send()
        .await?;

    if !resp.status().is_success() {
        let status = resp.status();
        let body = resp.text().await.unwrap_or_default();
        if let Ok(err) = serde_json::from_str::<ErrorBody>(&body) {
            let msg = err.message.or(err.error).unwrap_or(body);
            bail!("login failed ({}): {}", status, msg);
        }
        bail!("login failed ({}): {}", status, body);
    }

    let auth: AuthResponse = resp.json().await?;
    Ok(auth)
}

pub async fn signup(relay_url: &str, email: &str, password: &str) -> Result<AuthResponse> {
    let client = Client::new();
    let url = format!("{}/auth/signup", relay_url);
    let resp = client
        .post(&url)
        .json(&AuthRequest {
            email: email.to_string(),
            password: password.to_string(),
        })
        .send()
        .await?;

    if !resp.status().is_success() {
        let status = resp.status();
        let body = resp.text().await.unwrap_or_default();
        if let Ok(err) = serde_json::from_str::<ErrorBody>(&body) {
            let msg = err.message.or(err.error).unwrap_or(body);
            bail!("signup failed ({}): {}", status, msg);
        }
        bail!("signup failed ({}): {}", status, body);
    }

    let auth: AuthResponse = resp.json().await?;
    Ok(auth)
}

pub async fn whoami(relay_url: &str, config: &CliConfig) -> Result<MeResponse> {
    let token = config.access_token.as_deref().unwrap_or_default();

    if token.is_empty() {
        bail!("not logged in — run `maho login` first");
    }

    let client = Client::new();
    let url = format!("{}/auth/me", relay_url);
    let resp = client.get(&url).bearer_auth(token).send().await?;

    if !resp.status().is_success() {
        let status = resp.status();
        let body = resp.text().await.unwrap_or_default();
        bail!("whoami failed ({}): {}", status, body);
    }

    let me: MeResponse = resp.json().await?;
    Ok(me)
}
