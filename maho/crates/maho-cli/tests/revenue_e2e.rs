//! Revenue E2E integration test.
//!
//! ## What this test covers
//!
//! Spawns an in-process mock relay server (axum) and an in-process mock billing-proxy (axum),
//! then exercises the full client↔server contract:
//!   Free (429) → Pro upgrade (Webhook) → Stale Webhook guard → PAYG fallback path.
//!
//! ## What this test does NOT cover (mock-proxy limitation)
//!
//! This test uses a **host-target mock** for the billing proxy. The real
//! `maho-billing-proxy` is a wasm32 Cloudflare Worker with Durable Object state
//! (`UserUsage` DO). The following production behaviors are NOT exercised here:
//!
//! - Durable Object ceiling/reserve-commit logic (the DO's `reserve` + `commit`
//!   two-phase accounting that prevents double-spend under concurrent requests)
//! - `topup_ceiling_usd` PAYG credit reservation against the DO state
//! - Period-reset alarm (`next_reset_at`) and DO lifecycle
//! - KV-cache TTL degradation modes (`deny-paid`, `free-only`, `bypass-override`)
//! - Real wasm32 compilation and CF Worker runtime environment
//!
//! To exercise those paths, use the manual staging procedure in
//! `docs/operations/revenue-e2e-staging.md` with a live `wrangler dev` environment.
//! The automated test SUPPLEMENTS the manual staging procedure; it does NOT replace it.

use std::net::SocketAddr;
use std::sync::{Arc, Mutex};

use axum::extract::{Request, State};
use axum::response::IntoResponse;
use axum::routing::{get, post};
use axum::Router;
use hmac::{Hmac, Mac};
use serde::Deserialize;
use serde_json::json;
use sha2::Sha256;
use tempfile::TempDir;
use tokio::net::TcpListener;

type HmacSha256 = Hmac<Sha256>;

// ---------- Mock State Types ----------

#[derive(Debug, Clone)]
struct MockUser {
    id: i32,
    email: String,
    access_token: String,
    tier: String,
    subscription_status: String,
    subscription_event_at: Option<i64>,
    balance_usd: f64,
    usage_accumulated_usd: f64,
    base_ceiling_usd: f64,
}

#[derive(Debug, Default, Clone)]
struct SharedState {
    users: Arc<Mutex<Vec<MockUser>>>,
    last_proxy_tier_update: Arc<Mutex<Option<serde_json::Value>>>,
    last_proxy_credits_update: Arc<Mutex<Option<serde_json::Value>>>,
}

// ---------- Webhook Types ----------

#[derive(Debug, Deserialize)]
struct LemonSqueezyWebhook {
    meta: WebhookMeta,
    data: WebhookData,
}

#[derive(Debug, Deserialize)]
struct WebhookMeta {
    event_name: String,
    custom_data: Option<CustomData>,
}

#[derive(Debug, Deserialize)]
struct CustomData {
    user_id: Option<String>,
}

#[derive(Debug, Deserialize)]
struct WebhookData {
    id: String,
    attributes: WebhookAttributes,
}

#[derive(Debug, Deserialize)]
struct WebhookAttributes {
    variant_id: Option<i32>,
    status: Option<String>,
    created_at: Option<String>,
    total: Option<i32>,
}

// ---------- Signature Verification Helper ----------

fn compute_signature(secret: &str, body: &str) -> String {
    let mut mac = HmacSha256::new_from_slice(secret.as_bytes()).unwrap();
    mac.update(body.as_bytes());
    let result = mac.finalize();
    hex::encode(result.into_bytes())
}

mod hex {
    pub fn encode(bytes: impl AsRef<[u8]>) -> String {
        bytes
            .as_ref()
            .iter()
            .map(|b| format!("{:02x}", b))
            .collect()
    }

