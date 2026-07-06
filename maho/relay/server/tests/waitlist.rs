mod common;

use common::spawn_test_server_with_config;
use maho_relay::config::RelayConfig;
use reqwest::StatusCode;
use serde_json::json;
use serial_test::serial;

use std::net::SocketAddr;
use std::sync::Arc;

use dashmap::DashMap;
use sea_orm::{Database, EntityTrait};
use sea_orm_migration::MigratorTrait;
use tokio::net::TcpListener;

use maho_relay::room::RoomManager;
use maho_relay::server::{router as relay_router, AppState, FixedWindowRateLimiter};
use maho_relay::waitlist::RATE_LIMIT_PER_MINUTE;
use maho_relay_entity::waitlist_signups;

async fn spawn_server_with_db(config: RelayConfig) -> (SocketAddr, Arc<AppState>) {
    let db = Database::connect("sqlite::memory:").await.unwrap();
    maho_relay_migration::Migrator::up(&db, None).await.unwrap();

    let state = Arc::new(AppState {
        rooms: RoomManager::new(config.max_connections_per_room),
        signup_rate_limiter: FixedWindowRateLimiter::new(
            config.signup_rate_limit_per_ip_per_minute,
        ),
        login_rate_limiter: FixedWindowRateLimiter::new(
            config.login_rate_limit_per_ip_per_minute,
        ),
        waitlist_rate_limiter: FixedWindowRateLimiter::new(RATE_LIMIT_PER_MINUTE),
        config,
        db,
        ip_connections: DashMap::new(),
        ip_rooms: DashMap::new(),
    });

    let app = relay_router(Arc::clone(&state));
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr = listener.local_addr().unwrap();

    let state_clone = Arc::clone(&state);
    tokio::spawn(async move {
        axum::serve(
            listener,
            app.into_make_service_with_connect_info::<SocketAddr>(),
        )
        .await
        .unwrap();
    });
    drop(state_clone);

    (addr, state)
}

fn default_config() -> RelayConfig {
    RelayConfig::default()
}

#[tokio::test]
#[serial]
async fn test_waitlist_signup_ok() {
    let addr = spawn_test_server_with_config(default_config()).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let resp = client
        .post(format!("{}/waitlist", base))
        .json(&json!({ "email": "hello@example.com" }))
        .send()
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let body: serde_json::Value = resp.json().await.unwrap();
    assert_eq!(body["ok"], true);
    assert_eq!(body["already_signed_up"], false);
}

#[tokio::test]
#[serial]
async fn test_waitlist_signup_duplicate_is_idempotent() {
    let addr = spawn_test_server_with_config(default_config()).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let first = client
        .post(format!("{}/waitlist", base))
        .json(&json!({ "email": "dup@example.com" }))
        .send()
        .await
        .unwrap();
    assert_eq!(first.status(), StatusCode::OK);
    let first_body: serde_json::Value = first.json().await.unwrap();
    assert_eq!(first_body["ok"], true);
    assert_eq!(first_body["already_signed_up"], false);

    let second = client
        .post(format!("{}/waitlist", base))
        .json(&json!({ "email": "dup@example.com" }))
        .send()
        .await
        .unwrap();
    assert_eq!(second.status(), StatusCode::OK);
    let second_body: serde_json::Value = second.json().await.unwrap();
    assert_eq!(second_body["ok"], true);
    assert_eq!(second_body["already_signed_up"], true);
}

#[tokio::test]
#[serial]
async fn test_waitlist_signup_different_sources_allowed() {
    let addr = spawn_test_server_with_config(default_config()).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let browser_resp = client
        .post(format!("{}/waitlist", base))
        .json(&json!({ "email": "multi@example.com", "source": "browser" }))
        .send()
        .await
        .unwrap();
    assert_eq!(browser_resp.status(), StatusCode::OK);
    let browser_body: serde_json::Value = browser_resp.json().await.unwrap();
    assert_eq!(browser_body["ok"], true);
    assert_eq!(browser_body["already_signed_up"], false);

    let mail_resp = client
        .post(format!("{}/waitlist", base))
        .json(&json!({ "email": "multi@example.com", "source": "mail" }))
        .send()
        .await
        .unwrap();
    assert_eq!(mail_resp.status(), StatusCode::OK);
    let mail_body: serde_json::Value = mail_resp.json().await.unwrap();
    assert_eq!(mail_body["ok"], true);
    assert_eq!(mail_body["already_signed_up"], false);
}

