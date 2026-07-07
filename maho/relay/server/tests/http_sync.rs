mod common;

use base64::Engine;
use common::spawn_test_server_with_config;
use maho_relay::config::RelayConfig;
use reqwest::StatusCode;
use serde_json::json;

async fn signup(client: &reqwest::Client, base: &str, email: &str, device_name: &str) -> String {
    let resp = client
        .post(format!("{}/auth/signup", base))
        .json(&json!({
            "email": email,
            "password": "correct horse battery staple",
            "device_name": device_name,
            "device_type": "desktop"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let json: serde_json::Value = resp.json().await.unwrap();
    json["access_token"].as_str().unwrap().to_string()
}

async fn login(client: &reqwest::Client, base: &str, email: &str, device_name: &str) -> String {
    let resp = client
        .post(format!("{}/auth/login", base))
        .json(&json!({
            "email": email,
            "password": "correct horse battery staple",
            "device_name": device_name,
            "device_type": "desktop"
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let json: serde_json::Value = resp.json().await.unwrap();
    json["access_token"].as_str().unwrap().to_string()
}

#[tokio::test]
async fn test_sync_auth_required() {
    let addr = spawn_test_server_with_config(RelayConfig::default()).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let push_resp = client
        .post(format!("{}/sync/push", base))
        .json(&json!({"blobs": [base64::engine::general_purpose::STANDARD.encode(b"hello")] }))
        .send()
        .await
        .unwrap();
    assert_eq!(push_resp.status(), StatusCode::UNAUTHORIZED);

    let pull_resp = client
        .get(format!("{}/sync/pull?since_seq=0&limit=10", base))
        .send()
        .await
        .unwrap();
    assert_eq!(pull_resp.status(), StatusCode::UNAUTHORIZED);
}

#[tokio::test]
async fn test_sync_push_pull_roundtrip_and_cursor() {
    let config = RelayConfig {
        open_registration: true,
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let access_token = signup(&client, &base, "sync-one@example.com", "Device One").await;
    let push_body = json!({
        "blobs": [
            base64::engine::general_purpose::STANDARD.encode(b"first"),
            base64::engine::general_purpose::STANDARD.encode(b"second")
        ]
    });

    let push_resp = client
        .post(format!("{}/sync/push", base))
        .header("Authorization", format!("Bearer {}", access_token))
        .json(&push_body)
        .send()
        .await
        .unwrap();
    assert_eq!(push_resp.status(), StatusCode::OK);
    let push_json: serde_json::Value = push_resp.json().await.unwrap();
    assert_eq!(push_json["acks"].as_array().unwrap().len(), 2);
    assert_eq!(push_json["acks"][0]["seq"].as_i64().unwrap(), 0);
    assert_eq!(push_json["acks"][1]["seq"].as_i64().unwrap(), 1);

    let pull_resp = client
        .get(format!("{}/sync/pull?since_seq=-1&limit=10", base))
        .header("Authorization", format!("Bearer {}", access_token))
        .send()
        .await
        .unwrap();
    assert_eq!(pull_resp.status(), StatusCode::OK);
    let pull_json: serde_json::Value = pull_resp.json().await.unwrap();
    assert_eq!(pull_json["has_more"], false);
    let messages = pull_json["messages"].as_array().unwrap();
    assert_eq!(messages.len(), 2);
    assert_eq!(messages[0]["seq"].as_i64().unwrap(), 0);
    assert_eq!(messages[1]["seq"].as_i64().unwrap(), 1);
    let decoded = base64::engine::general_purpose::STANDARD
        .decode(messages[0]["blob"].as_str().unwrap())
        .unwrap();
    assert_eq!(decoded, b"first");

    let since_json = client
        .get(format!("{}/sync/pull?since_seq=0&limit=10", base))
        .header("Authorization", format!("Bearer {}", access_token))
        .send()
        .await
        .unwrap()
        .json::<serde_json::Value>()
        .await
        .unwrap();
    let since_messages = since_json["messages"].as_array().unwrap();
    assert_eq!(since_messages.len(), 1);
    assert_eq!(since_messages[0]["seq"].as_i64().unwrap(), 1);
}

#[tokio::test]
async fn test_sync_limit_and_has_more() {
    let config = RelayConfig {
        open_registration: true,
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let access_token = signup(&client, &base, "sync-paged@example.com", "Device Paged").await;

    let blobs: Vec<String> = (0..4)
        .map(|i| base64::engine::general_purpose::STANDARD.encode(format!("msg-{}", i)))
        .collect();
    let push_resp = client
        .post(format!("{}/sync/push", base))
        .header("Authorization", format!("Bearer {}", access_token))
        .json(&json!({"blobs": blobs}))
        .send()
        .await
        .unwrap();
    assert_eq!(push_resp.status(), StatusCode::OK);

    let pull_resp = client
        .get(format!("{}/sync/pull?since_seq=0&limit=2", base))
        .header("Authorization", format!("Bearer {}", access_token))
        .send()
        .await
        .unwrap();
    assert_eq!(pull_resp.status(), StatusCode::OK);
    let pull_json: serde_json::Value = pull_resp.json().await.unwrap();
    assert_eq!(pull_json["has_more"], true);
    assert_eq!(pull_json["messages"].as_array().unwrap().len(), 2);

    let clamped_resp = client
        .get(format!("{}/sync/pull?since_seq=-1&limit=999", base))
        .header("Authorization", format!("Bearer {}", access_token))
        .send()
        .await
        .unwrap();
    assert_eq!(clamped_resp.status(), StatusCode::OK);
    let clamped_json: serde_json::Value = clamped_resp.json().await.unwrap();
    assert_eq!(clamped_json["messages"].as_array().unwrap().len(), 4);
    assert_eq!(clamped_json["has_more"], false);
}

#[tokio::test]
async fn test_sync_room_isolation_by_device() {
    let config = RelayConfig {
        open_registration: true,
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let device_one_token = signup(&client, &base, "sync-isolation@example.com", "Device One").await;
    let device_two_token = login(&client, &base, "sync-isolation@example.com", "Device Two").await;

    let push_resp = client
        .post(format!("{}/sync/push", base))
        .header("Authorization", format!("Bearer {}", device_one_token))
        .json(&json!({"blobs": [base64::engine::general_purpose::STANDARD.encode(b"private")]}))
        .send()
        .await
        .unwrap();
    assert_eq!(push_resp.status(), StatusCode::OK);

    let same_device_pull = client
        .get(format!("{}/sync/pull?since_seq=-1&limit=10", base))
        .header("Authorization", format!("Bearer {}", device_one_token))
        .send()
        .await
        .unwrap();
    assert_eq!(same_device_pull.status(), StatusCode::OK);
    let same_json: serde_json::Value = same_device_pull.json().await.unwrap();
    assert_eq!(same_json["messages"].as_array().unwrap().len(), 1);

    let other_device_pull = client
        .get(format!("{}/sync/pull?since_seq=-1&limit=10", base))
        .header("Authorization", format!("Bearer {}", device_two_token))
        .send()
        .await
        .unwrap();
    assert_eq!(other_device_pull.status(), StatusCode::OK);
    let other_json: serde_json::Value = other_device_pull.json().await.unwrap();
    assert!(other_json["messages"].as_array().unwrap().is_empty());
}

#[tokio::test]
async fn test_snapshot_flow() {
    let config = RelayConfig {
        open_registration: true,
        ..RelayConfig::default()
    };
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let token = signup(&client, &base, "snap@example.com", "My Phone").await;

    // 1. GET non-existent snapshot -> 404
    let get_resp = client
        .get(format!("{}/sync/snapshots", base))
        .header("Authorization", format!("Bearer {}", token))
        .send()
        .await
        .unwrap();
    assert_eq!(get_resp.status(), StatusCode::NOT_FOUND);

    // 2. HEAD non-existent snapshot -> 404
    let head_resp = client
        .head(format!("{}/sync/snapshots", base))
        .header("Authorization", format!("Bearer {}", token))
        .send()
        .await
        .unwrap();
    assert_eq!(head_resp.status(), StatusCode::NOT_FOUND);

    // 3. POST snapshot
    let blob = base64::engine::general_purpose::STANDARD.encode(b"my-snapshot-data");
    let upload_resp = client
        .post(format!("{}/sync/snapshots", base))
        .header("Authorization", format!("Bearer {}", token))
        .json(&json!({
            "hlc_ts_ceiling": 10000,
            "creator_device_id": 999,
            "blob": blob
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(upload_resp.status(), StatusCode::OK);

    // 4. GET snapshot -> 200
    let get_resp2 = client
        .get(format!("{}/sync/snapshots", base))
        .header("Authorization", format!("Bearer {}", token))
        .send()
        .await
        .unwrap();
    assert_eq!(get_resp2.status(), StatusCode::OK);
    let body: serde_json::Value = get_resp2.json().await.unwrap();
    assert_eq!(body["hlc_ts_ceiling"], 10000);
    assert_eq!(body["creator_device_id"], 999);
    assert_eq!(body["blob"], blob);

    // 5. HEAD snapshot -> 200 with headers
    let head_resp2 = client
        .head(format!("{}/sync/snapshots", base))
        .header("Authorization", format!("Bearer {}", token))
        .send()
        .await
        .unwrap();
    assert_eq!(head_resp2.status(), StatusCode::OK);
    let headers = head_resp2.headers();
    assert_eq!(headers.get("X-Hlc-Ts-Ceiling").unwrap().to_str().unwrap(), "10000");
    assert_eq!(headers.get("X-Creator-Device-Id").unwrap().to_str().unwrap(), "999");

    // 6. POST older snapshot -> 409 Conflict
    let upload_resp_conflict = client
        .post(format!("{}/sync/snapshots", base))
        .header("Authorization", format!("Bearer {}", token))
        .json(&json!({
            "hlc_ts_ceiling": 5000,
            "creator_device_id": 999,
            "blob": blob
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(upload_resp_conflict.status(), StatusCode::CONFLICT);
}
