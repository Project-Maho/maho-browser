//! Google Sign-In (identity only) via OAuth2 PKCE + a loopback redirect.
//!
//! Scope is deliberately `openid email profile` — no Gmail scope. Mailbox
//! access is a separate consent handled by `mail-core`, because a restricted
//! scope like `https://mail.google.com/` drags the whole client through Google
//! app verification and CASA review before anyone can simply sign in.
//!
//! The browser split mirrors `mail-core::oauth::loopback`: this crate binds the
//! ephemeral loopback listener and returns the authorization URL for the C++
//! layer to open in a tab (only it can), then a background thread catches the
//! redirect, validates the CSRF `state`, exchanges the code, and hands the
//! resulting `id_token` back through a callback.

use std::collections::HashMap;
use std::io::Write;
use std::net::{Shutdown, TcpListener, TcpStream};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;

use base64::prelude::BASE64_URL_SAFE_NO_PAD;
use base64::Engine as _;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

use crate::error::CoreError;

type Result<T> = std::result::Result<T, CoreError>;

const AUTH_ENDPOINT: &str = "https://accounts.google.com/o/oauth2/v2/auth";
/// The desktop client cannot hold the Google client secret, so the code -> token
/// exchange is delegated to the relay (which holds the secret server-side). This
/// is the relay used when no build-time override is provided.
const DEFAULT_RELAY_BASE_URL: &str = "https://relay.mahobrowser.com";
const IDENTITY_SCOPES: &str = "openid email profile";
// Backstop only. A flow normally ends when the redirect arrives or the C++
// layer cancels it (popup closed before the redirect, or a new flow supersedes
// a stale one) via `cancel_google_sign_in`. The cap is deliberately generous:
// a first-time consent (account picker + password + passkey + consent screen)
// routinely takes minutes, and a short cap silently failed those sign-ins with
// "timed out waiting for redirect".
//
// REGRESSION CONTRACT: docs/operations/google-signin-loopback-reliability.md
// (F1a). Do NOT shorten this to a "reasonable" few-second/one-minute value.
const CALLBACK_TIMEOUT: Duration = Duration::from_secs(900);
const ACCEPT_POLL_INTERVAL: Duration = Duration::from_millis(50);
const MAX_REQUEST_BYTES: usize = 64 * 1024;
// Per-connection read budget, deliberately much shorter than CALLBACK_TIMEOUT.
//
// REGRESSION CONTRACT: docs/operations/google-signin-loopback-reliability.md
// (F5). The loopback port is reachable by anything on the machine, and a
// connection that is accepted but never sends a request used to inherit the
// whole 900s budget as its read timeout, wedging the flow until the backstop
// expired. The overall CALLBACK_TIMEOUT budget still bounds the flow; this only
// bounds one connection so the listener can move on to the next one.
const CONNECTION_READ_TIMEOUT: Duration = Duration::from_secs(5);

const SUCCESS_BODY: &str = "<html><head><meta charset=\"utf-8\"></head><body><h2>Signed in</h2><p>Returning to Maho\u{2026}</p><script>window.close()</script></body></html>";

#[derive(Debug, Clone, Serialize)]
pub struct StartGoogleSignIn {
    pub state: String,
    pub auth_url: String,
}

#[derive(Debug, Clone)]
pub struct PendingSignIn {
    pub verifier: String,
    pub nonce: String,
    pub redirect_uri: String,
}

#[derive(Debug, Clone)]
pub struct GoogleSignInResult {
    pub id_token: String,
    pub nonce: String,
}

#[derive(Debug, Deserialize)]
struct TokenResponse {
    id_token: Option<String>,
}

fn pending() -> &'static Mutex<HashMap<String, PendingSignIn>> {
    static PENDING: OnceLock<Mutex<HashMap<String, PendingSignIn>>> = OnceLock::new();
    PENDING.get_or_init(|| Mutex::new(HashMap::new()))
}

