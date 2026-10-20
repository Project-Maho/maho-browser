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

/// Upper bound for one relay auth request. Without it a relay that accepts the
/// connection but never answers hangs `maho login`/`whoami` forever.
const RELAY_REQUEST_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(15);

fn relay_client() -> Result<Client> {
    Ok(Client::builder().timeout(RELAY_REQUEST_TIMEOUT).build()?)
}

pub async fn login(relay_url: &str, email: &str, password: &str) -> Result<AuthResponse> {
    let client = relay_client()?;
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
    let client = relay_client()?;
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

    let client = relay_client()?;
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

#[cfg(test)]
mod tests {
    use super::*;

    /// A relay that accepts the TCP connection but never answers must fail
    /// with a timeout instead of hanging `maho login` forever.
    #[tokio::test]
    async fn login_against_silent_relay_times_out() {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();
        let hold = tokio::spawn(async move {
            let (_socket, _) = listener.accept().await.unwrap();
            std::future::pending::<()>().await;
        });
        let outcome = tokio::time::timeout(
            RELAY_REQUEST_TIMEOUT + std::time::Duration::from_secs(10),
            login(&format!("http://{addr}"), "a@example.com", "pw"),
        )
        .await
        .expect("login must be bounded by the relay request timeout");
        let Err(err) = outcome else {
            panic!("silent relay must not yield a session");
        };
        assert!(
            err.downcast_ref::<reqwest::Error>().is_some_and(reqwest::Error::is_timeout),
            "expected a request timeout, got: {err:#}"
        );
        hold.abort();
    }
}
