mod common;

use std::time::Duration;

use common::{
    auth, auth_with_catchup, connect_ws, read_binary, read_text, spawn_test_server,
    try_read_text, TEST_ROOM, TEST_ROOM_B,
};
use futures_util::SinkExt;
use tokio_tungstenite::tungstenite::Message;

// --- Auth Flow Tests ---

#[tokio::test]
async fn test_auth_valid() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    auth(&mut ws, TEST_ROOM).await;
}

#[tokio::test]
async fn test_auth_missing_key() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;

    ws.send(Message::Text(r#"{"type":"auth"}"#.into()))
        .await
        .unwrap();

    let text = read_text(&mut ws).await;
    assert!(text.contains("error"), "Expected error, got: {}", text);
}

#[tokio::test]
async fn test_auth_invalid_room_id() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;

    ws.send(Message::Text(
        r#"{"type":"auth","key":"not-valid-hex"}"#.into(),
    ))
    .await
    .unwrap();

    let text = read_text(&mut ws).await;
    assert!(
        text.contains("error"),
        "Expected error for invalid room_id, got: {}",
        text
    );
}

#[tokio::test]
async fn test_auth_timeout() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;

    tokio::time::sleep(Duration::from_secs(6)).await;

    let result = try_read_text(&mut ws, 1000).await;
    if let Some(text) = result {
        assert!(
            text.contains("error") || text.contains("timeout"),
            "Expected timeout error, got: {}",
            text
        );
    }
}

#[tokio::test]
async fn test_auth_non_auth_first_message() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;

    ws.send(Message::Text(
        r#"{"type":"data","payload":"hello"}"#.into(),
    ))
    .await
    .unwrap();

    let text = read_text(&mut ws).await;
    assert!(
        text.contains("error"),
        "Expected error for non-auth first message, got: {}",
        text
    );
}

#[tokio::test]
async fn test_auth_duplicate() {
    let addr = spawn_test_server().await;
    let mut ws = connect_ws(addr).await;
    auth(&mut ws, TEST_ROOM).await;

    ws.send(Message::Text(
        format!(r#"{{"type":"auth","key":"{}"}}"#, TEST_ROOM).into(),
    ))
    .await
    .unwrap();

    tokio::time::sleep(Duration::from_millis(200)).await;
}

// --- Message Exchange Tests ---

#[tokio::test]
async fn test_message_1to1() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let mut ws_b = connect_ws(addr).await;
    auth(&mut ws_b, TEST_ROOM).await;

    ws_a.send(Message::Binary(b"hello-b".to_vec().into()))
        .await
        .unwrap();

    let received = read_binary(&mut ws_b).await;
    assert_eq!(received, b"hello-b");

    let ack = read_text(&mut ws_a).await;
    assert!(ack.contains("ack"), "Expected ack, got: {}", ack);
}

#[tokio::test]
async fn test_message_broadcast() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let mut ws_b = connect_ws(addr).await;
    auth(&mut ws_b, TEST_ROOM).await;

    let mut ws_c = connect_ws(addr).await;
    auth(&mut ws_c, TEST_ROOM).await;

    ws_a.send(Message::Binary(b"broadcast-msg".to_vec().into()))
        .await
        .unwrap();

    let b_msg = read_binary(&mut ws_b).await;
    let c_msg = read_binary(&mut ws_c).await;
    assert_eq!(b_msg, b"broadcast-msg");
    assert_eq!(c_msg, b"broadcast-msg");

    let ack = read_text(&mut ws_a).await;
    assert!(ack.contains("ack"));

    let echo = try_read_text(&mut ws_a, 200).await;
    assert!(echo.is_none(), "Sender should not receive echo");
}

#[tokio::test]
async fn test_message_ack() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    ws_a.send(Message::Binary(b"msg1".to_vec().into()))
        .await
        .unwrap();

    let ack = read_text(&mut ws_a).await;
    assert!(ack.contains("ack"));
    assert!(ack.contains("seq"));

    let parsed: serde_json::Value = serde_json::from_str(&ack).unwrap();
    assert_eq!(parsed["type"], "ack");
    assert!(parsed["seq"].is_number());
}