fn take_pending(state: &str) -> Option<PendingSignIn> {
    match pending().lock() {
        Ok(mut map) => map.remove(state),
        Err(mut poisoned) => poisoned.get_mut().remove(state),
    }
}

fn put_pending(state: String, entry: PendingSignIn) {
    match pending().lock() {
        Ok(mut map) => {
            map.insert(state, entry);
        }
        Err(mut poisoned) => {
            poisoned.get_mut().insert(state, entry);
        }
    }
}

// Per-flow cancellation flags, keyed by CSRF `state`. The listener thread polls
// its flag so the C++ layer can end a flow deterministically instead of leaking
// a thread + bound loopback port until the backstop timeout.
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

/// Signals the in-flight sign-in identified by `state` to stop waiting for its
/// redirect. Returns true when a matching pending flow was found. Idempotent;
/// safe to call after the flow already finished.
pub fn cancel_google_sign_in(state: &str) -> bool {
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

pub fn pkce_challenge(verifier: &str) -> String {
    let mut hasher = Sha256::new();
    hasher.update(verifier.as_bytes());
    BASE64_URL_SAFE_NO_PAD.encode(hasher.finalize())
}

fn random_token() -> String {
    let mut bytes = [0u8; 32];
    rand::RngCore::fill_bytes(&mut rand::rngs::OsRng, &mut bytes);
    BASE64_URL_SAFE_NO_PAD.encode(bytes)
}

pub fn resolve_client_id(client_id: &str) -> Result<String> {
    if !client_id.is_empty() {
        return Ok(client_id.to_string());
    }
    match option_env!("MAHO_GOOGLE_CLIENT_ID").filter(|v| !v.is_empty()) {
        Some(value) => Ok(value.to_string()),
        None => Err(CoreError::Parse("missing Google client_id".to_string())),
    }
}

pub fn build_authorization_url(
    client_id: &str,
    redirect_uri: &str,
    state: &str,
    nonce: &str,
    challenge: &str,
) -> String {
    let params = [
        ("client_id", client_id),
        ("redirect_uri", redirect_uri),
        ("response_type", "code"),
        ("scope", IDENTITY_SCOPES),
        ("state", state),
        ("nonce", nonce),
        ("code_challenge", challenge),
        ("code_challenge_method", "S256"),
        ("access_type", "online"),
        ("prompt", "select_account"),
    ];
    let query = params
        .iter()
        .map(|(k, v)| format!("{}={}", k, urlencode(v)))
        .collect::<Vec<_>>()
        .join("&");
    format!("{AUTH_ENDPOINT}?{query}")
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

/// Binds an ephemeral loopback listener and returns `{state, auth_url}` before
/// the redirect arrives, so the caller can open the URL in a Maho tab. `on_done`
/// runs on the callback thread once the exchange finishes.
pub fn start_google_sign_in<F>(client_id: &str, on_done: F) -> Result<StartGoogleSignIn>
where
    F: FnOnce(Result<GoogleSignInResult>) + Send + 'static,
{
    let client_id = resolve_client_id(client_id)?;
    let listener = TcpListener::bind("127.0.0.1:0")
        .map_err(|e| CoreError::Network(format!("loopback bind failed: {e}")))?;
    let port = listener
        .local_addr()
        .map_err(|e| CoreError::Network(format!("loopback local_addr failed: {e}")))?
        .port();
    let redirect_uri = format!("http://127.0.0.1:{port}");

    let verifier = random_token();
    let challenge = pkce_challenge(&verifier);
    let state = random_token();
    let nonce = random_token();

    let auth_url = build_authorization_url(&client_id, &redirect_uri, &state, &nonce, &challenge);

    put_pending(
        state.clone(),
        PendingSignIn {
            verifier,
            nonce: nonce.clone(),
            redirect_uri,
        },
    );

    let expected_state = state.clone();
    let cancel = register_cancel(state.clone());
    let spawned = std::thread::Builder::new()
        .name("maho-google-signin".to_string())
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
            "failed to spawn sign-in listener: {e}"
        )));
    }

    Ok(StartGoogleSignIn { state, auth_url })
}

