use crate::auth::tokens::hash_token_str;
use crate::config::RelayConfig;
use crate::db;
use crate::errors::AppError;
use crate::RoomDO;
use base64::Engine;
use serde::{Deserialize, Serialize};
use worker::*;

const AUTH_TIMEOUT_SECS: u64 = 5;
const MAX_MESSAGES_PER_MIN: u32 = 100;
const MAX_UNAUTH_CONNECTIONS_PER_ROOM: usize = 2;
const SYNC_PROTOCOL_VERSION: u32 = 2;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
struct WsSyncEnvelopeV2 {
    protocol_version: u32,
    delivery_id: String,
    payload: String,
    relay_seq: Option<i64>,
}

impl WsSyncEnvelopeV2 {
    fn validate(&self, max_bytes: usize) -> std::result::Result<Vec<u8>, &'static str> {
        if self.protocol_version != SYNC_PROTOCOL_VERSION {
            return Err("unsupported_protocol_version");
        }
        if uuid::Uuid::parse_str(&self.delivery_id).is_err() {
            return Err("invalid_delivery_id");
        }
        let payload = base64::engine::general_purpose::STANDARD
            .decode(&self.payload)
            .map_err(|_| "invalid_payload_encoding")?;
        if payload.is_empty() {
            return Err("empty_payload");
        }
        if payload.len() > max_bytes {
            return Err("message_too_large");
        }
        Ok(payload)
    }

    fn with_seq(&self, seq: i64) -> Self {
        Self {
            protocol_version: self.protocol_version,
            delivery_id: self.delivery_id.clone(),
            payload: self.payload.clone(),
            relay_seq: Some(seq),
        }
    }
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct WsCtx {
    pub peer_id: String,
    pub authenticated: bool,
    #[serde(default)]
    pub access_hash: Option<Vec<u8>>,
    #[serde(default)]
    pub room_key: Option<String>,
    pub connected_at: i64,
    pub rate_count: u32,
    pub rate_window_start: i64,
}

impl WsCtx {
    fn new(now: i64) -> Self {
        Self {
            peer_id: uuid::Uuid::new_v4().to_string(),
            authenticated: false,
            access_hash: None,
            room_key: None,
            connected_at: now,
            rate_count: 0,
            rate_window_start: now,
        }
    }
}

pub async fn ws_upgrade(req: Request, env: Env) -> crate::errors::Result<Response> {
    let url = req.url()?;
    let mut key: Option<String> = None;
    for (k, v) in url.query_pairs() {
        if k == "key" {
            key = Some(v.to_string());
            break;
        }
    }
    let key = do_room_key(key, req.headers().get("X-Room-Key")?);
    let Some(key) = key else {
        return Err(AppError::BadRequest("missing_room_key".to_string()));
    };
    if !is_valid_room_id(&key) {
        return Err(AppError::BadRequest("invalid_room_key".to_string()));
    }

    let namespace = env.durable_object("ROOM")?;
    let id = namespace.id_from_name(&key)?;
    let stub = id.get_stub()?;
    stub.fetch_with_request(req).await.map_err(AppError::from)
}

