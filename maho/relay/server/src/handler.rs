use std::net::IpAddr;
use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::time::Instant;

use axum::extract::ws::{Message, WebSocket};
use base64::Engine;
use futures_util::{SinkExt, StreamExt};
use tokio::sync::mpsc;

use crate::auth::tokens::hash_token_str;
use crate::server::AppState;
use crate::storage;

struct RateLimit {
    count: u32,
    window_start: Instant,
}

struct IpConnectionGuard {
    state: Arc<AppState>,
    ip: IpAddr,
}

impl Drop for IpConnectionGuard {
    fn drop(&mut self) {
        if let Some(counter) = self.state.ip_connections.get(&self.ip) {
            let prev = counter.fetch_sub(1, Ordering::Relaxed);
            if prev <= 1 {
                drop(counter);
                self.state
                    .ip_connections
                    .remove_if(&self.ip, |_, c| c.load(Ordering::Relaxed) == 0);
            }
        }
    }
}

struct PeerGuard {
    state: Arc<AppState>,
    room_id: String,
    peer_id: String,
    ip: IpAddr,
}

impl Drop for PeerGuard {
    fn drop(&mut self) {
        self.state.rooms.leave(&self.room_id, &self.peer_id);
        if let Some(rooms) = self.state.ip_rooms.get(&self.ip) {
            rooms.remove(&self.room_id);
            if rooms.is_empty() {
                drop(rooms);
                self.state
                    .ip_rooms
                    .remove_if(&self.ip, |_, s| s.is_empty());
            }
        }
    }
}

