use sea_orm::{
    ActiveModelTrait, ColumnTrait, ConnectionTrait, DatabaseConnection, DbErr, EntityTrait,
    QueryFilter, QueryOrder, QueryResult, Set, TransactionTrait,
};

use maho_relay_entity::{devices, message, room_meta, sessions, users};

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
