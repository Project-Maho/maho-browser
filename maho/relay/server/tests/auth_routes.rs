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

#[tokio::test]
async fn test_auth_introspect() {
    let config = RelayConfig {
        open_registration: true,
        service_token: "test-secret-service-token".to_string(),
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    // 1. Signup a user to get an access token
    let signup_resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "introspect@example.com",
            "password": "correct horse battery staple",
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(signup_resp.status(), StatusCode::OK);
    let signup_json: serde_json::Value = signup_resp.json().await.unwrap();
    let access_token = signup_json["access_token"].as_str().unwrap().to_string();

    // 2. Introspect token with correct service token
    let introspect_resp = client
        .post(format!("{}/auth/introspect", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({ "token": access_token }))
        .send()
        .await
        .unwrap();
    assert_eq!(introspect_resp.status(), StatusCode::OK);
    let intro_json: serde_json::Value = introspect_resp.json().await.unwrap();
    assert_eq!(intro_json["tier"], "free");
    assert_eq!(intro_json["subscription_status"], "none");
    assert!(!intro_json["email_verified"].as_bool().unwrap());

    // 3. Introspect token with incorrect service token
    let bad_service_resp = client
        .post(format!("{}/auth/introspect", base))
        .header("Authorization", "Bearer bad-token")
        .json(&json!({ "token": access_token }))
        .send()
        .await
        .unwrap();
    assert_eq!(bad_service_resp.status(), StatusCode::UNAUTHORIZED);

    // 4. Introspect invalid user token
    let bad_user_resp = client
        .post(format!("{}/auth/introspect", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({ "token": "not-a-valid-token" }))
        .send()
        .await
        .unwrap();
    assert_eq!(bad_user_resp.status(), StatusCode::UNAUTHORIZED);
}

#[tokio::test]
async fn test_email_verification() {
    let config = RelayConfig {
        open_registration: true,
        service_token: "test-secret-service-token".to_string(),
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    // 1. Sign up
    let signup_resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "verify-test@example.com",
            "password": "correct horse battery staple",
            "display_name": "Verifier"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(signup_resp.status(), StatusCode::OK);
    let signup_json: serde_json::Value = signup_resp.json().await.unwrap();
    let access_token = signup_json["access_token"].as_str().unwrap().to_string();

    // 2. Request verification token
    let req_verify_resp = client
        .post(format!("{}/auth/request_verify", base))
        .header("Authorization", format!("Bearer {}", access_token))
        .send()
        .await
        .unwrap();
    assert_eq!(req_verify_resp.status(), StatusCode::OK);
    let req_verify_json: serde_json::Value = req_verify_resp.json().await.unwrap();
    let dev_token = req_verify_json["dev_token"].as_str().unwrap().to_string();

    // 3. Verify using the token
    let verify_resp = client
        .post(format!("{}/auth/verify", base))
        .json(&json!({ "token": dev_token }))
        .send()
        .await
        .unwrap();
    assert_eq!(verify_resp.status(), StatusCode::OK);
    let verify_json: serde_json::Value = verify_resp.json().await.unwrap();
    assert_eq!(verify_json["status"], "success");
    let bypass_token = verify_json["verify_bypass_token"].as_str().unwrap().to_string();
    assert!(bypass_token.contains(":"));

    // 4. Introspect and assert user is now verified
    let introspect_resp = client
        .post(format!("{}/auth/introspect", base))
        .header("Authorization", "Bearer test-secret-service-token")
        .json(&json!({ "token": access_token }))
        .send()
        .await
        .unwrap();
    assert_eq!(introspect_resp.status(), StatusCode::OK);
    let intro_json: serde_json::Value = introspect_resp.json().await.unwrap();
    assert!(intro_json["email_verified"].as_bool().unwrap());
}

#[tokio::test]
async fn test_consume_bypass_first_use_succeeds_replay_conflicts_invalid_returns_401() {
    let service_token = "test-service-token-bypass";
    let config = RelayConfig {
        open_registration: true,
        service_token: service_token.to_string(),
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let signup_resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "bypass-user@example.com",
            "password": "correct horse battery staple",
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(signup_resp.status(), StatusCode::OK);
    let signup_json: serde_json::Value = signup_resp.json().await.unwrap();
    let access_token = signup_json["access_token"].as_str().unwrap().to_string();

    let req_verify_resp = client
        .post(format!("{}/auth/request_verify", base))
        .header("Authorization", format!("Bearer {}", access_token))
        .send()
        .await
        .unwrap();
    assert_eq!(req_verify_resp.status(), StatusCode::OK);
    let req_json: serde_json::Value = req_verify_resp.json().await.unwrap();
    let dev_token = req_json["dev_token"].as_str().unwrap().to_string();

    let verify_resp = client
        .post(format!("{}/auth/verify", base))
        .json(&json!({ "token": dev_token }))
        .send()
        .await
        .unwrap();
    assert_eq!(verify_resp.status(), StatusCode::OK);
    let verify_json: serde_json::Value = verify_resp.json().await.unwrap();
    let bypass_token = verify_json["verify_bypass_token"].as_str().unwrap().to_string();

    let first_consume = client
        .post(format!("{}/auth/consume_bypass", base))
        .header("Authorization", format!("Bearer {}", service_token))
        .json(&json!({ "token": bypass_token }))
        .send()
        .await
        .unwrap();
    assert_eq!(first_consume.status(), StatusCode::OK);
    let consume_json: serde_json::Value = first_consume.json().await.unwrap();
    assert!(consume_json["user_id"].as_i64().is_some());

    let second_consume = client
        .post(format!("{}/auth/consume_bypass", base))
        .header("Authorization", format!("Bearer {}", service_token))
        .json(&json!({ "token": bypass_token }))
        .send()
        .await
        .unwrap();
    assert_eq!(second_consume.status(), StatusCode::CONFLICT);

    let bad_hmac_resp = client
        .post(format!("{}/auth/consume_bypass", base))
        .header("Authorization", format!("Bearer {}", service_token))
        .json(&json!({ "token": "1:9999999999:deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" }))
        .send()
        .await
        .unwrap();
    assert_eq!(bad_hmac_resp.status(), StatusCode::UNAUTHORIZED);
}

#[tokio::test]
async fn test_admin_force_verify_email() {
    let config = RelayConfig {
        open_registration: true,
        service_token: "test-admin-service-token".to_string(),
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let signup_resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "admin-verify@example.com",
            "password": "correct horse battery staple",
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(signup_resp.status(), StatusCode::OK);
    let signup_json: serde_json::Value = signup_resp.json().await.unwrap();
    let user_id = signup_json["account"]["id"].as_i64().unwrap();
    let access_token = signup_json["access_token"].as_str().unwrap().to_string();

    let intro_before = client
        .post(format!("{}/auth/introspect", base))
        .header("Authorization", "Bearer test-admin-service-token")
        .json(&json!({ "token": access_token }))
        .send()
        .await
        .unwrap();
    assert_eq!(intro_before.status(), StatusCode::OK);
    let intro_before_json: serde_json::Value = intro_before.json().await.unwrap();
    assert!(!intro_before_json["email_verified"].as_bool().unwrap());

    let force_resp = client
        .post(format!("{}/auth/admin/force_verify_email", base))
        .header("Authorization", "Bearer test-admin-service-token")
        .json(&json!({ "user_id": user_id, "reason": "support ticket #1234 — email delivery failure" }))
        .send()
        .await
        .unwrap();
    assert_eq!(force_resp.status(), StatusCode::OK);
    let force_json: serde_json::Value = force_resp.json().await.unwrap();
    assert_eq!(force_json["user_id"], user_id);
    assert!(force_json["email_verified"].as_bool().unwrap());

    let intro_after = client
        .post(format!("{}/auth/introspect", base))
        .header("Authorization", "Bearer test-admin-service-token")
        .json(&json!({ "token": access_token }))
        .send()
        .await
        .unwrap();
    assert_eq!(intro_after.status(), StatusCode::OK);
    let intro_after_json: serde_json::Value = intro_after.json().await.unwrap();
    assert!(intro_after_json["email_verified"].as_bool().unwrap());

    let wrong_token_resp = client
        .post(format!("{}/auth/admin/force_verify_email", base))
        .header("Authorization", "Bearer wrong-service-token")
        .json(&json!({ "user_id": user_id, "reason": "bad actor" }))
        .send()
        .await
        .unwrap();
    assert_eq!(wrong_token_resp.status(), StatusCode::UNAUTHORIZED);

    let not_found_resp = client
        .post(format!("{}/auth/admin/force_verify_email", base))
        .header("Authorization", "Bearer test-admin-service-token")
        .json(&json!({ "user_id": 99999, "reason": "non-existent user" }))
        .send()
        .await
        .unwrap();
    assert_eq!(not_found_resp.status(), StatusCode::NOT_FOUND);
}
