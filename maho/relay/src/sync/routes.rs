use crate::auth::middleware::get_auth_context;
use crate::db;
use crate::errors::{AppError, Result};
use aes_gcm::{
    aead::{Aead, Payload},
    Aes256Gcm, KeyInit, Nonce,
};
use base64::{engine::general_purpose::STANDARD, Engine};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use worker::*;

/// Maximum V2 envelopes accepted in one `/sync/push` batch and returned by one
/// `/sync/pull` page. Mobile cycles are bounded, so a device with a long backlog
/// makes progress across several short foreground/background runs.
const MAX_PUSH_BATCH: usize = 64;
const MAX_PULL_LIMIT: usize = 128;
const DEFAULT_PULL_LIMIT: usize = 64;
const SYNC_BOOTSTRAP_VERSION: u8 = 1;
const SYNC_BOOTSTRAP_NONCE_LEN: usize = 12;
const SYNC_BOOTSTRAP_SEED_LEN: usize = 32;

#[derive(Debug, Deserialize)]
pub struct SyncBootstrapPutRequest {
    pub version: u8,
    pub room_id: String,
    pub seed: String,
}

#[derive(Debug, Serialize)]
pub struct SyncBootstrapResponse {
    pub version: u8,
    pub room_id: String,
    pub seed: String,
    pub created_at: i64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct ParsedSyncBootstrap {
    room_id: String,
    seed: [u8; SYNC_BOOTSTRAP_SEED_LEN],
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct EncryptedSyncBootstrap {
    ciphertext: Vec<u8>,
    nonce: Vec<u8>,
}

/// Encrypts the account bootstrap before D1 persistence using AES-256-GCM.
/// The authenticated data binds ciphertext to the account and canonical room.
struct SyncBootstrapCipher {
    secret: Vec<u8>,
}

impl SyncBootstrapCipher {
    fn from_env(env: &Env) -> Result<Self> {
        let config = crate::config::RelayConfig::from_env(env);
        let secret = config.sync_bootstrap_secret.ok_or_else(|| {
            AppError::Internal("sync bootstrap secret not configured".to_string())
        })?;
        Self::from_secret(&secret).map_err(AppError::Internal)
    }

    fn from_secret(secret: &str) -> std::result::Result<Self, String> {
        let normalized = secret.trim();
        if normalized.len() < 32 {
            return Err("sync bootstrap secret must be at least 32 bytes".to_string());
        }
        Ok(Self {
            secret: normalized.as_bytes().to_vec(),
        })
    }

    fn encrypt(
        &self,
        seed: &[u8; SYNC_BOOTSTRAP_SEED_LEN],
        user_id: i32,
        room_id: &str,
    ) -> std::result::Result<EncryptedSyncBootstrap, String> {
        let mut nonce = [0u8; SYNC_BOOTSTRAP_NONCE_LEN];
        getrandom::getrandom(&mut nonce).map_err(|error| error.to_string())?;
        let key = self.key();
        let cipher = Aes256Gcm::new_from_slice(&key).map_err(|error| error.to_string())?;
        let aad = self.aad(user_id, room_id);
        let ciphertext = cipher
            .encrypt(
                Nonce::from_slice(&nonce),
                Payload {
                    msg: seed,
                    aad: &aad,
                },
            )
            .map_err(|_| "failed to encrypt sync bootstrap".to_string())?;
        Ok(EncryptedSyncBootstrap {
            ciphertext,
            nonce: nonce.to_vec(),
        })
    }

    fn decrypt(
        &self,
        encrypted: &EncryptedSyncBootstrap,
        user_id: i32,
        room_id: &str,
    ) -> std::result::Result<[u8; SYNC_BOOTSTRAP_SEED_LEN], String> {
        if encrypted.nonce.len() != SYNC_BOOTSTRAP_NONCE_LEN {
            return Err("invalid encrypted sync bootstrap".to_string());
        }
        let key = self.key();
        let cipher = Aes256Gcm::new_from_slice(&key).map_err(|error| error.to_string())?;
        let aad = self.aad(user_id, room_id);
        let decrypted = cipher
            .decrypt(
                Nonce::from_slice(&encrypted.nonce),
                Payload {
                    msg: &encrypted.ciphertext,
                    aad: &aad,
                },
            )
            .map_err(|_| "invalid encrypted sync bootstrap".to_string())?;
        decrypted
            .try_into()
            .map_err(|_| "invalid encrypted sync bootstrap".to_string())
    }

    fn key(&self) -> [u8; SYNC_BOOTSTRAP_SEED_LEN] {
        let mut hasher = Sha256::new();
        hasher.update(b"maho-sync-bootstrap-v1-key");
        hasher.update(&self.secret);
        hasher.finalize().into()
    }

    fn aad(&self, user_id: i32, room_id: &str) -> Vec<u8> {
        format!("maho-sync-bootstrap-v1:{user_id}:{room_id}").into_bytes()
    }
}

fn parse_sync_bootstrap_request(
    request: &SyncBootstrapPutRequest,
) -> std::result::Result<ParsedSyncBootstrap, &'static str> {
    if request.version != SYNC_BOOTSTRAP_VERSION {
        return Err("unsupported_bootstrap_version");
    }
    let room_id = parse_room_id(Some(&request.room_id)).map_err(|_| "invalid_room_id")?;
    let decoded = STANDARD
        .decode(request.seed.as_bytes())
        .map_err(|_| "invalid_bootstrap_seed")?;
    let seed = decoded.try_into().map_err(|_| "invalid_bootstrap_seed")?;
    Ok(ParsedSyncBootstrap { room_id, seed })
}

fn bootstrap_response(
    cipher: &SyncBootstrapCipher,
    user_id: i32,
    record: db::SyncBootstrapRecord,
) -> Result<SyncBootstrapResponse> {
    if record.version != i32::from(SYNC_BOOTSTRAP_VERSION) {
        return Err(AppError::Internal(
            "unsupported stored sync bootstrap version".to_string(),
        ));
    }
    let encrypted = EncryptedSyncBootstrap {
        ciphertext: record.ciphertext,
        nonce: record.nonce,
    };
    let seed = cipher
        .decrypt(&encrypted, user_id, &record.room_id)
        .map_err(AppError::Internal)?;
    Ok(SyncBootstrapResponse {
        version: SYNC_BOOTSTRAP_VERSION,
        room_id: record.room_id,
        seed: STANDARD.encode(seed),
        created_at: record.created_at,
    })
}

/// The one opaque transport envelope shared by WebSocket fan-out, WebSocket
/// catch-up, and mobile REST pull. `payload` is base64 of the client's encrypted
/// bytes; the relay never decodes application content beyond checking that it is
/// syntactically base64 and within the size bound.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SyncEnvelopeV2 {
    pub protocol_version: u32,
    pub delivery_id: String,
    pub payload: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub relay_seq: Option<i64>,
}

