use crate::auth::tokens::hash_token_str;
use crate::db;
use crate::errors::AppError;
use crate::RoomDO;
use base64::Engine;
use serde::{Deserialize, Serialize};
use worker::*;

const AUTH_TIMEOUT_SECS: u64 = 5;
const MAX_MESSAGE_SIZE: usize = 1_048_576;
const MAX_MESSAGES_PER_MIN: u32 = 100;
const MAX_ROOM_BYTES: i64 = 50 * 1_048_576;
const MAX_CONNECTIONS_PER_ROOM: usize = 5;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct WsCtx {
    pub peer_id: String,
    pub authenticated: bool,
    pub connected_at: i64,
    pub rate_count: u32,
    pub rate_window_start: i64,
}

impl WsCtx {
    fn new(now: i64) -> Self {
        Self {
            peer_id: uuid::Uuid::new_v4().to_string(),
            authenticated: false,
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
    let key = key.or_else(|| req.headers().get("X-Room-Key").ok().flatten());
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

pub async fn handle_do_fetch(room: &mut RoomDO, req: Request) -> Result<Response> {
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
    let key = match key {
        Some(k) if is_valid_room_id(&k) => k,
        _ => return Response::error("invalid_room_key", 400),
    };

    if room.room_id.is_none() {
        room.room_id = Some(key.clone());
    }

    let existing = room.state.get_websockets();
    if existing.len() >= MAX_CONNECTIONS_PER_ROOM {
        return Response::error("too_many_connections", 429);
    }

    let pair = WebSocketPair::new()?;
    let client = pair.client;
    let server = pair.server;

    let now = chrono::Utc::now().timestamp();
    let ctx = WsCtx::new(now);
    server.serialize_attachment(&ctx)?;
    room.state.accept_web_socket(&server);

    Response::from_websocket(client)
}

pub async fn handle_ws_message(
    room: &mut RoomDO,
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

    let (payload_bytes, echo_text): (Vec<u8>, Option<String>) = match message {
        WebSocketIncomingMessage::String(t) => (t.as_bytes().to_vec(), Some(t)),
        WebSocketIncomingMessage::Binary(b) => (b, None),
    };

    if payload_bytes.len() > MAX_MESSAGE_SIZE {
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
    let room_id = match room.room_id.clone() {
        Some(r) => r,
        None => {
            let _ = ws.send_with_str(r#"{"type":"error","msg":"room_not_initialized"}"#);
            return Ok(());
        }
    };

    match db::get_room_size(&db, &room_id).await {
        Ok(size) if size + payload_bytes.len() as i64 > MAX_ROOM_BYTES => {
            let _ = ws.send_with_str(r#"{"type":"error","msg":"room_size_limit_exceeded"}"#);
            let _ = ws.serialize_attachment(&ctx);
            return Ok(());
        }
        Err(e) => {
            console_error!("get_room_size failed: {e}");
        }
        _ => {}
    }

    let seq = match db::store_message(&db, &room_id, payload_bytes.clone()).await {
        Ok(s) => s,
        Err(e) => {
            console_error!("store_message failed room={}: {e}", redact_key(&room_id));
            let _ = ws.send_with_str(r#"{"type":"error","msg":"storage_error"}"#);
            let _ = ws.serialize_attachment(&ctx);
            return Ok(());
        }
    };

    let peers = room.state.get_websockets();
    for peer in peers.iter() {
        let peer_ctx: Option<WsCtx> = peer.deserialize_attachment().ok().flatten();
        let is_sender = peer_ctx
            .as_ref()
            .map(|c| c.peer_id == ctx.peer_id)
            .unwrap_or(false);
        if is_sender {
            continue;
        }
        match echo_text.as_ref() {
            Some(t) => {
                let _ = peer.send_with_str(t);
            }
            None => {
                let _ = peer.send_with_bytes(&payload_bytes);
            }
        }
    }

    let ack = format!(r#"{{"type":"ack","seq":{seq}}}"#);
    let _ = ws.send_with_str(&ack);

    let _ = ws.serialize_attachment(&ctx);
    Ok(())
}

pub async fn handle_ws_close(_room: &mut RoomDO, _ws: WebSocket) -> Result<()> {
    Ok(())
}

async fn process_auth(
    room: &mut RoomDO,
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

    if let Some(existing) = &room.room_id {
        if existing != &key {
            let _ = ws.send_with_str(r#"{"type":"error","msg":"room_mismatch"}"#);
            let _ = ws.close::<&str>(Some(1008), Some("room_mismatch"));
            return Ok(());
        }
    } else {
        room.room_id = Some(key.clone());
    }

    let token = auth_msg
        .get("token")
        .and_then(|v| v.as_str())
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty());

    let require_auth = room
        .env
        .var("REQUIRE_AUTH")
        .ok()
        .map(|v| v.to_string().eq_ignore_ascii_case("true"))
        .unwrap_or(true);

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
                    Ok(_) => {}
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
                if let Err(e) =
                    db::touch_session_and_device(&db, context.session.id, context.device.id, now)
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

    ctx.authenticated = true;
    ctx.rate_window_start = now;
    ctx.rate_count = 0;

    let auth_ok = format!(r#"{{"type":"auth_ok","peer_id":"{}"}}"#, ctx.peer_id);
    let _ = ws.send_with_str(&auth_ok);

    if let Some(since_seq) = auth_msg.get("since_seq").and_then(|v| v.as_i64()) {
        match db::get_messages_since(&db, &key, since_seq).await {
            Ok(messages) => {
                for m in messages {
                    let b64 = base64::engine::general_purpose::STANDARD.encode(&m.blob);
                    let payload =
                        format!(r#"{{"type":"catchup","seq":{},"blob":"{}"}}"#, m.seq, b64);
                    if ws.send_with_str(&payload).is_err() {
                        return Ok(());
                    }
                }
            }
            Err(e) => console_error!("catchup query failed room={}: {e}", redact_key(&key)),
        }
    }

    let _ = ws.send_with_str(r#"{"type":"catchup_done"}"#);

    let _ = ws.serialize_attachment(&ctx);
    Ok(())
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
