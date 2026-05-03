mod common;

use base64::Engine;
use common::{
    auth, auth_with_catchup, connect_ws, read_binary, read_text, spawn_test_server, TEST_ROOM,
};
use futures_util::SinkExt;
use tokio_tungstenite::tungstenite::Message;

#[tokio::test]
async fn test_seed_join_two_peers() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let mut ws_b = connect_ws(addr).await;
    auth(&mut ws_b, TEST_ROOM).await;

    // A sends → B receives
    ws_a.send(Message::Binary(b"from-a".to_vec().into()))
        .await
        .unwrap();
    let received = read_binary(&mut ws_b).await;
    assert_eq!(received, b"from-a");
    let _ack = read_text(&mut ws_a).await;

    // B sends → A receives
    ws_b.send(Message::Binary(b"from-b".to_vec().into()))
        .await
        .unwrap();
    let received = read_binary(&mut ws_a).await;
    assert_eq!(received, b"from-b");
    let _ack = read_text(&mut ws_b).await;
}

#[tokio::test]
async fn test_tab_send_relay() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let mut ws_b = connect_ws(addr).await;
    auth(&mut ws_b, TEST_ROOM).await;

    let blob: Vec<u8> = (0..256).map(|i| (i % 256) as u8).collect();
    ws_a.send(Message::Binary(blob.clone().into()))
        .await
        .unwrap();

    let received = read_binary(&mut ws_b).await;
    assert_eq!(received, blob);
}

#[tokio::test]
async fn test_offline_catchup_three_messages() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let blobs: Vec<Vec<u8>> = (0..3)
        .map(|i| format!("offline-msg-{}", i).into_bytes())
        .collect();

    for blob in &blobs {
        ws_a.send(Message::Binary(blob.clone().into()))
            .await
            .unwrap();
        let _ack = read_text(&mut ws_a).await;
    }

    let mut ws_b = connect_ws(addr).await;
    let catchup = auth_with_catchup(&mut ws_b, TEST_ROOM, -1).await;
    assert_eq!(catchup.len(), 3);

    for (i, msg) in catchup.iter().enumerate() {
        let parsed: serde_json::Value = serde_json::from_str(msg).unwrap();
        assert_eq!(parsed["type"], "catchup");
        let decoded = base64::engine::general_purpose::STANDARD
            .decode(parsed["blob"].as_str().unwrap())
            .unwrap();
        assert_eq!(decoded, blobs[i]);
    }
}

#[tokio::test]
async fn test_bidirectional_offline() {
    let addr = spawn_test_server().await;

    // A sends 2 messages
    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    for i in 0..2 {
        ws_a.send(Message::Binary(format!("a-msg-{}", i).into_bytes().into()))
            .await
            .unwrap();
        let _ack = read_text(&mut ws_a).await;
    }
    ws_a.close(None).await.unwrap();

    // B connects, sends 2 messages
    let mut ws_b = connect_ws(addr).await;
    let _catchup = auth_with_catchup(&mut ws_b, TEST_ROOM, -1).await;

    for i in 0..2 {
        ws_b.send(Message::Binary(format!("b-msg-{}", i).into_bytes().into()))
            .await
            .unwrap();
        let _ack = read_text(&mut ws_b).await;
    }

    // A reconnects with since_seq=1 → gets B's 2 messages (seq 2, 3)
    let mut ws_a2 = connect_ws(addr).await;
    let catchup_a = auth_with_catchup(&mut ws_a2, TEST_ROOM, 1).await;
    assert_eq!(catchup_a.len(), 2);

    // B reconnects with since_seq=3 → gets 0 new
    ws_b.close(None).await.unwrap();
    let mut ws_b2 = connect_ws(addr).await;
    let catchup_b = auth_with_catchup(&mut ws_b2, TEST_ROOM, 3).await;
    assert_eq!(catchup_b.len(), 0);
}