pub async fn handle_do_fetch(room: &RoomDO, req: Request) -> Result<Response> {
    if req.path() == "/internal/sync/notify" && req.method() == Method::Post {
        return match handle_notification(room, req).await {
            Ok(response) => Ok(response),
            Err(error) => error.to_response(),
        };
    }
    let upgrade = req.headers().get("Upgrade")?.unwrap_or_default();
    if !upgrade.eq_ignore_ascii_case("websocket") {
        return Response::error("expected_websocket_upgrade", 400);
    }

    let url = req.url()?;
    let mut key: Option<String> = None;
    for (k, v) in url.query_pairs() {
        if k == "key" {
            key = Some(v.to_string());
            break;
        }
    }
    let key = match do_room_key(key, req.headers().get("X-Room-Key")?) {
        Some(k) if is_valid_room_id(&k) => k,
        _ => return Response::error("invalid_room_key", 400),
    };

    if room.room_id.borrow().is_none() {
        room.room_id.replace(Some(key.clone()));
    }

    let config = RelayConfig::from_env(&room.env);

    // Count authenticated vs unauthenticated sockets separately: an idle
    // unauthenticated socket must not consume an authenticated slot (that is
    // the DoS the auth-timeout alarm defends against), and gets a lower cap.
    let existing = room.state.get_websockets();
    let mut authed = 0usize;
    let mut unauthed = 0usize;
    for sock in existing.iter() {
        match sock.deserialize_attachment::<WsCtx>() {
            Ok(Some(c)) if c.authenticated => authed += 1,
            _ => unauthed += 1,
        }
    }
    if authed >= config.max_connections_per_room as usize {
        return Response::error("too_many_connections", 429);
    }
    if unauthed >= MAX_UNAUTH_CONNECTIONS_PER_ROOM {
        return Response::error("too_many_unauthenticated_connections", 429);
    }

    let pair = WebSocketPair::new()?;
    let client = pair.client;
    let server = pair.server;

    let now = chrono::Utc::now().timestamp();
    let mut ctx = WsCtx::new(now);
    ctx.room_key = Some(key);
    server.serialize_attachment(&ctx)?;
    room.state.accept_web_socket(&server);

    // Arm an alarm so a socket that never sends the auth message is reaped even
    // if it stays silent (the per-message timeout only fires on inbound data).
    let _ = room
        .state
        .storage()
        .set_alarm(std::time::Duration::from_secs(AUTH_TIMEOUT_SECS + 1))
        .await;

    Response::from_websocket(client)
}