    pub fn decode(hex_str: &str) -> Option<Vec<u8>> {
        if hex_str.len() % 2 != 0 {
            return None;
        }
        let mut bytes = Vec::new();
        for i in (0..hex_str.len()).step_by(2) {
            let res = u8::from_str_radix(&hex_str[i..i + 2], 16).ok()?;
            bytes.push(res);
        }
        Some(bytes)
    }
}

fn verify_signature(secret: &str, body: &[u8], signature_hex: &str) -> bool {
    let signature_bytes = match hex::decode(signature_hex) {
        Some(bytes) => bytes,
        None => return false,
    };
    let mut mac = match HmacSha256::new_from_slice(secret.as_bytes()) {
        Ok(m) => m,
        Err(_) => return false,
    };
    mac.update(body);
    let result = mac.finalize();
    let computed_bytes = result.into_bytes();

    computed_bytes.as_slice() == signature_bytes.as_slice()
}

// ---------- Mock Relay Server ----------

fn mock_relay(state: SharedState) -> Router {
    Router::new()
        .route("/health", get(|| async { "ok" }))
        .route("/auth/signup", post(handle_signup))
        .route("/auth/login", post(handle_login))
        .route("/auth/me", get(handle_me))
        .route("/auth/introspect", post(handle_introspect))
        .route("/billing/lemonsqueezy", post(handle_webhook))
        .route("/billing/credit_balance", get(handle_credit_balance))
        .with_state(state)
}

async fn handle_signup(
    State(state): State<SharedState>,
    axum::Json(body): axum::Json<serde_json::Value>,
) -> impl IntoResponse {
    let email = body["email"].as_str().unwrap_or_default().to_string();

    let mut users = state.users.lock().unwrap();
    let id = (users.len() + 1) as i32;
    let access_token = format!("at_{}", id);

    let user = MockUser {
        id,
        email,
        access_token: access_token.clone(),
        tier: "free".to_string(),
        subscription_status: "none".to_string(),
        subscription_event_at: None,
        balance_usd: 0.0,
        usage_accumulated_usd: 0.0,
        base_ceiling_usd: 0.50,
    };
    users.push(user.clone());

    (
        axum::http::StatusCode::OK,
        axum::Json(json!({
            "access_token": access_token,
            "refresh_token": format!("rt_{}", id),
            "account": { "id": id, "email": user.email }
        })),
    )
}

async fn handle_login(
    State(state): State<SharedState>,
    axum::Json(body): axum::Json<serde_json::Value>,
) -> impl IntoResponse {
    let email = body["email"].as_str().unwrap_or_default().to_string();

    let users = state.users.lock().unwrap();
    if let Some(user) = users.iter().find(|u| u.email == email) {
        (
            axum::http::StatusCode::OK,
            axum::Json(json!({
                "access_token": user.access_token,
                "refresh_token": "rt_some",
                "account": { "id": user.id, "email": user.email }
            })),
        )
    } else {
        (
            axum::http::StatusCode::UNAUTHORIZED,
            axum::Json(json!({ "error": "unauthorized" })),
        )
    }
}

async fn handle_me(State(state): State<SharedState>, req: Request) -> impl IntoResponse {
    let auth = req
        .headers()
        .get("authorization")
        .and_then(|v| v.to_str().ok())
        .unwrap_or_default();
    let token = auth.strip_prefix("Bearer ").unwrap_or_default();

    let users = state.users.lock().unwrap();
    if let Some(user) = users.iter().find(|u| u.access_token == token) {
        (
            axum::http::StatusCode::OK,
            axum::Json(json!({ "email": user.email, "tier": user.tier })),
        )
    } else {
        (
            axum::http::StatusCode::UNAUTHORIZED,
            axum::Json(json!({ "error": "unauthorized" })),
        )
    }
}

