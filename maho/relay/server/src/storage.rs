use sea_orm::{
    ActiveModelTrait, ColumnTrait, ConnectionTrait, DatabaseConnection, DbErr, EntityTrait,
    QueryFilter, QueryOrder, QueryResult, Set, TransactionTrait,
};

use maho_relay_entity::{consumed_bypass_tokens, credits, devices, message, room_meta, sessions, users};

#[derive(Clone, Debug)]
pub struct AuthContext {
    pub user: users::Model,
    pub device: devices::Model,
    pub session: sessions::Model,
}

#[derive(Clone, Debug)]
pub struct NewSession {
    pub user_id: i32,
    pub device_id: i32,
    pub token_hash: Vec<u8>,
    pub refresh_hash: Vec<u8>,
    pub expires_at: i64,
    pub refresh_expires_at: i64,
    pub created_at: i64,
}

pub async fn store_message(
    db: &DatabaseConnection,
    room_id: &str,
    blob: Vec<u8>,
) -> Result<i64, DbErr> {
    let txn = db.begin().await?;
    let now = chrono::Utc::now().timestamp();
    let blob_len = blob.len() as i64;

    let meta = room_meta::Entity::find_by_id(room_id.to_string())
        .one(&txn)
        .await?;

    let seq = match meta {
        Some(m) => {
            let seq = m.next_seq;
            let mut am: room_meta::ActiveModel = m.into();
            am.next_seq = Set(seq + 1);
            am.last_activity = Set(now);
            am.total_bytes = Set(am.total_bytes.unwrap() + blob_len);
            am.update(&txn).await?;
            seq
        }
        None => {
            room_meta::ActiveModel {
                room_id: Set(room_id.to_string()),
                next_seq: Set(1),
                last_activity: Set(now),
                total_bytes: Set(blob_len),
            }
            .insert(&txn)
            .await?;
            0
        }
    };

    message::ActiveModel {
        id: sea_orm::ActiveValue::NotSet,
        room_id: Set(room_id.to_string()),
        seq: Set(seq),
        blob: Set(blob),
        created_at: Set(now),
    }
    .insert(&txn)
    .await?;

    txn.commit().await?;
    Ok(seq)
}

pub async fn get_room_size(db: &DatabaseConnection, room_id: &str) -> Result<i64, DbErr> {
    let meta = room_meta::Entity::find_by_id(room_id.to_string())
        .one(db)
        .await?;
    Ok(meta.map(|m| m.total_bytes).unwrap_or(0))
}

pub async fn get_messages_since(
    db: &DatabaseConnection,
    room_id: &str,
    since_seq: i64,
) -> Result<Vec<message::Model>, DbErr> {
    message::Entity::find()
        .filter(message::Column::RoomId.eq(room_id))
        .filter(message::Column::Seq.gt(since_seq))
        .order_by_asc(message::Column::Seq)
        .all(db)
        .await
}

pub async fn prune_room(
    db: &DatabaseConnection,
    room_id: &str,
    max_age_secs: i64,
) -> Result<u64, DbErr> {
    let cutoff = chrono::Utc::now().timestamp() - max_age_secs;

    let res = message::Entity::delete_many()
        .filter(message::Column::RoomId.eq(room_id))
        .filter(message::Column::CreatedAt.lt(cutoff))
        .exec(db)
        .await?;

    let result: Option<i64> = db
        .query_one(sea_orm::Statement::from_sql_and_values(
            sea_orm::DatabaseBackend::Sqlite,
            "SELECT COALESCE(SUM(LENGTH(blob)), 0) FROM messages WHERE room_id = $1",
            [room_id.into()],
        ))
        .await?
        .map(|r: QueryResult| r.try_get_by_index::<i64>(0).unwrap_or(0));

    if let Some(total) = result {
        if let Some(meta) = room_meta::Entity::find_by_id(room_id.to_string())
            .one(db)
            .await?
        {
            let mut am: room_meta::ActiveModel = meta.into();
            am.total_bytes = Set(total);
            am.update(db).await?;
        }
    }

    Ok(res.rows_affected)
}