fn accept_and_exchange(
    listener: &TcpListener,
    expected_state: &str,
    cancel: &AtomicBool,
) -> Result<GoogleSignInResult> {
    let start = std::time::Instant::now();
    loop {
        let mut stream = accept_with_timeout(listener, cancel, start)?;

        // A connection that never sends a complete request (a speculative
        // preconnect socket, a local security scanner probing the freshly
        // opened listening port, a bare TCP health check) must not consume the
        // flow: drop it and keep waiting for the real redirect. Before F5 this
        // single connection inherited the entire 900s budget as its read
        // timeout, so the OAuth redirect that arrived on the *next* connection
        // was never accepted and the provider page hung indefinitely.
        let request = match read_http_request(&mut stream) {
            Ok(request) if is_oauth_callback(&request) => request,
            Ok(_) => {
                write_error_response(&mut stream, "Not the Maho sign-in callback", 400);
                continue;
            }
            Err(_) => {
                let _ = stream.shutdown(Shutdown::Both);
                continue;
            }
        };

        return handle_callback(&mut stream, &request, expected_state);
    }
}

/// True when the request carries any OAuth redirect parameter, i.e. it is
/// plausibly Google's callback rather than an unrelated probe.
fn is_oauth_callback(request: &str) -> bool {
    query_param(request, "state").is_some()
        || query_param(request, "code").is_some()
        || query_param(request, "error").is_some()
}

fn handle_callback(
    stream: &mut TcpStream,
    request: &str,
    expected_state: &str,
) -> Result<GoogleSignInResult> {
    let callback_state = query_param(request, "state")
        .ok_or_else(|| CoreError::Parse("no state parameter in callback".to_string()))?;
    if callback_state != expected_state {
        write_error_response(stream, "Sign-in state mismatch (possible CSRF)", 400);
        return Err(CoreError::Parse(
            "sign-in state mismatch (possible CSRF)".to_string(),
        ));
    }

    let entry = take_pending(expected_state)
        .ok_or_else(|| CoreError::Parse("unknown sign-in state".to_string()))?;

    let code = match extract_code(request) {
        Ok(code) => code,
        Err(e) => {
            write_error_response(stream, &e.to_string(), 400);
            return Err(e);
        }
    };

    match exchange_code(&entry, &code) {
        Ok(id_token) => {
            write_success_response(stream);
            Ok(GoogleSignInResult {
                id_token,
                nonce: entry.nonce,
            })
        }
        Err(e) => {
            write_error_response(stream, &e.to_string(), 502);
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

fn resolve_relay_base_url() -> String {
    match option_env!("MAHO_RELAY_URL").filter(|v| !v.is_empty()) {
        Some(value) => value.trim_end_matches('/').to_string(),
        None => DEFAULT_RELAY_BASE_URL.to_string(),
    }
}

/// Delegates the PKCE code -> token exchange to the relay, which holds the
/// Google client secret server-side and returns only the `id_token`. The
/// desktop OAuth client type requires the secret at Google's token endpoint
/// even with PKCE, so the exchange must not happen in this client.
fn exchange_code(entry: &PendingSignIn, code: &str) -> Result<String> {
    let url = format!("{}/auth/oauth/google/exchange", resolve_relay_base_url());
    let body = serde_json::json!({
        "code": code,
        "code_verifier": entry.verifier,
        "redirect_uri": entry.redirect_uri,
    })
    .to_string();

    let response = post_json_blocking(&url, &body)?;

    let parsed: TokenResponse = serde_json::from_str(&response)
        .map_err(|e| CoreError::Parse(format!("exchange response parse failed: {e}")))?;

    parsed
        .id_token
        .filter(|t| !t.is_empty())
        .ok_or_else(|| CoreError::Parse("exchange response missing id_token".to_string()))
}

/// Blocking wrapper around the shared async client: this runs on the dedicated
/// callback thread, which has no reactor of its own.
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
            return Err(CoreError::Network(format!(
                "token exchange returned HTTP {}",
                status.as_u16()
            )));
        }
        Ok(text)
    })
}