#[tokio::test]
#[serial]
async fn test_waitlist_signup_invalid_email() {
    let addr = spawn_test_server_with_config(default_config()).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let resp = client
        .post(format!("{}/waitlist", base))
        .json(&json!({ "email": "not-an-email" }))
        .send()
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
    let body: serde_json::Value = resp.json().await.unwrap();
    assert_eq!(body["error"], "invalid_email");
}

#[tokio::test]
#[serial]
async fn test_waitlist_signup_missing_email() {
    let addr = spawn_test_server_with_config(default_config()).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let resp = client
        .post(format!("{}/waitlist", base))
        .json(&json!({ "email": "" }))
        .send()
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
    let body: serde_json::Value = resp.json().await.unwrap();
    assert_eq!(body["error"], "missing_email");
}

#[tokio::test]
#[serial]
async fn test_waitlist_signup_invalid_source() {
    let addr = spawn_test_server_with_config(default_config()).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let resp = client
        .post(format!("{}/waitlist", base))
        .json(&json!({ "email": "a@b.co", "source": "xxx" }))
        .send()
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
    let body: serde_json::Value = resp.json().await.unwrap();
    assert_eq!(body["error"], "invalid_source");
}

#[tokio::test]
#[serial]
async fn test_waitlist_signup_rate_limit() {
    let addr = spawn_test_server_with_config(default_config()).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    for i in 0..5 {
        let resp = client
            .post(format!("{}/waitlist", base))
            .json(&json!({ "email": format!("rl{}@example.com", i) }))
            .send()
            .await
            .unwrap();
        assert_ne!(
            resp.status(),
            StatusCode::TOO_MANY_REQUESTS,
            "Request {} should not be rate limited",
            i
        );
    }

    let sixth = client
        .post(format!("{}/waitlist", base))
        .json(&json!({ "email": "rl5@example.com" }))
        .send()
        .await
        .unwrap();
    assert_eq!(sixth.status(), StatusCode::TOO_MANY_REQUESTS);
    let body: serde_json::Value = sixth.json().await.unwrap();
    assert_eq!(body["error"], "rate_limit_exceeded");
}

#[tokio::test]
#[serial]
async fn test_waitlist_signup_with_metadata() {
    let (addr, state) = spawn_server_with_db(default_config()).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let resp = client
        .post(format!("{}/waitlist", base))
        .json(&json!({
            "email": "meta@example.com",
            "source": "general",
            "metadata": { "platform": "macos", "referrer": "twitter" }
        }))
        .send()
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let body: serde_json::Value = resp.json().await.unwrap();
    assert_eq!(body["ok"], true);
    assert_eq!(body["already_signed_up"], false);

    let row = waitlist_signups::Entity::find()
        .one(&state.db)
        .await
        .unwrap()
        .expect("expected a row");

    assert_eq!(row.email, "meta@example.com");
    let stored: serde_json::Value =
        serde_json::from_str(row.metadata.as_deref().expect("metadata must be Some")).unwrap();
    assert_eq!(stored["platform"], "macos");
    assert_eq!(stored["referrer"], "twitter");
}

#[tokio::test]
#[serial]
async fn test_waitlist_signup_metadata_too_large() {
    let addr = spawn_test_server_with_config(default_config()).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let large_value = "x".repeat(5000);
    let resp = client
        .post(format!("{}/waitlist", base))
        .json(&json!({
            "email": "toobig@example.com",
            "metadata": { "data": large_value }
        }))
        .send()
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
    let body: serde_json::Value = resp.json().await.unwrap();
    assert_eq!(body["error"], "metadata_too_large");
}