impl SyncEnvelopeV2 {
    /// Validates envelope syntax and size, returning the decoded opaque bytes.
    /// The error strings are the client-facing 4xx contract.
    pub fn validate(&self, max_bytes: usize) -> std::result::Result<Vec<u8>, &'static str> {
        if self.protocol_version != 2 {
            return Err("unsupported_protocol_version");
        }
        if !is_valid_delivery_id(&self.delivery_id) {
            return Err("invalid_delivery_id");
        }
        if self.payload.is_empty() {
            return Err("empty_payload");
        }
        if crate::config::base64_exceeds_cap(self.payload.len(), max_bytes) {
            return Err("message_too_large");
        }
        let decoded = base64::engine::general_purpose::STANDARD
            .decode(self.payload.as_bytes())
            .map_err(|_| "invalid_payload_encoding")?;
        if decoded.is_empty() {
            return Err("empty_payload");
        }
        if decoded.len() > max_bytes {
            return Err("message_too_large");
        }
        Ok(decoded)
    }

    /// The stored form handed back to readers: identical opaque payload plus the
    /// room sequence the relay assigned.
    pub fn with_relay_seq(&self, seq: i64) -> Self {
        Self {
            protocol_version: self.protocol_version,
            delivery_id: self.delivery_id.clone(),
            payload: self.payload.clone(),
            relay_seq: Some(seq),
        }
    }

    /// Rebuilds the envelope for a stored log row. Legacy V1 rows carry no
    /// delivery id; they are surfaced with a stable synthetic one derived from
    /// the room sequence so a V2 reader can still de-duplicate them.
    pub fn from_stored(message: &db::StoredMessage) -> Self {
        Self {
            protocol_version: 2,
            delivery_id: message
                .delivery_id
                .clone()
                .unwrap_or_else(|| legacy_delivery_id(message.seq)),
            payload: base64::engine::general_purpose::STANDARD.encode(&message.blob),
            relay_seq: Some(message.seq),
        }
    }
}

