use super::*;

#[test]
fn pkce_challenge_matches_rfc7636_s256_vector() {
    let verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    assert_eq!(
        pkce_challenge(verifier),
        "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"
    );
}

#[test]
fn authorization_url_carries_pkce_state_and_nonce() {
    let url = build_authorization_url(
        "client-123",
        "http://127.0.0.1:5411",
        "state-abc",
        "nonce-xyz",
        "challenge-987",
    );
    assert!(url.starts_with("https://accounts.google.com/o/oauth2/v2/auth?"));
    assert!(url.contains("client_id=client-123"));
    assert!(url.contains("state=state-abc"));
    assert!(url.contains("nonce=nonce-xyz"));
    assert!(url.contains("code_challenge=challenge-987"));
    assert!(url.contains("code_challenge_method=S256"));
    assert!(url.contains("redirect_uri=http%3A%2F%2F127.0.0.1%3A5411"));
}

#[test]
fn authorization_url_requests_identity_scopes_only() {
    let url = build_authorization_url("c", "http://127.0.0.1:1", "s", "n", "ch");
    assert!(url.contains("scope=openid%20email%20profile"));
    assert!(
        !url.contains("mail.google.com"),
        "identity sign-in must not request a Gmail scope: {url}"
    );
    assert!(
        !url.contains("calendar"),
        "unexpected calendar scope: {url}"
    );
}

#[test]
fn resolve_client_id_prefers_explicit_value() {
    assert_eq!(
        resolve_client_id("explicit-client").expect("explicit id should resolve"),
        "explicit-client"
    );
}

#[test]
fn resolve_client_id_without_baked_value_errors() {
    if option_env!("MAHO_GOOGLE_CLIENT_ID").is_none() {
        assert!(resolve_client_id("").is_err());
    }
}

#[test]
fn request_target_extracts_path_and_query() {
    assert_eq!(
        request_target("GET /cb?code=a&state=b HTTP/1.1\r\nHost: x\r\n\r\n"),
        "/cb?code=a&state=b"
    );
    assert_eq!(request_target(""), "");
}

#[test]
fn query_param_reads_named_values() {
    let req = "GET /cb?code=abc&state=xyz HTTP/1.1\r\n\r\n";
    assert_eq!(query_param(req, "code").as_deref(), Some("abc"));
    assert_eq!(query_param(req, "state").as_deref(), Some("xyz"));
    assert!(query_param(req, "missing").is_none());
}

#[test]
fn query_param_decodes_percent_and_plus() {
    let req = "GET /cb?error=access%5Fdenied&note=a+b HTTP/1.1\r\n\r\n";
    assert_eq!(query_param(req, "error").as_deref(), Some("access_denied"));
    assert_eq!(query_param(req, "note").as_deref(), Some("a b"));
}

#[test]
fn extract_code_returns_authorization_code() {
    let req = "GET /cb?code=the-code&state=s HTTP/1.1\r\n\r\n";
    assert_eq!(
        extract_code(req).expect("code should parse"),
        "the-code".to_string()
    );
}

#[test]
fn extract_code_surfaces_provider_denial_before_missing_code() {
    let req = "GET /cb?error=access_denied&state=s HTTP/1.1\r\n\r\n";
    let err = extract_code(req).expect_err("denial should error");
    assert!(err.to_string().contains("access_denied"));
}

#[test]
fn extract_code_without_code_errors() {
    assert!(extract_code("GET /cb?state=s HTTP/1.1\r\n\r\n").is_err());
}

#[test]
fn error_page_escapes_provider_controlled_message() {
    let body = error_response_body("<script>alert('x')</script>&\"");
    assert!(
        !body.contains("<script>"),
        "raw script tag reflected: {body}"
    );
    assert!(body.contains("&lt;script&gt;"));
    assert!(body.contains("&quot;"));
    assert!(body.contains("&#39;"));
}

struct ChunkedReader {
    chunks: Vec<Vec<u8>>,
    idx: usize,
}

