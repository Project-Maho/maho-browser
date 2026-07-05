mod common;

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

#[tokio::test]
async fn test_shortcuts_api_e2e_and_lww_isolation() {
    let mut config = RelayConfig::default();
    config.open_registration = true;
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    // 1. Unauthorized GET/PUT should return 401
    let get_resp = client
        .get(format!("{}/api/v1/shortcuts", base))
        .send()
        .await
        .unwrap();
    assert_eq!(get_resp.status(), StatusCode::UNAUTHORIZED);

    // 2. Signup two devices in different accounts (isolation test)
    let token_a = signup(&client, &base, "deviceA@maho.app", "DeviceA").await;
    let token_b = signup(&client, &base, "deviceB@maho.app", "DeviceB").await;

    // 3. GET initially returns empty shortcuts array inside envelope
    let get_resp_a = client
        .get(format!("{}/api/v1/shortcuts", base))
        .header("Authorization", format!("Bearer {}", token_a))
        .send()
        .await
        .unwrap();
    assert_eq!(get_resp_a.status(), StatusCode::OK);
    let get_json_a: serde_json::Value = get_resp_a.json().await.unwrap();
    let val_a: serde_json::Value = serde_json::from_str(get_json_a["json_data"].as_str().unwrap()).unwrap();
    assert!(val_a["shortcuts"].as_array().unwrap().is_empty());

    // 4. PUT shortcut on Device A with updatedAt = 100
    let shortcuts_payload_a = json!([
        {
            "action": "new_tab",
            "keyCombo": { "key": "t", "modifiers": ["meta"] },
            "enabled": true,
            "updatedAt": 100
        }
    ]);
    let put_resp_a = client
        .put(format!("{}/api/v1/shortcuts", base))
        .header("Authorization", format!("Bearer {}", token_a))
        .json(&json!({ "json_data": shortcuts_payload_a.to_string() }))
        .send()
        .await
        .unwrap();
    assert_eq!(put_resp_a.status(), StatusCode::OK);

    // 5. Verify Device B remains empty (Isolation)
    let get_resp_b = client
        .get(format!("{}/api/v1/shortcuts", base))
        .header("Authorization", format!("Bearer {}", token_b))
        .send()
        .await
        .unwrap();
    let get_json_b: serde_json::Value = get_resp_b.json().await.unwrap();
    let val_b: serde_json::Value = serde_json::from_str(get_json_b["json_data"].as_str().unwrap()).unwrap();
    assert!(val_b["shortcuts"].as_array().unwrap().is_empty());

    // 6. Device A concurrent PUT with older timestamp (90) - should NOT overwrite (LWW check)
    let shortcuts_payload_a_old = json!([
        {
            "action": "new_tab",
            "keyCombo": { "key": "x", "modifiers": ["meta"] },
            "enabled": true,
            "updatedAt": 90
        }
    ]);
    let put_resp_a_old = client
        .put(format!("{}/api/v1/shortcuts", base))
        .header("Authorization", format!("Bearer {}", token_a))
        .json(&json!({ "json_data": shortcuts_payload_a_old.to_string() }))
        .send()
        .await
        .unwrap();
    assert_eq!(put_resp_a_old.status(), StatusCode::OK);

    // Verify metadata still has "t" key and updatedAt = 100
    let get_resp_a2 = client
        .get(format!("{}/api/v1/shortcuts", base))
        .header("Authorization", format!("Bearer {}", token_a))
        .send()
        .await
        .unwrap();
    let get_json_a2: serde_json::Value = get_resp_a2.json().await.unwrap();
    let val_a2: serde_json::Value = serde_json::from_str(get_json_a2["json_data"].as_str().unwrap()).unwrap();
    let list_a2 = val_a2["shortcuts"].as_array().unwrap();
    assert_eq!(list_a2.len(), 1);
    assert_eq!(list_a2[0]["action"], "new_tab");
    assert_eq!(list_a2[0]["keyCombo"]["key"], "t");
    assert_eq!(list_a2[0]["updated_at"], 100);

    // 7. Device A PUT with newer timestamp (110) - should overwrite (LWW check)
    let shortcuts_payload_a_new = json!([
        {
            "action": "new_tab",
            "keyCombo": { "key": "n", "modifiers": ["meta"] },
            "enabled": false,
            "updatedAt": 110
        }
    ]);
    let put_resp_a_new = client
        .put(format!("{}/api/v1/shortcuts", base))
        .header("Authorization", format!("Bearer {}", token_a))
        .json(&json!({ "json_data": shortcuts_payload_a_new.to_string() }))
        .send()
        .await
        .unwrap();
    assert_eq!(put_resp_a_new.status(), StatusCode::OK);

    // Verify metadata has "n" key, enabled = false and updatedAt = 110
    let get_resp_a3 = client
        .get(format!("{}/api/v1/shortcuts", base))
        .header("Authorization", format!("Bearer {}", token_a))
        .send()
        .await
        .unwrap();
    let get_json_a3: serde_json::Value = get_resp_a3.json().await.unwrap();
    let val_a3: serde_json::Value = serde_json::from_str(get_json_a3["json_data"].as_str().unwrap()).unwrap();
    let list_a3 = val_a3["shortcuts"].as_array().unwrap();
    assert_eq!(list_a3.len(), 1);
    assert_eq!(list_a3[0]["action"], "new_tab");
    assert_eq!(list_a3[0]["keyCombo"]["key"], "n");
    assert_eq!(list_a3[0]["enabled"], false);
    assert_eq!(list_a3[0]["updated_at"], 110);
}

