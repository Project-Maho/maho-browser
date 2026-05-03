mod common;

use common::{connect_ws, read_text, spawn_test_server_with_config};
use futures_util::SinkExt;
use maho_relay::config::RelayConfig;
use reqwest::StatusCode;
use serde_json::json;
use tokio_tungstenite::tungstenite::Message;

#[tokio::test]
async fn test_websocket_requires_token_when_enabled() {
    let config = RelayConfig {
        require_auth: true,
        open_registration: true,
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let signup_resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": "ws@example.com",
            "password": "password1234",
            "device_name": "ws-device"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(signup_resp.status(), StatusCode::OK);
    let signup_json: serde_json::Value = signup_resp.json().await.unwrap();
    let token = signup_json["access_token"].as_str().unwrap().to_string();
    let room_id = signup_json["device"]["room_id"].as_str().unwrap().to_string();

    let mut ws_without_token = connect_ws(addr).await;
    ws_without_token
        .send(Message::Text(
            format!(r#"{{"type":"auth","key":"{}"}}"#, room_id).into(),
        ))
        .await
        .unwrap();
    let rejected = read_text(&mut ws_without_token).await;
    assert!(rejected.contains("access token required"));

    let mut ws_with_token = connect_ws(addr).await;
    ws_with_token
        .send(Message::Text(
            format!(r#"{{"type":"auth","key":"{}","token":"{}"}}"#, room_id, token).into(),
        ))
        .await
        .unwrap();
    let accepted = read_text(&mut ws_with_token).await;
    assert!(accepted.contains("auth_ok"), "Expected auth_ok, got: {}", accepted);
}
