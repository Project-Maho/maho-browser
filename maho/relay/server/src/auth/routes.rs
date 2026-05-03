use std::sync::Arc;

use axum::extract::{ConnectInfo, Path, State};
use axum::http::StatusCode;
use std::net::SocketAddr;
use axum::response::{IntoResponse, Response};
use axum::routing::{delete, get, post};
use axum::{Json, Router};
use sea_orm::{DatabaseTransaction, TransactionTrait};

use crate::auth::middleware::BearerAuth;
use crate::auth::models::{
    normalize_device_name, normalize_device_type, normalize_display_name, normalize_email,
    AuthResponse, DeleteDeviceResponse, ErrorResponse, LoginRequest, LogoutResponse, MeResponse,
    PublicDevice, PublicSession, PublicUser, RefreshRequest, SignupRequest,
};
use crate::auth::passwords::{hash_password, verify_password};
use crate::auth::tokens::{
    generate_access_token_string, generate_refresh_token_string, hash_token_str,
};
use crate::server::AppState;
use crate::storage;
use maho_relay_entity::users;

const ACCESS_TOKEN_TTL_SECS: i64 = 60 * 60;
const REFRESH_TOKEN_TTL_SECS: i64 = 60 * 60 * 24 * 30;
const LOGIN_LOCK_THRESHOLD: i32 = 10;
const LOGIN_LOCK_SECS: i64 = 15 * 60;

struct NewAccountSession {
    email: String,
    password_hash: String,
    display_name: String,
    session: NewSessionRequest,
}

struct NewSessionRequest {
    device_name: String,
    device_type: String,
    room_id: String,
    now: i64,
    access_token: String,
    refresh_token: String,
}

pub fn router() -> Router<Arc<AppState>> {
    Router::new()
        .route("/signup", post(signup))
        .route("/login", post(login))
        .route("/logout", post(logout))
        .route("/refresh", post(refresh))
        .route("/me", get(me))
        .route("/devices/{id}", delete(delete_device))
}

async fn signup(
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
    State(state): State<Arc<AppState>>,
    Json(payload): Json<SignupRequest>,
) -> Result<Json<AuthResponse>, ApiError> {
    if !state.signup_rate_limiter.allow(addr.ip()) {
        return Err(ApiError::rate_limited("signup rate limit exceeded"));
    }
    if !state.config.open_registration {
        return Err(ApiError::forbidden("registration is disabled"));
    }

    let email = normalize_email(&payload.email).ok_or_else(|| ApiError::bad_request("email is required"))?;
    if payload.password.trim().is_empty() {
        return Err(ApiError::bad_request("password is required"));
    }
    if payload.password.chars().count() < state.config.min_password_length {
        return Err(ApiError::bad_request(format!(
            "password must be at least {} characters",
            state.config.min_password_length
        )));
    }

    if storage::find_user_by_email(&state.db, &email)
        .await
        .map_err(|_| ApiError::internal("failed to check existing user"))?
        .is_some()
    {
        return Err(ApiError::conflict("email already exists"));
    }

    let password_hash = hash_password(&payload.password)
        .map_err(|_| ApiError::internal("failed to hash password"))?;
    let display_name = normalize_display_name(payload.display_name.as_deref(), &email);
    let device_name = normalize_device_name(payload.device_name.as_deref());
    let device_type = normalize_device_type(payload.device_type.as_deref());
    let now = chrono::Utc::now().timestamp();
    let room_id = uuid::Uuid::new_v4().simple().to_string();
    let access_token = generate_access_token_string();
    let refresh_token = generate_refresh_token_string();

    let txn = state.db.begin().await.map_err(|_| ApiError::internal("failed to start transaction"))?;
    let response = create_account_session(&txn, NewAccountSession {
        email,
        password_hash,
        display_name,
        session: NewSessionRequest {
            device_name,
            device_type,
            room_id,
            now,
            access_token,
            refresh_token,
        },
    })
    .await?;
    txn.commit().await.map_err(|_| ApiError::internal("failed to commit transaction"))?;

    Ok(Json(response))
}