/// Acknowledgement of one durable delivery. The client clears an outbox row only
/// when its exact `delivery_id` comes back.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RelayAckV2 {
    pub delivery_id: String,
    pub seq: i64,
}

#[derive(Debug, Deserialize)]
pub struct PushRequest {
    /// The recovery-derived 32-hex sync room. Never the per-device auth room.
    pub room_id: Option<String>,
    #[serde(default)]
    pub messages: Vec<SyncEnvelopeV2>,
}

#[derive(Debug, Serialize)]
pub struct PushResponse {
    pub acks: Vec<RelayAckV2>,
}

#[derive(Debug, Serialize)]
pub struct PullResponse {
    pub messages: Vec<SyncEnvelopeV2>,
    pub has_more: bool,
}

#[derive(Debug, Deserialize)]
pub struct UploadSnapshotRequest {
    pub blob: String,
    pub hlc_ts_ceiling: i64,
    /// Explicit recovery-derived room. Absent only for legacy clients that
    /// predate the shared-room protocol, which stay on their device room.
    #[serde(default)]
    pub room_id: Option<String>,
}

#[derive(Debug, Serialize)]
pub struct DownloadSnapshotResponse {
    pub hlc_ts_ceiling: i64,
    pub created_at: i64,
    pub creator_device_id: i32,
    pub blob: String,
}

/// A sync room id is exactly the 32-hex value the client derives from the
/// recovery seed. It must be supplied explicitly: `auth.device.room_id` is a
/// per-device auth record and would isolate every device of one account.
pub fn parse_room_id(raw: Option<&str>) -> std::result::Result<String, &'static str> {
    let Some(raw) = raw.map(str::trim).filter(|s| !s.is_empty()) else {
        return Err("missing_room_id");
    };
    if !is_valid_room_id(raw) {
        return Err("invalid_room_id");
    }
    Ok(raw.to_string())
}

fn is_valid_room_id(s: &str) -> bool {
    s.len() == 32 && s.chars().all(|c| c.is_ascii_hexdigit())
}

/// Canonical 8-4-4-4-12 lowercase-or-uppercase hex UUID, matching the delivery
/// ids clients mint with `uuid::Uuid::new_v4()`.
fn is_valid_delivery_id(s: &str) -> bool {
    let groups = [8usize, 4, 4, 4, 12];
    let mut parts = s.split('-');
    for expected in groups {
        let Some(part) = parts.next() else {
            return false;
        };
        if part.len() != expected || !part.chars().all(|c| c.is_ascii_hexdigit()) {
            return false;
        }
    }
    parts.next().is_none()
}

fn legacy_delivery_id(seq: i64) -> String {
    format!("legacy-v1-seq-{seq}")
}

/// Claims an unowned room for the authenticated user and rejects a room owned by
/// anyone else. Identical to the Room Durable Object's check, so HTTP and
/// WebSocket cannot diverge on who may touch a room.
async fn authorize_room(
    db: &D1Database,
    room_id: &str,
    user_id: i32,
    now: i64,
) -> Result<()> {
    match db::get_room_owner_user_id(db, room_id).await? {
        Some(owner) if owner != user_id => Err(AppError::Unauthorized(
            "room_owned_by_another_user".to_string(),
        )),
        Some(_) => Ok(()),
        None => {
            db::claim_room_ownership(db, room_id, user_id, now).await?;
            // Re-verify fully fail-closed: after the ON CONFLICT DO NOTHING
            // claim, require the owner to be exactly this user (a concurrent
            // claim may have won; None should be unreachable but is rejected).
            if db::get_room_owner_user_id(db, room_id).await? != Some(user_id) {
                return Err(AppError::Unauthorized(
                    "room_owned_by_another_user".to_string(),
                ));
            }
            Ok(())
        }
    }
}

