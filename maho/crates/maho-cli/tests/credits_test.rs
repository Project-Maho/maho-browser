use std::net::SocketAddr;
use std::process::Command;

use axum::http::{HeaderMap, StatusCode};
use axum::routing::get;
use axum::Router;
use tempfile::TempDir;
use tokio::net::TcpListener;

/// Mock relay serving ONLY `/auth/subscription`, gated on a user bearer token.
/// It intentionally does not serve `/billing/credit_balance`, so a client that
/// targets the wrong (service-token) endpoint gets a 404 and fails the test.
async fn spawn_mock_subscription_relay(balance_usd: f64, lifetime_usd: f64) -> SocketAddr {
    let app = Router::new().route(
        "/auth/subscription",
        get(move |headers: HeaderMap| async move {
            let authed = headers
                .get("authorization")
                .and_then(|v| v.to_str().ok())
                .map(|v| {
                    v.strip_prefix("Bearer ")
                        .is_some_and(|t| !t.trim().is_empty())
                })
                .unwrap_or(false);
            if !authed {
                return (
                    StatusCode::UNAUTHORIZED,
                    axum::Json(serde_json::json!({ "error": "missing_user_token" })),
                );
            }
            (
                StatusCode::OK,
                axum::Json(serde_json::json!({
                    "tier": "payg",
                    "subscription_status": "active",
                    "subscription_expires_at": null,
                    "period_start_at": null,
                    "anniversary_reset_day": null,
                    "tier_ceiling_usd": 0.5,
                    "credit_balance_usd": balance_usd,
                    "lifetime_purchased_usd": lifetime_usd,
                    "has_payment_method": true
                })),
            )
        }),
    );
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr = listener.local_addr().unwrap();
    tokio::spawn(async move {
        axum::serve(listener, app).await.unwrap();
    });
    addr
}

/// Mock relay whose `/auth/subscription` always fails with the given status.
async fn spawn_mock_failing_relay(status: StatusCode) -> SocketAddr {
    let app = Router::new().route(
        "/auth/subscription",
        get(move || async move { (status, axum::Json(serde_json::json!({ "error": "boom" }))) }),
    );
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr = listener.local_addr().unwrap();
    tokio::spawn(async move {
        axum::serve(listener, app).await.unwrap();
    });
    addr
}

fn maho_cmd_with_token(config_dir: &std::path::Path, token: &str) -> Command {
    let config_path = config_dir.join("cli.toml");
    std::fs::write(&config_path, format!("access_token = \"{token}\"\n")).unwrap();
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_maho"));
    cmd.env("MAHO_CONFIG_DIR", config_dir);
    cmd.env("MAHO_CLI_TEST", "1");
    cmd.stdin(std::process::Stdio::null());
    cmd
}

fn maho_cmd_no_token(config_dir: &std::path::Path) -> Command {
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_maho"));
    cmd.env("MAHO_CONFIG_DIR", config_dir);
    cmd.env("MAHO_CLI_TEST", "1");
    cmd.stdin(std::process::Stdio::null());
    cmd
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn test_balance_subcommand_prints_value_from_subscription_contract() {
    let addr = spawn_mock_subscription_relay(42.50, 100.0).await;
    let relay_url = format!("http://{addr}");
    let tmp = TempDir::new().unwrap();

    let output = maho_cmd_with_token(tmp.path(), "test-access-token")
        .args(["--relay-url", &relay_url, "balance"])
        .output()
        .expect("failed to run maho balance");

    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        output.status.success(),
        "balance should succeed: stdout={stdout}, stderr={stderr}"
    );
    assert!(
        stdout.contains("$42.50"),
        "stdout should contain credit balance $42.50, got: {stdout}"
    );
    assert!(
        stdout.contains("$100.00"),
        "stdout should contain lifetime purchased $100.00, got: {stdout}"
    );
    assert!(
        stdout.to_lowercase().contains("payg credit balance"),
        "stdout should contain balance header, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn test_balance_requires_user_token_endpoint() {
    // The mock only accepts a bearer token on /auth/subscription. A success
    // here proves the CLI targets the user-authenticated endpoint and sends
    // the caller's own token, not the service-token /billing/credit_balance.
    let addr = spawn_mock_subscription_relay(7.0, 7.0).await;
    let relay_url = format!("http://{addr}");
    let tmp = TempDir::new().unwrap();

    let output = maho_cmd_with_token(tmp.path(), "user-token-abc")
        .args(["--relay-url", &relay_url, "balance"])
        .output()
        .expect("failed to run maho balance");

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(output.status.success(), "balance should succeed: {stdout}");
    assert!(stdout.contains("$7.00"), "got: {stdout}");
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn test_balance_surfaces_relay_failure_instead_of_faking_success() {
    let addr = spawn_mock_failing_relay(StatusCode::UNAUTHORIZED).await;
    let relay_url = format!("http://{addr}");
    let tmp = TempDir::new().unwrap();

    let output = maho_cmd_with_token(tmp.path(), "test-access-token")
        .args(["--relay-url", &relay_url, "balance"])
        .output()
        .expect("failed to run maho balance");

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        !output.status.success(),
        "balance must fail when relay rejects the token, got stdout: {stdout}"
    );
    assert!(
        !stdout.contains("PAYG Credit Balance"),
        "must not print a balance on failure, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn test_buy_credits_amount_10_prints_url() {
    let tmp = TempDir::new().unwrap();

    let output = maho_cmd_no_token(tmp.path())
        .args(["buy-credits", "--amount", "10"])
        .output()
        .expect("failed to run maho buy-credits");

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("credit-pack-10"),
        "stdout should contain credit-pack-10 URL, got: {stdout}"
    );
    assert!(
        stdout.contains("$10"),
        "stdout should mention $10, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn test_buy_credits_suppresses_browser_open_messages_in_test_env() {
    let tmp = TempDir::new().unwrap();

    let output = maho_cmd_no_token(tmp.path())
        .args(["buy-credits", "--amount", "50"])
        .output()
        .expect("failed to run maho buy-credits");

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("Checkout URL:"),
        "URL header should print even with MAHO_CLI_TEST set, got: {stdout}"
    );
    assert!(
        !stdout.contains("Opened in Maho Browser"),
        "Maho-Browser open status must be suppressed when MAHO_CLI_TEST is set, got: {stdout}"
    );
    assert!(
        !stdout.contains("Opened in your default browser"),
        "default-browser open status must be suppressed when MAHO_CLI_TEST is set, got: {stdout}"
    );
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn test_balance_without_login_errors() {
    let addr = spawn_mock_subscription_relay(0.0, 0.0).await;
    let relay_url = format!("http://{addr}");
    let tmp = TempDir::new().unwrap();

    let output = maho_cmd_no_token(tmp.path())
        .args(["--relay-url", &relay_url, "balance"])
        .output()
        .expect("failed to run maho balance");

    assert!(
        !output.status.success(),
        "balance without login should fail"
    );
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("Not logged in"),
        "stderr should say 'Not logged in', got: {stderr}"
    );
}
