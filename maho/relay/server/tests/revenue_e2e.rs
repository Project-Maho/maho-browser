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

static LAST_PROXY_TIER_UPDATE: Mutex<Option<serde_json::Value>> = Mutex::new(None);
static LAST_PROXY_TOPUP: Mutex<Option<serde_json::Value>> = Mutex::new(None);
static LAST_PROXY_CREDITS_UPDATE: Mutex<Option<serde_json::Value>> = Mutex::new(None);

async fn mock_update_tier(Json(body): Json<serde_json::Value>) -> impl axum::response::IntoResponse {
    *LAST_PROXY_TIER_UPDATE.lock().unwrap() = Some(body);
    StatusCode::OK
}

async fn mock_topup(Json(body): Json<serde_json::Value>) -> impl axum::response::IntoResponse {
    *LAST_PROXY_TOPUP.lock().unwrap() = Some(body);
    StatusCode::OK
}

async fn mock_credits_update(Json(body): Json<serde_json::Value>) -> impl axum::response::IntoResponse {
    *LAST_PROXY_CREDITS_UPDATE.lock().unwrap() = Some(body);
    StatusCode::OK
}

fn compute_signature(secret: &str, body: &str) -> String {
    let mut mac = HmacSha256::new_from_slice(secret.as_bytes()).unwrap();
    mac.update(body.as_bytes());
    format!("{:x}", mac.finalize().into_bytes())
}