#[tokio::test]
async fn test_message_ordering() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let mut ws_b = connect_ws(addr).await;
    auth(&mut ws_b, TEST_ROOM).await;

    for i in 0..3 {
        ws_a.send(Message::Binary(format!("msg{}", i).into_bytes().into()))
            .await
            .unwrap();
        let _ack = read_text(&mut ws_a).await;
    }

    for i in 0..3 {
        let received = read_binary(&mut ws_b).await;
        assert_eq!(received, format!("msg{}", i).into_bytes());
    }
}

#[tokio::test]
async fn test_binary_message() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let mut ws_b = connect_ws(addr).await;
    auth(&mut ws_b, TEST_ROOM).await;

    let non_utf8: Vec<u8> = (0..256).map(|i| i as u8).collect();
    ws_a.send(Message::Binary(non_utf8.clone().into()))
        .await
        .unwrap();

    let received = read_binary(&mut ws_b).await;
    assert_eq!(received, non_utf8);
}

#[tokio::test]
async fn test_message_too_large() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let large_msg = vec![0u8; 1_048_577];
    ws_a.send(Message::Binary(large_msg.into())).await.unwrap();

    let resp = read_text(&mut ws_a).await;
    assert!(
        resp.contains("too large"),
        "Expected too large error, got: {}",
        resp
    );
}

// --- Offline Catchup Tests ---

#[tokio::test]
async fn test_catchup_basic() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    for i in 0..3 {
        ws_a.send(Message::Binary(format!("msg{}", i).into_bytes().into()))
            .await
            .unwrap();
        let _ack = read_text(&mut ws_a).await;
    }

    let mut ws_b = connect_ws(addr).await;
    let catchup = auth_with_catchup(&mut ws_b, TEST_ROOM, -1).await;

    assert_eq!(catchup.len(), 3);
    for msg in &catchup {
        assert!(msg.contains("catchup"));
        assert!(msg.contains("seq"));
        assert!(msg.contains("blob"));
    }
}

#[tokio::test]
async fn test_catchup_partial() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let mut seqs = Vec::new();
    for i in 0..5 {
        ws_a.send(Message::Binary(format!("msg{}", i).into_bytes().into()))
            .await
            .unwrap();
        let ack = read_text(&mut ws_a).await;
        let parsed: serde_json::Value = serde_json::from_str(&ack).unwrap();
        seqs.push(parsed["seq"].as_i64().unwrap());
    }

    let mut ws_b = connect_ws(addr).await;
    let catchup = auth_with_catchup(&mut ws_b, TEST_ROOM, seqs[2]).await;

    assert_eq!(catchup.len(), 2);
}

#[tokio::test]
async fn test_catchup_then_live() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    ws_a.send(Message::Binary(b"stored-msg".to_vec().into()))
        .await
        .unwrap();
    let _ack = read_text(&mut ws_a).await;

    let mut ws_b = connect_ws(addr).await;
    let catchup = auth_with_catchup(&mut ws_b, TEST_ROOM, -1).await;
    assert_eq!(catchup.len(), 1);

    ws_a.send(Message::Binary(b"live-msg".to_vec().into()))
        .await
        .unwrap();
    let _ack = read_text(&mut ws_a).await;

    let live = read_binary(&mut ws_b).await;
    assert_eq!(live, b"live-msg");
}

#[tokio::test]
async fn test_catchup_empty() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    ws_a.send(Message::Binary(b"msg".to_vec().into()))
        .await
        .unwrap();
    let ack = read_text(&mut ws_a).await;
    let parsed: serde_json::Value = serde_json::from_str(&ack).unwrap();
    let last_seq = parsed["seq"].as_i64().unwrap();

    let mut ws_b = connect_ws(addr).await;
    let catchup = auth_with_catchup(&mut ws_b, TEST_ROOM, last_seq).await;
    assert_eq!(catchup.len(), 0);
}

#[tokio::test]
async fn test_catchup_room_isolation() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    for i in 0..3 {
        ws_a.send(Message::Binary(format!("room-a-{}", i).into_bytes().into()))
            .await
            .unwrap();
        let _ack = read_text(&mut ws_a).await;
    }

    let mut ws_b = connect_ws(addr).await;
    let catchup = auth_with_catchup(&mut ws_b, TEST_ROOM_B, -1).await;
    assert_eq!(catchup.len(), 0);
}

// --- Connection Lifecycle Tests ---