async fn handle_introspect(
    State(state): State<SharedState>,
    axum::Json(body): axum::Json<serde_json::Value>,
) -> impl IntoResponse {
    let token = body["token"].as_str().unwrap_or_default().to_string();

    let users = state.users.lock().unwrap();
    if let Some(user) = users.iter().find(|u| u.access_token == token) {
        (
            axum::http::StatusCode::OK,
            axum::Json(json!({
                "user_id": user.id,
                "tier": user.tier,
                "subscription_status": user.subscription_status,
                "balance_usd": user.balance_usd,
                "usage_accumulated_usd": user.usage_accumulated_usd,
                "base_ceiling_usd": user.base_ceiling_usd,
            })),
        )
    } else {
        (
            axum::http::StatusCode::UNAUTHORIZED,
            axum::Json(json!({ "error": "unauthorized" })),
        )
    }
}

async fn handle_webhook(State(state): State<SharedState>, req: Request) -> impl IntoResponse {
    let signature = req
        .headers()
        .get("x-signature")
        .and_then(|v| v.to_str().ok())
        .unwrap_or_default()
        .to_string();

    let body_bytes = axum::body::to_bytes(req.into_body(), usize::MAX)
        .await
        .unwrap();

    // Verify signature with LS_WEBHOOK_SECRET = "test-ls-secret"
    if !verify_signature("test-ls-secret", &body_bytes, &signature) {
        return (
            axum::http::StatusCode::UNAUTHORIZED,
            axum::Json(json!({ "error": "invalid signature" })),
        );
    }

    let payload: LemonSqueezyWebhook = serde_json::from_slice(&body_bytes).unwrap();
    let event_name = &payload.meta.event_name;
    let custom_data = payload.meta.custom_data.as_ref().unwrap();
    let user_id: i32 = custom_data.user_id.as_ref().unwrap().parse().unwrap();

    let created_at_sec = payload
        .data
        .attributes
        .created_at
        .as_ref()
        .and_then(|s| chrono::DateTime::parse_from_rfc3339(s).ok())
        .map(|dt| dt.timestamp())
        .unwrap_or(0);

    let mut users = state.users.lock().unwrap();
    if let Some(user) = users.iter_mut().find(|u| u.id == user_id) {
        // Monotonic guard using raw event time
        if let Some(last_event) = user.subscription_event_at {
            if created_at_sec <= last_event {
                // Reject stale event
                return (
                    axum::http::StatusCode::OK,
                    axum::Json(json!({ "message": "stale event ignored" })),
                );
            }
        }

        user.subscription_event_at = Some(created_at_sec);

        if event_name == "subscription_created" || event_name == "subscription_updated" {
            let variant_id = payload.data.attributes.variant_id.unwrap_or(0);
            let status = payload
                .data
                .attributes
                .status
                .clone()
                .unwrap_or_else(|| "active".to_string());

            user.subscription_status = status;
            match variant_id {
                1001 => {
                    user.tier = "pro".to_string();
                    user.base_ceiling_usd = 2.10;
                }
                2002 => {
                    user.tier = "max".to_string();
                    user.base_ceiling_usd = 13.30;
                }
                _ => {
                    user.tier = "free".to_string();
                    user.base_ceiling_usd = 0.50;
                }
            }

            let update_body = json!({
                "user_id": user.id.to_string(),
                "tier": user.tier,
                "base_ceiling_usd": user.base_ceiling_usd,
            });
            *state.last_proxy_tier_update.lock().unwrap() = Some(update_body);
        } else if event_name == "order_created" {
            let variant_id = payload.data.attributes.variant_id.unwrap_or(0);
            let amount_usd = match variant_id {
                4001 => 10.0,
                4002 => 50.0,
                4003 => 100.0,
                _ => (payload.data.attributes.total.unwrap_or(0) as f64) / 100.0,
            };

            user.balance_usd += amount_usd;

            let update_body = json!({
                "user_id": user.id.to_string(),
                "new_balance_usd": user.balance_usd,
                "delta_usd": amount_usd,
            });
            *state.last_proxy_credits_update.lock().unwrap() = Some(update_body);
        }

        (
            axum::http::StatusCode::OK,
            axum::Json(json!({ "message": "webhook processed successfully" })),
        )
    } else {
        (
            axum::http::StatusCode::NOT_FOUND,
            axum::Json(json!({ "error": "user not found" })),
        )
    }
}

