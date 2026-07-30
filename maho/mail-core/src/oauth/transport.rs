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
}

pub(crate) struct UserInfo {
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
            return Err(MailFfiError::OAuth(format!(
                "token endpoint returned {status}: {body}"
            )));
        }
        let token: TokenResp = resp
            .json()
            .map_err(|e| MailFfiError::OAuth(format!("token parse: {e}")))?;
        Ok(TokenExchange {
            access_token: token.access_token,
            refresh_token: token.refresh_token,
        })
    }

    fn fetch_userinfo(&self, userinfo_url: &str, access_token: &str) -> Result<UserInfo> {
        #[derive(Deserialize)]
        struct UserInfoResp {
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
            email: info.email,
            name: info.name.unwrap_or_default(),
        })
    }
}
