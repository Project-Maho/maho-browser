use std::sync::Arc;

use axum::extract::{ConnectInfo, Path, State};
use axum::http::StatusCode;
use std::net::SocketAddr;
use axum::response::{IntoResponse, Response};
use axum::routing::{delete, get, post};
use axum::{Json, Router};
use sea_orm::{DatabaseTransaction, EntityTrait, TransactionTrait};

use crate::auth::middleware::BearerAuth;
use crate::auth::models::{
    normalize_device_name, normalize_device_type, normalize_display_name, normalize_email,
    AdminForceVerifyEmailRequest, AdminForceVerifyEmailResponse,
    AdminSetCreditBalanceRequest, AdminSetCreditBalanceResponse,
    AuthResponse, ConsumeBypassRequest, ConsumeBypassResponse, DeleteDeviceResponse, ErrorResponse,
    LoginRequest, LogoutResponse, MeResponse, PublicDevice, PublicSession, PublicUser,
    RefreshRequest, SignupRequest, SubscriptionResponse,
    IntrospectRequest, IntrospectResponse, VerifyRequest, VerifyResponse, RequestVerifyResponse,
};
use crate::auth::passwords::{hash_password, verify_password};
use crate::auth::tokens::{
    generate_access_token_string, generate_refresh_token_string, hash_token_str,
};
use crate::server::AppState;
use crate::storage;
use maho_relay_entity::users;
use sha2::Sha256;
use hmac::{Hmac, Mac};

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
        .route("/subscription", get(subscription))
        .route("/devices/{id}", delete(delete_device))
        .route("/introspect", post(introspect))
        .route("/request_verify", post(request_verify))
        .route("/verify", post(verify))
        .route("/consume_bypass", post(consume_bypass))
        .route("/admin/force_verify_email", post(admin_force_verify_email))
        .route("/admin/set_credit_balance", post(admin_set_credit_balance))
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

fn tier_ceiling_usd(tier: &str) -> f64 {
    match tier {
        "pro" => 2.10,
        "max" => 13.30,
        _ => 0.50,
    }
}

