mod common;

use common::spawn_test_server_with_config;
use maho_relay::config::RelayConfig;
use reqwest::StatusCode;
use serde_json::json;
use hmac::{Hmac, Mac};
use sha2::Sha256;
use std::sync::Mutex;
use axum::{routing::post, Router, Json};
use serial_test::serial;

type HmacSha256 = Hmac<Sha256>;

static LAST_CREDITS_UPDATE: Mutex<Option<serde_json::Value>> = Mutex::new(None);

async fn mock_credits_update(Json(body): Json<serde_json::Value>) -> impl axum::response::IntoResponse {
    *LAST_CREDITS_UPDATE.lock().unwrap_or_else(|p| p.into_inner()) = Some(body);
    StatusCode::OK
}

fn compute_signature(secret: &str, body: &str) -> String {
    let mut mac = HmacSha256::new_from_slice(secret.as_bytes()).unwrap();
    mac.update(body.as_bytes());
    format!("{:x}", mac.finalize().into_bytes())
}

#[tokio::test]
#[serial]
async fn test_credit_pack_4002_purchase_adds_50_balance() {
    *LAST_CREDITS_UPDATE.lock().unwrap_or_else(|p| p.into_inner()) = None;

    let app = Router::new()
        .route("/billing/credits_update", post(mock_credits_update));

    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let mock_proxy_addr = listener.local_addr().unwrap();
    tokio::spawn(async move {
        axum::serve(listener, app).await.unwrap();
    });

    std::env::set_var("LEMONSQUEEZY_WEBHOOK_SECRET", "test-ls-secret");
    std::env::set_var("MAHO_PROXY_URL", format!("http://{}", mock_proxy_addr));
    std::env::set_var("MAHO_RELAY_SERVICE_TOKEN", "test-secret-service-token");

    let config = RelayConfig {
        open_registration: true,
        service_token: "test-secret-service-token".to_string(),
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    // Register user
    let signup_resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "payg-alice@example.com",
            "password": "correct horse battery staple",
            "display_name": "Alice"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(signup_resp.status(), StatusCode::OK);
    let signup_json: serde_json::Value = signup_resp.json().await.unwrap();
    let user_id = signup_json["account"]["id"].as_i64().unwrap();

    // Purchase variant 4002 ($50 PAYG credit pack)
    let payload = json!({
        "meta": {
            "event_name": "order_created",
            "custom_data": { "user_id": user_id.to_string() }
        },
        "data": {
            "id": "event-payg-4002-001",
            "type": "orders",
            "attributes": {
                "variant_id": 4002,
                "status": "paid",
                "total": 5000
            }
        }
    });
    let payload_str = payload.to_string();
    let sig = compute_signature("test-ls-secret", &payload_str);

    let resp = client
        .post(format!("{}/billing/lemonsqueezy", base))
        .header("X-Signature", sig)
        .header("Content-Type", "application/json")
        .body(payload_str)
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    tokio::time::sleep(tokio::time::Duration::from_millis(150)).await;

    // Verify proxy was notified with correct balance
    {
        let captured = LAST_CREDITS_UPDATE.lock().unwrap_or_else(|p| p.into_inner());
        assert!(captured.is_some(), "Proxy should have received credits_update notification");
        let body = captured.as_ref().unwrap();
        assert_eq!(body["user_id"], user_id.to_string());
        assert_eq!(body["new_balance_usd"], 50.0);
        assert_eq!(body["delta_usd"], 50.0);
    }

    // Verify balance via admin endpoint
    let admin_resp = client
        .post(format!("{}/auth/admin/set_credit_balance", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({
            "user_id": user_id,
            "new_balance_usd": 50.0,
            "reason": "verify-read-only-idempotent"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(admin_resp.status(), StatusCode::OK);
    let admin_json: serde_json::Value = admin_resp.json().await.unwrap();
    assert_eq!(admin_json["new_balance_usd"], 50.0);
}

#[tokio::test]
#[serial]
async fn test_credit_consume_deducts_balance_and_rejects_when_insufficient() {
    use sea_orm::Database;
    use sea_orm_migration::MigratorTrait;
    use maho_relay::storage;

    let db = Database::connect("sqlite::memory:").await.unwrap();
    maho_relay_migration::Migrator::up(&db, None).await.unwrap();

    let now = chrono::Utc::now().timestamp();

    // Create a user first
    let user = storage::insert_user(
        &db,
        "payg-consume@example.com".to_string(),
        "hash".to_string(),
        "Consumer".to_string(),
        now,
    )
    .await
    .unwrap();

    // Set balance to $5
    let balance = storage::add_credit_balance(&db, user.id, 5.0, now).await.unwrap();
    assert_eq!(balance, 5.0);

    // Consume $3 — should succeed
    let balance = storage::consume_credit(&db, user.id, 3.0, now).await.unwrap();
    assert_eq!(balance, 2.0);

    // Attempt to consume $5 — should fail (only $2 remaining)
    let result = storage::consume_credit(&db, user.id, 5.0, now).await;
    assert!(result.is_err());
    let err_msg = result.unwrap_err().to_string();
    assert!(err_msg.contains("insufficient"), "Expected insufficient balance error, got: {}", err_msg);

    // Verify balance unchanged at $2
    let balance = storage::get_credit_balance(&db, user.id).await.unwrap();
    assert_eq!(balance, 2.0);
}

#[tokio::test]
#[serial]
async fn test_admin_set_credit_balance_overrides_existing() {
    std::env::set_var("LEMONSQUEEZY_WEBHOOK_SECRET", "test-ls-secret");
    std::env::set_var("MAHO_PROXY_URL", "http://127.0.0.1:1");
    std::env::set_var("MAHO_RELAY_SERVICE_TOKEN", "test-secret-service-token");

    let config = RelayConfig {
        open_registration: true,
        service_token: "test-secret-service-token".to_string(),
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    // Register user
    let signup_resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "payg-admin@example.com",
            "password": "correct horse battery staple",
            "display_name": "Admin Target"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(signup_resp.status(), StatusCode::OK);
    let signup_json: serde_json::Value = signup_resp.json().await.unwrap();
    let user_id = signup_json["account"]["id"].as_i64().unwrap();

    // Set balance to $0
    let resp = client
        .post(format!("{}/auth/admin/set_credit_balance", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({
            "user_id": user_id,
            "new_balance_usd": 0.0,
            "reason": "test: reset to zero"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let resp_json: serde_json::Value = resp.json().await.unwrap();
    assert_eq!(resp_json["user_id"], user_id);
    assert_eq!(resp_json["new_balance_usd"], 0.0);

    // Set balance to $30
    let resp = client
        .post(format!("{}/auth/admin/set_credit_balance", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({
            "user_id": user_id,
            "new_balance_usd": 30.0,
            "reason": "test: set to 30"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let resp_json: serde_json::Value = resp.json().await.unwrap();
    assert_eq!(resp_json["user_id"], user_id);
    assert_eq!(resp_json["new_balance_usd"], 30.0);
}

#[tokio::test]
#[serial]
async fn test_order_refunded_deducts_credit_balance() {
    *LAST_CREDITS_UPDATE.lock().unwrap_or_else(|p| p.into_inner()) = None;

    let app = Router::new().route("/billing/credits_update", post(mock_credits_update));
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let mock_proxy_addr = listener.local_addr().unwrap();
    tokio::spawn(async move {
        axum::serve(listener, app).await.unwrap();
    });

    std::env::set_var("LEMONSQUEEZY_WEBHOOK_SECRET", "test-ls-secret");
    std::env::set_var("MAHO_PROXY_URL", format!("http://{}", mock_proxy_addr));
    std::env::set_var("MAHO_RELAY_SERVICE_TOKEN", "test-secret-service-token");

    let config = RelayConfig {
        open_registration: true,
        service_token: "test-secret-service-token".to_string(),
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    // Register user
    let signup_resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "refund-test@example.com",
            "password": "correct horse battery staple",
            "display_name": "Refund Tester"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(signup_resp.status(), StatusCode::OK);
    let signup_json: serde_json::Value = signup_resp.json().await.unwrap();
    let user_id = signup_json["account"]["id"].as_i64().unwrap();

    // Seed $50 credit balance via admin endpoint
    let seed_resp = client
        .post(format!("{}/auth/admin/set_credit_balance", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({
            "user_id": user_id,
            "new_balance_usd": 50.0,
            "reason": "test: seed for refund"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(seed_resp.status(), StatusCode::OK);

    // Refund $25 — should leave $25
    let refund_payload = json!({
        "meta": { "event_name": "order_refunded", "custom_data": { "user_id": user_id.to_string() } },
        "data": {
            "id": "refund-evt-001",
            "type": "orders",
            "attributes": { "variant_id": 4002, "status": "refunded", "total": 2500 }
        }
    });
    let payload_str = refund_payload.to_string();
    let sig = compute_signature("test-ls-secret", &payload_str);

    let resp = client
        .post(format!("{}/billing/lemonsqueezy", base))
        .header("X-Signature", sig)
        .header("Content-Type", "application/json")
        .body(payload_str)
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);

    tokio::time::sleep(tokio::time::Duration::from_millis(150)).await;

    // Verify proxy got notification with delta=-25.0
    {
        let captured = LAST_CREDITS_UPDATE.lock().unwrap_or_else(|p| p.into_inner());
        assert!(captured.is_some(), "Proxy should have received credits_update for refund");
        let body = captured.as_ref().unwrap();
        assert_eq!(body["user_id"], user_id.to_string());
        let new_balance = body["new_balance_usd"].as_f64().unwrap();
        let delta = body["delta_usd"].as_f64().unwrap();
        assert!((new_balance - 25.0).abs() < 0.01, "new_balance should be ~$25, got {}", new_balance);
        assert!((delta - (-25.0)).abs() < 0.01, "delta should be -$25, got {}", delta);
    }
}

#[tokio::test]
#[serial]
async fn test_order_refunded_caps_at_current_balance() {
    *LAST_CREDITS_UPDATE.lock().unwrap_or_else(|p| p.into_inner()) = None;

    let app = Router::new().route("/billing/credits_update", post(mock_credits_update));
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let mock_proxy_addr = listener.local_addr().unwrap();
    tokio::spawn(async move {
        axum::serve(listener, app).await.unwrap();
    });

    std::env::set_var("LEMONSQUEEZY_WEBHOOK_SECRET", "test-ls-secret");
    std::env::set_var("MAHO_PROXY_URL", format!("http://{}", mock_proxy_addr));
    std::env::set_var("MAHO_RELAY_SERVICE_TOKEN", "test-secret-service-token");

    let config = RelayConfig {
        open_registration: true,
        service_token: "test-secret-service-token".to_string(),
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let signup_resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "refund-cap-test@example.com",
            "password": "correct horse battery staple",
            "display_name": "Cap Tester"
        }))
        .send()
        .await
        .unwrap();
    let signup_json: serde_json::Value = signup_resp.json().await.unwrap();
    let user_id = signup_json["account"]["id"].as_i64().unwrap();

    // Seed only $10 credit
    let _ = client
        .post(format!("{}/auth/admin/set_credit_balance", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({ "user_id": user_id, "new_balance_usd": 10.0, "reason": "test: low seed" }))
        .send().await.unwrap();

    // Refund $50 — should cap at $10, leave $0
    let refund_payload = json!({
        "meta": { "event_name": "order_refunded", "custom_data": { "user_id": user_id.to_string() } },
        "data": {
            "id": "refund-evt-cap-001",
            "type": "orders",
            "attributes": { "variant_id": 4002, "status": "refunded", "total": 5000 }
        }
    });
    let payload_str = refund_payload.to_string();
    let sig = compute_signature("test-ls-secret", &payload_str);

    let resp = client
        .post(format!("{}/billing/lemonsqueezy", base))
        .header("X-Signature", sig)
        .header("Content-Type", "application/json")
        .body(payload_str)
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);

    tokio::time::sleep(tokio::time::Duration::from_millis(150)).await;

    {
        let captured = LAST_CREDITS_UPDATE.lock().unwrap_or_else(|p| p.into_inner());
        assert!(captured.is_some(), "Proxy should have received credits_update");
        let body = captured.as_ref().unwrap();
        let new_balance = body["new_balance_usd"].as_f64().unwrap();
        assert!((new_balance - 0.0).abs() < 0.01, "new_balance should be capped at $0, got {}", new_balance);
    }
}
