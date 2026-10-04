//! OAuth2 PKCE loopback sign-in for AI providers. Security contract: the
//! authorization `state` is compared before the code is exchanged (CSRF), and
//! the code exchange uses PKCE with no client secret, so endpoints and client
//! ids must come from the caller rather than being embedded here.

use std::collections::HashMap;
use std::io::Write;
use std::net::{Shutdown, TcpListener, TcpStream};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use base64::prelude::BASE64_URL_SAFE_NO_PAD;
use base64::Engine as _;
use serde::{Deserialize, Serialize};

use crate::error::CoreError;
use crate::google_identity::{
    error_response_body, extract_code, pkce_challenge, query_param, read_http_request,
};

type Result<T> = std::result::Result<T, CoreError>;

const CALLBACK_TIMEOUT: Duration = Duration::from_secs(900);
const CONNECTION_READ_TIMEOUT: Duration = Duration::from_secs(5);
const ACCEPT_POLL_INTERVAL: Duration = Duration::from_millis(50);

const SUCCESS_RESPONSE: &str = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n<html><head><meta charset=\"utf-8\"></head><body><h2>Connected</h2><p>Returning to Maho\u{2026}</p><script>window.close()</script></body></html>";

#[derive(Debug, Clone, Deserialize)]
pub struct ProviderOAuthConfig {
    pub provider: String,
    pub client_id: String,
    pub authorize_url: String,
    pub token_url: String,
    #[serde(default)]
    pub scopes: String,
    #[serde(default)]
    pub audience: String,
    /// Zero binds an ephemeral port; providers with a registered redirect URI
    /// must pin their exact port and path.
    #[serde(default)]
    pub redirect_port: u16,
    #[serde(default)]
    pub redirect_path: String,
    #[serde(default)]
    pub extra_authorize_params: HashMap<String, String>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StartProviderOAuth {
    pub state: String,
    pub auth_url: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ProviderOAuthTokens {
    pub provider: String,
    pub access_token: String,
    pub refresh_token: String,
    /// Unix seconds; 0 means the provider reported no lifetime.
    pub expires_at: i64,
    pub token_type: String,
    pub scope: String,
}

#[derive(Debug, Clone)]
struct PendingFlow {
    verifier: String,
    redirect_uri: String,
    config: ProviderOAuthConfig,
}

#[derive(Debug, Deserialize)]
struct TokenResponse {
    access_token: Option<String>,
    refresh_token: Option<String>,
    expires_in: Option<i64>,
    token_type: Option<String>,
    scope: Option<String>,
    error: Option<String>,
    error_description: Option<String>,
}

fn pending() -> &'static Mutex<HashMap<String, PendingFlow>> {
    static PENDING: OnceLock<Mutex<HashMap<String, PendingFlow>>> = OnceLock::new();
    PENDING.get_or_init(|| Mutex::new(HashMap::new()))
}

fn take_pending(state: &str) -> Option<PendingFlow> {
    match pending().lock() {
        Ok(mut map) => map.remove(state),
        Err(mut poisoned) => poisoned.get_mut().remove(state),
    }
}

fn put_pending(state: String, entry: PendingFlow) {
    match pending().lock() {
        Ok(mut map) => {
            map.insert(state, entry);
        }
        Err(mut poisoned) => {
            poisoned.get_mut().insert(state, entry);
        }
    }
}

fn cancels() -> &'static Mutex<HashMap<String, Arc<AtomicBool>>> {
    static CANCELS: OnceLock<Mutex<HashMap<String, Arc<AtomicBool>>>> = OnceLock::new();
    CANCELS.get_or_init(|| Mutex::new(HashMap::new()))
}

fn register_cancel(state: String) -> Arc<AtomicBool> {
    let flag = Arc::new(AtomicBool::new(false));
    match cancels().lock() {
        Ok(mut map) => {
            map.insert(state, Arc::clone(&flag));
        }
        Err(mut poisoned) => {
            poisoned.get_mut().insert(state, Arc::clone(&flag));
        }
    }
    flag
}