#[tokio::test]
async fn test_disconnect_clean() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let mut ws_b = connect_ws(addr).await;
    auth(&mut ws_b, TEST_ROOM).await;

    ws_a.close(None).await.unwrap();

    tokio::time::sleep(Duration::from_millis(200)).await;

    let mut ws_c = connect_ws(addr).await;
    auth(&mut ws_c, TEST_ROOM).await;

    ws_b.send(Message::Binary(b"still-alive".to_vec().into()))
        .await
        .unwrap();

    let received = read_binary(&mut ws_c).await;
    assert_eq!(received, b"still-alive");
}

#[tokio::test]
async fn test_reconnect_catchup() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    ws_a.send(Message::Binary(b"before-disconnect".to_vec().into()))
        .await
        .unwrap();
    let ack = read_text(&mut ws_a).await;
    let parsed: serde_json::Value = serde_json::from_str(&ack).unwrap();
    let last_seq = parsed["seq"].as_i64().unwrap();

    ws_a.close(None).await.unwrap();

    let mut ws_b = connect_ws(addr).await;
    auth(&mut ws_b, TEST_ROOM).await;

    ws_b.send(Message::Binary(b"while-away".to_vec().into()))
        .await
        .unwrap();
    let _ack = read_text(&mut ws_b).await;

    let mut ws_a2 = connect_ws(addr).await;
    let catchup = auth_with_catchup(&mut ws_a2, TEST_ROOM, last_seq).await;
    assert_eq!(catchup.len(), 1);
}

// --- Text Frame Tests ---

#[tokio::test]
async fn test_text_message_ack() {
    let addr = spawn_test_server().await;
    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    ws_a.send(Message::Text("hello".into())).await.unwrap();

    let ack = read_text(&mut ws_a).await;
    let parsed: serde_json::Value = serde_json::from_str(&ack).unwrap();
    assert_eq!(parsed["type"], "ack");
    assert!(parsed["seq"].is_number());
}

#[tokio::test]
async fn test_text_message_broadcast() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let mut ws_b = connect_ws(addr).await;
    auth(&mut ws_b, TEST_ROOM).await;

    let payload = "text-broadcast-payload";
    ws_a.send(Message::Text(payload.into())).await.unwrap();

    let received = read_text(&mut ws_b).await;
    assert_eq!(received, payload);

    let ack = read_text(&mut ws_a).await;
    assert!(ack.contains("ack"));
}

#[tokio::test]
async fn test_text_message_catchup() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    for i in 0..3 {
        ws_a.send(Message::Text(format!("text-catchup-{}", i).into()))
            .await
            .unwrap();
        let _ack = read_text(&mut ws_a).await;
    }

    let mut ws_b = connect_ws(addr).await;
    let catchup = auth_with_catchup(&mut ws_b, TEST_ROOM, -1).await;

    assert_eq!(catchup.len(), 3);
    for msg in &catchup {
        assert!(msg.contains("catchup"));
        assert!(msg.contains("blob"));
    }
}

#[tokio::test]
async fn test_text_message_too_large() {
    let addr = spawn_test_server().await;
    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let large_text = "x".repeat(1_048_577);
    ws_a.send(Message::Text(large_text.into())).await.unwrap();

    let resp = read_text(&mut ws_a).await;
    assert!(
        resp.contains("too large"),
        "Expected too large error, got: {}",
        resp
    );
}

#[tokio::test]
async fn test_mixed_text_binary() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let mut ws_b = connect_ws(addr).await;
    auth(&mut ws_b, TEST_ROOM).await;

    // Send: Text, Binary, Text
    ws_a.send(Message::Text("first-text".into())).await.unwrap();
    let _ack = read_text(&mut ws_a).await;

    ws_a.send(Message::Binary(b"middle-binary".to_vec().into()))
        .await
        .unwrap();
    let _ack = read_text(&mut ws_a).await;

    ws_a.send(Message::Text("last-text".into())).await.unwrap();
    let _ack = read_text(&mut ws_a).await;

    // B receives: Text, Binary, Text
    let msg1 = read_text(&mut ws_b).await;
    assert_eq!(msg1, "first-text");

    let msg2 = read_binary(&mut ws_b).await;
    assert_eq!(msg2, b"middle-binary");

    let msg3 = read_text(&mut ws_b).await;
    assert_eq!(msg3, "last-text");
}

// --- Retention Tests ---

#[tokio::test]
async fn test_prune_on_timer() {
    // TODO: Testing the prune timer requires either mocking time or waiting
    // for the hourly interval. Skipping for now as prune_room is already
    // tested at the unit level in storage.rs.
}
