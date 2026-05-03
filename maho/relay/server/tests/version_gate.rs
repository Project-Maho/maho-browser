mod common;

use common::{connect_ws, read_text, spawn_test_server, try_read_text, TEST_ROOM};
use futures_util::SinkExt;
use tokio_tungstenite::tungstenite::Message;

fn auth_msg(room: &str, version: Option<&str>, since_seq: Option<i64>) -> String {
    let mut obj = serde_json::json!({"type": "auth", "key": room});
    if let Some(v) = version {
        obj["client_version"] = serde_json::Value::String(v.to_string());
    }
    if let Some(s) = since_seq {
        obj["since_seq"] = serde_json::Value::Number(s.into());
    }
    obj.to_string()
}

// --- Happy path ---

#[tokio::test]
async fn test_version_current_accepted() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, Some("1.0.0"), None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("auth_ok"), "Expected auth_ok, got: {text}");
}

#[tokio::test]
async fn test_version_above_minimum_accepted() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, Some("2.5.3"), None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("auth_ok"), "Expected auth_ok, got: {text}");
}

#[tokio::test]
async fn test_version_missing_still_accepted() {
    // client_version is optional; omitting it should NOT reject
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, None, None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("auth_ok"), "No version should still auth_ok, got: {text}");
}

// --- Rejection: outdated versions ---

#[tokio::test]
async fn test_version_below_minimum_rejected() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, Some("0.9.9"), None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("client_outdated"), "Expected client_outdated, got: {text}");
    assert!(text.contains("1.0.0"), "Should include minimum_version, got: {text}");
}

#[tokio::test]
async fn test_version_0_0_1_rejected() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, Some("0.0.1"), None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("client_outdated"), "Expected client_outdated, got: {text}");
}

#[tokio::test]
async fn test_version_0_99_99_rejected() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, Some("0.99.99"), None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("client_outdated"), "Expected client_outdated, got: {text}");
}

// --- Rejection closes connection (no further messages) ---

#[tokio::test]
async fn test_rejected_client_gets_no_auth_ok() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, Some("0.1.0"), None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("client_outdated"));
    // Connection should be closed; no more messages
    let next = try_read_text(&mut ws, 500).await;
    assert!(next.is_none(), "Expected no more messages after rejection, got: {:?}", next);
}

// --- Malformed versions ---

#[tokio::test]
async fn test_version_empty_string() {
    // "" parses to (0,0,0) which is < (1,0,0) => should be rejected
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, Some(""), None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("client_outdated"), "Empty version should be rejected, got: {text}");
}

#[tokio::test]
async fn test_version_garbage_string() {
    // "abc" => (0,0,0) < (1,0,0) => rejected
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, Some("abc"), None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("client_outdated"), "Garbage version should be rejected, got: {text}");
}

#[tokio::test]
async fn test_version_partial_semver() {
    // "1" => parses to (1,0,0) which is NOT < (1,0,0) => accepted
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, Some("1"), None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("auth_ok"), "Version '1' should be accepted, got: {text}");
}

#[tokio::test]
async fn test_version_with_prerelease_tag() {
    // "1.0.0-beta" => "1", "0", "0-beta"; "0-beta".parse::<u32>() fails => 0
    // So (1,0,0) which is NOT < (1,0,0) => accepted
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, Some("1.0.0-beta"), None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    // Due to parse: "0-beta" fails to parse => 0, so (1,0,0) >= (1,0,0) => accepted
    assert!(text.contains("auth_ok"), "1.0.0-beta should be accepted, got: {text}");
}

#[tokio::test]
async fn test_version_non_string_type() {
    // Pass client_version as a number instead of string
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(
        r#"{"type":"auth","key":"0123456789abcdef0123456789abcdef","client_version":100}"#.into(),
    )).await.unwrap();
    let text = read_text(&mut ws).await;
    // as_str() returns None for number => client_version = None => accepted (optional)
    assert!(text.contains("auth_ok"), "Non-string version should be treated as absent, got: {text}");
}

// --- Catchup still works with version ---

#[tokio::test]
async fn test_catchup_works_with_version() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, Some("1.2.3"), Some(0)).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("auth_ok"), "Expected auth_ok, got: {text}");
    // Read catchup_done
    let text = read_text(&mut ws).await;
    assert!(text.contains("catchup_done"), "Expected catchup_done, got: {text}");
}

// --- Error response format for version rejection ---

#[tokio::test]
async fn test_rejection_has_correct_error_fields() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, Some("0.5.0"), None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    let json: serde_json::Value = serde_json::from_str(&text).unwrap();
    assert_eq!(json["type"], "error");
    assert_eq!(json["code"], "client_outdated");
    assert_eq!(json["minimum_version"], "1.0.0");
    assert!(!json["message"].as_str().unwrap().is_empty());
}

// --- Existing auth errors still work ---

#[tokio::test]
async fn test_invalid_json_still_errors() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text("not json at all".into())).await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("invalid auth json"), "Expected invalid auth json, got: {text}");
}

#[tokio::test]
async fn test_wrong_message_type_still_errors() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(r#"{"type":"hello"}"#.into())).await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("expected auth message"), "Expected expected auth message, got: {text}");
}

#[tokio::test]
async fn test_invalid_room_key_still_errors() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg("badkey", Some("1.0.0"), None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("invalid room key"), "Expected invalid room key, got: {text}");
}

// --- Version gate happens BEFORE room join (no side effects) ---

#[tokio::test]
async fn test_rejected_client_doesnt_consume_room_slot() {
    let addr = spawn_test_server().await;

    // Send 5 outdated clients (room max is 5 connections)
    for _ in 0..5 {
        let mut ws = connect_ws(addr).await;
        ws.send(Message::Text(auth_msg(TEST_ROOM, Some("0.1.0"), None).into()))
            .await.unwrap();
        let text = read_text(&mut ws).await;
        assert!(text.contains("client_outdated"));
    }

    // Now a valid client should still be able to join
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, Some("1.0.0"), None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("auth_ok"), "Room should not be full from rejected clients, got: {text}");
}

// --- Non-auth messages unchanged after successful auth ---

#[tokio::test]
async fn test_post_auth_messaging_unaffected() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    ws.send(Message::Text(auth_msg(TEST_ROOM, Some("1.5.0"), None).into()))
        .await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("auth_ok"));
    let _ = read_text(&mut ws).await; // catchup_done

    // Send a data message
    ws.send(Message::Text("hello world".into())).await.unwrap();
    let text = read_text(&mut ws).await;
    assert!(text.contains("ack"), "Expected ack after data message, got: {text}");
}
