mod common;

use common::spawn_test_server_with_config;
use maho_relay::config::RelayConfig;
use reqwest::StatusCode;
use serde_json::json;

#[tokio::test]
async fn test_signup_login_refresh_me_logout_and_delete_device() {
    let config = RelayConfig {
        open_registration: true,
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let signup_resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "alice@example.com",
            "password": "correct horse battery staple",
            "display_name": "Alice",
            "device_name": "Alice Laptop",
            "device_type": "desktop"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(signup_resp.status(), StatusCode::OK);
    let signup_json: serde_json::Value = signup_resp.json().await.unwrap();
    let access_token = signup_json["access_token"].as_str().unwrap().to_string();
    let refresh_token = signup_json["refresh_token"].as_str().unwrap().to_string();
    let device_id = signup_json["device"]["id"].as_i64().unwrap();

    let me_resp = client
        .get(format!("{}/auth/me", base))
        .header("Authorization", format!("Bearer {}", access_token))
        .send()
        .await
        .unwrap();
    assert_eq!(me_resp.status(), StatusCode::OK);
    let me_json: serde_json::Value = me_resp.json().await.unwrap();
    assert_eq!(me_json["account"]["email"], "alice@example.com");
    assert!(!me_json["devices"].as_array().unwrap().is_empty());

    let refresh_resp = client
        .post(format!("{}/auth/refresh", base))
        .json(&json!({"refresh_token": refresh_token}))
        .send()
        .await
        .unwrap();
    assert_eq!(refresh_resp.status(), StatusCode::OK);
    let refresh_json: serde_json::Value = refresh_resp.json().await.unwrap();
    let rotated_access = refresh_json["access_token"].as_str().unwrap().to_string();
    let rotated_refresh = refresh_json["refresh_token"].as_str().unwrap().to_string();
    assert_ne!(rotated_access, access_token);
    assert_ne!(rotated_refresh, refresh_token);

    let old_me = client
        .get(format!("{}/auth/me", base))
        .header("Authorization", format!("Bearer {}", access_token))
        .send()
        .await
        .unwrap();
    assert_eq!(old_me.status(), StatusCode::UNAUTHORIZED);

    let refreshed_me = client
        .get(format!("{}/auth/me", base))
        .header("Authorization", format!("Bearer {}", rotated_access))
        .send()
        .await
        .unwrap();
    assert_eq!(refreshed_me.status(), StatusCode::OK);

    let delete_resp = client
        .delete(format!("{}/auth/devices/{}", base, device_id))
        .header("Authorization", format!("Bearer {}", rotated_access))
        .send()
        .await
        .unwrap();
    assert_eq!(delete_resp.status(), StatusCode::OK);

    let after_delete = client
        .get(format!("{}/auth/me", base))
        .header("Authorization", format!("Bearer {}", rotated_access))
        .send()
        .await
        .unwrap();
    assert_eq!(after_delete.status(), StatusCode::UNAUTHORIZED);

    let logout_resp = client
        .post(format!("{}/auth/logout", base))
        .header("Authorization", format!("Bearer {}", rotated_access))
        .send()
        .await
        .unwrap();
    assert_eq!(logout_resp.status(), StatusCode::UNAUTHORIZED);
}

#[tokio::test]
async fn test_signup_rejected_when_open_registration_disabled() {
    let config = RelayConfig {
        open_registration: false,
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "blocked@example.com",
            "password": "password1234"
        }))
        .send()
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::FORBIDDEN);
}

#[tokio::test]
async fn test_signup_rejected_when_password_too_short() {
    let config = RelayConfig {
        open_registration: true,
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "short@example.com",
            "password": "shortpass"
        }))
        .send()
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
    let body: serde_json::Value = resp.json().await.unwrap();
    assert!(body["error"].as_str().unwrap().contains("password must be at least"));
}

#[tokio::test]
async fn test_signup_rate_limit_enforced() {
    let config = RelayConfig {
        open_registration: true,
        signup_rate_limit_per_ip_per_minute: 1,
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let first = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "rate-one@example.com",
            "password": "password1234"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(first.status(), StatusCode::OK);

    let second = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "rate-two@example.com",
            "password": "password1234"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(second.status(), StatusCode::TOO_MANY_REQUESTS);
}

#[tokio::test]
async fn test_login_rate_limit_enforced() {
    let config = RelayConfig {
        open_registration: true,
        login_rate_limit_per_ip_per_minute: 1,
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let signup_resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "login-rate@example.com",
            "password": "password1234"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(signup_resp.status(), StatusCode::OK);

    let first_login = client
        .post(format!("{}/auth/login", base))
        .json(&json!({
            "email": "login-rate@example.com",
            "password": "password1234"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(first_login.status(), StatusCode::OK);

    let second_login = client
        .post(format!("{}/auth/login", base))
        .json(&json!({
            "email": "login-rate@example.com",
            "password": "password1234"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(second_login.status(), StatusCode::TOO_MANY_REQUESTS);
}
