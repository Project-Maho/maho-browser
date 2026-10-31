// Copyright 2026 Maho Browser. All rights reserved.

//! OAuth2 PKCE onboarding for IMAP/SMTP providers (Gmail, Microsoft).
//!
//! The Rust crate owns only the token-side of the flow: it mints the PKCE
//! challenge and authorization URL (`start_oauth`), and later exchanges the
//! authorization code the browser delivers for tokens and creates the account
//! (`complete_oauth`). Opening a system browser / binding a loopback listener
//! is a C++-layer concern and deliberately NOT done here.
//!
//! Network calls live behind [`transport::OAuthTransport`] so unit tests inject
//! a fake and never touch the wire; live exchange is manual-QA only.
//!
//! [`loopback`] adds the browser-specific split: because only the C++ layer can
//! open a Maho tab, mail-core binds the ephemeral loopback redirect listener and
//! returns the authorization URL for the browser to open, then a background
//! thread catches the redirect and drives [`complete_oauth`].

mod loopback;
mod transport;

pub use loopback::{sign_in_with_oauth_loopback, sign_in_with_oauth_loopback_with_options};

use base64::prelude::BASE64_URL_SAFE_NO_PAD;
use base64::Engine as _;
use rand::RngCore;
use rusqlite::Connection;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::time::Instant;

use maho_core::models::account::{AccountResponse, CreateAccountRequest, Encryption};

use crate::account;
use crate::error::{MailFfiError, Result};
use crate::state::{self, PkceEntry};

use transport::{OAuthTransport, ReqwestTransport, TokenExchangeRequest};

struct ProviderConfig {
    auth_endpoint: &'static str,
    userinfo_endpoint: &'static str,
    scopes: &'static str,
    imap_host: &'static str,
    imap_port: u16,
    smtp_host: &'static str,
    smtp_port: u16,
}

fn provider_config(provider: &str) -> Result<ProviderConfig> {
    match provider {
        "gmail" => Ok(ProviderConfig {
            auth_endpoint: "https://accounts.google.com/o/oauth2/v2/auth",
            userinfo_endpoint: "https://openidconnect.googleapis.com/v1/userinfo",
            scopes: "https://mail.google.com/ https://www.googleapis.com/auth/calendar.events openid email profile",
            imap_host: "imap.gmail.com",
            imap_port: 993,
            smtp_host: "smtp.gmail.com",
            smtp_port: 587,
        }),
        "outlook" => Ok(ProviderConfig {
            auth_endpoint: "https://login.microsoftonline.com/common/oauth2/v2.0/authorize",
            userinfo_endpoint: "https://graph.microsoft.com/oidc/userinfo",
            scopes: "https://outlook.office365.com/IMAP.AccessAsUser.All https://outlook.office365.com/SMTP.Send offline_access openid email profile",
            imap_host: "outlook.office365.com",
            imap_port: 993,
            smtp_host: "smtp.office365.com",
            smtp_port: 587,
        }),
        other => Err(MailFfiError::InvalidRequest(format!(
            "unknown oauth provider {other}"
        ))),
    }
}

/// Derive the RFC 7636 S256 code challenge from a verifier:
/// `BASE64URL-NO-PAD(SHA256(ASCII(verifier)))`.
pub fn pkce_challenge(verifier: &str) -> String {
    let mut hasher = Sha256::new();
    hasher.update(verifier.as_bytes());
    BASE64_URL_SAFE_NO_PAD.encode(hasher.finalize())
}

/// Generate a `(verifier, S256 challenge)` PKCE pair. The verifier is 32 random
/// bytes (base64url-no-pad → 43 chars, within RFC 7636's 43–128 range).
pub fn generate_pkce() -> (String, String) {
    let verifier = BASE64_URL_SAFE_NO_PAD.encode(random_bytes_32());
    let challenge = pkce_challenge(&verifier);
    (verifier, challenge)
}

fn generate_state() -> String {
    BASE64_URL_SAFE_NO_PAD.encode(random_bytes_32())
}

