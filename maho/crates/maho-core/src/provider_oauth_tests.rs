use super::*;
use std::io::Read;
use std::sync::mpsc;

const SIGNAL_TIMEOUT: Duration = Duration::from_secs(10);

fn config() -> ProviderOAuthConfig {
    ProviderOAuthConfig {
        provider: "anthropic".to_string(),
        client_id: "client-abc".to_string(),
        authorize_url: "https://example.test/oauth/authorize".to_string(),
        token_url: "https://example.test/oauth/token".to_string(),
        scopes: "user:inference user:profile".to_string(),
        audience: String::new(),
        redirect_port: 0,
        redirect_path: "/callback".to_string(),
        extra_authorize_params: HashMap::new(),
    }
}

fn loopback_port(auth_url: &str) -> u16 {
    let marker = "localhost%3A";
    let start = auth_url.find(marker).expect("redirect uri in auth url") + marker.len();
    auth_url[start..]
        .chars()
        .take_while(char::is_ascii_digit)
        .collect::<String>()
        .parse()
        .expect("loopback port")
}

#[test]
fn authorization_url_carries_pkce_and_state() {
    let mut cfg = config();
    cfg.extra_authorize_params
        .insert("code".to_string(), "true".to_string());

    let url = build_authorization_url(&cfg, "http://localhost:9999/callback", "state-1", "chal-1");

    assert!(url.starts_with("https://example.test/oauth/authorize?"));
    assert!(url.contains("client_id=client-abc"));
    assert!(url.contains("response_type=code"));
    assert!(url.contains("state=state-1"));
    assert!(url.contains("code_challenge=chal-1"));
    assert!(url.contains("code_challenge_method=S256"));
    assert!(url.contains("scope=user%3Ainference%20user%3Aprofile"));
    assert!(url.contains("redirect_uri=http%3A%2F%2Flocalhost%3A9999%2Fcallback"));
    assert!(url.contains("code=true"));
}

#[test]
fn authorization_url_appends_to_existing_query() {
    let mut cfg = config();
    cfg.authorize_url = "https://example.test/oauth/authorize?tenant=eu".to_string();

    let url = build_authorization_url(&cfg, "http://localhost:1/", "s", "c");

    assert!(url.contains("authorize?tenant=eu&client_id=client-abc"));
}

#[test]
fn validate_config_rejects_incomplete_or_insecure_configs() {
    let mut blank_client = config();
    blank_client.client_id = "  ".to_string();
    assert!(validate_config(&blank_client).is_err());

    let mut blank_provider = config();
    blank_provider.provider = String::new();
    assert!(validate_config(&blank_provider).is_err());

    let mut http_authorize = config();
    http_authorize.authorize_url = "http://example.test/oauth/authorize".to_string();
    assert!(validate_config(&http_authorize).is_err());

    let mut http_token = config();
    http_token.token_url = "http://example.test/oauth/token".to_string();
    assert!(validate_config(&http_token).is_err());

    assert!(validate_config(&config()).is_ok());
}

#[test]
fn token_response_success_computes_absolute_expiry() {
    let before = unix_now();
    let tokens = parse_token_response(
        "anthropic",
        r#"{"access_token":"at-1","refresh_token":"rt-1","expires_in":3600,"token_type":"Bearer","scope":"user:inference"}"#,
        String::new(),
    )
    .expect("tokens");

    assert_eq!(tokens.provider, "anthropic");
    assert_eq!(tokens.access_token, "at-1");
    assert_eq!(tokens.refresh_token, "rt-1");
    assert_eq!(tokens.scope, "user:inference");
    assert!(tokens.expires_at >= before + 3600);
}

#[test]
fn token_response_keeps_existing_refresh_token_when_omitted() {
    let tokens = parse_token_response(
        "openai",
        r#"{"access_token":"at-2"}"#,
        "previous-refresh".to_string(),
    )
    .expect("tokens");

    assert_eq!(tokens.refresh_token, "previous-refresh");
    assert_eq!(tokens.token_type, "Bearer");
    assert_eq!(tokens.expires_at, 0);
}