fn take_cancel(state: &str) {
    match cancels().lock() {
        Ok(mut map) => {
            map.remove(state);
        }
        Err(mut poisoned) => {
            poisoned.get_mut().remove(state);
        }
    }
}

pub fn cancel_provider_oauth(state: &str) -> bool {
    let flag = match cancels().lock() {
        Ok(map) => map.get(state).cloned(),
        Err(poisoned) => poisoned.get_ref().get(state).cloned(),
    };
    match flag {
        Some(flag) => {
            flag.store(true, Ordering::SeqCst);
            true
        }
        None => false,
    }
}

fn random_token() -> String {
    let mut bytes = [0u8; 32];
    rand::RngCore::fill_bytes(&mut rand::rngs::OsRng, &mut bytes);
    BASE64_URL_SAFE_NO_PAD.encode(bytes)
}

fn urlencode(value: &str) -> String {
    value
        .bytes()
        .map(|b| match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                (b as char).to_string()
            }
            _ => format!("%{b:02X}"),
        })
        .collect()
}

fn unix_now() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

pub fn validate_config(config: &ProviderOAuthConfig) -> Result<()> {
    if config.provider.trim().is_empty() {
        return Err(CoreError::Parse("missing provider".to_string()));
    }
    if config.client_id.trim().is_empty() {
        return Err(CoreError::Parse(
            "missing OAuth client id for provider".to_string(),
        ));
    }
    for (label, url) in [
        ("authorize_url", &config.authorize_url),
        ("token_url", &config.token_url),
    ] {
        if !url.starts_with("https://") {
            return Err(CoreError::Parse(format!("{label} must be https")));
        }
    }
    Ok(())
}

pub fn build_authorization_url(
    config: &ProviderOAuthConfig,
    redirect_uri: &str,
    state: &str,
    challenge: &str,
) -> String {
    let mut params: Vec<(String, String)> = vec![
        ("client_id".to_string(), config.client_id.clone()),
        ("redirect_uri".to_string(), redirect_uri.to_string()),
        ("response_type".to_string(), "code".to_string()),
        ("state".to_string(), state.to_string()),
        ("code_challenge".to_string(), challenge.to_string()),
        ("code_challenge_method".to_string(), "S256".to_string()),
    ];
    if !config.scopes.trim().is_empty() {
        params.push(("scope".to_string(), config.scopes.clone()));
    }
    if !config.audience.trim().is_empty() {
        params.push(("audience".to_string(), config.audience.clone()));
    }
    let mut extras: Vec<(&String, &String)> = config.extra_authorize_params.iter().collect();
    extras.sort_by(|a, b| a.0.cmp(b.0));
    for (key, value) in extras {
        params.push((key.clone(), value.clone()));
    }

    let query = params
        .iter()
        .map(|(k, v)| format!("{}={}", urlencode(k), urlencode(v)))
        .collect::<Vec<_>>()
        .join("&");
    let separator = if config.authorize_url.contains('?') {
        "&"
    } else {
        "?"
    };
    format!("{}{}{}", config.authorize_url, separator, query)
}

pub fn start_provider_oauth<F>(
    config: ProviderOAuthConfig,
    on_done: F,
) -> Result<StartProviderOAuth>
where
    F: FnOnce(Result<ProviderOAuthTokens>) + Send + 'static,
{
    validate_config(&config)?;

    let bind_addr = format!("127.0.0.1:{}", config.redirect_port);
    let listener = TcpListener::bind(&bind_addr)
        .map_err(|e| CoreError::Network(format!("loopback bind failed: {e}")))?;
    let port = listener
        .local_addr()
        .map_err(|e| CoreError::Network(format!("loopback local_addr failed: {e}")))?
        .port();

    let path = config.redirect_path.trim();
    let redirect_uri = if path.is_empty() {
        format!("http://localhost:{port}")
    } else if path.starts_with('/') {
        format!("http://localhost:{port}{path}")
    } else {
        format!("http://localhost:{port}/{path}")
    };

    let verifier = random_token();
    let challenge = pkce_challenge(&verifier);
    let state = random_token();
    let auth_url = build_authorization_url(&config, &redirect_uri, &state, &challenge);

    put_pending(
        state.clone(),
        PendingFlow {
            verifier,
            redirect_uri,
            config,
        },
    );

    let expected_state = state.clone();
    let cancel = register_cancel(state.clone());
    let spawned = std::thread::Builder::new()
        .name("maho-provider-oauth".to_string())
        .spawn(move || {
            let result = accept_and_exchange(&listener, &expected_state, &cancel);
            take_pending(&expected_state);
            take_cancel(&expected_state);
            on_done(result);
        });

    if let Err(e) = spawned {
        take_pending(&state);
        take_cancel(&state);
        return Err(CoreError::Network(format!(
            "failed to spawn OAuth listener: {e}"
        )));
    }

    Ok(StartProviderOAuth { state, auth_url })
}