fn random_bytes_32() -> [u8; 32] {
    let mut out = [0u8; 32];
    rand::thread_rng().fill_bytes(&mut out);
    out
}

/// Authorization URL + opaque CSRF/lookup state returned to the caller.
#[derive(Debug, Clone, Serialize)]
pub struct StartOAuthResponse {
    pub state: String,
    pub auth_url: String,
}

/// Optional identity-binding inputs supplied by the main Google sign-in flow.
#[derive(Debug, Clone, Default, Deserialize)]
pub struct StartOAuthOptions {
    #[serde(default)]
    pub login_hint: Option<String>,
    #[serde(default)]
    pub expected_google_sub: Option<String>,
    #[serde(default)]
    pub reauthorize_account_id: Option<String>,
}

/// Optional completion input for platforms that complete outside the pending
/// start flow. A value here overrides the subject captured at start.
#[derive(Debug, Clone, Default, Deserialize)]
pub struct CompleteOAuthOptions {
    #[serde(default)]
    pub expected_google_sub: Option<String>,
}

fn normalize_expected_google_sub(value: Option<String>) -> Result<Option<String>> {
    match value {
        Some(value) => {
            let normalized = value.trim();
            if normalized.is_empty() {
                return Err(MailFfiError::InvalidRequest(
                    "expected_google_sub must not be empty or whitespace".to_string(),
                ));
            }
            Ok(Some(normalized.to_string()))
        }
        None => Ok(None),
    }
}

/// Build the provider authorization URL with PKCE S256, correctly
/// percent-encoded (via the `url` crate re-exported by reqwest — no extra dep).
pub fn build_authorization_url(
    provider: &str,
    client_id: &str,
    redirect_uri: &str,
    challenge: &str,
    state: &str,
    scopes: &str,
) -> Result<String> {
    build_authorization_url_with_options(
        provider,
        client_id,
        redirect_uri,
        challenge,
        state,
        scopes,
        &StartOAuthOptions::default(),
    )
}

pub fn build_authorization_url_with_options(
    provider: &str,
    client_id: &str,
    redirect_uri: &str,
    challenge: &str,
    state: &str,
    scopes: &str,
    options: &StartOAuthOptions,
) -> Result<String> {
    let config = provider_config(provider)?;
    let mut url = reqwest::Url::parse(config.auth_endpoint)
        .map_err(|e| MailFfiError::OAuth(format!("authorization url build failed: {e}")))?;
    {
        let mut query = url.query_pairs_mut();
        query
            .append_pair("client_id", client_id)
            .append_pair("redirect_uri", redirect_uri)
            .append_pair("response_type", "code")
            .append_pair("scope", scopes)
            .append_pair("access_type", "offline")
            .append_pair("prompt", "consent")
            .append_pair("code_challenge", challenge)
            .append_pair("code_challenge_method", "S256")
            .append_pair("state", state);
        if provider == "gmail" {
            query.append_pair("include_granted_scopes", "true");
        }
        if let Some(login_hint) = options
            .login_hint
            .as_deref()
            .filter(|value| !value.is_empty())
        {
            query.append_pair("login_hint", login_hint);
        }
    }
    Ok(url.into())
}

/// Resolve the effective OAuth client_id for `provider`.
///
/// The build-time-baked `GMAIL_CLIENT_ID` / `OUTLOOK_CLIENT_ID` always wins.
/// A caller-supplied value is only honored for providers that have no baked
/// identity (self-hosted/custom setups), because this value reaches the
/// authorization URL: letting an untrusted caller override the client_id for a
/// known provider would point Maho's consent screen at a third-party OAuth app
/// and hand that app the resulting grant. Returns `InvalidRequest` (never a
/// panic) when no client_id is available.
pub fn resolve_client_id(provider: &str, client_id: &str) -> Result<String> {
    let baked = match provider {
        "gmail" => option_env!("GMAIL_CLIENT_ID"),
        "outlook" => option_env!("OUTLOOK_CLIENT_ID"),
        _ => None,
    };
    if let Some(value) = baked.filter(|value| !value.is_empty()) {
        return Ok(value.to_string());
    }
    // No baked identity for this provider: fall back to the caller value.
    if !client_id.is_empty() {
        return Ok(client_id.to_string());
    }
    Err(MailFfiError::InvalidRequest(format!(
        "missing client_id for provider {provider}"
    )))
}