#[test]
fn token_response_surfaces_provider_error_payload() {
    let err = parse_token_response(
        "openai",
        r#"{"error":"invalid_grant","error_description":"code already redeemed"}"#,
        String::new(),
    )
    .expect_err("error");

    let message = err.to_string();
    assert!(message.contains("invalid_grant"), "{message}");
    assert!(message.contains("code already redeemed"), "{message}");
}

#[test]
fn token_response_without_access_token_is_rejected() {
    let err = parse_token_response("openai", r#"{"token_type":"Bearer"}"#, String::new())
        .expect_err("error");
    assert!(err.to_string().contains("access_token"));
}

#[test]
fn refresh_requires_a_refresh_token() {
    let err = refresh_provider_oauth(&config(), "   ").expect_err("error");
    assert!(err.to_string().contains("refresh token"));
}

#[test]
fn callback_with_mismatched_state_is_rejected_before_code_exchange() {
    let (tx, rx) = mpsc::channel();
    let started = start_provider_oauth(config(), move |result| {
        let _ = tx.send(result.err().map(|e| e.to_string()));
    })
    .expect("start");

    let port = loopback_port(&started.auth_url);
    let mut stream = TcpStream::connect(("127.0.0.1", port)).expect("connect");
    stream
        .write_all(b"GET /callback?code=abc&state=forged HTTP/1.1\r\nHost: localhost\r\n\r\n")
        .expect("write request");
    let mut response = String::new();
    stream.read_to_string(&mut response).expect("read response");

    assert!(response.starts_with("HTTP/1.1 400"), "{response}");
    assert!(cancel_provider_oauth(&started.state), "mismatch must leave sign-in pending");
    let outcome = rx.recv_timeout(SIGNAL_TIMEOUT).expect("callback signal");
    let message = outcome.expect("cancelled sign-in must fail");
    assert!(message.contains("cancelled"), "{message}");
}

#[test]
fn callback_reports_provider_denial() {
    let (tx, rx) = mpsc::channel();
    let started = start_provider_oauth(config(), move |result| {
        let _ = tx.send(result.err().map(|e| e.to_string()));
    })
    .expect("start");

    let port = loopback_port(&started.auth_url);
    let mut stream = TcpStream::connect(("127.0.0.1", port)).expect("connect");
    let request = format!(
        "GET /callback?error=access_denied&state={} HTTP/1.1\r\nHost: localhost\r\n\r\n",
        started.state
    );
    stream.write_all(request.as_bytes()).expect("write request");
    let mut response = String::new();
    stream.read_to_string(&mut response).expect("read response");

    let outcome = rx.recv_timeout(SIGNAL_TIMEOUT).expect("callback signal");
    let message = outcome.expect("denied sign-in must fail");
    assert!(message.contains("access_denied"), "{message}");
}

fn oauth_loopback_completes_after_unrelated(first_request: Option<&str>) {
    use tokio::io::{AsyncReadExt, AsyncWriteExt};

    let token_listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let token_address = token_listener.local_addr().unwrap();
    token_listener.set_nonblocking(true).unwrap();
    let token_server = std::thread::spawn(move || {
        let runtime = tokio::runtime::Builder::new_current_thread().enable_all().build().unwrap();
        runtime.block_on(async move {
            tokio::time::timeout(SIGNAL_TIMEOUT, async move {
                let listener = tokio::net::TcpListener::from_std(token_listener).unwrap();
                let (mut stream, _) = listener.accept().await.unwrap();
                let mut headers = Vec::new();
                while !headers.ends_with(b"\r\n\r\n") {
                    headers.push(stream.read_u8().await.unwrap());
                }
                let headers = String::from_utf8(headers).unwrap();
                let length: usize = headers.lines().find_map(|line| {
                    let (name, value) = line.split_once(':')?;
                    name.eq_ignore_ascii_case("content-length").then(|| value.trim().parse().unwrap())
                }).unwrap();
                let mut body = vec![0; length];
                stream.read_exact(&mut body).await.unwrap();
                let body: serde_json::Value = serde_json::from_slice(&body).unwrap();
                assert_eq!(body["code"], "browser-code");
                assert_eq!(body["code_verifier"], "test-verifier");
                let tokens = r#"{"access_token":"test-access","refresh_token":"test-refresh"}"#;
                let response = format!("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{tokens}", tokens.len());
                stream.write_all(response.as_bytes()).await.unwrap();
            }).await
        })
    });

    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let address = listener.local_addr().unwrap();
    let state = random_token();
    let mut cfg = config();
    // Inject only the token endpoint, not the exchange or callback machinery.
    cfg.token_url = format!("http://{token_address}/token");
    put_pending(state.clone(), PendingFlow {
        verifier: "test-verifier".to_string(),
        redirect_uri: format!("http://{address}/callback"),
        config: cfg,
    });
    let (tx, rx) = mpsc::channel();
    let expected_state = state.clone();
    let worker = std::thread::spawn(move || {
        let result = accept_and_exchange(&listener, &expected_state, &AtomicBool::new(false));
        take_pending(&expected_state);
        tx.send(result).unwrap();
    });

    let mut stray = TcpStream::connect(address).unwrap();
    stray.set_read_timeout(Some(SIGNAL_TIMEOUT)).unwrap();
    if let Some(request) = first_request {
        stray.write_all(request.as_bytes()).unwrap();
    }
    stray.shutdown(Shutdown::Write).unwrap();
    let mut rejected = String::new();
    stray.read_to_string(&mut rejected).unwrap();
    drop(stray);

    let browser_response = (|| -> std::io::Result<String> {
        let mut browser = TcpStream::connect(address)?;
        browser.set_read_timeout(Some(SIGNAL_TIMEOUT))?;
        let request = format!("GET /callback?code=browser-code&state={state} HTTP/1.1\r\nHost: localhost\r\n\r\n");
        browser.write_all(request.as_bytes())?;
        let mut response = String::new();
        browser.read_to_string(&mut response)?;
        Ok(response)
    })();
    let result = rx.recv_timeout(SIGNAL_TIMEOUT).expect("completion signal");
    worker.join().unwrap();
    let tokens = result.expect("stray connection must not end sign-in");
    token_server.join().unwrap().expect("token exchange signal");
    assert_eq!(tokens.access_token, "test-access");
    assert_eq!(tokens.refresh_token, "test-refresh");
    assert!(rejected.starts_with("HTTP/1.1 400"), "{rejected}");
    assert!(browser_response.unwrap().starts_with("HTTP/1.1 200"));
}

#[test]
fn oauth_loopback_completes_after_empty_preconnect() {
    oauth_loopback_completes_after_unrelated(None);
}

#[test]
fn oauth_loopback_completes_after_mismatched_state() {
    oauth_loopback_completes_after_unrelated(Some(
        "GET /callback?code=forged&state=wrong-state HTTP/1.1\r\nHost: localhost\r\n\r\n",
    ));
}

#[test]
fn oauth_loopback_completes_after_favicon_request() {
    oauth_loopback_completes_after_unrelated(Some(
        "GET /favicon.ico HTTP/1.1\r\nHost: localhost\r\n\r\n",
    ));
}

#[test]
fn cancel_stops_a_pending_sign_in() {
    let (tx, rx) = mpsc::channel();
    let started = start_provider_oauth(config(), move |result| {
        let _ = tx.send(result.err().map(|e| e.to_string()));
    })
    .expect("start");

    assert!(cancel_provider_oauth(&started.state));

    let outcome = rx.recv_timeout(SIGNAL_TIMEOUT).expect("callback signal");
    let message = outcome.expect("cancelled sign-in must fail");
    assert!(message.contains("cancelled"), "{message}");
    assert!(!cancel_provider_oauth(&started.state));
}