#[tokio::test]
async fn test_three_device_broadcast() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let mut ws_b = connect_ws(addr).await;
    auth(&mut ws_b, TEST_ROOM).await;

    let mut ws_c = connect_ws(addr).await;
    auth(&mut ws_c, TEST_ROOM).await;

    // A sends → B,C receive
    ws_a.send(Message::Binary(b"from-a".to_vec().into()))
        .await
        .unwrap();
    assert_eq!(read_binary(&mut ws_b).await, b"from-a");
    assert_eq!(read_binary(&mut ws_c).await, b"from-a");
    let _ack = read_text(&mut ws_a).await;

    // B sends → A,C receive
    ws_b.send(Message::Binary(b"from-b".to_vec().into()))
        .await
        .unwrap();
    assert_eq!(read_binary(&mut ws_a).await, b"from-b");
    assert_eq!(read_binary(&mut ws_c).await, b"from-b");
    let _ack = read_text(&mut ws_b).await;

    // C sends → A,B receive
    ws_c.send(Message::Binary(b"from-c".to_vec().into()))
        .await
        .unwrap();
    assert_eq!(read_binary(&mut ws_a).await, b"from-c");
    assert_eq!(read_binary(&mut ws_b).await, b"from-c");
    let _ack = read_text(&mut ws_c).await;
}

#[tokio::test]
async fn test_reconnect_with_catchup() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let mut ws_b = connect_ws(addr).await;
    auth(&mut ws_b, TEST_ROOM).await;

    // A sends 3 messages, B receives live
    for i in 0..3 {
        ws_a.send(Message::Binary(format!("msg-{}", i).into_bytes().into()))
            .await
            .unwrap();
        let _ack = read_text(&mut ws_a).await;
        let _live = read_binary(&mut ws_b).await;
    }

    // B disconnects
    ws_b.close(None).await.unwrap();

    // A sends 2 more
    for i in 3..5 {
        ws_a.send(Message::Binary(format!("msg-{}", i).into_bytes().into()))
            .await
            .unwrap();
        let _ack = read_text(&mut ws_a).await;
    }

    // B reconnects with since_seq=2 → gets 2 via catchup (seq 3, 4)
    let mut ws_b2 = connect_ws(addr).await;
    let catchup = auth_with_catchup(&mut ws_b2, TEST_ROOM, 2).await;
    assert_eq!(catchup.len(), 2);
}

#[tokio::test]
async fn test_bulk_entity_sync_100() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let blobs: Vec<Vec<u8>> = (0..100)
        .map(|i| {
            let mut blob = vec![0u8; 64];
            blob[0] = (i & 0xff) as u8;
            blob[1] = ((i >> 8) & 0xff) as u8;
            blob
        })
        .collect();

    for blob in &blobs {
        ws_a.send(Message::Binary(blob.clone().into()))
            .await
            .unwrap();
        let _ack = read_text(&mut ws_a).await;
    }

    let mut ws_b = connect_ws(addr).await;
    let catchup = auth_with_catchup(&mut ws_b, TEST_ROOM, -1).await;
    assert_eq!(catchup.len(), 100);

    // Verify seq continuity and blob content
    for (i, msg) in catchup.iter().enumerate() {
        let parsed: serde_json::Value = serde_json::from_str(msg).unwrap();
        assert_eq!(parsed["seq"].as_i64().unwrap(), i as i64);
        let decoded = base64::engine::general_purpose::STANDARD
            .decode(parsed["blob"].as_str().unwrap())
            .unwrap();
        assert_eq!(decoded, blobs[i]);
    }
}

#[tokio::test]
async fn test_rapid_updates_ordering() {
    let addr = spawn_test_server().await;

    let mut ws_a = connect_ws(addr).await;
    auth(&mut ws_a, TEST_ROOM).await;

    let mut ws_b = connect_ws(addr).await;
    auth(&mut ws_b, TEST_ROOM).await;

    // Send 10 messages rapidly
    for i in 0..10 {
        ws_a.send(Message::Binary(format!("rapid-{}", i).into_bytes().into()))
            .await
            .unwrap();
    }

    // Collect all acks
    for _ in 0..10 {
        let ack = read_text(&mut ws_a).await;
        assert!(ack.contains("ack"));
    }

    // B receives all 10 in order
    for i in 0..10 {
        let received = read_binary(&mut ws_b).await;
        assert_eq!(received, format!("rapid-{}", i).into_bytes());
    }
}