impl std::io::Read for ChunkedReader {
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
fn read_http_request_assembles_segments_split_by_kernel() {
    let mut reader = ChunkedReader {
        chunks: vec![
            b"GET /cb?sta".to_vec(),
            b"te=abc&code=xyz HTTP/1.1\r\nHost: x\r\n\r\n".to_vec(),
        ],
        idx: 0,
    };
    let req = read_http_request(&mut reader).expect("request should assemble");
    assert_eq!(query_param(&req, "state").as_deref(), Some("abc"));
    assert_eq!(query_param(&req, "code").as_deref(), Some("xyz"));
}

#[test]
fn pending_entries_are_single_use() {
    let state = "state-single-use".to_string();
    put_pending(
        state.clone(),
        PendingSignIn {
            verifier: "v".to_string(),
            nonce: "n".to_string(),
            redirect_uri: "http://127.0.0.1:1".to_string(),
        },
    );
    assert!(take_pending(&state).is_some());
    assert!(
        take_pending(&state).is_none(),
        "a replayed state must not resolve a second time"
    );
}

#[test]
fn relay_base_url_defaults_and_trims_trailing_slash() {
    // Without a build-time override the default production relay is used, and
    // the exchange path is appended without a doubled slash.
    let base = resolve_relay_base_url();
    assert!(!base.ends_with('/'), "base url must not end with a slash");
    assert!(base.starts_with("https://"), "relay base url must be https");
}

#[test]
fn loopback_success_page_declares_utf8_charset() {
    // The success page contains a U+2026 ellipsis; without an explicit
    // charset the browser decodes the UTF-8 bytes as Latin-1 and shows "â€¦".
    let response = super::success_response();
    assert!(
        response.contains("charset=utf-8"),
        "success response must declare charset=utf-8, got: {response}"
    );
    assert!(response.contains('\u{2026}'));
    // A length-delimited body completes without depending on socket teardown.
    assert!(
        response.contains(&format!("Content-Length: {}", SUCCESS_BODY.len())),
        "success response must carry a matching Content-Length, got: {response}"
    );
}

#[test]
fn loopback_success_page_auto_closes_tab() {
    // After the redirect the loopback page should close its own tab so no
    // stale 127.0.0.1 callback tab lingers (a lingering tab can be restored
    // and later hit a dead port -> "site can't be reached").
    assert!(
        super::success_response().contains("window.close()"),
        "success page must self-close"
    );
}

#[test]
fn accept_with_timeout_returns_promptly_when_cancelled() {
    // A pre-set cancel flag must short-circuit the accept loop with a
    // cancellation error, without waiting for any redirect or the backstop.
    let listener = std::net::TcpListener::bind("127.0.0.1:0").expect("bind loopback");
    let cancel = std::sync::atomic::AtomicBool::new(true);
    let start = std::time::Instant::now();
    let result = accept_with_timeout(&listener, &cancel, start);
    let err = result.expect_err("cancelled accept must error");
    assert!(
        err.to_string().contains("cancelled"),
        "error should name cancellation, got: {err}"
    );
    assert!(
        start.elapsed() < std::time::Duration::from_secs(5),
        "cancel must short-circuit well under the backstop timeout"
    );
}

#[test]
fn cancel_google_sign_in_flags_registered_flow_only() {
    let flag = register_cancel("state-cancel-unit-1".to_string());
    assert!(!flag.load(std::sync::atomic::Ordering::SeqCst));
    assert!(
        cancel_google_sign_in("state-cancel-unit-1"),
        "registered flow must report cancelled"
    );
    assert!(
        flag.load(std::sync::atomic::Ordering::SeqCst),
        "cancel must set the shared flag"
    );
    assert!(
        !cancel_google_sign_in("state-cancel-unit-nonexistent"),
        "unknown state must be a no-op"
    );
    take_cancel("state-cancel-unit-1");
}

#[test]
fn idle_probe_connection_does_not_consume_the_flow() {
    // REGRESSION (F5): the loopback listener used to accept exactly one
    // connection and give it the entire backstop budget as its read timeout.
    // Anything that opened the port without sending a request (a speculative
    // preconnect socket, a local security scanner) therefore consumed the
    // flow, and the real OAuth redirect - which arrives on the *next*
    // connection - was never accepted. The provider page then hung until the
    // 900s backstop expired, which is what Windows certification observed.
    //
    // Here the first connection closes without sending anything, and the
    // second carries a callback. Reaching the callback at all (even to reject
    // it on state mismatch) proves the listener moved past the dead probe.
    use std::io::{Read, Write};

    let listener = std::net::TcpListener::bind("127.0.0.1:0").expect("bind loopback");
    let port = listener.local_addr().expect("local_addr").port();
    let cancel = std::sync::atomic::AtomicBool::new(false);

    let worker = std::thread::spawn(move || {
        super::accept_and_exchange(&listener, "expected-state", &cancel)
    });

    // Probe: connect, send nothing, close. Must be skipped, not consumed.
    let probe = std::net::TcpStream::connect(("127.0.0.1", port)).expect("probe connect");
    probe
        .shutdown(std::net::Shutdown::Both)
        .expect("probe shutdown");

    // Real callback on a fresh connection, with a deliberately wrong state so
    // the flow rejects it locally instead of reaching the relay.
    let mut callback = std::net::TcpStream::connect(("127.0.0.1", port)).expect("callback connect");
    callback
        .write_all(
            b"GET /?state=not-the-expected-state&code=abc HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n",
        )
        .expect("write callback request");
    let mut response = String::new();
    let _ = callback.read_to_string(&mut response);

    let err = worker
        .join()
        .expect("listener thread")
        .expect_err("mismatched state must be rejected");
    assert!(
        err.to_string().contains("state mismatch"),
        "listener must have processed the second connection, got: {err}"
    );
    assert!(
        response.contains("400"),
        "callback connection must receive the rejection response, got: {response}"
    );
}

#[test]
fn oauth_callback_predicate_ignores_unrelated_requests() {
    assert!(!super::is_oauth_callback(""));
    assert!(!super::is_oauth_callback(
        "GET /favicon.ico HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
    ));
    assert!(super::is_oauth_callback(
        "GET /?state=s&code=c HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
    ));
    assert!(super::is_oauth_callback(
        "GET /?error=access_denied HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
    ));
}