fn query_param(req: &Request, name: &str) -> Result<Option<String>> {
    let url = req.url()?;
    Ok(url
        .query_pairs()
        .find(|(k, _)| k == name)
        .map(|(_, v)| v.to_string()))
}

pub async fn get_bootstrap(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    let record = db::get_sync_bootstrap(&db, auth.user.id)
        .await?
        .ok_or_else(|| AppError::NotFound("sync_bootstrap_not_configured".to_string()))?;
    let cipher = SyncBootstrapCipher::from_env(&env)?;
    Response::from_json(&bootstrap_response(&cipher, auth.user.id, record)?).map_err(AppError::from)
}

pub async fn put_bootstrap(mut req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    let request: SyncBootstrapPutRequest = req.json().await.map_err(|error| {
        console_error!("invalid_json detail (sync bootstrap): {error}");
        AppError::BadRequest("invalid_json".to_string())
    })?;
    let parsed = parse_sync_bootstrap_request(&request)
        .map_err(|error| AppError::BadRequest(error.to_string()))?;
    let now = chrono::Utc::now().timestamp();
    authorize_room(&db, &parsed.room_id, auth.user.id, now).await?;
    let cipher = SyncBootstrapCipher::from_env(&env)?;
    let encrypted = cipher
        .encrypt(&parsed.seed, auth.user.id, &parsed.room_id)
        .map_err(AppError::Internal)?;
    let created = db::create_sync_bootstrap(
        &db,
        auth.user.id,
        &parsed.room_id,
        i32::from(SYNC_BOOTSTRAP_VERSION),
        encrypted.ciphertext,
        encrypted.nonce,
        now,
    )
    .await?;
    let did_create = created.is_some();
    let record = match created {
        Some(record) => record,
        None => db::get_sync_bootstrap(&db, auth.user.id)
            .await?
            .ok_or_else(|| {
                AppError::Internal("sync bootstrap disappeared after conflict".to_string())
            })?,
    };
    let response = bootstrap_response(&cipher, auth.user.id, record)?;
    if response.room_id != parsed.room_id
        || STANDARD.decode(&response.seed).ok().as_deref() != Some(parsed.seed.as_slice())
    {
        return Err(AppError::Conflict(
            "sync_bootstrap_already_configured".to_string(),
        ));
    }
    let status = if did_create { 201 } else { 200 };
    Response::from_json(&response)
        .map(|response| response.with_status(status))
        .map_err(AppError::from)
}

/// Authenticated at-least-once push of V2 envelopes into the shared room log.
/// Re-sending an already stored delivery id returns the original sequence, so a
/// client that lost the response can retry safely.
pub async fn push(mut req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    let now = chrono::Utc::now().timestamp();

    let payload: PushRequest = req.json().await.map_err(|e| {
        console_error!("invalid_json detail (sync push): {e}");
        AppError::BadRequest("invalid_json".to_string())
    })?;

    let room_id = parse_room_id(payload.room_id.as_deref())
        .map_err(|e| AppError::BadRequest(e.to_string()))?;
    authorize_room(&db, &room_id, auth.user.id, now).await?;

    if payload.messages.is_empty() {
        return Response::from_json(&PushResponse { acks: Vec::new() }).map_err(AppError::from);
    }
    if payload.messages.len() > MAX_PUSH_BATCH {
        return Err(AppError::BadRequest("too_many_messages".to_string()));
    }

    let max_message_bytes = crate::config::RelayConfig::from_env(&env).max_message_size_bytes;
    let mut decoded_batch = Vec::with_capacity(payload.messages.len());
    for envelope in &payload.messages {
        let bytes = envelope
            .validate(max_message_bytes)
            .map_err(|e| AppError::BadRequest(e.to_string()))?;
        decoded_batch.push((envelope.delivery_id.clone(), bytes));
    }

    let mut acks = Vec::with_capacity(decoded_batch.len());
    for (delivery_id, bytes) in decoded_batch {
        let seq = db::store_message_with_delivery_id(&db, &room_id, &delivery_id, bytes).await?;
        acks.push(RelayAckV2 { delivery_id, seq });
    }

    let _ =
        db::touch_session_and_device(&db, auth.user.id, auth.session.id, auth.device.id, now).await;

    Response::from_json(&PushResponse { acks }).map_err(AppError::from)
}

