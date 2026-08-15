// Copyright 2026 Maho Browser. All rights reserved.

//! Loopback-listener OAuth2 sign-in, ported from the Tauri app's
//! `run_local_oauth2_authorization` (`mail/src-tauri/src/commands/oauth2.rs`).
//!
//! The Tauri app runs the whole flow inline: bind a loopback listener, open the
//! system browser, block on `accept()` with a 300s timeout, extract the code,
//! and return it to the caller. The browser splits that in two, because only
//! the C++ layer can open a Maho tab: [`sign_in_with_oauth_loopback`] binds the
//! ephemeral listener and returns `{state, auth_url}` immediately for the
//! browser to open, then a background thread catches the redirect, validates
//! the CSRF `state`, and drives the existing [`complete_oauth`] exchange.
//!
//! Uses blocking `std::net` on a dedicated `std::thread` (no tokio in this
//! path); `TcpStream::set_read_timeout` caps the request read at ~300s,
//! mirroring the Tauri app's `accept()` timeout.

use std::io::Write;
use std::net::{Shutdown, TcpListener, TcpStream};
#[cfg(target_os = "linux")]
use std::os::fd::{BorrowedFd, FromRawFd, IntoRawFd};
use std::time::Duration;

use crate::error::{MailFfiError, Result};
use crate::oauth::{
    complete_oauth, start_oauth_with_options, StartOAuthOptions, StartOAuthResponse,
};
use crate::state;

const CALLBACK_TIMEOUT: Duration = Duration::from_secs(30 * 60);
#[cfg(target_os = "linux")]
const LOOPBACK_LISTENER_FD_ENV: &str = "MAHO_MAIL_OAUTH_LOOPBACK_FD";

const SUCCESS_RESPONSE: &str = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n<html><head><meta charset=\"utf-8\"></head><body><h2>Signed in</h2><p>Returning to Maho\u{2026}</p><script>window.close()</script></body></html>";

/// Bind an ephemeral loopback listener, mint the PKCE authorization URL bound to
/// `http://127.0.0.1:{port}`, and spawn the background thread that catches the
/// redirect and completes onboarding. Returns `{state, auth_url}` immediately —
/// before the redirect arrives — for the C++ layer to open in a tab.
pub fn sign_in_with_oauth_loopback(provider: &str) -> Result<StartOAuthResponse> {
    sign_in_with_oauth_loopback_with_options(provider, StartOAuthOptions::default())
}

pub fn sign_in_with_oauth_loopback_with_options(
    provider: &str,
    options: StartOAuthOptions,
) -> Result<StartOAuthResponse> {
    let (listener, started) = prepare_loopback_oauth(provider, "", options)?;
    let expected_state = started.state.clone();
    let provider_owned = provider.to_string();
    let spawned = std::thread::Builder::new()
        .name("maho-mail-oauth".to_string())
        .spawn(move || run_callback_listener(listener, provider_owned, expected_state));

    if let Err(e) = spawned {
        state::pending_pkce().take(&started.state);
        return Err(MailFfiError::OAuth(format!(
            "failed to spawn oauth callback listener: {e}"
        )));
    }

    Ok(started)
}

fn prepare_loopback_oauth(
    provider: &str,
    client_id: &str,
    options: StartOAuthOptions,
) -> Result<(TcpListener, StartOAuthResponse)> {
    let listener = oauth_loopback_listener()?;
    let port = listener
        .local_addr()
        .map_err(|e| MailFfiError::OAuth(format!("loopback local_addr failed: {e}")))?
        .port();
    log::info!("[mail-ffi] oauth loopback listening on 127.0.0.1:{port}");
    let redirect_uri = format!("http://127.0.0.1:{port}");
    let started = start_oauth_with_options(provider, client_id, &redirect_uri, options)?;
    Ok((listener, started))
}

#[cfg(target_os = "linux")]
fn oauth_loopback_listener() -> Result<TcpListener> {
    let raw_fd = std::env::var(LOOPBACK_LISTENER_FD_ENV)
        .map_err(|_| MailFfiError::OAuth("sandboxed loopback listener is unavailable".to_string()))?
        .parse::<i32>()
        .map_err(|_| {
            MailFfiError::OAuth("sandboxed loopback listener fd is invalid".to_string())
        })?;
    // SAFETY: the browser helper creates and retains this descriptor for the
    // process lifetime before entering seccomp. Duplicate it so each OAuth
    // attempt owns only its independent listener handle.
    let owned = unsafe { BorrowedFd::borrow_raw(raw_fd) }
        .try_clone_to_owned()
        .map_err(|e| MailFfiError::OAuth(format!("loopback listener clone failed: {e}")))?;
    // SAFETY: `owned` is a duplicated listening TCP socket and ownership is
    // transferred exactly once into TcpListener.
    Ok(unsafe { TcpListener::from_raw_fd(owned.into_raw_fd()) })
}

