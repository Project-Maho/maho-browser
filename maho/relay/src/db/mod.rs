use crate::errors::{AppError, Result};
use serde::{Deserialize, Serialize};
use worker::*;

mod config_updates;
pub use config_updates::{find_latest_config_update, insert_config_update, ConfigUpdate};

pub mod jobs;

mod release_updates;
pub use release_updates::{
    find_latest_macos_release, find_latest_windows_release, upsert_release_update, ReleaseUpdate,
};

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
    #[serde(default)]
    pub client_device_id: Option<String>,
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

pub struct NewSession<'a> {
    pub user_id: i32,
    pub device_id: i32,
    pub token_hash: &'a [u8],
    pub refresh_hash: &'a [u8],
    pub expires_at: i64,
    pub refresh_expires_at: i64,
    pub now: i64,
}

pub(crate) fn js<T: serde::Serialize>(
    val: &T,
) -> std::result::Result<wasm_bindgen::JsValue, AppError> {
    let js_val =
        serde_wasm_bindgen::to_value(val).map_err(|e| AppError::Internal(e.to_string()))?;
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
    let stmt = stmt.bind(&[
        js(&email)?,
        js(&password_hash)?,
        js(&display_name)?,
        js(&now)?,
    ])?;
    let user = stmt
        .first::<User>(None)
        .await?
        .ok_or_else(|| AppError::Internal("Failed to insert user".to_string()))?;
    Ok(user)
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OAuthIdentity {
    pub id: i32,
    pub user_id: i32,
    pub provider: String,
    pub provider_sub: String,
    pub email: String,
    pub created_at: i64,
    pub last_login_at: Option<i64>,
}

pub async fn insert_oauth_user(
    db: &D1Database,
    email: &str,
    display_name: &str,
    email_verified: bool,
    now: i64,
) -> Result<User> {
    let stmt = db.prepare(
        "INSERT INTO users (email, password_hash, display_name, created_at, failed_login_attempts, email_verified, has_payment_method, tier, subscription_status)
         VALUES (?1, '', ?2, ?3, 0, ?4, 0, 'free', 'none')
         RETURNING *",
    );
    let verified_flag: i32 = if email_verified { 1 } else { 0 };
    let stmt = stmt.bind(&[
        js(&email)?,
        js(&display_name)?,
        js(&now)?,
        js(&verified_flag)?,
    ])?;
    stmt.first::<User>(None)
        .await?
        .ok_or_else(|| AppError::Internal("oauth user insert returned no row".to_string()))
}

pub async fn find_oauth_identity(
    db: &D1Database,
    provider: &str,
    provider_sub: &str,
) -> Result<Option<OAuthIdentity>> {
    let stmt =
        db.prepare("SELECT * FROM oauth_identities WHERE provider = ?1 AND provider_sub = ?2");
    let stmt = stmt.bind(&[js(&provider)?, js(&provider_sub)?])?;
    Ok(stmt.first::<OAuthIdentity>(None).await?)
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct OAuthAccountMetadata {
    pub provider: String,
    pub provider_sub: String,
}

const FIND_GOOGLE_ACCOUNT_METADATA_SQL: &str =
    "SELECT provider, provider_sub FROM oauth_identities \
     WHERE user_id = ?1 AND provider = 'google' ORDER BY id";

fn valid_google_account_metadata(metadata: &OAuthAccountMetadata) -> bool {
    !metadata.provider_sub.trim().is_empty()
}

pub async fn find_google_account_metadata(
    db: &D1Database,
    user_id: i32,
) -> Result<Option<OAuthAccountMetadata>> {
    let stmt = db.prepare(FIND_GOOGLE_ACCOUNT_METADATA_SQL);
    let stmt = stmt.bind(&[js(&user_id)?])?;
    let rows = stmt.all().await?;
    let metadata: Vec<OAuthAccountMetadata> = rows.results()?;
    Ok(metadata.into_iter().find(valid_google_account_metadata))
}

/// Links an identity, tolerating a concurrent duplicate link of the same
/// `(provider, provider_sub)`: the partial unique index turns the loser of the
/// race into a no-op insert, and the existing row is returned instead.
pub async fn link_oauth_identity(
    db: &D1Database,
    user_id: i32,
    provider: &str,
    provider_sub: &str,
    email: &str,
    now: i64,
) -> Result<OAuthIdentity> {
    let stmt = db.prepare(
        "INSERT INTO oauth_identities (user_id, provider, provider_sub, email, created_at, last_login_at)
         VALUES (?1, ?2, ?3, ?4, ?5, ?5)
         ON CONFLICT(provider, provider_sub) DO NOTHING
         RETURNING *",
    );
    let stmt = stmt.bind(&[
        js(&user_id)?,
        js(&provider)?,
        js(&provider_sub)?,
        js(&email)?,
        js(&now)?,
    ])?;
    if let Some(identity) = stmt.first::<OAuthIdentity>(None).await? {
        return Ok(identity);
    }
    find_oauth_identity(db, provider, provider_sub)
        .await?
        .ok_or_else(|| AppError::Internal("oauth identity vanished after conflict".to_string()))
}

pub async fn touch_oauth_identity(db: &D1Database, id: i32, now: i64) -> Result<()> {
    let stmt = db.prepare("UPDATE oauth_identities SET last_login_at = ?2 WHERE id = ?1");
    let stmt = stmt.bind(&[js(&id)?, js(&now)?])?;
    stmt.run().await?;
    Ok(())
}

pub async fn mark_email_verified(db: &D1Database, user_id: i32) -> Result<()> {
    let stmt = db.prepare("UPDATE users SET email_verified = 1 WHERE id = ?1");
    let stmt = stmt.bind(&[js(&user_id)?])?;
    stmt.run().await?;
    Ok(())
}

/// Claims a one-time OAuth nonce. `Ok(false)` means it was already used, so the
/// caller must reject the id_token as a replay.
pub async fn claim_oauth_nonce(db: &D1Database, nonce_hash: &[u8], now: i64) -> Result<bool> {
    let stmt = db.prepare(
        "INSERT INTO consumed_oauth_nonces (nonce_hash, consumed_at) VALUES (?1, ?2)
         ON CONFLICT(nonce_hash) DO NOTHING
         RETURNING consumed_at",
    );
    let stmt = stmt.bind(&[js(&nonce_hash)?, js(&now)?])?;
    #[derive(Deserialize)]
    struct ClaimRow {
        #[allow(dead_code)]
        consumed_at: i64,
    }
    Ok(stmt.first::<ClaimRow>(None).await?.is_some())
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
         WHERE id = ?4",
    );
    let stmt = stmt.bind(&[
        js(&failed_attempts)?,
        js(&locked_until)?,
        js(&last_failed_at)?,
        js(&id)?,
    ])?;
    stmt.run().await?;
    Ok(())
}

pub async fn insert_device(
    db: &D1Database,
    user_id: i32,
    name: &str,
    device_type: &str,
    room_id: &str,
    client_device_id: Option<&str>,
    now: i64,
) -> Result<Option<Device>> {
    let stmt = db.prepare(
        "INSERT INTO devices (user_id, name, device_type, room_id, client_device_id, created_at)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6)
         ON CONFLICT(user_id, client_device_id) WHERE client_device_id IS NOT NULL DO NOTHING
         RETURNING *",
    );
    let stmt = stmt.bind(&[
        js(&user_id)?,
        js(&name)?,
        js(&device_type)?,
        js(&room_id)?,
        js(&client_device_id)?,
        js(&now)?,
    ])?;
    Ok(stmt.first::<Device>(None).await?)
}

pub async fn find_device_by_client_id(
    db: &D1Database,
    user_id: i32,
    client_device_id: &str,
) -> Result<Option<Device>> {
    let stmt =
        db.prepare("SELECT * FROM devices WHERE user_id = ?1 AND client_device_id = ?2 LIMIT 1");
    let stmt = stmt.bind(&[js(&user_id)?, js(&client_device_id)?])?;
    let res = stmt.first::<Device>(None).await?;
    Ok(res)
}

/// Whether a stable device to reuse or a fresh one to create.
#[derive(Debug, PartialEq, Eq)]
pub enum DeviceAction {
    Reuse,
    CreateNew,
}

/// Reuse only when the client sent a stable id AND a matching device exists;
/// otherwise create a new device (preserving legacy behavior when no id given).
pub fn pick_device(existing: Option<&Device>, provided_id: Option<&str>) -> DeviceAction {
    match (provided_id, existing) {
        (Some(_), Some(_)) => DeviceAction::Reuse,
        _ => DeviceAction::CreateNew,
    }
}

/// Client-supplied opaque device id constraint: 1..=64 of [A-Za-z0-9_-].
pub fn is_valid_client_device_id(s: &str) -> bool {
    !s.is_empty()
        && s.len() <= 64
        && s.chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-')
}

pub async fn find_device_by_id(db: &D1Database, id: i32) -> Result<Option<Device>> {
    let stmt = db.prepare("SELECT * FROM devices WHERE id = ?1");
    let stmt = stmt.bind(&[js(&id)?])?;
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

pub async fn insert_session(db: &D1Database, session: &NewSession<'_>) -> Result<Session> {
    let stmt = db.prepare(
        "INSERT INTO sessions (user_id, device_id, token_hash, refresh_hash, expires_at, refresh_expires_at, created_at)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
         RETURNING *"
    );
    let stmt = stmt.bind(&[
        js(&session.user_id)?,
        js(&session.device_id)?,
        js(&session.token_hash)?,
        js(&session.refresh_hash)?,
        js(&session.expires_at)?,
        js(&session.refresh_expires_at)?,
        js(&session.now)?,
    ])?;
    let session = stmt
        .first::<Session>(None)
        .await?
        .ok_or_else(|| AppError::Internal("Failed to insert session".to_string()))?;
    Ok(session)
}

pub async fn find_session_by_access_hash(
    db: &D1Database,
    token_hash: &[u8],
) -> Result<Option<Session>> {
    let stmt = db.prepare("SELECT * FROM sessions WHERE token_hash = ?1");
    let stmt = stmt.bind(&[js(&token_hash)?])?;
    let res = stmt.first::<Session>(None).await?;
    Ok(res)
}

pub async fn find_session_by_refresh_hash(
    db: &D1Database,
    refresh_hash: &[u8],
) -> Result<Option<Session>> {
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

pub async fn delete_device_and_sessions(db: &D1Database, device_id: i32) -> Result<()> {
    let stmt1 = db
        .prepare("DELETE FROM sessions WHERE device_id = ?1")
        .bind(&[js(&device_id)?])?;
    let stmt2 = db
        .prepare("DELETE FROM devices WHERE id = ?1")
        .bind(&[js(&device_id)?])?;
    db.batch(vec![stmt1, stmt2]).await?;
    Ok(())
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AccountDeletionAction {
    Delete,
    Anonymize,
    RetainUnlinked,
}

/// Complete schema inventory for account deletion. The three retained tables
/// contain billing idempotency/refund or OAuth replay records but no user id,
/// so they cannot be safely targeted and contain no relay account identifier.
pub fn account_deletion_plan() -> &'static [(&'static str, AccountDeletionAction)] {
    use AccountDeletionAction::{Anonymize, Delete, RetainUnlinked};
    &[
        ("messages", Delete),
        ("snapshots", Delete),
        ("user_sync_bootstrap", Delete),
        ("room_meta", Delete),
        ("room_ownership", Delete),
        ("sessions", Delete),
        ("oauth_identities", Delete),
        ("consumed_bypass_tokens", Delete),
        ("user_activity_days", Delete),
        ("handoff_jobs", Delete),
        ("credits", Delete),
        ("devices", Delete),
        ("waitlist_signups", Delete),
        ("admin_audit", Anonymize),
        ("users", Delete),
        ("processed_events", RetainUnlinked),
        ("order_refunds", RetainUnlinked),
        ("consumed_oauth_nonces", RetainUnlinked),
    ]
}

const DELETE_ACCOUNT_SQL: &str = "
DELETE FROM messages WHERE room_id IN (SELECT room_id FROM room_ownership WHERE user_id = ?1 UNION SELECT room_id FROM devices WHERE user_id = ?1);
DELETE FROM snapshots WHERE room_id IN (SELECT room_id FROM room_ownership WHERE user_id = ?1 UNION SELECT room_id FROM devices WHERE user_id = ?1);
DELETE FROM user_sync_bootstrap WHERE user_id = ?1;
DELETE FROM room_meta WHERE room_id IN (SELECT room_id FROM room_ownership WHERE user_id = ?1 UNION SELECT room_id FROM devices WHERE user_id = ?1);
DELETE FROM room_ownership WHERE user_id = ?1;
DELETE FROM sessions WHERE user_id = ?1;
DELETE FROM oauth_identities WHERE user_id = ?1;
DELETE FROM consumed_bypass_tokens WHERE user_id = ?1;
DELETE FROM user_activity_days WHERE user_id = ?1;
DELETE FROM handoff_jobs WHERE user_id = ?1;
DELETE FROM credits WHERE user_id = ?1;
DELETE FROM devices WHERE user_id = ?1;
DELETE FROM waitlist_signups WHERE lower(email) = lower(?2);
UPDATE admin_audit SET target_user_id = 0 WHERE target_user_id = ?1;
DELETE FROM users WHERE id = ?1;
";

/// Deletes account-linked service data in one D1 transaction. Admin audit rows
/// are retained for operational accountability but detached to sentinel user id
/// 0; their schema prohibits NULL and stores no email or other direct PII.
pub async fn delete_account(db: &D1Database, user_id: i32, email: &str) -> Result<()> {
    let mut statements = Vec::new();
    for sql in DELETE_ACCOUNT_SQL
        .split(';')
        .map(str::trim)
        .filter(|s| !s.is_empty())
    {
        let stmt = db.prepare(sql);
        let stmt = if sql.contains("?2") {
            stmt.bind(&[js(&user_id)?, js(&email)?])?
        } else {
            stmt.bind(&[js(&user_id)?])?
        };
        statements.push(stmt);
    }
    // D1 batch executes sequentially as one transaction and rolls the whole
    // batch back if any statement fails.
    db.batch(statements).await?;
    Ok(())
}

const ROTATE_SESSION_TOKENS_SQL: &str =
    "UPDATE sessions
     SET token_hash = ?1, refresh_hash = ?2, expires_at = ?3, refresh_expires_at = ?4, last_used_at = ?5
     WHERE refresh_hash = ?6 AND refresh_expires_at > ?5
     RETURNING *";

pub async fn rotate_session_tokens(
    db: &D1Database,
    presented_refresh_hash: &[u8],
    token_hash: &[u8],
    refresh_hash: &[u8],
    expires_at: i64,
    refresh_expires_at: i64,
    now: i64,
) -> Result<Option<Session>> {
    let stmt = db.prepare(ROTATE_SESSION_TOKENS_SQL);
    let stmt = stmt.bind(&[
        js(&token_hash)?,
        js(&refresh_hash)?,
        js(&expires_at)?,
        js(&refresh_expires_at)?,
        js(&now)?,
        js(&presented_refresh_hash)?,
    ])?;
    Ok(stmt.first::<Session>(None).await?)
}

pub async fn touch_session_and_device(
    db: &D1Database,
    user_id: i32,
    session_id: i32,
    device_id: i32,
    now: i64,
) -> Result<()> {
    let stmt1 = db
        .prepare("UPDATE sessions SET last_used_at = ?1 WHERE id = ?2")
        .bind(&[js(&now)?, js(&session_id)?])?;
    let stmt2 = db
        .prepare("UPDATE devices SET last_seen = ?1 WHERE id = ?2")
        .bind(&[js(&now)?, js(&device_id)?])?;
    let stmt3 = db
        .prepare(
            "INSERT INTO user_activity_days (user_id, day) VALUES (?1, ?2)
             ON CONFLICT(user_id, day) DO NOTHING",
        )
        .bind(&[js(&user_id)?, js(&day_index(now))?])?;
    db.batch(vec![stmt1, stmt2, stmt3]).await?;
    Ok(())
}

/// UTC day index: the number of whole days since the Unix epoch.
pub fn day_index(unix_ts: i64) -> i64 {
    unix_ts.div_euclid(86_400)
}

/// The day index at which a `created_at` account would count as retained on
/// day N (D1 -> offset 1, D7 -> offset 7).
pub fn retention_day(created_at: i64, offset_days: i64) -> i64 {
    day_index(created_at) + offset_days
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
         ON CONFLICT(room_id) DO NOTHING",
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

#[derive(Debug, Clone, Deserialize)]
pub struct SyncBootstrapRecord {
    pub room_id: String,
    pub version: i32,
    pub ciphertext: Vec<u8>,
    pub nonce: Vec<u8>,
    pub created_at: i64,
}

pub async fn get_sync_bootstrap(
    db: &D1Database,
    user_id: i32,
) -> Result<Option<SyncBootstrapRecord>> {
    let row = db
        .prepare(
            "SELECT room_id, version, ciphertext, nonce, created_at
             FROM user_sync_bootstrap
             WHERE user_id = ?1",
        )
        .bind(&[js(&user_id)?])?
        .first::<SyncBootstrapRecord>(None)
        .await?;
    Ok(row)
}

pub async fn create_sync_bootstrap(
    db: &D1Database,
    user_id: i32,
    room_id: &str,
    version: i32,
    ciphertext: Vec<u8>,
    nonce: Vec<u8>,
    created_at: i64,
) -> Result<Option<SyncBootstrapRecord>> {
    let row = db
        .prepare(
            "INSERT INTO user_sync_bootstrap
                (user_id, room_id, version, ciphertext, nonce, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6)
             ON CONFLICT DO NOTHING
             RETURNING room_id, version, ciphertext, nonce, created_at",
        )
        .bind(&[
            js(&user_id)?,
            js(&room_id)?,
            js(&version)?,
            serde_wasm_bindgen::to_value(&ciphertext)
                .map_err(|error| AppError::Internal(error.to_string()))?,
            serde_wasm_bindgen::to_value(&nonce)
                .map_err(|error| AppError::Internal(error.to_string()))?,
            js(&created_at)?,
        ])?
        .first::<SyncBootstrapRecord>(None)
        .await?;
    Ok(row)
}

/// Atomically claim a webhook event for processing.
///
/// Inserts the marker BEFORE any side effect and reports whether this call won
/// the claim. `ON CONFLICT DO NOTHING RETURNING` yields a row only on a fresh
/// insert, so a duplicate/concurrent delivery gets `Ok(false)` and must be
/// treated as already-processed. Claiming first (not last) is what makes credit
/// application idempotent under LemonSqueezy retries.
pub async fn claim_event(db: &D1Database, event_id: &str, now: i64) -> Result<bool> {
    let stmt = db.prepare(
        "INSERT INTO processed_events (event_id, processed_at) VALUES (?1, ?2)
         ON CONFLICT(event_id) DO NOTHING
         RETURNING event_id",
    );
    let stmt = stmt.bind(&[js(&event_id)?, js(&now)?])?;
    #[derive(Deserialize)]
    struct ClaimRow {
        #[allow(dead_code)]
        event_id: String,
    }
    Ok(stmt.first::<ClaimRow>(None).await?.is_some())
}

/// Returns true when `incoming` is older than the last applied subscription
/// event and must be skipped. `allow_equal` lets an equal-timestamp event still
/// apply: downgrades (cancel/expire) pass `true` so they win ties against a
/// paired `subscription_updated`, while upgrades pass `false` so they cannot
/// overwrite a same-second downgrade. Mirrors the SQL guard in
/// [`update_user_subscription`].
pub fn is_stale_event(stored: Option<i64>, incoming: i64, allow_equal: bool) -> bool {
    match stored {
        Some(s) if allow_equal => incoming < s,
        Some(s) => incoming <= s,
        None => false,
    }
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
    allow_equal_event_at: bool,
) -> Result<bool> {
    let has_payment_val: Option<f64> = has_payment_method.map(|b| if b { 1.0 } else { 0.0 });
    // Downgrades win equal-timestamp ties (<=) so a paired subscription_updated
    // cannot restore paid access at the same second; upgrades use < so they
    // cannot overwrite a same-second downgrade. Only a fixed operator token is
    // interpolated (no injection surface).
    let cmp = if allow_equal_event_at { "<=" } else { "<" };
    let sql = format!(
        "UPDATE users SET
            tier = ?1,
            subscription_status = ?2,
            subscription_expires_at = CASE WHEN ?1 = 'free' THEN NULL
                ELSE COALESCE(?3, subscription_expires_at) END,
            period_start_at = COALESCE(?4, period_start_at),
            anniversary_reset_day = COALESCE(?5, anniversary_reset_day),
            has_payment_method = COALESCE(?6, has_payment_method),
            lemonsqueezy_customer_id = COALESCE(?7, lemonsqueezy_customer_id),
            subscription_event_at = COALESCE(?8, subscription_event_at)
         WHERE id = ?9
           AND (subscription_event_at IS NULL OR subscription_event_at {cmp} ?8)
         RETURNING id"
    );
    let stmt = db.prepare(&sql);
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
    #[derive(Deserialize)]
    struct UpdatedRow {
        #[allow(dead_code)]
        id: i32,
    }
    Ok(stmt.first::<UpdatedRow>(None).await?.is_some())
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
         WHERE id = ?3",
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
    // First purchase inserts a fully-populated row (created_at, updated_at,
    // lifetime_purchased_usd, last_purchase_at all stamped); the schema marks
    // created_at/updated_at NOT NULL with no default, so omitting them fails the
    // insert. A subsequent top-up increments both the spendable balance and the
    // lifetime-purchased total and refreshes the purchase timestamps.
    let stmt = db.prepare(
        "INSERT INTO credits (user_id, balance_usd, lifetime_purchased_usd, last_purchase_at, created_at, updated_at)
         VALUES (?1, ?2, ?2, ?3, ?3, ?3)
         ON CONFLICT(user_id) DO UPDATE SET
            balance_usd = balance_usd + ?2,
            lifetime_purchased_usd = lifetime_purchased_usd + ?2,
            last_purchase_at = ?3,
            updated_at = ?3
         RETURNING balance_usd",
    );
    let stmt = stmt.bind(&[js(&user_id)?, js(&amount_usd)?, js(&now)?])?;
    #[derive(Deserialize)]
    struct CreditRow {
        balance_usd: f64,
    }
    let row = stmt
        .first::<CreditRow>(None)
        .await?
        .ok_or_else(|| AppError::Internal("failed to upsert credit balance".to_string()))?;
    Ok(row.balance_usd)
}

/// Field values produced by applying a PAYG purchase (`amount_usd` at `now`) to
/// a credit row. This is the pure, host-testable mirror of the upsert SQL in
/// [`add_credit_balance`] (D1 is wasm-only, so the arithmetic is validated here
/// instead of against a live database). `existing` is `None` for a first
/// purchase (row inserted) and `Some((balance_usd, lifetime_purchased_usd))`
/// for a subsequent top-up (row updated).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct CreditTopup {
    pub balance_usd: f64,
    pub lifetime_purchased_usd: f64,
    pub last_purchase_at: i64,
    pub updated_at: i64,
}

pub fn topup_effect(existing: Option<(f64, f64)>, amount_usd: f64, now: i64) -> CreditTopup {
    let (balance, lifetime) = existing.unwrap_or((0.0, 0.0));
    CreditTopup {
        balance_usd: balance + amount_usd,
        lifetime_purchased_usd: lifetime + amount_usd,
        last_purchase_at: now,
        updated_at: now,
    }
}

/// Atomically consume `amount_usd` from a user's PAYG credit balance.
///
/// The deduction is a single conditional `UPDATE ... WHERE balance_usd >= ?amount`,
/// so concurrent consume calls cannot double-spend: there is no read-then-write
/// window. On success the row's `last_consumption_at` and `updated_at` are stamped.
/// Returns `Some(new_balance)` when the balance covered the amount and was
/// debited, or `None` when the row is missing or the balance was insufficient
/// (in which case the stored balance is left untouched).
pub async fn try_consume_credit(
    db: &D1Database,
    user_id: i32,
    amount_usd: f64,
    now: i64,
) -> Result<Option<f64>> {
    let stmt = db.prepare(
        "UPDATE credits
         SET balance_usd = balance_usd - ?2,
             last_consumption_at = ?3,
             updated_at = ?3
         WHERE user_id = ?1 AND balance_usd >= ?2
         RETURNING balance_usd",
    );
    let stmt = stmt.bind(&[js(&user_id)?, js(&amount_usd)?, js(&now)?])?;
    #[derive(Deserialize)]
    struct CreditRow {
        balance_usd: f64,
    }
    Ok(stmt.first::<CreditRow>(None).await?.map(|r| r.balance_usd))
}

pub async fn chargeback_credit(
    db: &D1Database,
    user_id: i32,
    amount_usd: f64,
    now: i64,
) -> Result<f64> {
    // Atomic clamped deduction: no read-then-write race, and never below zero.
    // A refund for a user with no credits row is a no-op (returns 0.0).
    let stmt = db.prepare(
        "UPDATE credits SET balance_usd = MAX(0, balance_usd - ?2), updated_at = ?3
         WHERE user_id = ?1
         RETURNING balance_usd",
    );
    let stmt = stmt.bind(&[js(&user_id)?, js(&amount_usd)?, js(&now)?])?;
    #[derive(Deserialize)]
    struct CreditRow {
        balance_usd: f64,
    }
    Ok(stmt
        .first::<CreditRow>(None)
        .await?
        .map(|r| r.balance_usd)
        .unwrap_or(0.0))
}

/// Incremental refund (cents) to charge back given the previously recorded
/// cumulative and the new cumulative. None when not strictly greater (a stale
/// or duplicate refund event).
pub fn refund_delta_cents(prior_cents: i64, new_cents: i64) -> Option<i64> {
    if new_cents > prior_cents {
        Some(new_cents - prior_cents)
    } else {
        None
    }
}

/// Advance the recorded cumulative refund for `order_id` to `new_cents` and
/// return the incremental cents to charge back (None if not newer). The
/// conditional upsert only advances on a strictly larger cumulative, so a
/// concurrent event that already advanced past `new_cents` makes this a no-op.
/// Refund events are low-frequency and each is processed once via the webhook
/// claim key, so the pre-read delta is correct in practice; truly concurrent
/// distinct refunds on one order (very rare) could over-deduct.
pub async fn record_refund_delta(
    db: &D1Database,
    order_id: &str,
    new_cents: i64,
    now: i64,
) -> Result<Option<i64>> {
    #[derive(Deserialize)]
    struct RefundRow {
        refunded_cents: i64,
    }
    let prior = db
        .prepare("SELECT refunded_cents FROM order_refunds WHERE order_id = ?1")
        .bind(&[js(&order_id)?])?
        .first::<RefundRow>(None)
        .await?
        .map(|r| r.refunded_cents)
        .unwrap_or(0);

    let Some(delta) = refund_delta_cents(prior, new_cents) else {
        return Ok(None);
    };

    let applied = db
        .prepare(
            "INSERT INTO order_refunds (order_id, refunded_cents, updated_at) VALUES (?1, ?2, ?3)
             ON CONFLICT(order_id) DO UPDATE SET refunded_cents = ?2, updated_at = ?3
               WHERE excluded.refunded_cents > order_refunds.refunded_cents
             RETURNING order_id",
        )
        .bind(&[js(&order_id)?, js(&new_cents)?, js(&now)?])?
        .first::<serde_json::Value>(None)
        .await?
        .is_some();

    Ok(if applied { Some(delta) } else { None })
}

pub async fn get_credit_balance_usd(db: &D1Database, user_id: i32) -> Result<f64> {
    let stmt = db.prepare("SELECT balance_usd FROM credits WHERE user_id = ?1");
    let stmt = stmt.bind(&[js(&user_id)?])?;
    #[derive(Deserialize)]
    struct CreditRow {
        balance_usd: f64,
    }
    Ok(stmt
        .first::<CreditRow>(None)
        .await?
        .map(|r| r.balance_usd)
        .unwrap_or(0.0))
}

/// Set an existing user's credit balance to an absolute value (used by the
/// admin/chargeback flows). Prefer [`try_consume_credit`] for spend paths, which
/// deducts conditionally and atomically.
pub async fn set_credit_balance(
    db: &D1Database,
    user_id: i32,
    new_balance_usd: f64,
    now: i64,
) -> Result<f64> {
    #[derive(Deserialize)]
    struct BalanceRow {
        balance_usd: f64,
    }
    let prev = db
        .prepare("SELECT balance_usd FROM credits WHERE user_id = ?1")
        .bind(&[js(&user_id)?])?
        .first::<BalanceRow>(None)
        .await?
        .map(|r| r.balance_usd)
        .unwrap_or(0.0);

    let stmt = db.prepare(
        "INSERT INTO credits (user_id, balance_usd, lifetime_purchased_usd, created_at, updated_at)
         VALUES (?1, ?2, 0.0, ?3, ?3)
         ON CONFLICT(user_id) DO UPDATE SET balance_usd = ?2, updated_at = ?3",
    );
    let stmt = stmt.bind(&[js(&user_id)?, js(&new_balance_usd)?, js(&now)?])?;
    stmt.run().await?;
    Ok(prev)
}

pub async fn insert_admin_audit(
    db: &D1Database,
    action: &str,
    target_user_id: i32,
    reason: &str,
    detail: Option<&str>,
    now: i64,
) -> Result<()> {
    let stmt = db.prepare(
        "INSERT INTO admin_audit (action, target_user_id, reason, detail, created_at)
         VALUES (?1, ?2, ?3, ?4, ?5)",
    );
    let stmt = stmt.bind(&[
        js(&action)?,
        js(&target_user_id)?,
        js(&reason)?,
        js(&detail)?,
        js(&now)?,
    ])?;
    stmt.run().await?;
    Ok(())
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StoredMessage {
    pub seq: i64,
    pub blob: Vec<u8>,
    /// Present only for protocol V2 rows; legacy V1 log entries have none.
    #[serde(default)]
    pub delivery_id: Option<String>,
}

/// SQL mirrored by [`store_message_sqlite`] for host-side coverage: allocate the
/// next room sequence atomically in one upsert.
const ALLOCATE_ROOM_SEQ_SQL: &str = "INSERT INTO room_meta (room_id, next_seq, last_activity, total_bytes)
     VALUES (?1, 1, ?2, ?3)
     ON CONFLICT(room_id) DO UPDATE SET
        next_seq = room_meta.next_seq + 1,
        last_activity = ?2,
        total_bytes = room_meta.total_bytes + ?3
     RETURNING next_seq";

/// SQL mirrored by [`store_message_sqlite`]: the partial unique index on
/// `(room_id, delivery_id)` makes a retried delivery a no-op insert, so the
/// duplicate is detected by the absence of a returned row rather than by a read.
const INSERT_V2_MESSAGE_SQL: &str =
    "INSERT INTO messages (room_id, seq, blob, created_at, delivery_id) VALUES (?1, ?2, ?3, ?4, ?5)
     ON CONFLICT(room_id, delivery_id) WHERE delivery_id IS NOT NULL DO NOTHING
     RETURNING seq";

const SELECT_SEQ_BY_DELIVERY_SQL: &str =
    "SELECT seq FROM messages WHERE room_id = ?1 AND delivery_id = ?2";

#[derive(Deserialize)]
struct SeqRow {
    seq: i64,
}

pub async fn store_message(db: &D1Database, room_id: &str, blob: Vec<u8>) -> Result<i64> {
    store_message_inner(db, room_id, None, blob).await
}

/// Idempotent V2 store: the same `(room_id, delivery_id)` always resolves to the
/// single sequence the relay assigned the first time, and never appends a second
/// copy of the encrypted payload.
pub async fn store_message_with_delivery_id(
    db: &D1Database,
    room_id: &str,
    delivery_id: &str,
    blob: Vec<u8>,
) -> Result<i64> {
    store_message_inner(db, room_id, Some(delivery_id), blob).await
}

async fn store_message_inner(
    db: &D1Database,
    room_id: &str,
    delivery_id: Option<&str>,
    blob: Vec<u8>,
) -> Result<i64> {
    // Fast path: a retry of an already stored delivery must not consume another
    // room sequence. Correctness does not depend on this read - the unique index
    // below is the authority - it only avoids burning a sequence per retry.
    if let Some(delivery_id) = delivery_id {
        if let Some(seq) = find_seq_by_delivery_id(db, room_id, delivery_id).await? {
            return Ok(seq);
        }
    }

    let now = chrono::Utc::now().timestamp();
    let blob_len = blob.len() as i64;

    #[derive(Deserialize)]
    struct NextSeqRow {
        next_seq: i64,
    }

    // Atomic seq allocation: one upsert increments next_seq and RETURNs it, so
    // concurrent messages to a room cannot read the same seq and collide on the
    // idx_messages_room_seq unique index.
    let row = db
        .prepare(ALLOCATE_ROOM_SEQ_SQL)
        .bind(&[js(&room_id)?, js(&now)?, js(&blob_len)?])?
        .first::<NextSeqRow>(None)
        .await?
        .ok_or_else(|| AppError::Internal("room_meta upsert returned no row".to_string()))?;

    let seq = assigned_seq(row.next_seq);

    let blob_js =
        serde_wasm_bindgen::to_value(&blob).map_err(|e| AppError::Internal(e.to_string()))?;

    let Some(delivery_id) = delivery_id else {
        db.prepare("INSERT INTO messages (room_id, seq, blob, created_at) VALUES (?1, ?2, ?3, ?4)")
            .bind(&[js(&room_id)?, js(&seq)?, blob_js, js(&now)?])?
            .run()
            .await?;
        return Ok(seq);
    };

    let inserted = db
        .prepare(INSERT_V2_MESSAGE_SQL)
        .bind(&[
            js(&room_id)?,
            js(&seq)?,
            blob_js,
            js(&now)?,
            js(&delivery_id)?,
        ])?
        .first::<SeqRow>(None)
        .await?;

    match inserted {
        Some(row) => Ok(row.seq),
        // A concurrent send of the same delivery id won the unique index; the
        // stored sequence is authoritative and this attempt's seq is skipped.
        None => find_seq_by_delivery_id(db, room_id, delivery_id)
            .await?
            .ok_or_else(|| {
                AppError::Internal("delivery row vanished after conflict".to_string())
            }),
    }
}

async fn find_seq_by_delivery_id(
    db: &D1Database,
    room_id: &str,
    delivery_id: &str,
) -> Result<Option<i64>> {
    let row = db
        .prepare(SELECT_SEQ_BY_DELIVERY_SQL)
        .bind(&[js(&room_id)?, js(&delivery_id)?])?
        .first::<SeqRow>(None)
        .await?;
    Ok(row.map(|r| r.seq))
}

/// Host-testable mirror of [`store_message_with_delivery_id`] against real
/// SQLite (D1 is wasm-only). It runs the exact same statements in the same
/// order, so the idempotency contract is validated rather than asserted.
#[cfg(test)]
fn store_message_sqlite(
    db: &rusqlite::Connection,
    room_id: &str,
    delivery_id: &str,
    blob: &[u8],
    now: i64,
) -> rusqlite::Result<i64> {
    use rusqlite::{params, OptionalExtension};

    let existing: Option<i64> = db
        .query_row(SELECT_SEQ_BY_DELIVERY_SQL, params![room_id, delivery_id], |row| {
            row.get(0)
        })
        .optional()?;
    if let Some(seq) = existing {
        return Ok(seq);
    }

    let next_seq: i64 = db.query_row(
        ALLOCATE_ROOM_SEQ_SQL,
        params![room_id, now, blob.len() as i64],
        |row| row.get(0),
    )?;
    let seq = assigned_seq(next_seq);

    let inserted: Option<i64> = db
        .query_row(
            INSERT_V2_MESSAGE_SQL,
            params![room_id, seq, blob, now, delivery_id],
            |row| row.get(0),
        )
        .optional()?;

    match inserted {
        Some(seq) => Ok(seq),
        None => db.query_row(SELECT_SEQ_BY_DELIVERY_SQL, params![room_id, delivery_id], |row| {
            row.get(0)
        }),
    }
}

/// Assigned seq given the post-increment `next_seq` the room_meta upsert
/// returned (first message -> 0). A gap appears if the message INSERT later
/// fails; that is safe because catchup queries `seq > since_seq` tolerate gaps.
/// A failed insert also over-counts `room_meta.total_bytes` (the upsert already
/// added the size); the scheduled retention job recomputes total_bytes from the
/// actual messages, so any drift self-corrects on the next run.
pub fn assigned_seq(returned_next_seq: i64) -> i64 {
    returned_next_seq - 1
}

pub async fn get_messages_since(
    db: &D1Database,
    room_id: &str,
    since_seq: i64,
) -> Result<Vec<StoredMessage>> {
    let stmt = db.prepare(
        "SELECT seq, blob, delivery_id FROM messages WHERE room_id = ?1 AND seq > ?2 ORDER BY seq ASC",
    );
    let stmt = stmt.bind(&[js(&room_id)?, js(&since_seq)?])?;
    let result = stmt.all().await?;
    let messages: Vec<StoredMessage> = result.results()?;
    Ok(messages)
}

/// Bounded ordered page of the room log for the mobile REST pull cycle. Reads
/// `limit + 1` rows so the caller can report `has_more` without a second query.
pub async fn get_messages_page(
    db: &D1Database,
    room_id: &str,
    after_seq: i64,
    limit: usize,
) -> Result<(Vec<StoredMessage>, bool)> {
    let probe = limit as i64 + 1;
    let stmt = db.prepare(
        "SELECT seq, blob, delivery_id FROM messages WHERE room_id = ?1 AND seq > ?2 ORDER BY seq ASC LIMIT ?3",
    );
    let stmt = stmt.bind(&[js(&room_id)?, js(&after_seq)?, js(&probe)?])?;
    let result = stmt.all().await?;
    let mut messages: Vec<StoredMessage> = result.results()?;
    let has_more = messages.len() > limit;
    messages.truncate(limit);
    Ok((messages, has_more))
}

pub async fn get_room_size(db: &D1Database, room_id: &str) -> Result<i64> {
    #[derive(Deserialize)]
    struct SizeRow {
        total_bytes: i64,
    }
    let row = db
        .prepare("SELECT total_bytes FROM room_meta WHERE room_id = ?1")
        .bind(&[js(&room_id)?])?
        .first::<SizeRow>(None)
        .await?;
    Ok(row.map(|r| r.total_bytes).unwrap_or(0))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sync_v2_migration_enforces_room_scoped_delivery_id_idempotency() {
        use rusqlite::Connection;

        let db = Connection::open_in_memory().unwrap();
        db.execute_batch(include_str!("../../migrations/0001_create_tables.sql"))
            .unwrap();
        db.execute_batch(include_str!(
            "../../migrations/0022_sync_v2_delivery_ids.sql"
        ))
        .unwrap();
        db.execute_batch(include_str!(
            "../../migrations/0023_create_user_sync_bootstrap.sql"
        ))
        .unwrap();
        db.execute_batch(include_str!(
            "../../migrations/0023_create_user_sync_bootstrap.sql"
        ))
        .unwrap();

        db.execute(
            "INSERT INTO messages (room_id, seq, blob, created_at, delivery_id) VALUES (?1, 0, x'01', 1, ?2)",
            ["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "550e8400-e29b-41d4-a716-446655440000"],
        )
        .unwrap();
        assert!(db
            .execute(
                "INSERT INTO messages (room_id, seq, blob, created_at, delivery_id) VALUES (?1, 1, x'02', 1, ?2)",
                ["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "550e8400-e29b-41d4-a716-446655440000"],
            )
            .is_err());
        db.execute(
            "INSERT INTO messages (room_id, seq, blob, created_at, delivery_id) VALUES (?1, 0, x'02', 1, ?2)",
            ["bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "550e8400-e29b-41d4-a716-446655440000"],
        )
        .unwrap();
    }

    #[test]
    fn duplicate_delivery_returns_the_original_sequence_without_growing_the_log() {
        use rusqlite::{params, Connection};

        let db = Connection::open_in_memory().unwrap();
        db.execute_batch(include_str!("../../migrations/0001_create_tables.sql"))
            .unwrap();
        db.execute_batch(include_str!(
            "../../migrations/0022_sync_v2_delivery_ids.sql"
        ))
        .unwrap();
        let room = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        let delivery = "550e8400-e29b-41d4-a716-446655440000";

        let first = store_message_sqlite(&db, room, delivery, b"opaque", 1).unwrap();
        let duplicate = store_message_sqlite(&db, room, delivery, b"different", 2).unwrap();

        assert_eq!(first, 0);
        assert_eq!(duplicate, first);
        assert_eq!(
            db.query_row("SELECT COUNT(*) FROM messages", [], |row| row.get::<_, i64>(0))
                .unwrap(),
            1
        );
        assert_eq!(
            db.query_row(
                "SELECT blob FROM messages WHERE room_id = ?1 AND delivery_id = ?2",
                params![room, delivery],
                |row| row.get::<_, Vec<u8>>(0),
            )
            .unwrap(),
            b"opaque"
        );
    }

    #[test]
    fn account_bootstrap_migration_keeps_one_encrypted_seed_per_account() {
        use rusqlite::{params, Connection, OptionalExtension};

        let db = Connection::open_in_memory().unwrap();
        db.execute_batch(include_str!("../../migrations/0001_create_tables.sql"))
            .unwrap();
        db.execute_batch(include_str!("../../migrations/0002_create_auth_tables.sql"))
            .unwrap();
        db.execute_batch(include_str!("../../migrations/0022_sync_v2_delivery_ids.sql"))
            .unwrap();
        db.execute_batch(include_str!(
            "../../migrations/0023_create_user_sync_bootstrap.sql"
        ))
        .unwrap();

        db.execute(
            "INSERT INTO users (id, email, password_hash, display_name, created_at)
             VALUES (1, 'first@example.com', 'hash', 'First', 1),
                    (2, 'second@example.com', 'hash', 'Second', 1)",
            [],
        )
        .unwrap();

        let insert = |user_id: i64, room_id: &str, ciphertext: &[u8], nonce: &[u8]| {
            db.query_row(
                "INSERT INTO user_sync_bootstrap
                    (user_id, room_id, version, ciphertext, nonce, created_at)
                 VALUES (?1, ?2, 1, ?3, ?4, 10)
                 ON CONFLICT DO NOTHING
                 RETURNING room_id",
                params![user_id, room_id, ciphertext, nonce],
                |row| row.get::<_, String>(0),
            )
            .optional()
        };

        let canonical_room = "0123456789abcdef0123456789abcdef";
        assert_eq!(
            insert(1, canonical_room, b"opaque-seed", b"nonce-12byte").unwrap(),
            Some(canonical_room.to_string())
        );
        assert_eq!(
            insert(
                1,
                "fedcba9876543210fedcba9876543210",
                b"different-seed",
                b"nonce-2-000",
            )
            .unwrap(),
            None,
            "a competing first-device bootstrap must not replace the canonical room"
        );
        assert_eq!(
            insert(2, canonical_room, b"another-seed", b"nonce-3-000").unwrap(),
            None,
            "one canonical room must not be claimed by another account"
        );
        assert_eq!(
            db.query_row(
                "SELECT room_id FROM user_sync_bootstrap WHERE user_id = 1",
                [],
                |row| row.get::<_, String>(0),
            )
            .unwrap(),
            canonical_room
        );
        assert_eq!(
            db.query_row(
                "SELECT ciphertext FROM user_sync_bootstrap WHERE user_id = 1",
                [],
                |row| row.get::<_, Vec<u8>>(0),
            )
            .unwrap(),
            b"opaque-seed",
            "the database contract must contain ciphertext, never a plaintext seed column"
        );
    }

    #[test]
    fn refresh_rotation_consumes_the_presented_hash_once() {
        use rusqlite::{params, Connection, OptionalExtension};

        let db = Connection::open_in_memory().unwrap();
        db.execute_batch(include_str!("../../migrations/0002_create_auth_tables.sql"))
            .unwrap();
        db.execute(
            "INSERT INTO users (id, email, password_hash, display_name, created_at) VALUES (1, 'u@example.com', 'hash', 'U', 1)",
            [],
        )
        .unwrap();
        db.execute(
            "INSERT INTO devices (id, user_id, name, device_type, room_id, created_at) VALUES (1, 1, 'd', 'desktop', 'r', 1)",
            [],
        )
        .unwrap();
        db.execute(
            "INSERT INTO sessions (id, user_id, device_id, token_hash, refresh_hash, expires_at, refresh_expires_at, created_at) VALUES (1, 1, 1, x'01', x'02', 100, 1000, 1)",
            [],
        )
        .unwrap();

        let rotate = |new_access: &[u8], new_refresh: &[u8]| {
            db.query_row(
                ROTATE_SESSION_TOKENS_SQL,
                params![
                    new_access,
                    new_refresh,
                    200_i64,
                    2000_i64,
                    100_i64,
                    &[2_u8][..]
                ],
                |row| row.get::<_, i32>(0),
            )
            .optional()
            .unwrap()
        };

        assert_eq!(rotate(&[3], &[4]), Some(1));
        assert_eq!(
            rotate(&[5], &[6]),
            None,
            "reusing the consumed refresh hash must not rotate the session twice"
        );
        let stored_refresh: Vec<u8> = db
            .query_row(
                "SELECT refresh_hash FROM sessions WHERE id = 1",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(stored_refresh, vec![4]);
    }

    #[test]
    fn expired_refresh_hash_is_not_rotated() {
        use rusqlite::{params, Connection, OptionalExtension};

        let db = Connection::open_in_memory().unwrap();
        db.execute_batch(include_str!("../../migrations/0002_create_auth_tables.sql"))
            .unwrap();
        db.execute(
            "INSERT INTO users (id, email, password_hash, display_name, created_at) VALUES (1, 'u@example.com', 'hash', 'U', 1)",
            [],
        )
        .unwrap();
        db.execute(
            "INSERT INTO devices (id, user_id, name, device_type, room_id, created_at) VALUES (1, 1, 'd', 'desktop', 'r', 1)",
            [],
        )
        .unwrap();
        db.execute(
            "INSERT INTO sessions (id, user_id, device_id, token_hash, refresh_hash, expires_at, refresh_expires_at, created_at) VALUES (1, 1, 1, x'01', x'02', 100, 99, 1)",
            [],
        )
        .unwrap();

        let rotated = db
            .query_row(
                ROTATE_SESSION_TOKENS_SQL,
                params![
                    &[3_u8][..],
                    &[4_u8][..],
                    200_i64,
                    2000_i64,
                    100_i64,
                    &[2_u8][..]
                ],
                |row| row.get::<_, i32>(0),
            )
            .optional()
            .unwrap();

        assert_eq!(rotated, None);
        let stored_refresh: Vec<u8> = db
            .query_row(
                "SELECT refresh_hash FROM sessions WHERE id = 1",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(stored_refresh, vec![2]);
    }

    #[test]
    fn topup_first_purchase_seeds_balance_lifetime_and_timestamps() {
        let effect = topup_effect(None, 10.0, 1_700_000_000);
        assert_eq!(
            effect,
            CreditTopup {
                balance_usd: 10.0,
                lifetime_purchased_usd: 10.0,
                last_purchase_at: 1_700_000_000,
                updated_at: 1_700_000_000,
            }
        );
    }

    #[test]
    fn topup_repeat_purchase_increments_balance_and_lifetime() {
        let existing = Some((3.0, 10.0));
        let effect = topup_effect(existing, 50.0, 1_700_000_500);
        assert_eq!(
            effect,
            CreditTopup {
                balance_usd: 53.0,
                lifetime_purchased_usd: 60.0,
                last_purchase_at: 1_700_000_500,
                updated_at: 1_700_000_500,
            }
        );
    }

    #[test]
    fn topup_lifetime_never_decreases_when_balance_was_spent_down() {
        let existing = Some((0.0, 100.0));
        let effect = topup_effect(existing, 25.0, 42);
        assert_eq!(effect.balance_usd, 25.0);
        assert_eq!(effect.lifetime_purchased_usd, 125.0);
    }

    #[test]
    fn is_stale_event_boundaries() {
        // Upgrades (allow_equal = false): equal timestamp is stale (loses tie).
        assert!(!is_stale_event(None, 100, false));
        assert!(is_stale_event(Some(100), 100, false));
        assert!(!is_stale_event(Some(100), 101, false));
        assert!(is_stale_event(Some(100), 99, false));
        // Downgrades (allow_equal = true): equal timestamp applies (wins tie).
        assert!(!is_stale_event(Some(100), 100, true));
        assert!(!is_stale_event(Some(100), 101, true));
        assert!(is_stale_event(Some(100), 99, true));
    }

    fn sample_device() -> Device {
        Device {
            id: 1,
            user_id: 7,
            name: "d".to_string(),
            device_type: "desktop".to_string(),
            room_id: "r".to_string(),
            created_at: 0,
            last_seen: None,
            client_device_id: Some("abc".to_string()),
        }
    }

    #[test]
    fn pick_device_reuses_only_with_id_and_existing() {
        let d = sample_device();
        assert_eq!(pick_device(Some(&d), Some("abc")), DeviceAction::Reuse);
        assert_eq!(pick_device(None, Some("abc")), DeviceAction::CreateNew);
        assert_eq!(pick_device(Some(&d), None), DeviceAction::CreateNew);
        assert_eq!(pick_device(None, None), DeviceAction::CreateNew);
    }

    #[test]
    fn client_device_id_validation() {
        assert!(is_valid_client_device_id("Abc-123_XYZ"));
        assert!(is_valid_client_device_id(&"a".repeat(64)));
        assert!(!is_valid_client_device_id(""));
        assert!(!is_valid_client_device_id(&"a".repeat(65)));
        assert!(!is_valid_client_device_id("has space"));
        assert!(!is_valid_client_device_id("emoji😀"));
    }

    #[test]
    fn assigned_seq_derivation() {
        assert_eq!(assigned_seq(1), 0);
        assert_eq!(assigned_seq(2), 1);
        assert_eq!(assigned_seq(100), 99);
    }

    #[test]
    fn google_account_metadata_lookup_handles_present_absent_and_malformed_rows() {
        use rusqlite::{params, Connection};

        let db = Connection::open_in_memory().unwrap();
        db.execute_batch(include_str!("../../migrations/0002_create_auth_tables.sql"))
            .unwrap();
        db.execute_batch(include_str!(
            "../../migrations/0021_add_oauth_identities.sql"
        ))
        .unwrap();
        db.execute(
            "INSERT INTO users (id, email, password_hash, display_name, created_at) \
             VALUES (1, 'google@example.com', '', 'Google', 1), \
                    (2, 'other@example.com', 'hash', 'Other', 1), \
                    (3, 'malformed@example.com', '', 'Malformed', 1), \
                    (4, 'password@example.com', 'hash', 'Password', 1), \
                    (5, 'whitespace@example.com', '', 'Whitespace', 1), \
                    (6, 'tab@example.com', '', 'Tab', 1), \
                    (7, 'newline@example.com', '', 'Newline', 1), \
                    (8, 'malformed-first@example.com', '', 'Malformed First', 1), \
                    (9, 'multiple-valid@example.com', '', 'Multiple Valid', 1)",
            [],
        )
        .unwrap();
        db.execute(
            "INSERT INTO oauth_identities (user_id, provider, provider_sub, email, created_at) \
             VALUES (1, 'google', 'canonical-sub', 'google@example.com', 1), \
                    (2, 'github', 'other-sub', 'other@example.com', 1), \
                    (3, 'google', '', 'malformed@example.com', 1), \
                    (5, 'google', '   ', 'whitespace@example.com', 1), \
                    (6, 'google', char(9), 'tab@example.com', 1), \
                    (7, 'google', char(10), 'newline@example.com', 1), \
                    (8, 'google', char(13), 'malformed-first@example.com', 1), \
                    (8, 'google', 'valid-later-sub', 'malformed-first@example.com', 2), \
                    (9, 'google', 'lowest-id-sub', 'multiple-valid@example.com', 1), \
                    (9, 'google', 'higher-id-sub', 'multiple-valid@example.com', 2)",
            [],
        )
        .unwrap();

        let lookup = |user_id| {
            let mut stmt = db.prepare(FIND_GOOGLE_ACCOUNT_METADATA_SQL).unwrap();
            let found = stmt
                .query_map(params![user_id], |row| {
                    Ok(OAuthAccountMetadata {
                        provider: row.get(0)?,
                        provider_sub: row.get(1)?,
                    })
                })
                .unwrap()
                .map(std::result::Result::unwrap)
                .find(valid_google_account_metadata);
            found
        };

        assert_eq!(
            lookup(1),
            Some(OAuthAccountMetadata {
                provider: "google".to_string(),
                provider_sub: "canonical-sub".to_string(),
            })
        );
        assert_eq!(lookup(2), None, "non-Google identities are not exposed");
        assert_eq!(
            lookup(3),
            None,
            "empty provider_sub is treated as malformed"
        );
        assert_eq!(lookup(4), None, "absent identities omit OAuth metadata");
        assert_eq!(
            lookup(5),
            None,
            "space-only provider_sub is treated as malformed"
        );
        assert_eq!(
            lookup(6),
            None,
            "tab-only provider_sub is treated as malformed"
        );
        assert_eq!(
            lookup(7),
            None,
            "newline-only provider_sub is treated as malformed"
        );
        assert_eq!(
            lookup(8),
            Some(OAuthAccountMetadata {
                provider: "google".to_string(),
                provider_sub: "valid-later-sub".to_string(),
            }),
            "a malformed older row must not hide a valid later identity"
        );
        assert_eq!(
            lookup(9),
            Some(OAuthAccountMetadata {
                provider: "google".to_string(),
                provider_sub: "lowest-id-sub".to_string(),
            }),
            "multiple valid identities choose the lowest id deterministically"
        );
    }

    #[test]
    fn account_deletion_removes_linked_service_data_and_only_retains_detached_audit_data() {
        use rusqlite::{params, Connection};

        let db = Connection::open_in_memory().unwrap();
        for migration in [
            include_str!("../../migrations/0001_create_tables.sql"),
            include_str!("../../migrations/0002_create_auth_tables.sql"),
            include_str!("../../migrations/0003_add_billing.sql"),
            include_str!("../../migrations/0004_add_email_verification.sql"),
            include_str!("../../migrations/0005_add_consumed_bypass_tokens.sql"),
            include_str!("../../migrations/0006_add_payg_credits.sql"),
            include_str!("../../migrations/0007_add_has_payment_method.sql"),
            include_str!("../../migrations/0008_add_waitlist_signups.sql"),
            include_str!("../../migrations/0009_add_subscription_event_at.sql"),
            include_str!("../../migrations/0010_create_snapshots.sql"),
            include_str!("../../migrations/0011_create_room_ownership.sql"),
            include_str!("../../migrations/0012_create_config_updates.sql"),
            include_str!("../../migrations/0014_create_handoff_jobs.sql"),
            include_str!("../../migrations/0015_create_admin_audit.sql"),
            include_str!("../../migrations/0016_add_client_device_id.sql"),
            include_str!("../../migrations/0017_config_updates_composite_pk.sql"),
            include_str!("../../migrations/0018_create_user_activity_days.sql"),
            include_str!("../../migrations/0019_drop_funnel_events.sql"),
            include_str!("../../migrations/0020_create_order_refunds.sql"),
            include_str!("../../migrations/0021_add_oauth_identities.sql"),
            include_str!("../../migrations/0022_sync_v2_delivery_ids.sql"),
            include_str!("../../migrations/0023_create_user_sync_bootstrap.sql"),
        ] {
            db.execute_batch(migration).unwrap();
        }

        db.execute("INSERT INTO users (id,email,password_hash,display_name,created_at) VALUES (1,?1,'hash','Delete',1),(2,'keep@example.com','hash','Keep',1)", ["delete@example.com"]).unwrap();
        db.execute("INSERT INTO devices (id,user_id,name,device_type,room_id,created_at) VALUES (10,1,'d','desktop','delete-room',1),(20,2,'d','desktop','keep-room',1)", []).unwrap();
        db.execute("INSERT INTO sessions (user_id,device_id,token_hash,refresh_hash,expires_at,refresh_expires_at,created_at) VALUES (1,10,x'01',x'02',9,9,1),(2,20,x'03',x'04',9,9,1)", []).unwrap();
        db.execute(
            "INSERT INTO room_ownership VALUES ('delete-room',1,1),('keep-room',2,1)",
            [],
        )
        .unwrap();
        db.execute("INSERT INTO messages (room_id,seq,blob,created_at) VALUES ('delete-room',0,x'01',1),('keep-room',0,x'02',1)", []).unwrap();
        db.execute(
            "INSERT INTO room_meta VALUES ('delete-room',1,1,1),('keep-room',1,1,1)",
            [],
        )
        .unwrap();
        db.execute(
            "INSERT INTO snapshots VALUES ('delete-room',1,1,10,x'01'),('keep-room',1,1,20,x'02')",
            [],
        )
        .unwrap();
        db.execute("INSERT INTO oauth_identities (user_id,provider,provider_sub,email,created_at) VALUES (1,'google','delete-sub','delete@example.com',1),(2,'google','keep-sub','keep@example.com',1)", []).unwrap();
        db.execute(
            "INSERT INTO consumed_bypass_tokens VALUES (x'01',1,1),(x'02',2,1)",
            [],
        )
        .unwrap();
        db.execute("INSERT INTO user_activity_days VALUES (1,1),(2,1)", [])
            .unwrap();
        db.execute("INSERT INTO handoff_jobs (id,user_id,job_type,created_at,updated_at) VALUES ('delete-job',1,'research',1,1),('keep-job',2,'research',1,1)", []).unwrap();
        db.execute("INSERT INTO credits (user_id,balance_usd,lifetime_purchased_usd,created_at,updated_at) VALUES (1,5,5,1,1),(2,5,5,1,1)", []).unwrap();
        db.execute("INSERT INTO waitlist_signups (email,ip_hash,created_at) VALUES ('delete@example.com','x',1),('keep@example.com','y',1)", []).unwrap();
        db.execute("INSERT INTO admin_audit (action,target_user_id,reason,created_at) VALUES ('set_credit_balance',1,'retained detached',1),('set_credit_balance',2,'keep',1)", []).unwrap();
        db.execute(
            "INSERT INTO processed_events VALUES ('billing-event',1)",
            [],
        )
        .unwrap();
        db.execute("INSERT INTO order_refunds VALUES ('order-1',100,1)", [])
            .unwrap();
        db.execute("INSERT INTO consumed_oauth_nonces VALUES (x'09',1)", [])
            .unwrap();

        let tx = db.unchecked_transaction().unwrap();
        for sql in DELETE_ACCOUNT_SQL
            .split(';')
            .map(str::trim)
            .filter(|s| !s.is_empty())
        {
            if sql.contains("?2") {
                tx.execute(sql, params![1, "delete@example.com"]).unwrap();
            } else {
                tx.execute(sql, [1]).unwrap();
            }
        }
        tx.commit().unwrap();

        for table in [
            "users",
            "devices",
            "sessions",
            "room_ownership",
            "messages",
            "room_meta",
            "snapshots",
            "user_sync_bootstrap",
            "oauth_identities",
            "consumed_bypass_tokens",
            "user_activity_days",
            "handoff_jobs",
            "credits",
        ] {
            let remaining: i64 = db
                .query_row(&format!("SELECT COUNT(*) FROM {table}"), [], |row| {
                    row.get(0)
                })
                .unwrap();
            assert_eq!(
                remaining, 1,
                "{table} should retain only the other user's row"
            );
        }
        assert_eq!(db.query_row("SELECT COUNT(*) FROM waitlist_signups WHERE lower(email)=lower('delete@example.com')", [], |row| row.get::<_, i64>(0)).unwrap(), 0);
        assert_eq!(
            db.query_row(
                "SELECT COUNT(*) FROM admin_audit WHERE target_user_id=0",
                [],
                |row| row.get::<_, i64>(0)
            )
            .unwrap(),
            1
        );
        for table in ["processed_events", "order_refunds", "consumed_oauth_nonces"] {
            let retained: i64 = db
                .query_row(&format!("SELECT COUNT(*) FROM {table}"), [], |row| {
                    row.get(0)
                })
                .unwrap();
            assert_eq!(
                retained, 1,
                "{table} is retained because it has no account identifier"
            );
        }

        use AccountDeletionAction::{Anonymize, Delete, RetainUnlinked};
        assert_eq!(
            account_deletion_plan(),
            &[
                ("messages", Delete),
                ("snapshots", Delete),
                ("user_sync_bootstrap", Delete),
                ("room_meta", Delete),
                ("room_ownership", Delete),
                ("sessions", Delete),
                ("oauth_identities", Delete),
                ("consumed_bypass_tokens", Delete),
                ("user_activity_days", Delete),
                ("handoff_jobs", Delete),
                ("credits", Delete),
                ("devices", Delete),
                ("waitlist_signups", Delete),
                ("admin_audit", Anonymize),
                ("users", Delete),
                ("processed_events", RetainUnlinked),
                ("order_refunds", RetainUnlinked),
                ("consumed_oauth_nonces", RetainUnlinked)
            ]
        );
    }

    #[test]
    fn refund_delta_only_charges_the_increment() {
        // First refund of $2 on a $10 order.
        assert_eq!(refund_delta_cents(0, 200), Some(200));
        // Second partial refund raising cumulative to $5 -> charge the $3 delta.
        assert_eq!(refund_delta_cents(200, 500), Some(300));
        // Duplicate/stale event at same or lower cumulative -> nothing.
        assert_eq!(refund_delta_cents(500, 500), None);
        assert_eq!(refund_delta_cents(500, 300), None);
    }

    #[test]
    fn day_index_and_retention_boundaries() {
        // 1_700_000_000 = 2023-11-14T22:13:20Z -> day 19675, still that day at +86399s.
        assert_eq!(day_index(1_700_000_000), 19_675);
        assert_eq!(day_index(19_675 * 86_400), 19_675);
        assert_eq!(day_index(19_675 * 86_400 - 1), 19_674);
        // D1 / D7 offsets from a signup on day 19675.
        assert_eq!(retention_day(1_700_000_000, 1), 19_676);
        assert_eq!(retention_day(1_700_000_000, 7), 19_682);
    }
}