/// Authenticated paginated read of the same shared room log the WebSocket
/// catch-up serves, in the same order and the same envelope shape.
pub async fn pull(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    let now = chrono::Utc::now().timestamp();

    let room_id = parse_room_id(query_param(&req, "room_id")?.as_deref())
        .map_err(|e| AppError::BadRequest(e.to_string()))?;
    authorize_room(&db, &room_id, auth.user.id, now).await?;

    let after_seq = match query_param(&req, "after_seq")? {
        Some(raw) => raw
            .trim()
            .parse::<i64>()
            .map_err(|_| AppError::BadRequest("invalid_after_seq".to_string()))?,
        None => -1,
    };
    let limit = match query_param(&req, "limit")? {
        Some(raw) => raw
            .trim()
            .parse::<usize>()
            .map_err(|_| AppError::BadRequest("invalid_limit".to_string()))?,
        None => DEFAULT_PULL_LIMIT,
    };
    if limit == 0 || limit > MAX_PULL_LIMIT {
        return Err(AppError::BadRequest("invalid_limit".to_string()));
    }

    let (stored, has_more) = db::get_messages_page(&db, &room_id, after_seq, limit).await?;
    let messages = stored.iter().map(SyncEnvelopeV2::from_stored).collect();

    let _ =
        db::touch_session_and_device(&db, auth.user.id, auth.session.id, auth.device.id, now).await;

    Response::from_json(&PullResponse { messages, has_more }).map_err(AppError::from)
}

pub async fn upload_snapshot(mut req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    let now = chrono::Utc::now().timestamp();

    let max_bytes = crate::config::max_snapshot_bytes(&env);

    let payload: UploadSnapshotRequest = req.json().await.map_err(|e| {
        console_error!("invalid_json detail (snapshot upload): {e}");
        AppError::BadRequest("invalid_json".to_string())
    })?;

    // Snapshots are recovery/bootstrap support, not the steady-state outbox
    // protocol, but they must live in the same shared room as the log so a new
    // device can bootstrap from them. A client that sends the explicit derived
    // room gets it; the legacy per-device namespace stays readable for clients
    // that predate the shared-room protocol.
    let room_id = match payload.room_id.as_deref() {
        Some(_) => parse_room_id(payload.room_id.as_deref())
            .map_err(|e| AppError::BadRequest(e.to_string()))?,
        None => auth.device.room_id.clone(),
    };
    authorize_room(&db, &room_id, auth.user.id, now).await?;

    // Reject oversize uploads on the encoded length before allocating a decode
    // buffer, then confirm the exact decoded size.
    if crate::config::base64_exceeds_cap(payload.blob.len(), max_bytes) {
        return Err(AppError::BadRequest("snapshot_too_large".to_string()));
    }

    let decoded = base64::engine::general_purpose::STANDARD
        .decode(payload.blob.as_bytes())
        .map_err(|_| AppError::BadRequest("invalid_base64_blob".to_string()))?;

    if decoded.len() > max_bytes {
        return Err(AppError::BadRequest("snapshot_too_large".to_string()));
    }

    // Single conditional upsert: the newer-hlc guard is in the statement, so no
    // SELECT-then-write TOCTOU. Older/equal hlc returns no row -> snapshot_not_newer.
    let blob_js: wasm_bindgen::JsValue =
        serde_wasm_bindgen::to_value(&decoded).map_err(|e| AppError::Internal(e.to_string()))?;
    let stmt = db.prepare(
        "INSERT INTO snapshots (room_id, hlc_ts_ceiling, creator_device_id, blob, created_at)
         VALUES (?1, ?2, ?3, ?4, ?5)
         ON CONFLICT(room_id) DO UPDATE SET
            hlc_ts_ceiling = ?2,
            creator_device_id = ?3,
            blob = ?4,
            created_at = ?5
         WHERE excluded.hlc_ts_ceiling > snapshots.hlc_ts_ceiling
         RETURNING room_id",
    );
    let stmt = stmt.bind(&[
        db::js(&room_id)?,
        db::js(&payload.hlc_ts_ceiling)?,
        db::js(&auth.device.id)?,
        blob_js,
        db::js(&now)?,
    ])?;
    let applied = stmt.first::<serde_json::Value>(None).await?.is_some();
    if !applied {
        return Err(AppError::Conflict("snapshot_not_newer".to_string()));
    }

    let _ =
        db::touch_session_and_device(&db, auth.user.id, auth.session.id, auth.device.id, now).await;

    Response::ok("")
        .map(|r| r.with_status(200))
        .map_err(AppError::from)
}