async fn login(
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
    State(state): State<Arc<AppState>>,
    Json(payload): Json<LoginRequest>,
) -> Result<Json<AuthResponse>, ApiError> {
    if !state.login_rate_limiter.allow(addr.ip()) {
        return Err(ApiError::rate_limited("login rate limit exceeded"));
    }

    let email = normalize_email(&payload.email).ok_or_else(|| ApiError::bad_request("email is required"))?;
    if payload.password.trim().is_empty() {
        return Err(ApiError::bad_request("password is required"));
    }
    if payload.password.chars().count() < state.config.min_password_length {
        return Err(ApiError::bad_request(format!(
            "password must be at least {} characters",
            state.config.min_password_length
        )));
    }

    let mut user = storage::find_user_by_email(&state.db, &email)
        .await
        .map_err(|_| ApiError::internal("failed to load user"))?
        .ok_or_else(|| ApiError::unauthorized("invalid credentials"))?;

    let now = chrono::Utc::now().timestamp();
    if user.locked_until.map(|locked_until| locked_until > now).unwrap_or(false) {
        return Err(ApiError::locked("account is temporarily locked"));
    }

    let password_ok = verify_password(&payload.password, &user.password_hash)
        .map_err(|_| ApiError::internal("failed to verify password"))?;

    if !password_ok {
        user.failed_login_attempts += 1;
        user.last_failed_login_at = Some(now);
        user.locked_until = if user.failed_login_attempts >= LOGIN_LOCK_THRESHOLD {
            Some(now + LOGIN_LOCK_SECS)
        } else {
            None
        };
        storage::update_user_login_state(&state.db, &user)
            .await
            .map_err(|_| ApiError::internal("failed to record failed login"))?;

        return Err(if user.locked_until.is_some() {
            ApiError::locked("account temporarily locked")
        } else {
            ApiError::unauthorized("invalid credentials")
        });
    }

    user.failed_login_attempts = 0;
    user.last_failed_login_at = None;
    user.locked_until = None;
    storage::update_user_login_state(&state.db, &user)
        .await
        .map_err(|_| ApiError::internal("failed to reset login state"))?;

    let device_name = normalize_device_name(payload.device_name.as_deref());
    let device_type = normalize_device_type(payload.device_type.as_deref());
    let room_id = uuid::Uuid::new_v4().simple().to_string();
    let access_token = generate_access_token_string();
    let refresh_token = generate_refresh_token_string();
    let txn = state.db.begin().await.map_err(|_| ApiError::internal("failed to start transaction"))?;
    let response = create_session_for_user(&txn,
        user,
        NewSessionRequest {
            device_name,
            device_type,
            room_id,
            now,
            access_token,
            refresh_token,
        },
    )
    .await?;
    txn.commit().await.map_err(|_| ApiError::internal("failed to commit transaction"))?;

    Ok(Json(response))
}

async fn logout(
    State(state): State<Arc<AppState>>,
    auth: BearerAuth,
) -> Result<Json<LogoutResponse>, ApiError> {
    storage::delete_session_by_id(&state.db, auth.0.session.id)
        .await
        .map_err(|_| ApiError::internal("failed to delete session"))?;
    Ok(Json(LogoutResponse { ok: true }))
}

async fn refresh(
    State(state): State<Arc<AppState>>,
    Json(payload): Json<RefreshRequest>,
) -> Result<Json<AuthResponse>, ApiError> {
    if payload.refresh_token.trim().is_empty() {
        return Err(ApiError::bad_request("refresh_token is required"));
    }

    let refresh_hash = hash_token_str(&payload.refresh_token);
    let now = chrono::Utc::now().timestamp();
    let txn = state.db.begin().await.map_err(|_| ApiError::internal("failed to start transaction"))?;
    let response = rotate_session(&txn, refresh_hash, now).await?;
    txn.commit().await.map_err(|_| ApiError::internal("failed to commit transaction"))?;
    Ok(Json(response))
}

async fn me(
    State(state): State<Arc<AppState>>,
    auth: BearerAuth,
) -> Result<Json<MeResponse>, ApiError> {
    let devices = storage::list_devices_for_user(&state.db, auth.0.user.id)
        .await
        .map_err(|_| ApiError::internal("failed to load devices"))?
        .into_iter()
        .map(PublicDevice::from)
        .collect();

    Ok(Json(MeResponse {
        account: PublicUser::from(auth.0.user),
        devices,
        current_device: PublicDevice::from(auth.0.device),
        current_session: PublicSession::from(auth.0.session),
    }))
}

async fn delete_device(
    State(state): State<Arc<AppState>>,
    auth: BearerAuth,
    Path(device_id): Path<i32>,
) -> Result<Json<DeleteDeviceResponse>, ApiError> {
    let device = storage::find_device_by_id(&state.db, device_id)
        .await
        .map_err(|_| ApiError::internal("failed to load device"))?
        .ok_or_else(|| ApiError::not_found("device not found"))?;

    if device.user_id != auth.0.user.id {
        return Err(ApiError::forbidden("device does not belong to current user"));
    }

    storage::delete_device_and_sessions(&state.db, device.id)
        .await
        .map_err(|_| ApiError::internal("failed to delete device"))?;
    Ok(Json(DeleteDeviceResponse { ok: true }))
}

async fn create_account_session(
    txn: &DatabaseTransaction,
    request: NewAccountSession,
) -> Result<AuthResponse, ApiError> {
    let user = storage::insert_user(
        txn,
        request.email,
        request.password_hash,
        request.display_name,
        request.session.now,
    )
        .await
        .map_err(|_| ApiError::internal("failed to create user"))?;
    create_session_response(txn, user, request.session).await
}