pub async fn list_room_ids(db: &DatabaseConnection) -> Result<Vec<String>, DbErr> {
    let metas = room_meta::Entity::find().all(db).await?;
    Ok(metas.into_iter().map(|m| m.room_id).collect())
}

pub async fn find_user_by_email<T>(db: &T, email: &str) -> Result<Option<users::Model>, DbErr>
where
    T: ConnectionTrait,
{
    users::Entity::find()
        .filter(users::Column::Email.eq(email))
        .one(db)
        .await
}

pub async fn find_user_by_id<T>(db: &T, user_id: i32) -> Result<Option<users::Model>, DbErr>
where
    T: ConnectionTrait,
{
    users::Entity::find_by_id(user_id).one(db).await
}

pub async fn find_device_by_id<T>(db: &T, device_id: i32) -> Result<Option<devices::Model>, DbErr>
where
    T: ConnectionTrait,
{
    devices::Entity::find_by_id(device_id).one(db).await
}

pub async fn list_devices_for_user<T>(db: &T, user_id: i32) -> Result<Vec<devices::Model>, DbErr>
where
    T: ConnectionTrait,
{
    devices::Entity::find()
        .filter(devices::Column::UserId.eq(user_id))
        .order_by_asc(devices::Column::Id)
        .all(db)
        .await
}

pub async fn insert_user<T>(
    db: &T,
    email: String,
    password_hash: String,
    display_name: String,
    now: i64,
) -> Result<users::Model, DbErr>
where
    T: ConnectionTrait,
{
    users::ActiveModel {
        id: sea_orm::ActiveValue::NotSet,
        email: Set(email),
        password_hash: Set(password_hash),
        display_name: Set(display_name),
        created_at: Set(now),
        locked_until: Set(None),
        failed_login_attempts: Set(0),
        last_failed_login_at: Set(None),
        tier: Set("free".to_string()),
        subscription_status: Set("none".to_string()),
        email_verified: Set(false),
        subscription_expires_at: Set(None),
        period_start_at: Set(None),
        anniversary_reset_day: Set(None),
        verification_token: Set(None),
        verification_token_expires_at: Set(None),
        has_payment_method: Set(false),
        lemonsqueezy_customer_id: Set(None),
        subscription_event_at: Set(None),
    }
    .insert(db)
    .await
}

pub async fn update_user_login_state<T>(db: &T, user: &users::Model) -> Result<users::Model, DbErr>
where
    T: ConnectionTrait,
{
    let am: users::ActiveModel = user.clone().into();
    am.update(db).await
}

pub async fn insert_device<T>(
    db: &T,
    user_id: i32,
    name: String,
    device_type: String,
    room_id: String,
    now: i64,
) -> Result<devices::Model, DbErr>
where
    T: ConnectionTrait,
{
    devices::ActiveModel {
        id: sea_orm::ActiveValue::NotSet,
        user_id: Set(user_id),
        name: Set(name),
        device_type: Set(device_type),
        room_id: Set(room_id),
        created_at: Set(now),
        last_seen: Set(None),
    }
    .insert(db)
    .await
}

pub async fn touch_device<T>(db: &T, device_id: i32, now: i64) -> Result<devices::Model, DbErr>
where
    T: ConnectionTrait,
{
    let device = devices::Entity::find_by_id(device_id)
        .one(db)
        .await?
        .ok_or_else(|| DbErr::RecordNotFound("device not found".into()))?;
    let mut am: devices::ActiveModel = device.into();
    am.last_seen = Set(Some(now));
    am.update(db).await
}