pub async fn latest_snapshot(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    let now = chrono::Utc::now().timestamp();

    let requested = query_param(&req, "room_id")?;
    let room_id = match requested.as_deref() {
        Some(_) => {
            parse_room_id(requested.as_deref()).map_err(|e| AppError::BadRequest(e.to_string()))?
        }
        None => auth.device.room_id.clone(),
    };
    authorize_room(&db, &room_id, auth.user.id, now).await?;

    #[derive(Deserialize)]
    struct SnapshotRow {
        hlc_ts_ceiling: i64,
        created_at: i64,
        creator_device_id: i32,
        blob: Vec<u8>,
    }

    let row = db.prepare("SELECT hlc_ts_ceiling, created_at, creator_device_id, blob FROM snapshots WHERE room_id = ?1")
        .bind(&[room_id.into()])?
        .first::<SnapshotRow>(None).await?;

    match row {
        Some(snap) => {
            let encoded = base64::engine::general_purpose::STANDARD.encode(&snap.blob);
            let response = DownloadSnapshotResponse {
                hlc_ts_ceiling: snap.hlc_ts_ceiling,
                created_at: snap.created_at,
                creator_device_id: snap.creator_device_id,
                blob: encoded,
            };
            Response::from_json(&response).map_err(AppError::from)
        }
        None => Response::error("not_found", 404).map_err(AppError::from),
    }
}

#[cfg(test)]
mod v2_tests {
    use super::*;

    const DELIVERY_ID: &str = "550e8400-e29b-41d4-a716-446655440000";
    const SHARED_V2_FIXTURE: &str =
        include_str!("../../../tests/fixtures/sync-v2/protocol.json");

    #[test]
    fn shared_v2_fixture_uses_relay_push_and_ack_schema() {
        let fixture: serde_json::Value =
            serde_json::from_str(SHARED_V2_FIXTURE).expect("parse shared fixture");
        let push: PushRequest =
            serde_json::from_value(fixture["push"].clone()).expect("parse V2 push request");
        let ack: RelayAckV2 =
            serde_json::from_value(fixture["ack"].clone()).expect("parse relay ACK");

        assert_eq!(
            push.room_id.as_deref(),
            Some("0123456789abcdef0123456789abcdef")
        );
        assert_eq!(push.messages.len(), 1);
        assert_eq!(push.messages[0].delivery_id, ack.delivery_id);
    }

    #[test]
    fn v2_envelope_validates_and_relay_assignment_preserves_payload() {
        let envelope = SyncEnvelopeV2 {
            protocol_version: 2,
            delivery_id: DELIVERY_ID.to_string(),
            payload: "AAEC/w==".to_string(),
            relay_seq: None,
        };

        let decoded = envelope.validate(4).expect("valid envelope");
        assert_eq!(decoded, vec![0, 1, 2, 255]);

        let stored = envelope.with_relay_seq(7);
        assert_eq!(stored.relay_seq, Some(7));
        assert_eq!(stored.payload, "AAEC/w==");
    }