#[cfg(not(target_os = "linux"))]
fn oauth_loopback_listener() -> Result<TcpListener> {
    TcpListener::bind("127.0.0.1:0")
        .map_err(|e| MailFfiError::OAuth(format!("loopback bind failed: {e}")))
}

fn run_callback_listener(listener: TcpListener, provider: String, expected_state: String) {
    let result = accept_and_complete(&listener, &expected_state);
    state::cancellation_store().remove(&expected_state);
    state::pending_pkce().take(&expected_state);
    match result {
        Ok(email) => log::info!("[mail-ffi] oauth loopback onboarded {email} ({provider})"),
        Err(e) => log::error!("[mail-ffi] oauth loopback failed ({provider}): {e}"),
    }
}

fn accept_and_complete(listener: &TcpListener, expected_state: &str) -> Result<String> {
    listener
        .set_nonblocking(true)
        .map_err(|e| MailFfiError::OAuth(format!("set_nonblocking failed: {e}")))?;

    let start_time = std::time::Instant::now();

    let mut stream = loop {
        if state::cancellation_store().is_cancelled(expected_state) {
            return Err(MailFfiError::OAuth("OAuth sign-in cancelled".to_string()));
        }
        if start_time.elapsed() >= CALLBACK_TIMEOUT {
            return Err(MailFfiError::OAuth(
                "OAuth sign-in timed out waiting for redirect".to_string(),
            ));
        }

        match listener.accept() {
            Ok((stream, _)) => {
                break stream;
            }
            Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                std::thread::sleep(Duration::from_millis(50));
            }
            Err(e) => {
                return Err(MailFfiError::OAuth(format!("callback accept failed: {e}")));
            }
        }
    };

    stream
        .set_nonblocking(false)
        .map_err(|e| MailFfiError::OAuth(format!("failed to set stream blocking: {e}")))?;
    stream
        .set_read_timeout(Some(CALLBACK_TIMEOUT))
        .map_err(|e| MailFfiError::OAuth(format!("callback set_read_timeout failed: {e}")))?;

    let request = read_http_request(&mut stream)?;

    let callback_state = extract_state_from_request(&request)?;
    if callback_state != expected_state {
        write_error_response(&mut stream, "OAuth state mismatch (possible CSRF)", 400);
        return Err(MailFfiError::OAuth(
            "OAuth state mismatch (possible CSRF)".to_string(),
        ));
    }
    let code = match extract_code_from_request(&request) {
        Ok(c) => c,
        Err(e) => {
            write_error_response(&mut stream, &format!("OAuth failed: {e}"), 400);
            return Err(e);
        }
    };

    let ctx = state::ctx()?;
    let mut conn = ctx
        .pool
        .get()
        .map_err(|e| MailFfiError::Pool(e.to_string()))?;

    match complete_oauth(&mut conn, &ctx.credential_key, &callback_state, &code) {
        Ok(account) => {
            write_success_response(&mut stream);
            Ok(account.email)
        }
        Err(e) => {
            write_error_response(&mut stream, &format!("OAuth completion failed: {e}"), 500);
            Err(e)
        }
    }
}

fn write_success_response(stream: &mut TcpStream) {
    if let Err(e) = stream.write_all(SUCCESS_RESPONSE.as_bytes()) {
        log::error!("[mail-ffi] oauth callback: failed to write response: {e}");
    }
    if let Err(e) = stream.flush() {
        log::error!("[mail-ffi] oauth callback: failed to flush stream: {e}");
    }
    if let Err(e) = stream.shutdown(Shutdown::Both) {
        log::error!("[mail-ffi] oauth callback: failed to shutdown stream: {e}");
    }
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
        log::error!("[mail-ffi] oauth callback: failed to write error response: {e}");
    }
    let _ = stream.flush();
    let _ = stream.shutdown(Shutdown::Both);
}