fn accept_and_exchange(
    listener: &TcpListener,
    expected_state: &str,
    cancel: &AtomicBool,
) -> Result<ProviderOAuthTokens> {
    // Keep one overall deadline: probes must neither consume nor extend a flow.
    let start = std::time::Instant::now();
    let (mut stream, request) = loop {
        let mut stream = accept_with_timeout(listener, cancel, start)?;
        let request = match read_http_request(&mut stream) {
            Ok(request) => request,
            Err(e) => {
                tracing::debug!("skipping unreadable provider OAuth connection: {e}");
                write_error_response(&mut stream, "Invalid OAuth callback request", 400);
                continue;
            }
        };
        if query_param(&request, "state").as_deref() != Some(expected_state) {
            write_error_response(&mut stream, "Missing or mismatched sign-in state", 400);
            continue;
        }
        break (stream, request);
    };

    let entry = take_pending(expected_state)
        .ok_or_else(|| CoreError::Parse("unknown sign-in state".to_string()))?;

    let code = match extract_code(&request) {
        Ok(code) => code,
        Err(e) => {
            write_error_response(&mut stream, &e.to_string(), 400);
            return Err(e);
        }
    };

    match exchange_code(&entry, &code) {
        Ok(tokens) => {
            write_success_response(&mut stream);
            Ok(tokens)
        }
        Err(e) => {
            write_error_response(&mut stream, &e.to_string(), 502);
            Err(e)
        }
    }
}

fn accept_with_timeout(
    listener: &TcpListener,
    cancel: &AtomicBool,
    start: std::time::Instant,
) -> Result<TcpStream> {
    listener
        .set_nonblocking(true)
        .map_err(|e| CoreError::Network(format!("set_nonblocking failed: {e}")))?;
    loop {
        if cancel.load(Ordering::SeqCst) {
            return Err(CoreError::Network("sign-in cancelled".to_string()));
        }
        if start.elapsed() >= CALLBACK_TIMEOUT {
            return Err(CoreError::Network(
                "sign-in timed out waiting for redirect".to_string(),
            ));
        }
        match listener.accept() {
            Ok((stream, _)) => {
                stream
                    .set_nonblocking(false)
                    .map_err(|e| CoreError::Network(format!("set blocking failed: {e}")))?;
                stream
                    .set_read_timeout(Some(CONNECTION_READ_TIMEOUT))
                    .map_err(|e| CoreError::Network(format!("set_read_timeout failed: {e}")))?;
                return Ok(stream);
            }
            Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                std::thread::sleep(ACCEPT_POLL_INTERVAL);
            }
            Err(e) => return Err(CoreError::Network(format!("callback accept failed: {e}"))),
        }
    }
}

fn exchange_code(entry: &PendingFlow, code: &str) -> Result<ProviderOAuthTokens> {
    let body = serde_json::json!({
        "grant_type": "authorization_code",
        "client_id": entry.config.client_id,
        "code": code,
        "code_verifier": entry.verifier,
        "redirect_uri": entry.redirect_uri,
    })
    .to_string();

    let response = post_json_blocking(&entry.config.token_url, &body)?;
    parse_token_response(&entry.config.provider, &response, String::new())
}