/// True when `redirect_uri` is an OAuth redirect Maho is willing to use.
///
/// Only loopback redirects are accepted. The redirect target is embedded in the
/// authorization URL, so an arbitrary value would let a caller have the
/// provider deliver the authorization code to a remote endpoint it controls.
pub fn is_allowed_redirect_uri(redirect_uri: &str) -> bool {
    let Ok(url) = reqwest::Url::parse(redirect_uri) else {
        return false;
    };
    if url.scheme() != "http" {
        return false;
    }
    matches!(
        url.host_str(),
        Some("127.0.0.1") | Some("localhost") | Some("[::1]")
    )
}

/// Resolve a confidential-client secret supplied only through the build
/// environment. It is never accepted from renderer-controlled onboarding input.
pub fn resolve_client_secret(provider: &str, client_secret: &str) -> String {
    let baked = match provider {
        "gmail" => option_env!("GMAIL_CLIENT_SECRET"),
        "outlook" => option_env!("OUTLOOK_CLIENT_SECRET"),
        _ => None,
    };
    baked
        .filter(|value| !value.is_empty())
        .unwrap_or(client_secret)
        .to_string()
}

/// Begin PKCE onboarding: mint verifier/challenge/state, register the pending
/// entry, and return the authorization URL for the browser to open.
pub fn start_oauth(
    provider: &str,
    client_id: &str,
    redirect_uri: &str,
) -> Result<StartOAuthResponse> {
    start_oauth_with_options(
        provider,
        client_id,
        redirect_uri,
        StartOAuthOptions::default(),
    )
}

pub fn start_oauth_with_options(
    provider: &str,
    client_id: &str,
    redirect_uri: &str,
    options: StartOAuthOptions,
) -> Result<StartOAuthResponse> {
    let expected_google_sub = normalize_expected_google_sub(options.expected_google_sub.clone())?;
    let config = provider_config(provider)?;
    if !is_allowed_redirect_uri(redirect_uri) {
        return Err(MailFfiError::InvalidRequest(
            "redirect_uri must be a loopback address".to_string(),
        ));
    }
    let client_id = resolve_client_id(provider, client_id)?;
    let client_secret = resolve_client_secret(provider, "");
    let (verifier, challenge) = generate_pkce();
    let state_token = generate_state();
    let auth_url = build_authorization_url_with_options(
        provider,
        &client_id,
        redirect_uri,
        &challenge,
        &state_token,
        config.scopes,
        &options,
    )?;

    state::pending_pkce().insert(
        state_token.clone(),
        PkceEntry {
            code_verifier: verifier,
            provider: provider.to_string(),
            client_id,
            client_secret,
            redirect_uri: redirect_uri.to_string(),
            expected_google_sub,
            reauthorize_account_id: options.reauthorize_account_id,
            created_at: Instant::now(),
        },
    );

    Ok(StartOAuthResponse {
        state: state_token,
        auth_url,
    })
}

struct OAuthCompletion<'a> {
    conn: &'a mut Connection,
    credential_key: &'a [u8; 32],
    transport: &'a dyn OAuthTransport,
}