async fn handle_credit_balance(
    State(state): State<SharedState>,
    req: Request,
) -> impl IntoResponse {
    let auth = req
        .headers()
        .get("authorization")
        .and_then(|v| v.to_str().ok())
        .unwrap_or_default();
    let token = auth.strip_prefix("Bearer ").unwrap_or_default();

    let users = state.users.lock().unwrap();
    if let Some(user) = users.iter().find(|u| u.access_token == token) {
        (
            axum::http::StatusCode::OK,
            axum::Json(json!({
                "user_id": user.id,
                "balance_usd": user.balance_usd,
                "last_purchase_at": null,
                "last_consumption_at": null,
            })),
        )
    } else {
        (
            axum::http::StatusCode::UNAUTHORIZED,
            axum::Json(json!({ "error": "unauthorized" })),
        )
    }
}

// ---------- Mock Billing Proxy Server ----------

fn mock_proxy(state: SharedState) -> Router {
    Router::new()
        .route("/v1/chat/completions", post(handle_chat))
        .route("/billing/update_tier", post(mock_update_tier))
        .route("/billing/credits_update", post(mock_credits_update))
        .with_state(state)
}

async fn handle_chat(State(state): State<SharedState>, req: Request) -> impl IntoResponse {
    let auth = req
        .headers()
        .get("authorization")
        .and_then(|v| v.to_str().ok())
        .unwrap_or_default();
    let token = auth.strip_prefix("Bearer ").unwrap_or_default();

    let mut users = state.users.lock().unwrap();
    let user = match users.iter_mut().find(|u| u.access_token == token) {
        Some(u) => u,
        None => {
            println!("Mock Proxy: Token {} not found in users", token);
            let res = axum::response::Response::builder()
                .status(axum::http::StatusCode::UNAUTHORIZED)
                .header(axum::http::header::CONTENT_TYPE, "application/json")
                .body(axum::body::Body::from(
                    json!({ "error": "unauthorized" }).to_string(),
                ))
                .unwrap();
            return res;
        }
    };

    let total_ceiling = user.base_ceiling_usd + user.balance_usd;
    println!(
        "Mock Proxy: User {} usage = {}, ceiling = {}",
        user.email, user.usage_accumulated_usd, total_ceiling
    );
    if user.usage_accumulated_usd + 1e-9 >= total_ceiling {
        println!("Mock Proxy: User {} quota exceeded!", user.email);
        let res = axum::response::Response::builder()
            .status(axum::http::StatusCode::TOO_MANY_REQUESTS)
            .header(axum::http::header::CONTENT_TYPE, "application/json")
            .body(axum::body::Body::from(
                json!({ "error": "Billing quota exceeded for the current period" }).to_string(),
            ))
            .unwrap();
        return res;
    }

    // Increment usage
    user.usage_accumulated_usd += 0.05;
    println!(
        "Mock Proxy: Incremented usage to {}",
        user.usage_accumulated_usd
    );

    let sse_body = concat!(
        "data: {\"choices\":[{\"delta\":{\"content\":\"hello from maho proxy\"}}]}\n\n",
        "data: [DONE]\n\n",
    );

    let mut builder = axum::response::Response::builder()
        .status(axum::http::StatusCode::OK)
        .header(axum::http::header::CONTENT_TYPE, "text/event-stream");

    if user.usage_accumulated_usd > user.base_ceiling_usd {
        builder = builder.header("x-maho-source", "payg");
    }

    builder
        .body(axum::body::Body::from(sse_body.to_string()))
        .unwrap()
}

