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

    let outcome = rx.recv_timeout(SIGNAL_TIMEOUT).expect("callback signal");
    let message = outcome.expect("state mismatch must fail");
    assert!(message.contains("state mismatch"), "{message}");
    assert!(response.starts_with("HTTP/1.1 400"), "{response}");
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
