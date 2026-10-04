//! End-to-end integration tests for the `maho` CLI binary.
//!
//! Spawns a mock relay server, then exercises
//! the full login → whoami → logout flow via `std::process::Command`.

use std::net::SocketAddr;
use std::process::Command;
use std::sync::{Arc, Mutex};

use axum::body::Body;
use axum::extract::{Request, State};
use axum::response::IntoResponse;
use axum::routing::{get, post};
use axum::Router;
use tempfile::TempDir;
use tokio::net::TcpListener;

// ---------- Shared state for mock servers ----------

/// Mock relay state: stores one user (email, password, token) after signup.
#[derive(Clone, Default)]
struct RelayState {
    users: Arc<Mutex<Vec<MockUser>>>,
}

#[derive(Clone)]
struct MockUser {
    email: String,
    password: String,
    access_token: String,
    refresh_token: String,
}

// ---------- Mock Relay ----------

fn mock_relay(state: RelayState) -> Router {
    Router::new()
        .route("/auth/signup", post(handle_signup))
        .route("/auth/login", post(handle_login))
        .route("/auth/me", get(handle_me))
        .route("/health", get(|| async { "ok" }))
        .with_state(state)
}

async fn handle_signup(
    State(state): State<RelayState>,
    axum::Json(body): axum::Json<serde_json::Value>,
) -> impl IntoResponse {
    let email = body["email"].as_str().unwrap_or_default().to_string();
    let password = body["password"].as_str().unwrap_or_default().to_string();

    if email.is_empty() || password.is_empty() {
        return (
            axum::http::StatusCode::BAD_REQUEST,
            axum::Json(serde_json::json!({"error": "email and password required"})),
        );
    }

    if password.len() < 12 {
        return (
            axum::http::StatusCode::BAD_REQUEST,
            axum::Json(serde_json::json!({"error": "password must be at least 12 characters"})),
        );
    }

    let users = state.users.lock().unwrap();
    if users.iter().any(|u| u.email == email) {
        return (
            axum::http::StatusCode::CONFLICT,
            axum::Json(serde_json::json!({"error": "email already exists"})),
        );
    }
    drop(users);

    let access_token = format!("at_{}", email.replace(['@', '.'], "_"));
    let refresh_token = format!("rt_{}", email.replace(['@', '.'], "_"));

    let user = MockUser {
        email: email.clone(),
        password,
        access_token: access_token.clone(),
        refresh_token: refresh_token.clone(),
    };
    state.users.lock().unwrap().push(user);

    (
        axum::http::StatusCode::OK,
        axum::Json(serde_json::json!({
            "access_token": access_token,
            "refresh_token": refresh_token,
            "token_type": "Bearer",
            "expires_at": 9999999999u64,
            "refresh_expires_at": 9999999999u64,
        })),
    )
}

async fn handle_login(
    State(state): State<RelayState>,
    axum::Json(body): axum::Json<serde_json::Value>,
) -> impl IntoResponse {
    let email = body["email"].as_str().unwrap_or_default().to_string();
    let password = body["password"].as_str().unwrap_or_default().to_string();

    let users = state.users.lock().unwrap();
    let user = users
        .iter()
        .find(|u| u.email == email && u.password == password);

    match user {
        Some(u) => (
            axum::http::StatusCode::OK,
            axum::Json(serde_json::json!({
                "access_token": u.access_token,
                "refresh_token": u.refresh_token,
                "token_type": "Bearer",
                "expires_at": 9999999999u64,
                "refresh_expires_at": 9999999999u64,
            })),
        ),
        None => (
            axum::http::StatusCode::UNAUTHORIZED,
            axum::Json(serde_json::json!({"error": "invalid credentials"})),
        ),
    }
}

async fn handle_me(State(state): State<RelayState>, req: Request<Body>) -> impl IntoResponse {
    let auth_header = req
        .headers()
        .get("authorization")
        .and_then(|v| v.to_str().ok())
        .unwrap_or_default()
        .to_string();

    let token = auth_header.strip_prefix("Bearer ").unwrap_or_default();

    let users = state.users.lock().unwrap();
    let user = users.iter().find(|u| u.access_token == token);

    match user {
        Some(u) => (
            axum::http::StatusCode::OK,
            axum::Json(serde_json::json!({
                "email": u.email,
                "tier": null,
            })),
        ),
        None => (
            axum::http::StatusCode::UNAUTHORIZED,
            axum::Json(serde_json::json!({"error": "invalid token"})),
        ),
    }
}

// ---------- Test helpers ----------

async fn spawn_mock_relay() -> (SocketAddr, RelayState) {
    let state = RelayState::default();
    let app = mock_relay(state.clone());
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr = listener.local_addr().unwrap();
    tokio::spawn(async move {
        axum::serve(listener, app).await.unwrap();
    });
    (addr, state)
}

fn maho_cmd(config_dir: &std::path::Path) -> Command {
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_maho"));
    cmd.env("MAHO_CONFIG_DIR", config_dir);
    // Prevent interactive prompts from blocking
    cmd.stdin(std::process::Stdio::null());
    cmd
}