async fn subscription(
    State(state): State<Arc<AppState>>,
    auth: BearerAuth,
) -> Result<Json<SubscriptionResponse>, ApiError> {
    let user = auth.0.user;
    let credits_row = maho_relay_entity::credits::Entity::find_by_id(user.id)
        .one(&state.db)
        .await
        .map_err(|_| ApiError::internal("failed to load credits"))?;
    let (credit_balance_usd, lifetime_purchased_usd) = credits_row
        .map(|row| (row.balance_usd, row.lifetime_purchased_usd))
        .unwrap_or((0.0, 0.0));

    Ok(Json(SubscriptionResponse {
        tier_ceiling_usd: tier_ceiling_usd(&user.tier),
        tier: user.tier,
        subscription_status: user.subscription_status,
        subscription_expires_at: user.subscription_expires_at,
        period_start_at: user.period_start_at,
        anniversary_reset_day: user.anniversary_reset_day,
        credit_balance_usd,
        lifetime_purchased_usd,
        has_payment_method: user.has_payment_method,
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

async fn introspect(
    State(state): State<Arc<AppState>>,
    headers: axum::http::HeaderMap,
    Json(payload): Json<IntrospectRequest>,
) -> Result<Json<IntrospectResponse>, ApiError> {
    // 1. Verify service token
    let auth_header = headers
        .get(axum::http::header::AUTHORIZATION)
        .and_then(|v| v.to_str().ok())
        .ok_or_else(|| ApiError::unauthorized("missing authorization header"))?;

    let service_token = auth_header
        .strip_prefix("Bearer ")
        .or_else(|| auth_header.strip_prefix("bearer "))
        .map(str::trim)
        .ok_or_else(|| ApiError::unauthorized("invalid bearer token format"))?;

    if service_token != state.config.service_token {
        return Err(ApiError::unauthorized("invalid service token"));
    }

    // 2. Lookup user token
    let token_hash = hash_token_str(&payload.token);
    let context = storage::find_active_session_context_by_access_hash(&state.db, token_hash)
        .await
        .map_err(|e| ApiError::internal(format!("failed to load session: {}", e)))?
        .ok_or_else(|| ApiError::unauthorized("invalid or expired user token"))?;

    Ok(Json(IntrospectResponse {
        user_id: context.user.id,
        tier: context.user.tier,
        subscription_status: context.user.subscription_status,
        email_verified: context.user.email_verified,
        expires_at: context.session.expires_at,
        period_start_at: context.user.period_start_at,
    }))
}

async fn admin_force_verify_email(
    State(state): State<Arc<AppState>>,
    headers: axum::http::HeaderMap,
    Json(payload): Json<AdminForceVerifyEmailRequest>,
) -> Result<Json<AdminForceVerifyEmailResponse>, ApiError> {
    let auth_header = headers
        .get(axum::http::header::AUTHORIZATION)
        .and_then(|v| v.to_str().ok())
        .ok_or_else(|| ApiError::unauthorized("missing authorization header"))?;

    let service_token = auth_header
        .strip_prefix("Bearer ")
        .or_else(|| auth_header.strip_prefix("bearer "))
        .map(str::trim)
        .ok_or_else(|| ApiError::unauthorized("invalid bearer token format"))?;

    if service_token != state.config.service_token {
        return Err(ApiError::unauthorized("invalid service token"));
    }

    let reason = payload.reason.trim().to_string();
    let user_id = payload.user_id;

    let updated = storage::admin_force_email_verified(&state.db, user_id)
        .await
        .map_err(|e| ApiError::internal(format!("failed to force-verify email: {}", e)))?;

    if !updated {
        return Err(ApiError::not_found("user not found"));
    }

    tracing::warn!(
        user_id = user_id,
        reason = %reason,
        "ADMIN: force-verified email for user"
    );

    Ok(Json(AdminForceVerifyEmailResponse {
        user_id,
        email_verified: true,
    }))
}

async fn admin_set_credit_balance(
    State(state): State<Arc<AppState>>,
    headers: axum::http::HeaderMap,
    Json(payload): Json<AdminSetCreditBalanceRequest>,
) -> Result<Json<AdminSetCreditBalanceResponse>, ApiError> {
    let auth_header = headers
        .get(axum::http::header::AUTHORIZATION)
        .and_then(|v| v.to_str().ok())
        .ok_or_else(|| ApiError::unauthorized("missing authorization header"))?;

    let service_token = auth_header
        .strip_prefix("Bearer ")
        .or_else(|| auth_header.strip_prefix("bearer "))
        .map(str::trim)
        .ok_or_else(|| ApiError::unauthorized("invalid bearer token format"))?;

    use subtle::ConstantTimeEq;
    if service_token.as_bytes().ct_eq(state.config.service_token.as_bytes()).unwrap_u8() != 1 {
        return Err(ApiError::unauthorized("invalid service token"));
    }

    let reason = payload.reason.trim().to_string();
    let user_id = payload.user_id;
    let new_balance = payload.new_balance_usd;
    let now = chrono::Utc::now().timestamp();

    storage::admin_set_credit_balance(&state.db, user_id, new_balance, now)
        .await
        .map_err(|e| ApiError::internal(format!("failed to set credit balance: {}", e)))?;

    tracing::warn!(
        user_id = user_id,
        new_balance_usd = new_balance,
        reason = %reason,
        "ADMIN: set credit balance for user"
    );

    Ok(Json(AdminSetCreditBalanceResponse {
        user_id,
        new_balance_usd: new_balance,
    }))
}

async fn request_verify(
    State(state): State<Arc<AppState>>,
    auth: BearerAuth,
) -> Result<Json<RequestVerifyResponse>, ApiError> {
    let token = uuid::Uuid::new_v4().simple().to_string();
    let expires_at = chrono::Utc::now().timestamp() + 24 * 3600; // 24 hours
    
    storage::set_email_verification_token(&state.db, auth.0.user.id, &token, expires_at)
        .await
        .map_err(|e| ApiError::internal(format!("failed to save verification token: {}", e)))?;
        
    // C4 fix: dev_token exposure and logging only in debug mode
    let dev_token = if cfg!(debug_assertions) {
        tracing::info!("DEV EMAIL VERIFICATION LINK: https://relay.maho.co/auth/verify?token={}", token);
        Some(token)
    } else {
        None
    };
    
    Ok(Json(RequestVerifyResponse {
        status: "success".to_string(),
        dev_token,
    }))
}

async fn verify(
    State(state): State<Arc<AppState>>,
    Json(payload): Json<VerifyRequest>,
) -> Result<Json<VerifyResponse>, ApiError> {
    let now = chrono::Utc::now().timestamp();
    
    let user = storage::verify_email_token(&state.db, &payload.token, now)
        .await
        .map_err(|e| ApiError::internal(format!("failed to verify token: {}", e)))?
        .ok_or_else(|| ApiError::bad_request("invalid or expired verification token"))?;
        
    // Invalidate auth cache on billing-proxy (H11)
    let session_hashes = storage::find_session_hashes_by_user_id(&state.db, user.id)
        .await
        .map_err(|e| ApiError::internal(format!("failed to load user sessions: {}", e)))?;
        
    if !session_hashes.is_empty() {
        let keys: Vec<String> = session_hashes.into_iter()
            .map(|hash| format!("session:{}", hex_encode(&hash)))
            .collect();
            
        let proxy_url = std::env::var("MAHO_PROXY_URL").unwrap_or_default();
        let service_token = std::env::var("MAHO_RELAY_SERVICE_TOKEN").unwrap_or_default();
        
        if !proxy_url.is_empty() && !service_token.is_empty() {
            #[derive(serde::Serialize)]
            struct InvalidateReq {
                keys: Vec<String>,
            }
            let client = reqwest::Client::new();
            match client.post(format!("{}/billing/invalidate_cache", proxy_url))
                .header("Authorization", format!("Bearer {service_token}"))
                .json(&InvalidateReq { keys })
                .send()
                .await
            {
                Ok(resp) if resp.status().is_success() => {
                    tracing::info!("Successfully invalidated billing proxy cache for user {}", user.id);
                }
                Ok(resp) => {
                    tracing::error!("Failed to invalidate billing proxy cache for user {}: status {}", user.id, resp.status());
                }
                Err(e) => {
                    tracing::error!("Failed to contact billing proxy for cache invalidation for user {}: {}", user.id, e);
                }
            }
        }
    }

    // Generate bypass token
    let bypass_token = generate_bypass_token(user.id, &state.config.service_token);
    
    Ok(Json(VerifyResponse {
        status: "success".to_string(),
        verify_bypass_token: bypass_token,
    }))
}

pub fn parse_bypass_token(token: &str, service_token: &str) -> Option<(i32, i64)> {
    let parts: Vec<&str> = token.splitn(3, ':').collect();
    if parts.len() != 3 {
        return None;
    }
    let user_id_str = parts[0];
    let expires_at_str = parts[1];
    let sig_hex = parts[2];

    let sig_bytes = {
        if !sig_hex.len().is_multiple_of(2) {
            return None;
        }
        let mut bytes = Vec::with_capacity(sig_hex.len() / 2);
        for i in (0..sig_hex.len()).step_by(2) {
            let b = u8::from_str_radix(&sig_hex[i..i + 2], 16).ok()?;
            bytes.push(b);
        }
        bytes
    };

    let expires_at: i64 = expires_at_str.parse().ok()?;
    let now = chrono::Utc::now().timestamp();
    if now >= expires_at {
        return None;
    }

    let payload = format!("{}:{}", user_id_str, expires_at_str);
    let mut mac = HmacSha256::new_from_slice(service_token.as_bytes()).ok()?;
    mac.update(payload.as_bytes());
    let computed = mac.finalize().into_bytes();

    use subtle::ConstantTimeEq;
    if computed.as_slice().ct_eq(&sig_bytes).unwrap_u8() != 1 {
        return None;
    }

    let user_id: i32 = user_id_str.parse().ok()?;
    Some((user_id, expires_at))
}

async fn consume_bypass(
    State(state): State<Arc<AppState>>,
    headers: axum::http::HeaderMap,
    Json(payload): Json<ConsumeBypassRequest>,
) -> Result<Json<ConsumeBypassResponse>, ApiError> {
    let auth_header = headers
        .get(axum::http::header::AUTHORIZATION)
        .and_then(|v| v.to_str().ok())
        .ok_or_else(|| ApiError::unauthorized("missing authorization header"))?;

    let service_token = auth_header
        .strip_prefix("Bearer ")
        .or_else(|| auth_header.strip_prefix("bearer "))
        .map(str::trim)
        .ok_or_else(|| ApiError::unauthorized("invalid bearer token format"))?;

    if service_token != state.config.service_token {
        return Err(ApiError::unauthorized("invalid service token"));
    }

    let (user_id, _expires_at) = parse_bypass_token(&payload.token, &state.config.service_token)
        .ok_or_else(|| ApiError::unauthorized("invalid or expired bypass token"))?;

    use sha2::Digest;
    let token_hash: Vec<u8> = Sha256::digest(payload.token.as_bytes()).to_vec();
    let now = chrono::Utc::now().timestamp();

    let consumed = storage::try_consume_bypass_token(&state.db, &token_hash, user_id, now)
        .await
        .map_err(|e| ApiError::internal(format!("storage error: {}", e)))?;

    if consumed {
        Ok(Json(ConsumeBypassResponse { user_id }))
    } else {
        Err(ApiError::conflict("bypass token already consumed"))
    }
}

fn hex_encode(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for &b in bytes {
        s.push_str(&format!("{:02x}", b));
    }
    s
}

fn generate_bypass_token(user_id: i32, service_token: &str) -> String {
    let expires_at = chrono::Utc::now().timestamp() + 300; // 5 minutes
    let payload = format!("{}:{}", user_id, expires_at);
    let mut mac = HmacSha256::new_from_slice(service_token.as_bytes()).unwrap();
    mac.update(payload.as_bytes());
    let sig = format!("{:x}", mac.finalize().into_bytes());
    format!("{}:{}:{}", user_id, expires_at, sig)
}

type HmacSha256 = Hmac<Sha256>;