pub fn request_target(request: &str) -> &str {
    request
        .lines()
        .next()
        .unwrap_or("")
        .split_whitespace()
        .nth(1)
        .unwrap_or("")
}

pub fn query_param(request: &str, key: &str) -> Option<String> {
    let target = request_target(request);
    let query = target.split_once('?').map(|(_, q)| q)?;
    query.split('&').find_map(|pair| {
        let (k, v) = pair.split_once('=')?;
        if k == key {
            Some(urldecode(v))
        } else {
            None
        }
    })
}

fn urldecode(value: &str) -> String {
    let bytes = value.as_bytes();
    let mut out: Vec<u8> = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        match bytes[i] {
            b'%' if i + 2 < bytes.len() => {
                let hex = std::str::from_utf8(&bytes[i + 1..i + 3]).unwrap_or("");
                match u8::from_str_radix(hex, 16) {
                    Ok(byte) => {
                        out.push(byte);
                        i += 3;
                    }
                    Err(_) => {
                        out.push(bytes[i]);
                        i += 1;
                    }
                }
            }
            b'+' => {
                out.push(b' ');
                i += 1;
            }
            b => {
                out.push(b);
                i += 1;
            }
        }
    }
    String::from_utf8_lossy(&out).into_owned()
}

pub fn extract_code(request: &str) -> Result<String> {
    if let Some(error) = query_param(request, "error") {
        return Err(CoreError::Parse(format!("sign-in denied: {error}")));
    }
    query_param(request, "code")
        .ok_or_else(|| CoreError::Parse("no authorization code in callback".to_string()))
}

/// The provider controls the `error=` value reflected onto this page, so it must
/// be escaped or the loopback page becomes a reflected-XSS sink.
pub fn html_escape(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
        .replace('\'', "&#39;")
}

pub fn error_response_body(msg: &str) -> String {
    format!(
        "<html><body><h2>Sign in failed</h2><p>Error: {}</p></body></html>",
        html_escape(msg)
    )
}

/// Reads until the header terminator rather than once: a single read can
/// truncate a request the kernel splits across TCP segments, losing `state`.
pub fn read_http_request<R: std::io::Read>(reader: &mut R) -> Result<String> {
    let mut data: Vec<u8> = Vec::new();
    let mut buf = [0u8; 1024];
    loop {
        let n = reader
            .read(&mut buf)
            .map_err(|e| CoreError::Network(format!("callback read failed: {e}")))?;
        if n == 0 {
            break;
        }
        data.extend_from_slice(&buf[..n]);
        if data.windows(4).any(|w| w == b"\r\n\r\n") || data.len() >= MAX_REQUEST_BYTES {
            break;
        }
    }
    Ok(String::from_utf8_lossy(&data).into_owned())
}

/// The full loopback success response. Built rather than stored as a constant
/// so `Content-Length` always matches the body: a length-delimited body
/// completes the moment it is written, instead of depending on how the peer
/// interprets the socket teardown.
fn success_response() -> String {
    format!(
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: {}\r\n\r\n{}",
        SUCCESS_BODY.len(),
        SUCCESS_BODY
    )
}

fn write_success_response(stream: &mut TcpStream) {
    let response = success_response();
    if let Err(e) = stream.write_all(response.as_bytes()) {
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
    eprintln!("google sign-in callback stream error: {err}");
}

#[cfg(test)]
#[path = "google_identity_tests.rs"]
mod google_identity_tests;
