#![allow(dead_code)]
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::Duration;

use axum::Router;
use dashmap::DashMap;
use futures_util::{SinkExt, StreamExt};
use sea_orm::Database;
use sea_orm_migration::MigratorTrait;
use tokio::net::TcpListener;
use tokio_tungstenite::tungstenite::Message;
use tokio_tungstenite::{connect_async, MaybeTlsStream};

use maho_relay::config::RelayConfig;
use maho_relay::room::RoomManager;
use maho_relay::server::{router as relay_router, AppState, FixedWindowRateLimiter};

pub type WsStream = tokio_tungstenite::WebSocketStream<MaybeTlsStream<tokio::net::TcpStream>>;

pub const TEST_ROOM: &str = "0123456789abcdef0123456789abcdef";
#[allow(dead_code)]
pub const TEST_ROOM_B: &str = "abcdef0123456789abcdef0123456789";

async fn build_app(config: RelayConfig) -> Router {
    let db = Database::connect("sqlite::memory:").await.unwrap();
    maho_relay_migration::Migrator::up(&db, None).await.unwrap();

    let state = Arc::new(AppState {
        rooms: RoomManager::new(config.max_connections_per_room),
        signup_rate_limiter: FixedWindowRateLimiter::new(config.signup_rate_limit_per_ip_per_minute),
        login_rate_limiter: FixedWindowRateLimiter::new(config.login_rate_limit_per_ip_per_minute),
        waitlist_rate_limiter: FixedWindowRateLimiter::new(maho_relay::waitlist::RATE_LIMIT_PER_MINUTE),
        config,
        db,
        ip_connections: DashMap::new(),
        ip_rooms: DashMap::new(),
    });

    relay_router(state)
}

pub async fn spawn_test_server() -> SocketAddr {
    spawn_test_server_with_config(RelayConfig {
        require_auth: false,
        open_registration: false,
        ..RelayConfig::default()
    })
    .await
}

pub async fn spawn_test_server_with_config(config: RelayConfig) -> SocketAddr {
    let app = build_app(config).await;

    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr = listener.local_addr().unwrap();

    tokio::spawn(async move {
        axum::serve(
            listener,
            app.into_make_service_with_connect_info::<SocketAddr>(),
        )
        .await
        .unwrap();
    });

    addr
}

pub async fn connect_ws(addr: SocketAddr) -> WsStream {
    let url = format!("ws://{}/ws", addr);
    let (stream, _) = connect_async(url).await.unwrap();
    stream
}

pub async fn auth(stream: &mut WsStream, room_id: &str) {
    stream
        .send(Message::Text(
            format!(r#"{{"type":"auth","key":"{}"}}"#, room_id).into(),
        ))
        .await
        .unwrap();

    let text = read_text(stream).await;
    assert!(text.contains("auth_ok"), "Expected auth_ok, got: {}", text);

    let text = read_text(stream).await;
    assert!(
        text.contains("catchup_done"),
        "Expected catchup_done, got: {}",
        text
    );
}

pub async fn auth_with_catchup(stream: &mut WsStream, room_id: &str, since_seq: i64) -> Vec<String> {
    stream
        .send(Message::Text(
            format!(
                r#"{{"type":"auth","key":"{}","since_seq":{}}}"#,
                room_id, since_seq
            )
            .into(),
        ))
        .await
        .unwrap();

    let text = read_text(stream).await;
    assert!(text.contains("auth_ok"), "Expected auth_ok, got: {}", text);

    let mut catchup_msgs = Vec::new();
    loop {
        let text = read_text(stream).await;
        if text.contains("catchup_done") {
            break;
        }
        catchup_msgs.push(text);
    }
    catchup_msgs
}

pub async fn read_text(stream: &mut WsStream) -> String {
    loop {
        let msg = tokio::time::timeout(Duration::from_secs(5), stream.next())
            .await
            .expect("timeout reading message")
            .unwrap()
            .unwrap();
        match msg {
            Message::Text(data) => return data.to_string(),
            Message::Ping(_) | Message::Pong(_) => continue,
            other => panic!("Expected Text, got: {:?}", other),
        }
    }
}

pub async fn read_binary(stream: &mut WsStream) -> Vec<u8> {
    loop {
        let msg = tokio::time::timeout(Duration::from_secs(5), stream.next())
            .await
            .expect("timeout reading message")
            .unwrap()
            .unwrap();
        match msg {
            Message::Binary(data) => return data.to_vec(),
            Message::Ping(_) | Message::Pong(_) => continue,
            other => panic!("Expected Binary, got: {:?}", other),
        }
    }
}

#[allow(dead_code)]
pub async fn try_read_text(stream: &mut WsStream, timeout_ms: u64) -> Option<String> {
    let deadline = tokio::time::Instant::now() + Duration::from_millis(timeout_ms);
    loop {
        let remaining = deadline.saturating_duration_since(tokio::time::Instant::now());
        if remaining.is_zero() {
            return None;
        }
        match tokio::time::timeout(remaining, stream.next()).await {
            Ok(Some(Ok(Message::Text(data)))) => return Some(data.to_string()),
            Ok(Some(Ok(Message::Ping(_)))) | Ok(Some(Ok(Message::Pong(_)))) => continue,
            _ => return None,
        }
    }
}