pub async fn handle_socket(mut socket: WebSocket, state: Arc<AppState>, ip: IpAddr) {
    let _ip_guard = IpConnectionGuard {
        state: state.clone(),
        ip,
    };

    let auth = match tokio::time::timeout(
        std::time::Duration::from_secs(state.config.auth_timeout_secs),
        socket.recv(),
    )
    .await
    {
        Ok(Some(Ok(Message::Text(text)))) => text,
        _ => {
            let _ = socket
                .send(Message::Text(
                    r#"{"type":"error","msg":"auth timeout"}"#.into(),
                ))
                .await;
            return;
        }
    };

    let auth_msg: serde_json::Value = match serde_json::from_str(&auth) {
        Ok(v) => v,
        Err(_) => {
            let _ = socket
                .send(Message::Text(
                    r#"{"type":"error","msg":"invalid auth json"}"#.into(),
                ))
                .await;
            return;
        }
    };

    if auth_msg.get("type").and_then(|v| v.as_str()) != Some("auth") {
        let _ = socket
            .send(Message::Text(
                r#"{"type":"error","msg":"expected auth message"}"#.into(),
            ))
            .await;
        return;
    }

    let room_id = match auth_msg.get("key").and_then(|v| v.as_str()) {
        Some(k) if is_valid_room_id(k) => k.to_string(),
        _ => {
            let _ = socket
                .send(Message::Text(
                    r#"{"type":"error","msg":"invalid room key"}"#.into(),
                ))
                .await;
            return;
        }
    };

    let token = auth_msg
        .get("token")
        .and_then(|v| v.as_str())
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty());

    let client_version = auth_msg
        .get("client_version")
        .and_then(|v| v.as_str())
        .map(String::from);

    let minimum_client_version = std::env::var("MAHO_RELAY_MINIMUM_CLIENT_VERSION")
        .ok()
        .filter(|s| !s.trim().is_empty())
        .unwrap_or_else(|| "1.0.0".to_string());

    if let Some(ref version) = client_version {
        if version_less_than(version, &minimum_client_version) {
            let error_msg = serde_json::json!({
                "type": "error",
                "code": "client_outdated",
                "minimum_version": minimum_client_version,
                "message": "Client version too old. Please update."
            });
            let _ = socket.send(Message::Text(error_msg.to_string().into())).await;
            return;
        }
    }

    let since_seq = auth_msg.get("since_seq").and_then(|v| v.as_i64());
    let token_context = if let Some(token) = token {
        let token_hash = hash_token_str(&token);
        let context = match storage::find_active_session_context_by_access_hash(&state.db, token_hash)
            .await
        {
            Ok(Some(context)) => context,
            Ok(None) => {
                let _ = socket
                    .send(Message::Text(
                        r#"{"type":"error","msg":"invalid access token"}"#.into(),
                    ))
                    .await;
                return;
            }
            Err(e) => {
                tracing::error!(error = %e, "failed to load websocket session");
                let _ = socket
                    .send(Message::Text(
                        r#"{"type":"error","msg":"auth lookup failed"}"#.into(),
                    ))
                    .await;
                return;
            }
        };

        if context.device.room_id != room_id {
            let _ = socket
                .send(Message::Text(
                    r#"{"type":"error","msg":"room mismatch"}"#.into(),
                ))
                .await;
            return;
        }

        if let Err(e) = storage::touch_session_and_device(&state.db, context.session.id, context.device.id)
            .await
        {
            tracing::error!(error = %e, "failed to update session/device activity");
        }

        Some(context)
    } else if state.config.require_auth {
        let _ = socket
            .send(Message::Text(
                r#"{"type":"error","msg":"access token required"}"#.into(),
            ))
            .await;
        return;
    } else {
        None
    };

    if state.config.require_auth && token_context.is_none() {
        let _ = socket
            .send(Message::Text(
                r#"{"type":"error","msg":"access token required"}"#.into(),
            ))
            .await;
        return;
    }

    if let Some(ref context) = token_context {
        if context.session.expires_at <= chrono::Utc::now().timestamp() {
            let _ = socket
                .send(Message::Text(
                    r#"{"type":"error","msg":"session expired"}"#.into(),
                ))
                .await;
            return;
        }
    }

    {
        let ip_room_set = state.ip_rooms.entry(ip).or_default();
        if !ip_room_set.contains(&room_id)
            && ip_room_set.len() as u32 >= state.config.max_rooms_per_ip
        {
            let _ = socket
                .send(Message::Text(
                    r#"{"type":"error","msg":"too many rooms for this IP"}"#.into(),
                ))
                .await;
            return;
        }
    }

    let peer_id = uuid::Uuid::new_v4().to_string();
    let (tx, mut rx) = mpsc::unbounded_channel::<Message>();

    if let Err(e) = state.rooms.join(&room_id, &peer_id, tx) {
        let _ = socket
            .send(Message::Text(
                format!(r#"{{"type":"error","msg":"{}"}}"#, e).into(),
            ))
            .await;
        return;
    }

    state
        .ip_rooms
        .entry(ip)
        .or_default()
        .insert(room_id.clone());

    let _peer_guard = PeerGuard {
        state: state.clone(),
        room_id: room_id.clone(),
        peer_id: peer_id.clone(),
        ip,
    };

    tracing::info!(
        "peer authenticated: room={}, peer={}, version={}",
        room_id,
        peer_id,
        client_version.as_deref().unwrap_or("unknown")
    );

    let _ = socket
        .send(Message::Text(
            format!(r#"{{"type":"auth_ok","peer_id":"{}"}}"#, peer_id).into(),
        ))
        .await;

    if let Some(seq) = since_seq {
        match storage::get_messages_since(&state.db, &room_id, seq).await {
            Ok(messages) => {
                for msg in messages {
                    let payload = serde_json::json!({
                        "type": "catchup",
                        "seq": msg.seq,
                        "blob": base64_encode(&msg.blob),
                    });
                    if socket
                        .send(Message::Text(payload.to_string().into()))
                        .await
                        .is_err()
                    {
                        return;
                    }
                }
            }
            Err(e) => {
                tracing::error!(error = %e, "catchup query failed");
            }
        }
    }

    let _ = socket
        .send(Message::Text(r#"{"type":"catchup_done"}"#.into()))
        .await;

    let (ws_tx, mut ws_rx) = socket.split();
    let ws_tx = Arc::new(tokio::sync::Mutex::new(ws_tx));

    let room_id_clone = room_id.clone();
    let peer_id_clone = peer_id.clone();
    let state_clone = state.clone();
    let max_msg_size = state.config.max_message_size_bytes;
    let max_per_min = state.config.max_messages_per_minute;
    let max_room_bytes = state.config.max_room_size_mb as usize * 1_048_576;
    let ws_tx_clone = ws_tx.clone();

    let mut recv_task = tokio::spawn(async move {
        let mut rate = RateLimit {
            count: 0,
            window_start: Instant::now(),
        };

        while let Some(Ok(msg)) = ws_rx.next().await {
            let (data, broadcast_msg): (Vec<u8>, Message) = match msg {
                Message::Text(t) => {
                    let bytes = t.as_bytes().to_vec();
                    (bytes.clone(), Message::Text(t))
                }
                Message::Binary(b) => {
                    let bytes = b.to_vec();
                    (bytes, Message::Binary(b))
                }
                Message::Close(_) => break,
                _ => continue,
            };

            if data.len() > max_msg_size {
                let _ = ws_tx_clone
                    .lock()
                    .await
                    .send(Message::Text(
                        r#"{"type":"error","msg":"message too large"}"#.into(),
                    ))
                    .await;
                continue;
            }

            if rate.window_start.elapsed().as_secs() >= 60 {
                rate.count = 0;
                rate.window_start = Instant::now();
            }
            rate.count += 1;
            if rate.count > max_per_min {
                let _ = ws_tx_clone
                    .lock()
                    .await
                    .send(Message::Text(
                        r#"{"type":"error","msg":"rate limit exceeded"}"#.into(),
                    ))
                    .await;
                continue;
            }

            if let Ok(current_size) =
                storage::get_room_size(&state_clone.db, &room_id_clone).await
            {
                if current_size as usize + data.len() > max_room_bytes {
                    let _ = ws_tx_clone
                        .lock()
                        .await
                        .send(Message::Text(
                            r#"{"type":"error","msg":"room size limit exceeded"}"#.into(),
                        ))
                        .await;
                    continue;
                }
            }

            match storage::store_message(&state_clone.db, &room_id_clone, data).await {
                Ok(seq) => {
                    state_clone
                        .rooms
                        .broadcast(&room_id_clone, &broadcast_msg, &peer_id_clone);
                    let _ = ws_tx_clone
                        .lock()
                        .await
                        .send(Message::Text(
                            format!(r#"{{"type":"ack","seq":{}}}"#, seq).into(),
                        ))
                        .await;
                }
                Err(e) => {
                    tracing::error!(error = %e, "failed to store message");
                    let _ = ws_tx_clone
                        .lock()
                        .await
                        .send(Message::Text(
                            r#"{"type":"error","msg":"storage error"}"#.into(),
                        ))
                        .await;
                }
            }
        }
    });

    let ws_tx_fwd = ws_tx.clone();
    let mut forward_task = tokio::spawn(async move {
        while let Some(msg) = rx.recv().await {
            let _ = ws_tx_fwd.lock().await.send(msg).await;
        }
    });

    let ws_tx_ping = ws_tx;
    let mut ping_task = tokio::spawn(async move {
        let mut interval = tokio::time::interval(std::time::Duration::from_secs(30));
        loop {
            interval.tick().await;
            if ws_tx_ping
                .lock()
                .await
                .send(Message::Ping(vec![].into()))
                .await
                .is_err()
            {
                break;
            }
        }
    });

    tokio::select! {
        _ = &mut recv_task => {},
        _ = &mut forward_task => {},
        _ = &mut ping_task => {},
    }
    recv_task.abort();
    forward_task.abort();
    ping_task.abort();
}

fn is_valid_room_id(s: &str) -> bool {
    s.len() == 32 && s.chars().all(|c| c.is_ascii_hexdigit())
}

fn version_less_than(a: &str, b: &str) -> bool {
    let parse = |s: &str| -> (u32, u32, u32) {
        let parts: Vec<u32> = s.split('.').filter_map(|p| p.parse().ok()).collect();
        (
            parts.first().copied().unwrap_or(0),
            parts.get(1).copied().unwrap_or(0),
            parts.get(2).copied().unwrap_or(0),
        )
    };
    parse(a) < parse(b)
}

fn base64_encode(data: &[u8]) -> String {
    base64::engine::general_purpose::STANDARD.encode(data)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_version_less_than_basic() {
        assert!(version_less_than("0.9.9", "1.0.0"));
        assert!(!version_less_than("1.0.0", "1.0.0"));
        assert!(!version_less_than("1.0.1", "1.0.0"));
        assert!(!version_less_than("2.0.0", "1.0.0"));
    }

    #[test]
    fn test_version_less_than_major_minor_patch() {
        assert!(version_less_than("0.0.1", "0.0.2"));
        assert!(version_less_than("0.1.0", "0.2.0"));
        assert!(version_less_than("1.0.0", "2.0.0"));
        assert!(!version_less_than("1.1.0", "1.0.9"));
    }

    #[test]
    fn test_version_less_than_empty() {
        assert!(version_less_than("", "1.0.0"));
        assert!(!version_less_than("1.0.0", ""));
    }

    #[test]
    fn test_version_less_than_partial() {
        assert!(!version_less_than("1", "1.0.0"));
        assert!(!version_less_than("1.0", "1.0.0"));
        assert!(version_less_than("0", "1.0.0"));
    }

    #[test]
    fn test_version_less_than_garbage() {
        assert!(version_less_than("abc", "1.0.0"));
        assert!(version_less_than("x.y.z", "1.0.0"));
    }

    #[test]
    fn test_version_less_than_prerelease() {
        assert!(!version_less_than("1.0.0-beta", "1.0.0"));
    }
}