#[tokio::test]
async fn test_shortcuts_multi_device_isolation() {
    let mut config = RelayConfig::default();
    config.open_registration = true;
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let token_a = signup(&client, &base, "isolationA@maho.app", "DevA").await;
    let token_b = signup(&client, &base, "isolationB@maho.app", "DevB").await;

    let payload = json!([{
        "action": "close_tab",
        "keyCombo": { "key": "w", "modifiers": ["meta"] },
        "enabled": true,
        "updatedAt": 500
    }]);
    let put_resp = client
        .put(format!("{}/api/v1/shortcuts", base))
        .header("Authorization", format!("Bearer {}", token_a))
        .json(&json!({ "json_data": payload.to_string() }))
        .send()
        .await
        .unwrap();
    assert_eq!(put_resp.status(), StatusCode::OK);

    let get_b = client
        .get(format!("{}/api/v1/shortcuts", base))
        .header("Authorization", format!("Bearer {}", token_b))
        .send()
        .await
        .unwrap();
    assert_eq!(get_b.status(), StatusCode::OK);
    let body_b: serde_json::Value = get_b.json().await.unwrap();
    let data_b: serde_json::Value =
        serde_json::from_str(body_b["json_data"].as_str().unwrap()).unwrap();
    assert!(
        data_b["shortcuts"].as_array().unwrap().is_empty(),
        "User B should not see User A's shortcuts"
    );

    let get_a = client
        .get(format!("{}/api/v1/shortcuts", base))
        .header("Authorization", format!("Bearer {}", token_a))
        .send()
        .await
        .unwrap();
    let body_a: serde_json::Value = get_a.json().await.unwrap();
    let data_a: serde_json::Value =
        serde_json::from_str(body_a["json_data"].as_str().unwrap()).unwrap();
    let shortcuts_a = data_a["shortcuts"].as_array().unwrap();
    assert_eq!(shortcuts_a.len(), 1);
    assert_eq!(shortcuts_a[0]["action"], "close_tab");
}

#[tokio::test]
async fn test_shortcuts_lww_merge() {
    let mut config = RelayConfig::default();
    config.open_registration = true;
    let addr = spawn_test_server_with_config(config).await;
    let base = format!("http://{}", addr);
    let client = reqwest::Client::new();

    let token = signup(&client, &base, "lwwuser@maho.app", "LWWDevice").await;

    let device1_put = json!([{
        "action": "reload_tab",
        "keyCombo": { "key": "r", "modifiers": ["meta"] },
        "enabled": true,
        "updatedAt": 200
    }]);
    client
        .put(format!("{}/api/v1/shortcuts", base))
        .header("Authorization", format!("Bearer {}", token))
        .json(&json!({ "json_data": device1_put.to_string() }))
        .send()
        .await
        .unwrap();

    let device2_put = json!([{
        "action": "reload_tab",
        "keyCombo": { "key": "f5", "modifiers": [] },
        "enabled": false,
        "updatedAt": 300
    }]);
    client
        .put(format!("{}/api/v1/shortcuts", base))
        .header("Authorization", format!("Bearer {}", token))
        .json(&json!({ "json_data": device2_put.to_string() }))
        .send()
        .await
        .unwrap();

    let device1_stale = json!([{
        "action": "reload_tab",
        "keyCombo": { "key": "r", "modifiers": ["ctrl"] },
        "enabled": true,
        "updatedAt": 150
    }]);
    client
        .put(format!("{}/api/v1/shortcuts", base))
        .header("Authorization", format!("Bearer {}", token))
        .json(&json!({ "json_data": device1_stale.to_string() }))
        .send()
        .await
        .unwrap();

    let get_resp = client
        .get(format!("{}/api/v1/shortcuts", base))
        .header("Authorization", format!("Bearer {}", token))
        .send()
        .await
        .unwrap();
    let body: serde_json::Value = get_resp.json().await.unwrap();
    let data: serde_json::Value =
        serde_json::from_str(body["json_data"].as_str().unwrap()).unwrap();
    let shortcuts = data["shortcuts"].as_array().unwrap();
    assert_eq!(shortcuts.len(), 1);
    assert_eq!(shortcuts[0]["action"], "reload_tab");
    assert_eq!(shortcuts[0]["keyCombo"]["key"], "f5");
    assert_eq!(shortcuts[0]["enabled"], false);
    assert_eq!(shortcuts[0]["updated_at"], 300);
}