pub fn refresh_provider_oauth(
    config: &ProviderOAuthConfig,
    refresh_token: &str,
) -> Result<ProviderOAuthTokens> {
    validate_config(config)?;
    if refresh_token.trim().is_empty() {
        return Err(CoreError::Parse("missing refresh token".to_string()));
    }

    let body = serde_json::json!({
        "grant_type": "refresh_token",
        "client_id": config.client_id,
        "refresh_token": refresh_token,
    })
    .to_string();

    let response = post_json_blocking(&config.token_url, &body)?;
    parse_token_response(&config.provider, &response, refresh_token.to_string())
}

fn parse_token_response(
    provider: &str,
    response: &str,
    fallback_refresh_token: String,
) -> Result<ProviderOAuthTokens> {
    let parsed: TokenResponse = serde_json::from_str(response)
        .map_err(|e| CoreError::Parse(format!("token response parse failed: {e}")))?;

    if let Some(error) = parsed.error.filter(|e| !e.is_empty()) {
        let detail = parsed
            .error_description
            .filter(|d| !d.is_empty())
            .unwrap_or_else(|| error.clone());
        return Err(CoreError::Network(format!(
            "token endpoint rejected the request: {error} ({detail})"
        )));
    }

    let access_token = parsed
        .access_token
        .filter(|t| !t.is_empty())
        .ok_or_else(|| CoreError::Parse("token response missing access_token".to_string()))?;

    let refresh_token = parsed
        .refresh_token
        .filter(|t| !t.is_empty())
        .unwrap_or(fallback_refresh_token);

    let expires_at = parsed
        .expires_in
        .filter(|seconds| *seconds > 0)
        .map(|seconds| unix_now() + seconds)
        .unwrap_or(0);

    Ok(ProviderOAuthTokens {
        provider: provider.to_string(),
        access_token,
        refresh_token,
        expires_at,
        token_type: parsed.token_type.unwrap_or_else(|| "Bearer".to_string()),
        scope: parsed.scope.unwrap_or_default(),
    })
}

fn post_json_blocking(url: &str, body: &str) -> Result<String> {
    let client = crate::http::shared_http_client();
    let url = url.to_string();
    let body = body.to_string();
    let runtime = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .map_err(|e| CoreError::Network(format!("runtime build failed: {e}")))?;
    runtime.block_on(async move {
        let response = client
            .post(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .body(body)
            .send()
            .await
            .map_err(|e| CoreError::Network(format!("token exchange failed: {e}")))?;
        let status = response.status();
        let text = response
            .text()
            .await
            .map_err(|e| CoreError::Network(format!("token response read failed: {e}")))?;
        if !status.is_success() {
            if text.trim_start().starts_with('{') {
                return Ok(text);
            }
            return Err(CoreError::Network(format!(
                "token exchange returned HTTP {}",
                status.as_u16()
            )));
        }
        Ok(text)
    })
}

fn write_success_response(stream: &mut TcpStream) {
    if let Err(e) = stream.write_all(SUCCESS_RESPONSE.as_bytes()) {
        log_write_failure(e);
        return;
    }
    finish_stream(stream);
}

fn write_error_response(stream: &mut TcpStream, msg: &str, status: u16) {
    let body = error_response_body(msg);
    let response = format!(
        "HTTP/1.1 {} Error\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: {}\r\n\r\n{}",
        status,
        body.len(),
        body
    );
    if let Err(e) = stream.write_all(response.as_bytes()) {
        log_write_failure(e);
        return;
    }
    finish_stream(stream);
}

fn finish_stream(stream: &mut TcpStream) {
    if let Err(e) = stream.flush() {
        log_write_failure(e);
    }
    if let Err(e) = stream.shutdown(Shutdown::Both) {
        log_write_failure(e);
    }
}

fn log_write_failure(err: std::io::Error) {
    eprintln!("provider OAuth callback stream error: {err}");
}

#[cfg(test)]
#[path = "provider_oauth_tests.rs"]
mod provider_oauth_tests;