impl OAuthCompletion<'_> {
    fn complete(&mut self, state_token: &str, code: &str) -> Result<AccountResponse> {
        self.complete_with_options(state_token, code, CompleteOAuthOptions::default())
    }

    fn complete_with_options(
        &mut self,
        state_token: &str,
        code: &str,
        options: CompleteOAuthOptions,
    ) -> Result<AccountResponse> {
        if code.trim().is_empty() {
            return Err(MailFfiError::OAuth("empty authorization code".to_string()));
        }
        let completion_expected_google_sub =
            normalize_expected_google_sub(options.expected_google_sub)?;
        let (entry, completion) = state::pending_pkce().begin(state_token)?;
        let captured_expected_google_sub =
            normalize_expected_google_sub(entry.expected_google_sub.clone())?;

        let config = provider_config(&entry.provider)?;
        let auth_type = format!("oauth2_{}", entry.provider);
        let token_url = account::token_endpoint(&auth_type)
            .ok_or_else(|| MailFfiError::OAuth(format!("unknown provider {}", entry.provider)))?;

        let token = self.transport.exchange_code(&TokenExchangeRequest {
            token_url,
            client_id: &entry.client_id,
            client_secret: &entry.client_secret,
            redirect_uri: &entry.redirect_uri,
            code,
            verifier: &entry.code_verifier,
        })?;
        let userinfo = self
            .transport
            .fetch_userinfo(config.userinfo_endpoint, &token.access_token)?;

        let expected_google_sub = completion_expected_google_sub.or(captured_expected_google_sub);
        if entry.provider == "gmail" {
            if let Some(expected) = expected_google_sub {
                let actual = userinfo
                    .subject
                    .as_deref()
                    .map(str::trim)
                    .filter(|value| !value.is_empty())
                    .ok_or(MailFfiError::GoogleIdentityMissingSubject)?;
                if actual != expected {
                    return Err(MailFfiError::GoogleIdentityMismatch {
                        expected,
                        actual: actual.to_string(),
                    });
                }
            }
        }

        let request = CreateAccountRequest {
            email: userinfo.email.clone(),
            display_name: userinfo.name,
            auth_type: Some(auth_type),
            imap_host: config.imap_host.to_string(),
            imap_port: config.imap_port,
            imap_encryption: Encryption::Tls,
            smtp_host: config.smtp_host.to_string(),
            smtp_port: config.smtp_port,
            smtp_encryption: Encryption::StartTls,
            username: userinfo.email,
            password: None,
            oauth2_client_id: Some(entry.client_id),
            oauth2_client_secret: Some(entry.client_secret).filter(|s| !s.is_empty()),
            oauth2_access_token: Some(token.access_token),
            oauth2_refresh_token: token.refresh_token,
            oauth2_expires_at: token.expires_at,
        };
        completion.commit(|| match entry.reauthorize_account_id {
            Some(account_id) => account::replace_oauth_credentials(
                self.conn,
                self.credential_key,
                &account_id,
                request,
            ),
            None => account::create_account(self.conn, self.credential_key, request),
        })
    }
}

/// Finish PKCE onboarding: recover the verifier by `state` (rejecting unknown or
/// expired state before any network), exchange the code for tokens, fetch
/// userinfo, and create the account with secrets stored encrypted.
pub fn complete_oauth(
    conn: &mut Connection,
    credential_key: &[u8; 32],
    state_token: &str,
    code: &str,
) -> Result<AccountResponse> {
    complete_oauth_with_options(
        conn,
        credential_key,
        state_token,
        code,
        CompleteOAuthOptions::default(),
    )
}

pub fn complete_oauth_with_options(
    conn: &mut Connection,
    credential_key: &[u8; 32],
    state_token: &str,
    code: &str,
    options: CompleteOAuthOptions,
) -> Result<AccountResponse> {
    OAuthCompletion {
        conn,
        credential_key,
        transport: &ReqwestTransport,
    }
    .complete_with_options(state_token, code, options)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_random_bytes_32_entropy_not_fixed_uuid_version() {
        let mut all_have_uuid_v4_nibble = true;
        for _ in 0..50 {
            let bytes = random_bytes_32();
            // If random_bytes_32 concatenates two UUIDv4s, byte 6 has fixed version nibble 0x4
            if (bytes[6] >> 4) != 0x4 {
                all_have_uuid_v4_nibble = false;
                break;
            }
        }
        assert!(
            !all_have_uuid_v4_nibble,
            "random_bytes_32 should not be derived from UUIDv4 (fixed version bit 0x4 in byte 6)"
        );
    }
}

#[cfg(test)]
#[path = "oauth_tests.rs"]
mod oauth_tests;

#[cfg(test)]
#[path = "account_review_tests.rs"]
mod account_review_tests;