#[tokio::test]
#[serial]
async fn test_revenue_path_e2e() {
    // Reset shared state
    *LAST_PROXY_TIER_UPDATE.lock().unwrap() = None;
    *LAST_PROXY_TOPUP.lock().unwrap() = None;

    let app = Router::new()
        .route("/billing/update_tier", post(mock_update_tier))
        .route("/billing/topup", post(mock_topup));

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

    // 1. Register Alice
    let signup_resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "revenue-alice@example.com",
            "password": "correct horse battery staple",
            "display_name": "Alice"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(signup_resp.status(), StatusCode::OK);
    let signup_json: serde_json::Value = signup_resp.json().await.unwrap();
    let user_id = signup_json["account"]["id"].as_i64().unwrap();
    let access_token = signup_json["access_token"].as_str().unwrap().to_string();

    // Verify initial free tier
    let intro_resp1 = client
        .post(format!("{}/auth/introspect", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({ "token": access_token }))
        .send()
        .await
        .unwrap();
    assert_eq!(intro_resp1.status(), StatusCode::OK);
    let intro_json1: serde_json::Value = intro_resp1.json().await.unwrap();
    assert_eq!(intro_json1["tier"], "free");
    assert_eq!(intro_json1["subscription_status"], "none");

    // 2. Upgrade to Pro via subscription_created webhook
    let ls_payload = json!({
        "meta": {
            "event_name": "subscription_created",
            "custom_data": {
                "user_id": user_id.to_string()
            }
        },
        "data": {
            "id": "event-ls-12345",
            "type": "subscriptions",
            "attributes": {
                "variant_id": 1001,
                "status": "active",
                "created_at": "2026-06-27T12:00:00Z",
                "renews_at": "2026-07-27T12:00:00Z"
            }
        }
    });

    let payload_str = ls_payload.to_string();
    let signature = compute_signature("test-ls-secret", &payload_str);

    let webhook_resp = client
        .post(format!("{}/billing/lemonsqueezy", base))
        .header("X-Signature", signature)
        .header("Content-Type", "application/json")
        .body(payload_str)
        .send()
        .await
        .unwrap();
    assert_eq!(webhook_resp.status(), StatusCode::OK);

    tokio::time::sleep(tokio::time::Duration::from_millis(150)).await;

    // Assert proxy received tier update with correct base_ceiling_usd
    {
        let captured = LAST_PROXY_TIER_UPDATE.lock().unwrap();
        assert!(captured.is_some(), "Proxy should have received update_tier notification");
        let body = captured.as_ref().unwrap();
        assert_eq!(body["user_id"], user_id.to_string());
        assert_eq!(body["tier"], "pro");
        assert_eq!(body["base_ceiling_usd"], 2.1, "Pro tier base_ceiling_usd must be $2.10");
    }

    // 3. Verify introspect shows pro
    let intro_resp2 = client
        .post(format!("{}/auth/introspect", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({ "token": access_token }))
        .send()
        .await
        .unwrap();
    assert_eq!(intro_resp2.status(), StatusCode::OK);
    let intro_json2: serde_json::Value = intro_resp2.json().await.unwrap();
    assert_eq!(intro_json2["tier"], "pro");
    assert_eq!(intro_json2["subscription_status"], "active");

    // 4. Top-up $10 (variant 3001)
    let topup_3001_payload = json!({
        "meta": {
            "event_name": "order_created",
            "custom_data": { "user_id": user_id.to_string() }
        },
        "data": {
            "id": "event-topup-3001",
            "type": "orders",
            "attributes": {
                "variant_id": 3001,
                "status": "paid",
                "total": 1000
            }
        }
    });
    let topup_str = topup_3001_payload.to_string();
    let topup_sig = compute_signature("test-ls-secret", &topup_str);
    let resp = client
        .post(format!("{}/billing/lemonsqueezy", base))
        .header("X-Signature", topup_sig)
        .header("Content-Type", "application/json")
        .body(topup_str)
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    tokio::time::sleep(tokio::time::Duration::from_millis(150)).await;
    {
        let captured = LAST_PROXY_TOPUP.lock().unwrap();
        assert!(captured.is_some(), "Proxy should have received topup notification");
        let body = captured.as_ref().unwrap();
        assert_eq!(body["user_id"], user_id.to_string());
        assert_eq!(body["amount_usd"], 10.0, "Variant 3001 must map to $10 top-up");
    }

    // 5. Top-up $50 (variant 3002)
    *LAST_PROXY_TOPUP.lock().unwrap() = None;
    let topup_3002_payload = json!({
        "meta": {
            "event_name": "order_created",
            "custom_data": { "user_id": user_id.to_string() }
        },
        "data": {
            "id": "event-topup-3002",
            "type": "orders",
            "attributes": {
                "variant_id": 3002,
                "status": "paid",
                "total": 5000
            }
        }
    });
    let topup_str = topup_3002_payload.to_string();
    let topup_sig = compute_signature("test-ls-secret", &topup_str);
    let resp = client
        .post(format!("{}/billing/lemonsqueezy", base))
        .header("X-Signature", topup_sig)
        .header("Content-Type", "application/json")
        .body(topup_str)
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    tokio::time::sleep(tokio::time::Duration::from_millis(150)).await;
    {
        let captured = LAST_PROXY_TOPUP.lock().unwrap();
        assert!(captured.is_some());
        let body = captured.as_ref().unwrap();
        assert_eq!(body["amount_usd"], 50.0, "Variant 3002 must map to $50 top-up");
    }

    // 6. Top-up $100 (variant 3003)
    *LAST_PROXY_TOPUP.lock().unwrap() = None;
    let topup_3003_payload = json!({
        "meta": {
            "event_name": "order_created",
            "custom_data": { "user_id": user_id.to_string() }
        },
        "data": {
            "id": "event-topup-3003",
            "type": "orders",
            "attributes": {
                "variant_id": 3003,
                "status": "paid",
                "total": 10000
            }
        }
    });
    let topup_str = topup_3003_payload.to_string();
    let topup_sig = compute_signature("test-ls-secret", &topup_str);
    let resp = client
        .post(format!("{}/billing/lemonsqueezy", base))
        .header("X-Signature", topup_sig)
        .header("Content-Type", "application/json")
        .body(topup_str)
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    tokio::time::sleep(tokio::time::Duration::from_millis(150)).await;
    {
        let captured = LAST_PROXY_TOPUP.lock().unwrap();
        assert!(captured.is_some());
        let body = captured.as_ref().unwrap();
        assert_eq!(body["amount_usd"], 100.0, "Variant 3003 must map to $100 top-up");
    }
}

#[tokio::test]
#[serial]
async fn test_subscription_cancelled_downgrades_to_free() {
    *LAST_PROXY_TIER_UPDATE.lock().unwrap() = None;

    let app = Router::new()
        .route("/billing/update_tier", post(mock_update_tier))
        .route("/billing/topup", post(mock_topup));

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

    // Register Bob
    let signup_resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "revenue-bob-cancel@example.com",
            "password": "correct horse battery staple",
            "display_name": "Bob"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(signup_resp.status(), StatusCode::OK);
    let signup_json: serde_json::Value = signup_resp.json().await.unwrap();
    let user_id = signup_json["account"]["id"].as_i64().unwrap();
    let access_token = signup_json["access_token"].as_str().unwrap().to_string();

    // Upgrade Bob to Pro
    let upgrade_payload = json!({
        "meta": {
            "event_name": "subscription_created",
            "custom_data": { "user_id": user_id.to_string() }
        },
        "data": {
            "id": "event-bob-upgrade",
            "type": "subscriptions",
            "attributes": {
                "variant_id": 1001,
                "status": "active",
                "created_at": "2026-06-01T12:00:00Z",
                "renews_at": "2026-07-01T12:00:00Z"
            }
        }
    });
    let payload_str = upgrade_payload.to_string();
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

    // Verify Pro
    let intro = client
        .post(format!("{}/auth/introspect", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({ "token": access_token }))
        .send()
        .await
        .unwrap();
    let intro_json: serde_json::Value = intro.json().await.unwrap();
    assert_eq!(intro_json["tier"], "pro");

    // Now cancel subscription
    *LAST_PROXY_TIER_UPDATE.lock().unwrap() = None;
    let cancel_payload = json!({
        "meta": {
            "event_name": "subscription_cancelled",
            "custom_data": { "user_id": user_id.to_string() }
        },
        "data": {
            "id": "event-bob-cancel",
            "type": "subscriptions",
            "attributes": {
                "variant_id": 1001,
                "status": "cancelled",
                "created_at": "2026-06-01T12:00:00Z",
                "renews_at": "2026-07-01T12:00:00Z"
            }
        }
    });
    let payload_str = cancel_payload.to_string();
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

    // Assert proxy was notified with tier=free
    {
        let captured = LAST_PROXY_TIER_UPDATE.lock().unwrap();
        assert!(captured.is_some(), "Proxy should have received tier update on cancellation");
        let body = captured.as_ref().unwrap();
        assert_eq!(body["tier"], "free", "Cancelled subscription must downgrade to free tier");
        assert_eq!(body["base_ceiling_usd"], 0.5, "Free tier ceiling is $0.50");
    }

    // Assert introspect reflects free tier
    let intro = client
        .post(format!("{}/auth/introspect", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({ "token": access_token }))
        .send()
        .await
        .unwrap();
    let intro_json: serde_json::Value = intro.json().await.unwrap();
    assert_eq!(intro_json["tier"], "free");
    assert_eq!(intro_json["subscription_status"], "cancelled");
}

#[tokio::test]
#[serial]
async fn test_stale_period_webhook_rejected() {
    *LAST_PROXY_TIER_UPDATE.lock().unwrap() = None;

    let app = Router::new()
        .route("/billing/update_tier", post(mock_update_tier))
        .route("/billing/topup", post(mock_topup));

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

    // Register Carol
    let signup_resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "revenue-carol-stale@example.com",
            "password": "correct horse battery staple",
            "display_name": "Carol"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(signup_resp.status(), StatusCode::OK);
    let signup_json: serde_json::Value = signup_resp.json().await.unwrap();
    let user_id = signup_json["account"]["id"].as_i64().unwrap();
    let access_token = signup_json["access_token"].as_str().unwrap().to_string();

    // Set Carol to Max with a RECENT created_at (June 2026)
    let fresh_payload = json!({
        "meta": {
            "event_name": "subscription_created",
            "custom_data": { "user_id": user_id.to_string() }
        },
        "data": {
            "id": "event-carol-fresh",
            "type": "subscriptions",
            "attributes": {
                "variant_id": 2002,
                "status": "active",
                "created_at": "2026-06-15T12:00:00Z",
                "renews_at": "2026-07-15T12:00:00Z"
            }
        }
    });
    let payload_str = fresh_payload.to_string();
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

    // Confirm Max tier
    let intro = client
        .post(format!("{}/auth/introspect", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({ "token": access_token }))
        .send()
        .await
        .unwrap();
    let intro_json: serde_json::Value = intro.json().await.unwrap();
    assert_eq!(intro_json["tier"], "max");

    // Verify proxy was notified with Max ceiling
    {
        let captured = LAST_PROXY_TIER_UPDATE.lock().unwrap();
        assert!(captured.is_some(), "Proxy should have received tier update for Max");
        let body = captured.as_ref().unwrap();
        assert_eq!(body["tier"], "max");
        assert_eq!(body["base_ceiling_usd"], 13.3, "Max tier base_ceiling_usd must be $13.30");
    }

    // Now send a STALE webhook with an older created_at (January 2026)
    // The monotonic guard in storage::update_user_subscription should reject this
    *LAST_PROXY_TIER_UPDATE.lock().unwrap() = None;
    let stale_payload = json!({
        "meta": {
            "event_name": "subscription_updated",
            "custom_data": { "user_id": user_id.to_string() }
        },
        "data": {
            "id": "event-carol-stale",
            "type": "subscriptions",
            "attributes": {
                "variant_id": 1001,
                "status": "active",
                "created_at": "2026-01-01T12:00:00Z",
                "renews_at": "2026-02-01T12:00:00Z"
            }
        }
    });
    let payload_str = stale_payload.to_string();
    let sig = compute_signature("test-ls-secret", &payload_str);
    let resp = client
        .post(format!("{}/billing/lemonsqueezy", base))
        .header("X-Signature", sig)
        .header("Content-Type", "application/json")
        .body(payload_str)
        .send()
        .await
        .unwrap();
    // Webhook endpoint still returns 200 (processed), but the update is a no-op
    assert_eq!(resp.status(), StatusCode::OK);
    tokio::time::sleep(tokio::time::Duration::from_millis(150)).await;

    // Assert the user record is UNCHANGED — still Max tier
    let intro = client
        .post(format!("{}/auth/introspect", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({ "token": access_token }))
        .send()
        .await
        .unwrap();
    let intro_json: serde_json::Value = intro.json().await.unwrap();
    assert_eq!(intro_json["tier"], "max", "Stale webhook must not downgrade tier");
    assert_eq!(intro_json["subscription_status"], "active", "Stale webhook must not change status");
}

#[tokio::test]
#[serial]
async fn test_payg_credit_fallback_when_subscription_exhausted() {
    *LAST_PROXY_TIER_UPDATE.lock().unwrap() = None;
    *LAST_PROXY_CREDITS_UPDATE.lock().unwrap() = None;

    let app = Router::new()
        .route("/billing/update_tier", post(mock_update_tier))
        .route("/billing/topup", post(mock_topup))
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
            "email": "payg-fallback@example.com",
            "password": "correct horse battery staple",
            "display_name": "Fallback User"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(signup_resp.status(), StatusCode::OK);
    let signup_json: serde_json::Value = signup_resp.json().await.unwrap();
    let user_id = signup_json["account"]["id"].as_i64().unwrap();
    let access_token = signup_json["access_token"].as_str().unwrap().to_string();

    // Upgrade to Pro via subscription_created
    let sub_payload = json!({
        "meta": {
            "event_name": "subscription_created",
            "custom_data": { "user_id": user_id.to_string() }
        },
        "data": {
            "id": "event-payg-fallback-sub",
            "type": "subscriptions",
            "attributes": {
                "variant_id": 1001,
                "status": "active",
                "created_at": "2026-06-28T12:00:00Z",
                "renews_at": "2026-07-28T12:00:00Z"
            }
        }
    });
    let payload_str = sub_payload.to_string();
    let sig = compute_signature("test-ls-secret", &payload_str);
    let resp = client
        .post(format!("{}/billing/lemonsqueezy", base))
        .header("X-Signature", &sig)
        .header("Content-Type", "application/json")
        .body(payload_str)
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    tokio::time::sleep(tokio::time::Duration::from_millis(150)).await;

    // Verify Pro tier
    let intro = client
        .post(format!("{}/auth/introspect", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({ "token": access_token }))
        .send()
        .await
        .unwrap();
    let intro_json: serde_json::Value = intro.json().await.unwrap();
    assert_eq!(intro_json["tier"], "pro");

    // Purchase $10 PAYG credit pack (variant 4001)
    let credit_payload = json!({
        "meta": {
            "event_name": "order_created",
            "custom_data": { "user_id": user_id.to_string() }
        },
        "data": {
            "id": "event-payg-fallback-credit",
            "type": "orders",
            "attributes": {
                "variant_id": 4001,
                "status": "paid",
                "total": 1000
            }
        }
    });
    let payload_str = credit_payload.to_string();
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

    // Verify credits_update notification fired with $10 balance
    {
        let captured = LAST_PROXY_CREDITS_UPDATE.lock().unwrap();
        assert!(captured.is_some(), "Proxy should have received credits_update notification");
        let body = captured.as_ref().unwrap();
        assert_eq!(body["user_id"], user_id.to_string());
        assert_eq!(body["new_balance_usd"], 10.0, "Credit balance should be $10");
        assert_eq!(body["delta_usd"], 10.0);
    }

    // Verify balance via admin endpoint
    let admin_resp = client
        .post(format!("{}/auth/admin/set_credit_balance", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({
            "user_id": user_id,
            "new_balance_usd": 10.0,
            "reason": "verify balance unchanged"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(admin_resp.status(), StatusCode::OK);
    let admin_json: serde_json::Value = admin_resp.json().await.unwrap();
    assert_eq!(admin_json["new_balance_usd"], 10.0);
}
