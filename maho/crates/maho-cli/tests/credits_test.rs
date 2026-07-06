use std::net::SocketAddr;
use std::process::Command;

use axum::routing::get;
use axum::Router;
use tempfile::TempDir;
use tokio::net::TcpListener;

async fn spawn_mock_relay_with_balance(balance_usd: f64) -> SocketAddr {
    let app = Router::new().route(
        "/billing/credit_balance",
        get(move || async move {
            axum::Json(serde_json::json!({
                "user_id": 1,
                "balance_usd": balance_usd,
                "last_purchase_at": 1_718_460_720i64,
                "last_consumption_at": 1_719_476_100i64,
            }))
        }),
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
    std::fs::write(
        &config_path,
        format!("access_token = \"{token}\"\n"),
    )
    .unwrap();
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
async fn test_balance_subcommand_prints_value() {
    let addr = spawn_mock_relay_with_balance(42.50).await;
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
        "stdout should contain $42.50, got: {stdout}"
    );
    assert!(
        stdout.to_lowercase().contains("payg credit balance"),
        "stdout should contain balance header, got: {stdout}"
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
    let addr = spawn_mock_relay_with_balance(0.0).await;
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