/// The request-target (path + query) from an HTTP request line, e.g.
/// `/callback?code=abc&state=xyz` from `GET /callback?code=abc&state=xyz HTTP/1.1`.
fn request_target(request: &str) -> &str {
    request
        .lines()
        .next()
        .unwrap_or("")
        .split_whitespace()
        .nth(1)
        .unwrap_or("")
}

fn parse_callback_url(request: &str) -> Result<reqwest::Url> {
    let target = request_target(request);
    reqwest::Url::parse(&format!("http://127.0.0.1{target}"))
        .map_err(|e| MailFfiError::OAuth(format!("callback url parse failed: {e}")))
}

fn query_value(url: &reqwest::Url, key: &str) -> Option<String> {
    url.query_pairs()
        .find(|(k, _)| k == key)
        .map(|(_, v)| v.into_owned())
}

/// Extract the authorization `code` from the redirect request, surfacing an
/// `error=` denial first — mirroring the Tauri app's `extract_code_from_request`.
fn extract_code_from_request(request: &str) -> Result<String> {
    let url = parse_callback_url(request)?;
    if let Some(error) = query_value(&url, "error") {
        return Err(MailFfiError::OAuth(format!("OAuth denied: {error}")));
    }
    query_value(&url, "code")
        .ok_or_else(|| MailFfiError::OAuth("No authorization code in callback".to_string()))
}

/// Extract the CSRF `state` from the redirect request — mirroring the Tauri
/// app's `extract_state_from_request`.
fn extract_state_from_request(request: &str) -> Result<String> {
    let url = parse_callback_url(request)?;
    query_value(&url, "state")
        .ok_or_else(|| MailFfiError::OAuth("No state parameter in callback".to_string()))
}

#[cfg(test)]
#[path = "loopback_tests.rs"]
mod loopback_tests;

/// HTML-escape a string for safe reflection into the error page. The OAuth
/// provider controls the `error=` query value that reaches this page, so it
/// must never be interpolated into HTML unescaped (reflected XSS).
fn html_escape(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
        .replace('\'', "&#39;")
}

/// Build the error-page HTML body with the (escaped) message.
fn error_response_body(msg: &str) -> String {
    format!(
        "<html><body><h2>Sign in failed</h2><p>Error: {}</p></body></html>",
        html_escape(msg)
    )
}

/// Read an HTTP request from the callback socket until the header terminator
/// (`\r\n\r\n`) is seen, EOF, or a size cap. A single fixed read can truncate a
/// request that the kernel delivers in multiple TCP segments (common with
/// Microsoft's long authorization codes), losing the `state`/`code` params.
fn read_http_request<R: std::io::Read>(reader: &mut R) -> Result<String> {
    let mut data: Vec<u8> = Vec::new();
    let mut buf = [0u8; 1024];
    const MAX_REQUEST_BYTES: usize = 64 * 1024;
    loop {
        let n = reader
            .read(&mut buf)
            .map_err(|e| MailFfiError::OAuth(format!("callback read failed: {e}")))?;
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

#[cfg(test)]
#[allow(clippy::disallowed_methods)]
mod ulw_t9_tests {
    use super::{error_response_body, read_http_request};
    use std::io::Read;

    struct ChunkedReader {
        chunks: Vec<Vec<u8>>,
        idx: usize,
    }
    impl Read for ChunkedReader {
        fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
            if self.idx >= self.chunks.len() {
                return Ok(0);
            }
            let chunk = &self.chunks[self.idx];
            self.idx += 1;
            let n = chunk.len().min(buf.len());
            buf[..n].copy_from_slice(&chunk[..n]);
            Ok(n)
        }
    }

    #[test]
    fn error_body_escapes_html() {
        let out = error_response_body("<script>alert('x')</script>&");
        assert!(
            !out.contains("<script>"),
            "must not reflect raw <script>: {out}"
        );
        assert!(
            out.contains("&lt;script&gt;"),
            "must contain escaped form: {out}"
        );
    }

    #[test]
    fn assembles_request_split_across_segments() {
        let mut r = ChunkedReader {
            chunks: vec![
                b"GET /cb?sta".to_vec(),
                b"te=abc&code=xyz HTTP/1.1\r\nHost: x\r\n\r\n".to_vec(),
            ],
            idx: 0,
        };
        let req = read_http_request(&mut r).unwrap();
        assert!(req.contains("state=abc"), "lost state param: {req}");
        assert!(req.contains("code=xyz"), "lost code param: {req}");
    }
}
