use serde::{Deserialize, Serialize};
use worker::*;
use crate::errors::{AppError, Result};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct User {
    pub id: i32,
    pub email: String,
    pub password_hash: String,
    pub display_name: String,
    pub created_at: i64,
    pub locked_until: Option<i64>,
    pub failed_login_attempts: i32,
    pub last_failed_login_at: Option<i64>,
    pub tier: String,
    pub subscription_status: String,
    pub email_verified: f64,
    pub subscription_expires_at: Option<i64>,
    pub period_start_at: Option<i64>,
    pub anniversary_reset_day: Option<i32>,
    pub verification_token: Option<String>,
    pub verification_token_expires_at: Option<i64>,
    pub has_payment_method: f64,
    pub lemonsqueezy_customer_id: Option<String>,
    pub subscription_event_at: Option<i64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Device {
    pub id: i32,
    pub user_id: i32,
    pub name: String,
    pub device_type: String,
    pub room_id: String,
    pub created_at: i64,
    pub last_seen: Option<i64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Session {
    pub id: i32,
    pub user_id: i32,
    pub device_id: i32,
    pub token_hash: Vec<u8>,
    pub refresh_hash: Vec<u8>,
    pub expires_at: i64,
    pub refresh_expires_at: i64,
    pub last_used_at: Option<i64>,
    pub created_at: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ConfigUpdate {
    pub version: i32,
    pub payload: String,
    pub signature: String,
    pub rollout_threshold: i32,
    pub channel: String,
}

pub async fn find_latest_config_update(
    db: &D1Database,
    channel: &str,
    bucket: i32,
) -> Result<Option<ConfigUpdate>> {
    let stmt = db.prepare(
        "SELECT version, payload, signature, rollout_threshold, channel \
         FROM config_updates \
         WHERE channel = ?1 AND ?2 < rollout_threshold \
         ORDER BY version DESC \
         LIMIT 1"
    );
    let stmt = stmt.bind(&[js(&channel)?, js(&bucket)?])?;
    let res = stmt.first::<ConfigUpdate>(None).await?;
    Ok(res)
}

pub async fn insert_config_update(
    db: &D1Database,
    update: &ConfigUpdate,
) -> Result<()> {
    let stmt = db.prepare(
        "INSERT OR REPLACE INTO config_updates \
         (version, payload, signature, rollout_threshold, channel) \
         VALUES (?1, ?2, ?3, ?4, ?5)"
    );
    let stmt = stmt.bind(&[
        js(&update.version)?,
        js(&update.payload)?,
        js(&update.signature)?,
        js(&update.rollout_threshold)?,
        js(&update.channel)?,
    ])?;
    stmt.run().await?;
    Ok(())
}

pub(crate) fn js<T: serde::Serialize>(val: &T) -> std::result::Result<wasm_bindgen::JsValue, AppError> {
    let js_val = serde_wasm_bindgen::to_value(val).map_err(|e| AppError::Internal(e.to_string()))?;
    if js_val.is_undefined() {
        Ok(wasm_bindgen::JsValue::null())
    } else {
        Ok(js_val)
    }
}

pub async fn find_user_by_email(db: &D1Database, email: &str) -> Result<Option<User>> {
    let stmt = db.prepare("SELECT * FROM users WHERE email = ?1");
    let stmt = stmt.bind(&[js(&email)?])?;
    let res = stmt.first::<User>(None).await?;
    Ok(res)
}

pub async fn find_user_by_id(db: &D1Database, id: i32) -> Result<Option<User>> {
    let stmt = db.prepare("SELECT * FROM users WHERE id = ?1");
    let stmt = stmt.bind(&[js(&id)?])?;
    let res = stmt.first::<User>(None).await?;
    Ok(res)
}

pub async fn insert_user(
    db: &D1Database,
    email: &str,
    password_hash: &str,
    display_name: &str,
    now: i64,
) -> Result<User> {
    let stmt = db.prepare(
        "INSERT INTO users (email, password_hash, display_name, created_at, failed_login_attempts, email_verified, has_payment_method, tier, subscription_status)
         VALUES (?1, ?2, ?3, ?4, 0, 0, 0, 'free', 'none')
         RETURNING *"
    );
    let stmt = stmt.bind(&[js(&email)?, js(&password_hash)?, js(&display_name)?, js(&now)?])?;
    let user = stmt.first::<User>(None).await?
        .ok_or_else(|| AppError::Internal("Failed to insert user".to_string()))?;
    Ok(user)
}

pub async fn update_user_login_state(
    db: &D1Database,
    id: i32,
    failed_attempts: i32,
    locked_until: Option<i64>,
    last_failed_at: Option<i64>,
) -> Result<()> {
    let stmt = db.prepare(
        "UPDATE users
         SET failed_login_attempts = ?1, locked_until = ?2, last_failed_login_at = ?3
         WHERE id = ?4"
    );
    let stmt = stmt.bind(&[js(&failed_attempts)?, js(&locked_until)?, js(&last_failed_at)?, js(&id)?])?;
    stmt.run().await?;
    Ok(())
}

pub async fn insert_device(
    db: &D1Database,
    user_id: i32,
    name: &str,
    device_type: &str,
    room_id: &str,
    now: i64,
) -> Result<Device> {
    let stmt = db.prepare(
        "INSERT INTO devices (user_id, name, device_type, room_id, created_at)
         VALUES (?1, ?2, ?3, ?4, ?5)
         RETURNING *"
    );
    let stmt = stmt.bind(&[js(&user_id)?, js(&name)?, js(&device_type)?, js(&room_id)?, js(&now)?])?;
    let device = stmt.first::<Device>(None).await?
        .ok_or_else(|| AppError::Internal("Failed to insert device".to_string()))?;
    Ok(device)
}

pub async fn find_device_by_id(db: &D1Database, id: i32) -> Result<Option<Device>> {
    let stmt = db.prepare("SELECT * FROM devices WHERE id = ?1");
    let stmt = stmt.bind(&[js(&id)?])?;
    let res = stmt.first::<Device>(None).await?;
    Ok(res)
}

pub async fn find_device_by_room_id(db: &D1Database, room_id: &str) -> Result<Option<Device>> {
    let stmt = db.prepare("SELECT * FROM devices WHERE room_id = ?1");
    let stmt = stmt.bind(&[js(&room_id)?])?;
    let res = stmt.first::<Device>(None).await?;
    Ok(res)
}

pub async fn list_devices_for_user(db: &D1Database, user_id: i32) -> Result<Vec<Device>> {
    let stmt = db.prepare("SELECT * FROM devices WHERE user_id = ?1 ORDER BY id ASC");
    let stmt = stmt.bind(&[js(&user_id)?])?;
    let res = stmt.all().await?;
    Ok(res.results::<Device>().unwrap_or_default())
}

pub async fn touch_device(db: &D1Database, id: i32, now: i64) -> Result<()> {
    let stmt = db.prepare("UPDATE devices SET last_seen = ?1 WHERE id = ?2");
    let stmt = stmt.bind(&[js(&now)?, js(&id)?])?;
    stmt.run().await?;
    Ok(())
}

pub async fn insert_session(
    db: &D1Database,
    user_id: i32,
    device_id: i32,
    token_hash: &[u8],
    refresh_hash: &[u8],
    expires_at: i64,
    refresh_expires_at: i64,
    now: i64,
) -> Result<Session> {
    let stmt = db.prepare(
        "INSERT INTO sessions (user_id, device_id, token_hash, refresh_hash, expires_at, refresh_expires_at, created_at)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
         RETURNING *"
    );
    let stmt = stmt.bind(&[
        js(&user_id)?,
        js(&device_id)?,
        js(&token_hash)?,
        js(&refresh_hash)?,
        js(&expires_at)?,
        js(&refresh_expires_at)?,
        js(&now)?,
    ])?;
    let session = stmt.first::<Session>(None).await?
        .ok_or_else(|| AppError::Internal("Failed to insert session".to_string()))?;
    Ok(session)
}

pub async fn find_session_by_access_hash(db: &D1Database, token_hash: &[u8]) -> Result<Option<Session>> {
    let stmt = db.prepare("SELECT * FROM sessions WHERE token_hash = ?1");
    let stmt = stmt.bind(&[js(&token_hash)?])?;
    let res = stmt.first::<Session>(None).await?;
    Ok(res)
}

pub async fn find_session_by_refresh_hash(db: &D1Database, refresh_hash: &[u8]) -> Result<Option<Session>> {
    let stmt = db.prepare("SELECT * FROM sessions WHERE refresh_hash = ?1");
    let stmt = stmt.bind(&[js(&refresh_hash)?])?;
    let res = stmt.first::<Session>(None).await?;
    Ok(res)
}

pub async fn delete_session_by_id(db: &D1Database, id: i32) -> Result<()> {
    let stmt = db.prepare("DELETE FROM sessions WHERE id = ?1");
    let stmt = stmt.bind(&[js(&id)?])?;
    stmt.run().await?;
    Ok(())
}

pub async fn delete_sessions_for_device(db: &D1Database, device_id: i32) -> Result<()> {
    let stmt = db.prepare("DELETE FROM sessions WHERE device_id = ?1");
    let stmt = stmt.bind(&[js(&device_id)?])?;
    stmt.run().await?;
    Ok(())
}

pub async fn delete_device_and_sessions(db: &D1Database, device_id: i32) -> Result<()> {
    let stmt1 = db.prepare("DELETE FROM sessions WHERE device_id = ?1").bind(&[js(&device_id)?])?;
    let stmt2 = db.prepare("DELETE FROM devices WHERE id = ?1").bind(&[js(&device_id)?])?;
    db.batch(vec![stmt1, stmt2]).await?;
    Ok(())
}

pub async fn rotate_session_tokens(
    db: &D1Database,
    id: i32,
    token_hash: &[u8],
    refresh_hash: &[u8],
    expires_at: i64,
    refresh_expires_at: i64,
    now: i64,
) -> Result<Session> {
    let stmt = db.prepare(
        "UPDATE sessions
         SET token_hash = ?1, refresh_hash = ?2, expires_at = ?3, refresh_expires_at = ?4, last_used_at = ?5
         WHERE id = ?6
         RETURNING *"
    );
    let stmt = stmt.bind(&[
        js(&token_hash)?,
        js(&refresh_hash)?,
        js(&expires_at)?,
        js(&refresh_expires_at)?,
        js(&now)?,
        js(&id)?,
    ])?;
    let session = stmt.first::<Session>(None).await?
        .ok_or_else(|| AppError::Internal("Failed to rotate session".to_string()))?;
    Ok(session)
}

pub async fn touch_session_and_device(
    db: &D1Database,
    session_id: i32,
    device_id: i32,
    now: i64,
) -> Result<()> {
    let stmt1 = db.prepare("UPDATE sessions SET last_used_at = ?1 WHERE id = ?2").bind(&[js(&now)?, js(&session_id)?])?;
    let stmt2 = db.prepare("UPDATE devices SET last_seen = ?1 WHERE id = ?2").bind(&[js(&now)?, js(&device_id)?])?;
    db.batch(vec![stmt1, stmt2]).await?;
    Ok(())
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct AuthContext {
    pub user: User,
    pub device: Device,
    pub session: Session,
}

pub async fn find_active_session_context_by_access_hash(
    db: &D1Database,
    token_hash: &[u8],
) -> Result<Option<AuthContext>> {
    let now = chrono::Utc::now().timestamp();
    let session = find_session_by_access_hash(db, token_hash).await?;
    let Some(session) = session else {
        return Ok(None);
    };

    if session.expires_at <= now {
        return Ok(None);
    }

    let user = find_user_by_id(db, session.user_id).await?;
    let device = find_device_by_id(db, session.device_id).await?;

    match (user, device) {
        (Some(user), Some(device)) => Ok(Some(AuthContext {
            user,
            device,
            session,
        })),
        _ => Ok(None),
    }
}

pub async fn claim_room_ownership(
    db: &D1Database,
    room_id: &str,
    user_id: i32,
    now: i64,
) -> Result<()> {
    let stmt = db.prepare(
        "INSERT INTO room_ownership (room_id, user_id, claimed_at) VALUES (?1, ?2, ?3)
         ON CONFLICT(room_id) DO NOTHING"
    );
    let stmt = stmt.bind(&[js(&room_id)?, js(&user_id)?, js(&now)?])?;
    stmt.run().await?;
    Ok(())
}

pub async fn get_room_owner_user_id(db: &D1Database, room_id: &str) -> Result<Option<i32>> {
    #[derive(Deserialize)]
    struct OwnerRow {
        user_id: i32,
    }
    let stmt = db.prepare("SELECT user_id FROM room_ownership WHERE room_id = ?1");
    let stmt = stmt.bind(&[js(&room_id)?])?;
    let row = stmt.first::<OwnerRow>(None).await?;
    Ok(row.map(|r| r.user_id))
}

pub async fn get_latest_room_for_user(db: &D1Database, user_id: i32) -> Result<Option<String>> {
    #[derive(Deserialize)]
    struct RoomRow {
        room_id: String,
    }
    let stmt = db.prepare(
        "SELECT room_id FROM room_ownership WHERE user_id = ?1 ORDER BY claimed_at DESC LIMIT 1"
    );
    let stmt = stmt.bind(&[js(&user_id)?])?;
    let row = stmt.first::<RoomRow>(None).await?;
    Ok(row.map(|r| r.room_id))
}

pub async fn has_processed_event(db: &D1Database, event_id: &str) -> Result<bool> {
    let stmt = db.prepare("SELECT 1 FROM processed_events WHERE event_id = ?1 LIMIT 1");
    let stmt = stmt.bind(&[js(&event_id)?])?;
    let res = stmt.first::<serde_json::Value>(None).await?;
    Ok(res.is_some())
}

pub async fn mark_event_processed(db: &D1Database, event_id: &str, now: i64) -> Result<()> {
    let stmt = db.prepare(
        "INSERT INTO processed_events (event_id, processed_at) VALUES (?1, ?2)
         ON CONFLICT(event_id) DO NOTHING"
    );
    let stmt = stmt.bind(&[js(&event_id)?, js(&now)?])?;
    stmt.run().await?;
    Ok(())
}

#[allow(clippy::too_many_arguments)]
pub async fn update_user_subscription(
    db: &D1Database,
    user_id: i32,
    tier: &str,
    status: &str,
    expires_at: Option<i64>,
    period_start_at: Option<i64>,
    anniversary_reset_day: Option<i32>,
    has_payment_method: Option<bool>,
    lemonsqueezy_customer_id: Option<String>,
    subscription_event_at: Option<i64>,
) -> Result<()> {
    let has_payment_val: Option<f64> = has_payment_method.map(|b| if b { 1.0 } else { 0.0 });
    let stmt = db.prepare(
        "UPDATE users SET
            tier = ?1,
            subscription_status = ?2,
            subscription_expires_at = COALESCE(?3, subscription_expires_at),
            period_start_at = COALESCE(?4, period_start_at),
            anniversary_reset_day = COALESCE(?5, anniversary_reset_day),
            has_payment_method = COALESCE(?6, has_payment_method),
            lemonsqueezy_customer_id = COALESCE(?7, lemonsqueezy_customer_id),
            subscription_event_at = COALESCE(?8, subscription_event_at)
         WHERE id = ?9"
    );
    let stmt = stmt.bind(&[
        js(&tier)?,
        js(&status)?,
        js(&expires_at)?,
        js(&period_start_at)?,
        js(&anniversary_reset_day)?,
        js(&has_payment_val)?,
        js(&lemonsqueezy_customer_id)?,
        js(&subscription_event_at)?,
        js(&user_id)?,
    ])?;
    stmt.run().await?;
    Ok(())
}

pub async fn update_user_payment_method_and_customer(
    db: &D1Database,
    user_id: i32,
    has_payment_method: bool,
    lemonsqueezy_customer_id: Option<String>,
) -> Result<()> {
    let has_payment_val: f64 = if has_payment_method { 1.0 } else { 0.0 };
    let stmt = db.prepare(
        "UPDATE users SET
            has_payment_method = ?1,
            lemonsqueezy_customer_id = COALESCE(?2, lemonsqueezy_customer_id)
         WHERE id = ?3"
    );
    let stmt = stmt.bind(&[
        js(&has_payment_val)?,
        js(&lemonsqueezy_customer_id)?,
        js(&user_id)?,
    ])?;
    stmt.run().await?;
    Ok(())
}

pub async fn add_credit_balance(
    db: &D1Database,
    user_id: i32,
    amount_usd: f64,
    now: i64,
) -> Result<f64> {
    let stmt = db.prepare(
        "INSERT INTO credits (user_id, balance_usd, updated_at)
         VALUES (?1, ?2, ?3)
         ON CONFLICT(user_id) DO UPDATE SET
            balance_usd = balance_usd + ?2,
            updated_at = ?3
         RETURNING balance_usd"
    );
    let stmt = stmt.bind(&[js(&user_id)?, js(&amount_usd)?, js(&now)?])?;
    #[derive(Deserialize)]
    struct CreditRow {
        balance_usd: f64,
    }
    let row = stmt.first::<CreditRow>(None).await?
        .ok_or_else(|| AppError::Internal("failed to upsert credit balance".to_string()))?;
    Ok(row.balance_usd)
}

pub async fn chargeback_credit(
    db: &D1Database,
    user_id: i32,
    amount_usd: f64,
    now: i64,
) -> Result<f64> {
    let stmt = db.prepare("SELECT balance_usd FROM credits WHERE user_id = ?1");
    let stmt = stmt.bind(&[js(&user_id)?])?;
    #[derive(Deserialize)]
    struct CreditRow {
        balance_usd: f64,
    }
    let current = stmt.first::<CreditRow>(None).await?
        .map(|r| r.balance_usd)
        .unwrap_or(0.0);

    let deduction = amount_usd.min(current);
    let new_balance = current - deduction;

    let stmt = db.prepare(
        "INSERT INTO credits (user_id, balance_usd, updated_at)
         VALUES (?1, ?2, ?3)
         ON CONFLICT(user_id) DO UPDATE SET
            balance_usd = ?2,
            updated_at = ?3"
    );
    let stmt = stmt.bind(&[js(&user_id)?, js(&new_balance)?, js(&now)?])?;
    stmt.run().await?;
    Ok(new_balance)
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StoredMessage {
    pub seq: i64,
    pub blob: Vec<u8>,
}

pub async fn store_message(
    db: &D1Database,
    room_id: &str,
    blob: Vec<u8>,
) -> Result<i64> {
    let now = chrono::Utc::now().timestamp();
    let blob_len = blob.len() as i64;

    #[derive(Deserialize)]
    struct MetaRow {
        next_seq: i64,
        total_bytes: i64,
    }

    let meta = db.prepare("SELECT next_seq, total_bytes FROM room_meta WHERE room_id = ?1")
        .bind(&[js(&room_id)?])?
        .first::<MetaRow>(None).await?;

    let seq = match meta {
        Some(m) => {
            let assigned = m.next_seq;
            db.prepare(
                "UPDATE room_meta SET next_seq = ?1, last_activity = ?2, total_bytes = ?3 WHERE room_id = ?4"
            )
                .bind(&[
                    js(&(assigned + 1))?,
                    js(&now)?,
                    js(&(m.total_bytes + blob_len))?,
                    js(&room_id)?,
                ])?
                .run().await?;
            assigned
        }
        None => {
            db.prepare(
                "INSERT INTO room_meta (room_id, next_seq, last_activity, total_bytes) VALUES (?1, 1, ?2, ?3)"
            )
                .bind(&[js(&room_id)?, js(&now)?, js(&blob_len)?])?
                .run().await?;
            0
        }
    };

    let blob_js = serde_wasm_bindgen::to_value(&blob)
        .map_err(|e| AppError::Internal(e.to_string()))?;
    db.prepare(
        "INSERT INTO messages (room_id, seq, blob, created_at) VALUES (?1, ?2, ?3, ?4)"
    )
        .bind(&[js(&room_id)?, js(&seq)?, blob_js, js(&now)?])?
        .run().await?;

    Ok(seq)
}

pub async fn get_messages_since(
    db: &D1Database,
    room_id: &str,
    since_seq: i64,
) -> Result<Vec<StoredMessage>> {
    let stmt = db.prepare(
        "SELECT seq, blob FROM messages WHERE room_id = ?1 AND seq > ?2 ORDER BY seq ASC"
    );
    let stmt = stmt.bind(&[js(&room_id)?, js(&since_seq)?])?;
    let result = stmt.all().await?;
    let messages: Vec<StoredMessage> = result.results()?;
    Ok(messages)
}

pub async fn get_room_size(db: &D1Database, room_id: &str) -> Result<i64> {
    #[derive(Deserialize)]
    struct SizeRow { total_bytes: i64 }
    let row = db.prepare("SELECT total_bytes FROM room_meta WHERE room_id = ?1")
        .bind(&[js(&room_id)?])?
        .first::<SizeRow>(None).await?;
    Ok(row.map(|r| r.total_bytes).unwrap_or(0))
}

pub async fn get_room_next_seq(db: &D1Database, room_id: &str) -> Result<i64> {
    #[derive(Deserialize)]
    struct SeqRow { next_seq: i64 }
    let row = db.prepare("SELECT next_seq FROM room_meta WHERE room_id = ?1")
        .bind(&[js(&room_id)?])?
        .first::<SeqRow>(None).await?;
    Ok(row.map(|r| r.next_seq).unwrap_or(1))
}