async fn mock_update_tier(
    State(_state): State<SharedState>,
    axum::Json(_body): axum::Json<serde_json::Value>,
) -> impl IntoResponse {
    axum::http::StatusCode::OK
}

async fn mock_credits_update(
    State(_state): State<SharedState>,
    axum::Json(_body): axum::Json<serde_json::Value>,
) -> impl IntoResponse {
    axum::http::StatusCode::OK
}

// ---------- Test Runners ----------

async fn spawn_servers() -> (SocketAddr, SocketAddr, SharedState) {
    let state = SharedState::default();

    // Spawn Relay
    let relay_app = mock_relay(state.clone());
    let relay_listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let relay_addr = relay_listener.local_addr().unwrap();
    tokio::spawn(async move {
        axum::serve(relay_listener, relay_app).await.unwrap();
    });

    // Spawn Proxy
    let proxy_app = mock_proxy(state.clone());
    let proxy_listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let proxy_addr = proxy_listener.local_addr().unwrap();
    tokio::spawn(async move {
        axum::serve(proxy_listener, proxy_app).await.unwrap();
    });

    (relay_addr, proxy_addr, state)
}

fn maho_cmd(config_dir: &std::path::Path) -> std::process::Command {
    let mut cmd = std::process::Command::new(env!("CARGO_BIN_EXE_maho"));
    cmd.env("MAHO_CONFIG_DIR", config_dir);
    cmd.stdin(std::process::Stdio::null());
    cmd
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn test_revenue_e2e_flow() {
    let (relay_addr, proxy_addr, state) = spawn_servers().await;
    let tmp = TempDir::new().unwrap();
    let relay_url = format!("http://{}", relay_addr);
    let proxy_url = format!("http://{}/v1", proxy_addr);

    // 1. Signup a free user
    let output = maho_cmd(tmp.path())
        .args([
            "--relay-url",
            &relay_url,
            "--proxy-url",
            &proxy_url,
            "signup",
            "--email",
            "revenue@example.com",
            "--password",
            "correct horse battery staple",
        ])
        .output()
        .unwrap();
    assert!(output.status.success());

    // Verify initial Free tier via whoami
    let output = maho_cmd(tmp.path())
        .args(["--relay-url", &relay_url, "whoami"])
        .output()
        .unwrap();
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("revenue@example.com"));
    assert!(stdout.contains("free"));

    // 2. Chat until quota exhausted
    // Initial ceiling = $0.50, each chat costs $0.05.
    // 10 chats = $0.50. The 11th chat should hit 429.
    for i in 1..=10 {
        let output = maho_cmd(tmp.path())
            .args([
                "--relay-url",
                &relay_url,
                "--proxy-url",
                &proxy_url,
                "chat",
                &format!("msg {}", i),
            ])
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "Chat {} failed: {}",
            i,
            String::from_utf8_lossy(&output.stderr)
        );
    }

    // 11th chat -> expects 429
    let output = maho_cmd(tmp.path())
        .args([
            "--relay-url",
            &relay_url,
            "--proxy-url",
            &proxy_url,
            "chat",
            "msg 11",
        ])
        .output()
        .unwrap();
    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        !output.status.success(),
        "11th chat should fail with 429: stdout={}, stderr={}",
        stdout,
        stderr
    );
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("quota exceeded") || stderr.contains("429"));

    // 3. Upgrade to Pro via subscription_created webhook
    let user_id = {
        let users = state.users.lock().unwrap();
        users[0].id
    };

    let ls_payload = json!({
        "meta": {
            "event_name": "subscription_created",
            "custom_data": { "user_id": user_id.to_string() }
        },
        "data": {
            "id": "evt-pro-sub-1",
            "attributes": {
                "variant_id": 1001,
                "status": "active",
                "created_at": "2026-07-12T15:00:00Z"
            }
        }
    });

    let payload_str = ls_payload.to_string();
    let sig = compute_signature("test-ls-secret", &payload_str);

    let client = reqwest::Client::new();
    let resp = client
        .post(format!("{}/billing/lemonsqueezy", relay_url))
        .header("x-signature", sig)
        .header("content-type", "application/json")
        .body(payload_str)
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), reqwest::StatusCode::OK);

    // Verify whoami reflects Pro tier
    let output = maho_cmd(tmp.path())
        .args(["--relay-url", &relay_url, "whoami"])
        .output()
        .unwrap();
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("pro"),
        "Whoami should show Pro tier, got: {}",
        stdout
    );

    // 4. Stale Webhook Rejection Guard
    // Send updated webhook with an older created_at (e.g. 2026-06-01)
    let stale_payload = json!({
        "meta": {
            "event_name": "subscription_updated",
            "custom_data": { "user_id": user_id.to_string() }
        },
        "data": {
            "id": "evt-stale-sub",
            "attributes": {
                "variant_id": 2002, // attempts to upgrade/downgrade but stale
                "status": "active",
                "created_at": "2026-06-01T12:00:00Z"
            }
        }
    });

    let stale_str = stale_payload.to_string();
    let stale_sig = compute_signature("test-ls-secret", &stale_str);
    let resp = client
        .post(format!("{}/billing/lemonsqueezy", relay_url))
        .header("x-signature", stale_sig)
        .header("content-type", "application/json")
        .body(stale_str)
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), reqwest::StatusCode::OK);

    // Verify tier remains Pro (Max upgrade from stale webhook ignored)
    let output = maho_cmd(tmp.path())
        .args(["--relay-url", &relay_url, "whoami"])
        .output()
        .unwrap();
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("pro"),
        "Stale webhook must not change tier, got: {}",
        stdout
    );

    // 5. PAYG fallback
    // Buy $10 credits (variant 4001)
    let payg_payload = json!({
        "meta": {
            "event_name": "order_created",
            "custom_data": { "user_id": user_id.to_string() }
        },
        "data": {
            "id": "evt-payg-credit",
            "attributes": {
                "variant_id": 4001,
                "total": 1000,
                "created_at": "2026-07-12T16:00:00Z"
            }
        }
    });

    let payg_str = payg_payload.to_string();
    let payg_sig = compute_signature("test-ls-secret", &payg_str);
    let resp = client
        .post(format!("{}/billing/lemonsqueezy", relay_url))
        .header("x-signature", payg_sig)
        .header("content-type", "application/json")
        .body(payg_str)
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), reqwest::StatusCode::OK);

    // Verify balance is $10 via CLI
    let output = maho_cmd(tmp.path())
        .args(["--relay-url", &relay_url, "balance"])
        .output()
        .unwrap();
    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        output.status.success(),
        "maho balance failed: stdout={}, stderr={}",
        stdout,
        stderr
    );
    assert!(
        stdout.contains("$10.00"),
        "Balance should show $10.00, got: {}",
        stdout
    );

    // Exhaust Pro budget ($2.10).
    // Pro had $0.50 used already during free tier.
    // Let's exhaust the remaining $1.60 of Pro budget (32 chats) to reach $2.10.
    for i in 1..=32 {
        let output = maho_cmd(tmp.path())
            .args([
                "--relay-url",
                &relay_url,
                "--proxy-url",
                &proxy_url,
                "chat",
                &format!("pro msg {}", i),
            ])
            .output()
            .unwrap();
        assert!(output.status.success());
    }

    // Now send the next chat (33rd). It should exceed the $2.10 Pro base ceiling,
    // fallback to PAYG credits, and succeed.
    let output = maho_cmd(tmp.path())
        .args([
            "--relay-url",
            &relay_url,
            "--proxy-url",
            &proxy_url,
            "chat",
            "payg fallback msg",
        ])
        .output()
        .unwrap();
    assert!(output.status.success(), "PAYG fallback chat should succeed");
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("hello from maho proxy"));
}