// ---------- Tests ----------

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn test_login_signup_whoami_logout_flow() {
    let (relay_addr, _relay_state) = spawn_mock_relay().await;
    let tmp = TempDir::new().unwrap();
    let relay_url = format!("http://{}", relay_addr);

    // 1. Signup
    let output = maho_cmd(tmp.path())
        .args([
            "--relay-url",
            &relay_url,
            "login",
            "--signup",
            "--email",
            "e2e@example.com",
            "--password",
            "correct horse battery staple",
        ])
        .output()
        .expect("failed to run maho");

    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        output.status.success(),
        "signup failed: stdout={stdout}, stderr={stderr}"
    );
    assert!(stdout.contains("Logged in as e2e@example.com"));

    // Verify config file was written with token
    let config_path = tmp.path().join("cli.toml");
    assert!(config_path.exists(), "config file should exist after login");
    let config_content = std::fs::read_to_string(&config_path).unwrap();
    assert!(config_content.contains("access_token"));

    // 2. Whoami
    let output = maho_cmd(tmp.path())
        .args(["--relay-url", &relay_url, "whoami"])
        .output()
        .expect("failed to run maho whoami");

    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        output.status.success(),
        "whoami failed: stdout={stdout}, stderr={stderr}"
    );
    assert!(
        stdout.contains("e2e@example.com"),
        "whoami should show email, got: {stdout}"
    );
    assert!(
        stdout.contains("free"),
        "whoami should show free tier, got: {stdout}"
    );

    // 3. Config show
    let output = maho_cmd(tmp.path())
        .args(["config", "show"])
        .output()
        .expect("failed to run maho config show");

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(output.status.success());
    // Token should be redacted (partial display)
    assert!(
        stdout.contains("..."),
        "config show should redact tokens, got: {stdout}"
    );
    assert!(
        stdout.contains("e2e@example.com"),
        "config show should display email, got: {stdout}"
    );

    // 4. Logout
    let output = maho_cmd(tmp.path())
        .args(["logout"])
        .output()
        .expect("failed to run maho logout");

    assert!(output.status.success());
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("Logged out"));

    // Config file should be removed
    assert!(
        !config_path.exists(),
        "config file should be removed after logout"
    );
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn test_signup_subcommand_alias() {
    let (relay_addr, _) = spawn_mock_relay().await;
    let tmp = TempDir::new().unwrap();
    let relay_url = format!("http://{}", relay_addr);

    // Use the `signup` subcommand directly
    let output = maho_cmd(tmp.path())
        .args([
            "--relay-url",
            &relay_url,
            "signup",
            "--email",
            "alias@example.com",
            "--password",
            "correct horse battery staple",
        ])
        .output()
        .expect("failed to run maho signup");

    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        output.status.success(),
        "signup subcommand failed: stdout={stdout}, stderr={stderr}"
    );
    assert!(stdout.contains("Logged in as alias@example.com"));
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn test_invalid_credentials() {
    let (relay_addr, relay_state) = spawn_mock_relay().await;
    let tmp = TempDir::new().unwrap();
    let relay_url = format!("http://{}", relay_addr);

    // First create a user
    relay_state.users.lock().unwrap().push(MockUser {
        email: "exists@example.com".to_string(),
        password: "correct horse battery staple".to_string(),
        access_token: "at_exists".to_string(),
        refresh_token: "rt_exists".to_string(),
    });

    // Try login with wrong password
    let output = maho_cmd(tmp.path())
        .args([
            "--relay-url",
            &relay_url,
            "login",
            "--email",
            "exists@example.com",
            "--password",
            "wrong password that is long enough",
        ])
        .output()
        .expect("failed to run maho");

    assert!(!output.status.success(), "login with bad creds should fail");
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.to_lowercase().contains("invalid"),
        "stderr should mention 'invalid', got: {stderr}"
    );
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn test_relay_down_connection_refused() {
    let tmp = TempDir::new().unwrap();
    // Use a port that is definitely not listening
    let relay_url = "http://127.0.0.1:1"; // port 1 is almost never open

    let output = maho_cmd(tmp.path())
        .args([
            "--relay-url",
            relay_url,
            "login",
            "--email",
            "test@example.com",
            "--password",
            "correct horse battery staple",
        ])
        .output()
        .expect("failed to run maho");

    assert!(
        !output.status.success(),
        "login with relay down should fail"
    );
    let stderr = String::from_utf8_lossy(&output.stderr);
    // On macOS/Linux the error is typically "Connection refused" or "tcp connect error"
    let stderr_lower = stderr.to_lowercase();
    assert!(
        stderr_lower.contains("connection refused")
            || stderr_lower.contains("tcp connect error")
            || stderr_lower.contains("error"),
        "stderr should indicate connection failure, got: {stderr}"
    );
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn test_whoami_without_login() {
    let (relay_addr, _) = spawn_mock_relay().await;
    let tmp = TempDir::new().unwrap();
    let relay_url = format!("http://{}", relay_addr);

    // No login performed
    let output = maho_cmd(tmp.path())
        .args(["--relay-url", &relay_url, "whoami"])
        .output()
        .expect("failed to run maho whoami");

    assert!(!output.status.success(), "whoami without login should fail");
    let stderr = String::from_utf8_lossy(&output.stderr);
    let stderr_lower = stderr.to_lowercase();
    assert!(
        stderr_lower.contains("not logged in") || stderr_lower.contains("login"),
        "stderr should indicate not logged in, got: {stderr}"
    );
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn test_help_shows_auth_group_with_signup() {
    let tmp = TempDir::new().unwrap();

    // Top-level --help shows the auth group
    let output = maho_cmd(tmp.path())
        .args(["--help"])
        .output()
        .expect("failed to run maho --help");

    assert!(output.status.success());
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("auth"),
        "--help should show auth group, got: {stdout}"
    );

    // `maho auth --help` shows signup as a nested command
    let output2 = maho_cmd(tmp.path())
        .args(["auth", "--help"])
        .output()
        .expect("failed to run maho auth --help");

    assert!(output2.status.success());
    let stdout2 = String::from_utf8_lossy(&output2.stdout);
    assert!(
        stdout2.contains("signup"),
        "maho auth --help should show signup subcommand, got: {stdout2}"
    );
}