pub async fn insert_session<T>(db: &T, new_session: NewSession) -> Result<sessions::Model, DbErr>
where
    T: ConnectionTrait,
{
    sessions::ActiveModel {
        id: sea_orm::ActiveValue::NotSet,
        user_id: Set(new_session.user_id),
        device_id: Set(new_session.device_id),
        token_hash: Set(new_session.token_hash),
        refresh_hash: Set(new_session.refresh_hash),
        expires_at: Set(new_session.expires_at),
        refresh_expires_at: Set(new_session.refresh_expires_at),
        last_used_at: Set(None),
        created_at: Set(new_session.created_at),
    }
    .insert(db)
    .await
}

pub async fn find_session_by_access_hash<T>(
    db: &T,
    token_hash: Vec<u8>,
) -> Result<Option<sessions::Model>, DbErr>
where
    T: ConnectionTrait,
{
    sessions::Entity::find()
        .filter(sessions::Column::TokenHash.eq(token_hash))
        .one(db)
        .await
}

pub async fn find_session_by_refresh_hash<T>(
    db: &T,
    refresh_hash: Vec<u8>,
) -> Result<Option<sessions::Model>, DbErr>
where
    T: ConnectionTrait,
{
    sessions::Entity::find()
        .filter(sessions::Column::RefreshHash.eq(refresh_hash))
        .one(db)
        .await
}

pub async fn find_active_session_context_by_access_hash<T>(
    db: &T,
    token_hash: [u8; 32],
) -> Result<Option<AuthContext>, DbErr>
where
    T: ConnectionTrait,
{
    let now = chrono::Utc::now().timestamp();
    let session = find_session_by_access_hash(db, token_hash.to_vec()).await?;
    let Some(session) = session else {
        return Ok(None);
    };

    if session.expires_at <= now {
        return Ok(None);
    }

    load_session_context(db, session).await
}