    #[test]
    fn v2_envelope_rejects_version_uuid_encoding_empty_and_size() {
        let valid = SyncEnvelopeV2 {
            protocol_version: 2,
            delivery_id: DELIVERY_ID.to_string(),
            payload: "AA==".to_string(),
            relay_seq: None,
        };

        let mut wrong_version = valid.clone();
        wrong_version.protocol_version = 1;
        assert_eq!(wrong_version.validate(10), Err("unsupported_protocol_version"));

        let mut invalid_id = valid.clone();
        invalid_id.delivery_id = "not-a-uuid".to_string();
        assert_eq!(invalid_id.validate(10), Err("invalid_delivery_id"));

        let mut invalid_payload = valid.clone();
        invalid_payload.payload = "%%%".to_string();
        assert_eq!(invalid_payload.validate(10), Err("invalid_payload_encoding"));

        let mut empty_payload = valid.clone();
        empty_payload.payload.clear();
        assert_eq!(empty_payload.validate(10), Err("empty_payload"));

        assert_eq!(valid.validate(0), Err("message_too_large"));
    }

    #[test]
    fn explicit_room_id_is_required_and_device_room_is_not_a_substitute() {
        let device_room = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        assert_eq!(parse_room_id(None), Err("missing_room_id"));
        assert_eq!(parse_room_id(Some("short")), Err("invalid_room_id"));
        assert_eq!(
            parse_room_id(Some(device_room)),
            Ok(device_room.to_string())
        );
    }

    #[test]
    fn push_and_pull_contracts_use_delivery_ids_and_ordered_envelopes() {
        let ack = RelayAckV2 {
            delivery_id: DELIVERY_ID.to_string(),
            seq: 9,
        };
        assert_eq!(
            serde_json::to_value(ack).unwrap(),
            serde_json::json!({"delivery_id": DELIVERY_ID, "seq": 9})
        );

        let response = PullResponse {
            messages: vec![SyncEnvelopeV2 {
                protocol_version: 2,
                delivery_id: DELIVERY_ID.to_string(),
                payload: "AA==".to_string(),
                relay_seq: Some(9),
            }],
            has_more: false,
        };
        assert_eq!(response.messages[0].relay_seq, Some(9));
    }

    #[test]
    fn account_bootstrap_requires_a_versioned_exact_seed_and_encrypts_it() {
        let seed = [0x5au8; 32];
        let request = SyncBootstrapPutRequest {
            version: 1,
            room_id: "0123456789abcdef0123456789abcdef".to_string(),
            seed: STANDARD.encode(seed),
        };

        let parsed = parse_sync_bootstrap_request(&request).expect("valid bootstrap request");
        assert_eq!(parsed.seed, seed);
        assert_eq!(parsed.room_id, request.room_id);

        let cipher = SyncBootstrapCipher::from_secret("test-only-bootstrap-secret-material")
            .expect("valid test cipher");
        let encrypted = cipher
            .encrypt(&parsed.seed, 7, &parsed.room_id)
            .expect("encrypt bootstrap seed");
        assert_ne!(encrypted.ciphertext, seed);
        assert_eq!(
            cipher.decrypt(&encrypted, 7, &parsed.room_id).unwrap(),
            seed,
            "the same authenticated account can recover its stored seed"
        );
        assert!(
            cipher.decrypt(&encrypted, 8, &parsed.room_id).is_err(),
            "ciphertext must be bound to its account and canonical room"
        );
        let response = SyncBootstrapResponse {
            version: 1,
            room_id: parsed.room_id.clone(),
            seed: STANDARD.encode(seed),
            created_at: 42,
        };
        assert_eq!(
            serde_json::to_value(response).unwrap(),
            serde_json::json!({
                "version": 1,
                "room_id": "0123456789abcdef0123456789abcdef",
                "seed": STANDARD.encode(seed),
                "created_at": 42
            })
        );

        let malformed = SyncBootstrapPutRequest {
            version: 1,
            room_id: request.room_id,
            seed: STANDARD.encode([0x5au8; 31]),
        };
        assert_eq!(
            parse_sync_bootstrap_request(&malformed),
            Err("invalid_bootstrap_seed")
        );
    }
}