async fn create_session_for_user(
    txn: &DatabaseTransaction,
    user: users::Model,
    request: NewSessionRequest,
) -> Result<AuthResponse, ApiError> {
    create_session_response(txn, user, request).await
}

async fn create_session_response(
    txn: &DatabaseTransaction,
    user: users::Model,
    request: NewSessionRequest,
) -> Result<AuthResponse, ApiError> {
    let access_hash = hash_token_str(&request.access_token).to_vec();
    let refresh_hash = hash_token_str(&request.refresh_token).to_vec();
    let device = storage::insert_device(
        txn,
        user.id,
        request.device_name,
        request.device_type,
        request.room_id,
        request.now,
    )
        .await
        .map_err(|_| ApiError::internal("failed to create device"))?;
    let session = storage::insert_session(txn, storage::NewSession {
        user_id: user.id,
        device_id: device.id,
        token_hash: access_hash,
        refresh_hash,
        expires_at: request.now + ACCESS_TOKEN_TTL_SECS,
        refresh_expires_at: request.now + REFRESH_TOKEN_TTL_SECS,
        created_at: request.now,
    })
    .await
    .map_err(|_| ApiError::internal("failed to create session"))?;

    Ok(AuthResponse {
        access_token: request.access_token,
        refresh_token: request.refresh_token,
        token_type: "Bearer",
        expires_at: session.expires_at,
        refresh_expires_at: session.refresh_expires_at,
        account: PublicUser::from(user),
        device: PublicDevice::from(device),
        session: PublicSession::from(session),
    })
}

async fn rotate_session(
    txn: &DatabaseTransaction,
    refresh_hash: [u8; 32],
    now: i64,
) -> Result<AuthResponse, ApiError> {
    let session = storage::find_session_by_refresh_hash(txn, refresh_hash.to_vec())
        .await
        .map_err(|_| ApiError::internal("failed to load session"))?
        .ok_or_else(|| ApiError::unauthorized("invalid refresh token"))?;

    if session.refresh_expires_at <= now {
        return Err(ApiError::unauthorized("refresh token expired"));
    }

    let user = storage::find_user_by_id(txn, session.user_id)
        .await
        .map_err(|_| ApiError::internal("failed to load user"))?
        .ok_or_else(|| ApiError::internal("user missing"))?;
    let device = storage::find_device_by_id(txn, session.device_id)
        .await
        .map_err(|_| ApiError::internal("failed to load device"))?
        .ok_or_else(|| ApiError::internal("device missing"))?;

    let access_token = generate_access_token_string();
    let refresh_token = generate_refresh_token_string();
    let updated = storage::rotate_session_tokens(
        txn,
        session.id,
        hash_token_str(&access_token).to_vec(),
        hash_token_str(&refresh_token).to_vec(),
        now + ACCESS_TOKEN_TTL_SECS,
        now + REFRESH_TOKEN_TTL_SECS,
        now,
    )
    .await
    .map_err(|_| ApiError::internal("failed to rotate session"))?;

    storage::touch_device(txn, device.id, now)
        .await
        .map_err(|_| ApiError::internal("failed to update device"))?;

    Ok(AuthResponse {
        access_token,
        refresh_token,
        token_type: "Bearer",
        expires_at: updated.expires_at,
        refresh_expires_at: updated.refresh_expires_at,
        account: PublicUser::from(user),
        device: PublicDevice::from(device),
        session: PublicSession::from(updated),
    })
}

#[derive(Debug)]
struct ApiError {
    status: StatusCode,
    message: String,
}

impl ApiError {
    fn bad_request(message: impl Into<String>) -> Self {
        Self {
            status: StatusCode::BAD_REQUEST,
            message: message.into(),
        }
    }

    fn unauthorized(message: impl Into<String>) -> Self {
        Self {
            status: StatusCode::UNAUTHORIZED,
            message: message.into(),
        }
    }

    fn forbidden(message: impl Into<String>) -> Self {
        Self {
            status: StatusCode::FORBIDDEN,
            message: message.into(),
        }
    }

    fn conflict(message: impl Into<String>) -> Self {
        Self {
            status: StatusCode::CONFLICT,
            message: message.into(),
        }
    }

    fn locked(message: impl Into<String>) -> Self {
        Self {
            status: StatusCode::LOCKED,
            message: message.into(),
        }
    }

    fn not_found(message: impl Into<String>) -> Self {
        Self {
            status: StatusCode::NOT_FOUND,
            message: message.into(),
        }
    }

    fn rate_limited(message: impl Into<String>) -> Self {
        Self {
            status: StatusCode::TOO_MANY_REQUESTS,
            message: message.into(),
        }
    }

    fn internal(message: impl Into<String>) -> Self {
        Self {
            status: StatusCode::INTERNAL_SERVER_ERROR,
            message: message.into(),
        }
    }
}

impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        (
            self.status,
            Json(ErrorResponse {
                error: self.message,
            }),
        )
            .into_response()
    }
}