pub async fn load_session_context<T>(
    db: &T,
    session: sessions::Model,
) -> Result<Option<AuthContext>, DbErr>
where
    T: ConnectionTrait,
{
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

pub async fn touch_session_and_device<T>(
    db: &T,
    session_id: i32,
    device_id: i32,
) -> Result<(), DbErr>
where
    T: ConnectionTrait,
{
    let now = chrono::Utc::now().timestamp();

    if let Some(session) = sessions::Entity::find_by_id(session_id).one(db).await? {
        let mut am: sessions::ActiveModel = session.into();
        am.last_used_at = Set(Some(now));
        am.update(db).await?;
    }

    if let Some(device) = devices::Entity::find_by_id(device_id).one(db).await? {
        let mut am: devices::ActiveModel = device.into();
        am.last_seen = Set(Some(now));
        am.update(db).await?;
    }

    Ok(())
}

pub async fn rotate_session_tokens<T>(
    db: &T,
    session_id: i32,
    access_token_hash: Vec<u8>,
    refresh_token_hash: Vec<u8>,
    expires_at: i64,
    refresh_expires_at: i64,
    now: i64,
) -> Result<sessions::Model, DbErr>
where
    T: ConnectionTrait,
{
    let session = sessions::Entity::find_by_id(session_id)
        .one(db)
        .await?
        .ok_or_else(|| DbErr::RecordNotFound("session not found".into()))?;
    let mut am: sessions::ActiveModel = session.into();
    am.token_hash = Set(access_token_hash);
    am.refresh_hash = Set(refresh_token_hash);
    am.expires_at = Set(expires_at);
    am.refresh_expires_at = Set(refresh_expires_at);
    am.last_used_at = Set(Some(now));
    am.update(db).await
}

pub async fn delete_session_by_id<T>(db: &T, session_id: i32) -> Result<u64, DbErr>
where
    T: ConnectionTrait,
{
    Ok(sessions::Entity::delete_by_id(session_id).exec(db).await?.rows_affected)
}

pub async fn delete_expired_sessions<T>(db: &T) -> Result<u64, DbErr>
where
    T: ConnectionTrait,
{
    Ok(sessions::Entity::delete_many()
        .filter(sessions::Column::RefreshExpiresAt.lte(chrono::Utc::now().timestamp()))
        .exec(db)
        .await?
        .rows_affected)
}

pub async fn delete_sessions_for_device<T>(db: &T, device_id: i32) -> Result<u64, DbErr>
where
    T: ConnectionTrait,
{
    Ok(sessions::Entity::delete_many()
        .filter(sessions::Column::DeviceId.eq(device_id))
        .exec(db)
        .await?
        .rows_affected)
}

pub async fn delete_device_and_sessions(
    db: &DatabaseConnection,
    device_id: i32,
) -> Result<(), DbErr> {
    let txn = db.begin().await?;
    delete_sessions_for_device(&txn, device_id).await?;
    devices::Entity::delete_by_id(device_id).exec(&txn).await?;
    txn.commit().await?;
    Ok(())
}

pub async fn has_processed_event<T>(db: &T, event_id: &str) -> Result<bool, DbErr>
where
    T: ConnectionTrait,
{
    let opt = maho_relay_entity::processed_events::Entity::find()
        .filter(maho_relay_entity::processed_events::Column::EventId.eq(event_id))
        .one(db)
        .await?;
    Ok(opt.is_some())
}

pub async fn mark_event_processed<T>(db: &T, event_id: &str, processed_at: i64) -> Result<(), DbErr>
where
    T: ConnectionTrait,
{
    let active_model = maho_relay_entity::processed_events::ActiveModel {
        event_id: Set(event_id.to_string()),
        processed_at: Set(processed_at),
    };
    maho_relay_entity::processed_events::Entity::insert(active_model).exec(db).await?;
    Ok(())
}

pub async fn update_user_subscription<T>(
    db: &T,
    user_id: i32,
    tier: &str,
    status: &str,
    expires_at: Option<i64>,
    period_start_at: Option<i64>,
    anniversary_reset_day: Option<i32>,
    has_payment_method: Option<bool>,
    customer_id: Option<String>,
    event_created_at: Option<i64>,
) -> Result<(), DbErr>
where
    T: ConnectionTrait,
{
    if let Some(user) = users::Entity::find_by_id(user_id).one(db).await? {
        if let (Some(new_event), Some(old_event)) = (event_created_at, user.subscription_event_at) {
            if new_event < old_event {
                tracing::warn!(
                    "Rejecting out-of-order webhook update for user {}: old event = {}, new = {}",
                    user_id, old_event, new_event
                );
                return Ok(());
            }
        }
        let mut active: users::ActiveModel = user.into();
        active.tier = Set(tier.to_string());
        active.subscription_status = Set(status.to_string());
        active.subscription_expires_at = Set(expires_at);
        active.period_start_at = Set(period_start_at);
        active.anniversary_reset_day = Set(anniversary_reset_day);
        if let Some(ev) = event_created_at {
            active.subscription_event_at = Set(Some(ev));
        }
        if let Some(hpm) = has_payment_method {
            active.has_payment_method = Set(hpm);
        }
        if let Some(cid) = customer_id {
            active.lemonsqueezy_customer_id = Set(Some(cid));
        }
        active.update(db).await?;
    }
    Ok(())
}

pub async fn update_user_payment_method_and_customer<T>(
    db: &T,
    user_id: i32,
    has_payment_method: bool,
    customer_id: Option<String>,
) -> Result<(), DbErr>
where
    T: ConnectionTrait,
{
    if let Some(user) = users::Entity::find_by_id(user_id).one(db).await? {
        let mut active: users::ActiveModel = user.into();
        active.has_payment_method = Set(has_payment_method);
        if let Some(cid) = customer_id {
            active.lemonsqueezy_customer_id = Set(Some(cid));
        }
        active.update(db).await?;
    }
    Ok(())
}

pub async fn set_email_verification_token<T>(
    db: &T,
    user_id: i32,
    token: &str,
    expires_at: i64,
) -> Result<(), DbErr>
where
    T: ConnectionTrait,
{
    use sha2::{Digest, Sha256};
    let hashed = format!("{:x}", Sha256::digest(token.as_bytes()));
    if let Some(user) = users::Entity::find_by_id(user_id).one(db).await? {
        let mut active: users::ActiveModel = user.into();
        active.verification_token = Set(Some(hashed));
        active.verification_token_expires_at = Set(Some(expires_at));
        active.update(db).await?;
    }
    Ok(())
}

pub async fn verify_email_token<T>(
    db: &T,
    token: &str,
    now: i64,
) -> Result<Option<users::Model>, DbErr>
where
    T: ConnectionTrait,
{
    use sha2::{Digest, Sha256};
    let hashed = format!("{:x}", Sha256::digest(token.as_bytes()));
    if let Some(user) = users::Entity::find()
        .filter(users::Column::VerificationToken.eq(hashed))
        .filter(users::Column::VerificationTokenExpiresAt.gt(now))
        .one(db)
        .await?
    {
        let mut active: users::ActiveModel = user.clone().into();
        active.email_verified = Set(true);
        active.verification_token = Set(None);
        active.verification_token_expires_at = Set(None);
        let updated = active.update(db).await?;
        Ok(Some(updated))
    } else {
        Ok(None)
    }
}
/// Force-set `email_verified = true` for `user_id`.
///
/// Returns `true` if the user was found and updated, `false` if the user does not exist.
pub async fn admin_force_email_verified<T>(db: &T, user_id: i32) -> Result<bool, DbErr>
where
    T: ConnectionTrait,
{
    if let Some(user) = users::Entity::find_by_id(user_id).one(db).await? {
        let mut active: users::ActiveModel = user.into();
        active.email_verified = Set(true);
        active.update(db).await?;
        Ok(true)
    } else {
        Ok(false)
    }
}

pub async fn find_session_hashes_by_user_id<T>(
    db: &T,
    user_id: i32,
) -> Result<Vec<Vec<u8>>, DbErr>
where
    T: ConnectionTrait,
{
    let list = sessions::Entity::find()
        .filter(sessions::Column::UserId.eq(user_id))
        .all(db)
        .await?;
    Ok(list.into_iter().map(|s| s.token_hash).collect())
}

/// Atomically consume a bypass token identified by its SHA-256 hash.
///
/// Returns `true` on the first successful insert (token not previously consumed).
/// Returns `false` if the token was already consumed (PRIMARY KEY conflict).
pub async fn try_consume_bypass_token<T>(
    db: &T,
    token_hash: &[u8],
    user_id: i32,
    consumed_at: i64,
) -> Result<bool, DbErr>
where
    T: ConnectionTrait,
{
    let active_model = consumed_bypass_tokens::ActiveModel {
        token_hash: Set(token_hash.to_vec()),
        user_id: Set(user_id),
        consumed_at: Set(consumed_at),
    };
    match consumed_bypass_tokens::Entity::insert(active_model).exec(db).await {
        Ok(_) => Ok(true),
        Err(DbErr::RecordNotInserted) => Ok(false),
        Err(DbErr::Exec(ref msg)) if msg.to_string().contains("UNIQUE constraint failed") => {
            Ok(false)
        }
        Err(e) => Err(e),
    }
}

/// Get the current PAYG credit balance for a user. Returns 0.0 if no credits row exists.
pub async fn get_credit_balance<T: ConnectionTrait>(db: &T, user_id: i32) -> Result<f64, DbErr> {
    let row = credits::Entity::find_by_id(user_id).one(db).await?;
    Ok(row.map(|r| r.balance_usd).unwrap_or(0.0))
}

/// Add credit balance for a user. Creates the credits row if missing. Returns new balance.
pub async fn add_credit_balance<T: ConnectionTrait>(
    db: &T,
    user_id: i32,
    amount_usd: f64,
    now: i64,
) -> Result<f64, DbErr> {
    if amount_usd <= 0.0 {
        tracing::warn!(
            user_id = user_id,
            amount_usd = amount_usd,
            "add_credit_balance called with zero or negative amount"
        );
        return get_credit_balance(db, user_id).await;
    }

    let existing = credits::Entity::find_by_id(user_id).one(db).await?;
    match existing {
        Some(row) => {
            let new_balance = row.balance_usd + amount_usd;
            let new_lifetime = row.lifetime_purchased_usd + amount_usd;
            let mut active: credits::ActiveModel = row.into();
            active.balance_usd = Set(new_balance);
            active.lifetime_purchased_usd = Set(new_lifetime);
            active.last_purchase_at = Set(Some(now));
            active.updated_at = Set(now);
            active.update(db).await?;
            Ok(new_balance)
        }
        None => {
            credits::ActiveModel {
                user_id: Set(user_id),
                balance_usd: Set(amount_usd),
                lifetime_purchased_usd: Set(amount_usd),
                last_purchase_at: Set(Some(now)),
                last_consumption_at: Set(None),
                created_at: Set(now),
                updated_at: Set(now),
            }
            .insert(db)
            .await?;
            Ok(amount_usd)
        }
    }
}

/// Consume credits from a user's balance. Returns new balance after deduction.
/// Returns Err if balance is insufficient.
#[allow(dead_code)]
pub async fn consume_credit<T: ConnectionTrait>(
    db: &T,
    user_id: i32,
    amount_usd: f64,
    now: i64,
) -> Result<f64, DbErr> {
    if amount_usd <= 0.0 {
        tracing::warn!(
            user_id = user_id,
            amount_usd = amount_usd,
            "consume_credit called with zero or negative amount"
        );
        return get_credit_balance(db, user_id).await;
    }

    let row = credits::Entity::find_by_id(user_id)
        .one(db)
        .await?
        .ok_or_else(|| DbErr::Custom("insufficient credit balance".to_string()))?;

    if row.balance_usd < amount_usd {
        return Err(DbErr::Custom(format!(
            "insufficient credit balance: have {}, need {}",
            row.balance_usd, amount_usd
        )));
    }

    let new_balance = row.balance_usd - amount_usd;
    let mut active: credits::ActiveModel = row.into();
    active.balance_usd = Set(new_balance);
    active.last_consumption_at = Set(Some(now));
    active.updated_at = Set(now);
    active.update(db).await?;
    Ok(new_balance)
}

/// Process a refund chargeback: deduct refund_amount from user's balance, capped at current balance.
/// Per ADR 0010 §refund-policy. Returns new balance (always >= 0).
#[allow(dead_code)]
pub async fn chargeback_credit<T: ConnectionTrait>(
    db: &T,
    user_id: i32,
    refund_amount: f64,
    now: i64,
) -> Result<f64, DbErr> {
    if refund_amount <= 0.0 {
        tracing::warn!(
            user_id = user_id,
            refund_amount = refund_amount,
            "chargeback_credit called with zero or negative amount"
        );
        return get_credit_balance(db, user_id).await;
    }

    let existing = credits::Entity::find_by_id(user_id).one(db).await?;
    match existing {
        Some(row) => {
            let capped_deduction = refund_amount.min(row.balance_usd);
            let new_balance = row.balance_usd - capped_deduction;
            let mut active: credits::ActiveModel = row.into();
            active.balance_usd = Set(new_balance);
            active.updated_at = Set(now);
            active.update(db).await?;
            Ok(new_balance)
        }
        None => Ok(0.0),
    }
}

/// Admin force-set a user's credit balance. For support tooling.
pub async fn admin_set_credit_balance<T: ConnectionTrait>(
    db: &T,
    user_id: i32,
    new_balance: f64,
    now: i64,
) -> Result<(), DbErr> {
    let existing = credits::Entity::find_by_id(user_id).one(db).await?;
    match existing {
        Some(row) => {
            let mut active: credits::ActiveModel = row.into();
            active.balance_usd = Set(new_balance);
            active.updated_at = Set(now);
            active.update(db).await?;
        }
        None => {
            credits::ActiveModel {
                user_id: Set(user_id),
                balance_usd: Set(new_balance),
                lifetime_purchased_usd: Set(0.0),
                last_purchase_at: Set(None),
                last_consumption_at: Set(None),
                created_at: Set(now),
                updated_at: Set(now),
            }
            .insert(db)
            .await?;
        }
    }
    Ok(())
}

use maho_relay_entity::snapshots;

pub async fn get_snapshot(
    db: &DatabaseConnection,
    room_id: &str,
) -> Result<Option<snapshots::Model>, DbErr> {
    snapshots::Entity::find_by_id(room_id.to_string())
        .one(db)
        .await
}

pub async fn save_snapshot(
    db: &DatabaseConnection,
    room_id: &str,
    hlc_ts_ceiling: i64,
    creator_device_id: i64,
    blob: Vec<u8>,
) -> Result<(), DbErr> {
    let now = chrono::Utc::now().timestamp();
    let existing = snapshots::Entity::find_by_id(room_id.to_string())
        .one(db)
        .await?;

    if let Some(snap) = existing {
        if hlc_ts_ceiling <= snap.hlc_ts_ceiling {
            return Err(DbErr::Custom("Snapshot is not newer than existing".to_string()));
        }
        let mut am: snapshots::ActiveModel = snap.into();
        am.hlc_ts_ceiling = Set(hlc_ts_ceiling);
        am.created_at = Set(now);
        am.creator_device_id = Set(creator_device_id);
        am.blob = Set(blob);
        am.update(db).await?;
    } else {
        snapshots::ActiveModel {
            room_id: Set(room_id.to_string()),
            hlc_ts_ceiling: Set(hlc_ts_ceiling),
            created_at: Set(now),
            creator_device_id: Set(creator_device_id),
            blob: Set(blob),
        }
        .insert(db)
        .await?;
    }

    let cutoff_time = now - 24 * 3600;
    message::Entity::delete_many()
        .filter(message::Column::RoomId.eq(room_id.to_string()))
        .filter(message::Column::CreatedAt.lt(cutoff_time))
        .exec(db)
        .await?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    use sea_orm::Database;
    use sea_orm_migration::MigratorTrait;

    async fn setup_db() -> DatabaseConnection {
        let db = Database::connect("sqlite::memory:").await.unwrap();
        maho_relay_migration::Migrator::up(&db, None).await.unwrap();
        db
    }

    #[tokio::test]
    async fn delete_expired_sessions_removes_only_expired_rows() {
        let db = setup_db().await;
        let now = chrono::Utc::now().timestamp();

        let user = insert_user(
            &db,
            "cleanup@example.com".to_string(),
            "hash".to_string(),
            "Cleanup User".to_string(),
            now,
        )
        .await
        .unwrap();
        let expired_device = insert_device(
            &db,
            user.id,
            "Expired Device".to_string(),
            "desktop".to_string(),
            "room-expired".to_string(),
            now,
        )
        .await
        .unwrap();
        let active_device = insert_device(
            &db,
            user.id,
            "Active Device".to_string(),
            "desktop".to_string(),
            "room-active".to_string(),
            now,
        )
        .await
        .unwrap();

        insert_session(
            &db,
            NewSession {
                user_id: user.id,
                device_id: expired_device.id,
                token_hash: vec![1; 32],
                refresh_hash: vec![2; 32],
                expires_at: now + 60,
                refresh_expires_at: now - 1,
                created_at: now,
            },
        )
        .await
        .unwrap();
        insert_session(
            &db,
            NewSession {
                user_id: user.id,
                device_id: active_device.id,
                token_hash: vec![3; 32],
                refresh_hash: vec![4; 32],
                expires_at: now + 60,
                refresh_expires_at: now + 60,
                created_at: now,
            },
        )
        .await
        .unwrap();

        let deleted = delete_expired_sessions(&db).await.unwrap();
        assert_eq!(deleted, 1);

        let sessions = sessions::Entity::find().all(&db).await.unwrap();
        assert_eq!(sessions.len(), 1);
        assert_eq!(sessions[0].device_id, active_device.id);
    }
}