/// Reap unauthenticated timeouts and revoked/expired sessions, including silent
/// recipients. Re-arm while sockets remain so passive sessions are checked too.
pub async fn handle_alarm(room: &RoomDO) -> Result<Response> {
    let now = chrono::Utc::now().timestamp();
    let mut remaining_unauth = false;
    for sock in room.state.get_websockets().iter() {
        let ctx: Option<WsCtx> = sock.deserialize_attachment().ok().flatten();
        let Some(ctx) = ctx else { continue };
        if ctx.authenticated {
            if !socket_authorized(room, &ctx).await? {
                sock.close::<&str>(Some(1008), Some("session_revoked"))?;
            } else {
                remaining_unauth = true;
            }
            continue;
        }
        if now - ctx.connected_at > AUTH_TIMEOUT_SECS as i64 {
            let _ = sock.send_with_str(r#"{"type":"error","msg":"auth_timeout"}"#);
            let _ = sock.close::<&str>(Some(1008), Some("auth_timeout"));
        } else {
            remaining_unauth = true;
        }
    }
    if remaining_unauth {
        let _ = room
            .state
            .storage()
            .set_alarm(std::time::Duration::from_secs(AUTH_TIMEOUT_SECS + 1))
            .await;
    }
    Response::ok("alarm_handled")
}

pub async fn handle_ws_message(
    room: &RoomDO,
    ws: WebSocket,
    message: WebSocketIncomingMessage,
) -> Result<()> {
    let Some(mut ctx): Option<WsCtx> = ws.deserialize_attachment().ok().flatten() else {
        let _ = ws.send_with_str(r#"{"type":"error","msg":"missing_context"}"#);
        let _ = ws.close::<&str>(Some(1011), Some("missing_context"));
        return Ok(());
    };

    if !ctx.authenticated {
        return process_auth(room, ws, message, ctx).await;
    }
    if !socket_authorized(room, &ctx).await? {
        ws.close::<&str>(Some(1008), Some("session_revoked"))?;
        return Ok(());
    }

    let config = RelayConfig::from_env(&room.env);

    let raw_message = match message {
        WebSocketIncomingMessage::String(text) => text,
        WebSocketIncomingMessage::Binary(_) => {
            let _ = ws.send_with_str(r#"{"type":"error","msg":"expected_v2_envelope"}"#);
            return Ok(());
        }
    };
    let envelope: WsSyncEnvelopeV2 = match serde_json::from_str(&raw_message) {
        Ok(envelope) => envelope,
        Err(_) => {
            let _ = ws.send_with_str(r#"{"type":"error","msg":"invalid_v2_envelope"}"#);
            return Ok(());
        }
    };
    let payload_bytes = match envelope.validate(config.max_message_size_bytes) {
        Ok(payload) => payload,
        Err(error) => {
            let error = serde_json::json!({"type": "error", "msg": error.to_string()}).to_string();
            let _ = ws.send_with_str(&error);
            return Ok(());
        }
    };

    if payload_bytes.len() > config.max_message_size_bytes {
        let _ = ws.send_with_str(r#"{"type":"error","msg":"message_too_large"}"#);
        return Ok(());
    }

    let now = chrono::Utc::now().timestamp();
    if now - ctx.rate_window_start >= 60 {
        ctx.rate_count = 0;
        ctx.rate_window_start = now;
    }
    ctx.rate_count = ctx.rate_count.saturating_add(1);
    if ctx.rate_count > MAX_MESSAGES_PER_MIN {
        let _ = ws.send_with_str(r#"{"type":"error","msg":"rate_limit_exceeded"}"#);
        let _ = ws.serialize_attachment(&ctx);
        return Ok(());
    }

    let db = room.env.d1("MAHO_DB")?;
    let room_id = match message_room_id(room.room_id.borrow().clone(), &ctx) {
        Some(r) => r,
        None => {
            let _ = ws.send_with_str(r#"{"type":"error","msg":"room_not_initialized"}"#);
            return Ok(());
        }
    };

    let seq = match db::store_message_with_delivery_id(
        &db,
        &room_id,
        &envelope.delivery_id,
        payload_bytes,
        config.max_room_size_mb as i64 * 1_048_576,
    )
    .await
    {
        Ok(s) => s,
        Err(AppError::BadRequest(message)) if message == "room_size_limit_exceeded" => {
            ws.send_with_str(r#"{"type":"error","msg":"room_size_limit_exceeded"}"#)?;
            ws.serialize_attachment(&ctx)?;
            return Ok(());
        }
        Err(e) => {
            console_error!("store_message failed room={}: {e}", redact_key(&room_id));
            let _ = ws.send_with_str(r#"{"type":"error","msg":"storage_error"}"#);
            let _ = ws.serialize_attachment(&ctx);
            return Ok(());
        }
    };

    broadcast(room, &room_id, &envelope.with_seq(seq), &ctx.peer_id).await?;

    let ack = serde_json::json!({
        "type": "ack",
        "delivery_id": envelope.delivery_id,
        "seq": seq,
    })
    .to_string();
    let _ = ws.send_with_str(&ack);

    let _ = ws.serialize_attachment(&ctx);
    Ok(())
}

async fn broadcast(room: &RoomDO, key: &str, envelope: &WsSyncEnvelopeV2, sender: &str) -> Result<()> {
    let payload = serde_json::to_string(envelope)?;
    for peer in room.state.get_websockets() {
        let peer_ctx: Option<WsCtx> = peer.deserialize_attachment().ok().flatten();
        if !should_receive(peer_ctx.as_ref(), sender) {
            continue;
        }
        let ctx = peer_ctx.as_ref().unwrap();
        if ctx.room_key.as_deref() != Some(key) || !socket_authorized(room, ctx).await? {
            peer.close::<&str>(Some(1008), Some("session_revoked"))?;
            continue;
        }
        peer.send_with_str(&payload)?;
    }
    Ok(())
}

// Reachable only through the Room namespace. Recheck the forwarded user's
// session/ownership and load the authoritative row; never trust a pushed blob.
async fn handle_notification(room: &RoomDO, req: Request) -> crate::errors::Result<Response> {
    let db = room.env.d1("MAHO_DB")?;
    let auth = crate::auth::middleware::get_auth_context(&req, &db).await?;
    let url = req.url()?;
    let key = url.query_pairs().find(|(k, _)| k == "key").map(|(_, v)| v.into_owned())
        .filter(|key| is_valid_room_id(key))
        .ok_or_else(|| AppError::BadRequest("invalid_room_key".to_string()))?;
    if db::get_room_owner_user_id(&db, &key).await? != Some(auth.user.id) {
        return Err(AppError::Auth("room_not_owned".to_string()));
    }
    let delivery = url.query_pairs().find(|(k, _)| k == "delivery_id").map(|(_, v)| v.into_owned())
        .ok_or_else(|| AppError::BadRequest("missing_delivery_id".to_string()))?;
    let stored = db.prepare("SELECT seq, blob, delivery_id FROM messages WHERE room_id = ?1 AND delivery_id = ?2")
        .bind(&[db::js(&key)?, db::js(&delivery)?])?
        .first::<db::StoredMessage>(None).await?
        .ok_or_else(|| AppError::NotFound("delivery_not_found".to_string()))?;
    let envelope = WsSyncEnvelopeV2 {
        protocol_version: SYNC_PROTOCOL_VERSION,
        delivery_id: delivery,
        payload: base64::engine::general_purpose::STANDARD.encode(stored.blob),
        relay_seq: Some(stored.seq),
    };
    broadcast(room, &key, &envelope, "").await?;
    Response::ok("notified").map_err(AppError::from)
}

pub async fn handle_ws_close(_room: &RoomDO, _ws: WebSocket) -> Result<()> {
    Ok(())
}

async fn process_auth(
    room: &RoomDO,
    ws: WebSocket,
    message: WebSocketIncomingMessage,
    mut ctx: WsCtx,
) -> Result<()> {
    let text = match message {
        WebSocketIncomingMessage::String(t) => t,
        WebSocketIncomingMessage::Binary(_) => {
            let _ = ws.send_with_str(r#"{"type":"error","msg":"expected_auth_json"}"#);
            let _ = ws.close::<&str>(Some(1008), Some("expected_auth_json"));
            return Ok(());
        }
    };

    let now = chrono::Utc::now().timestamp();
    if now - ctx.connected_at > AUTH_TIMEOUT_SECS as i64 {
        let _ = ws.send_with_str(r#"{"type":"error","msg":"auth_timeout"}"#);
        let _ = ws.close::<&str>(Some(1008), Some("auth_timeout"));
        return Ok(());
    }

    let auth_msg: serde_json::Value = match serde_json::from_str(&text) {
        Ok(v) => v,
        Err(_) => {
            let _ = ws.send_with_str(r#"{"type":"error","msg":"invalid_auth_json"}"#);
            let _ = ws.close::<&str>(Some(1008), Some("invalid_auth_json"));
            return Ok(());
        }
    };

    if auth_msg.get("type").and_then(|v| v.as_str()) != Some("auth") {
        let _ = ws.send_with_str(r#"{"type":"error","msg":"expected_auth_message"}"#);
        let _ = ws.close::<&str>(Some(1008), Some("expected_auth_message"));
        return Ok(());
    }

    let key = match auth_msg.get("key").and_then(|v| v.as_str()) {
        Some(k) if is_valid_room_id(k) => k.to_string(),
        _ => {
            let _ = ws.send_with_str(r#"{"type":"error","msg":"invalid_room_key"}"#);
            let _ = ws.close::<&str>(Some(1008), Some("invalid_room_key"));
            return Ok(());
        }
    };

    let room_id = message_room_id(room.room_id.borrow().clone(), &ctx);
    if let Some(existing) = room_id {
        if existing != key {
            let _ = ws.send_with_str(r#"{"type":"error","msg":"room_mismatch"}"#);
            let _ = ws.close::<&str>(Some(1008), Some("room_mismatch"));
            return Ok(());
        }
    } else {
        room.room_id.replace(Some(key.clone()));
    }

    let token = auth_msg
        .get("token")
        .and_then(|v| v.as_str())
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty());

    let require_auth = RelayConfig::from_env(&room.env).require_auth;

    let db = room.env.d1("MAHO_DB")?;

    if let Some(token) = token {
        let token_hash = hash_token_str(&token);
        match db::find_active_session_context_by_access_hash(&db, &token_hash).await {
            Ok(Some(context)) => {
                db::claim_room_ownership(&db, &key, context.user.id, now)
                    .await
                    .ok();
                match db::get_room_owner_user_id(&db, &key).await {
                    Ok(Some(owner_id)) if owner_id != context.user.id => {
                        let _ = ws.send_with_str(
                            r#"{"type":"error","msg":"room_owned_by_another_user"}"#,
                        );
                        let _ = ws.close::<&str>(Some(1008), Some("room_owned_by_another_user"));
                        return Ok(());
                    }
                    Ok(Some(_)) => {}
                    Ok(None) => {
                        ws.close::<&str>(Some(1008), Some("room_not_owned"))?;
                        return Ok(());
                    }
                    Err(e) => {
                        console_error!(
                            "room ownership lookup failed key={}: {e}",
                            redact_key(&key)
                        );
                        let _ = ws.send_with_str(r#"{"type":"error","msg":"auth_lookup_failed"}"#);
                        let _ = ws.close::<&str>(Some(1011), Some("auth_lookup_failed"));
                        return Ok(());
                    }
                }
                ctx.access_hash = Some(token_hash.to_vec());
                if let Err(e) = db::touch_session_and_device(
                    &db,
                    context.user.id,
                    context.session.id,
                    context.device.id,
                    now,
                )
                .await
                {
                    console_error!("touch_session_and_device failed: {e}");
                }
            }
            Ok(None) => {
                let _ = ws.send_with_str(r#"{"type":"error","msg":"invalid_access_token"}"#);
                let _ = ws.close::<&str>(Some(1008), Some("invalid_access_token"));
                return Ok(());
            }
            Err(e) => {
                console_error!("session lookup failed: {e}");
                let _ = ws.send_with_str(r#"{"type":"error","msg":"auth_lookup_failed"}"#);
                let _ = ws.close::<&str>(Some(1011), Some("auth_lookup_failed"));
                return Ok(());
            }
        }
    } else if require_auth {
        let _ = ws.send_with_str(r#"{"type":"error","msg":"access_token_required"}"#);
        let _ = ws.close::<&str>(Some(1008), Some("access_token_required"));
        return Ok(());
    }

    ctx.room_key = Some(key.clone());
    ctx.authenticated = true;
    ctx.rate_window_start = now;
    ctx.rate_count = 0;

    let auth_ok = format!(r#"{{"type":"auth_ok","peer_id":"{}"}}"#, ctx.peer_id);
    let _ = ws.send_with_str(&auth_ok);

    let messages = if let Some(since_seq) = auth_msg.get("since_seq").and_then(|v| v.as_i64()) {
        db::get_messages_since(&db, &key, since_seq).await.map_err(|e| e.to_string())
    } else {
        Ok(Vec::new())
    };
    if let Err(e) = send_catchup(messages, |payload| ws.send_with_str(payload).map_err(|e| e.to_string())) {
        console_error!("catchup failed room={}: {e}", redact_key(&key));
        ws.close::<&str>(Some(1011), Some("catchup_failed"))?;
        return Ok(());
    }

    let _ = ws.serialize_attachment(&ctx);
    Ok(())
}

fn send_catchup(
    messages: std::result::Result<Vec<db::StoredMessage>, String>,
    mut send: impl FnMut(&str) -> std::result::Result<(), String>,
) -> std::result::Result<(), String> {
    let messages = match messages {
        Ok(messages) => messages,
        Err(error) => {
            send(r#"{"type":"error","msg":"catchup_failed","retryable":true}"#)?;
            return Err(error);
        }
    };
    for m in messages {
        let envelope = WsSyncEnvelopeV2 {
            protocol_version: SYNC_PROTOCOL_VERSION,
            delivery_id: m.delivery_id.unwrap_or_else(|| format!("legacy-v1-seq-{}", m.seq)),
            payload: base64::engine::general_purpose::STANDARD.encode(&m.blob),
            relay_seq: Some(m.seq),
        };
        send(&serde_json::to_string(&envelope).map_err(|e| e.to_string())?)?;
    }
    send(r#"{"type":"catchup_done"}"#)
}

fn do_room_key(query: Option<String>, header: Option<String>) -> Option<String> {
    query.or(header)
}

fn message_room_id(cached: Option<String>, ctx: &WsCtx) -> Option<String> {
    cached.or_else(|| ctx.room_key.clone())
}

fn traffic_authorized(authenticated: bool, require_auth: bool, has_token: bool, active: bool) -> bool {
    authenticated && if has_token { active } else { !require_auth }
}

// Revalidate both senders and passive recipients. A cached boolean must not
// outlive logout, device/account deletion, access-token rotation or expiry.
async fn socket_authorized(room: &RoomDO, ctx: &WsCtx) -> Result<bool> {
    let mut active = false;
    if let (Some(hash), Some(key)) = (&ctx.access_hash, &ctx.room_key) {
        let db = room.env.d1("MAHO_DB")?;
        if let Some(context) = db::find_active_session_context_by_access_hash(&db, hash)
            .await.map_err(|e| Error::RustError(e.to_string()))? {
            active = db::get_room_owner_user_id(&db, key)
                .await.map_err(|e| Error::RustError(e.to_string()))?
                == Some(context.user.id);
        }
    }
    Ok(traffic_authorized(ctx.authenticated, RelayConfig::from_env(&room.env).require_auth,
        ctx.access_hash.is_some(), active))
}

fn should_receive(peer: Option<&WsCtx>, sender: &str) -> bool {
    peer.is_some_and(|c| c.authenticated && c.peer_id != sender)
}

fn is_valid_room_id(s: &str) -> bool {
    s.len() == 32 && s.chars().all(|c| c.is_ascii_hexdigit())
}

fn redact_key(key: &str) -> String {
    use sha2::{Digest, Sha256};
    let digest = Sha256::digest(key.as_bytes());
    let mut prefix = String::with_capacity(8);
    for byte in digest.iter().take(4) {
        prefix.push_str(&format!("{byte:02x}"));
    }
    prefix
}

#[cfg(test)]
mod websocket_v2_tests {
    use super::*;

    const DELIVERY_ID: &str = "550e8400-e29b-41d4-a716-446655440000";

    #[test]
    fn failed_catchup_never_emits_success() {
        let mut wire = Vec::new();
        let result = send_catchup(Err("database unavailable".to_string()), |payload| {
            wire.push(serde_json::from_str::<serde_json::Value>(payload).unwrap());
            Ok(())
        });
        assert!(result.is_err());
        assert!(wire.iter().all(|message| message["type"] != "catchup_done"));
        assert_eq!(wire[0]["type"], "error");
    }

    #[test]
    fn header_only_websocket_selects_the_requested_room() {
        let key = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa".to_string();
        assert_eq!(do_room_key(None, Some(key.clone())), Some(key));
    }

    #[test]
    fn hibernated_socket_keeps_its_authenticated_room() {
        let mut ctx = WsCtx::new(0);
        ctx.authenticated = true;
        ctx.room_key = Some("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa".to_string());
        let restored: WsCtx = serde_json::from_str(&serde_json::to_string(&ctx).unwrap()).unwrap();
        assert_eq!(message_room_id(None, &restored), ctx.room_key);
    }

    #[test]
    fn established_socket_cannot_send_or_receive_after_revocation() {
        assert!(!traffic_authorized(true, true, true, false));
        assert!(!traffic_authorized(true, true, false, false));
        assert!(!traffic_authorized(true, false, true, false));
        assert!(traffic_authorized(true, true, true, true));
        assert!(traffic_authorized(true, false, false, false));
    }

    #[test]
    fn broadcast_excludes_unauthenticated_and_missing_attachments() {
        let mut peer = WsCtx::new(0);
        assert!(!should_receive(Some(&peer), "sender"));
        assert!(!should_receive(None, "sender"));
        peer.authenticated = true;
        assert!(should_receive(Some(&peer), "sender"));
        assert!(!should_receive(Some(&peer), &peer.peer_id));
    }

    #[test]
    fn websocket_v2_envelope_rejects_invalid_metadata_without_decoding_payload() {
        let envelope = WsSyncEnvelopeV2 {
            protocol_version: SYNC_PROTOCOL_VERSION,
            delivery_id: DELIVERY_ID.to_string(),
            payload: "AA==".to_string(),
            relay_seq: None,
        };
        assert_eq!(envelope.validate(1).expect("opaque payload"), vec![0]);

        let mut invalid_version = envelope.clone();
        invalid_version.protocol_version = 1;
        assert_eq!(
            invalid_version.validate(1),
            Err("unsupported_protocol_version")
        );

        let mut invalid_id = envelope.clone();
        invalid_id.delivery_id = "not-a-uuid".to_string();
        assert_eq!(invalid_id.validate(1), Err("invalid_delivery_id"));
    }

    #[test]
    fn websocket_v2_echo_preserves_delivery_id_and_assigns_sequence() {
        let envelope = WsSyncEnvelopeV2 {
            protocol_version: SYNC_PROTOCOL_VERSION,
            delivery_id: DELIVERY_ID.to_string(),
            payload: "AA==".to_string(),
            relay_seq: None,
        };

        let echoed = envelope.with_seq(42);
        assert_eq!(echoed.delivery_id, DELIVERY_ID);
        assert_eq!(echoed.relay_seq, Some(42));
        assert_eq!(echoed.payload, envelope.payload);
    }
}
