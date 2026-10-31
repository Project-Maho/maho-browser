// Copyright 2026 Maho Browser. All rights reserved.

//! Network seam for the OAuth2 token exchange + userinfo lookup. Isolated from
//! the flow orchestration so the parent module stays wire-free and unit tests
//! inject a fake instead of hitting Google/Microsoft.

use std::time::Duration;

use serde::Deserialize;

use crate::error::{MailFfiError, Result};

pub(crate) struct TokenExchangeRequest<'a> {
    pub token_url: &'a str,
    pub client_id: &'a str,
    pub client_secret: &'a str,
    pub redirect_uri: &'a str,
    pub code: &'a str,
    pub verifier: &'a str,
}

pub(crate) struct TokenExchange {
    pub access_token: String,
    pub refresh_token: Option<String>,
    pub expires_at: Option<String>,
}

pub(crate) struct UserInfo {
    pub subject: Option<String>,
    pub email: String,
    pub name: String,
}

pub(crate) trait OAuthTransport {
    fn exchange_code(&self, request: &TokenExchangeRequest<'_>) -> Result<TokenExchange>;
    fn fetch_userinfo(&self, userinfo_url: &str, access_token: &str) -> Result<UserInfo>;
}

pub(crate) struct ReqwestTransport;

impl OAuthTransport for ReqwestTransport {
    fn exchange_code(&self, request: &TokenExchangeRequest<'_>) -> Result<TokenExchange> {
        #[derive(Deserialize)]
        struct TokenResp {
            access_token: String,
            refresh_token: Option<String>,
            expires_in: Option<i64>,
            expires_at: Option<String>,
        }
        let http = reqwest::blocking::Client::builder()
            .timeout(Duration::from_secs(30))
            .build()
            .map_err(|e| MailFfiError::OAuth(format!("http client: {e}")))?;
        let mut params = vec![
            ("grant_type", "authorization_code"),
            ("client_id", request.client_id),
            ("redirect_uri", request.redirect_uri),
            ("code", request.code),
            ("code_verifier", request.verifier),
        ];
        if !request.client_secret.is_empty() {
            params.push(("client_secret", request.client_secret));
        }
        let resp = http
            .post(request.token_url)
            .form(&params)
            .send()
            .map_err(|e| MailFfiError::OAuth(format!("token request: {e}")))?;
        if !resp.status().is_success() {
            let status = resp.status();
            let body = resp.text().unwrap_or_default();
            #[derive(Deserialize)]
            struct OAuthError {
                error: Option<String>,
                error_description: Option<String>,
            }
            let err_code = serde_json::from_str::<OAuthError>(&body)
                .ok()
                .and_then(|e| {
                    let code = match e.error.as_deref() {
                        Some("invalid_request") => "invalid_request",
                        Some("invalid_client") => "invalid_client",
                        Some("invalid_grant") => "invalid_grant",
                        Some("unauthorized_client") => "unauthorized_client",
                        Some("unsupported_grant_type") => "unsupported_grant_type",
                        Some("invalid_scope") => "invalid_scope",
                        _ => return None,
                    };
                    if e.error_description.as_deref() == Some("client_secret is missing.") {
                        Some(format!("{code} (client_secret is missing)"))
                    } else {
                        Some(code.to_string())
                    }
                });
            let msg = match err_code {
                Some(code) => format!("token endpoint returned {status}: {code}"),
                None => format!("token endpoint returned {status}"),
            };
            log::error!("[mail-ffi] {msg}");
            return Err(MailFfiError::OAuth(msg));
        }
        let token: TokenResp = resp
            .json()
            .map_err(|e| MailFfiError::OAuth(format!("token parse: {e}")))?;
        let expires_at = token.expires_at.or_else(|| {
            token.expires_in.and_then(|secs| {
                chrono::Duration::seconds(secs)
                    .to_std()
                    .ok()
                    .and_then(|duration| {
                        chrono::Utc::now()
                            .checked_add_signed(chrono::Duration::from_std(duration).ok()?)
                    })
                    .map(|dt| dt.to_rfc3339())
            })
        });
        Ok(TokenExchange {
            access_token: token.access_token,
            refresh_token: token.refresh_token,
            expires_at,
        })
    }

    fn fetch_userinfo(&self, userinfo_url: &str, access_token: &str) -> Result<UserInfo> {
        #[derive(Deserialize)]
        struct UserInfoResp {
            #[serde(rename = "sub")]
            subject: Option<String>,
            email: String,
            name: Option<String>,
        }
        let http = reqwest::blocking::Client::builder()
            .timeout(Duration::from_secs(30))
            .build()
            .map_err(|e| MailFfiError::OAuth(format!("http client: {e}")))?;
        let resp = http
            .get(userinfo_url)
            .bearer_auth(access_token)
            .send()
            .map_err(|e| MailFfiError::OAuth(format!("userinfo request: {e}")))?;
        if !resp.status().is_success() {
            return Err(MailFfiError::OAuth(format!(
                "userinfo endpoint returned {}",
                resp.status()
            )));
        }
        let info: UserInfoResp = resp
            .json()
            .map_err(|e| MailFfiError::OAuth(format!("userinfo parse: {e}")))?;
        Ok(UserInfo {
            subject: info.subject,
            email: info.email,
            name: info.name.unwrap_or_default(),
        })
    }
}
